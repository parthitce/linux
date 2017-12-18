/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#define DRM_DEBUG_CODE
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <video/of_display_timing.h>
#include <drm/owl_drm.h>
#include <video/display_timing.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include "owl_drm_drv.h"
#include "owl_drm_fbdev.h"
#include "owl_drm_crtc.h"
#include "owl_drm_hdmi.h"
#include "owl_drm_iommu.h"

static struct device *g_dev = NULL;
static bool hdmi_display_is_connected(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
 	bool is_connected;
	is_connected = owl_panel_hpd_is_connected(ctx->panel);

	DRM_DEBUG_KMS("is connected %d\n", is_connected);
	return is_connected;
}

static void *hdmi_get_panel(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	DRM_DEBUG_KMS("\n");

	return ctx->panel;
}

static int hdmi_check_timing(struct device *dev, void *timing)
{
	DRM_DEBUG_KMS("\n");

	/* TODO. */

	return 0;
}

static int hdmi_display_power_on(struct device *dev, int mode)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	struct owl_panel *panel = ctx->panel;

	DRM_DEBUG_KMS("mode %d\n", mode);

	switch (mode) {
		case DRM_MODE_DPMS_ON:
			owl_panel_enable(panel);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:
			owl_panel_disable(panel);
			break;
		default:
			DRM_DEBUG_KMS("unspecified mode %d\n", mode);
			break;
	}


	return 0;
}

struct edid *hdmi_get_edid(struct device *dev,
				struct drm_connector *connector)
{

}

static void *hdmi_get_modelist(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	return ctx->panel;
}

static struct owl_drm_display_ops hdmi_display_ops = {
	.type 		= OWL_DRM_DISPLAY_TYPE_HDMI,
	.is_connected 	= hdmi_display_is_connected,
	.get_panel 	= hdmi_get_panel,
	.check_timing 	= hdmi_check_timing,
	.power_on 	= hdmi_display_power_on,
	/* .get_edid 	= hdmi_get_edid, */
	/* .get_modelist = hdmi_get_modelist, */
};

static void hdmi_path_dpms(struct device *subdrv_dev, int mode)
{
	struct hdmi_context *ctx = get_hdmi_context(subdrv_dev);

	struct owl_de_path *path = ctx->path;

	DRM_DEBUG_KMS("path %d mod %d\n", path->id, mode);

	mutex_lock(&ctx->lock);

	switch (mode) {
		case DRM_MODE_DPMS_ON:
			/*
			 * enable primary hardware only if suspended status.
			 *
			 * P.S. hdmi_dpms function would be called at booting time so
			 * clk_enable could be called double time.
			 */

			owl_de_path_enable(path);

			if (ctx->suspended)
				pm_runtime_get_sync(subdrv_dev);
			break;
		case DRM_MODE_DPMS_STANDBY:
		case DRM_MODE_DPMS_SUSPEND:
		case DRM_MODE_DPMS_OFF:

			owl_de_path_disable(path);

			if (!ctx->suspended)
				pm_runtime_put_sync(subdrv_dev);
			break;
		default:
			DRM_DEBUG_KMS("unspecified mode %d\n", mode);
			break;
	}

	mutex_unlock(&ctx->lock);
}

static void hdmi_path_mode_set(struct device *subdrv_dev)
{
	struct hdmi_context *ctx  = get_hdmi_context(subdrv_dev);
	struct owl_de_path *path = ctx->path;
	struct owl_panel *panel  = ctx->panel;
	struct owl_de_path_info p_info;
	int i;

	DRM_DEBUG_KMS("do nothing, fixme\n");

}

static void hdmi_path_apply(struct device *subdrv_dev)
{
	struct hdmi_context *ctx = get_hdmi_context(subdrv_dev);
	struct owl_de_path *path = ctx->path;
	int i;
	DRM_DEBUG_KMS("path %d\n", path->id);

}

static void hdmi_path_prepare(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	struct owl_de_path *path = ctx->path;

	DRM_DEBUG_KMS(" do nothing\n");
}

