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
 *	2015/9/9: Created by Lipeng.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <video/owl_dss.h>

#include "hdmi.h"
#include "ip-sx00.h"

#define CLK_CTL_TEST

enum ip_sx00_ic_type {
	IC_TYPE_S500,
	IC_TYPE_S900,
};

struct ip_sx00_hwdiff {
	enum ip_sx00_ic_type		ic_type;

	int				hp_start;
	int				hp_end;
	int				vp_start;
	int				vp_end;
	int				mode_start;
	int				mode_end;
};

struct ip_sx00_data {
	struct hdmi_ip			ip;

	const struct ip_sx00_hwdiff	*hwdiff;

	struct regulator		*tmds_avdd;
};
#define IP_TO_IP_DATA(ip) container_of((ip), struct ip_sx00_data, ip)


static const struct ip_sx00_hwdiff ip_s500 = {
	.ic_type			= IC_TYPE_S500,
	.hp_start			= 16,
	.hp_end				= 28,
	.vp_start			= 4,
	.vp_end				= 15,
	.mode_start			= 0,
	.mode_end			= 0,
};

static const struct ip_sx00_hwdiff ip_s900 = {
	.ic_type			= IC_TYPE_S900,
	.hp_start			= 15,
	.hp_end				= 27,
	.vp_start			= 0,
	.vp_end				= 11,
	.mode_start			= 18,
	.mode_end			= 19,
};

/*
 * HDMI IP configurations
 */

/* devclk will used by HDMI HPD */
static void __ip_devclk_enable(struct hdmi_ip *ip, bool enable)
{
#ifdef CLK_CTL_TEST
	uint32_t val;

	/* HDMIA */
	val = IO_READU32(CMU_DEVCLKEN0);
	if (enable)
		val |= (1 << 22);
	else
		val &= ~(1 << 22);
	IO_WRITEU32(CMU_DEVCLKEN0, val);

	/* HDMI */
	val = IO_READU32(CMU_DEVCLKEN1);
	if (enable)
		val |= (1 << 3);
	else
		val &= ~(1 << 3);
	IO_WRITEU32(CMU_DEVCLKEN1, val);
#else
	/* TODO */
#endif

}

static int __ip_pll_enable(struct hdmi_ip *ip)
{
	uint32_t pix_rate = 0;
	uint32_t reg_val, val;

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	switch (ip->cfg->vid) {
	case VID640x480P_60_4VS3:
		pix_rate = 25200000;
		break;

	case VID720x576P_50_4VS3:
	case VID720x480P_60_4VS3:
		pix_rate = 27000000;
		break;

	case VID1280x720P_60_16VS9:
	case VID1280x720P_50_16VS9:
		pix_rate = 74250000;
		break;

	case VID1920x1080P_60_16VS9:
	case VID1920x1080P_50_16VS9:
		pix_rate = 148500000;
		break;

	case VID3840x2160p_30:
	case VID3840x1080p_60:
	case VID4096x2160p_30:
		pix_rate = 297000000;
		break;

	case VID2560x1024p_75:
		pix_rate = 270000000;
		break;

	case VID2560x1024p_60:
		pix_rate = 216000000;
		break;

	case VID1280x1024p_60:
		pix_rate = 108000000;
		break;

	default:
		return -EINVAL;
	}

	if (ip->settings.hdmi_mode == MHL_24BIT) {
		val = IO_READU32(CMU_TVOUTPLL);
		val &= ~(1 << 0);
		IO_WRITEU32(CMU_TVOUTPLL, 0x100);

		IO_WRITEU32(CMU_TVOUTPLLDEBUG0, 0);

		switch (pix_rate) {
		case 25200000:
			val = 0x400311;
			break;

		case 27000000:
			val = 0x410311;
			break;

		case 74250000:
			val = 0x440311;
			break;

		case 148500000:
			val = 0x460311;
			break;

		case 297000000:
			val = 0x440211;
			break;

		default:
			val = 0x440311;
			break;
		}
		IO_WRITEU32(CMU_TVOUTPLL, val);

		return 0;
	}

#ifdef CLK_CTL_TEST
	switch (pix_rate) {
	case 25200000:
		reg_val = 0;
		break;

	case 27000000:
		reg_val = 1;
		break;

	case 74250000:
		reg_val = 4;
		break;

	case 108000000:
		reg_val = 5;
		break;

	case 148500000:
		reg_val = 6;
		break;

	case 297000000:
		reg_val = 7;
		break;

	default:
		reg_val = 0;
		break;
	}

	/* disable PLL */
	val = IO_READU32(CMU_TVOUTPLL);
	val &= ~(1 << 0);
	IO_WRITEU32(CMU_TVOUTPLL, val);

	/* 24M enable */
	val = IO_READU32(CMU_TVOUTPLL);
	val |= (1 << 8);
	IO_WRITEU32(CMU_TVOUTPLL, val);

	/* set PLL */
	val = IO_READU32(CMU_TVOUTPLL);
	val &= ~(0x7 << 16);
	val |= (reg_val << 16);
	IO_WRITEU32(CMU_TVOUTPLL, val);
	mdelay(1);

	val = IO_READU32(SPS_LDO_CTL);
	val &= 0xfffffff0;

	if (ip->cfg->vid == VID3840x2160p_30 ||
	    ip->cfg->vid == VID3840x1080p_60 ||
	    ip->cfg->vid == VID4096x2160p_30 ||
	    ip->cfg->vid == VID2560x1024p_60 ||
	    ip->cfg->vid == VID2560x1024p_75) {
		IO_WRITEU32(CMU_TVOUTPLLDEBUG0, 0x80000000);
		if (ip->cfg->vid == VID2560x1024p_60)
			IO_WRITEU32(CMU_TVOUTPLLDEBUG1, 0x44042);
		else if (ip->cfg->vid == VID2560x1024p_75)
			IO_WRITEU32(CMU_TVOUTPLLDEBUG1, 0x56042);
		else
			IO_WRITEU32(CMU_TVOUTPLLDEBUG1, 0x5f642);

		val |= 0xe;
	} else {
		IO_WRITEU32(CMU_TVOUTPLLDEBUG0, 0x0);

		val |= 0xa;
	}

	IO_WRITEU32(SPS_LDO_CTL, val);

	/* enable PLL */
	val = IO_READU32(CMU_TVOUTPLL);
	val |= (1 << 0);
	IO_WRITEU32(CMU_TVOUTPLL, val);
	mdelay(1);
#else
#endif

	return 0;
}

