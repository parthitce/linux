//SPDX-License-Identifier: GPL-2.0
/*
 * Voltage regulation driver for Actions Semi ATC2603C and ATC2609A PMIC
 *
 * Author: Parthiban Nallathambi <parthiban@linumiz.com>
 * Author: Saravanan Sekar <saravanan@linumiz.com>
 *
 */

#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/atc260x/atc260x.h>

#define ATC2603C_DC_MODE_MASK	BIT(5) | BIT(6)
#define ATC2603C_DC_MODE_EN	BIT(5)
#define ATC2603C_DC_MODE	BIT(6)

/* Regulators IDs */
enum {
	ATC2603C_DCDC1 = 0,
	ATC2603C_DCDC2,
	ATC2603C_DCDC3,
	ATC2603C_LDO1,
	ATC2603C_LDO2,
	ATC2603C_LDO3,
	ATC2603C_LDO5,
	ATC2603C_LDO6,
	ATC2603C_LDO7,
	ATC2603C_LDO8,
	ATC2603C_LDO11,
	ATC2603C_SWITCH_LDO13,
	ATC2603C_REG_ID_MAX,
};

enum {
	ATC2609A_DCDC0 = 0,
	ATC2609A_DCDC1,
	ATC2609A_DCDC2,
	ATC2609A_DCDC3,
	ATC2609A_DCDC4,
	ATC2609A_LDO0,
	ATC2609A_LDO1,
	ATC2609A_LDO2,
	ATC2609A_LDO3,
	ATC2609A_LDO4,
	ATC2609A_LDO6,
	ATC2609A_LDO7,
	ATC2609A_LDO8,
	ATC2609A_LDO9,
	ATC2609A_REG_ID_MAX,
};

struct atc260x_regulator_config {
	bool regulator_is_switch;
	unsigned int ov_thresh;
	unsigned int uv_thresh;
	unsigned int ldo_oc_thresh;
};

struct atc260x_regulators {
	struct device *dev;
	struct atc260x_dev *atc260x;
	struct atc260x_regulator_config config[ATC2609A_REG_ID_MAX];
};

static int atc2603c_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct atc260x_dev *atc260x = rdev_get_drvdata(rdev);
	int dc_mod, ret;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		/* forced pwm mode */
		dc_mod = 2;
		break;
	case REGULATOR_MODE_NORMAL:
		/* enable automatic pwm/pfm mode */
		dc_mod = 1;
		break;
	case REGULATOR_MODE_IDLE:
		/* forced pfm mode */
		dc_mod = 0;
		break;
	default:
		dev_err(&rdev->dev, "Not supported buck mode %s\n", __func__);
		/* defaults to automatic */
		dc_mod = 1;
	}

	ret = regmap_update_bits(atc260x->regmap, rdev->desc->vsel_reg, ATC2603C_DC_MODE_MASK, dc_mod);
	if (ret)
		dev_err(&rdev->dev, "Error updating mode PWM/PFM, defaults to automatic mode\n");

	return ret;
}

static unsigned int atc2603c_buck_get_mode(struct regulator_dev *rdev)
{
	struct atc260x_dev *atc260x = rdev_get_drvdata(rdev);
	int dc_mod, ret;

	ret = regmap_read(atc260x->regmap, rdev->desc->vsel_reg, &dc_mod);
	if (ret) {
		dev_err(&rdev->dev, "Error getting mode PWM/PFM\n");
		return REGULATOR_MODE_INVALID;
	}

	dc_mod &= ATC2603C_DC_MODE_MASK;
	if (!dc_mod)
		return REGULATOR_MODE_IDLE;
	else if (dc_mod == 1 || dc_mod == 3)
		return REGULATOR_MODE_NORMAL;
	else if (dc_mod == 2)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_INVALID;
}

static int atc2609a_set_voltage_ldo(struct regulator_dev *rdev, int min_uV,
					int max_uV, unsigned *selector)
{
	int rid = rdev_get_id(rdev);

	switch (rid) {
	case ATC2609A_LDO3 ... ATC2609A_LDO8:
		break;
	default:
		return -EINVAL;
	}

	if (rdev->desc == NULL)
		return -EINVAL;

	return 0;
}

static const struct regulator_ops atc2603c_buck_enable_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_mode		= atc2603c_buck_get_mode,
	.set_mode		= atc2603c_buck_set_mode,
};

static const struct regulator_ops atc2603c_buck_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.get_mode		= atc2603c_buck_get_mode,
	.set_mode		= atc2603c_buck_set_mode,
};

