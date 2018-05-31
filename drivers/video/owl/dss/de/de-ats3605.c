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
 *	2015/8/21: Created by Lipeng.
 */
#define DEBUGX
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <video/owl_dss.h>

#include "de-ats3605.h"

/*
 * some useful definition for awlful scaler
 */
#include "de-ats3605.h"

static struct owl_de_device		de_ats3605_device;

/* DE_IRQENABLE cannot be read, use a static variable to hold it */
static uint32_t				irq_enable_val;
static spinlock_t			irq_enable_val_lock;


/*
 * NOTE:
 *	the following macros must be used after
 *	executing 'owl_de_register'
 */
#define de_readl(idx)		readl(de_ats3605_device.base + (idx))
#define de_writel(val, idx)	writel(val, de_ats3605_device.base + (idx))

#define de_debug(format, ...) \
	dev_dbg(&de_ats3605_device.pdev->dev, format, ## __VA_ARGS__)
#define de_info(format, ...) \
	dev_info(&de_ats3605_device.pdev->dev, format, ## __VA_ARGS__)
#define de_err(format, ...) \
	dev_err(&de_ats3605_device.pdev->dev, format, ## __VA_ARGS__)

#define ML_ID(video)			((video)->id / 4)
#define SL_ID(video)			((video)->id % 4)

struct de_ats3605_pdata {
	struct clk			*de1_clk;
	struct clk			*de2_clk;
	struct clk			*de3_clk;
	struct clk			*parent_clk;
};

/*===================================================================
 *			ATS3605 DE path
 *==================================================================*/

static int de_ats3605_path_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path %d enable %d\n", __func__, path->id, enable);

	val = de_readl(DE_PATH_EN(path->id));
	val |= enable;
	de_writel(val, DE_PATH_EN(path->id));

	return 0;
}

static bool de_ats3605_path_is_enabled(struct owl_de_path *path)
{
	uint32_t val;

	val = de_readl(DE_PATH_EN(path->id));

	return REG_GET_VAL(val, DE_PATH_ENABLE_BIT, DE_PATH_ENABLE_BIT) == 1;
}

static int de_ats3605_path_attach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;
	
	/* enable video layer in path */
	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 1,
			  DE_PATH_VIDEO_ENABLE_BIT,
			  DE_PATH_VIDEO_ENABLE_BIT);
	de_writel(val, DE_PATH_CTL(path->id));
	
	return 0;
}

static int de_ats3605_path_detach(struct owl_de_path *path,
			       struct owl_de_video *video)
{
	uint32_t val;

	de_debug("%s, attach video%d to path%d\n",
		 __func__, video->id, path->id);

	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;
	
	/* disable video layer in path */
	val = de_readl(DE_PATH_CTL(path->id));
	val = REG_SET_VAL(val, 0,
			  DE_PATH_VIDEO_ENABLE_BIT,
			  DE_PATH_VIDEO_ENABLE_BIT);
	de_writel(val, DE_PATH_CTL(path->id));
	
	return 0;
}

static int de_ats3605_path_detach_all(struct owl_de_path *path)
{
	int i;
	uint32_t val, val2;

	de_debug("%s, path %d\n", __func__, path->id);
	if (path->info.type == OWL_DISPLAY_TYPE_DUMMY)
		return 0;
	
	for (i = 0; i < 2; i++) {
		/* disable video layer in path */
		val = de_readl(DE_PATH_CTL(i));
		val = REG_SET_VAL(val, 0,
				DE_PATH_VIDEO_ENABLE_BIT,
				DE_PATH_VIDEO_ENABLE_BIT);
		de_writel(val, DE_PATH_CTL(i));
	}

	return 0;
}

static void __path_display_type_set(struct owl_de_path *path,
				    enum owl_display_type type)
{
	uint32_t val;

	de_debug("%s, path %d type %d\n", __func__, path->id, type);

	val = de_readl(DE_OUTPUT_CON);
	de_writel(val, DE_OUTPUT_CON);
}

static void __path_size_set(struct owl_de_path *path,
			    uint32_t width, uint32_t height)
{
	uint32_t val;

	de_debug("%s, path %d %dx%d\n", __func__, path->id, width, height);

	val = REG_VAL(height - 1, DE_PATH_SIZE_HEIGHT_END_BIT,
			  DE_PATH_SIZE_HEIGHT_BEGIN_BIT)
		| REG_VAL(width - 1, DE_PATH_SIZE_WIDTH_END_BIT,
			  DE_PATH_SIZE_WIDTH_BEGIN_BIT);

	de_writel(val, DE_PATH_SIZE(path->id));
}

static void __path_dither_enable(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	de_debug("%s, path %d, enable %d\n", __func__, path->id, enable);

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	val = de_readl(DE_PATH_CTL(path->id));

	val = REG_SET_VAL(val, enable, DE_PATH_DITHER_ENABLE_BIT,
				DE_PATH_DITHER_ENABLE_BIT);

	de_writel(val, DE_PATH_CTL(path->id));
}

