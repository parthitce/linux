/*
 * linux/drivers/video/owl/dss/dsi.c
 *
 * Copyright (C) 2009 Actions Corporation
 * Author: Hui Wang  <wanghui@actions-semi.com>
 *
 * Some code and ideas taken from drivers/video/owl/ driver
 * by leopard.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <video/owl_dss.h>

#include <linux/bootafinfo.h>

#include "dsihw.h"
#include "dsi.h"

/*TO DO*/
#define CMU_DSICLK		(0x0028)

#ifdef DSI_DEBUG_ENABLE
/* 0, error; 1, error+info; 2, error+info+debug */
int owl_dsi_debug = 2;
module_param_named(debug, owl_dsi_debug, int, 0644);
#endif


struct owl_dsi_config {
	uint32_t	lcd_mode;	/* 0: command mode, 1: video mode*/
	uint32_t	lane_count;	/* 0: 1, 1:1~2, 2:1~3, 3:1~4 */
	uint32_t	lane_polarity;	/* 0: normal, 1: reversed*/
	uint32_t	video_mode;	/* 0:sync mode,1:de mode,2:burst mode*/
	uint32_t	pclk_rate;	/* MHz*/
};

#define DSI_ID_S500 0
#define DSI_ID_S700 1
#define DSI_ID_S900 2

struct dsi_diffs {
	int id;
};

struct dsi_data {
	struct owl_display_ctrl	ctrl;
	struct owl_dsi_config configs;
	const struct dsi_diffs *diffs;

	bool enabled;

	struct mutex lock;
	/*
	*resources used by DSI controller
	*/
	void __iomem *base;
	void __iomem *cmu_dsiclk_reg;
	void __iomem *sps_ctl_reg;	/*TO DO*/
	struct platform_device *pdev;
	struct reset_control *rst;
	struct clk *clk;
};