static const struct regulator_ops atc2603c_buck_3_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.get_mode		= atc2603c_buck_get_mode,
	.set_mode		= atc2603c_buck_set_mode,
	.set_pull_down		= regulator_set_pull_down_regmap,
};

static const struct regulator_ops atc2603c_ldo_soft_enable_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_soft_start		= regulator_set_soft_start_regmap,
};

static const struct regulator_ops atc2603c_ldo_enable_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops atc2603c_ldo_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.set_soft_start		= regulator_set_soft_start_regmap,
};

static const struct regulator_ops atc2603c_ldo_11_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops atc2609a_enable_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops atc2609a_multirange_ops = {
	.set_voltage		= atc2609a_set_voltage_ldo,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops atc2609a_ldo_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops atc2609a_ops_range = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

#define ATC_DESC_EN(_family, _id, _match, _min, _max, _step, _vreg,	\
		 _vmask, _ereg, _emask, _invert, _ops) 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min),				\
		.uV_step	= (_step),				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.enable_is_inverted = (_invert), \
		.ops		= &_ops,						\
	}

#define ATC_DESC(_family, _id, _match, _min, _max, _step, _vreg,	\
		 _vmask, _ops)		 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min),				\
		.uV_step	= (_step),				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.ops		= &_ops,						\
	}

#define ATC_DESC_SOFT(_family, _id, _match, _min, _max, _step, _vreg,	\
		 _vmask, _smask, _ops)		 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min),				\
		.uV_step	= (_step),				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.soft_start_mask = (_smask), \
		.soft_start_reg	= (_vreg), \
		.ops		= &_ops,						\
	}

#define ATC_DESC_SOFT_EN(_family, _id, _match, _min, _max, _step, _vreg,	\
		 _vmask, _emask, _ops) 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min),				\
		.uV_step	= (_step),				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_vreg),					\
		.enable_mask	= (_emask),					\
		.soft_start_mask = BIT(12), \
		.soft_start_reg	= (_vreg), \
		.ops		= &_ops,						\
	}

#define ATC_DESC_PULL(_family, _id, _match, _min, _max, _step, _vreg,	\
		 _vmask, _ops)		 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min),				\
		.uV_step	= (_step),				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.pull_down_reg	= (_vreg), \
		.pull_down_mask	= BIT(12), \
		.ops		= &_ops,						\
	}

#define ATC_DESC_RANGES(_family, _id, _match, _ranges, _n_voltages,	\
			_vreg, _vmask, _ereg, _emask, _ops)				\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (_n_voltages),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.linear_ranges	= (_ranges),					\
		.n_linear_ranges = ARRAY_SIZE(_ranges),				\
		.ops		= &_ops,				\
	}

static const struct regulator_linear_range atc2609a_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(600 * 1000, 0x0, 0x7f, 6250),
	REGULATOR_LINEAR_RANGE(1400 * 1000, 0x80, 0xe8, 25 * 1000),
};

static const struct regulator_linear_range atc2609a_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(2300 * 1000, 0x0, 0xb, 100 * 1000),
	REGULATOR_LINEAR_RANGE(3400 * 1000, 0xc, 0xf, 0),
};

static struct regulator_desc atc2603c_regulators[] = {
	ATC_DESC(ATC2603C, DCDC1, "dcdc1", 700 * 1000, 1400 * 1000, 25 * 1000,
			ATC2603C_PMU_DC1_CTL0, GENMASK(7, 11), atc2603c_buck_ops),
	/* TODO chip version 0x1 is default here */
	ATC_DESC_EN(ATC2603C, DCDC2, "dcdc2", 1000 * 1000, 1800 * 1000, 50 * 1000,
			ATC2603C_PMU_DC2_CTL0, GENMASK(8, 12), ATC2603C_PMU_DC2_CTL0, BIT(15), false, atc2603c_buck_enable_ops),
	ATC_DESC_PULL(ATC2603C, DCDC3, "dcdc3", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_DC3_CTL0, GENMASK(9, 11), atc2603c_buck_3_ops),
	ATC_DESC_SOFT(ATC2603C, LDO1, "ldo1", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO1_CTL, GENMASK(13, 15), BIT(12), atc2603c_ldo_ops),
	ATC_DESC_SOFT(ATC2603C, LDO2, "ldo2", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO2_CTL, GENMASK(13, 15), BIT(12), atc2603c_ldo_ops),
	ATC_DESC_SOFT(ATC2603C, LDO3, "ldo3", 1500 * 1000, 2000 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO3_CTL, GENMASK(13, 15), BIT(12), atc2603c_ldo_ops),
	ATC_DESC_SOFT_EN(ATC2603C, LDO5, "ldo5", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO5_CTL, GENMASK(13, 15), BIT(0), atc2603c_ldo_soft_enable_ops),
	ATC_DESC_SOFT(ATC2603C, LDO6, "ldo6", 700 * 1000, 1400 * 1000, 25 * 1000,
			ATC2603C_PMU_LDO6_CTL, GENMASK(11, 15), BIT(10), atc2603c_ldo_ops),
	ATC_DESC_SOFT_EN(ATC2603C, LDO7, "ldo7", 1500 * 1000, 2000 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO7_CTL, GENMASK(13, 15), BIT(0), atc2603c_ldo_soft_enable_ops),
	ATC_DESC(ATC2603C, LDO11, "ldo11", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_LDO11_CTL, GENMASK(13, 15), atc2603c_ldo_11_ops),
	ATC_DESC_EN(ATC2603C, SWITCH_LDO13, "switch_ldo13", 3000 * 1000, 3300 * 1000, 100 * 1000,
			ATC2603C_PMU_SWITCH_CTL, GENMASK(3, 4), ATC2603C_PMU_SWITCH_CTL, BIT(15) | BIT(5), true, atc2603c_ldo_enable_ops),
};

