/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"

#define to_owl_connector(x)	container_of(x, struct owl_connector, base)

struct owl_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	struct owl_drm_panel *panel;
};

void copy_mode_owl_to_drm(struct drm_display_mode *mode,
		struct owl_videomode *owl_mode)
{
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

void copy_mode_drm_to_owl(struct owl_videomode *owl_mode,
		struct drm_display_mode *mode)
{
	memset(owl_mode, 0, sizeof(*owl_mode));

	owl_mode->pixclock = (1000000000UL / mode->clock);
	owl_mode->refresh = drm_mode_vrefresh(mode);

	owl_mode->xres = mode->hdisplay;
	owl_mode->hbp = mode->hsync_start - mode->hdisplay;
	owl_mode->hsw = mode->hsync_end - mode->hsync_start;
	owl_mode->hfp = mode->htotal - mode->hsync_end; 

	owl_mode->yres = mode->vdisplay;
	owl_mode->vbp = mode->vsync_start - mode->vdisplay;
	owl_mode->vsw = mode->vsync_end - mode->vsync_start;
	owl_mode->vfp = mode->vtotal - mode->vsync_end; 

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		owl_mode->vmode |= FB_VMODE_INTERLACED;
	else
		owl_mode->vmode |= FB_VMODE_NONINTERLACED;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		owl_mode->vmode |= FB_VMODE_DOUBLE;
}

static int owl_connector_get_modes(struct drm_connector *connector)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);
	struct owl_drm_panel *panel = owl_connector->panel;
	struct owl_videomode *owl_videomodes;
	int i, num_modes = 0;

	num_modes = panel->funcs->get_modes(panel, NULL, 0);
	if (!num_modes)
		goto out;

	owl_videomodes = kcalloc(num_modes, sizeof(*owl_videomodes), GFP_KERNEL);
	if (!owl_videomodes)
		return 0;

	num_modes = panel->funcs->get_modes(panel, owl_videomodes, num_modes);

	for (i = 0; i < num_modes; i++) {
		struct drm_display_mode *mode = drm_mode_create(connector->dev);

		copy_mode_owl_to_drm(mode, &owl_videomodes[i]);
		mode->type  = DRM_MODE_TYPE_DRIVER;
		mode->type |= i ? 0 : DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	kfree(owl_videomodes);
out:
	DBG_KMS("connector=%u, num_modes=%d", connector->base.id, num_modes);
	return num_modes;
}

static int owl_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);
	struct owl_drm_panel *panel = owl_connector->panel;
	struct owl_videomode owl_mode;
	int ret = MODE_BAD;

	copy_mode_drm_to_owl(&owl_mode, mode);

	/*
	 * if the panel driver doesn't have a validate_mode, it's most likely
	 * a fixed resolution panel, check if the mode match with the
	 * panel's mode
	 */
	if (panel->funcs->validate_mode) {
		if (panel->funcs->validate_mode(panel, &owl_mode))
			ret = MODE_OK;
	} else if (panel->funcs->get_modes) {
		struct owl_videomode m;
		if (panel->funcs->get_modes(panel, &m, 1) &&
			!memcmp(&owl_mode, &m, sizeof(m)))
			ret = MODE_OK;
	}

	DBG_KMS("connector=%u, mode %s: "
		"%d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
		connector->base.id, (ret == MODE_OK) ? "valid" : "invalid",
		mode->base.id, mode->name, mode->vrefresh, mode->clock,
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start,
		mode->vsync_end, mode->vtotal, mode->type, mode->flags);

	return ret;
}

static struct drm_encoder *owl_connector_attached_encoder(struct drm_connector *connector)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);

	DBG_KMS("connector=%u, encoder=%u", connector->base.id, owl_connector->encoder->base.id);
	return owl_connector->encoder;
}

static enum drm_connector_status owl_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);
	struct owl_drm_panel *panel = owl_connector->panel;
	enum drm_connector_status ret;

	if (panel->funcs->detect) {
		if (panel->funcs->detect(panel))
			ret = connector_status_connected;
		else
			ret = connector_status_disconnected;
	} else if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS) {
		ret = connector_status_connected;
	} else {
		ret = connector_status_unknown;
	}

	DBG_KMS("connector=%u, force=%d, status=%d", connector->base.id, force, ret);

	return ret;
}

static void owl_connector_destroy(struct drm_connector *connector)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);

	DBG_KMS("connector=%u", connector->base.id);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(owl_connector);
}

static struct drm_connector_funcs owl_connector_funcs = {
	.dpms       = drm_helper_connector_dpms,
	.detect	    = owl_connector_detect,
	.fill_modes	= drm_helper_probe_single_connector_modes,
	.destroy    = owl_connector_destroy,
};

static struct drm_connector_helper_funcs owl_connector_helper_funcs = {
	.get_modes	= owl_connector_get_modes,
	.mode_valid	= owl_connector_mode_valid,
	.best_encoder = owl_connector_attached_encoder,
};

/*
 * Instead of relying on the helpers for modeset, the owl_crtc code
 * calls these functions in the proper sequence.
 */
int owl_connector_set_enabled(struct drm_connector *connector, bool enabled)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);
	struct owl_drm_panel *panel = owl_connector->panel;
	int ret = 0;

	if (enabled) {
		if (panel->funcs->prepare)
			ret |= panel->funcs->prepare(panel);
		if (panel->funcs->enable)
			ret |= panel->funcs->enable(panel);
	} else {
		if (panel->funcs->disable)
			ret |= panel->funcs->disable(panel);
		if (panel->funcs->unprepare)
			ret |= panel->funcs->unprepare(panel);
	}

	return ret;
}

int owl_connector_update(struct drm_connector *connector, struct drm_display_mode *mode)
{
	struct owl_connector *owl_connector = to_owl_connector(connector);
	struct owl_drm_panel *panel = owl_connector->panel;
	struct owl_videomode owl_mode;
	int ret = -EINVAL;

	ret = owl_connector_mode_valid(connector, mode);
	if (ret != MODE_OK) {
		ERR("could not set mode: %d", ret);
		return ret;		
	}

	if (panel->funcs->set_mode)
		ret = panel->funcs->set_mode(panel, &owl_mode);

	return ret;
}

struct drm_connector *owl_connector_init(struct drm_device *dev, int type,
		struct drm_encoder *encoder, struct owl_drm_panel *panel)
{
	struct owl_connector *owl_connector;
	struct drm_connector *connector;
	int ret;

	owl_connector = kzalloc(sizeof(*owl_connector), GFP_KERNEL);
	if (!owl_connector) {
		ret = -ENOMEM;
		goto fail;
	}

	owl_connector->encoder = encoder;
	owl_connector->panel = panel;
	connector = &owl_connector->base;

	switch (type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		connector->interlace_allowed = true;
		connector->polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	drm_connector_init(dev, connector, &owl_connector_funcs, type);
	drm_connector_helper_add(connector, &owl_connector_helper_funcs);
	drm_sysfs_connector_add(connector);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret) {
		ERR("failed to attach a connector to a encoder");
		goto fail_sysfs;
	}

	DBG_KMS("connector=%u, type=%d, encoder=%u", connector->base.id, type,
			encoder->base.id);
	return connector;
fail_sysfs:
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(owl_connector);
fail:
	return ERR_PTR(ret);
}
