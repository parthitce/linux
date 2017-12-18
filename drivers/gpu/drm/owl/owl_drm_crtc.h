/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_CRTC_H_
#define _OWL_DRM_CRTC_H_

int owl_drm_crtc_create(struct drm_device *dev, unsigned int nr);
int owl_drm_crtc_enable_vblank(struct drm_device *dev, int crtc);
void owl_drm_crtc_disable_vblank(struct drm_device *dev, int crtc);
void owl_drm_crtc_finish_pageflip(struct drm_device *dev, int crtc);

#endif
