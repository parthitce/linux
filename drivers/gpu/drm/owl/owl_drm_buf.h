/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_BUF_H_
#define _OWL_DRM_BUF_H_

/* create and initialize buffer object. */
struct owl_drm_gem_buf *owl_drm_init_buf(struct drm_device *dev,
						unsigned int size);

/* destroy buffer object. */
void owl_drm_fini_buf(struct drm_device *dev,
				struct owl_drm_gem_buf *buffer);

/* allocate physical memory region and setup sgt. */
int owl_drm_alloc_buf(struct drm_device *dev,
				struct owl_drm_gem_buf *buf,
				unsigned int flags);

/* release physical memory region, and sgt. */
void owl_drm_free_buf(struct drm_device *dev,
				unsigned int flags,
				struct owl_drm_gem_buf *buffer);

#endif