inline struct dsi_data *dsihw_get_dsidrv_data(struct platform_device *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

inline void dsihw_write_reg(struct dsi_data *dsi, const u16 index, u32 val)
{
	writel(val, dsi->base + index);
}

inline u32 dsihw_read_reg(struct dsi_data *dsi, const u16 index)
{
	return readl(dsi->base + index);
}

void dsihw_dump_regs(struct dsi_data *dsi)
{
#define DUMPREG(name, r) DSIINFO("%s %08x\n", name, dsihw_read_reg(dsi, r))
	DUMPREG("DSI_CTRL	value is ", DSI_CTRL);
	DUMPREG("DSI_SIZE	value is ", DSI_SIZE);
	DUMPREG("DSI_COLOR	value is ", DSI_COLOR);
	DUMPREG("DSI_VIDEO_CFG	value is ", DSI_VIDEO_CFG);
	DUMPREG("DSI_RGBHT0	value is ", DSI_RGBHT0);
	DUMPREG("DSI_RGBHT1	value is ", DSI_RGBHT1);
	DUMPREG("DSI_RGBVT0	value is ", DSI_RGBVT0);
	DUMPREG("DSI_RGBVT1	value is ", DSI_RGBVT1);
	DUMPREG("DSI_TIMEOUT	value is ", DSI_TIMEOUT);
	DUMPREG("DSI_TR_STA	value is ", DSI_TR_STA);
	DUMPREG("DSI_INT_EN	value is ", DSI_INT_EN);
	DUMPREG("DSI_ERROR_REPORT	value is ", DSI_ERROR_REPORT);
	DUMPREG("DSI_FIFO_ODAT	value is ", DSI_FIFO_ODAT);
	DUMPREG("DSI_FIFO_IDAT	value is ", DSI_FIFO_IDAT);
	DUMPREG("DSI_IPACK	value is ", DSI_IPACK);
	DUMPREG("DSI_PACK_CFG	value is ", DSI_PACK_CFG);
	DUMPREG("DSI_PACK_HEADER	value is ", DSI_PACK_HEADER);
	DUMPREG("DSI_TX_TRIGGER	value is ", DSI_TX_TRIGGER);
	DUMPREG("DSI_RX_TRIGGER	value is ", DSI_RX_TRIGGER);
	DUMPREG("DSI_LANE_CTRL	value is ", DSI_LANE_CTRL);
	DUMPREG("DSI_LANE_STA	value is ", DSI_LANE_STA);
	DUMPREG("DSI_PHY_T0	value is ", DSI_PHY_T0);
	DUMPREG("DSI_PHY_T1	value is ", DSI_PHY_T1);
	DUMPREG("DSI_PHY_T2	value is ", DSI_PHY_T2);
	DUMPREG("DSI_APHY_DEBUG0 value is ", DSI_APHY_DEBUG0);
	DUMPREG("DSI_APHY_DEBUG1 value is ", DSI_APHY_DEBUG1);
	DUMPREG("DSI_SELF_TEST  value is ", DSI_SELF_TEST);
	DUMPREG("DSI_PIN_MAP  value is ", DSI_PIN_MAP);
	DUMPREG("DSI_PHY_CTRL  value is ", DSI_PHY_CTRL);
	DUMPREG("DSI_FT_TEST  value is ", DSI_FT_TEST);
}

/*
 * dsi may be powered on in boot stage,
 * in this case we should update frameworks' status,
 * such as gpio, clock and so on
 */
static void dsihw_update_power_status(struct dsi_data *dsi)
{
	clk_prepare_enable(dsi->clk);
}
static uint32_t dsi_get_hsclk(struct dsi_data *dsi, uint16_t frame_rate,
		uint16_t lane_num, uint16_t xres, uint16_t hsw,
		uint16_t hfp, uint16_t hbp, uint16_t vtotal)
{
	uint16_t pixel2pro = 1;
	uint32_t h_bit = 0;/*horizontal bit*/
	uint32_t h_byte = 0;/*horizontal byte*/
	uint32_t per_lane_bps = 0;

	uint32_t htotal = 0;
	uint32_t dsi_hsclk = 0;

	uint32_t bpp = PANEL_BPP(dsi->ctrl.panel);
	uint16_t v_h = frame_rate;/*frequency vertial*/

	switch (bpp) {
	case 16:
		pixel2pro = 2;
		break;

	case 18:
		pixel2pro = 3;
		break;

	case 24:
		pixel2pro = 3;
		break;
	}
	DSIDBG("pixel2pro = %d\n", pixel2pro);
	switch (dsi->configs.video_mode) {
	case 0:/*sync mode*/
		h_byte = 4 + (hsw * pixel2pro + 6)
		+ (hbp * pixel2pro + 6) + (xres * pixel2pro + 6)
		+ (hfp * pixel2pro + 6);
		break;

	case 1:/*de mode*/
		h_byte = 4 + ((hsw + hbp)*pixel2pro + 6)
		+ ((xres * pixel2pro) + 6) + (hfp * pixel2pro + 6);
		break;

	case 2:/*burst mode*/
		h_byte = 4 + ((hsw + hbp)*pixel2pro + 6)
		+ ((xres * pixel2pro) + 6) + (1000 + 6)
		+ (hfp * pixel2pro + 6);/*bllp_reg = 1000*/
		break;
	}

	h_bit = h_byte * 8;
	htotal = h_bit / lane_num;/*date per lane*/
	per_lane_bps = htotal * vtotal * v_h;
	dsi_hsclk = (per_lane_bps / 2) / 1000000;/*Double edge transport*/

	if ((dsi_hsclk > 840) || (dsi_hsclk < 120)) {
		DSIERR("get_hsclk failed !\n");
		return -1;
	}
	DSIDBG("%s dsi_hsclk = %d\n", __func__, dsi_hsclk);

	return dsi_hsclk;
}

static unsigned int dsi_clk_get_divider(unsigned int parent_rate,
					unsigned int target_rate)
{
	unsigned int divider;

	divider = parent_rate / target_rate;

	DSIDBG("%s: parent_rate %d, target_rate %d, divider %d\n",
		__func__, parent_rate, target_rate, divider);
	return divider;
}

static int dsi_set_dsi_clk(struct dsi_data *dsi)
{
	struct owl_videomode *mode = &dsi->ctrl.panel->mode;

	uint32_t hsclk_reg = 0;

	uint32_t cmu_dsiclk;
	uint32_t vtotal;
	uint16_t divider_reg = 0;

	DSIDBG("%s\n", __func__);

	vtotal = mode->yres + mode->vsw + mode->vfp + mode->vbp;
	DSIDBG("vtotal = %d\n", vtotal);

	hsclk_reg = dsi_get_hsclk(dsi, 60,
				dsi->configs.lane_count, mode->xres,
			mode->hsw, mode->hfp, mode->hbp, vtotal);

	hsclk_reg = (hsclk_reg / 6) * 6;

	divider_reg = dsi_clk_get_divider(hsclk_reg, dsi->configs.pclk_rate)-1;

	if (dsi->diffs->id == DSI_ID_S900) {
		DSIDBG("%s, s900\n", __func__);
		cmu_dsiclk = 0x300 | (divider_reg << 16) | (hsclk_reg/6);
		DSIDBG("s900 cmu_dsiclk = %x\n", cmu_dsiclk);
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);
		cmu_dsiclk = (1 << 16) | 101 << 8 | 2;
		DSIDBG("s700 cmu_dsiclk = %x\n", cmu_dsiclk);
	}

	writel(cmu_dsiclk, dsi->cmu_dsiclk_reg);
	DSIDBG("%s ok!\n", __func__);
	return 0;
}
static void dsihw_power_on(struct dsi_data *dsi)
{
	int val;
	DSIDBG("%s\n", __func__);
	reset_control_assert(dsi->rst);
	/* TODO */
	val = readl(dsi->sps_ctl_reg);
	val |= (1 << 9);
	writel(val, dsi->sps_ctl_reg);
	/* clock config */
	dsi_set_dsi_clk(dsi);
	/* clock framework is only used for enable/diable clocks */
	clk_prepare_enable(dsi->clk);
	mdelay(10);
	reset_control_deassert(dsi->rst);
}

