/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <uapi/drm/owl_drm.h>
#include <drm/owl_drm.h>

#include "owl_drm_drv.h"
#include "owl_drm_fb.h"
#include "owl_drm_gem.h"
#include "owl_drm_iommu.h"
#include "owl_drm_encoder.h"

#define to_owl_fb(x)	container_of(x, struct owl_drm_fb, fb)

/*
 * owl specific framebuffer structure.
 *
 * @fb: drm framebuffer obejct.
 * @buf_cnt: a buffer count to drm framebuffer.
 * @owl_gem_obj: array of owl specific gem object containing a gem object.
 */
struct owl_drm_fb {
	struct drm_framebuffer		fb;
	unsigned int			buf_cnt;
	struct owl_drm_gem_obj		*owl_gem_obj[MAX_FB_BUFFER];
};

static int check_fb_gem_memory_type(struct drm_device *drm_dev,
		struct owl_drm_gem_obj *owl_gem_obj)
{
	unsigned int flags;

	/*
	 * if owl drm driver supports iommu then framebuffer can use
	 * all the buffer types.
	 */
	if (is_drm_iommu_supported(drm_dev))
		return 0;

	flags = owl_gem_obj->flags;

	/*
	 * without iommu support, not support physically non-continuous memory
	 * for framebuffer.
	 */
	if (IS_NONCONTIG_BUFFER(flags)) {
		DRM_ERROR("cannot use this gem memory type for fb.\n");
		return -EINVAL;
	}

	return 0;
}

static void owl_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct owl_drm_fb *owl_fb = to_owl_fb(fb);
	unsigned int i;

	DRM_DEBUG_KMS("\n");

	/* make sure that overlay data are updated before relesing fb. */
	owl_drm_encoder_complete_scanout(fb);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < ARRAY_SIZE(owl_fb->owl_gem_obj); i++) {
		struct drm_gem_object *obj;

		if (owl_fb->owl_gem_obj[i] == NULL)
			continue;

		obj = &owl_fb->owl_gem_obj[i]->base;
		drm_gem_object_unreference_unlocked(obj);
	}

	kfree(owl_fb);
	owl_fb = NULL;
}

static int owl_drm_fb_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv,
		unsigned int *handle)
{
	struct owl_drm_fb *owl_fb = to_owl_fb(fb);

	DRM_DEBUG_KMS("\n");

	/* This fb should have only one gem object. */
	if (WARN_ON(owl_fb->buf_cnt != 1))
		return -EINVAL;

	return drm_gem_handle_create(file_priv,
			&owl_fb->owl_gem_obj[0]->base, handle);
}

static int owl_drm_fb_dirty(struct drm_framebuffer *fb,
		struct drm_file *file_priv, unsigned flags,
		unsigned color, struct drm_clip_rect *clips,
		unsigned num_clips)
{
	DRM_DEBUG_KMS("\n");

	/* TODO */

	return 0;
}

static struct drm_framebuffer_funcs owl_drm_fb_funcs = {
	.destroy	= owl_drm_fb_destroy,
	.create_handle	= owl_drm_fb_create_handle,
	.dirty		= owl_drm_fb_dirty,
};

void owl_drm_fb_set_buf_cnt(struct drm_framebuffer *fb,
		unsigned int cnt)
{
	struct owl_drm_fb *owl_fb;

	owl_fb = to_owl_fb(fb);

	owl_fb->buf_cnt = cnt;
}

unsigned int owl_drm_fb_get_buf_cnt(struct drm_framebuffer *fb)
{
	struct owl_drm_fb *owl_fb;

	owl_fb = to_owl_fb(fb);

	return owl_fb->buf_cnt;
}

struct drm_framebuffer *owl_drm_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd,
		struct drm_gem_object *obj)
{
	struct owl_drm_fb *owl_fb;
	struct owl_drm_gem_obj *owl_gem_obj;
	int ret;

	owl_gem_obj = to_owl_gem_obj(obj);

	ret = check_fb_gem_memory_type(dev, owl_gem_obj);
	if (ret < 0) {
		DRM_ERROR("cannot use this gem memory type for fb.\n");
		return ERR_PTR(-EINVAL);
	}

