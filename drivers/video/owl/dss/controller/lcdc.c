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
 *	2015/11/30: Created by Lipeng.
 */
#define DEBUGX
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

#include "lcdc.h"

#define CMU_LCDCLK		(0x0024)

enum lcd_port_type {
	LCD_PORT_TYPE_RGB = 0,
	LCD_PORT_TYPE_CPU,
	LCD_PORT_TYPE_LVDS,
	LCD_PORT_TYPE_LCD,
};

struct lcdc_config {
	uint32_t			port_type;

	uint32_t			vsync_inversion;
	uint32_t			hsync_inversion;
	uint32_t			dclk_inversion;
	uint32_t			lde_inversion;

	/* properties for lvds port */
	uint32_t			lvds_format; 		/* 0: 18-bit; 1: 24-bit */
	uint32_t			lvds_channel; 		/* 0: single channel; 1: dual channel */
	uint32_t			lvds_bit_mapping; 	/* 0: NS Mode; 1: JEIDA Mode */
	uint32_t			lvds_ch_swap;  		/* 0: No Swap; 1: Odd/Even Swap */
	uint32_t			lvds_mirror;  		/* 0: normal; 1: mirror */

	uint32_t			pclk_parent;
	uint32_t			pclk_rate;
	/* properties for cpu port */
	uint32_t			cpu_format;  		/* 0: 16 bit RGB 565 1 transfer; 
								 * 1: 18 bit RGB 666 1 transfer
								 * 2:  8 bit RGB 565 2 transfer
								 * 3:  9 bit RGB 666 2 transfer
								 * 4:  8 bit RGB 888 3 transfer
								 * 5:  6 bit RGB 666 3 transfer
								 */
};

struct lcdc_data {
	struct owl_display_ctrl	ctrl;
	struct lcdc_config	configs;

	enum owl_dss_state	state;
	struct mutex		lock;

	/*
	 * resource used by LCD controller
	 */
	struct platform_device	*pdev;

	void __iomem		*base;
	void __iomem		*cmu_base;

	struct reset_control	*rst;

	struct clk              *lcd_clk;
	struct clk              *lcd_clk_parent;
};

#define lcdc_writel(lcdc, index, val)	writel((val), lcdc->base + (index))
#define lcdc_readl(lcdc, index)		readl(lcdc->base + (index))

static void lcdc_clk_enable(struct lcdc_data *lcdc)
{
	dev_dbg(&lcdc->pdev->dev, "%s\n", __func__);

	clk_set_parent(lcdc->lcd_clk, lcdc->lcd_clk_parent);
	clk_set_rate(lcdc->lcd_clk, lcdc->configs.pclk_rate);
	clk_prepare_enable(lcdc->lcd_clk);
}

static void lcdc_clk_disable(struct lcdc_data *lcdc)
{
	dev_dbg(&lcdc->pdev->dev, "%s\n", __func__);

	clk_disable_unprepare(lcdc->lcd_clk);
}

static void lcdc_reset_assert(struct lcdc_data *lcdc)
{
	if (!IS_ERR(lcdc->rst))
		reset_control_assert(lcdc->rst);
}

static void lcdc_reset_deassert(struct lcdc_data *lcdc)
{
	if (!IS_ERR(lcdc->rst))
		reset_control_deassert(lcdc->rst);
}
static void lcdc_set_interface_type(struct lcdc_data *lcdc, int interface_type)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_CTL);
	val = REG_SET_VAL(val, interface_type, 31, 31);
	lcdc_writel(lcdc, LCDC_CTL, val);
}

static void lcdc_set_cpu_format(struct lcdc_data *lcdc)
{
	uint32_t val;
	
	val = lcdc_readl(lcdc, LCDC_CPU_CTL);
	val = REG_SET_VAL(val, lcdc->configs.cpu_format, 6, 4);
	lcdc_writel(lcdc, LCDC_CPU_CTL, val);
}

static void lcdc_set_size(struct lcdc_data *lcdc,
			  uint16_t width, uint16_t height)
{
	uint32_t val;

	BUG_ON((width > (1 << 12)) || (height > (1 << 12)));

	val = REG_VAL(height - 1, 27, 16) | REG_VAL(width - 1, 11, 0);

	lcdc_writel(lcdc, LCDC_SIZE, val);
}