static void __ip_pll_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

#ifdef CLK_CTL_TEST
	if (ip->cfg->vid == VID3840x2160p_30 ||
	    ip->cfg->vid == VID3840x1080p_60 ||
	    ip->cfg->vid == VID4096x2160p_30 ||
	    ip->cfg->vid == VID2560x1024p_60 ||
	    ip->cfg->vid == VID2560x1024p_75) {
		IO_WRITEU32(CMU_TVOUTPLLDEBUG1, 0x2614a);
		IO_WRITEU32(CMU_TVOUTPLLDEBUG0, 0x0);
	}

	val = IO_READU32(SPS_LDO_CTL);
	val &= 0xfffffff0;
	val |= 0xa;
	IO_WRITEU32(SPS_LDO_CTL, val);

	val = IO_READU32(CMU_TVOUTPLL);
	val &= ~(1 << 8);	/* 24M disable */
	val &= ~(1 << 0);	/* PLL disable */
	IO_WRITEU32(CMU_TVOUTPLL, val);

	IO_WRITEU32(CMU_TVOUTPLL, 0);
	IO_WRITEU32(CMU_TVOUTPLLDEBUG1, 0x2614a);
	IO_WRITEU32(CMU_TVOUTPLLDEBUG0, 0);
#else
	/* TODO */
#endif
}

