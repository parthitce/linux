/*
 * Actions OWL SoC Power domain controller driver
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

//#define DEBUG
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_clock.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <dt-bindings/pm-domains/pm-domains-s900.h>

#define OWL_POWER_DOMAIN_TIMEOUT	5000	/* us */

/* SPS Registers Offset */
#define SPS_PG_CTL			0x0000
#define SPS_PG_ACK			0x0004

#define SPS_LDO_CTL			0x0014

#define NOC_BASE			0xE0500000

static void __iomem *sps_base;
static void __iomem *noc_base;
static DEFINE_SPINLOCK(sps_lock);

struct owl_pm_clock_entry {
	struct list_head node;
	struct clk *clk;
};

struct owl_pm_reset_entry {
	struct list_head node;
	struct reset_control *rst;
};

struct owl_pm_device_entry {
	struct list_head node;
	struct device *dev;

	struct list_head clk_list;
	struct list_head rst_list;
};

struct owl_pm_domain {
	struct generic_pm_domain domain;
	char const *name;
	int id;
	int flag;
	bool is_off;
	int enable_bit;
	int status_bit;
	struct list_head dev_list;
	struct mutex dev_lock;
};

struct owl_pm_domain_info {
	unsigned int	num_domains;
	struct owl_pm_domain **domains;
};

static struct owl_pm_domain *genpd_to_owl_pm_domain(
		struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct owl_pm_domain, domain);
}

static int owl_pd_isolate(struct owl_pm_domain *opd, bool enable_isolation)
{
	unsigned int ctrl;
	unsigned long flags;

	/* only GPU need isolation */
	if (opd->id != POWER_DOMAIN_GPU_B)
		return 0;

	spin_lock_irqsave(&sps_lock, flags);

	ctrl = readl(sps_base + SPS_PG_CTL);

	/* software control GPU powergate */
	ctrl &= ~BIT(11);
	if (enable_isolation)
		/* enable isolation */
		ctrl &= ~BIT(12);
	else
		/* disable isolation */
		ctrl |= BIT(12);
	writel(ctrl, sps_base + SPS_PG_CTL);

	spin_unlock_irqrestore(&sps_lock, flags);

	return 0;
}

static int owl_usb3_avdd(struct owl_pm_domain *opd, bool enable)
{
	unsigned int ctrl;
	unsigned long flags;

	if (opd->id != POWER_DOMAIN_USB3)
		return 0;

	spin_lock_irqsave(&sps_lock, flags);

	ctrl = readl(sps_base + SPS_LDO_CTL);

	if (enable)
		ctrl |= BIT(17);
	else
		ctrl &= ~BIT(17);
	writel(ctrl, sps_base + SPS_LDO_CTL);

	spin_unlock_irqrestore(&sps_lock, flags);

	return 0;
}

static int owl_pd_powergate(struct owl_pm_domain *opd, bool power_on)
{
	unsigned int ctrl, is_on;
	int timeout = OWL_POWER_DOMAIN_TIMEOUT;
	unsigned long flags;

	pr_debug("%s: before powergate(on:%d) SPS_CTL 0x%x, SPS_ACK 0x%x\n",
		__func__, power_on,
		readl(sps_base + SPS_PG_CTL),
		readl(sps_base + SPS_PG_ACK));

	if(!power_on)
		owl_usb3_avdd(opd, false);

	/* disable isolation before powergate on */
	if (power_on)
		owl_pd_isolate(opd, false);

	spin_lock_irqsave(&sps_lock, flags);

	ctrl = readl(sps_base + SPS_PG_CTL);
	if (power_on)
		ctrl |= BIT(opd->enable_bit);
	else
		ctrl &= ~BIT(opd->enable_bit);
	writel(ctrl, sps_base + SPS_PG_CTL);

	spin_unlock_irqrestore(&sps_lock, flags);

	while (timeout) {
		is_on = readl(sps_base + SPS_PG_ACK) & BIT(opd->status_bit);
		if ((power_on && is_on) || (!power_on && !is_on))
			break;
		timeout--;
		udelay(1);
	}

	if (!timeout) {
		pr_err("%s: wait power on %s ack timeout\n",
			__func__, opd->domain.name);
		 return -ETIMEDOUT;
	}

	/* enable isolation after powergate off */
	if (!power_on)
		owl_pd_isolate(opd, true);

	if(power_on)
		owl_usb3_avdd(opd, true);
	
	pr_debug("%s: after powergate(on:%d) SPS_CTL0x %x, SPS_ACK 0x%x\n",
		__func__, power_on,
		readl(sps_base + SPS_PG_CTL),
		readl(sps_base + SPS_PG_ACK));

	return 0;
}