static struct regulator_desc atc2609a_regulators[] = {
	ATC_DESC_EN(ATC2609A, DCDC0, "dcdc0", 600 * 1000, 219375, 6250,
			ATC2609A_PMU_DC0_CTL0, GENMASK(8, 15), ATC2609A_PMU_DC_OSC, BIT(4), false, atc2609a_enable_ops),
	ATC_DESC_EN(ATC2609A, DCDC1, "dcdc1", 600 * 1000, 219375, 6250,
			ATC2609A_PMU_DC1_CTL0, GENMASK(8, 15), ATC2609A_PMU_DC_OSC, BIT(5), false, atc2609a_enable_ops),
	ATC_DESC_EN(ATC2609A, DCDC2, "dcdc2", 600 * 1000, 219375, 6250,
			ATC2609A_PMU_DC2_CTL0, GENMASK(8, 15), ATC2609A_PMU_DC_OSC, BIT(6), false, atc2609a_enable_ops),
	ATC_DESC_RANGES(ATC2609A, DCDC3, "dcdc3", atc2609a_dcdc3_ranges, 232,
			ATC2609A_PMU_DC2_CTL0, GENMASK(8, 15), ATC2609A_PMU_DC_OSC, BIT(6), atc2609a_ops_range),
	ATC_DESC_EN(ATC2609A, DCDC4, "dcdc4", 600 * 1000, 219375, 6250,
			ATC2609A_PMU_DC4_CTL0, GENMASK(8, 15), ATC2609A_PMU_DC_OSC, BIT(8), false, atc2609a_enable_ops),
	ATC_DESC_RANGES(ATC2609A, LDO0, "ldo0", atc2609a_ldo_ranges, 15,
			ATC2609A_PMU_LDO0_CTL0, GENMASK(2, 5), ATC2609A_PMU_LDO0_CTL0, BIT(0), atc2609a_ops_range),
	ATC_DESC_RANGES(ATC2609A, LDO1, "ldo1", atc2609a_ldo_ranges, 15,
			ATC2609A_PMU_LDO1_CTL0, GENMASK(2, 5), ATC2609A_PMU_LDO1_CTL0, BIT(0), atc2609a_ops_range),
	ATC_DESC_RANGES(ATC2609A, LDO2, "ldo2", atc2609a_ldo_ranges, 15,
			ATC2609A_PMU_LDO2_CTL0, GENMASK(2, 5), ATC2609A_PMU_LDO2_CTL0, BIT(0), atc2609a_ops_range),
	ATC_DESC_EN(ATC2609A, LDO3, "ldo3", 700 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO3_CTL0, GENMASK(1, 4), ATC2609A_PMU_LDO3_CTL0, BIT(0), false, atc2609a_multirange_ops),
	ATC_DESC_EN(ATC2609A, LDO4, "ldo4", 700 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO4_CTL0, GENMASK(1, 4), ATC2609A_PMU_LDO4_CTL0, BIT(0), false, atc2609a_multirange_ops),
	ATC_DESC_EN(ATC2609A, LDO6, "ldo6", 700 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO6_CTL0, GENMASK(1, 4), ATC2609A_PMU_LDO6_CTL0, BIT(0), false, atc2609a_multirange_ops),
	ATC_DESC_EN(ATC2609A, LDO7, "ldo7", 700 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO7_CTL0, GENMASK(1, 4), ATC2609A_PMU_LDO7_CTL0, BIT(0), false, atc2609a_multirange_ops),
	ATC_DESC_EN(ATC2609A, LDO8, "ldo8", 700 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO8_CTL0, GENMASK(1, 4), ATC2609A_PMU_LDO8_CTL0, BIT(0), atc2609a_multirange_ops),
	ATC_DESC(ATC2609A, LDO9, "ldo9", 2600 * 1000, 3300 * 1000, 100 * 1000,
			ATC2609A_PMU_LDO9_CTL, GENMASK(13, 15), atc2609a_ldo_ops),
};