static int __ip_phy_enable(struct hdmi_ip *ip)
{
	uint32_t val;
	uint32_t tx_1 = 0, tx_2 = 0;
	uint32_t phyctrl_1 = 0, phyctrl_2 = 0;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	val = hdmi_ip_readl(ip, TMDS_EODR0);
	val = REG_SET_VAL(val, 1, 31, 31);
	hdmi_ip_writel(ip, TMDS_EODR0, val);

	if (ip_data->hwdiff->ic_type == IC_TYPE_S500) {
		switch (ip->cfg->vid) {
		case VID640x480P_60_4VS3:
			tx_1 = 0x819c2984;
			tx_2 = 0x18f80f87;
			break;

		case VID720x576P_50_4VS3:
		case VID720x480P_60_4VS3:
			tx_1 = 0x819c2984;
			tx_2 = 0x18f80f87;
			break;

		case VID1280x720P_60_16VS9:
		case VID1280x720P_50_16VS9:
			tx_1 = 0x81942986;
			tx_2 = 0x18f80f87;
			break;

		case VID1920x1080P_60_16VS9:
		case VID1920x1080P_50_16VS9:
			tx_1 = 0x8190284f;
			tx_2 = 0x18fa0f87;
			break;

		case VID3840x2160p_30:
		case VID3840x1080p_60:
		case VID4096x2160p_30:
			tx_1 = 0x8086284F;
			tx_2 = 0x000E0F01;
			break;

		default:
			goto vid_err;
		}
	} else if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		if (ip->settings.hdmi_mode != MHL_24BIT) {
			switch (ip->cfg->vid) {
			case VID640x480P_60_4VS3:
				tx_1 = 0x808c2904;
				tx_2 = 0x00f00fc1;
				break;

			case VID720x576P_50_4VS3:
			case VID720x480P_60_4VS3:
				tx_1 = 0x808c2904;
				tx_2 = 0x00f00fc1;
				break;

			case VID1280x720P_60_16VS9:
			case VID1280x720P_50_16VS9:
				tx_1 = 0x80882904;
				tx_2 = 0x00f00fc1;
				break;

			case VID1920x1080P_60_16VS9:
			case VID1920x1080P_50_16VS9:
				tx_1 = 0x80842846;
				tx_2 = 0x00000FC1;
				break;

			case VID3840x2160p_30:
			case VID3840x1080p_60:
			case VID4096x2160p_30:
			case VID2560x1024p_75:
				tx_1 = 0x8080284F;
				tx_2 = 0x000E0F01;
				break;

			case VID2560x1024p_60:
				tx_1 = 0x8084284F;
				tx_2 = 0x000E0FC1;
				break;

			case VID1280x1024p_60:
				tx_1 = 0x80882904;
				tx_2 = 0x00f00fc1;
				break;

			default:
				goto vid_err;
			}

			/* set tx pll locked to clkhdmi's fall edge */
			tx_1 = REG_SET_VAL(tx_1, 1, 13, 13);

		} else {
			switch (ip->cfg->vid) {
			case VID640x480P_60_4VS3:
				phyctrl_1 = 0x0496f485;
				phyctrl_2 = 0x2101b;
				break;

			case VID720x576P_50_4VS3:
			case VID720x480P_60_4VS3:
				phyctrl_1 = 0x0496f485;
				phyctrl_2 = 0x2101b;
				break;

			case VID1280x720P_60_16VS9:
			case VID1280x720P_50_16VS9:
				phyctrl_2 = 0x2081b;
				phyctrl_1 = 0x0497f885;
				break;

			case VID1920x1080P_60_16VS9:
			case VID1920x1080P_50_16VS9:
				phyctrl_2 = 0x2001b;
				phyctrl_1 = 0x04abfb05;
				break;

			default:
				goto vid_err;
			}
		}
	} else {
		switch (ip->cfg->vid) {
		case VID640x480P_60_4VS3:
			tx_1 = 0x819c0986;
			tx_2 = 0x18f80f87;
			break;

		case VID720x576P_50_4VS3:
		case VID720x480P_60_4VS3:
			tx_1 = 0x819c0986;
			tx_2 = 0x18f80f87;
			break;

		case VID1280x720P_60_16VS9:
		case VID1280x720P_50_16VS9:
			tx_1 = 0x81982986;
			tx_2 = 0x18f80f87;
			break;

		case VID1920x1080P_60_16VS9:
		case VID1920x1080P_50_16VS9:
			tx_1 = 0x81940986;
			tx_2 = 0x18f80f87;
			break;

		case VID3840x2160p_30:
		case VID3840x1080p_60:
		case VID4096x2160p_30:
			tx_1 = 0x8086284F;
			tx_2 = 0x000E0F01;
			break;

		default:
			goto vid_err;
			break;
		}
	}

	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip->settings.hdmi_mode == MHL_24BIT) {
		hdmi_ip_writel(ip, MHL_PHYCTL1, phyctrl_1);
		hdmi_ip_writel(ip, MHL_PHYCTL2, phyctrl_2);
	} else {
		hdmi_ip_writel(ip, HDMI_TX_1, tx_1);

		/* do not enable HDMI lane util video enable */
		tx_2 &= (~(0xf << 8));
		hdmi_ip_writel(ip, HDMI_TX_2, tx_2);
	}
	udelay(500);

	/* ATM900A need TDMS clock calibration */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		val = hdmi_ip_readl(ip, CEC_DDC_HPD);

		/* 0 to 1, start calibration */
		val = REG_SET_VAL(val, 0, 20, 20);
		hdmi_ip_writel(ip, CEC_DDC_HPD, val);

		udelay(10);

		val = REG_SET_VAL(val, 1, 20, 20);
		hdmi_ip_writel(ip, CEC_DDC_HPD, val);

		while (1) {
			val = hdmi_ip_readl(ip, CEC_DDC_HPD);
			if ((val >> 24) & 0x1)
				break;
		}
	}

	return 0;

vid_err:
	dev_err(&ip->pdev->dev, "no surpport this vid %d\n", ip->cfg->vid);
	return -EINVAL;
}

static void __ip_phy_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	/* tdms disable */
	val = hdmi_ip_readl(ip, TMDS_EODR0);
	val = REG_SET_VAL(val, 0, 31, 31);
	hdmi_ip_writel(ip, TMDS_EODR0, val);

	/* tx pll power off */
	val = hdmi_ip_readl(ip, HDMI_TX_1);
	val = REG_SET_VAL(val, 0, 23, 23);
	hdmi_ip_writel(ip, HDMI_TX_1, val);

	/* LDO_TMDS power off */
	val = hdmi_ip_readl(ip, HDMI_TX_2);

	val = REG_SET_VAL(val, 0, 27, 27);
	if (ip_data->hwdiff->ic_type == IC_TYPE_S900) {
		val = REG_SET_VAL(val, 0, 17, 17);
		val = REG_SET_VAL(val, 0, 11, 8);
	}

	hdmi_ip_writel(ip, HDMI_TX_2, val);
}

