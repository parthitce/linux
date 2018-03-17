/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/kfifo.h>
#include "owl_drv.h"
#include "owl_gem.h"

/*
 * plane funcs
 */
struct callback {
	void (*fxn)(void *);
	void *arg;
};

#define to_owl_plane(x) container_of(x, struct owl_plane, base)

struct owl_plane {
	struct drm_plane base;
	struct owl_drm_overlay *overlay;
	struct owl_overlay_info info;

	/* position/orientation of scanout within the fb: */
	struct owl_drm_window win;
	bool enabled;

	/* last fb that we pinned: */
	struct drm_framebuffer *pinned_fb;

	/* set of bo's pending unpin until next post_apply() */
	DECLARE_KFIFO_PTR(unpin_fifo, struct drm_gem_object *);

	struct owl_drm_apply apply;

	// XXX maybe get rid of this and handle vblank in crtc too?
	struct callback apply_done_cb;
};

static const uint32_t owl_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_YUV420, /* YU12 */
	DRM_FORMAT_YVU420, /* YV12 */
};

static void unpin(void *arg, struct drm_gem_object *bo)
{
	struct drm_plane *plane = arg;
	struct owl_plane *owl_plane = to_owl_plane(plane);

	if (kfifo_put(&owl_plane->unpin_fifo, (const struct drm_gem_object **)&bo)) {
		/* also hold a ref so it isn't free'd while pinned */
		drm_gem_object_reference(bo);
	} else {
		ERR("unpin fifo full!");
		owl_gem_unpin(bo);
	}
}

/* update which fb (if any) is pinned for scanout */
static int update_pin(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);
	struct owl_overlay_info *info = &owl_plane->info;
	struct drm_framebuffer *pinned_fb = owl_plane->pinned_fb;
	int i;

	if (pinned_fb != fb) {
		int ret = 0;

		DBG_KMS("%p -> %p", pinned_fb, fb);

		if (fb) {
			drm_framebuffer_reference(fb);
			info->fb_width 	   = fb->width;
			info->fb_height    = fb->height;
			info->pixel_format = fb->pixel_format;
			for (i = 0; i < owl_framebuffer_get_planes(fb); i++) {
				ret = owl_gem_pin(owl_framebuffer_bo(fb, i), &info->dma_addr[i]);
				if (ret) {
					/* something went wrong.. unpin what has been pinned */
					for (i--; i >= 0; i--)
						unpin(plane, owl_framebuffer_bo(fb, i));
					break;
				} else {
					info->offsets[i] = fb->offsets[i];
					info->pitches[i] = fb->pitches[i];
				}
			}
		}

		if (pinned_fb) {
			drm_framebuffer_unreference(pinned_fb);
			/* unpin old fb */
			for (i = 0; i < owl_framebuffer_get_planes(pinned_fb); i++)
				unpin(plane, owl_framebuffer_bo(pinned_fb, i));
		}

		if (ret) {
			ERR("could not swap %p -> %p", owl_plane->pinned_fb, fb);
			if (fb)
				drm_framebuffer_unreference(fb);
			owl_plane->pinned_fb = NULL;
			return ret;
		}

		owl_plane->pinned_fb = fb;
	}

	return 0;
}

static void owl_plane_pre_apply(struct owl_drm_apply *apply)
{
	struct owl_plane *owl_plane = container_of(apply, struct owl_plane, apply);
	struct drm_plane *plane = &owl_plane->base;
	struct owl_drm_overlay *overlay = owl_plane->overlay;
	struct owl_overlay_info *info = &owl_plane->info;
	struct owl_drm_window *win = &owl_plane->win;
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(plane->dev);
	struct owl_drm_panel *panel = NULL;
	bool enabled = owl_plane->enabled && plane->crtc;

	DBG_KMS("plane=%u, enabled=%d", plane->base.id, enabled);

	/* if fb has changed, pin new fb: */
	update_pin(plane, enabled ? plane->fb : NULL);

	if (plane->crtc)
		panel = owl_dssdev_get_panel(dssdev, &plane->crtc->base);

	/* FIXME: detach previous panel only when a new panel exist ? */
	if (panel && overlay->panel != panel) {
		if (overlay->panel)
			owl_dssdev_detach_overlay(overlay, overlay->panel);
		if (panel)
			owl_dssdev_attach_overlay(overlay, panel);
	}

	if (!overlay->panel) {
		ERR("overlay %p not attached to a panel", overlay);
		return;
	}

	if (!enabled) {
		overlay->funcs->disable(overlay);
		return;
	}

	/* set overlay range of source display */
	info->src_x      = win->src_x;
	info->src_y      = win->src_y;
	info->src_width  = win->src_w;
	info->src_height = win->src_h;
	/* set overlay range to be displayed. */
	info->crtc_x 	  = win->crtc_x;
	info->crtc_y 	  = win->crtc_y;
	info->crtc_width  = win->crtc_w;
	info->crtc_height = win->crtc_h;
	/* set drm mode data. */
	info->mode_width  = plane->crtc->mode.hdisplay;
	info->mode_height = plane->crtc->mode.vdisplay;

	DBG_KMS("overlay fb pixel_format %u, fb width %u, height %u",
			info->pixel_format, info->fb_width, info->fb_height);
	DBG_KMS("overlay source crop: x/y(%u, %u), width/height(%u, %u)",
			info->src_x, info->src_y, info->src_width, info->src_height);
	DBG_KMS("overlay display frame: x/y(%u, %u), width/height(%u, %u)",
			info->crtc_x, info->crtc_y, info->crtc_width, info->crtc_height);

	/* and finally, update owldss: */
	overlay->funcs->enable(overlay);
	overlay->funcs->apply(overlay, info);
}