static void lcdc_set_mode(struct lcdc_data *lcdc,
			  uint16_t hbp, uint16_t hfp, uint16_t hsw,
			  uint16_t vbp , uint16_t vfp, uint16_t vsw)
{
	uint32_t val;

	BUG_ON((hbp > (1 << 9)) || (hfp > (1 << 9)) || (hsw > (1 << 9)));
	BUG_ON((vbp > (1 << 9)) || (vfp > (1 << 9)) || (vsw > (1 << 9)));

	val = REG_VAL(hsw - 1, 29, 20) | REG_VAL(hfp - 1, 19, 10)
		| REG_VAL(hbp - 1, 9, 0);
	lcdc_writel(lcdc, LCDC_TIM1, val);
	val = REG_VAL(vsw - 1, 29, 20) | REG_VAL(vfp - 1, 19, 10)
		| REG_VAL(vbp - 1, 9, 0);
	lcdc_writel(lcdc, LCDC_TIM2, val);
}

static void lcdc_set_default_color(struct lcdc_data *lcdc, uint32_t color)
{
	lcdc_writel(lcdc, LCDC_COLOR, color);
}

static void lcdc_set_preline(struct lcdc_data *lcdc)
{
	int preline;
	uint32_t val;

	preline = owl_panel_get_preline_num(lcdc->ctrl.panel);
	preline -= 1;
	preline = (preline < 0 ? 0 : preline);
	preline = (preline > 31 ? 31 : preline);

	val = lcdc_readl(lcdc, LCDC_TIM0);
	val = REG_SET_VAL(0, preline, 12, 8);
	val = REG_SET_VAL(val, 1, 13, 13);
	lcdc_writel(lcdc, LCDC_TIM0, val);
}

static void lcdc_set_vsync_inv(struct lcdc_data *lcdc, uint8_t vsync_inv)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_TIM0);
	val = REG_SET_VAL(val, vsync_inv, 7, 7);
	lcdc_writel(lcdc, LCDC_TIM0, val);
}

static void lcdc_set_hsync_inv(struct lcdc_data *lcdc, uint8_t hsync_inv)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_TIM0);
	val = REG_SET_VAL(val, hsync_inv, 6, 6);
	lcdc_writel(lcdc, LCDC_TIM0, val);
}

static void lcdc_set_dclk_inv(struct lcdc_data *lcdc, uint8_t dclk_inv)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_TIM0);
	val = REG_SET_VAL(val, dclk_inv, 5, 5);
	lcdc_writel(lcdc, LCDC_TIM0, val);
}

static void lcdc_set_lde_inv(struct lcdc_data *lcdc, uint8_t led_inv)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_TIM0);
	val = REG_SET_VAL(val, led_inv, 4, 4);
	lcdc_writel(lcdc, LCDC_TIM0, val);
}

static void lcdc_set_rb_swap(struct lcdc_data *lcdc, bool rb_swap)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_CTL);
	val = REG_SET_VAL(val, rb_swap, 1, 1);
	lcdc_writel(lcdc, LCDC_CTL, val);
}

/* set lcd format according to dssdev's bpp */
static void lcdc_set_single_format(struct lcdc_data *lcdc)
{
	uint32_t val;
	uint32_t format = 0;

	if (PANEL_BPP(lcdc->ctrl.panel) == 24)
		format = 0;
	else if (PANEL_BPP(lcdc->ctrl.panel) == 18)
		format = 1;
	else if (PANEL_BPP(lcdc->ctrl.panel) == 16)
		format = 2;
	else if (PANEL_BPP(lcdc->ctrl.panel) == 8)
		format = 3;

	val = lcdc_readl(lcdc, LCDC_CTL);
	val = REG_SET_VAL(val, format, 18, 16);
	lcdc_writel(lcdc, LCDC_CTL, val);
}

static void lcdc_set_single_from(struct lcdc_data *lcdc, uint8_t single)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_CTL);
	val = REG_SET_VAL(val, single, 7, 6);

	lcdc_writel(lcdc, LCDC_CTL, val);
}

static void lcdc_single_enable(struct lcdc_data *lcdc, bool enable)
{
	uint32_t val;

	val = lcdc_readl(lcdc, LCDC_CTL);
	val = REG_SET_VAL(val, enable, 0, 0);

	lcdc_writel(lcdc, LCDC_CTL, val);
}