static int owl_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de;
	struct owl_pm_clock_entry *ce;
	struct owl_pm_reset_entry *re;
	int ret;

	pr_debug("%s %d: domain %s, power_on %d\n",
		__func__, __LINE__, domain->name, power_on);

	opd = genpd_to_owl_pm_domain(domain);

	/* no clk, set power domain will fail */
	if (list_empty(&opd->dev_list)) {
		pr_warning("%s: no devices in %s power domain\n", __func__,
			opd->domain.name);
	}

	/* assert reset for the devices in power domain */
	list_for_each_entry(de, &opd->dev_list, node) {
		list_for_each_entry(re, &de->rst_list, node) {
			reset_control_assert(re->rst);
		}
	}

	/* real powergate operation */
	ret = owl_pd_powergate(opd, power_on);

	/* deassert reset for the devices in power domain */
	list_for_each_entry(de, &opd->dev_list, node) {
		/* enable clock for reset */
		list_for_each_entry(ce, &de->clk_list, node) {
			clk_prepare_enable(ce->clk);
		}

		/* deassert reset */
		list_for_each_entry(re, &de->rst_list, node) {
			reset_control_deassert(re->rst);
		}

		/* set noc parameter for 4K video decoder performace */
		if (opd->id == POWER_DOMAIN_GPU_B) {
			unsigned long flags;

			spin_lock_irqsave(&sps_lock, flags);

			writel(0x0, noc_base + 0x908);
			writel(0x0, noc_base + 0xa08);

			spin_unlock_irqrestore(&sps_lock, flags);
		}

		/* disable clock after reset */
		list_for_each_entry(ce, &de->clk_list, node) {
			clk_disable_unprepare(ce->clk);
		}
	}

	return ret;
}

void owl_pd_attach_dev(struct device *dev)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de;
	struct owl_pm_clock_entry *ce;
	struct owl_pm_reset_entry *re;
	struct clk *clk;
	struct reset_control *rst;
	int i;

	pr_debug("%s: attach dev %s\n", __func__, dev_name(dev));

	opd = genpd_to_owl_pm_domain(pd_to_genpd(dev->pm_domain));

	de = kzalloc(sizeof(*de), GFP_KERNEL);
	if (!de) {
		pr_err("%s: Not enough memory for device entry\n", __func__);
		return;
	}

	/* add device to power domain */
	de->dev = dev;
	INIT_LIST_HEAD(&de->clk_list);
	INIT_LIST_HEAD(&de->rst_list);
	mutex_lock(&opd->dev_lock);
	list_add_tail(&de->node, &opd->dev_list);
	mutex_unlock(&opd->dev_lock);

	/* add device clocks of device to power domain */
	i = 0;
	while ((clk = of_clk_get(dev->of_node, i++)) && !IS_ERR(clk)) {
		ce = kzalloc(sizeof(*ce), GFP_KERNEL);
		if (!ce) {
			dev_err(dev, "Not enough memory for clock entry.\n");
			return;
		}

		ce->clk = clk;
		mutex_lock(&opd->dev_lock);
		list_add_tail(&ce->node, &de->clk_list);
		mutex_unlock(&opd->dev_lock);
	}

	/* add reset control of device to power domain */
	i = 0;
	while ((rst = of_reset_control_get_by_index(dev, i++)) && !IS_ERR(rst)) {
		re = kzalloc(sizeof(*re), GFP_KERNEL);
		if (!re) {
			dev_err(dev, "Not enough memory for reset entry.\n");
			return;
		}

		re->rst = rst;
		mutex_lock(&opd->dev_lock);
		list_add_tail(&re->node, &de->rst_list);
		mutex_unlock(&opd->dev_lock);
	}
}