static void hdmi_path_commit(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	struct owl_de_path *path = ctx->path;

	DRM_DEBUG_KMS("do nothing, fixme\n");

	if (ctx->suspended)
		return;
}

static int hdmi_path_enable_vblank(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	struct owl_de_path *path = ctx->path;

	DRM_DEBUG_KMS("path %d\n", path->id);

	if (ctx->suspended)
		return -EPERM;

	owl_de_path_enable_vsync(path);

	return 0;
}

static void hdmi_path_disable_vblank(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	struct owl_de_path *path = ctx->path;

	DRM_DEBUG_KMS("path %d\n", path->id);

	if (ctx->suspended)
		return;

	owl_de_path_disable_vsync(path);
}

static void hdmi_path_wait_for_vblank(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	/*
	 * wait for HDMI to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				DRM_HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");
}

static void hdmi_hotplug_event_cb(struct owl_panel *panel, void *data,
		u32 value)
{
	DRM_DEBUG_KMS("\n");

	struct hdmi_context 	*ctx = data;
	struct owl_drm_subdrv 	*subdrv = &ctx->subdrv;
	struct owl_drm_manager 	*manager = subdrv->manager;
	struct drm_device 	*drm_dev = subdrv->drm_dev;

	drm_helper_hpd_irq_event(drm_dev);
}

static void hdmi_vsync_event_cb(struct owl_panel *panel, void *data,
		u32 value)
{
	struct hdmi_context *ctx 	= data;
	struct owl_drm_subdrv *subdrv 	= &ctx->subdrv;
	struct owl_drm_manager *manager = subdrv->manager;
	struct drm_device *drm_dev 	= subdrv->drm_dev;

	ktime_t stamp = ktime_get();
	int duration, deviation;
	/* check the crtc is detached already from encoder */
	if (manager->pipe < 0)
		return;

	drm_handle_vblank(drm_dev, manager->pipe);
	owl_drm_crtc_finish_pageflip(drm_dev, manager->pipe);

	/* set wait vsync event to zero and wake up queue. */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		DRM_WAKEUP(&ctx->wait_vsync_queue);
	}
	duration = ktime_to_us(ktime_sub(stamp, ctx->pre_vsync_stamp));
	deviation = duration - ctx->pre_vsync_duration_us;

	#if 0
	if (deviation != 0)
		DRM_DEBUG_KMS(" duration %dus, deviation %dus\n", duration, deviation);
	#endif
	ctx->pre_vsync_stamp = stamp;
	ctx->pre_vsync_duration_us = duration;
}

static struct owl_drm_manager_ops hdmi_manager_ops = {
	.dpms 			= hdmi_path_dpms,
	.prepare 		= hdmi_path_prepare,

	.mode_set 		= hdmi_path_mode_set,
	.commit 		= hdmi_path_commit,

	.enable_vblank 		= hdmi_path_enable_vblank,
	.disable_vblank 	= hdmi_path_disable_vblank,
	.wait_for_vblank 	= hdmi_path_wait_for_vblank,
};

enum owl_color_mode hdmi_drm_format_to_owl_format(u32 color)
{
	enum owl_color_mode owl_color;
	/*TODO*/
	owl_color = OWL_DSS_COLOR_BGRX8888;
	return owl_color;

