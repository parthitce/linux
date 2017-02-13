/*
 * Actions OWL SoCs GPIO driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>

enum owl_gpio_id {
	S900_GPIO,
	S700_GPIO,
};

#define GPIO_PER_BANK				(32)
#define GPIO_BIT(gpio)				(1 << (gpio))

/* CTLR */
#define GPIO_CTLR_PENDING			(0x1 << 0)
#define GPIO_CTLR_ENABLE			(0x1 << 1)
#define GPIO_CTLR_SAMPLE_CLK			(0x1 << 2)
#define		GPIO_CTLR_SAMPLE_CLK_32K		(0x0 << 2)
#define		GPIO_CTLR_SAMPLE_CLK_24M		(0x1 << 2)


/* TYPE */
#define GPIO_INT_TYPE_MASK			(0x3)
#define GPIO_INT_TYPE_HIGH			(0x0)
#define GPIO_INT_TYPE_LOW			(0x1)
#define GPIO_INT_TYPE_RISING			(0x2)
#define GPIO_INT_TYPE_FALLING			(0x3)

/* pending mask for share intc_ctlr */
#define GPIO_CTLR_PENDING_MASK		(0x42108421)

struct gpio_regs {
	unsigned long		outen;
	unsigned long		inen;
	unsigned long		dat;
	unsigned long		intc_pd;
	unsigned long		intc_mask;
	unsigned long		intc_type;
	unsigned long		intc_ctlr;
};

struct owl_gpio_bank_data {
	int			nr_gpios;
	const struct gpio_regs	regs;
};

struct owl_gpio_bank {
	struct gpio_chip	gpio_chip;

	void __iomem		*base;
	int			id;
	int			nr_gpios;
	int			irq;
	struct irq_domain	*irq_domain;
	struct platform_device	*pdev;

	enum owl_gpio_id	devid;
	const struct gpio_regs	*regs;

	spinlock_t		lock;
};

/* lock for shared register between banks,  e.g. share_intc_ctlr in s700 */
static DEFINE_SPINLOCK(gpio_share_lock);

static inline int is_s900_gpio(struct owl_gpio_bank *bank)
{
	return bank->devid == S900_GPIO;
}

static inline int is_s700_gpio(struct owl_gpio_bank *bank)
{
	return bank->devid == S700_GPIO;
}

static inline struct owl_gpio_bank *to_owl_bank(struct gpio_chip *chip)
{
	return container_of(chip, struct owl_gpio_bank, gpio_chip);
}

static unsigned int read_intc_ctlr(struct owl_gpio_bank *bank)
{
	unsigned int val;

	/* s700: all banks share one INTC_CTLR register */
	if (is_s700_gpio(bank)) {
		val = readl_relaxed(bank->base + bank->regs->intc_ctlr);
		val = (val >> (5 * bank->id)) & 0x1f;
	} else
		val = readl_relaxed(bank->base + bank->regs->intc_ctlr);

	return val;
}

static void write_intc_ctlr(struct owl_gpio_bank *bank, unsigned int ctlr)
{
	unsigned int val;

	/* s700: all banks share one INTC_CTLR register */
	if (is_s700_gpio(bank)) {
		unsigned long flags;

		spin_lock_irqsave(&gpio_share_lock, flags);

		val = readl_relaxed(bank->base + bank->regs->intc_ctlr);
		/* don't clear other bank pending mask */
		val &= ~GPIO_CTLR_PENDING_MASK;
		val &= ~(0x1f << (5 * bank->id));
		val |= (ctlr & 0x1f) << (5 * bank->id);
		writel_relaxed(val, bank->base + bank->regs->intc_ctlr);

		spin_unlock_irqrestore(&gpio_share_lock, flags);

	} else
		writel_relaxed(ctlr, bank->base + bank->regs->intc_ctlr);
}


static int owl_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	/*
	 * Map back to global GPIO space and request muxing, the direction
	 * parameter does not matter for this controller.
	 */
	int gpio = chip->base + offset;


	return pinctrl_request_gpio(gpio);
}

