/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_FBDEV_H_
#define _OWL_DRM_FBDEV_H_

int owl_drm_fbdev_init(struct drm_device *dev);
int owl_drm_fbdev_reinit(struct drm_device *dev);
void owl_drm_fbdev_fini(struct drm_device *dev);
void owl_drm_fbdev_restore_mode(struct drm_device *dev);

#endif
