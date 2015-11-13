/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/8/24: Created by Lipeng.
 */
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>

#include <video/owl_dss.h>

#include "edpc.h"

struct edpc_config {
	uint32_t		link_rate;

	uint32_t		lane_count;
	uint32_t		lane_polarity;
	uint32_t		lane_mirror;

	uint32_t		mstream_polarity;
	uint32_t		user_sync_polarity;

	uint32_t		pclk_parent;
	uint32_t		pclk_rate;
};

struct edpc_data {
	struct owl_display_ctrl	ctrl;
	struct edpc_config	configs;

	enum owl_dss_state	state;
	struct mutex		lock;

	/*
	 * resources used by EDP controller
	 */
	struct platform_device	*pdev;

	void __iomem		*base;
	void __iomem		*cmu_edpclk_reg;

	struct reset_control	*rst;

	struct clk		*edp_clk;
	struct clk		*edp_pll_clk;
	struct clk		*edp_link_clk;

	struct clk		*edp_clk_parent;
};

#define edpc_writel(edpc, index, val)	writel((val), edpc->base + (index))
#define edpc_readl(edpc, index)		readl(edpc->base + (index))

static void edpc_clk_init(struct edpc_data *edpc)
{
	u32 edp_pll_rate = 810000000;

	/* edp_clk */
	clk_set_parent(edpc->edp_clk, edpc->edp_clk_parent);
	clk_set_rate(edpc->edp_clk, edpc->configs.pclk_rate);

	/*
	 * 0, 1.62Gbps--810M
	 * 1, 2.7Gbps--1350M
	 * 2, 5.4Gbps--2700M
	 */
	if (edpc->configs.link_rate == 0)
		edp_pll_rate = 810000000;
	else if (edpc->configs.link_rate == 1)
		edp_pll_rate = 1350000000;
	else if (edpc->configs.link_rate == 2)
		edp_pll_rate = 2700000000;

	clk_set_rate(edpc->edp_pll_clk, edp_pll_rate);
}

static void edpc_clk_enable(struct edpc_data *edpc)
{
	dev_dbg(&edpc->pdev->dev, "%s\n", __func__);

	clk_prepare_enable(edpc->edp_pll_clk);

	clk_prepare_enable(edpc->edp_clk);
	clk_prepare_enable(edpc->edp_link_clk);
}

static void edpc_clk_disable(struct edpc_data *edpc)
{
	dev_dbg(&edpc->pdev->dev, "%s\n", __func__);

	clk_disable_unprepare(edpc->edp_link_clk);
	clk_disable_unprepare(edpc->edp_clk);

	clk_disable_unprepare(edpc->edp_pll_clk);
}

static void edpc_reset(struct edpc_data *edpc)
{
	if (IS_ERR(edpc->rst))
		return;

	reset_control_assert(edpc->rst);
	mdelay(5);
	reset_control_deassert(edpc->rst);
}

static void edpc_set_size(struct edpc_data *edpc,
			  uint16_t width, uint16_t height)
{
	u32 val;

	BUG_ON((width > (1 << 14)) || (height > (1 << 14)));

	val = REG_VAL(width, 14, 0);
	edpc_writel(edpc, EDP_MSTREAM_HRES, val);

	val = REG_VAL(height, 14, 0);
	edpc_writel(edpc, EDP_MSTREAM_VRES, val);
}

static void edpc_set_mode(struct edpc_data *edpc,
			  uint16_t width, uint16_t height,
			  uint16_t hbp, uint16_t hfp,
			  uint16_t hsw, uint16_t vbp,
			  uint16_t vfp, uint16_t vsw)
{
	BUG_ON((hbp > (1 << 15)) || (hfp > (1 << 15)) || (hsw > (1 << 15)));

	BUG_ON((vbp > (1 << 15)) || (vfp > (1 << 15)) || (vsw > (1 << 15)));

	edpc_writel(edpc, EDP_MSTREAM_HSWIDTH, hsw);
	edpc_writel(edpc, EDP_MSTREAM_HSTART, hbp + hsw);
	edpc_writel(edpc, EDP_MSTREAM_HTOTAL, hsw + hbp + width + hfp);

	edpc_writel(edpc, EDP_MSTREAM_VSWIDTH, vsw);
	edpc_writel(edpc, EDP_MSTREAM_VSTART, vbp + vsw);
	edpc_writel(edpc, EDP_MSTREAM_VTOTAL, vsw + vbp + height + vfp);

	/* others, TODO */
	edpc_writel(edpc, EDP_MSTREAM_POLARITY,
		    edpc->configs.mstream_polarity);

	/* 64byte each micro packet */
	edpc_writel(edpc, EDP_MTRANSFER_UNIT, 64);

	edpc_writel(edpc, EDP_N_VID, 0x8000);
	edpc_writel(edpc, EDP_USER_DATA_COUNT, (width * 18 / 16 - 1));
	edpc_writel(edpc, EDP_USER_SYNC_POLARITY,
		    edpc->configs.user_sync_polarity);
}

