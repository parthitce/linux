#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/mfd/atc260x/atc260x.h>

#include <asm/io.h>

#define CMU_STABILITY_WAIT_US		50
#define ATC260X_CMU_DEVRST_INT		BIT(2)
#define ATC260X_PAD_EN_MASK		BIT(0)
#define ATC2603C_PMU_OC_INT_EN_MAGIC	0x1bc0
#define ATC2603C_PMU_DBG_FILTER_EN	BIT(6)
#define ATC2603C_PMU_DBG_EN		BIT(7)
#define ATC2603C_PMU_DISABLE_PULLDOWN	BIT(5)
#define ATC2603C_PMU_DISABLE_EFUSE	BIT(11)
#define ATC2609A_PMU_OC_INT_EN_MAGIC	0x0ff8

static const struct regmap_config atc2603c_regmap_config = {
	.reg_bits       = 8,
	.val_bits       = 16,
/*
	.rd_table       = &atc2603c_readable_table,
	.wr_table       = &atc2603c_writeable_table,
	.volatile_table = &atc2603c_volatile_table,
*/
	.max_register   = ATC2603C_SADDR,
	.cache_type     = REGCACHE_RBTREE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static const struct regmap_config atc2609a_regmap_config = {
	.reg_bits       = 8,
	.val_bits       = 16,
/*
	.rd_table       = &atc2609a_readable_table,
	.wr_table       = &atc2609a_writeable_table,
	.volatile_table = &atc2609a_volatile_table,
*/
	.max_register   = ATC2609A_SADDR,
	.cache_type     = REGCACHE_RBTREE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

enum {
	ATC260X_IRQ_AUDIO = 0,
	ATC260X_IRQ_OV,
	ATC260X_IRQ_OC,
	ATC260X_IRQ_OT,
	ATC260X_IRQ_UV,
	ATC260X_IRQ_ALARM,
	ATC260X_IRQ_ONOFF,
	ATC260X_IRQ_WKUP,
	ATC260X_IRQ_IR,
	ATC260X_IRQ_REMCON,
	ATC260X_IRQ_POWER_IN,
};

static const struct regmap_irq atc2603c_regmap_irqs[] = {
	REGMAP_IRQ_REG(ATC260X_IRQ_AUDIO,	0, BIT(0)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OV,		0, BIT(1)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OC,		0, BIT(2)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OT,		0, BIT(3)),
	REGMAP_IRQ_REG(ATC260X_IRQ_UV,		0, BIT(4)),
	REGMAP_IRQ_REG(ATC260X_IRQ_ALARM,	0, BIT(5)),
	REGMAP_IRQ_REG(ATC260X_IRQ_ONOFF,	0, BIT(6)),
	REGMAP_IRQ_REG(ATC260X_IRQ_WKUP,	0, BIT(7)),
	REGMAP_IRQ_REG(ATC260X_IRQ_IR,		0, BIT(8)),
	REGMAP_IRQ_REG(ATC260X_IRQ_REMCON,	0, BIT(9)),
	REGMAP_IRQ_REG(ATC260X_IRQ_POWER_IN,	0, BIT(10)),
};

static const struct regmap_irq atc2609a_regmap_irqs[] = {
	REGMAP_IRQ_REG(ATC260X_IRQ_AUDIO,	0, BIT(0)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OV,		0, BIT(1)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OC,		0, BIT(2)),
	REGMAP_IRQ_REG(ATC260X_IRQ_OT,		0, BIT(3)),
	REGMAP_IRQ_REG(ATC260X_IRQ_UV,		0, BIT(4)),
	REGMAP_IRQ_REG(ATC260X_IRQ_ALARM,	0, BIT(5)),
	REGMAP_IRQ_REG(ATC260X_IRQ_ONOFF,	0, BIT(6)),
	REGMAP_IRQ_REG(ATC260X_IRQ_WKUP,	0, BIT(7)),
	REGMAP_IRQ_REG(ATC260X_IRQ_IR,		0, BIT(8)),
	REGMAP_IRQ_REG(ATC260X_IRQ_REMCON,	0, BIT(9)),
	REGMAP_IRQ_REG(ATC260X_IRQ_POWER_IN,	0, BIT(10)),
};

static const struct regmap_irq_chip atc2603c_regmap_irq_chip = {
	.name			= "atc2603c_irq_chip",
	.status_base		= ATC2603C_INTS_PD,
	.mask_base		= ATC2603C_INTS_MSK,
	.mask_invert		= true,
	.irqs			= atc2603c_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(atc2603c_regmap_irqs),
	.num_regs		= 1,

};

static const struct regmap_irq_chip atc2609a_regmap_irq_chip = {
	.name			= "atc2609a_irq_chip",
	.status_base		= ATC2609A_INTS_PD,
	.mask_base		= ATC2609A_INTS_MSK,
	.mask_invert		= true,
	.irqs			= atc2609a_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(atc2609a_regmap_irqs),
	.num_regs		= 1,

};

static const struct resource atc260x_regulator_resources[] = {
	DEFINE_RES_IRQ_NAMED(ATC260X_IRQ_OV, "ATC260X_OV"),
	DEFINE_RES_IRQ_NAMED(ATC260X_IRQ_OC, "ATC260X_OC"),
	DEFINE_RES_IRQ_NAMED(ATC260X_IRQ_OT, "ATC260X_OT"),
	DEFINE_RES_IRQ_NAMED(ATC260X_IRQ_UV, "ATC260X_UV"),
};

static const struct resource atc2603c_onoff_resources[] = {
	DEFINE_RES_IRQ_NAMED(6, "ATC260C_ONOFF"),
};

static const struct mfd_cell atc2603c_cells[] = {
	{
		.name = "atc2603c-regulator",
		.resources = atc260x_regulator_resources,
		.num_resources = ARRAY_SIZE(atc260x_regulator_resources),
	},
	{
		.name = "atc2603c-onoff",
		.of_compatible = "actions,atc2603c-onoff",
		.num_resources = ARRAY_SIZE(atc2603c_onoff_resources),
		.resources = atc2603c_onoff_resources,
	},
};

static const struct mfd_cell atc2609a_cells[] = {
	{
		.name = "atc2609a-regulator",
	},
};

static int atc260x_pmic_init(struct atc260x_dev *atc260x)
{
	int ret;
	int val;
	unsigned int cmu_reg, int_reg, pad_reg;

	switch (atc260x->variant) {
	case ATC2603C_ID:
		cmu_reg = ATC2603C_CMU_DEVRST;
		int_reg = ATC2603C_INTS_MSK;
		pad_reg = ATC2603C_PAD_EN;
		break;
	case ATC2609A_ID:
		cmu_reg = ATC2609A_CMU_DEVRST;
		int_reg = ATC2609A_INTS_MSK;
		pad_reg = ATC2609A_PAD_EN;
		break;
	}

	ret = regmap_read(atc260x->regmap, cmu_reg, &val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to preserve CMU DEVRST value\n");
		return val;
	}

	ret = regmap_update_bits(atc260x->regmap, cmu_reg, ATC260X_CMU_DEVRST_INT, 0);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to reset CMU\n");
		return ret;
	}
	udelay(CMU_STABILITY_WAIT_US);

	ret = regmap_write(atc260x->regmap, cmu_reg, val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to restore CMU DEVRST\n");
		return ret;
	}
	udelay(CMU_STABILITY_WAIT_US);

	ret = regmap_write(atc260x->regmap, int_reg, 0);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to restore CMU DEVRST\n");
		return ret;
	}

	ret = regmap_update_bits(atc260x->regmap, pad_reg, ATC260X_PAD_EN_MASK, 1);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to reset CMU\n");
		return ret;
	}

	return 0;
}