static void __path_dither_set(struct owl_de_path *path,
			      enum owl_dither_mode mode)
{
	uint32_t val;

	de_debug("%s, path %d, mode %d\n", __func__, path->id, mode);

	if (path->id != 1)	/* TODO */
		return;

	if (mode == DITHER_DISABLE) {
		__path_dither_enable(path, false);
		return;
	}

	val = de_readl(DE_PATH_CTL(path->id));

	switch (mode) {
	case DITHER_24_TO_18:
		val = REG_SET_VAL(val, 0x7, 14, 12);/* rgb666 */
		break;

	case DITHER_24_TO_16:
		val = REG_SET_VAL(val, 0x2, 14, 12);/* rgb565 */
		break;

	default:
		return;
	}

	de_writel(val, DE_PATH_CTL(path->id));

	__path_dither_enable(path, true);
}

static void __path_vmode_set(struct owl_de_path *path, int vmode)
{
	uint32_t val;

	de_debug("%s, path %d vmode %d\n", __func__, path->id, vmode);

	val = de_readl(DE_PATH_CTL(path->id));

	if (vmode == DSS_VMODE_INTERLACED)
		val = REG_SET_VAL(val, 1, DE_PATH_CTL_INTERLACE_BIT,
				  DE_PATH_CTL_INTERLACE_BIT);
	else
		val = REG_SET_VAL(val, 0, DE_PATH_CTL_INTERLACE_BIT,
				  DE_PATH_CTL_INTERLACE_BIT);
	de_writel(val, DE_PATH_CTL(path->id));

}

static void __path_output_format_set(struct owl_de_path *path, bool is_yuv)
{
	uint32_t val;
	de_debug("%s, path %d format :%s\n",
		__func__, path->id, is_yuv == 1 ? "yuv" : "rgb");
}

static void __path_default_color_set(struct owl_de_path *path, uint32_t color)
{
	de_debug("%s, path %d color %x\n", __func__, path->id, color);

	de_writel(color, DE_PATH_BK(path->id));
}

static void de_ats3605_path_apply_info(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

	de_debug("%s, path%d\n", __func__, path->id);
	
	__path_display_type_set(path, info->type);

	__path_size_set(path, info->width, info->height);

	__path_dither_set(path, info->dither_mode);

	__path_vmode_set(path, info->vmode);

	/* for test */
	__path_default_color_set(path, 0x0);
}

static void de_ats3605_path_set_fcr(struct owl_de_path *path)
{
	uint32_t val;

	de_debug("%s, path%d\n", __func__, path->id);

	val = de_readl(DE_PATH_FCR(path->id));
	val |= 0x1;
	de_writel(val, DE_PATH_FCR(path->id));

	val = de_readl(DE_OUTPUT_STAT);
	if (val != 0) {
		printk(KERN_DEBUG "###DE fifo underflow 0x%x\n", val);
		de_writel(val, DE_OUTPUT_STAT);
	}
}

static bool de_ats3605_path_is_fcr_set(struct owl_de_path *path)
{
	return !!(de_readl(DE_PATH_FCR(path->id)) & (1 << DE_PATH_FCR_BIT));
}

static void de_ats3605_path_preline_enable(struct owl_de_path *path, bool enable)
{
	unsigned long flags;

	de_debug("%s, path%d, enable %d\n", __func__, path->id, enable);

	spin_lock_irqsave(&irq_enable_val_lock, flags);

	if (enable) {
		if (path->id == 0)
			irq_enable_val |= (1 << DE_PATH0_IRQENABLE_BIT);
		else if (path->id == 1)
			irq_enable_val |= (1 <<  DE_PATH1_IRQENABLE_BIT);
	} else {
		if (path->id == 0)
			irq_enable_val &= ~(1 << DE_PATH0_IRQENABLE_BIT);
		else if (path->id == 1)
			irq_enable_val &= ~(1 << DE_PATH1_IRQENABLE_BIT);
	}

	de_writel(irq_enable_val, DE_IRQENABLE);

	spin_unlock_irqrestore(&irq_enable_val_lock, flags);
}

static bool de_ats3605_path_is_preline_enabled(struct owl_de_path *path)
{
	uint32_t val;
	unsigned long flags;

	/*
	 * IC BUG:
	 * can not read 'DE_IRQENABLE'
	 */
	spin_lock_irqsave(&irq_enable_val_lock, flags);
	val = irq_enable_val;
	spin_unlock_irqrestore(&irq_enable_val_lock, flags);

	return !!(val & (1 << (path->id == 0 ? DE_PATH0_IRQENABLE_BIT : DE_PATH1_IRQENABLE_BIT)));
}

static bool de_ats3605_path_is_preline_pending(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & (1 << (path->id == 0 ? DE_PATH0_PENDING_BIT: DE_PATH1_PENDING_BIT)));
}