static void edpc_set_default_color(struct edpc_data *edpc, uint32_t color)
{
	edpc_writel(edpc, EDP_RGB_COLOR, color);
}

static void edpc_set_single_format(struct edpc_data *edpc, uint8_t format)
{
	u32 val;

	val = edpc_readl(edpc, EDP_MSTREAM_MISC0);
	val = REG_SET_VAL(val, format, 2, 1);
	edpc_writel(edpc, EDP_MSTREAM_MISC0, val);
}

static void edpc_set_data_width(struct edpc_data *edpc)
{
	u8 bit_depth;
	u32 val;

	switch (PANEL_BPP(edpc->ctrl.panel)) {
	case 18:
		bit_depth = 0;
		break;

	case 24:
		bit_depth = 1;
		break;

	case 30:
		bit_depth = 2;
		break;

	case 36:
		bit_depth = 3;
		break;

	case 48:
		bit_depth = 4;
		break;

	default:
		bit_depth = 0;
		break;
	}

	val = edpc_readl(edpc, EDP_MSTREAM_MISC0);
	val = REG_SET_VAL(val, bit_depth, 7, 5);
	edpc_writel(edpc, EDP_MSTREAM_MISC0, val);
}

static void edpc_set_preline(struct edpc_data *edpc)
{
	int preline;
	u32 val;

	preline = owl_panel_calc_prelines(edpc->ctrl.panel);
	preline = (preline < 0 ? 0 : preline);
	preline = (preline > 31 ? 31 : preline);

	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, preline, 8, 4);
	edpc_writel(edpc, EDP_RGB_CTL, val);
}

static void edpc_set_single_from(struct edpc_data *edpc, uint8_t single)
{
	u32 val;

	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, single, 1, 1);
	edpc_writel(edpc, EDP_RGB_CTL, val);
}

static void edpc_phy_config(struct edpc_data *edpc)
{
	edpc_writel(edpc, EDP_PHY_PREEM_L0, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L1, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L2, 0);
	edpc_writel(edpc, EDP_PHY_PREEM_L3, 0);

	edpc_writel(edpc, EDP_PHY_VSW_L0, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L1, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L2, 0);
	edpc_writel(edpc, EDP_PHY_VSW_L3, 0);

	/* set the aux channel voltage swing 400mV */
	edpc_writel(edpc, EDP_PHY_VSW_AUX, 0);

	/* set no Mirror,polarity not changed */
	edpc_writel(edpc, EDP_PHY_CTRL, 0);
}

static void edpc_tx_init(struct edpc_data *edpc)
{
	edpc_writel(edpc, EDP_CORE_TX_EN, 0x00);
	edpc_writel(edpc, EDP_PHY_PWR_DOWN, 0x00);
	edpc_writel(edpc, EDP_PHY_RESET, 0x00);
	mdelay(1);	/* wait for complete */
}

static void edpc_link_config(struct edpc_data *edpc)
{
	/* set aux clock 1MHz */
	edpc_writel(edpc, EDP_AUX_CLK_DIV, 24);

	edpc_writel(edpc, EDP_LNK_LANE_COUNT, edpc->configs.lane_count);

	/* enable enhanced frame */
	edpc_writel(edpc, EDP_LNK_ENHANCED, 0x01);

	/* eDP enable, use only for embedded application */
	edpc_writel(edpc, EDP_LNK_SCR_RST, 0x00);

	/* enable internal scramble */
	edpc_writel(edpc, EDP_LNK_SCR_CTRL, 0x00);
}

static void edpc_display_init_edp(struct edpc_data *edpc)
{
	struct owl_videomode *mode = &edpc->ctrl.panel->mode;

	edpc_set_size(edpc, mode->xres, mode->yres);
	edpc_set_mode(edpc, mode->xres, mode->yres, mode->hbp, mode->hfp,
		      mode->hsw, mode->vbp, mode->vfp, mode->vsw);

	edpc_set_default_color(edpc, 0xff);
	edpc_set_single_format(edpc, 0);
	edpc_set_data_width(edpc);

	edpc_set_preline(edpc);
	edpc_set_single_from(edpc, 0);	/* 0 DE, 1 default color */
	edpc_phy_config(edpc);
	edpc_tx_init(edpc);

	edpc_link_config(edpc);
}