	switch (color) {
		case DRM_FORMAT_RGB565:
			owl_color = OWL_DSS_COLOR_RGB565;
			break;

		case DRM_FORMAT_BGR565:
			owl_color = OWL_DSS_COLOR_BGR565;
			break;

		case DRM_FORMAT_BGRA8888:
			owl_color = OWL_DSS_COLOR_BGRA8888;
			break;
		case DRM_FORMAT_BGRX8888:
			owl_color = OWL_DSS_COLOR_BGRX8888;
			break;

		case DRM_FORMAT_RGBA8888:
			owl_color = OWL_DSS_COLOR_RGBA8888;
			break;
		case DRM_FORMAT_RGBX8888:
			owl_color = OWL_DSS_COLOR_RGBX8888;
			break;

		case DRM_FORMAT_ABGR8888:
			owl_color = OWL_DSS_COLOR_ABGR8888;
			break;
		case DRM_FORMAT_XBGR8888:
			owl_color = OWL_DSS_COLOR_XBGR8888;
			break;

		case DRM_FORMAT_ARGB8888:
			owl_color = OWL_DSS_COLOR_ARGB8888;
			break;
		case DRM_FORMAT_XRGB8888:
			owl_color = OWL_DSS_COLOR_XRGB8888;
			break;

			/* Y VU */
		case DRM_FORMAT_NV12:
			owl_color = OWL_DSS_COLOR_NV12;
			break;
			/* Y UV */
		case DRM_FORMAT_NV21:
			owl_color = OWL_DSS_COLOR_NV21;
			break;
			/* Y V U */
		case DRM_FORMAT_YVU420:
			owl_color = OWL_DSS_COLOR_YVU420;
			break;
			/* Y U V */
		case DRM_FORMAT_YUV420:
			owl_color = OWL_DSS_COLOR_YUV420;
			break;

		default:
			BUG();
			break;
	}

	return owl_color;
}

static int overlay_info_to_video_info(struct owl_panel *panel,
		struct owl_drm_overlay *overlay,
		struct owl_de_video_info *info)
{
	u8 x_subsampling, y_subsampling;
	u32 paddr;
	unsigned long offset;

	int plane, ret = 0;
	int draw_width, draw_height;
	int disp_x, disp_y, disp_width, disp_height; /* real display area */

	owl_panel_get_draw_size(panel, &draw_width, &draw_height);
	owl_panel_get_disp_area(panel, &disp_x, &disp_y,
			&disp_width, &disp_height);

	DRM_DEBUG_KMS("disp size(%dx%d), draw size(%dx%d)\n",
			disp_width, disp_height, draw_width, draw_height);
	DRM_DEBUG_KMS("disp area(%d,%d %dx%d)\n",
			disp_x, disp_y, disp_width, disp_height);

	DRM_DEBUG_KMS("crop: (l,t,r,b) --> display: (l,t,r,b)\n");
	DRM_DEBUG_KMS("%d,%d~%d,%d --> %d,%d~%d,%d\n",
			overlay->fb_x, overlay->fb_y,
			overlay->src_width, overlay->src_height,
			overlay->crtc_x, overlay->crtc_y,
			overlay->crtc_x + overlay->crtc_width,
			overlay->crtc_y + overlay->crtc_height);

	info->xoff = overlay->fb_x;
	info->yoff = overlay->fb_y;
	info->width = overlay->src_width;
	info->height = overlay->src_height;

	info->pos_x = overlay->crtc_x;
	info->pos_y = overlay->crtc_y;
	info->out_width = overlay->crtc_width;
	info->out_height = overlay->crtc_height;

	if (info->width != info->out_width || info->height != info->out_height)
		info->is_original_scaled = true;
	else
		info->is_original_scaled = false;

	DRM_DEBUG_KMS(" is_original_scaled %d\n", info->is_original_scaled);

	/*
	 * caculate real position without scaling
	 */
	info->real_pos_x = (info->pos_x == 0 ? 0 : (info->pos_x - (info->width - info->out_width) / 2));
	info->real_pos_x = (info->real_pos_x < 0 ? 0 : info->real_pos_x);

	info->real_pos_y = (info->pos_y == 0 ? 0 : (info->pos_y - (info->height - info->out_height) / 2));
	info->real_pos_y = (info->real_pos_y < 0 ? 0 : info->real_pos_y);

	DRM_DEBUG_KMS("original area-->\n");
	DRM_DEBUG_KMS("(pos_x,pos_y/real_pos_x,real_pos_y WidthxHeight)\n");
	DRM_DEBUG_KMS("(%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)\n",
			info->xoff, info->yoff, info->width, info->height,
			info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
			info->out_width, info->out_height);