void owl_pd_detach_dev(struct device *dev)
{
	struct owl_pm_domain *opd;
	struct owl_pm_device_entry *de, *_de;
	struct owl_pm_clock_entry *ce, *_ce;
	struct owl_pm_reset_entry *re, *_re;

	pr_debug("%s: detach dev %s\n", __func__, dev_name(dev));

	opd = genpd_to_owl_pm_domain(pd_to_genpd(dev->pm_domain));
	mutex_lock(&opd->dev_lock);

	list_for_each_entry_safe(de, _de, &opd->dev_list, node) {
		if (de->dev == dev) {
			list_for_each_entry_safe(ce, _ce, &de->clk_list, node) {
				clk_put(ce->clk);
				list_del(&ce->node);
				kfree(ce);
			}

			list_for_each_entry_safe(re, _re, &de->rst_list, node) {
				reset_control_put(re->rst);
				list_del(&re->node);
				kfree(re);
			}

			list_del(&de->node);
			kfree(de);
		}
	}

	mutex_unlock(&opd->dev_lock);
}

static int owl_pd_power_on(struct generic_pm_domain *domain)
{
	return owl_pd_power(domain, true);
}

static int owl_pd_power_off(struct generic_pm_domain *domain)
{
	return owl_pd_power(domain, false);
}