static void de_ats3605_path_clear_preline_pending(struct owl_de_path *path)
{
	de_writel((1 << (path->id == 0 ? DE_PATH0_PENDING_BIT: DE_PATH1_PENDING_BIT)), DE_IRQSTATUS);
}

static bool de_ats3605_path_is_vb_valid(struct owl_de_path *path)
{
	return !!(de_readl(DE_IRQSTATUS) & (1 << (path->id  == 0 ?  DE_PATH0_VB_BIT :  DE_PATH1_VB_BIT)));
}

static void __path_gamma_set(struct owl_de_path *path, uint32_t *gamma_val)
{
	bool is_busy;
	uint32_t idx, val;

	for (idx = 0; idx < (256 * 3 / 4); idx++) {
		/* write index */
		val = REG_SET_VAL(val, idx, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
				DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);
		de_writel(val, DE_PATH_GAMMA_IDX(path->id));

		/* write ram */
		de_writel(gamma_val[idx], DE_PATH_GAMMA_RAM(path->id));

	}

	/* write finish, clear write bit and index */
	val = de_readl(DE_PATH_GAMMA_IDX(path->id));
	val = REG_SET_VAL(val, 0, DE_PATH_GAMMA_IDX_INDEX_END_BIT,
					DE_PATH_GAMMA_IDX_INDEX_BEGIN_BIT);
	de_writel(val, DE_PATH_GAMMA_IDX(path->id));
}

static void de_ats3605_path_set_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;
	uint8_t gamma_data[256 * 3];
	int is_enabled;
	int i = 0;

	de_debug("gamma_r %d, gamma_g %d, gamma_b %d\n",
		info->gamma_r_val, info->gamma_g_val, info->gamma_b_val);

	if ((info->gamma_r_val < 0) || (info->gamma_g_val < 0)
			|| (info->gamma_b_val < 0))
		de_err("unavailable gamma val!");

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	/* R */
	for (i = 0; i < 256; i++) {
		gamma_data[i] = i * info->gamma_r_val / 100;
	}

	/* G */
	for (i = 0; i < 256; i++) {
		gamma_data[i + 256] = i * info->gamma_g_val / 100;
	}

	/* B */
	for (i = 0; i < 256; i++) {
		gamma_data[i + 256 * 2] = i * info->gamma_b_val / 100;
	}

	__path_gamma_set(path, &gamma_data[0]);

	de_debug("%s, ok!\n", __func__);
}

static bool de_ats3605_path_is_gamma_enabled(struct owl_de_path *path)
{
	uint32_t val;

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	val = de_readl(DE_PATH_GAMMA_ENABLE(path->id));

	return REG_GET_VAL(val, DE_PATH_GAMMA_ENABLE_BIT,
			DE_PATH_GAMMA_ENABLE_BIT) == 1;
}

static int de_ats3605_path_enable_gamma(struct owl_de_path *path, bool enable)
{
	uint32_t val;

	/* only valid for LCD, TODO */
	if (path->id != 1)
		return;

	de_debug("%s, enable %d\n", __func__, enable);

	val = de_readl(DE_PATH_GAMMA_ENABLE(path->id));

	val = REG_SET_VAL(val, enable, DE_PATH_GAMMA_ENABLE_BIT,
			DE_PATH_GAMMA_ENABLE_BIT);

	de_writel(val, DE_PATH_GAMMA_ENABLE(path->id));

}

static void de_ats3605_path_get_gamma_table(struct owl_de_path *path)
{
	struct owl_de_path_info *info = &path->info;

}

static struct owl_de_path_ops de_ats3605_path_ops = {
	.enable = de_ats3605_path_enable,
	.is_enabled = de_ats3605_path_is_enabled,

	.attach = de_ats3605_path_attach,
	.detach = de_ats3605_path_detach,
	.detach_all = de_ats3605_path_detach_all,

	.apply_info = de_ats3605_path_apply_info,

	.set_fcr = de_ats3605_path_set_fcr,
	.is_fcr_set = de_ats3605_path_is_fcr_set,

	.preline_enable = de_ats3605_path_preline_enable,
	.is_preline_enabled = de_ats3605_path_is_preline_enabled,
	.is_preline_pending = de_ats3605_path_is_preline_pending,
	.clear_preline_pending = de_ats3605_path_clear_preline_pending,

	.set_gamma_table = de_ats3605_path_set_gamma_table,
	.get_gamma_table = de_ats3605_path_get_gamma_table,
	.gamma_enable = de_ats3605_path_enable_gamma,
	.is_gamma_enabled = de_ats3605_path_is_gamma_enabled,

	.is_vb_valid = de_ats3605_path_is_vb_valid,
};

static struct owl_de_path de_ats3605_paths[] = {
	{
		.id			= 0,
		.name			= "hdmi",
		.supported_displays	= OWL_DISPLAY_TYPE_HDMI,
		.ops			= &de_ats3605_path_ops,
	},
	{
		.id			= 1,
		.name			= "lcd",
		.supported_displays	= OWL_DISPLAY_TYPE_LCD
					| OWL_DISPLAY_TYPE_DUMMY,
		.ops			= &de_ats3605_path_ops,
	},
};