	/*
	 * Adjust with draw size and display area size.
	 *
	 * There are 2 rectangles in the coordinate system, one is
	 * DRAW rectangle, denoted by (0,0 draw_width,draw_height),
	 * another is DISPLAY rectangle, denoted by
	 * (disp_x,disp_y disp_width,disp_height).
	 *
	 * Now, we need convert the target rectangle
	 * (pos_x,pos_y out_width,out_height) from DRAW rectangle to
	 * DISPLAY rectangle, the algorithm is:
	 * 	out_width / draw_width = out_width_new / disp_width,
	 * 	out_height / draw_height = out_height_new / disp_height,
	 *	(pos_x - 0) / draw_width = (pso_x_new - disp_x) / disp_width
	 *	(pos_y - 0) / draw_height = (pso_y_new - disp_y) / disp_height
	 */
#define DIV_ROUND(x, y) (((x) + ((y) / 2)) / y)
	info->pos_x = DIV_ROUND(info->pos_x * disp_width, draw_width) + disp_x;
	info->pos_y = DIV_ROUND(info->pos_y * disp_height, draw_height) + disp_y;

	info->out_width = DIV_ROUND(info->out_width * disp_width, draw_width);
	info->out_height = DIV_ROUND(info->out_height * disp_height, draw_height);
#undef DIV_ROUND

	DRM_DEBUG_KMS("area after adjusting-->\n");
	DRM_DEBUG_KMS("(pos_x,pos_y/real_pos_x,real_pos_y WidthxHeight)\n");
	DRM_DEBUG_KMS("(%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)\n",
			info->xoff, info->yoff, info->width, info->height,
			info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
			info->out_width, info->out_height);

	info->color_mode = hdmi_drm_format_to_owl_format(overlay->pixel_format);

	DRM_DEBUG_KMS("blend %d, alpha %d\n",
			OWL_BLENDING_COVERAGE, 255);
	/*TODO*/
	//info->blending = OWL_BLENDING_PREMULT;
	info->blending = OWL_BLENDING_COVERAGE;
	//info->blending = OWL_BLENDING_NONE;
	info->alpha = 255;

#if 0
	if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_H_EXT)
		info->rotation = 2;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_V_EXT)
		info->rotation = 1;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_ROT_180_EXT)
		info->rotation = 3;
	else
#endif
		info->rotation = 0;

#if 0
	info->mmu_enable = false;
	if (mappings == NULL) {
		/* maybe a validate buffer, paddr canbe zero */
		ADFDBG("#mappings is NULL)\n");
		paddr = 0;
	} else if (config_ext->stamp == OWL_ADF_FBDEV_BUF_ID) {
		ADFDBG("###NO MMU(maybe ADF FBDEV)\n");
		paddr = sg_phys(mappings->sg_tables[0]->sgl);
	} else {
		ret = owl_de_mmu_sg_table_to_da(mappings->sg_tables[0],
				config_ext->stamp, &paddr);
		if (ret < 0) {
			dev_err(dev, "MMU ERROR %d\n", ret);
			goto err_out;
		}
		ADFDBG("paddr %d\n", paddr);
		info->mmu_enable = true;
	}
#endif

	info->pitch[0] = overlay->pitch;
	info->offset[0] = info->offset[0];
	info->addr[0] = overlay->dma_addr[0] + info->offset[0];
	DRM_DEBUG_KMS("plane 0, Pitch %d, Offset %d, Addr %ld\n",
			info->pitch[0], info->offset[0], info->addr[0]);

	if (drm_format_num_planes(overlay->pixel_format) == 1) {
		info->n_planes = 1;
	} else {
		info->n_planes = drm_format_num_planes(overlay->pixel_format);
		for (plane = 1; plane < info->n_planes; plane++) {
			info->offset[plane] = overlay->offsets[plane];
			info->pitch[plane] = overlay->pitches[plane];
			info->addr[plane] = overlay->dma_addr[plane];

			DRM_DEBUG_KMS("%s: plane %d, P %d, O %d, A %ld\n", __func__,
					plane, info->pitch[plane], info->offset[plane],
					info->addr[plane]);
		}
	}

	/* you can use other values as you like */
	info->brightness = owl_panel_brightness_get(panel);
	info->contrast = owl_panel_contrast_get(panel);
	info->saturation = owl_panel_saturation_get(panel);

	return 0;
}