/*****************************************************************************              
* Description:
*	set IC as commond mode; but rs pin can be change;
*	IC is commond mode; 
*	RS is 0: cmd mode, 用于写寄存器的索引index
*	RS is 1: 一般是lcd的val模式, 用于写寄存器的内容value
*******************************************************************************/
void lcd_send_cmd(struct lcdc_data *lcdc,
			unsigned int rs, unsigned int cmd)
{
	unsigned int tmp = 0;
	dev_dbg(&lcdc->pdev->dev, "%s, rs %d, cmd 0x%x\n", __func__, rs, cmd);
	
	lcdc_writel(lcdc, LCDC_CPU_CMD, cmd);

	tmp = lcdc_readl(lcdc, LCDC_CPU_CTL);
	
	if (0 == rs) {
		tmp &= ~(0x01<<10);     /* RS low */
	} else {
		tmp |= (0x01<<10);      /* RS high */
	}
	
	tmp &= ~(0x03<<8);          /* write cmd */
	tmp |= (0x01<<0);           /* start transmission */
	
	lcdc_writel(lcdc, LCDC_CPU_CTL, tmp);
	udelay(1);
}

static void lcdc_cpu_port_refresh_frame(struct lcdc_data *lcdc)
{
	uint32_t val;
	int times = 3000;/* Recommended 30ms */
	dev_dbg(&lcdc->pdev->dev, "%s\n", __func__);
	
	val = lcdc_readl(lcdc, LCDC_CPU_CTL);
	val = REG_SET_VAL(val, 0x1, 10, 10); /* RS high for transfering RGB data  */
	val = REG_SET_VAL(val, 0x2, 9, 8); /* RGB data transfor select */
	val = REG_SET_VAL(val, 0x1, 0, 0); /* start transmission */
	lcdc_writel(lcdc, LCDC_CPU_CTL, val);

	/* Start data transfer until DE signal is reached, and bit is cleaned */
	do {
		mdelay(1);
		val = (0x01 & (lcdc_readl(lcdc, LCDC_CPU_CTL)));
		times--;
	} while((val != 0) && (times > 0));
	
	if (times <= 0)
		dev_err(&lcdc->pdev->dev,
			"err!! lcd wait LCDC_CPU_CTL bit0, to transmit timeout\n");
}

static void lcdc_lvds_port_enable(struct lcdc_data *lcdc, bool enable)
{
	uint32_t val;

	if (enable) {
		val = lcdc_readl(lcdc, LCDC_LVDS_ALG_CTL0);
		val = REG_SET_VAL(val, 0, 30, 31);
		val = REG_SET_VAL(val, 0, 4, 5);
		lcdc_writel(lcdc, LCDC_LVDS_ALG_CTL0, 0xc141a030);

		/* FIXME */
		lcdc_writel(lcdc, LCDC_LVDS_CTL, 0x000a9500);

		val = lcdc_readl(lcdc, LCDC_LVDS_CTL);
		val = REG_SET_VAL(val, lcdc->configs.lvds_mirror, 6, 6);
		val = REG_SET_VAL(val, lcdc->configs.lvds_ch_swap, 5, 5);
		val = REG_SET_VAL(val, lcdc->configs.lvds_bit_mapping, 4, 3);
		val = REG_SET_VAL(val, lcdc->configs.lvds_channel, 2, 2);
		val = REG_SET_VAL(val, lcdc->configs.lvds_format, 1, 1);
		lcdc_writel(lcdc, LCDC_LVDS_CTL, val);

		val = lcdc_readl(lcdc, LCDC_LVDS_CTL);
		val = REG_SET_VAL(val, enable, 0, 0);
		lcdc_writel(lcdc, LCDC_LVDS_CTL, val);
	} else {
		val = lcdc_readl(lcdc, LCDC_LVDS_ALG_CTL0);
		val = REG_SET_VAL(val, 0, 30, 31);
		val = REG_SET_VAL(val, 0, 4, 5);
		lcdc_writel(lcdc, LCDC_LVDS_ALG_CTL0, val);

		val = lcdc_readl(lcdc, LCDC_LVDS_CTL);
		val = REG_SET_VAL(val, enable, 0, 0);
		lcdc_writel(lcdc, LCDC_LVDS_CTL, val);
	}
}