/*===================================================================
 *			ATS3605 DE video layer
 *==================================================================*/
static int __de_mmu_enable(struct owl_de_video *video)
{
	uint32_t val;

	struct owl_de_video_info *info = &video->info;
	return 0;
}

static int __de_color_mode_to_hw_mode(enum owl_color_mode color_mode)
{
	int hw_format = 0;

	switch (color_mode) {
	case OWL_DSS_COLOR_BGR565:
		hw_format = 0;
		break;

	case OWL_DSS_COLOR_RGBA1555:
		hw_format = 4;
		break;

	case OWL_DSS_COLOR_BGRA8888:
		hw_format = 1;
		break;

	case OWL_DSS_COLOR_RGBA8888:
		hw_format = 5;
		break;

	default:
		BUG();
		break;
	}

	return hw_format;
}

static void __video_fb_addr_set(struct owl_de_video *video)
{
	struct owl_de_video_info *info = &video->info;
	/*
	 * set fb addr according to color mode.
	 */
	if (owl_dss_color_is_rgb(info->color_mode)) {
		de_writel(info->addr[0], DE_VIDEO_FB(video->id, 0));
		de_debug("%s, paddr 0x%x\n", __func__, info->addr[0]);
		return;
	}

	/*
	 * YUV
	 */
	if (info->color_mode == OWL_DSS_COLOR_YUV420) {
		/* OWL_DSS_COLOR_YVU420(DRM_FORMAT_YVU420), U/V switch. */
		de_writel(info->addr[0], DE_VIDEO_FB(video->id, 0));
		de_writel(info->addr[2], DE_VIDEO_FB(video->id, 1));
		de_writel(info->addr[1], DE_VIDEO_FB(video->id, 2));
	} else if (info->color_mode == OWL_DSS_COLOR_NV12) {
		de_writel(info->addr[0], DE_VIDEO_FB(video->id, 0));
		de_writel(info->addr[1], DE_VIDEO_FB(video->id, 1));
	}

	de_debug("%s, paddr 0x%x\n", __func__, info->addr[0]);
	de_debug("%s, paddr 0x%x\n", __func__, info->addr[1]);
	de_debug("%s, paddr 0x%x\n", __func__, info->addr[2]);

}

static void __video_pitch_set(struct owl_de_video *video)
{
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	/* stride val requested double word(8 bytes) in length for IC ats3605 DE */
	if (owl_dss_color_is_rgb(info->color_mode)) {
		val = info->pitch[0] / 8; 
		de_writel(val, DE_VIDEO_STR(video->id));
	} else {
		val = info->pitch[0] / 8 | (info->pitch[1] / 8 << 11) | (info->pitch[2] / 8 << 21);
		de_writel(val, DE_VIDEO_STR(video->id));
	}
}

static void __video_format_set(struct owl_de_video *video)
{
	int hw_format = 0;
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	hw_format = __de_color_mode_to_hw_mode(info->color_mode);
	de_debug("%s, color_mode = 0x%x, hw_format = 0x%x\n",
		 __func__, info->color_mode, hw_format);

	val = de_readl(DE_VIDEO_CFG(video->id));
	val |= 0x1 << 26;/* video critial ctl signal */
	val = REG_SET_VAL(val, hw_format, 2, 0);
	de_writel(val, DE_VIDEO_CFG(video->id));

	/*
	 * config YUV convert format
	 */
}

static void __video_rotate_set(struct owl_de_video *video)
{
	int rotation;
	uint32_t val;
	struct owl_de_video_info *info = &video->info;

	rotation = info->rotation;

	de_debug("%s, video %d, rotation %d\n", __func__, video->id, rotation);
}

static void __video_crop_set(struct owl_de_video *video)
{
	uint32_t val;

	int input_width, input_height;

	struct owl_de_video_info *info = &video->info;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer:
		 * Set macro layer size to crop size.
		 */
		input_width = info->width;
		input_height = info->height;
	} else {
		/*
		 * RGB layers share one macro layer:
		 * Set macro layer size to draw size(TODO, maybe improper).
		 */
		owl_panel_get_draw_size(video->path->current_panel,
					&input_width, &input_height);
	}

	de_debug("%s, video %d, input size %dx%d\n", __func__,
		 video->id, input_width, input_height);

	/*TODO*/
	de_writel(((input_height -1) << 16) | (input_width  - 1), DE_VIDEO_ISIZE(video->id));
}