static void hdmi_video_mode_set(struct device *dev,
		struct owl_drm_overlay *overlay)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	struct owl_de_path *path = ctx->path;
	struct owl_panel *panel = ctx->panel;

	struct owl_de_video_info v_info;
	struct owl_de_video *video;

	unsigned long offset;
	int win, ret = 0;

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	DRM_DEBUG_KMS("win id(zpos) %d\n", overlay->zpos);

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	video = ctx->video[win];

	owl_de_video_get_info(video, &v_info);

	ret = overlay_info_to_video_info(panel, overlay, &v_info);
	if ( ret < 0) {
		ret = -EFAULT;
		dev_err(dev, "overlay info to video info error\n");
		goto out;
	}

	ret = owl_de_video_info_validate(video, &v_info);
	if ( ret < 0) {
		ret = -EFAULT;
		dev_err(dev, "video info validate error %d\n", ret);
		goto out;
	}

	owl_de_video_set_info(video, &v_info);

	owl_de_path_attach(path, video);

out:
	DRM_DEBUG_KMS("ret %d\n", ret);
	return 0;
}

static void hdmi_win_set_pixfmt(struct device *dev, unsigned int win)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
}

static void hdmi_win_set_colkey(struct device *dev, unsigned int win)
{
	DRM_DEBUG_KMS("\n");
}

static void hdmi_video_apply(struct device *dev, int zpos)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	int win = zpos;

}

static void hdmi_video_commit(struct device *dev, int zpos)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	struct owl_de_path *path = ctx->path;
	struct owl_de_video *video;

	if (zpos < 0 || zpos > WINDOWS_NR)
		zpos = ctx->default_win;

	DRM_DEBUG_KMS("zpos %d\n", zpos);

	video = ctx->video[zpos];

	owl_de_path_apply(path);
	owl_de_path_wait_for_go(path);
}

static void hdmi_video_enable(struct device *dev, int zpos)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	int win = zpos;

}

static void hdmi_video_disable(struct device *dev, int zpos)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	int win = zpos;

	DRM_DEBUG_KMS("\n");

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;


	if (ctx->suspended) {
		/* do not resume this window*/
		return;
	}

	/*
	   drm_disp->win[win].enabled = false;
	   rk_drm_disp_handle(drm_disp,1<<win,RK_DRM_WIN_COMMIT | RK_DRM_DISPLAY_COMMIT);
	   */
}

static struct owl_drm_overlay_ops hdmi_overlay_ops = {
	.mode_set 	= hdmi_video_mode_set,
	.commit 	= hdmi_video_commit,

	/*
	 * this callback not used, FIXME
	 * */
	.apply		= hdmi_video_apply,
	.enable		= hdmi_video_enable,
	.disable	= hdmi_video_disable,
};

static struct owl_drm_manager hdmi_manager = {
	.pipe		= -1,
	.ops		= &hdmi_manager_ops,
	.overlay_ops	= &hdmi_overlay_ops,
	.display_ops	= &hdmi_display_ops,
};

static int hdmi_subdrv_probe(struct drm_device *drm_dev, struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	DRM_DEBUG_KMS("\n");

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = 1;

	/*
	 * with vblank_disable_allowed = 1, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = 1;

	/* attach this sub driver to iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_attach_device(drm_dev, dev);

	return 0;
}

static void hdmi_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("\n");

	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_detach_device(drm_dev, dev);
}

static void hdmi_clear_win(struct hdmi_context *ctx, int win)
{
	DRM_DEBUG_KMS("\n");
}

static void hdmi_window_suspend(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	int i;

	for (i = 0; i < WINDOWS_NR; i++)
		hdmi_video_disable(dev, i);

	hdmi_path_wait_for_vblank(dev);
}

static void hdmi_window_resume(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);
	int i;

}

static int hdmi_activate(struct hdmi_context *ctx, bool enable)
{
	struct device *dev = ctx->subdrv.dev;

	DRM_DEBUG_KMS("\n");

	if (enable) {
		int ret;

		ctx->suspended = false;

		/* if vblank was enabled status, enable it again. */
#if 0
		if (ctx->vblank_en)
			hdmi_path_enable_vblank(dev);
#endif
	} else {
		ctx->suspended = true;
	}

	return 0;
}

