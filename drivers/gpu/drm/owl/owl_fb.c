/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"
#include "owl_gem.h"

struct owl_framebuffer {
	struct drm_framebuffer	base;
	int			            n_planes;
	struct drm_gem_object	*planes[MAX_PLANES];
};
#define to_owl_framebuffer(x)	container_of(x, struct owl_framebuffer, base)

static int owl_framebuffer_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv,
		unsigned int *handle)
{
	struct owl_framebuffer *owl_fb = to_owl_framebuffer(fb);

	/* This fb should have only one gem object. */
	if (WARN_ON(owl_fb->n_planes != 1))
		return -EINVAL;

	return drm_gem_handle_create(file_priv, owl_fb->planes[0], handle);
}

static void owl_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct owl_framebuffer*owl_fb = to_owl_framebuffer(fb);
	int i;

	DBG("destroy: FB ID: %d (%p)", fb->base.id, fb);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < owl_fb->n_planes; i++) {
		struct drm_gem_object *bo = owl_fb->planes[i];
		drm_gem_object_unreference_unlocked(bo);
	}

	kfree(owl_fb);
}

static struct drm_framebuffer_funcs owl_framebuffer_funcs = {
	.create_handle = owl_framebuffer_create_handle,
	.destroy       = owl_framebuffer_destroy,
};

#ifdef CONFIG_DEBUG_FS
void owl_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	struct owl_framebuffer *owl_fb = to_owl_framebuffer(fb);
	int i;

	seq_printf(m, "fb: %dx%d@%4.4s (%2d, ID:%d)\n",
			fb->width, fb->height, (char *)&fb->pixel_format,
			fb->refcount.refcount.counter, fb->base.id);

	for (i = 0; i < owl_fb->n_planes; i++) {
		seq_printf(m, "   %d: offset=%d pitch=%d, obj: ",
				i, fb->offsets[i], fb->pitches[i]);
		owl_gem_describe(owl_fb->planes[i], m);
	}
}
#endif

int owl_framebuffer_get_planes(struct drm_framebuffer *fb)
{
	return to_owl_framebuffer(fb)->n_planes;
}

struct drm_gem_object *owl_framebuffer_bo(struct drm_framebuffer *fb, int plane)
{
	struct owl_framebuffer *owl_fb = to_owl_framebuffer(fb);
	return owl_fb->planes[plane];
}

struct drm_framebuffer *owl_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	struct owl_framebuffer *owl_fb;
	struct drm_framebuffer *fb;
	unsigned int hsub, vsub;
	int ret, i;

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d@%4.4s)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			(char *)&mode_cmd->pixel_format);

	owl_fb = kzalloc(sizeof(*owl_fb), GFP_KERNEL);
	if (!owl_fb)
		return ERR_PTR(-ENOMEM);

	fb = &owl_fb->base;

	owl_fb->n_planes = drm_format_num_planes(mode_cmd->pixel_format);
	if (owl_fb->n_planes > ARRAY_SIZE(owl_fb->planes)) {
		ret = -EINVAL;
		goto fail;
	}

	hsub = drm_format_horz_chroma_subsampling(mode_cmd->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(mode_cmd->pixel_format);

	for (i = 0; i < owl_fb->n_planes; i++) {
		unsigned int width = mode_cmd->width / (i ? hsub : 1);
		unsigned int height = mode_cmd->height / (i ? vsub : 1);
		unsigned int min_size;

		min_size = (height - 1) * mode_cmd->pitches[i]
			 + width * drm_format_plane_cpp(mode_cmd->pixel_format, i)
			 + mode_cmd->offsets[i];

		if (bos[i]->size < min_size) {
			ret = -EINVAL;
			goto fail;
		}

		owl_fb->planes[i] = bos[i];
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &owl_framebuffer_funcs);
	if (ret) {
		DEV_ERR(dev->dev, "framebuffer init failed: %d", ret);
		goto fail;
	}

	DBG("create: FB ID: %d (%p)", fb->base.id, fb);

	return fb;
fail:
	kfree(owl_fb);
	return ERR_PTR(ret);
}

struct drm_framebuffer *owl_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *bos[4] = { 0 };
	struct drm_framebuffer *fb;
	int ret, i, n = drm_format_num_planes(mode_cmd->pixel_format);

	for (i = 0; i < n; i++) {
		bos[i] = drm_gem_object_lookup(dev, file, mode_cmd->handles[i]);
		if (!bos[i]) {
			ret = -ENXIO;
			goto out_unref;
		}
	}

	fb = owl_framebuffer_init(dev, mode_cmd, bos);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto out_unref;
	}

	return fb;

out_unref:
	for (i = 0; i < n; i++)
		drm_gem_object_unreference_unlocked(bos[i]);
	return ERR_PTR(ret);
}

struct drm_framebuffer *
owl_alloc_fb(struct drm_device *dev, int w, int h, int p, uint32_t format)
{
	struct drm_mode_fb_cmd2 mode_cmd = {
		.pixel_format = format,
		.width = w,
		.height = h,
		.pitches = { p },
	};
	struct drm_gem_object *bo;
	struct drm_framebuffer *fb;
	unsigned int size;

	/* allocate backing bo */
	size = mode_cmd.pitches[0] * mode_cmd.height;
	DBG("allocating %d bytes for fb %d", size, dev->primary->index);

	bo = owl_gem_new(dev, size, OWL_BO_SCANOUT | OWL_BO_WC);
	if (IS_ERR(bo)) {
		DEV_ERR(dev->dev, "failed to allocate buffer object");
		return ERR_CAST(bo);
	}

	fb = owl_framebuffer_init(dev, &mode_cmd, &bo);
	if (IS_ERR(fb)) {
		DEV_ERR(dev->dev, "failed to allocate fb");
		/* note: if fb creation failed, we can't rely on fb destroy
		 * to unref the bo:
		 */
		drm_gem_object_unreference_unlocked(bo);
		return ERR_CAST(fb);
	}

	return fb;
}