static void __video_display_set(struct owl_de_video *video)
{
	uint32_t val;

	int a_x, a_y;
	int out_width, out_height;

	struct owl_de_video_info *info = &video->info;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer:
		 *
		 * Set sublayer's coordinate in macrolayer to (0,0);
		 *
		 * Set macro layer coordinate in path to (pos_x,pos_y);
		 *
		 * Set scaler output size to out_width,out_height.
		 */
		a_x = info->pos_x;
		a_y = info->pos_y;

		out_width = (info->out_width == 0 ?
			     info->width : info->out_width);
		out_height = (info->out_height == 0 ?
			      info->height : info->out_height);
	} else {
		/*
		 * RGB layers share one macro layer:
		 *
		 * Set sublayer's coordinate in macrolayer to
		 * real coordinate before scaling, if there is no scaling,
		 * real coordinate is equal to original coordinate;
		 *
		 * Set macro layer coordinate in path to (disp_x,disp_y),
		 * if there is no scaling, (disp_x,disp_y) = (0,0);
		 *
		 * Set scaler output size to disp_width,disp_height,
		 * if there is no scaling, it will be skipped;
		 */
		owl_panel_get_disp_area(video->path->current_panel, &a_x, &a_y,
					&out_width, &out_height);
	}

	BUG_ON((out_width > DE_PATH_SIZE_WIDTH) ||
	       (out_height > DE_PATH_SIZE_HEIGHT));

	val = REG_VAL(a_y, DE_PATH_A_COOR_Y_END_BIT,
		           DE_PATH_A_COOR_Y_BEGIN_BIT)
		| REG_VAL(a_x, DE_PATH_A_COOR_X_END_BIT,
		  	       DE_PATH_A_COOR_X_BEGIN_BIT);
	de_writel(val, DE_PATH_A_COOR(video->id));
	
	val = (out_height - 1) << 16 | (out_width - 1);
	de_writel(val, DE_VIDEO_OSIZE(video->id));
}

static void __video_set_scal_coef(struct owl_de_video *video, uint8_t scale_mode)
{
	struct owl_de_path *path = video->path;
	de_debug("%s, scale_mode %d\n", __func__, scale_mode);

	switch (scale_mode) {
	case DE_SCLCOEF_ZOOMIN:
		de_writel(0X00004000, DE_VIDEO_SCOEF(video->id, 0));
		de_writel(0xFF073EFC, DE_VIDEO_SCOEF(video->id, 1));
		de_writel(0xFE1038FA, DE_VIDEO_SCOEF(video->id, 2));
		de_writel(0xFC1B30F9, DE_VIDEO_SCOEF(video->id, 3));
		de_writel(0xFA2626FA, DE_VIDEO_SCOEF(video->id, 4));
		de_writel(0xF9301BFC, DE_VIDEO_SCOEF(video->id, 5));
		de_writel(0xFA3810FE, DE_VIDEO_SCOEF(video->id, 6));
		de_writel(0xFC3E07FF, DE_VIDEO_SCOEF(video->id, 7));
		break;

	case DE_SCLCOEF_HALF_ZOOMOUT:
		de_writel(0x00004000, DE_VIDEO_SCOEF(video->id, 0));
		de_writel(0x00083800, DE_VIDEO_SCOEF(video->id, 1));
		de_writel(0x00103000, DE_VIDEO_SCOEF(video->id, 2));
		de_writel(0x00182800, DE_VIDEO_SCOEF(video->id, 3));
		de_writel(0x00202000, DE_VIDEO_SCOEF(video->id, 4));
		de_writel(0x00281800, DE_VIDEO_SCOEF(video->id, 5));
		de_writel(0x00301000, DE_VIDEO_SCOEF(video->id, 6));
		de_writel(0x00380800, DE_VIDEO_SCOEF(video->id, 7));
		break;

	case DE_SCLCOEF_SMALLER_ZOOMOUT:
		de_writel(0X00102010, DE_VIDEO_SCOEF(video->id, 0));
		de_writel(0X02121E0E, DE_VIDEO_SCOEF(video->id, 1));
		de_writel(0X04141C0C, DE_VIDEO_SCOEF(video->id, 2));
		de_writel(0X06161A0A, DE_VIDEO_SCOEF(video->id, 3));
		de_writel(0X08181808, DE_VIDEO_SCOEF(video->id, 4));
		de_writel(0X0A1A1606, DE_VIDEO_SCOEF(video->id, 5));
		de_writel(0X0C1C1404, DE_VIDEO_SCOEF(video->id, 6));
		de_writel(0X0E1E1202, DE_VIDEO_SCOEF(video->id, 7));
		break;

	default:
		BUG();
	}
}