static void __ip_core_deepcolor_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.deep_color, 17, 16);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_pixel_fomat_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.pixel_encoding, 5, 4);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_preline_config(struct hdmi_ip *ip)
{
	int preline;
	uint32_t val = 0;

	preline = ip->settings.prelines;
	preline = (preline <= 0 ? 1 : preline);
	preline = (preline > 16 ? 16 : preline);

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, preline - 1, 23, 20);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_3d_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);

	if (ip->settings.mode_3d == MODE_3D_FRAME)
		val = REG_SET_VAL(val, 1, 8, 8);
	else
		val = REG_SET_VAL(val, 0, 8, 8);

	hdmi_ip_writel(ip, HDMI_SCHCR, val);
}

static void __ip_core_mode_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.hdmi_mode,
			  ip_data->hwdiff->mode_end,
			  ip_data->hwdiff->mode_start);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);

	/* ATM9009's HDMI mode, should set HDCP_KOWR & HDCP_OWR */
	if (ip_data->hwdiff->ic_type == IC_TYPE_S900 &&
	    ip->settings.hdmi_mode == 1) {
		/*42,end 651,star 505 */
		hdmi_ip_writel(ip, HDCP_KOWR,
			       HDCP_KOWR_HDCPREKEYKEEPOUTWIN(0x2a) |
			       HDCP_KOWR_HDCPVERKEEPOUTWINEND(0x28b) |
			       HDCP_KOWR_HDCPVERTKEEPOUTWINSTART(0x1f9));

		/*HDCP1.1 Mode: start 510,end 526 */
		hdmi_ip_writel(ip, HDCP_OWR, HDCP_OWR_HDCPOPPWINEND(0x20e) |
			       HDCP_OWR_HDCPOPPWINSTART(0x1fe));
	}
}

static void __ip_core_invert_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, ip->settings.bit_invert, 28, 28);
	val = REG_SET_VAL(val, ip->settings.channel_invert, 29, 29);

	hdmi_ip_writel(ip, HDMI_SCHCR, val);
	return;
}

static void __ip_core_colordepth_config(struct hdmi_ip *ip)
{
	uint32_t val = 0;

	uint32_t mode = ip->settings.deep_color;

	val = hdmi_ip_readl(ip, HDMI_GCPCR);

	val = REG_SET_VAL(val, mode, 7, 4);
	val = REG_SET_VAL(val, 1, 31, 31);

	if (mode > HDMI_PACKETMODE24BITPERPIXEL)
		val = REG_SET_VAL(val, 1, 30, 30);
	else
		val = REG_SET_VAL(val, 0, 30, 30);

	/* clear specify avmute flag in gcp packet */
	val = REG_SET_VAL(val, 1, 1, 1);

	hdmi_ip_writel(ip, HDMI_GCPCR, val);
}

static void __ip_core_input_src_config(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_ICR);

	if (ip->settings.hdmi_src == VITD) {
		val = REG_SET_VAL(val, 0x01, 24, 24);
		val = REG_SET_VAL(val, ip->settings.vitd_color, 23, 0);
	} else {
		val = REG_SET_VAL(val, 0x00, 24, 24);
	}

	hdmi_ip_writel(ip, HDMI_ICR, val);
}

static void __ip_video_format_config(struct hdmi_ip *ip)
{
	uint32_t val;
	uint32_t val_hp, val_vp;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);
	const struct owl_videomode *mode = &ip->cfg->mode;

	val_hp = mode->xres + mode->hbp + mode->hfp + mode->hsw;
	val_vp = mode->yres + mode->vbp + mode->vfp + mode->vsw;

	dev_dbg(&ip->pdev->dev, "x %d %d %d %d\n",
		mode->xres, mode->hbp, mode->hfp, mode->hsw);
	dev_dbg(&ip->pdev->dev, "x %d %d %d %d\n",
		mode->yres, mode->vbp, mode->vfp, mode->vsw);

	val = hdmi_ip_readl(ip, HDMI_VICTL);

	val = REG_SET_VAL(val, val_hp - 1, ip_data->hwdiff->hp_end,
			  ip_data->hwdiff->hp_start);

	if (ip->cfg->interlace == 0)
		val = REG_SET_VAL(val, val_vp - 1, ip_data->hwdiff->vp_end,
				  ip_data->hwdiff->vp_start);
	else
		val = REG_SET_VAL(val, val_vp * 2, ip_data->hwdiff->vp_end,
				  ip_data->hwdiff->vp_start);

	dev_dbg(&ip->pdev->dev, "%s: val = %x hp = %x vp=%x\n",
		__func__, val, val_hp, val_vp);

	hdmi_ip_writel(ip, HDMI_VICTL, val);
}

