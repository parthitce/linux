/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_DMABUF_H_
#define _OWL_DRM_DMABUF_H_

#ifdef CONFIG_DRM_OWL_DMABUF
struct dma_buf *owl_dmabuf_prime_export(struct drm_device *drm_dev,
				struct drm_gem_object *obj, int flags);

struct drm_gem_object *owl_dmabuf_prime_import(struct drm_device *drm_dev,
						struct dma_buf *dma_buf);
#else
#define owl_dmabuf_prime_export		NULL
#define owl_dmabuf_prime_import		NULL
#endif

#endif