static void lcdc_display_init_lcdc(struct lcdc_data *lcdc)
{
	struct owl_videomode *mode = &lcdc->ctrl.panel->mode;

	dev_dbg(&lcdc->pdev->dev, "%s\n", __func__);
	if (lcdc->configs.port_type == LCD_PORT_TYPE_CPU)
		lcdc_set_cpu_format(lcdc);

  	lcdc_set_interface_type(lcdc, lcdc->configs.port_type);
	lcdc_set_size(lcdc, mode->xres, mode->yres);
	lcdc_set_mode(lcdc, mode->hbp, mode->hfp, mode->hsw,
		      mode->vbp, mode->vfp, mode->vsw);

	lcdc_set_default_color(lcdc, 0xff);
	lcdc_set_single_format(lcdc);

	lcdc_set_preline(lcdc);
	lcdc_set_single_from(lcdc, 0x02);

	lcdc_set_rb_swap(lcdc, 0);
	lcdc_set_vsync_inv(lcdc, lcdc->configs.vsync_inversion);
	lcdc_set_hsync_inv(lcdc, lcdc->configs.hsync_inversion);
	lcdc_set_dclk_inv(lcdc, lcdc->configs.dclk_inversion);
	lcdc_set_lde_inv(lcdc, lcdc->configs.lde_inversion);

}

static bool lcdc_is_enabled(struct lcdc_data *lcdc)
{
	bool enabled = lcdc_readl(lcdc, LCDC_CTL) & 0x1;

	dev_info(&lcdc->pdev->dev, "%s, %d\n", __func__, enabled);
	return enabled;
}

static int owl_lcdc_enable(struct owl_display_ctrl *ctrl)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&lcdc->pdev->dev, "%s\n", __func__);

	mutex_lock(&lcdc->lock);

	if (lcdc->state != OWL_DSS_STATE_ON) {
		lcdc_reset_assert(lcdc);

		lcdc_clk_enable(lcdc);
		mdelay(5);

		lcdc_reset_deassert(lcdc);
		mdelay(1);

		lcdc_display_init_lcdc(lcdc);

		if (lcdc->configs.port_type == LCD_PORT_TYPE_LVDS)
			lcdc_lvds_port_enable(lcdc, true);

		lcdc_single_enable(lcdc, true);

		lcdc->state = OWL_DSS_STATE_ON;
	}

	mutex_unlock(&lcdc->lock);

	return 0;
}

static int owl_lcdc_disable(struct owl_display_ctrl *ctrl)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&lcdc->pdev->dev, "%s\n", __func__);

	mutex_lock(&lcdc->lock);

	if (lcdc->state == OWL_DSS_STATE_ON) {
		if (lcdc->configs.port_type == LCD_PORT_TYPE_LVDS)
			lcdc_lvds_port_enable(lcdc, false);

		lcdc_single_enable(lcdc, false);

		lcdc_clk_disable(lcdc);
		lcdc_reset_assert(lcdc);

		lcdc->state = OWL_DSS_STATE_OFF;
	}

	mutex_unlock(&lcdc->lock);

	return 0;
}
int owl_lcdc_aux_read(struct owl_display_ctrl *ctrl, char *buf, int count)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);

	return 0;
}

/*
 * command buffer format:
 * 	buffer 3 ~ (MIPI_MAX_PARS - 1) ---> parameters
 * 	buffer 2---> address
 * 	buffer 1---> number of parameters
 * 	buffer 0---> cmd delay
 *
 */
int owl_lcdc_aux_write(struct owl_display_ctrl *ctrl, const char *buf, int count)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);
	uint32_t parameters_num, address, cmd_delay;
	uint8_t *buffer = buf;
	int i;

	if (buffer != NULL && count > 0) {
		/* get command data type and cmd_delay */
		cmd_delay = buffer[0];
		parameters_num = buffer[1];
		address = buffer[2];

		dev_dbg(&lcdc->pdev->dev, "%s, delay %d parameters_num:0x%x address:0x%x\n",
				__func__, cmd_delay, parameters_num, address);
		lcd_send_cmd(lcdc, 0, address);

		for (i = 0; i < parameters_num; i++)
			lcd_send_cmd(lcdc, 1, buffer[3 + i]);

		if (cmd_delay > 0)
			mdelay(cmd_delay);
	}

	return 0;
}

int owl_lcdc_refresh_frame(struct owl_display_ctrl *ctrl)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);
	
	dev_dbg(&lcdc->pdev->dev, "%s\n", __func__);

	if (lcdc->configs.port_type != LCD_PORT_TYPE_CPU)
		return 0;

	lcdc_cpu_port_refresh_frame(lcdc);

	return 0;
}