static void __ip_video_interface_config(struct hdmi_ip *ip)
{
	uint32_t val;

	const struct owl_videomode *mode = &ip->cfg->mode;

	dev_dbg(&ip->pdev->dev, "%s: interlace %d\n",
		__func__, ip->cfg->interlace);

	if (ip->cfg->interlace == 0) {
		val = 0;
		hdmi_ip_writel(ip, HDMI_VIVSYNC, val);

		val = hdmi_ip_readl(ip, HDMI_VIVHSYNC);

		if (ip->cfg->vstart != 1) {
			val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
			val = REG_SET_VAL(val, ip->cfg->vstart - 2, 23, 12);
			val = REG_SET_VAL(val, ip->cfg->vstart + mode->vsw - 2,
					  27, 24);
		} else {
			val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
			val = REG_SET_VAL(val, mode->yres + mode->vbp
					  + mode->vfp + mode->vsw - 1,
					  23, 12);
			val = REG_SET_VAL(val, mode->vsw - 1, 27, 24);
		}
		hdmi_ip_writel(ip, HDMI_VIVHSYNC, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIVHSYNC 0x%x\n",
			__func__, val);

		/*
		 * VIALSEOF = (yres + vbp + vsp - 1) | ((vbp + vfp - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIALSEOF);
		val = REG_SET_VAL(val, ip->cfg->vstart - 1 + mode->vsw
				  + mode->vbp + mode->yres - 1, 23, 12);
		val = REG_SET_VAL(val, ip->cfg->vstart - 1 + mode->vsw
				  + mode->vbp - 1, 10, 0);
		hdmi_ip_writel(ip, HDMI_VIALSEOF, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIALSEOF 0x%x\n",
			__func__, val);

		val = 0;
		hdmi_ip_writel(ip, HDMI_VIALSEEF, val);

		/*
		 * VIADLSE = (xres + hbp + hsp - 1) | ((hbp + hsw - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIADLSE);
		val = REG_SET_VAL(val, mode->hbp +  mode->hsw - 1,
				  11, 0);
		val = REG_SET_VAL(val, mode->xres + mode->hbp
				  + mode->hsw - 1, 28, 16);
		hdmi_ip_writel(ip, HDMI_VIADLSE, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIADLSE 0x%x\n",
			__func__, val);
	} else {
		val = 0;
		hdmi_ip_writel(ip, HDMI_VIVSYNC, val);

		/*
		 * VIVHSYNC =
		 * (hsw -1 ) | ((yres + vsw + vfp + vbp - 1 ) << 12)
		 *  | (vfp -1 << 24)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIVHSYNC);
		val = REG_SET_VAL(val, mode->hsw - 1, 8, 0);
		val = REG_SET_VAL(val, (mode->yres + mode->vbp
				  + mode->vfp + mode->vsw) * 2, 22, 12);
		val = REG_SET_VAL(val, mode->vfp * 2, 22, 12);/* ??, TODO */
		hdmi_ip_writel(ip, HDMI_VIVHSYNC, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIVHSYNC 0x%x\n",
			__func__, val);

		/*
		 * VIALSEOF = (yres + vbp + vfp - 1) | ((vbp + vfp - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIALSEOF);
		val = REG_SET_VAL(val, mode->vbp + mode->vfp  - 1,
				  22, 12);
		val = REG_SET_VAL(val, (mode->yres + mode->vbp
					+ mode->vfp) * 2, 10, 0);
		hdmi_ip_writel(ip, HDMI_VIALSEOF, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIALSEOF 0x%x\n",
			__func__, val);

		val = 0;
		hdmi_ip_writel(ip, HDMI_VIALSEEF, val);

		/*
		 * VIADLSE = (xres + hbp + hsp - 1) | ((hbp + hsw - 1) << 12)
		 */
		val = hdmi_ip_readl(ip, HDMI_VIADLSE);
		val = REG_SET_VAL(val, mode->hbp +  mode->hsw - 1,
				  27, 16);
		val = REG_SET_VAL(val, mode->xres + mode->hbp
				  + mode->hsw - 1, 11, 0);
		hdmi_ip_writel(ip, HDMI_VIADLSE, val);
		dev_dbg(&ip->pdev->dev, "%s: HDMI_VIADLSE 0x%x\n",
			__func__, val);
	}
}

static void __ip_video_interval_packet_config(struct hdmi_ip *ip)
{
	uint32_t val;

	switch (ip->cfg->vid) {
	case VID640x480P_60_4VS3:
	case VID720x480P_60_4VS3:
	case VID720x576P_50_4VS3:
		val = 0x701;
		break;

	case VID1280x720P_60_16VS9:
	case VID1280x720P_50_16VS9:
	case VID1920x1080P_50_16VS9:
		val = 0x1107;
		break;

	case VID1920x1080P_60_16VS9:
	case VID3840x1080p_60:
		val = 0x1105;
		break;

	default:
		val = 0x1107;
		break;
	}

	hdmi_ip_writel(ip, HDMI_DIPCCR, val);
}

static void __ip_video_timing_config(struct hdmi_ip *ip)
{
	bool vsync_pol, hsync_pol, interlace, repeat;
	uint32_t val;
	const struct owl_videomode *mode = &ip->cfg->mode;

	vsync_pol = ((mode->sync & DSS_SYNC_VERT_HIGH_ACT) == 0);
	hsync_pol = ((mode->sync & DSS_SYNC_HOR_HIGH_ACT) == 0);

	interlace = ip->cfg->interlace;
	repeat = ip->cfg->repeat;

	val = hdmi_ip_readl(ip, HDMI_SCHCR);
	val = REG_SET_VAL(val, hsync_pol, 1, 1);
	val = REG_SET_VAL(val, vsync_pol, 2, 2);
	hdmi_ip_writel(ip, HDMI_SCHCR, val);

	val = hdmi_ip_readl(ip, HDMI_VICTL);
	val = REG_SET_VAL(val, interlace, 28, 28);
	val = REG_SET_VAL(val, repeat, 29, 29);
	hdmi_ip_writel(ip, HDMI_VICTL, val);
}