static void owl_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);
	const struct gpio_regs *regs = bank->regs;
	int gpio = chip->base + offset;
	unsigned long flags = 0;
	u32 val;

	spin_lock_irqsave(&bank->lock, flags);

	/* disable gpio output */
	val = readl(bank->base + regs->outen);
	val &= ~GPIO_BIT(offset);
	writel(val, bank->base + regs->outen);

	/* disable gpio input */
	val = readl(bank->base + regs->inen);
	val &= ~GPIO_BIT(offset);
	writel(val, bank->base + regs->inen);

	spin_unlock_irqrestore(&bank->lock, flags);

	pinctrl_free_gpio(gpio);
}

static int owl_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);
	const struct gpio_regs *regs = bank->regs;
	u32 val;

	val = readl(bank->base + regs->dat);

	return !!(val & GPIO_BIT(offset));
}

static void owl_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);
	const struct gpio_regs *regs = bank->regs;
	u32 val;

	val = readl(bank->base + regs->dat);

	if (value)
		val |= GPIO_BIT(offset);
	else
		val &= ~GPIO_BIT(offset);

	writel(val, bank->base + regs->dat);
}

static int owl_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);
	const struct gpio_regs *regs = bank->regs;
	unsigned long flags = 0;
	u32 val;

	spin_lock_irqsave(&bank->lock, flags);

	val = readl(bank->base + regs->outen);
	val &= ~GPIO_BIT(offset);
	writel(val, bank->base + regs->outen);

	val = readl(bank->base + regs->inen);
	val |= GPIO_BIT(offset);
	writel(val, bank->base + regs->inen);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int owl_gpio_direction_output(struct gpio_chip *chip,
		unsigned offset, int value)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);
	const struct gpio_regs *regs = bank->regs;
	unsigned long flags = 0;
	u32 val;

	spin_lock_irqsave(&bank->lock, flags);

	val = readl(bank->base + regs->inen);
	val &= ~GPIO_BIT(offset);
	writel(val, bank->base + regs->inen);

	val = readl(bank->base + regs->outen);
	val |= GPIO_BIT(offset);
	writel(val, bank->base + regs->outen);

	owl_gpio_set(chip, offset, value);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int owl_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct owl_gpio_bank *bank = to_owl_bank(chip);

	return irq_create_mapping(bank->irq_domain, offset);
}

static void owl_gpio_irq_mask(struct irq_data *d)
{
	struct owl_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	const struct gpio_regs *regs = bank->regs;
	int gpio = d->hwirq;
	unsigned long flags = 0;
	unsigned int val;

	spin_lock_irqsave(&bank->lock, flags);

	val = readl(bank->base + regs->intc_mask);
	val &= ~GPIO_BIT(gpio);
	writel(val, bank->base + regs->intc_mask);

	if (val == 0) {
		val = read_intc_ctlr(bank);
		val &= ~BIT(GPIO_CTLR_ENABLE);
		write_intc_ctlr(bank, val);
	}

	spin_unlock_irqrestore(&bank->lock, flags);
}

