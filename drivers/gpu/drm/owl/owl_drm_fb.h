/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_FB_H_
#define _OWL_DRM_FB_H

struct drm_framebuffer *
owl_drm_framebuffer_init(struct drm_device *dev,
			    struct drm_mode_fb_cmd2 *mode_cmd,
			    struct drm_gem_object *obj);

/* get memory information of a drm framebuffer */
struct owl_drm_gem_buf *owl_drm_fb_buffer(struct drm_framebuffer *fb,
						 int index);

void owl_drm_mode_config_init(struct drm_device *dev);

/* set a buffer count to drm framebuffer. */
void owl_drm_fb_set_buf_cnt(struct drm_framebuffer *fb,
						unsigned int cnt);

/* get a buffer count to drm framebuffer. */
unsigned int owl_drm_fb_get_buf_cnt(struct drm_framebuffer *fb);

#endif