static int owl_lcdc_power_on(struct owl_display_ctrl *ctrl)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&lcdc->pdev->dev, "%s\n", __func__);

}

static int owl_lcdc_power_off(struct owl_display_ctrl *ctrl)
{
	struct lcdc_data *lcdc = owl_ctrl_get_drvdata(ctrl);

	dev_info(&lcdc->pdev->dev, "%s\n", __func__);

}


static struct owl_display_ctrl_ops owl_lcd_ctrl_ops = {
	.enable = owl_lcdc_enable,
	.disable = owl_lcdc_disable,
	
	.power_on = owl_lcdc_power_on,
	.power_off = owl_lcdc_power_off,

	.aux_read = owl_lcdc_aux_read,
	.aux_write = owl_lcdc_aux_write,

	.refresh_frame = owl_lcdc_refresh_frame,
};

static int lcdc_parse_configs(struct lcdc_data *lcdc)
{
	struct device *dev = &lcdc->pdev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *entry;

	struct lcdc_config *configs = &lcdc->configs;

	entry = of_parse_phandle(of_node, "panel_configs", 0);
	if (!entry) {
		dev_err(dev, "no etry for 'panel_configs'\n");
		return -EINVAL;
	}
	dev_dbg(dev, "entry name: %s\n", entry->name);

	#define OF_READ_U32(name, p) of_property_read_u32(entry, (name), (p))

	if (OF_READ_U32("port_type", &configs->port_type))
		return -EINVAL;

	if (OF_READ_U32("vsync_inversion", &configs->vsync_inversion))
		return -EINVAL;
	if (OF_READ_U32("hsync_inversion", &configs->hsync_inversion))
		return -EINVAL;
	if (OF_READ_U32("dclk_inversion", &configs->dclk_inversion))
		return -EINVAL;
	if (OF_READ_U32("lde_inversion", &configs->lde_inversion))
		return -EINVAL;

	/* parse lvds port propertis */
	if (OF_READ_U32("lvds_format", &configs->lvds_format))
		configs->lvds_format = 0;
	if (OF_READ_U32("lvds_channel", &configs->lvds_channel))
		configs->lvds_channel = 0;
	if (OF_READ_U32("lvds_bit_mapping", &configs->lvds_bit_mapping))
		configs->lvds_bit_mapping = 0;
	if (OF_READ_U32("lvds_ch_swap", &configs->lvds_ch_swap))
		configs->lvds_ch_swap = 0;
	if (OF_READ_U32("lvds_mirror", &configs->lvds_mirror))
		configs->lvds_mirror = 0;

	if (OF_READ_U32("pclk_parent", &configs->pclk_parent))
		return -EINVAL;
	if (OF_READ_U32("pclk_rate", &configs->pclk_rate))
		return -EINVAL;

	dev_dbg(dev, "%s:\n", __func__);
	
	/* cpu port properties */
	if (OF_READ_U32("cpu_format", &configs->cpu_format))
		configs->cpu_format = 2; /* TODO */

	dev_dbg(dev, "%s:\n", __func__);
	dev_dbg(dev, "lvds_format %d, lvds_channel %d, lvds_bit_mapping %d\n",
			configs->lvds_format, configs->lvds_channel, configs->lvds_bit_mapping);

	dev_dbg(dev, "lvds_ch_swap %d, lvds_mirror\n", configs->lvds_ch_swap, configs->lvds_mirror);
	dev_dbg(dev, "port_type %d\n", configs->port_type);
	dev_dbg(dev, "vsync_inversion %d, hsync_inversion %d\n",
		configs->vsync_inversion, configs->hsync_inversion);
	dev_dbg(dev, "dclk_inversion %d, lde_inversion %d\n",
		configs->dclk_inversion, configs->lde_inversion);
	dev_dbg(dev, "pclk_parent %d pclk_rate %d\n",
		configs->pclk_parent, configs->pclk_rate);

	#undef OF_READ_U32

	return 0;
}

