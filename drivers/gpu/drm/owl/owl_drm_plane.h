/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _OWL_DRM_PLANE_H_
#define _OWL_DRM_PLANE_H_

int owl_drm_plane_mode_set(struct drm_plane *plane, struct drm_crtc *crtc,
			  struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			  unsigned int crtc_w, unsigned int crtc_h,
			  uint32_t src_x, uint32_t src_y,
			  uint32_t src_w, uint32_t src_h);
void owl_drm_plane_commit(struct drm_plane *plane);
void owl_drm_plane_dpms(struct drm_plane *plane, int mode);
struct drm_plane *owl_drm_plane_init(struct drm_device *dev,
				    unsigned int possible_crtcs, bool priv);

#endif
