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

#include <linux/completion.h>
#include <linux/atomic.h>
#include <video/owl_dss.h>
#include "owl_drv.h"

#define MAX_VIDEOS (MAX_PLANES)

#define DSS_DBG(mgr, fmt, ...) DBG("%s: " fmt, mgr->owl_panel->desc.name, ##__VA_ARGS__)
#define DSS_ERR(mgr, fmt, ...) ERR("%s: " fmt, mgr->owl_panel->desc.name, ##__VA_ARGS__)

#define subdrv_to_mgr(drv) container_of(drv, struct dispc_manager, subdrv)
#define panel_to_mgr(panel) dev_get_drvdata(panel->dev)
#define overlay_to_mgr(ovl) panel_to_mgr(ovl->panel)

enum {
	OWL_DRM_DISPLAY_Unknown  = -1,
	OWL_DRM_DISPLAY_PRIMARY  = 0, /* LVDS */
	OWL_DRM_DISPLAY_EXTERNAL = 1, /* HDMI, CVBS */

	OWL_DRM_NUM_DISPLAY_TYPES,
};

struct dispc_manager {
	/* OWL_DRM_DISPLAY_x */
	int type;

	/* public data registered to drm */
	struct owl_drm_subdrv subdrv;

	/* private owl-dss resource */
	struct owl_panel    *owl_panel;
	struct owl_de_path  *owl_path;
	struct owl_de_video *owl_videos[MAX_VIDEOS];
	int num_videos;

	/* callbacks to owl_panel */
	owl_panel_cb_t vsync;
	owl_panel_cb_t hotplug;

	/* statistics */
	atomic_t hotplug_counter;
	atomic_t vsync_counter;

	/* workaround of mode_set by simulating hotplug */
	struct completion modeset_completion;

	struct mutex mutex;
};

/* @display_type: OWL_DISPLAY_TYPE_x */
struct dispc_manager *dispc_manager_create(struct device *dev, int display_type);
void dispc_manager_destroy(struct dispc_manager *mgr);
/* @type: OWL_DRM_DISPLAY_x */
struct dispc_manager *dispc_manager_get(int type);

/* register/unregister panel to drm */
int dispc_subdrv_add_panels(struct drm_device *drm, struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel_funcs *funcs);
void dispc_subdrv_remove_panels(struct drm_device *drm, struct owl_drm_subdrv *subdrv);

/* register/unregister overlay to drm */
int dispc_subdrv_add_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv);
void dispc_subdrv_remove_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv);

/* common implemention of struct owl_drm_panel_funcs */
bool dispc_panel_detect(struct owl_drm_panel *panel);
int dispc_panel_enable(struct owl_drm_panel *panel);
int dispc_panel_disable(struct owl_drm_panel *panel);
int dispc_panel_prepare(struct owl_drm_panel *panel);
int dispc_panel_unprepare(struct owl_drm_panel *panel);
int dispc_panel_get_modes(struct owl_drm_panel *panel, struct owl_videomode *modes, int num_modes);
bool dispc_panel_validate_mode(struct owl_drm_panel *panel, struct owl_videomode *mode);
int dispc_panel_set_mode(struct owl_drm_panel *panel, struct owl_videomode *mode);
int dispc_panel_enable_vblank(struct owl_drm_panel *panel);
void dispc_panel_disable_vblank(struct owl_drm_panel *panel);

/* register vsync callback to struct owl_panel
 * @mgr: will pass to cb as param "data"
 */
static inline void dispc_panel_register_vsync(struct dispc_manager *mgr, owl_panel_cb_t cb)
{
	owl_panel_vsync_cb_set(mgr->owl_panel, cb, mgr);
	mgr->vsync = cb;
}

/* register hotplug callback to struct owl_panel
 * @mgr: will pass to cb as param "data"
 */
static inline void dispc_panel_register_hotplug(struct dispc_manager *mgr, owl_panel_cb_t cb)
{
	owl_panel_hotplug_cb_set(mgr->owl_panel, cb, mgr);
	mgr->hotplug = cb;
}

/* default vsync/hotplug callback to struct owl_panel
 * @data: always the address of "struct dispc_manager" which owl_panel belongs to
 */
void dispc_panel_default_vsync(struct owl_panel *owl_panel, void *data, u32 status);
void dispc_panel_default_hotplug(struct owl_panel *owl_panel, void *data, u32 status);

#endif /* _DSS_DISPC_H_ */