static void dsihw_power_off(struct dsi_data *dsi)
{
	int val;

	DSIDBG("%s\n", __func__);

	reset_control_assert(dsi->rst);

	clk_disable_unprepare(dsi->clk);

	/* TODO */
	val = readl(dsi->sps_ctl_reg);
	val &= ~(1 << 9);
	writel(val, dsi->sps_ctl_reg);
}

static void dsihw_phy_config(struct dsi_data *dsi)
{
	int tmp;

	DSIDBG("%s\n", __func__);

	tmp = dsihw_read_reg(dsi, DSI_CTRL);
	if (dsi->diffs->id == DSI_ID_S900) {
		DSIDBG("%s, s900\n", __func__);
		tmp |= (3 << 8);			/*enable all lanes*/
		tmp |= (1 << 31);			/*PHY enable*/
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);
		tmp |= (3 << 8);			/* enable all lanes */
		tmp |= ((dsi->configs.lcd_mode) << 12);	/* video mode*/
	}
	dsihw_write_reg(dsi, DSI_CTRL, tmp);
	if (dsi->diffs->id == DSI_ID_S900) {
		DSIDBG("%s, s900\n", __func__);
		dsihw_write_reg(dsi, DSI_PHY_T0, 0x1ba3);
		dsihw_write_reg(dsi, DSI_PHY_T1, 0x1b1b);
		dsihw_write_reg(dsi, DSI_PHY_T2, 0x2f06);
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);
		dsihw_write_reg(dsi, DSI_PHY_T0, 0xa5a);
		dsihw_write_reg(dsi, DSI_PHY_T1, 0x1b12);
		dsihw_write_reg(dsi, DSI_PHY_T2, 0x2f05);
	}
	tmp = 0;
	if (dsi->diffs->id == DSI_ID_S900) {
		dsihw_write_reg(dsi, DSI_PHY_CTRL, 0x7c600000);
		if (dsi->configs.lane_polarity) {
			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp |= 0xb4083;
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);
		} else {
			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp |= 0x800fb;
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);
		}
		DSIDBG("DSI_PHY_CTRL = %x\n", tmp);
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);
		tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
		tmp |= (0xfc << 8);
		tmp |= (1 << 24);	/* s700 PHY enable */
		dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);
	}
	dsihw_write_reg(dsi, DSI_PIN_MAP, 0x688);
	/* cal */
	mdelay(10);
	DSIDBG("cal start\n");
	if (dsi->diffs->id == DSI_ID_S900) {
		DSIDBG("%s, s900\n", __func__);

		tmp = dsihw_read_reg(dsi, DSI_CTRL);
		tmp |= (1 << 30);
		dsihw_write_reg(dsi, DSI_CTRL, tmp);

		/* wait for cal done */
		tmp = WAIT_WITH_TIMEOUT((dsihw_read_reg(dsi, DSI_CTRL)
					& (1 << 30)) == 0, 10000);
		if (tmp == 0)
			DSIDBG("wait PHY cal done timout !\n");
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);

		/* PHY cal & PHY clk cal enable */
		tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
		tmp |= ((1 << 25) | (1 << 28));
		dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);

		/* wait for PHY clk cal done */
		tmp = WAIT_WITH_TIMEOUT((dsihw_read_reg(dsi, DSI_PHY_CTRL)
					&(1 << 31)) != 0, 1000);
		if (tmp == 0)
			DSIDBG("wait PHY clk cal done timeout!\n");

		/* wait for cal done */
		tmp = WAIT_WITH_TIMEOUT((dsihw_read_reg(dsi, DSI_PHY_CTRL)
					&(1 << 25)) == 0, 10000);
		if (tmp == 0) {
			DSIDBG("wait PHY cal done timeout!\n");

			/* disable calibrate */
			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp &= (~(1 << 25));
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);

			/* force clock lane */
			tmp = dsihw_read_reg(dsi, DSI_LANE_CTRL);
			tmp |= ((1 << 1) | (1 << 4));
			dsihw_write_reg(dsi, DSI_LANE_CTRL, tmp);

			/* Select output node */
			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp |= (3 << 2);
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);

			/* Re-enable D-PHY */
			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp &= (~(1 << 24));
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);

			tmp = dsihw_read_reg(dsi, DSI_PHY_CTRL);
			tmp |= (1 << 24);
			dsihw_write_reg(dsi, DSI_PHY_CTRL, tmp);
		}
	}
	DSIDBG("cal done\n");

	/* continue clock EN */
	tmp = dsihw_read_reg(dsi, DSI_CTRL);
	tmp |= 0x40;
	dsihw_write_reg(dsi, DSI_CTRL, tmp);
	mdelay(1);

	DSIDBG("wait line stop\n");
	tmp = WAIT_WITH_TIMEOUT((dsihw_read_reg(dsi, DSI_LANE_STA)
				& 0x1020) != 0, 1000);
	if (tmp == 0)
		DSIDBG("wait line stop timeout!\n");

	DSIDBG("%s end.\n", __func__);
}

