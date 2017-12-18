/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <drm/owl_drm.h>
#include <video/owl_dss.h>
#include "owl_drm_drv.h"
#include "owl_drm_encoder.h"


#define to_owl_drm_connector(x)	container_of(x, struct owl_drm_connector,\
		drm_connector)

struct owl_drm_connector {
	struct drm_connector	drm_connector;
	uint32_t		encoder_id;
	struct owl_drm_manager *manager;
	uint32_t		dpms;
};

/* convert owl_video_timings to drm_display_mode */
static inline void convert_to_display_mode(struct drm_display_mode *mode,
					   struct owl_videomode *owl_mode)
{
	DRM_DEBUG_KMS("mode: %dx%d\n", owl_mode->xres, owl_mode->yres);

	/* owl_mode->pixclock in ps */
	mode->clock = (1000000000UL / owl_mode->pixclock);
	mode->vrefresh = owl_mode->refresh;

	mode->hdisplay = owl_mode->xres;
	mode->hsync_start = mode->hdisplay + owl_mode->hbp;
	mode->hsync_end = mode->hsync_start + owl_mode->hsw;
	mode->htotal = mode->hsync_end + owl_mode->hfp;

	mode->vdisplay = owl_mode->yres;
	mode->vsync_start = mode->vdisplay + owl_mode->vbp;
	mode->vsync_end = mode->vsync_start + owl_mode->vsw;
	mode->vtotal = mode->vsync_end + owl_mode->vfp;

	if (owl_mode->vmode & FB_VMODE_INTERLACED)
		mode->flags |= DRM_MODE_FLAG_INTERLACE;

	if (owl_mode->vmode & FB_VMODE_DOUBLE)
		mode->flags |= DRM_MODE_FLAG_DBLSCAN;
}

static inline void convert_fbmode_to_display_mode(struct drm_display_mode *mode,
		struct fb_videomode *timing)
{
	DRM_DEBUG_KMS("\n");

	mode->clock = timing->pixclock / 1000;
	mode->vrefresh = timing->refresh;

	mode->hdisplay = timing->xres;
	mode->hsync_start = mode->hdisplay + timing->right_margin;
	mode->hsync_end = mode->hsync_start + timing->hsync_len;
	mode->htotal = mode->hsync_end + timing->left_margin;

	mode->vdisplay = timing->yres;
	mode->vsync_start = mode->vdisplay + timing->lower_margin;
	mode->vsync_end = mode->vsync_start + timing->vsync_len;
	mode->vtotal = mode->vsync_end + timing->upper_margin;

	if (timing->vmode & FB_VMODE_INTERLACED)
		mode->flags |= DRM_MODE_FLAG_INTERLACE;

	if (timing->vmode & FB_VMODE_DOUBLE)
		mode->flags |= DRM_MODE_FLAG_DBLSCAN;
}

/* convert drm_display_mode to owl_video_timings */
	static inline void
convert_to_video_timing(struct fb_videomode *timing,
		struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS("\n");

	memset(timing, 0, sizeof(*timing));

	timing->pixclock = mode->clock * 1000;
	timing->refresh = drm_mode_vrefresh(mode);

	timing->xres = mode->hdisplay;
	timing->right_margin = mode->hsync_start - mode->hdisplay;
	timing->hsync_len = mode->hsync_end - mode->hsync_start;
	timing->left_margin = mode->htotal - mode->hsync_end;

	timing->yres = mode->vdisplay;
	timing->lower_margin = mode->vsync_start - mode->vdisplay;
	timing->vsync_len = mode->vsync_end - mode->vsync_start;
	timing->upper_margin = mode->vtotal - mode->vsync_end;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		timing->vmode = FB_VMODE_INTERLACED;
	else
		timing->vmode = FB_VMODE_NONINTERLACED;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		timing->vmode |= FB_VMODE_DOUBLE;
}