static void edpc_single_enable(struct edpc_data *edpc, bool enable)
{
	uint32_t val;

	/* start calibrate */
	val = edpc_readl(edpc, EDP_PHY_CAL_CTRL);
	val = REG_SET_VAL(val, enable, 8, 8);
	edpc_writel(edpc, EDP_PHY_CAL_CTRL, val);

	do {
		val = edpc_readl(edpc, EDP_PHY_CAL_CTRL);
	} while (val & 0x100);

	/* enable transmit */
	edpc_writel(edpc, EDP_CORE_TX_EN, enable);

	/* phy ctl */
	val = ((((1 << edpc->configs.lane_count) - 1) & 0xf) << 9);
	val |= (edpc->configs.lane_polarity << 4);
	val |= (edpc->configs.lane_mirror << 15);
	edpc_writel(edpc, EDP_PHY_CTRL, val);

	edpc_writel(edpc, EDP_CORE_FSCR_RST, 1);		/* TODO */

	/* Enable the main stream */
	val = edpc_readl(edpc, EDP_CORE_MSTREAM_EN);
	val = REG_SET_VAL(val, enable, 0, 0);
	edpc_writel(edpc, EDP_CORE_MSTREAM_EN, val);

	/* enable RGB interface	*/
	val = edpc_readl(edpc, EDP_RGB_CTL);
	val = REG_SET_VAL(val, enable, 0, 0);
	edpc_writel(edpc, EDP_RGB_CTL, val);
}

static bool edpc_is_enabled(struct edpc_data *edpc)
{
	bool enabled = false;

	enabled = ((edpc_readl(edpc, EDP_RGB_CTL) & 0x1) == 0x1);

	dev_info(&edpc->pdev->dev, "%s, %d\n", __func__, enabled);
	return enabled;
}

static int owl_edpc_enable(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&edpc->pdev->dev, "%s\n", __func__);

	mutex_lock(&edpc->lock);

	if (edpc->state == OWL_DSS_STATE_ON)
		goto ok_out;

	edpc_clk_enable(edpc);

	edpc_display_init_edp(edpc);

	edpc_single_enable(edpc, true);

	edpc->state = OWL_DSS_STATE_ON;

ok_out:
	mutex_unlock(&edpc->lock);
	return 0;
}

static int owl_edpc_disable(struct owl_display_ctrl *ctrl)
{
	struct edpc_data *edpc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&edpc->pdev->dev, "%s\n", __func__);

	mutex_lock(&edpc->lock);

	if (edpc->state == OWL_DSS_STATE_OFF)
		goto ok_out;

	edpc_single_enable(edpc, false);

	edpc_clk_disable(edpc);

	edpc->state = OWL_DSS_STATE_OFF;

ok_out:
	mutex_unlock(&edpc->lock);
	return 0;
}

static struct owl_display_ctrl_ops owl_edp_ctrl_ops = {
	.enable = owl_edpc_enable,
	.disable = owl_edpc_disable,
};

/*============================================================================
 *			platform driver
 *==========================================================================*/

static int edpc_parse_configs(struct edpc_data *edpc)
{
	struct device *dev = &edpc->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *entry;

	struct edpc_config *configs = &edpc->configs;

	entry = of_parse_phandle(of_node, "panel_configs", 0);
	if (!entry) {
		dev_err(dev, "no etry for 'panel_configs'\n");
		return -EINVAL;
	}
	dev_dbg(dev, "entry name: %s\n", entry->name);

	#define OF_READ_U32(name, p) of_property_read_u32(entry, (name), (p))

	if (OF_READ_U32("link_rate", &configs->link_rate))
		return -EINVAL;

	if (OF_READ_U32("lane_count", &configs->lane_count))
		return -EINVAL;
	if (OF_READ_U32("lane_polarity", &configs->lane_polarity))
		return -EINVAL;
	if (OF_READ_U32("lane_mirror", &configs->lane_mirror))
		return -EINVAL;

	if (OF_READ_U32("mstream_polarity", &configs->mstream_polarity))
		return -EINVAL;
	if (OF_READ_U32("user_sync_polarity", &configs->user_sync_polarity))
		return -EINVAL;

	if (OF_READ_U32("pclk_parent", &configs->pclk_parent))
		return -EINVAL;
	if (OF_READ_U32("pclk_rate", &configs->pclk_rate))
		return -EINVAL;

	dev_dbg(dev, "%s:\n", __func__);
	dev_dbg(dev, "link_rate %d\n", configs->link_rate);
	dev_dbg(dev, "lane_count %d, lane_polarity %x, lane_mirror = %d\n",
		configs->link_rate, configs->lane_count, configs->lane_mirror);
	dev_dbg(dev, "mstream_polarity %x, user_sync_polarity = %x\n",
		configs->mstream_polarity, configs->user_sync_polarity);
	dev_dbg(dev, "pclk_parent %d pclk_rate %d\n",
		configs->pclk_parent, configs->pclk_rate);

	#undef OF_READ_U32

	return 0;
}