static int lcdc_get_resources(struct lcdc_data *lcdc)
{
	struct platform_device *pdev = lcdc->pdev;
	struct device *dev = &pdev->dev;

	struct resource *res;

	dev_dbg(dev, "%s\n", __func__);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (IS_ERR(res)) {
		dev_err(dev, "can't get regs\n");
		return PTR_ERR(res);
	}
	lcdc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcdc->base)) {
		dev_err(dev, "map registers error\n");
		return -ENODEV;
	}
	dev_dbg(dev, "base: 0x%p\n", lcdc->base);

	lcdc->lcd_clk = devm_clk_get(dev, "lcd");
	if (IS_ERR(lcdc->lcd_clk)) {
		dev_err(dev, "can't get clock\n");
		return -EINVAL;
	}

	if (lcdc->configs.pclk_parent == 0)
		lcdc->lcd_clk_parent = devm_clk_get(&pdev->dev, "display_pll");
	else
		lcdc->lcd_clk_parent = devm_clk_get(&pdev->dev, "nand_pll");
	if (IS_ERR(lcdc->lcd_clk_parent)) {
		dev_err(dev, "can't get parent clk\n");
		return PTR_ERR(lcdc->lcd_clk_parent);
	}

	lcdc->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(lcdc->rst)) {
		dev_err(dev, "can't get reset\n");
		return PTR_ERR(lcdc->rst);
	}

	return 0;
}

static struct of_device_id owl_lcdc_match[] = {
	{
		.compatible = "actions,s900-lcd",
	},
	{
		.compatible = "actions,s700-lcd",
	},
	{
		.compatible = "actions,ats3605-lcd",
	},

	{},
};

static int owl_lcdc_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct lcdc_data		*lcdc;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(owl_lcdc_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	lcdc = devm_kzalloc(dev, sizeof(*lcdc), GFP_KERNEL);
	if (!lcdc) {
		dev_err(dev, "No Mem for lcdc\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, lcdc);
	lcdc->pdev = pdev;

	mutex_init(&lcdc->lock);

	if (lcdc_parse_configs(lcdc) < 0) {
		dev_err(dev, "parse configs failed\n");
		return -EINVAL;
	}

	ret = lcdc_get_resources(lcdc);
	if (ret < 0) {
		dev_err(dev, "get resources failed: %d\n", ret);
		return ret;
	}

	lcdc->ctrl.type = OWL_DISPLAY_TYPE_LCD;
	lcdc->ctrl.ops = &owl_lcd_ctrl_ops;

	owl_ctrl_set_drvdata(&lcdc->ctrl, lcdc);

	ret = owl_ctrl_register(&lcdc->ctrl);
	if (ret < 0) {
		dev_err(dev, "register lcd ctrl failed: %d\n", ret);
		return ret;
	}

	/* enable LCD clock at booting, or 'lcdc_is_enabled' will die!! */
	lcdc_clk_enable(lcdc);
	lcdc_reset_deassert(lcdc);
	mdelay(1);

	if (lcdc_is_enabled(lcdc)) {
		lcdc->state = OWL_DSS_STATE_ON;
	} else {
		lcdc->state = OWL_DSS_STATE_OFF;
		lcdc_clk_disable(lcdc);
		lcdc_reset_assert(lcdc);
	}

	dev_info(dev, "%s, OK\n", __func__);
	return 0;
}

static int __exit owl_lcdc_remove(struct platform_device *pdev)
{
	struct lcdc_data *lcdc = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "%s\n", __func__);

	owl_lcdc_disable(&lcdc->ctrl);

	return 0;
}

static int owl_lcdc_suspend(struct device *dev)
{
	struct lcdc_data *lcdc = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	owl_lcdc_disable(&lcdc->ctrl);
	return 0;
}

static int owl_lcdc_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	/* nothing to do, application will re-enable me later */

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(owl_lcdc_pm_ops, owl_lcdc_suspend,
			    owl_lcdc_resume, NULL);

static struct platform_driver owl_lcdc_driver = {
	.probe			= owl_lcdc_probe,
	.remove			= __exit_p(owl_lcdc_remove),
	.driver = {
		.name		= "owl-lcdc",
		.owner		= THIS_MODULE,
		.of_match_table	= owl_lcdc_match,
		.pm		= &owl_lcdc_pm_ops,
	},
};

int __init owl_lcdc_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&owl_lcdc_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

void __exit owl_lcdc_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&owl_lcdc_driver);
}

module_init(owl_lcdc_init);
module_exit(owl_lcdc_exit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL LCD Controller Driver");
MODULE_LICENSE("GPL v2");