static void owl_plane_post_apply(struct owl_drm_apply *apply)
{
	struct owl_plane *owl_plane = container_of(apply, struct owl_plane, apply);
	struct drm_gem_object *bo = NULL;
	struct callback cb;

	cb = owl_plane->apply_done_cb;
	owl_plane->apply_done_cb.fxn = NULL;

	while (kfifo_get(&owl_plane->unpin_fifo, &bo)) {
		owl_gem_unpin(bo);
		drm_gem_object_unreference_unlocked(bo);
	}

	if (cb.fxn)
		cb.fxn(cb.arg);

	/* TODO: flush cache */
/*
	if (owl_plane->enabled) {
		struct drm_plane *plane = &owl_plane->base;
		struct owl_overlay_info *info = &owl_plane->info;
		owl_framebuffer_flush(plane->fb, info->pos_x, info->pos_y,
				info->out_width, info->out_height);
	}
*/
}

static int apply(struct drm_plane *plane)
{
	if (plane->crtc) {
		struct owl_plane *owl_plane = to_owl_plane(plane);
		return owl_crtc_apply(plane->crtc, &owl_plane->apply);
	}

	return 0;
}

int owl_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h,
		void (*fxn)(void *), void *arg)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);
	struct owl_drm_window *win = &owl_plane->win;

	/*
	DBG_KMS("crtc=%u, plane=%u, fb=%u", crtc->base.id, plane->base.id, fb->base.id);
	*/
	win->crtc_x = crtc_x;
	win->crtc_y = crtc_y;
	win->crtc_w = crtc_w;
	win->crtc_h = crtc_h;

	/* src values are in Q16 fixed point, convert to integer: */
	win->src_x = src_x >> 16;
	win->src_y = src_y >> 16;
	win->src_w = src_w >> 16;
	win->src_h = src_h >> 16;

	if (fxn) {
		/* owl_crtc should ensure that a new page flip
		 * isn't permitted while there is one pending:
		 */
		BUG_ON(owl_plane->apply_done_cb.fxn);

		owl_plane->apply_done_cb.fxn = fxn;
		owl_plane->apply_done_cb.arg = arg;
	}

	plane->fb = fb;
	plane->crtc = crtc;

	return apply(plane);
}

static int owl_plane_update(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);

	DBG_KMS("crtc=%u, plane=%u, fb=%u", crtc->base.id, plane->base.id, fb->base.id);

	owl_plane->enabled = true;

	if (plane->fb)
		drm_framebuffer_unreference(plane->fb);

	drm_framebuffer_reference(fb);

	return owl_plane_mode_set(plane, crtc, fb,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x, src_y, src_w, src_h,
			NULL, NULL);
}

static int owl_plane_disable(struct drm_plane *plane)
{
	DBG_KMS("plane=%u", plane->base.id);
	return owl_plane_dpms(plane, DRM_MODE_DPMS_OFF);
}

static void owl_plane_destroy(struct drm_plane *plane)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);

	DBG_KMS("plane=%u", plane->base.id);

	owl_dssdev_free_overlay(owl_plane->overlay);
	owl_plane_disable(plane);
	drm_plane_cleanup(plane);

	WARN_ON(!kfifo_is_empty(&owl_plane->unpin_fifo));
	kfifo_free(&owl_plane->unpin_fifo);

	kfree(owl_plane);
}