static void __video_scaling_set(struct owl_de_video *video)
{
	uint8_t scale_mode;
	uint16_t w_factor, h_factor;
	uint16_t factor;
	uint32_t val;

	int width, height, out_width, out_height;
	int disp_x, disp_y;

	struct owl_de_video_info *info = &video->info;

	if (!owl_dss_color_is_rgb(info->color_mode) ||
	    info->is_original_scaled) {
		/*
		 * YUV layer or original scaled, monopolize a single macrolayer.
		 */
		width = info->width;
		height = info->height;

		out_width = (info->out_width == 0 ?
			     info->width : info->out_width);
		out_height = (info->out_height == 0 ?
			      info->height : info->out_height);
	} else {
		/* RGB layers share one macro layer(TODO, maybe improper) */
		owl_panel_get_draw_size(video->path->current_panel, &width, &height);

		owl_panel_get_disp_area(video->path->current_panel, &disp_x, &disp_y,
					&out_width, &out_height);
	}

	de_debug("%s, video %d, %dx%d->%dx%d\n", __func__,
		 video->id, width, height, out_width, out_height);

	w_factor = (width * 8192 + out_width - 1) / out_width;
	h_factor = (height * 8192 + out_height - 1) / out_height;

	val = h_factor << 16 | w_factor;
	de_writel(val, DE_VIDEO_SR(video->id));

	factor = (width * height * 10) / (out_width * out_height);
	if (factor <= 10)
		scale_mode = DE_SCLCOEF_ZOOMIN;
	else if (factor <= 40)
		scale_mode = DE_SCLCOEF_HALF_ZOOMOUT;
	else if (factor > 40)
		scale_mode = DE_SCLCOEF_SMALLER_ZOOMOUT;

	__video_set_scal_coef(video, scale_mode);
}

static void __video_alpha_set(struct owl_de_video *video)
{
	enum owl_blending_type blending;
	uint8_t alpha;

	uint32_t val;

	struct owl_de_video_info *info = &video->info;
	
	blending = info->blending;
	alpha = info->alpha;

	de_debug("%s, blending %d, alpha %d\n", __func__, blending, alpha);
}

static void __video_csc_set(struct owl_de_video *video)
{
	uint32_t val;

	uint32_t bri, con, sat;

	struct owl_de_video_info *info = &video->info;

	/* 0~200-->0~255 */
	bri = (info->brightness * 255 + 100) / 200;
	if (bri > 255)
		bri = 255;

	/* 0~200-->0~14 */
	con = (info->contrast * 14 + 100) / 200;
	if (con > 14)
		con = 14;

	sat = (info->saturation * 14 + 100) / 200;
	if (sat > 14)
		sat = 14;

	val = de_readl(DE_VIDEO_CFG(video->id));
	val = REG_SET_VAL(val, bri, 19, 12);
	val = REG_SET_VAL(val, sat, 11, 8);
	val = REG_SET_VAL(val, con, 7, 4);
	de_writel(val, DE_VIDEO_CFG(video->id));
};

static void de_ats3605_video_apply_info(struct owl_de_video *video)
{
	de_debug("%s, video %d, dirty %d\n", __func__, video->id, video->dirty);

	__de_mmu_enable(video);

	__video_fb_addr_set(video);
	__video_pitch_set(video);
	__video_format_set(video);
	__video_rotate_set(video);

	__video_crop_set(video);
	__video_display_set(video);

	__video_scaling_set(video);

	__video_alpha_set(video);

	__video_csc_set(video);
}

static struct owl_de_video_ops de_ats3605_video_ops = {
	.apply_info = de_ats3605_video_apply_info,
};

#define DE_ats3605_SUPPORTED_COLORS (OWL_DSS_COLOR_BGR565 | OWL_DSS_COLOR_BGRA8888 \
			| OWL_DSS_COLOR_RGBA8888 \
			| OWL_DSS_COLOR_YUV420 | OWL_DSS_COLOR_NV12 \
			| OWL_DSS_COLOR_RGBA1555)