static int owl_drm_connector_get_modes(struct drm_connector *connector)
{
	struct owl_drm_connector *owl_drm_connector = to_owl_drm_connector(connector);
	struct owl_drm_manager 	*manager = owl_drm_connector->manager;
	struct owl_drm_display_ops *display_ops = manager->display_ops;

	struct drm_display_mode *disp_mode = NULL;
	struct owl_panel *panel = NULL;
	struct owl_videomode *mode;

	struct edid *edid = NULL;
	unsigned int count = 0;
	int ret, i;

	DRM_DEBUG_KMS("\n");

	if (!display_ops) {
		DRM_DEBUG_KMS("display_ops is null.\n");
		return 0;
	}

	/*
	 * if get_edid() exists then get_edid() callback of hdmi side
	 * is called to get edid data through i2c interface else
	 * get timing from the FIMD driver(display controller).
	 *
	 * P.S. in case of lcd panel, count is always 1 if success
	 * because lcd panel has only one mode.
	 */
	if (display_ops->get_edid) {
		edid = display_ops->get_edid(manager->dev, connector);
		if (IS_ERR_OR_NULL(edid)) {
			ret = PTR_ERR(edid);
			edid = NULL;
			DRM_ERROR("Panel operation get_edid failed %d\n", ret);
			goto out;
		}

		count = drm_add_edid_modes(connector, edid);
		if (!count) {
			DRM_ERROR("Add edid modes failed %d\n", count);
			goto out;
		}

		drm_mode_connector_update_edid_property(connector, edid);
	}

	if (display_ops->get_panel)
		panel = display_ops->get_panel(manager->dev);
	else {
		DRM_ERROR("panel is NULL, get modelists failed\n");
		return -1;
	}

	/*
	 * if n_modes is not NULL, panel has more than one mode,
	 * else panel just has only one mode.
	 */
	DRM_DEBUG_KMS(" get modelist num %d\n", panel->n_modes);
	if (panel->n_modes) {
		for (i = 0; i < panel->n_modes; i++) {
			mode = &panel->mode_list[i];
			disp_mode = drm_mode_create(connector->dev);
			if (!mode) {
				DRM_ERROR("failed to create a new display mode.\n");
				return count;
			}

			convert_to_display_mode(disp_mode, mode);

			connector->display_info.width_mm = panel->desc.width_mm;
			connector->display_info.height_mm = panel->desc.height_mm;

			/*
			 * the best mode index is 0.
			 */
			if (i == 0)
				disp_mode->type |=  DRM_MODE_TYPE_PREFERRED;

			drm_mode_set_name(disp_mode);
			drm_mode_probed_add(connector, disp_mode);
			count++;
		}
	} else {
		disp_mode = drm_mode_create(connector->dev);
		if (!disp_mode) {
			DRM_ERROR("failed to create a new display mode.\n");
			return 0;
		}

		convert_to_display_mode(disp_mode, &panel->mode);
		connector->display_info.width_mm = panel->desc.width_mm;
		connector->display_info.height_mm = panel->desc.height_mm;

		disp_mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(disp_mode);
		drm_mode_probed_add(connector, disp_mode);
		count = 1;
	}
out:
	kfree(edid);
	return count;
}

static int owl_drm_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	struct owl_drm_connector *owl_drm_connector =
		to_owl_drm_connector(connector);
	struct owl_drm_manager *manager = owl_drm_connector->manager;
	struct owl_drm_display_ops *display_ops = manager->display_ops;
	struct fb_videomode timing;
	int ret = MODE_BAD;

	DRM_DEBUG_KMS("\n");

	convert_to_video_timing(&timing, mode);

	if (display_ops && display_ops->check_timing)
		if (!display_ops->check_timing(manager->dev, (void *)&timing))
			ret = MODE_OK;

	return ret;
}

struct drm_encoder *owl_drm_best_encoder(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct owl_drm_connector *owl_drm_connector =
		to_owl_drm_connector(connector);
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("\n");

	obj = drm_mode_object_find(dev, owl_drm_connector->encoder_id,
			DRM_MODE_OBJECT_ENCODER);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown ENCODER ID %d\n",
				owl_drm_connector->encoder_id);
		return NULL;
	}

	encoder = obj_to_encoder(obj);

	return encoder;
}

static struct drm_connector_helper_funcs owl_drm_connector_helper_funcs = {
	.get_modes	= owl_drm_connector_get_modes,
	.mode_valid	= owl_drm_connector_mode_valid,
	.best_encoder	= owl_drm_best_encoder,
};