static void __ip_video_start(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 1, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);

	val = hdmi_ip_readl(ip, HDMI_TX_2);
	val = REG_SET_VAL(val, 0xf, 11, 8);
	hdmi_ip_writel(ip, HDMI_TX_2, val);
}

static void __ip_video_stop(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 0, 0, 0);
	hdmi_ip_writel(ip, HDMI_CR, val);
}


static int ip_sx00_init(struct hdmi_ip *ip)
{
	uint32_t val;

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	ip->settings.hdmi_src = DE;
	ip->settings.vitd_color = 0xff0000;
	ip->settings.pixel_encoding = RGB444;
	ip->settings.color_xvycc = 0;
	ip->settings.deep_color = color_mode_24bit;

	ip->settings.channel_invert = 0;
	ip->settings.bit_invert = 0;

	__ip_devclk_enable(ip, true);

#ifdef CLK_CTL_TEST
	/* init LDO to a fix value */
	val = IO_READU32(SPS_LDO_CTL);
	val &= 0xfffffff0;
	val |= 0xa;
	IO_WRITEU32(SPS_LDO_CTL, val);
#endif

	return 0;
}

static void ip_sx00_exit(struct hdmi_ip *ip)
{
	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	__ip_devclk_enable(ip, false);
}

static int ip_sx00_power_on(struct hdmi_ip *ip)
{
	int ret = 0;

	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	ret = regulator_enable(ip_data->tmds_avdd);
	if (ret < 0)
		return ret;

	ret = regulator_set_voltage(ip_data->tmds_avdd, 1800000, 1800000);

	return ret;
}

static void ip_sx00_power_off(struct hdmi_ip *ip)
{
	struct ip_sx00_data *ip_data = IP_TO_IP_DATA(ip);

	dev_dbg(&ip->pdev->dev, "%s\n", __func__);

	/* set a default value in case it cannot be disable */
	regulator_set_voltage(ip_data->tmds_avdd, 1800000, 1800000);
	regulator_disable(ip_data->tmds_avdd);
}

static bool ip_sx00_is_power_on(struct hdmi_ip *ip)
{
	/* TODO */
	return (hdmi_ip_readl(ip, HDMI_CR) & 0x01) != 0;
}

static void ip_sx00_hpd_enable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);

	val = REG_SET_VAL(val, 0x0f, 27, 24);	/* hotplug debounce */
	val = REG_SET_VAL(val, 0x01, 31, 31);	/* enable hotplug interrupt */
	val = REG_SET_VAL(val, 0x01, 28, 28);	/* enable hotplug function */
	val = REG_SET_VAL(val, 0x00, 30, 30);	/* not clear pending bit */

	hdmi_ip_writel(ip, HDMI_CR, val);
}

static void ip_sx00_hpd_disable(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);

	val = REG_SET_VAL(val, 0x00, 31, 31);	/* disable hotplug interrupt */
	val = REG_SET_VAL(val, 0x00, 28, 28);	/* enable hotplug function */
	val = REG_SET_VAL(val, 0x01, 30, 30);	/* clear pending bit */

	hdmi_ip_writel(ip, HDMI_CR, val);
}

static bool ip_sx00_hpd_is_pending(struct hdmi_ip *ip)
{
	return (hdmi_ip_readl(ip, HDMI_CR) & (1 << 30)) != 0;
}

static void ip_sx00_hpd_clear_pending(struct hdmi_ip *ip)
{
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_CR);
	val = REG_SET_VAL(val, 0x01, 30, 30);	/* clear pending bit */
	hdmi_ip_writel(ip, HDMI_CR, val);
}

static bool ip_sx00_cable_status(struct hdmi_ip *ip)
{
	if ((hdmi_ip_readl(ip, HDMI_CR) & (1 << 29)) &&
	    ((hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 14)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 12)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 10)) ||
	     (hdmi_ip_readl(ip, CEC_DDC_HPD) & (3 << 8))))
		return true;
	else
		return false;
}


static int ip_sx00_video_enable(struct hdmi_ip *ip)
{
	int ret = 0;

	mdelay(5);
	ret = __ip_pll_enable(ip);
	if (ret < 0) {
		dev_err(&ip->pdev->dev, "pll enable failed\n");
		goto err_pll_enable;
	}
	mdelay(10);

	__ip_video_stop(ip);

	ret = __ip_phy_enable(ip);
	if (ret < 0) {
		dev_err(&ip->pdev->dev, "phy enable failed\n");
		goto err_phy_enable;
	}

	__ip_video_timing_config(ip);
	__ip_video_format_config(ip);
	__ip_video_interface_config(ip);
	__ip_video_interval_packet_config(ip);
	__ip_core_input_src_config(ip);
	__ip_core_pixel_fomat_config(ip);
	__ip_core_preline_config(ip);
	__ip_core_deepcolor_mode_config(ip);
	__ip_core_mode_config(ip);
	__ip_core_invert_config(ip);
	__ip_core_colordepth_config(ip);
	__ip_core_3d_mode_config(ip);

	hdmi_packet_gen_infoframe(ip);

	__ip_video_start(ip);

	return 0;

err_phy_enable:
	__ip_pll_disable(ip);

err_pll_enable:
	return ret;
}