static int edpc_get_resources(struct edpc_data *edpc)
{
	struct platform_device *pdev = edpc->pdev;
	struct device *dev = &pdev->dev;

	struct resource *res;

	dev_dbg(dev, "%s\n", __func__);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get regs\n");
		return PTR_ERR(res);
	}
	edpc->base = devm_ioremap_resource(dev, res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_edpclk");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get cmu_edpclk\n");
		return PTR_ERR(res);
	}
	edpc->cmu_edpclk_reg = devm_ioremap_resource(dev, res);

	dev_dbg(dev, "base: 0x%p, edpclk: 0x%p\n", edpc->base,
		edpc->cmu_edpclk_reg);

	edpc->edp_clk = devm_clk_get(dev, "edp");
	edpc->edp_pll_clk = devm_clk_get(dev, "edp_pll");
	edpc->edp_link_clk = devm_clk_get(dev, "edp_link");
	if (IS_ERR(edpc->edp_clk) || IS_ERR(edpc->edp_pll_clk) ||
	    IS_ERR(edpc->edp_link_clk)) {
		dev_err(dev, "can't get clock\n");
		return -EINVAL;
	}

	if (edpc->configs.pclk_parent == 0)
		edpc->edp_clk_parent = devm_clk_get(&pdev->dev, "assist_pll");
	else
		edpc->edp_clk_parent = devm_clk_get(&pdev->dev, "display_pll");

	if (IS_ERR(edpc->edp_clk_parent)) {
		dev_err(dev, "can't get parent clk\n");
		return PTR_ERR(edpc->edp_clk_parent);
	}

	edpc->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(edpc->rst)) {
		dev_err(dev, "can't get reset\n");
		return PTR_ERR(edpc->rst);
	}

	return 0;
}

static const struct of_device_id owl_edpc_match[] = {
	{
		.compatible	= "actions,s900-edp",
	},
};

static int __init owl_edpc_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct edpc_data *edpc;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(owl_edpc_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	edpc = devm_kzalloc(dev, sizeof(*edpc), GFP_KERNEL);
	if (edpc == NULL) {
		dev_err(dev, "No Mem for edpc\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, edpc);
	edpc->pdev = pdev;

	mutex_init(&edpc->lock);

	if (edpc_parse_configs(edpc) < 0) {
		dev_err(dev, "parse configs failed\n");
		return -EINVAL;
	}

	ret = edpc_get_resources(edpc);
	if (ret < 0) {
		dev_err(dev, "get resources failed: %d\n", ret);
		return ret;
	}

	edpc->ctrl.type = OWL_DISPLAY_TYPE_EDP;
	edpc->ctrl.ops = &owl_edp_ctrl_ops;

	owl_ctrl_set_drvdata(&edpc->ctrl, edpc);

	ret = owl_ctrl_register(&edpc->ctrl);
	if (ret < 0) {
		dev_err(dev, "register edp ctrl failed: %d\n", ret);
		return ret;
	}

	edpc_clk_init(edpc);

	/* enable EDP clock at booting, or 'edpc_is_enabled' will die!! */
	edpc_clk_enable(edpc);

	if (edpc_is_enabled(edpc)) {
		edpc->state = OWL_DSS_STATE_ON;
	} else {
		edpc->state = OWL_DSS_STATE_OFF;
		edpc_clk_disable(edpc);
	}

	dev_info(dev, "%s, OK\n", __func__);
	return 0;
}

static int __exit owl_edpc_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);

	return 0;
}

static struct platform_driver owl_edpc_driver = {
	.probe			= owl_edpc_probe,
	.remove			= __exit_p(owl_edpc_remove),
	.driver = {
		.name		= "owl-edpc",
		.owner		= THIS_MODULE,
		.of_match_table	= owl_edpc_match,
	},
};

int __init owl_edpc_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&owl_edpc_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

void __exit owl_edpc_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&owl_edpc_driver);
}

module_init(owl_edpc_init);
module_exit(owl_edpc_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL EDP Controller Driver");
MODULE_LICENSE("GPL v2");