struct generic_pm_domain *owl_of_genpd_xlate_onecell(
		struct of_phandle_args *genpdspec,
		void *data)
{
	struct owl_pm_domain_info *pdi = data;
	unsigned int idx = genpdspec->args[0];

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	if (idx >= pdi->num_domains) {
		pr_err("%s: invalid domain index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!pdi->domains[idx])
		return ERR_PTR(-ENOENT);

	return &pdi->domains[idx]->domain;
}

static int owl_of_genpd_add_provider(struct device_node *np,
			struct owl_pm_domain_info *pdi)
{
	return __of_genpd_add_provider(np, owl_of_genpd_xlate_onecell, pdi);
}

#define OWL_DOMAIN(_id, _name, _en_bit, _ack_bit, _is_off, _flag)	\
{									\
	.domain	= {							\
			.name = _name,					\
			.power_on_latency_ns = 500000,			\
			.power_off_latency_ns = 200000,			\
			.power_off = owl_pd_power_off,			\
			.power_on = owl_pd_power_on,			\
			.attach_dev = owl_pd_attach_dev,		\
			.detach_dev = owl_pd_detach_dev,		\
		},							\
	.id = _id,							\
	.enable_bit = _en_bit,						\
	.status_bit = _ack_bit,						\
	.is_off = _is_off,						\
	.flag	= _flag,						\
}

static struct owl_pm_domain s900_pm_domain_gpu_b =
	OWL_DOMAIN(POWER_DOMAIN_GPU_B, "gpu_b", 3, 3, true, 0);
static struct owl_pm_domain s900_pm_domain_vce =
	OWL_DOMAIN(POWER_DOMAIN_VCE, "vce", 4, 4, true, 0);
static struct owl_pm_domain s900_pm_domain_sensor =
	OWL_DOMAIN(POWER_DOMAIN_SENSOR, "sensor", 5, 5, true, 0);
static struct owl_pm_domain s900_pm_domain_vde =
	OWL_DOMAIN(POWER_DOMAIN_VDE, "vde", 6, 6, true, 0);
static struct owl_pm_domain s900_pm_domain_hde =
	OWL_DOMAIN(POWER_DOMAIN_HDE, "hde", 7, 7, true, 0);
static struct owl_pm_domain s900_pm_domain_usb3 =
	OWL_DOMAIN(POWER_DOMAIN_USB3, "usb3", 8, 8, true, 0);
static struct owl_pm_domain s900_pm_domain_ddr0 =
	OWL_DOMAIN(POWER_DOMAIN_DDR0, "ddr0", 9, 9, true, 0);
static struct owl_pm_domain s900_pm_domain_ddr1 =
	OWL_DOMAIN(POWER_DOMAIN_DDR1, "ddr1", 10, 10, true, 0);
static struct owl_pm_domain s900_pm_domain_de =
	OWL_DOMAIN(POWER_DOMAIN_DE, "de", 13, 11, false, 0);
static struct owl_pm_domain s900_pm_domain_nand =
	OWL_DOMAIN(POWER_DOMAIN_NAND, "nand", 14, 12, true, 0);
static struct owl_pm_domain s900_pm_domain_usb2h0 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H0, "usb2h0", 15, 13, true, 0);
static struct owl_pm_domain s900_pm_domain_usb2h1 =
	OWL_DOMAIN(POWER_DOMAIN_USB2H1, "usb2h1", 16, 14, true, 0);

static struct owl_pm_domain *s900_pm_domains[] = {
	[POWER_DOMAIN_GPU_B] = &s900_pm_domain_gpu_b,
	[POWER_DOMAIN_VCE] = &s900_pm_domain_vce,
	[POWER_DOMAIN_SENSOR] = &s900_pm_domain_sensor,
	[POWER_DOMAIN_VDE] = &s900_pm_domain_vde,
	[POWER_DOMAIN_HDE] = &s900_pm_domain_hde,
	[POWER_DOMAIN_USB3] = &s900_pm_domain_usb3,
	[POWER_DOMAIN_DDR0] = &s900_pm_domain_ddr0,
	[POWER_DOMAIN_DDR1] = &s900_pm_domain_ddr1,
	[POWER_DOMAIN_DE] = &s900_pm_domain_de,
	[POWER_DOMAIN_NAND] = &s900_pm_domain_nand,
	[POWER_DOMAIN_USB2H0] = &s900_pm_domain_usb2h0,
	[POWER_DOMAIN_USB2H1] = &s900_pm_domain_usb2h1,
};

struct owl_pm_domain_info s900_pm_domain_info = {
	.num_domains = ARRAY_SIZE(s900_pm_domains),
	.domains = s900_pm_domains,
};

static struct of_device_id owl_pm_domain_matches[] = {
	{
		.compatible	= "actions,s900-pm-domains",
		.data		= &s900_pm_domain_info,
	},
	{ },
};

static __init int owl_pm_domain_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	struct owl_pm_domain_info *pdi;
	struct owl_pm_domain *opd;
	int i;

	pr_info("[OWL] Power domain initialization\n");

	np = of_find_matching_node(NULL, owl_pm_domain_matches);
	if (!np)
		return -ENODEV;

	match = of_match_node(owl_pm_domain_matches, np);
	pdi = (struct owl_pm_domain_info *)match->data;
	if (!pdi)
		return -ENODEV;

	sps_base = of_iomap(np, 0);
	noc_base = ioremap(NOC_BASE, 0x1000);

	for (i = 0; i < pdi->num_domains; i++) {
		opd = pdi->domains[i];
		if (opd) {
			mutex_init(&opd->dev_lock);
			INIT_LIST_HEAD(&opd->dev_list);
			pm_genpd_init(&opd->domain, NULL, opd->is_off);
		}
	}

	return owl_of_genpd_add_provider(np, pdi);
}

arch_initcall(owl_pm_domain_init);