void owl_drm_display_power(struct drm_connector *connector, int mode)
{
	struct drm_encoder *encoder = owl_drm_best_encoder(connector);
	struct owl_drm_connector *owl_drm_connector;
	struct owl_drm_manager *manager = owl_drm_get_manager(encoder);
	struct owl_drm_display_ops *display_ops = manager->display_ops;

	owl_drm_connector = to_owl_drm_connector(connector);

	if (owl_drm_connector->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	if (display_ops && display_ops->power_on)
		display_ops->power_on(manager->dev, mode);

	owl_drm_connector->dpms = mode;
}

static void owl_drm_connector_dpms(struct drm_connector *connector,
		int mode)
{
	DRM_DEBUG_KMS(" mode %d\n", mode);

	/*
	 * in case that drm_crtc_helper_set_mode() is called,
	 * encoder/crtc->funcs->dpms() will be just returned
	 * because they already were DRM_MODE_DPMS_ON so only
	 * owl_drm_display_power() will be called.
	 */
	drm_helper_connector_dpms(connector, mode);

	owl_drm_display_power(connector, mode);

}

static int owl_drm_connector_fill_modes(struct drm_connector *connector,
		unsigned int max_width, unsigned int max_height)
{
	struct owl_drm_connector *owl_drm_connector = to_owl_drm_connector(connector);
	struct owl_drm_manager *manager 	= owl_drm_connector->manager;
	struct owl_drm_manager_ops *ops 	= manager->ops;
	unsigned int width, height;

	width = max_width;
	height = max_height;

	/*
	 * if specific driver want to find desired_mode using maxmum
	 * resolution then get max width and height from that driver.
	 */
	if (ops && ops->get_max_resol)
		ops->get_max_resol(manager->dev, &width, &height);

	return drm_helper_probe_single_connector_modes(connector, width,
							height);
}

/* get detection status of display device. */
static enum drm_connector_status owl_drm_connector_detect(struct drm_connector *connector,
							  bool force)
{
	struct owl_drm_connector *owl_drm_connector = to_owl_drm_connector(connector);
	struct owl_drm_manager *manager = owl_drm_connector->manager;
	struct owl_drm_display_ops *display_ops = manager->display_ops;
	enum drm_connector_status status = connector_status_disconnected;

	DRM_DEBUG_KMS("force %d\n", force);

	if (display_ops && display_ops->is_connected) {
		if (display_ops->is_connected(manager->dev))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;
	}

	return status;
}

static void owl_drm_connector_destroy(struct drm_connector *connector)
{
	struct owl_drm_connector *owl_drm_connector =
		to_owl_drm_connector(connector);

	DRM_DEBUG_KMS("\n");

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(owl_drm_connector);
}

static struct drm_connector_funcs owl_drm_connector_funcs = {
	.dpms		= owl_drm_connector_dpms,
	.fill_modes	= owl_drm_connector_fill_modes,
	.detect		= owl_drm_connector_detect,
	.destroy	= owl_drm_connector_destroy,
};

struct drm_connector *owl_drm_connector_create(struct drm_device *dev,
		struct drm_encoder *encoder)
{
	struct owl_drm_connector *owl_drm_connector;
	struct owl_drm_manager *manager = owl_drm_get_manager(encoder);
	struct drm_connector *connector;
	int type;
	int err;

	DRM_DEBUG_KMS(" type: %d\n", manager->display_ops->type);

	owl_drm_connector = kzalloc(sizeof(*owl_drm_connector), GFP_KERNEL);
	if (!owl_drm_connector) {
		DRM_ERROR("failed to allocate connector\n");
		return NULL;
	}

	connector = &owl_drm_connector->drm_connector;

	switch (manager->display_ops->type) {
		case OWL_DRM_DISPLAY_TYPE_HDMI:
			type = DRM_MODE_CONNECTOR_HDMIA;
			connector->interlace_allowed = true;
			connector->polled = DRM_CONNECTOR_POLL_HPD;
			break;
		case OWL_DRM_DISPLAY_TYPE_VIDI:
			type = DRM_MODE_CONNECTOR_VIRTUAL;
			connector->polled = DRM_CONNECTOR_POLL_HPD;
			break;
		case OWL_DRM_DISPLAY_TYPE_LCD:
			type = DRM_MODE_CONNECTOR_LVDS;
			break;
		default:
			type = DRM_MODE_CONNECTOR_Unknown;
			break;
	}

	drm_connector_init(dev, connector, &owl_drm_connector_funcs, type);
	drm_connector_helper_add(connector, &owl_drm_connector_helper_funcs);

	err = drm_sysfs_connector_add(connector);
	if (err)
		goto err_connector;

	owl_drm_connector->encoder_id = encoder->base.id;
	owl_drm_connector->manager = manager;
	owl_drm_connector->dpms = DRM_MODE_DPMS_OFF;
	connector->dpms = DRM_MODE_DPMS_OFF;
	connector->encoder = encoder;

	err = drm_mode_connector_attach_encoder(connector, encoder);
	if (err) {
		DRM_ERROR("failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	DRM_DEBUG_KMS("connector has been created\n");

	return connector;

err_sysfs:
	drm_sysfs_connector_remove(connector);
err_connector:
	drm_connector_cleanup(connector);
	kfree(owl_drm_connector);
	return NULL;
}