static void ip_sx00_video_disable(struct hdmi_ip *ip)
{
	__ip_video_stop(ip);
	__ip_phy_disable(ip);
	__ip_pll_disable(ip);
}

static bool ip_sx00_is_video_enabled(struct hdmi_ip *ip)
{
	return (hdmi_ip_readl(ip, HDMI_CR) & 0x01) != 0;
}

static int __ip_enable_write_ram_packet(struct hdmi_ip *ip)
{
	int i;
	uint32_t val;

	val = hdmi_ip_readl(ip, HDMI_OPCR);
	val |= (1 << 31);
	hdmi_ip_writel(ip, HDMI_OPCR, val);

	i = 100;
	while (i--) {
		val = hdmi_ip_readl(ip, HDMI_OPCR);
		val = val >> 31;
		if (val == 0)
			break;
		udelay(1);
	}

	return 0;
}


static int ip_sx00_packet_generate(struct hdmi_ip *ip, uint32_t no,
				   uint8_t *pkt)
{
	uint8_t tpkt[36];
	int i, j;
	uint32_t reg[9];
	uint32_t addr = 126 + no * 14;


	if (no >= PACKET_MAX)
		return -1;

	/* Packet Header */
	tpkt[0] = pkt[0];
	tpkt[1] = pkt[1];
	tpkt[2] = pkt[2];
	tpkt[3] = 0;

	/* Packet Word0 */
	tpkt[4] = pkt[3];
	tpkt[5] = pkt[4];
	tpkt[6] = pkt[5];
	tpkt[7] = pkt[6];

	/* Packet Word1 */
	tpkt[8] = pkt[7];
	tpkt[9] = pkt[8];
	tpkt[10] = pkt[9];
	tpkt[11] = 0;

	/* Packet Word2 */
	tpkt[12] = pkt[10];
	tpkt[13] = pkt[11];
	tpkt[14] = pkt[12];
	tpkt[15] = pkt[13];

	/* Packet Word3 */
	tpkt[16] = pkt[14];
	tpkt[17] = pkt[15];
	tpkt[18] = pkt[16];
	tpkt[19] = 0;

	/* Packet Word4 */
	tpkt[20] = pkt[17];
	tpkt[21] = pkt[18];
	tpkt[22] = pkt[19];
	tpkt[23] = pkt[20];

	/* Packet Word5 */
	tpkt[24] = pkt[21];
	tpkt[25] = pkt[22];
	tpkt[26] = pkt[23];
	tpkt[27] = 0;

	/* Packet Word6 */
	tpkt[28] = pkt[24];
	tpkt[29] = pkt[25];
	tpkt[30] = pkt[26];
	tpkt[31] = pkt[27];

	/* Packet Word7 */
	tpkt[32] = pkt[28];
	tpkt[33] = pkt[29];
	tpkt[34] = pkt[30];
	tpkt[35] = 0;

	/* for s900 change ?? TODO */
	for (i = 0; i < 9; i++) {
		reg[i] = 0;
		for (j = 0; j < 4; j++)
			reg[i] |= (tpkt[i * 4 + j]) << (j * 8);
	}

	hdmi_ip_writel(ip, HDMI_OPCR,    (1 << 8) | (addr & 0xff));
	hdmi_ip_writel(ip, HDMI_ORP6PH,  reg[0]);
	hdmi_ip_writel(ip, HDMI_ORSP6W0, reg[1]);
	hdmi_ip_writel(ip, HDMI_ORSP6W1, reg[2]);
	hdmi_ip_writel(ip, HDMI_ORSP6W2, reg[3]);
	hdmi_ip_writel(ip, HDMI_ORSP6W3, reg[4]);
	hdmi_ip_writel(ip, HDMI_ORSP6W4, reg[5]);
	hdmi_ip_writel(ip, HDMI_ORSP6W5, reg[6]);
	hdmi_ip_writel(ip, HDMI_ORSP6W6, reg[7]);
	hdmi_ip_writel(ip, HDMI_ORSP6W7, reg[8]);

	__ip_enable_write_ram_packet(ip);

	return 0;
}