void dsihw_send_short_packet(struct dsi_data *dsi, int data_type,
				int sp_data, int trans_mode)
{
	int tmp;

	DSIDBG("%s\n", __func__);

	tmp = dsihw_read_reg(dsi, DSI_CTRL);
	tmp &= 0xffffefff;
	dsihw_write_reg(dsi, DSI_CTRL, tmp);

	dsihw_write_reg(dsi, DSI_PACK_HEADER, sp_data);

	tmp = (data_type << 8) | (trans_mode << 14);
	dsihw_write_reg(dsi, DSI_PACK_CFG, tmp);
	mdelay(1);

	tmp = dsihw_read_reg(dsi, DSI_PACK_CFG);
	tmp |= 1;
	dsihw_write_reg(dsi, DSI_PACK_CFG, tmp);

	tmp = WAIT_WITH_TIMEOUT((dsihw_read_reg(dsi, DSI_TR_STA)
				& 0x80000) != 0, 10000);
	if (tmp == 0)
		DSIDBG("wait DSI_TR_STA timeout\n");


	dsihw_write_reg(dsi, DSI_TR_STA, 0x80000);
	DSIDBG("send short end\n");
}

static void dsihw_single_enable(struct dsi_data *dsi, bool enable)
{
	int tmp;

	DSIDBG("%s\n", __func__);

	if (enable) {
		tmp = dsihw_read_reg(dsi, DSI_VIDEO_CFG);
		tmp |= 0x01;
		dsihw_write_reg(dsi, DSI_VIDEO_CFG, tmp);
	} else {
		tmp = dsihw_read_reg(dsi, DSI_VIDEO_CFG);
		tmp &= (~0x01);
		dsihw_write_reg(dsi, DSI_VIDEO_CFG, tmp);
	}
}