#define DE_ats3605_CAPACITIES {					\
	.supported_colors	= DE_ats3605_SUPPORTED_COLORS,	\
	.pitch_align		= 1,				\
	.address_align		= 1,				\
	.rgb_limits = {						\
		.input_width	= {1, 4096},			\
		.input_height	= {1, 4096},			\
		.output_width	= {1, 4096},			\
		.output_height	= {1, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
	.yuv_limits = {						\
		.input_width	= {2, 4096},			\
		.input_height	= {2, 4096},			\
		.output_width	= {2, 4096},			\
		.output_height	= {2, 4096},			\
		.scaling_width	= {80, 80},			\
		.scaling_height	= {80, 80},			\
	},							\
}


/* need 2 video layers at most in boot */
static struct owl_de_video de_ats3605_videos[] = {
	/* video layer 0 */
	{
		.id			= 0,
		.sibling		= 0xf,
		.name			= "video0",
		.ops			= &de_ats3605_video_ops,
	},
	/* video layer 1 */
	{
		.id			= 1,
		.sibling		= 0xf,
		.name			= "video1",
		.ops			= &de_ats3605_video_ops,
	},
};


/*===================================================================
 *			ATS3605 DE video layer
 *==================================================================*/

static int de_ats3605_device_power_on(struct owl_de_device *de)
{
	uint32_t val;
	void __iomem *shareram = ioremap(0xB0200004, 4);

	struct de_ats3605_pdata *pdata = de->pdata;

	de_info("%s\n", __func__);

	clk_set_parent(pdata->de1_clk, pdata->parent_clk);
	clk_set_parent(pdata->de2_clk, pdata->parent_clk);
	clk_set_parent(pdata->de3_clk, pdata->parent_clk);
	
	clk_set_rate(pdata->de1_clk, 360000000);
	clk_set_rate(pdata->de2_clk, 360000000);
	clk_set_rate(pdata->de3_clk, 360000000);

	clk_prepare_enable(pdata->de1_clk);
	clk_prepare_enable(pdata->de2_clk);
	clk_prepare_enable(pdata->de3_clk);

	/*
	 * some specail settings
	 */

	/*
	 * set the sharesram control mode: de
	 * */
	/* enable share mem */
	val = readl(shareram);
	val = REG_SET_VAL(val, 0x1, 0, 0);
	writel(val, shareram);

	return 0;
}

static int de_ats3605_device_power_off(struct owl_de_device *de)
{
	struct de_ats3605_pdata *pdata = de->pdata;

	de_info("%s\n", __func__);
	
	clk_disable_unprepare(pdata->de1_clk);
	clk_disable_unprepare(pdata->de2_clk);
	clk_disable_unprepare(pdata->de3_clk);
	
	return 0;
}

static int de_ats3605_device_mmu_config(struct owl_de_device *de,
				     uint32_t base_addr)
{
	de_debug("%s\n", __func__);
	/* no mmu support for de ats3605 */
	return 0;
}

static void de_ats3605_device_dump_regs(struct owl_de_device *de)
{
	int i, j;

#define DUMPREG(r) de_info("%08x ~ %08x ~ %s\n", r, de_readl(r), #r)
	/*
	 * do not dump DE_IRQENABLE for ats3605, please see
	 * 'de_ats3605_path_is_preline_enabled' for detail
	 */
	DUMPREG(DE_IRQENABLE);
	DUMPREG(DE_IRQSTATUS);
	DUMPREG(DE_OUTPUT_CON);
	DUMPREG(DE_OUTPUT_STAT);

	for (i = 0; i < 2; i++) {
		pr_info("\npath %d ------------------>\n", i);
		DUMPREG(DE_PATH_CTL(i));
		DUMPREG(DE_PATH_FCR(i));
		DUMPREG(DE_PATH_EN(i));
		DUMPREG(DE_PATH_BK(i));
		DUMPREG(DE_PATH_SIZE(i));
		DUMPREG(DE_PATH_A_COOR(i));
		DUMPREG(DE_PATH_GAMMA_IDX(i));
	}

	for (i = 0; i < 2; i++) {
		pr_info("\nlayer %d ------------------>\n", i);
		DUMPREG(DE_VIDEO_CFG(i));
		DUMPREG(DE_VIDEO_ISIZE(i));
		DUMPREG(DE_VIDEO_OSIZE(i));
		DUMPREG(DE_VIDEO_FB(i, 0));
		DUMPREG(DE_VIDEO_FB(i, 1));
		DUMPREG(DE_VIDEO_FB(i, 2));
		DUMPREG(DE_VIDEO_STR(i));
	}

	for (i = 0; i < 2; i++) {
		pr_info("\nscaler %d ------------------>\n", i);
		DUMPREG(DE_VIDEO_SR(i));
		DUMPREG(DE_VIDEO_SCOEF(i, 0));
		DUMPREG(DE_VIDEO_SCOEF(i, 1));
		DUMPREG(DE_VIDEO_SCOEF(i, 2));
		DUMPREG(DE_VIDEO_SCOEF(i, 3));
		DUMPREG(DE_VIDEO_SCOEF(i, 4));
		DUMPREG(DE_VIDEO_SCOEF(i, 5));
		DUMPREG(DE_VIDEO_SCOEF(i, 6));
		DUMPREG(DE_VIDEO_SCOEF(i, 7));
	}
#undef DUMPREG

	pr_info("\n");

	pr_info("%08x %08x\n", de_readl(DE_PATH_CTL(0)),
		de_readl(DE_PATH_CTL(1)));
}

static inline void __de_ats3605_backup_regs(struct owl_de_device_regs *p, int reg)
{
	p->reg      = reg;
	p->value    = de_readl(reg);
}

static void de_ats3605_device_backup_regs(struct owl_de_device *de)
{
	int i, j, cnt = 0;
	struct owl_de_device_regs *regs = de->regs;

	de_info("%s\n", __func__);

	/*
	 * do not backup DE_IRQENABLE for ats3605, please see
	 * 'de_ats3605_path_is_preline_enabled' for detail
	 */
	__de_ats3605_backup_regs(&regs[cnt++], DE_IRQSTATUS);
	__de_ats3605_backup_regs(&regs[cnt++], DE_OUTPUT_CON);

	for (i = 0; i < 2; i++) {
		__de_ats3605_backup_regs(&regs[cnt++], DE_PATH_CTL(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_PATH_EN(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_PATH_BK(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_PATH_SIZE(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_PATH_GAMMA_IDX(i));
	}

	for (i = 0; i < 2; i++) {
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_CFG(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_ISIZE(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_OSIZE(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_FB(i, 0));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_FB(i, 1));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_FB(i, 2));
		
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_STR(i));
	}

	for (i = 0; i < 2; i++) {
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SR(i));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 0));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 1));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 2));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 3));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 4));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 5));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 6));
		__de_ats3605_backup_regs(&regs[cnt++], DE_VIDEO_SCOEF(i, 7));
	}

	de->regs_cnt = cnt;
}