	owl_fb = kzalloc(sizeof(*owl_fb), GFP_KERNEL);
	if (!owl_fb) {
		DRM_ERROR("failed to allocate owl drm framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	drm_helper_mode_fill_fb_struct(&owl_fb->fb, mode_cmd);
	owl_fb->owl_gem_obj[0] = owl_gem_obj;

	ret = drm_framebuffer_init(dev, &owl_fb->fb, &owl_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer\n");
		return ERR_PTR(ret);
	}

	return &owl_fb->fb;
}

static u32 owl_drm_format_num_buffers(struct drm_mode_fb_cmd2 *mode_cmd)
{
	unsigned int cnt = 0;
	unsigned int max_cnt = drm_format_num_planes(mode_cmd->pixel_format);

	while (cnt != max_cnt) {
		if (!mode_cmd->handles[cnt])
			break;
		cnt++;
	}

	return cnt;
}

static struct drm_framebuffer *owl_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct owl_drm_gem_obj *owl_gem_obj;
	struct owl_drm_fb *owl_fb;
	int i, ret;

	DRM_DEBUG_KMS("\n");

	owl_fb = kzalloc(sizeof(*owl_fb), GFP_KERNEL);
	if (!owl_fb) {
		DRM_ERROR("failed to allocate owl drm framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	/* retrieved the GEM object by a call to drm_gem_object_lookup.*/
	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object\n");
		ret = -ENOENT;
		goto err_free;
	}

	drm_helper_mode_fill_fb_struct(&owl_fb->fb, mode_cmd);
	owl_fb->owl_gem_obj[0] = to_owl_gem_obj(obj);
	owl_fb->buf_cnt = owl_drm_format_num_buffers(mode_cmd);

	DRM_DEBUG_KMS("buf_cnt = %d\n", owl_fb->buf_cnt);

	for (i = 1; i < owl_fb->buf_cnt; i++) {
		obj = drm_gem_object_lookup(dev, file_priv,
				mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("failed to lookup gem object\n");
			ret = -ENOENT;
			owl_fb->buf_cnt = i;
			goto err_unreference;
		}

		owl_gem_obj = to_owl_gem_obj(obj);
		owl_fb->owl_gem_obj[i] = owl_gem_obj;

		ret = check_fb_gem_memory_type(dev, owl_gem_obj);
		if (ret < 0) {
			DRM_ERROR("cannot use this gem memory type for fb.\n");
			goto err_unreference;
		}
	}

	ret = drm_framebuffer_init(dev, &owl_fb->fb, &owl_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to init framebuffer.\n");
		goto err_unreference;
	}

	return &owl_fb->fb;

err_unreference:
	for (i = 0; i < owl_fb->buf_cnt; i++) {
		struct drm_gem_object *obj;

		obj = &owl_fb->owl_gem_obj[i]->base;
		if (obj)
			drm_gem_object_unreference_unlocked(obj);
	}
err_free:
	kfree(owl_fb);
	return ERR_PTR(ret);
}

struct owl_drm_gem_buf *owl_drm_fb_buffer(struct drm_framebuffer *fb,
		int index)
{
	struct owl_drm_fb *owl_fb = to_owl_fb(fb);
	struct owl_drm_gem_buf *buffer;

	DRM_DEBUG_KMS("\n");

	if (index >= MAX_FB_BUFFER)
		return NULL;

	buffer = owl_fb->owl_gem_obj[index]->buffer;
	if (!buffer)
		return NULL;

	DRM_DEBUG_KMS("dma_addr = 0x%lx\n", (unsigned long)buffer->dma_addr);

	return buffer;
}

static void owl_drm_output_poll_changed(struct drm_device *dev)
{
	struct owl_drm_private *private = dev->dev_private;
	struct drm_fb_helper *fb_helper = private->fb_helper;

	if (fb_helper)
		drm_fb_helper_hotplug_event(fb_helper);
}

static const struct drm_mode_config_funcs owl_drm_mode_config_funcs = {
	.fb_create = owl_user_fb_create,
	.output_poll_changed = owl_drm_output_poll_changed,
};

void owl_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	dev->mode_config.funcs = &owl_drm_mode_config_funcs;
}