int owl_plane_dpms(struct drm_plane *plane, int mode)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);
	bool enabled = (mode == DRM_MODE_DPMS_ON);
	int ret = 0;

	DBG_KMS("plane=%u, mode=%d", plane->base.id, mode);

	if (enabled != owl_plane->enabled) {
		/* FIXME: do dmps ops only when some fb attached */
		if (plane->fb) {
			owl_plane->enabled = enabled;
			ret = apply(plane);
		}
	}

	return ret;
}

static void owl_plane_install_properties(struct drm_plane *plane, struct drm_mode_object *obj)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);
	struct owl_drm_overlay *overlay = owl_plane->overlay;
	struct owl_drm_private *priv = plane->dev->dev_private;
	struct drm_property *prop;
	int cap_scaling = 0;

	/* zpos property */
	prop = priv->plane_property[PLANE_PROP_ZPOS];
	if (!prop) {
		prop = drm_property_create_range(plane->dev, 0, "zpos", 0, priv->num_planes - 1);
		if (!prop)
			return;

		priv->plane_property[PLANE_PROP_ZPOS] = prop;
	}

	drm_object_attach_property(obj, prop, overlay->zpos);

	/* scaling property */
	prop = priv->plane_property[PLANE_PROP_SCALING];
	if (!prop) {
		prop = drm_property_create_range(plane->dev, DRM_MODE_PROP_IMMUTABLE, "scaling", 0, 1);
		if (!prop)
			return;

		priv->plane_property[PLANE_PROP_SCALING] = prop;
	}

	overlay->funcs->query(overlay, OVERLAY_CAP_SCALING, &cap_scaling);
	DBG("overlay %d: cap_scaling %d", overlay->zpos, cap_scaling);

	drm_object_attach_property(obj, prop, cap_scaling);
}

static int owl_plane_set_property(struct drm_plane *plane,
		struct drm_property *property, uint64_t val)
{
	struct owl_plane *owl_plane = to_owl_plane(plane);
	struct owl_drm_private *priv = plane->dev->dev_private;
	int ret = -EINVAL;

	DBG_KMS("plane=%u, zorder=%llu", plane->base.id, val);

	if (property == priv->plane_property[PLANE_PROP_ZPOS]) {
		owl_plane->win.zpos = val;
		/* TODO: need to support dynamic plane zpos. Just ignored it now, since
		 * trick has made in plane initialization for the cursor zorder.
		 */
		/* ret = apply(plane); */
		ret = 0;
	}

	return ret;
}

static struct drm_plane_funcs owl_plane_funcs = {
	.update_plane  = owl_plane_update,
	.disable_plane = owl_plane_disable,
	.destroy       = owl_plane_destroy,
	.set_property  = owl_plane_set_property,
};

struct drm_plane *owl_plane_init(struct drm_device *dev, bool private_plane)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(dev);
	struct owl_drm_overlay *overlay;
	struct owl_plane *owl_plane;
	struct drm_plane *plane;
	int ret;

	owl_plane = kzalloc(sizeof(*owl_plane), GFP_KERNEL);
	if (!owl_plane) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = kfifo_alloc(&owl_plane->unpin_fifo, 16, GFP_KERNEL);
	if (ret) {
		ERR("could not allocate unpin FIFO");
		goto fail_free;
	}

	plane = &owl_plane->base;

	owl_plane->apply.pre_apply  = owl_plane_pre_apply;
	owl_plane->apply.post_apply = owl_plane_post_apply;

	ret = drm_plane_init(dev, plane, (1u << priv->num_crtcs) - 1,
			&owl_plane_funcs, owl_formats, ARRAY_SIZE(owl_formats), private_plane);
	if (ret) {
		ERR("failed to initialize plane");
		goto fail_free_kfifo;
	}

	/* Hack to implement correct plane zpos for crtc, cursor and video:
	 * zpos(crtc) < zpos(video) < zpos(cursor)
	 */
	overlay = owl_dssdev_alloc_overlay(dssdev, plane);
	BUG_ON(overlay == NULL);

	owl_plane->overlay = overlay;
	owl_plane->win.zpos = overlay->zpos;

	owl_plane_install_properties(plane, &plane->base);

	DBG_KMS("plane=%u, private=%d", plane->base.id, private_plane);

	return plane;
fail_free_kfifo:
	kfifo_free(&owl_plane->unpin_fifo);
fail_free:
	kfree(owl_plane);
fail:
	return ERR_PTR(ret);
}