static void dsihw_ctl_config(struct dsi_data *dsi)
{
	int val = 0;

	DSIDBG("%s\n", __func__);

	val = dsihw_read_reg(dsi, DSI_CTRL);
	val |= (1 << 31);
	dsihw_write_reg(dsi, DSI_CTRL, val);


	/*color from*/
	val = dsihw_read_reg(dsi, DSI_CTRL);
	val = REG_SET_VAL(val, 0, 7, 7);/* 0 DE, 1 default color */
	dsihw_write_reg(dsi, DSI_CTRL, val);


	val = 0;
	val = dsihw_read_reg(dsi, DSI_CTRL);/* 0: command mode,1: video mode*/
	val = REG_SET_VAL(val, dsi->configs.lcd_mode, 12, 12);
	dsihw_write_reg(dsi, DSI_CTRL, val);

	/*0: 1 data lane,1: 2 data lanes 2: 3 data lanes 3: 4 data lanes*/
	val = 0;
	val = dsihw_read_reg(dsi, DSI_CTRL);
	val = REG_SET_VAL(val, (dsi->configs.lane_count - 1), 9, 8);
	dsihw_write_reg(dsi, DSI_CTRL, val);


	/* others,to do*/
	val = 0;
	val = dsihw_read_reg(dsi, DSI_CTRL);
	val = REG_SET_VAL(val, 1, 4, 4);/* EOTP enable*/
	dsihw_write_reg(dsi, DSI_CTRL, val);
}