static int hdmi_probe(struct platform_device *pdev)
{
	struct device 		*dev = &pdev->dev;
	struct owl_drm_subdrv 	*subdrv;
	struct hdmi_context 	*ctx;
	struct owl_panel	*panel = NULL;
	struct owl_de_path	*path = NULL;
	struct owl_de_video	*video = NULL;

	struct fb_modelist 	*modelist;
	struct fb_videomode 	*mode;

	int i;

	DRM_DEBUG_KMS("\n");

	g_dev = dev;
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* get path */
	path = owl_de_path_get_by_index(0);
	if (path == NULL || path->current_panel == NULL) {
		DRM_ERROR("get path error!");
		return -ENODEV;
	}

	/* get video */
	for (i = 0; i < WINDOWS_NR; i++) {
		ctx->video[i] = owl_de_video_get_by_index(4*i);
		if (ctx->video[i] == NULL) {
			DRM_ERROR(" get video error!");
			return -ENODEV;
		}
		DRM_DEBUG_KMS("video id %d\n", ctx->video[i]->id);
	}

	/* get panel */
	ctx->panel = path->current_panel;
	ctx->path = path;

	/* registers vsync and hotplug call back */
	owl_panel_vsync_cb_set(ctx->panel, hdmi_vsync_event_cb, ctx);
	owl_panel_hotplug_cb_set(ctx->panel, hdmi_hotplug_event_cb, ctx);

	/* enable hpd detect */
	owl_panel_hpd_enable(ctx->panel, 0);

	DRM_INIT_WAITQUEUE(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	subdrv = &ctx->subdrv;

	subdrv->dev = dev;
	subdrv->manager = &hdmi_manager;
	subdrv->probe = hdmi_subdrv_probe;
	subdrv->remove = hdmi_subdrv_remove;

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	hdmi_activate(ctx, true);
	ctx->default_win = 0;

	owl_drm_subdrv_register(subdrv);

	return 0;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("\n");

	owl_drm_subdrv_unregister(&ctx->subdrv);

	if (ctx->suspended)
		goto out;

	pm_runtime_set_suspended(dev);
	pm_runtime_put_sync(dev);

out:
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hdmi_suspend(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	/*
	 * do not use pm_runtime_suspend(). if pm_runtime_suspend() is
	 * called here, an error would be returned by that interface
	 * because the usage_count of pm runtime is more than 1.
	 */
	if (!pm_runtime_suspended(dev))
		return hdmi_activate(ctx, false);

	return 0;
}

static int hdmi_resume(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	/*
	 * if entered to sleep when hdmi panel was on, the usage_count
	 * of pm runtime would still be 1 so in this case, fimd driver
	 * should be on directly not drawing on pm runtime interface.
	 */
	if (!pm_runtime_suspended(dev)) {
		int ret;

		ret = hdmi_activate(ctx, true);
		if (ret < 0)
			return ret;

		/*
		 * in case of dpms on(standby), hdmi_path_apply function will
		 * be called by encoder's dpms callback to update fimd's
		 * registers but in case of sleep wakeup, it's not.
		 * so hdmi_path_apply function should be called at here.
		 */
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int hdmi_runtime_suspend(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	DRM_DEBUG_KMS("\n");

	return hdmi_activate(ctx, false);
}

static int hdmi_runtime_resume(struct device *dev)
{
	struct hdmi_context *ctx = get_hdmi_context(dev);

	DRM_DEBUG_KMS("\n");

	return hdmi_activate(ctx, true);
}
#endif

static const struct dev_pm_ops hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hdmi_suspend, hdmi_resume)
		SET_RUNTIME_PM_OPS(hdmi_runtime_suspend, hdmi_runtime_resume, NULL)
};

struct platform_driver hdmi_platform_driver = {
	.probe		= hdmi_probe,
	.remove		= hdmi_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "owl-drm-hdmi",
		.pm	= &hdmi_pm_ops,
	},
};