static int atc2603c_init(struct atc260x_dev *atc260x)
{
	int ret;
	int val;

	ret = regmap_read(atc260x->regmap, ATC2603C_PMU_OC_INT_EN, &val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to read magic/default INT value\n");
		return ret;
	}

	if (val != ATC2603C_PMU_OC_INT_EN_MAGIC) {
		dev_err(atc260x->dev, "Invalid device ID: Not ATC2603C\n");
		return -EINVAL;
	}

	ret = regmap_read(atc260x->regmap, ATC2603C_PMU_BDG_CTL, &val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to read debug/pullup config\n");
		return ret;
	}
	val |= ATC2603C_PMU_DBG_FILTER_EN | ATC2603C_PMU_DBG_EN;
	val &= ~(ATC2603C_PMU_DISABLE_PULLDOWN | ATC2603C_PMU_DISABLE_EFUSE);

	ret = regmap_write(atc260x->regmap, ATC2603C_PMU_BDG_CTL, val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to write debug/pullup config\n");
		return ret;
	}

/*
	regmap_update_bits(atc260x->regmap, ATC2603C_PAD_VSEL, BIT(2), 0);
	regmap_update_bits(atc260x->regmap, ATC2603C_MFP_CTL, GENMASK(7, 8), 256);
	regmap_update_bits(atc260x->regmap, ATC2603C_PAD_EN, GENMASK(2, 3), 12);
*/

	return atc260x_pmic_init(atc260x);
}

static int atc2609a_init(struct atc260x_dev *atc260x)
{
	int ret;
	int val;

	ret = regmap_read(atc260x->regmap, ATC2609A_PMU_OC_INT_EN, &val);
	if (ret < 0) {
		dev_err(atc260x->dev, "Failed to read magic/default INT value\n");
		return ret;
	}

	if (val != ATC2609A_PMU_OC_INT_EN_MAGIC) {
		dev_err(atc260x->dev, "Invalid device ID: Not ATC2609A\n");
		return -EINVAL;
	}


	return atc260x_pmic_init(atc260x);
}