static void dsihw_set_size(struct dsi_data *dsi, int width, int height)
{
	int tmp = 0;
	DSIDBG("%d,%d\n", width, height);

	tmp = dsihw_read_reg(dsi, DSI_SIZE);
	tmp = REG_SET_VAL(tmp, height, 27, 16);/* */
	dsihw_write_reg(dsi,  DSI_SIZE, tmp);

	DSIDBG("DSI_SIZE = %x\n", tmp);

}
static void dsihw_set_default_color(struct dsi_data *dsi, uint32_t color)
{
	DSIDBG("%s\n", __func__);
	dsihw_write_reg(dsi, DSI_COLOR, color);
}
static void dsihw_set_timings(struct dsi_data *dsi,
				uint16_t width, uint16_t height,
				uint16_t hbp, uint16_t hfp, uint16_t hsw,
				uint16_t vbp, uint16_t vfp, uint16_t vsw)
{
	uint32_t tmp = 0;
	uint32_t pixel2pro = 0;
	uint32_t bpp = PANEL_BPP(dsi->ctrl.panel);
	uint32_t hsw_reg = 0, hbp_reg = 0, hfp_reg = 0, bllp_reg = 0;

	uint32_t rgbht0_reg = 0, rgbht1_reg = 0, rgbvt0_reg = 0, rgbvt1_reg = 0;
	uint32_t vtotal = vbp + vfp + vsw + height;
	DSIDBG("%s\n", __func__);
	switch (bpp) {
	case 16:
		pixel2pro = 2;
		break;

	case 18:
		pixel2pro = 3;
		break;

	case 24:
		pixel2pro = 3;
		break;
	}

	switch (dsi->configs.video_mode) {
	case 0:/*sync mode*/
		hsw_reg  = hsw * pixel2pro;
		hbp_reg  = hbp * pixel2pro;
		hfp_reg  = hfp * pixel2pro;
		bllp_reg = 0;
		break;
	case 1:/*de mode*/
		hsw_reg = 0;
		hbp_reg = (hsw + hbp) * pixel2pro;
		hfp_reg = hfp * pixel2pro;
		bllp_reg = 0;
		break;
	case 2:/*burst mode*/
		hsw_reg  = 0;
		hbp_reg  = (hsw + hbp) * pixel2pro;
		hfp_reg  = hfp * pixel2pro;
		bllp_reg = 1000;
		break;
	}
	rgbht0_reg = ((hsw_reg<<20) | (hfp_reg<<10) | (hbp_reg)) & 0x3fffffff;
	rgbht1_reg = (bllp_reg) & 0xfff;
	/*preline | vsw | vtotal*/
	rgbvt0_reg = (0x01f00000 | ((vsw) << 13) | (vtotal)) & 0xfffffff;
	rgbvt1_reg = (vsw+vbp) & 0x1fffffff;

	DSIDBG("dsi_ht0 = %x\n", rgbht0_reg);
	DSIDBG("dsi_ht1 = %x\n", rgbht1_reg);
	DSIDBG("dsi_vt0 = %x\n", rgbvt0_reg);
	DSIDBG("dsi_vt1 = %x\n", rgbvt1_reg);

	dsihw_write_reg(dsi, DSI_RGBHT0, rgbht0_reg);
	dsihw_write_reg(dsi, DSI_RGBHT1, rgbht1_reg);
	dsihw_write_reg(dsi, DSI_RGBVT0, rgbvt0_reg);
	dsihw_write_reg(dsi, DSI_RGBVT1, rgbvt1_reg);

	/* others,to do*/
	dsihw_write_reg(dsi, DSI_PACK_CFG, 0x0);
	if (dsi->diffs->id == DSI_ID_S900) {
		DSIDBG("%s, s900\n", __func__);
		dsihw_write_reg(dsi, DSI_PACK_HEADER, 0x900);
	} else if (dsi->diffs->id == DSI_ID_S700) {
		DSIDBG("%s, s700\n", __func__);
		dsihw_write_reg(dsi, DSI_PACK_HEADER, 0xf00);
	}
	/*vidoe mode select*/
	tmp = dsihw_read_reg(dsi, DSI_VIDEO_CFG);
	tmp = REG_SET_VAL(tmp, dsi->configs.video_mode, 2, 1);
	dsihw_write_reg(dsi, DSI_VIDEO_CFG, tmp);

	tmp = 0;
	tmp = dsihw_read_reg(dsi, DSI_VIDEO_CFG);
	tmp = REG_SET_VAL(tmp, 1, 3, 3);
	dsihw_write_reg(dsi, DSI_VIDEO_CFG, tmp);

	/*TO DO*/
	tmp = 0;
	tmp = dsihw_read_reg(dsi, DSI_VIDEO_CFG);
	tmp = REG_SET_VAL(tmp, 0x3, 10, 8);/* RGB color format*/
	dsihw_write_reg(dsi, DSI_VIDEO_CFG, tmp);

	DSIDBG("DSI_VIDEO_CFG = %x\n", tmp);
}
static void dsihw_display_init(struct dsi_data *dsi)
{
	struct owl_videomode *mode = &dsi->ctrl.panel->mode;


	dsihw_set_size(dsi, mode->xres, mode->yres);
	dsihw_set_default_color(dsi, 0xff0000);

	dsihw_set_timings(dsi, mode->xres, mode->yres,
			mode->hbp, mode->hfp, mode->hsw,
			mode->vbp, mode->vfp, mode->vsw);
}
static void dsihw_init_dsi(struct dsi_data *dsi)
{
	DSIDBG("%s\n", __func__);

	dsihw_phy_config(dsi);
	send_cmd(dsi);

	dsihw_ctl_config(dsi);
	dsihw_display_init(dsi);
	dsihw_single_enable(dsi, true);
}

static int dsihw_parse_params(struct platform_device *pdev,
				struct dsi_data *dsi)
{
	char propname[20];

	int ret = 0;

	struct device_node *of_node;
	struct device_node *entry;

	DSIINFO("%s\n", __func__);

	of_node = pdev->dev.of_node;

	/* panel_configs */
	sprintf(propname, "panel_configs");
	DSIDBG("propname = %s\n", propname);
	entry = of_parse_phandle(of_node, propname, 0);
	if (!entry)
		return -EINVAL;