static void de_ats3605_device_restore_regs(struct owl_de_device *de)
{
	int i;

	de_info("%s\n", __func__);
	for (i = 0; i < de->regs_cnt; i++)
		de_writel(de->regs[i].value, de->regs[i].reg);
}

static struct owl_de_device_ops de_ats3605_device_ops = {
	.power_on = de_ats3605_device_power_on,
	.power_off = de_ats3605_device_power_off,

	.mmu_config = de_ats3605_device_mmu_config,

	.dump_regs = de_ats3605_device_dump_regs,
	.backup_regs = de_ats3605_device_backup_regs,
	.restore_regs = de_ats3605_device_restore_regs,
};

static struct owl_de_device de_ats3605_device = {
	.hw_id			= DE_HW_ID_ATS3605,

	.num_paths		= ARRAY_SIZE(de_ats3605_paths),
	.paths			= de_ats3605_paths,

	.num_videos		= ARRAY_SIZE(de_ats3605_videos),
	.videos			= de_ats3605_videos,

	.ops			= &de_ats3605_device_ops,
};


/*============================================================================
 *			platform driver
 *==========================================================================*/

static const struct of_device_id de_ats3605_match[] = {
	{
		.compatible	= "actions,ats3605-de",
	},
};

static int __init de_ats3605_probe(struct platform_device *pdev)
{
	int				ret = 0;

	struct device			*dev = &pdev->dev;
	const struct of_device_id	*match;

	struct de_ats3605_pdata		*pdata;

	dev_info(dev, "%s, pdev = 0x%p\n", __func__, pdev);

	match = of_match_device(of_match_ptr(de_ats3605_match), dev);
	if (!match) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	de_ats3605_device.pdev = pdev;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		dev_err(dev, "No Mem for pdata\n");
		return -ENOMEM;
	}

	de_ats3605_device.regs = devm_kzalloc(dev,
				sizeof(*de_ats3605_device.regs) * 256, GFP_KERNEL);
	if (de_ats3605_device.regs == NULL) {
		dev_err(dev, "No Mem for de_ats3605_device.regs\n");
		return -ENOMEM;
	}

	/*
	 * parse our special resources
	 */
	pdata->de1_clk = devm_clk_get(dev, "clk_de1");
	pdata->de2_clk = devm_clk_get(dev, "clk_de2");
	pdata->de3_clk = devm_clk_get(dev, "clk_de3");
	/* display_pll or dev_clk ???*/
	pdata->parent_clk = devm_clk_get(dev, "clk_parent"); 
	if (IS_ERR(pdata->de1_clk) || IS_ERR(pdata->de2_clk) ||
	    IS_ERR(pdata->de3_clk) || IS_ERR(pdata->parent_clk)) {
		dev_err(dev, "can't get de1 clk, de2 clk de3 or de parent_clk\n");
		return -EINVAL;
	}

	de_ats3605_device.pdata = pdata;

	ret = owl_de_register(&de_ats3605_device);
	if (ret < 0) {
		dev_err(dev, "register ats3605 de device failed(%d)\n", ret);
		return ret;
	}

	spin_lock_init(&irq_enable_val_lock);

	return 0;
}

static int __exit de_ats3605_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s, pdev = 0x%p\n", __func__, pdev);

	/* TODO */
	return 0;
}

static int de_ats3605_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_suspend(dev);

	return 0;
}

static int de_ats3605_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	/* you can do something special */

	owl_de_generic_resume(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(de_ats3605_pm_ops, de_ats3605_suspend, de_ats3605_resume);

static struct platform_driver de_ats3605_platform_driver = {
	.probe			= de_ats3605_probe,
	.remove			= __exit_p(de_ats3605_remove),
	.driver = {
		.name		= "de-ats3605",
		.owner		= THIS_MODULE,
		.of_match_table	= de_ats3605_match,
		.pm		= &de_ats3605_pm_ops,
	},
};

static int __init owl_de_ats3605_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&de_ats3605_platform_driver);
	if (ret) {
		pr_err("Failed to register platform driver\n");
		return ret;
	}

	return 0;
}

static void __exit owl_de_ats3605_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&de_ats3605_platform_driver);
}

module_init(owl_de_ats3605_init);
module_exit(owl_de_ats3605_exit);

MODULE_AUTHOR("huanghaiyu<huanghaiyu@actions-semi.com>");
MODULE_DESCRIPTION("OWL ats3605 DE Driver");
MODULE_LICENSE("GPL v2");
