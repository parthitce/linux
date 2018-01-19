/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _DSS_DISPC_H_
#define _DSS_DISPC_H_

#include <video/owl_dss.h>
#include "owl_drv.h"

#define MAX_VIDEOS (MAX_PLANES)

#define subdrv_to_mgr(drv) container_of(drv, struct dispc_manager, subdrv)
#define panel_to_mgr(panel) dev_get_drvdata(panel->dev)
#define overlay_to_mgr(ovl) panel_to_mgr(ovl->panel)

struct dispc_manager {
	struct owl_drm_subdrv subdrv;
	struct mutex mutex;

	/* owl private */
	struct owl_panel    *owl_panel;
	struct owl_de_path  *owl_path;
	struct owl_de_video *owl_videos[MAX_VIDEOS];
	int num_videos;
};

struct dispc_manager *dispc_manager_init(struct device *dev, int type);

int dispc_panel_enable(struct owl_drm_panel *panel);
int dispc_panel_disable(struct owl_drm_panel *panel);
int dispc_panel_prepare(struct owl_drm_panel *panel);
int dispc_panel_unprepare(struct owl_drm_panel *panel);
int dispc_panel_get_modes(struct owl_drm_panel *panel, struct owl_videomode *modes, int num_modes);
bool dispc_panel_validate_mode(struct owl_drm_panel *panel, struct owl_videomode *mode);
int dispc_panel_set_mode(struct owl_drm_panel *panel, struct owl_videomode *mode);
int dispc_panel_enable_vblank(struct owl_drm_panel *panel);
void dispc_panel_disable_vblank(struct owl_drm_panel *panel);
void dispc_panel_vsync_cb(struct owl_panel *owl_panel, void *data, u32 value);
void dispc_panel_hotplug_cb(struct owl_panel *owl_panel, void *data, u32 value);

int dispc_subdrv_add_panels(struct drm_device *drm, struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel_funcs *funcs);
void dispc_subdrv_remove_panels(struct drm_device *drm, struct owl_drm_subdrv *subdrv);

int dispc_subdrv_add_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv);
void dispc_subdrv_remove_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv);

#endif