	if (of_property_read_u32(entry, "lcd_mode",
		&dsi->configs.lcd_mode))
		return -EINVAL;
	if (of_property_read_u32(entry, "lane_count",
		&dsi->configs.lane_count))
		return -EINVAL;
	if (of_property_read_u32(entry, "lane_polarity",
		&dsi->configs.lane_polarity))
		return -EINVAL;
	if (of_property_read_u32(entry, "video_mode",
		&dsi->configs.video_mode))
		return -EINVAL;
	if (of_property_read_u32(entry, "pclk_rate",
		&dsi->configs.pclk_rate))
		return -EINVAL;

	if (ret < 0) {
		DSIERR("%s: parse panel_configs failed\n", __func__);
		return ret;
	}

	DSIDBG("lcd_mode      = %d\n", dsi->configs.lcd_mode);
	DSIDBG("lane_count    = %d\n", dsi->configs.lane_count);
	DSIDBG("video_mode    = %d\n", dsi->configs.video_mode);
	DSIDBG("lane_polarity = %d\n", dsi->configs.lane_polarity);
	DSIDBG("pclk_rate     = %d\n", dsi->configs.pclk_rate);

	return 0;
}

bool dsihw_is_enable(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dsi_data *dsi = dsihw_get_dsidrv_data(pdev);
	bool boot_dsi_inited = false;

	boot_dsi_inited = ((dsihw_read_reg(dsi, DSI_VIDEO_CFG) & 0x01) == 0x1);
	DSIINFO("DSI INITED FROM UBOOT??  %d\n", boot_dsi_inited);
	return boot_dsi_inited;
}
static int dsihw_display_enable(struct dsi_data *dsi)
{
	DSIDBG("%s\n", __func__);

	if (dsi->enabled)
		return 0;

	dsihw_power_on(dsi);
	dsihw_init_dsi(dsi);

	dsi->enabled = true;

	return 0;
}

static void dsihw_display_disable(struct dsi_data *dsi)
{
	DSIDBG("%s\n", __func__);

	if (!dsi->enabled)
		return;

	dsihw_single_enable(dsi, false);
	dsihw_power_off(dsi);

	dsi->enabled = false;
}

static int dsihw_ctrl_enable(struct owl_display_ctrl *ctrl)
{
	struct dsi_data *dsi = owl_ctrl_get_drvdata(ctrl);

	DSIINFO("%s\n", __func__);

	dsihw_display_enable(dsi);

	return 0;
}

static int dsihw_ctrl_disable(struct owl_display_ctrl *ctrl)
{
	struct dsi_data *dsi = owl_ctrl_get_drvdata(ctrl);

	DSIINFO("%s\n", __func__);

	dsihw_display_disable(dsi);

	return 0;
}

static struct owl_display_ctrl_ops owl_dsi_ctrl_ops = {
	.enable = dsihw_ctrl_enable,
	.disable = dsihw_ctrl_disable,
};

void test_dsi(struct dsi_data *dsi)
{

	DSIDBG("%s\n", __func__);

	dsihw_power_on(dsi);
	dsihw_phy_config(dsi);
	send_cmd(dsi);
	dsihw_single_enable(dsi, true);

	DSIDBG("%s end\n", __func__);
}

static const struct dsi_diffs dsi_diffs_s900 = {
	.id = DSI_ID_S900,
};

static const struct dsi_diffs dsi_diffs_s700 = {
	.id = DSI_ID_S700,
};


static struct of_device_id owl_dsihw_of_match[] = {
	{
		.compatible = "actions,s700-dsi",
		.data = &dsi_diffs_s700,
	},
	{
		.compatible = "actions,s900-dsi",
		.data = &dsi_diffs_s900,
	},
	{},
};