static void owl_gpio_irq_unmask(struct irq_data *d)
{
	struct owl_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	const struct gpio_regs *regs = bank->regs;
	int gpio = d->hwirq;
	unsigned long flags = 0;
	unsigned int val;

	spin_lock_irqsave(&bank->lock, flags);

	val = read_intc_ctlr(bank);
	val |= GPIO_CTLR_ENABLE | GPIO_CTLR_SAMPLE_CLK_24M;
	write_intc_ctlr(bank, val);

	val = readl(bank->base + regs->intc_mask);
	val |= GPIO_BIT(gpio);
	writel(val, bank->base + regs->intc_mask);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static void owl_gpio_irq_ack(struct irq_data *d)
{
	struct owl_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned long flags = 0;
	unsigned int val;
	unsigned int gpio = d->hwirq;

	spin_lock_irqsave(&bank->lock, flags);

	/* clear gpio pending */
	writel(GPIO_BIT(gpio), bank->base + bank->regs->intc_pd);

	/* clear bank pending */
	val = read_intc_ctlr(bank);
	write_intc_ctlr(bank, val);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int owl_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct owl_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	const struct gpio_regs *regs = bank->regs;
	int gpio = d->hwirq;
	unsigned long flags = 0;
	unsigned int type, val, offset;

	spin_lock_irqsave(&bank->lock, flags);

	if (flow_type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(d->irq, handle_edge_irq);
	else
		__irq_set_handler_locked(d->irq, handle_level_irq);

	flow_type &= IRQ_TYPE_SENSE_MASK;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		type = GPIO_INT_TYPE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = GPIO_INT_TYPE_FALLING;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = GPIO_INT_TYPE_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = GPIO_INT_TYPE_LOW;
		break;
	default:
		pr_err("[GPIO] %s: gpio %d, unknow irq type %d\n",
			__func__, gpio, flow_type);
	return -1;
	}

	offset = gpio < 16 ? 4 : 0;
	val = readl(bank->base + regs->intc_type + offset);
	val &= ~(0x3 << ((gpio % 16) * 2));
	val |= type << ((gpio % 16) * 2);
	writel(val, bank->base + regs->intc_type + offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

/*
 * When the summary IRQ is raised, any number of GPIO lines may be high.
 * It is the job of the summary handler to find all those GPIO lines
 * which have been set as summary IRQ lines and which are triggered,
 * and to call their interrupt handlers.
 */
static void owl_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct owl_gpio_bank *bank;
	unsigned int child_irq, i;
	unsigned long flags, pending;

	bank = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	/* get unmasked irq pending */
	spin_lock_irqsave(&bank->lock, flags);
	pending = readl(bank->base + bank->regs->intc_pd) &
		  readl(bank->base + bank->regs->intc_mask);
	spin_unlock_irqrestore(&bank->lock, flags);

	for_each_set_bit(i, &pending, 32) {
		child_irq = irq_find_mapping(bank->irq_domain, i);
		generic_handle_irq(child_irq);
	}

	chained_irq_exit(chip, desc);
}

static struct irq_chip owl_gpio_irq_chip = {
	.name           = "GPIO",
	.irq_mask       = owl_gpio_irq_mask,
	.irq_unmask     = owl_gpio_irq_unmask,
	.irq_ack        = owl_gpio_irq_ack,
	.irq_set_type   = owl_gpio_irq_set_type,
};


static int owl_gpio_bank_setup(struct owl_gpio_bank *bank)
{
	struct gpio_chip *chip = &bank->gpio_chip;

	chip->base = bank->id * GPIO_PER_BANK;
	chip->ngpio = bank->nr_gpios;
	chip->request = owl_gpio_request;
	chip->free = owl_gpio_free;
	chip->get = owl_gpio_get;
	chip->set = owl_gpio_set;
	chip->direction_input = owl_gpio_direction_input;
	chip->direction_output = owl_gpio_direction_output;
	chip->to_irq = owl_gpio_to_irq;
	chip->owner = THIS_MODULE;
	chip->dev = &bank->pdev->dev;
	chip->label = dev_name(chip->dev);
	chip->of_node = chip->dev->of_node;
	chip->owner = THIS_MODULE;

	return 0;
}

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;

static int owl_gpio_irq_setup(struct owl_gpio_bank *bank)
{
	struct gpio_chip *chip = &bank->gpio_chip;
	int irq, i;

	bank->irq_domain = irq_domain_add_linear(chip->of_node,
			  chip->ngpio,
			  &irq_domain_simple_ops,
			  NULL);
	if (!bank->irq_domain) {
		dev_err(chip->dev, "Couldn't allocate IRQ domain\n");
		return -ENXIO;
	}

	for (i = 0; i < chip->ngpio; i++) {
		irq = owl_gpio_to_irq(chip, i);

		irq_set_lockdep_class(irq, &gpio_lock_class);
		irq_set_chip_data(irq, bank);
		irq_set_chip_and_handler(irq, &owl_gpio_irq_chip,
				handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	irq = bank->irq;
	irq_set_chained_handler(irq, owl_gpio_irq_handler);
	irq_set_handler_data(irq, bank);

	return 0;
}

static const struct owl_gpio_bank_data s700_banks_data[5] = {
	/*      outen   inen     dat   intc_pd  intc_mask intc_type intc_ctlr */
	{ 32, {  0x0,    0x4,    0x8,    0x208,   0x20c,   0x230,   0x204 } },
	{ 32, {  0xc,    0x10,   0x14,   0x210,   0x214,   0x238,   0x204 } },
	{ 32, {  0x18,   0x1c,   0x20,   0x218,   0x21c,   0x240,   0x204 } },
	{ 32, {  0x24,   0x28,   0x2c,   0x220,   0x224,   0x248,   0x204 } },
	{ 5,  {  0x30,   0x34,   0x38,   0x228,   0x22c,   0x250,   0x204 } },
};

static const struct owl_gpio_bank_data s900_banks_data[6] = {
	/*      outen   inen    dat    intc_pd intc_mask intc_type intc_ctlr */
	{ 32, {  0x0,    0x4,   0x8,    0x208,   0x20c,   0x240,   0x204 } },
	{ 32, {  0xc,    0x10,  0x14,   0x210,   0x214,   0x248,   0x540 } },
	{ 32, {  0x18,   0x1c,  0x20,   0x218,   0x21c,   0x250,   0x544 } },
	{ 32, {  0x24,   0x28,  0x2c,   0x220,   0x224,   0x258,   0x548 } },
	{ 32, {  0x30,   0x34,  0x38,   0x228,   0x22c,   0x260,   0x54c } },
	{ 8,  {  0xf0,   0xf4,  0xf8,   0x230,   0x234,   0x268,   0x550 } },
};

static const struct of_device_id owl_gpio_dt_ids[] = {
	{ .compatible = "actions,s900-gpio", .data = (void *)S900_GPIO, },
	{ .compatible = "actions,s700-gpio", .data = (void *)S700_GPIO, },
	{ },
};
MODULE_DEVICE_TABLE(of, owl_gpio_dt_ids);

static int owl_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(owl_gpio_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	const struct owl_gpio_bank_data *pdata;
	struct owl_gpio_bank *bank;
	int ret;

	bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
	if (bank == NULL)
		return -ENOMEM;

	bank->id = of_alias_get_id(np, "gpio");
	if (bank->id < 0)
		return bank->id;
	bank->devid = (enum owl_gpio_id) of_id->data;

	pr_info("GPIO%c initialization\n", 'A' + bank->id);

	if (is_s900_gpio(bank)) {
		BUG_ON(bank->id >= ARRAY_SIZE(s900_banks_data));
		pdata = &s900_banks_data[bank->id];
	} else if (is_s700_gpio(bank)) {
		BUG_ON(bank->id >= ARRAY_SIZE(s700_banks_data));
		pdata = &s700_banks_data[bank->id];
	} else {
		return -ENOENT;
	}

	bank->base = of_iomap(dev->of_node, 0);
	if (IS_ERR(bank->base))
		return -ENOENT;

	bank->irq = irq_of_parse_and_map(np, 0);
	if (bank->irq < 0) {
		dev_err(dev, "failed to get IRQ");
		return -ENOENT;
	}

	bank->nr_gpios = pdata->nr_gpios;
	bank->regs = &pdata->regs;
	bank->pdev = pdev;
	spin_lock_init(&bank->lock);

	owl_gpio_bank_setup(bank);
	platform_set_drvdata(pdev, bank);

	ret = owl_gpio_irq_setup(bank);
	if (ret < 0) {
		dev_err(dev, "failed to setup irq, ret %d\n", ret);
		return ret;
	}

	ret = gpiochip_add(&bank->gpio_chip);
	if (ret < 0) {
		dev_err(dev, "failed to add GPIO chip, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver owl_gpio_driver = {
	.driver		= {
		.name	= "gpio-owl",
		.owner	= THIS_MODULE,
		.of_match_table = owl_gpio_dt_ids,
	},
	.probe		= owl_gpio_probe,
};

static int __init owl_gpio_init(void)
{
	return platform_driver_register(&owl_gpio_driver);
}
postcore_initcall(owl_gpio_init);

MODULE_AUTHOR("David Liu <liuwei@actions-semi.com>");
MODULE_DESCRIPTION("Actions OWL SoCs GPIO driver");
MODULE_LICENSE("GPL");