static int atc260x_match_device(struct atc260x_dev *atc260x)
{
	struct device *dev = atc260x->dev;
	const struct of_device_id *id;

	if (dev->of_node) {
		id = of_match_device(dev->driver->of_match_table, dev);
		if (!id) {
			dev_err(dev, "Unable to match OF ID\n");
			return -ENODEV;
		}
		atc260x->variant = (long)id->data;
	}

	switch (atc260x->variant) {
	case ATC2603C_ID:
		atc260x->nr_cells = ARRAY_SIZE(atc2603c_cells);
		atc260x->cells = atc2603c_cells;
		atc260x->regmap_cfg = &atc2603c_regmap_config;
		atc260x->regmap_irq_chip = &atc2603c_regmap_irq_chip;
		break;
	case ATC2609A_ID:
		atc260x->nr_cells = ARRAY_SIZE(atc2609a_cells);
		atc260x->cells = atc2609a_cells;
		atc260x->regmap_cfg = &atc2609a_regmap_config;
		atc260x->regmap_irq_chip = &atc2609a_regmap_irq_chip;
		break;
	default:
		dev_err(dev, "unsupported ATC260X ID %lu\n", atc260x->variant);
		return -EINVAL;
	}

	return 0;
}

static int atc260x_device_probe(struct atc260x_dev *atc260x)
{
	int ret;

	switch (atc260x->variant) {
	case ATC2603C_ID:
		ret = atc2603c_init(atc260x);
		break;
	case ATC2609A_ID:
		ret = atc2609a_init(atc260x);
		break;
	}
	if (ret)
		return ret;

	ret = regmap_add_irq_chip(atc260x->regmap, atc260x->irq,
			  IRQF_ONESHOT | IRQF_SHARED,
			   0, atc260x->regmap_irq_chip, &atc260x->regmap_irqc);
	if (ret) {
		dev_err(atc260x->dev, "failed to add irq chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(atc260x->dev, PLATFORM_DEVID_AUTO, atc260x->cells,
				atc260x->nr_cells, NULL, 0, NULL);
				//atc260x->nr_cells, NULL, 0, regmap_irq_get_domain(atc260x->regmap_irqc));
	if (ret) {
		dev_err(atc260x->dev, "failed to add MFD devices: %d\n", ret);
		regmap_del_irq_chip(atc260x->irq, atc260x->regmap_irqc);
		return ret;
	}

	return 0;
}

static int atc260x_device_remove(struct atc260x_dev *atc260x)
{
	mfd_remove_devices(atc260x->dev);
	regmap_del_irq_chip(atc260x->irq, atc260x->regmap_irqc);

	return 0;
}

static int atc260x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	int ret;
	struct atc260x_dev *atc260x;

	atc260x = devm_kzalloc(&i2c->dev, sizeof(*atc260x), GFP_KERNEL);
	if (!atc260x)
		return -ENOMEM;

	atc260x->dev = &i2c->dev;
	atc260x->irq = i2c->irq;
	dev_set_drvdata(atc260x->dev, atc260x);

	pr_info("%s interrupt number: %d\n", __func__, atc260x->irq);

	ret = atc260x_match_device(atc260x);
	if (ret)
		return ret;

	atc260x->regmap = devm_regmap_init_i2c(i2c, atc260x->regmap_cfg);
	if (IS_ERR(atc260x->regmap)) {
		ret = PTR_ERR(atc260x->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	return atc260x_device_probe(atc260x);
}

static int atc260x_i2c_remove(struct i2c_client *i2c)
{
	struct atc260x_dev *atc260x = i2c_get_clientdata(i2c);
	return atc260x_device_remove(atc260x);
}

static const struct of_device_id atc260x_i2c_of_match[] = {
	{.compatible = "actions,atc2603c", .data = (void *)ATC2603C_ID},
	{.compatible = "actions,atc2609a", .data = (void *)ATC2609A_ID},
	{},
};
MODULE_DEVICE_TABLE(of, atc260x_i2c_of_match);

static const struct i2c_device_id atc260x_i2c_id_tbl[] = {
	{"atc2603a", 0},
	{"atc2603c", 0},
	{"atc2609a", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, atc260x_i2c_id_tbl);

static struct i2c_driver atc260x_i2c_driver = {
	.probe = atc260x_i2c_probe,
	.remove = atc260x_i2c_remove,
	.driver = {
		.name   = "atc260x_i2c",
		.of_match_table = of_match_ptr(atc260x_i2c_of_match),
	},
	.id_table   = atc260x_i2c_id_tbl,
};

static int __init atc260x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&atc260x_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register atc260x I2C driver: %d\n", ret);
	return ret;
}

subsys_initcall(atc260x_i2c_init);
static void __exit atc260x_i2c_exit(void)
{
	i2c_del_driver(&atc260x_i2c_driver);
}
module_exit(atc260x_i2c_exit);

MODULE_DESCRIPTION("I2C support for atc260x PMICs");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Actions Semi.");
MODULE_AUTHOR("Parthiban Nallathambi <parthiban@linumiz.com>");
MODULE_AUTHOR("Saravanan Sekar <Saravanan@linumiz.com>");