static int dsihw_get_resources(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dsi_data *dsi = dsihw_get_dsidrv_data(pdev);

	struct resource *res;

	DSIDBG("%s\n", __func__);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (IS_ERR(res)) {
		DSIERR("can't get regs\n");
		return PTR_ERR(res);
	}
	dsi->base = devm_ioremap_resource(dev, res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_dsiclk");
	if (IS_ERR(res)) {
		DSIERR("can't get cmu_disclk\n");
		return PTR_ERR(res);
	}
	dsi->cmu_dsiclk_reg = devm_ioremap_resource(dev, res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sps_ldo_ctl");
	if (IS_ERR(res)) {
		DSIERR("can't get sps_ldo_ctl\n");
		return PTR_ERR(res);
	}
	dsi->sps_ctl_reg = devm_ioremap_resource(dev, res);


	DSIDBG("base: 0x%p, dsiclk: 0x%p, sps_ctl, 0x%p\n",
		dsi->base, dsi->cmu_dsiclk_reg, dsi->sps_ctl_reg);

	dsi->clk = devm_clk_get(dev, "dsi");
	if (IS_ERR(dsi->clk)) {
		DSIERR("can't get clock\n");
		return PTR_ERR(dsi->clk);
	}

	dsi->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(dsi->rst)) {
		DSIERR("can't get reset\n");
		return PTR_ERR(dsi->rst);
	}
	return 0;
}

static int owl_dsihw_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct dsi_data *dsi;

	struct device *dev = &pdev->dev;
	const struct of_device_id *match;

	DSIINFO("add hhy 10 15 %s\n", __func__);

	match = of_match_device(of_match_ptr(owl_dsihw_of_match), dev);
	if (!match) {
		DSIERR("No device match found\n");
		return -ENODEV;
	}

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;
	dsi->diffs = match->data;

	dsi->pdev = pdev;
	dev_set_drvdata(dev, dsi);

	mutex_init(&dsi->lock);

	ret = dsihw_get_resources(pdev);
	if (ret < 0) {
		DSIERR("get resources failed: %d\n", ret);
		return ret;
	}

	owl_dsi_create_sysfs(dev);

	ret = dsihw_parse_params(pdev, dsi);
	if (ret < 0) {
		DSIERR("parse dsi params error: %d\n", ret);
		return ret;
	}

	dsi->ctrl.type = OWL_DISPLAY_TYPE_DSI;
	dsi->ctrl.ops = &owl_dsi_ctrl_ops;

	owl_ctrl_set_drvdata(&dsi->ctrl, dsi);
	ret = owl_ctrl_register(&dsi->ctrl);
	if (ret < 0) {
		DSIERR("register dsi ctrl failed: %d\n", ret);
		return ret;
	}
	dsi->enabled = dsihw_is_enable(pdev);
	if (dsi->enabled)
		dsihw_update_power_status(dsi);

	DSIINFO("%s, end\n", __func__);
	return 0;
}

static int owl_dsihw_remove(struct platform_device *pdev)
{
	struct dsi_data *dsi = dsihw_get_dsidrv_data(pdev);

	owl_dsi_remove_sysfs(&pdev->dev);

	dsihw_power_off(dsi);
	dsi->enabled = false;

	return 0;
}

static struct platform_driver owl_dsihw_driver = {
	.driver = {
		.name = "owl_dsihw",
		.owner = THIS_MODULE,
		.of_match_table = owl_dsihw_of_match,
	},
	.probe = owl_dsihw_probe,
	.remove = owl_dsihw_remove,
};

int __init owl_dsihw_init(void)
{
	int r = 0;

	DSIINFO("%s\n", __func__);

	r = platform_driver_register(&owl_dsihw_driver);
	if (r)
		DSIERR("Failed to initialize dsi platform driver\n");
	return r;
}

void __exit owl_dsihw_exit(void)
{
	platform_driver_unregister(&owl_dsihw_driver);
}

module_init(owl_dsihw_init);
module_exit(owl_dsihw_exit);
MODULE_LICENSE("GPL");

void dsihw_fs_dump_regs(struct device *dev)
{
	struct dsi_data *dsi = dev_get_drvdata(dev);
	dsihw_dump_regs(dsi);
}

void test_fs_dsi(struct device *dev)
{
	struct dsi_data *data = dev_get_drvdata(dev);
	test_dsi(data);
}

void test_fs_longcmd(struct device *dev)
{
	/* TODO */
}