#ifdef CONFIG_OF
static struct of_regulator_match atc2603c_matches[] = {
	{ .name = "dcdc1", },
	{ .name = "dcdc2", },
	{ .name = "dcdc3", },
	{ .name = "ldo1", },
	{ .name = "ldo2", },
	{ .name = "ldo3", },
	{ .name = "ldo5", },
	{ .name = "ldo6", },
	{ .name = "ldo7", },
	{ .name = "ldo11", },
	{ .name = "ldo13", },
};
static struct of_regulator_match atc2609a_matches[] = {
	{ .name = "dcdc0", },
	{ .name = "dcdc1", },
	{ .name = "dcdc2", },
	{ .name = "dcdc3", },
	{ .name = "dcdc4", },
	{ .name = "ldo0", },
	{ .name = "ldo1", },
	{ .name = "ldo2", },
	{ .name = "ldo3", },
	{ .name = "ldo4", },
	{ .name = "ldo6", },
	{ .name = "ldo7", },
	{ .name = "ldo8", },
	{ .name = "ldo9", },
};

static int atc260x_get_regulator_dt_data(struct platform_device *pdev, struct atc260x_regulators *data)
{
	struct device_node *np, *regulators;
	struct of_regulator_match *matches;
	int count;

	np = pdev->dev.parent->of_node;
	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&pdev->dev, "No regulators node in device\n");
		return -ENODEV;
	}

	switch (atc260x->variant) {
	case ATC2603C_ID:
		count = ARRAY_SIZE(atc2603c_matches);
		matches = atc2603c_matches;
		break;
	case ATC2609A_ID:
		count = ARRAY_SIZE(atc2609a_matches);
		matches = atc2609a_matches;
		break;
	default:
		of_node_put(regulators);
		dev_err(&pdev->dev, "Unsupported ATC variant: %ld\n",
			atc260x->variant);
		return -EINVAL;
	}
}

static int atc260x_regulator_probe(struct platform_device *pdev)
{
	struct atc260x_dev *atc260x = dev_get_drvdata(pdev->dev.parent);
	struct atc260x_regulators *atc260x_data;
	struct regulator_dev *rdev;
	const struct regulator_desc *regulators;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
		.regmap = atc260x->regmap,
		.driver_data = atc260x,
	};
	int i, ret, nregulators;

	atc260x_data = devm_kzalloc(&pdev->dev, sizeof(*atc260x_data), GFP_KERNEL);
	if (!atc260x_data)
		return -ENOMEM;

	atc260x_data->dev = &pdev->dev;
	atc260x_data->atc260x = atc260x;
	platform_set_drvdata(pdev, atc260x_data);

	ret = atc260x_get_regulator_dt_data(pdev, atc260x_data);
	if (ret < 0)
		return ret;

	switch (atc260x->variant) {
	case ATC2603C_ID:
		regulators = atc2603c_regulators;
		nregulators = ATC2603C_REG_ID_MAX;
		break;
	case ATC2609A_ID:
		regulators = atc2609a_regulators;
		nregulators = ATC2609A_REG_ID_MAX;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported ATC variant: %ld\n",
			atc260x->variant);
		return -EINVAL;
	}
	for (i = 0; i < nregulators; i++) {
		const struct regulator_desc *desc = &regulators[i];

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %s\n",
				regulators[i].name);
			return PTR_ERR(rdev);
		}
	}
}

static struct platform_driver atc260x_regulator_driver = {
	.probe	= atc260x_regulator_probe,
	.driver	= {
		.name		= "atc260x-regulator",
	},
};

module_platform_driver(atc260x_regulator_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Regulator Driver for ATC260X PMIC");
MODULE_ALIAS("platform:atc260x-regulator");
MODULE_AUTHOR("Parthiban Nallathambi <parthiban@linumiz.com>");
MODULE_AUTHOR("Saravanan Sekar <Saravanan@linumiz.com>");