static int ip_sx00_packet_send(struct hdmi_ip *ip, uint32_t no, int period)
{
	uint32_t val;

	if (no > PACKET_MAX || no < 0)
		return -1;

	if (period > 0xf || period < 0)
		return -1;

	val = hdmi_ip_readl(ip, HDMI_RPCR);
	val &= (~(1 << no));
	hdmi_ip_writel(ip, HDMI_RPCR,  val);

	val = hdmi_ip_readl(ip, HDMI_RPCR);
	val &= (~(0xf << (no * 4 + 8)));
	hdmi_ip_writel(ip, HDMI_RPCR, val);

	/* enable and set period */
	if (period) {
		val = hdmi_ip_readl(ip, HDMI_RPCR);
		val |= (period << (no * 4 + 8));
		hdmi_ip_writel(ip, HDMI_RPCR,  val);

		val = hdmi_ip_readl(ip, HDMI_RPCR);
		val |= (1 << no);
		hdmi_ip_writel(ip, HDMI_RPCR,  val);
	}

	return 0;
}

static const struct hdmi_ip_ops ip_sx00_ops = {
	.init =	ip_sx00_init,
	.exit =	ip_sx00_exit,

	.power_on = ip_sx00_power_on,
	.power_off = ip_sx00_power_off,
	.is_power_on = ip_sx00_is_power_on,

	.hpd_enable = ip_sx00_hpd_enable,
	.hpd_disable = ip_sx00_hpd_disable,
	.hpd_is_pending = ip_sx00_hpd_is_pending,
	.hpd_clear_pending = ip_sx00_hpd_clear_pending,
	.cable_status = ip_sx00_cable_status,

	.video_enable = ip_sx00_video_enable,
	.video_disable = ip_sx00_video_disable,
	.is_video_enabled = ip_sx00_is_video_enabled,

	.packet_generate = ip_sx00_packet_generate,
	.packet_send = ip_sx00_packet_send,

	.hdcp_init = ip_sx00_hdcp_init,
	.hdcp_enable = ip_sx00_hdcp_enable,
};

static const struct of_device_id ip_sx00_of_match[] = {
	{
		.compatible = "actions,s500-hdmi",
		.data = &ip_s500,
	},
	{
		.compatible = "actions,s900-hdmi",
		.data = &ip_s900,
	},
	{}
};
MODULE_DEVICE_TABLE(of, ip_sx00_of_match);

static int ip_sx00_probe(struct platform_device *pdev)
{
	int ret = 0;

	struct device *dev = &pdev->dev;
	const struct of_device_id *match;

	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(ip_sx00_of_match, dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	ip_data = devm_kzalloc(dev, sizeof(*ip_data), GFP_KERNEL);
	if (ip_data == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, ip_data);

	ip_data->hwdiff = (struct ip_sx00_hwdiff *)match->data;

	ip_data->tmds_avdd = regulator_get(dev, "tmds-avdd");
	if (IS_ERR(ip_data->tmds_avdd)) {
		dev_err(dev, "tmds-avdd get error (%ld)\n",
			PTR_ERR(ip_data->tmds_avdd));
		return PTR_ERR(ip_data->tmds_avdd);
	}
	dev_dbg(dev, "tmds_avdd %p, current is %duv\n", ip_data->tmds_avdd,
		regulator_get_voltage(ip_data->tmds_avdd));

	ip_data->ip.pdev = pdev;
	ip_data->ip.ops = &ip_sx00_ops;

	ret = hdmi_ip_register(&ip_data->ip);
	if (ret < 0) {
		dev_err(dev, "hdmi_ip_register failed(%d)\n", ret);
		return ret;
	}

	return 0;
}

static int ip_sx00_remove(struct platform_device *pdev)
{
	struct ip_sx00_data *ip_data;

	pr_info("%s\n", __func__);

	ip_data = dev_get_drvdata(&pdev->dev);

	hdmi_ip_unregister(&ip_data->ip);

	return 0;
}

static int ip_sx00_suspend(struct device *dev)
{
	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);


	/* you can do something special */

	ip_data = dev_get_drvdata(dev);
	hdmi_ip_generic_suspend(&ip_data->ip);

	return 0;
}

static int ip_sx00_resume(struct device *dev)
{
	struct ip_sx00_data *ip_data;

	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	ip_data = dev_get_drvdata(dev);
	hdmi_ip_generic_resume(&ip_data->ip);

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(ip_sx00_pm_ops, ip_sx00_suspend,
			    ip_sx00_resume, NULL);

static struct platform_driver ip_sx00_driver = {
	.probe		= ip_sx00_probe,
	.remove         = ip_sx00_remove,

	.driver         = {
		.name   = "hdmi-ip-sx00",
		.owner  = THIS_MODULE,
		.of_match_table = ip_sx00_of_match,
		.pm		= &ip_sx00_pm_ops,
	},
};

int __init ip_sx00_platform_init(void)
{
	pr_info("%s\n", __func__);

	if (platform_driver_register(&ip_sx00_driver) < 0) {
		pr_err("failed to register ip_sx00_driver\n");
		return -ENODEV;
	}

	return 0;
}

void __exit ip_sx00_platform_uninit(void)
{
	pr_info("%s\n", __func__);

	platform_driver_unregister(&ip_sx00_driver);
}

module_init(ip_sx00_platform_init);
module_exit(ip_sx00_platform_uninit);

MODULE_AUTHOR("Lipeng<lipeng@actions-semi.com>");
MODULE_DESCRIPTION("OWL S900 DE Driver");
MODULE_LICENSE("GPL v2");
