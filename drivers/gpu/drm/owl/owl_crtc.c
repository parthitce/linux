/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"

#define to_owl_crtc(x)	container_of(x, struct owl_crtc, base)

enum owl_crtc_mode {
	CRTC_MODE_NORMAL,	/* normal mode */
	CRTC_MODE_BLANK,	/* The private plane of crtc is blank */
};

static const struct drm_prop_enum_list mode_names[] = {
	{ CRTC_MODE_NORMAL, "normal" },
	{ CRTC_MODE_BLANK, "blank" },
};

struct owl_crtc {
	struct drm_crtc base;

	/* primary and cursor planes for CRTC */
	struct drm_plane *primary;
	struct drm_plane *cursor;

	/* position of cursor plane on crtc */
	int cursor_x;
	int cursor_y;

	/**
	 * @index: Position inside the mode_config.list, can be used as an array
	 * index. It is invariant over the lifetime of the CRTC.
	 */
	unsigned int index;

	bool enabled;
	bool mode_update;

	struct owl_drm_apply apply;

#ifdef HAVE_DRM_IRQ
	bool pending;
	wait_queue_head_t pending_wait;
#endif

	/* list of in-progress apply's: */
	struct list_head pending_applies;

	/* list of queued apply's: */
	struct list_head queued_applies;

	/* for handling queued and in-progress applies: */
	struct work_struct apply_work;

	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;
	struct drm_framebuffer *old_fb;

	/* for handling page flips without caring about what
	 * the callback is called from.  Possibly we should just
	 * make owl_gem always call the cb from the worker so
	 * we don't have to care about this..
	 *
	 * XXX maybe fold into apply_work??
	 */
	struct work_struct page_flip_work;
};

/*
 * CRTC funcs:
 */

/**
 * owl_crtc_index - find the index of a registered CRTC
 * @crtc: CRTC to find index for
 *
 * Given a registered CRTC, return the index of that CRTC within a DRM
 * device's list of CRTCs.
 */
unsigned int owl_crtc_index(const struct drm_crtc *crtc)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	return owl_crtc->index;
}

/**
 * owl_crtc_mask - find the mask of a registered CRTC
 * @crtc: CRTC to find mask for
 *
 * Given a registered CRTC, return the mask bit of that CRTC for an
 * encoder's possible_crtcs field.
 */
uint32_t owl_crtc_mask(const struct drm_crtc *crtc)
{
	return 1 << owl_crtc_index(crtc);
}

#ifdef HAVE_DRM_IRQ
static bool owl_crtc_is_pending(struct drm_crtc *crtc)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	pending = owl_crtc->pending;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	return pending;
}

static void owl_crtc_set_pending(struct drm_crtc *crtc, bool pending)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	owl_crtc->pending = pending;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

int owl_crtc_wait_pending(struct drm_crtc *crtc)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);

	/*
	 * Timeout is set to a "sufficiently" high value, which should cover
	 * a single frame refresh even on slower displays.
	 */
	return wait_event_timeout(owl_crtc->pending_wait,
				  !owl_crtc_is_pending(crtc),
				  msecs_to_jiffies(250));
}
#endif /* HAVE_DRM_IRQ */

int owl_crtc_enable_vblank(struct drm_device *dev, int crtc_id)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[crtc_id];
	struct owl_drm_dssdev *dssdev = priv->dssdev;
	struct owl_drm_panel *panel = owl_dssdev_get_panel(dssdev, &crtc->base);

	DBG_KMS("crtc=%u, panel=%p", crtc->base.id, panel);

	if (panel && panel->funcs->enable_vblank)
		return panel->funcs->enable_vblank(panel);

	return 0;
}

void owl_crtc_disable_vblank(struct drm_device *dev, int crtc_id)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = priv->crtcs[crtc_id];
	struct owl_drm_dssdev *dssdev = priv->dssdev;
	struct owl_drm_panel *panel = owl_dssdev_get_panel(dssdev, &crtc->base);

	DBG_KMS("crtc=%u, panel=%p", crtc->base.id, panel);

	if (panel && panel->funcs->disable_vblank)
		panel->funcs->disable_vblank(panel);
}

int owl_crtc_handle_vblank(struct owl_drm_panel *panel)
{
	struct owl_crtc *owl_crtc;
	bool pending;

	if (!panel->connector ||
		!panel->connector->encoder ||
		!panel->connector->encoder->crtc)
		return -ENODEV;

	owl_crtc = to_owl_crtc(panel->connector->encoder->crtc);
	drm_handle_vblank(panel->drm, owl_crtc->index);

#ifdef HAVE_DRM_IRQ
	spin_lock(&panel->drm->event_lock);

	pending = owl_crtc->pending;
	owl_crtc->pending = false;

	spin_unlock(&panel->drm->event_lock);

	if (pending) {
		drm_vblank_put(panel->drm, owl_crtc->index);

		/* Wake up any waiting */
		wake_up(&owl_crtc->pending_wait);
	}
#endif /* HAVE_DRM_IRQ */

	return 0;
}

int owl_crtc_handle_hotplug(struct owl_drm_panel *panel)
{
	drm_helper_hpd_irq_event(panel->drm);
	return 0;
}

static int owl_crtc_set_config(struct drm_mode_set *set)
{
	size_t i;

	DBG_KMS("crtc=%u, num_connectors=%zu", set->crtc->base.id, set->num_connectors);
	for (i = 0; i < set->num_connectors; i++) {
		DBG_KMS("connector%zu=%u, connector_type=%d", i,
				set->connectors[i]->base.id, set->connectors[i]->connector_type);
	}

	if (set->mode)
		DBG_KMS("fb=%u, mode=%dx%d", set->fb->base.id, set->mode->hdisplay, set->mode->vdisplay);

	return drm_crtc_helper_set_config(set);
}

static void owl_crtc_destroy(struct drm_crtc *crtc)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);

	DBG_KMS("crtc=%u", crtc->base.id);

	owl_crtc->primary->funcs->destroy(owl_crtc->primary);
	owl_crtc->cursor->funcs->destroy(owl_crtc->cursor);
	drm_crtc_cleanup(crtc);

	kfree(owl_crtc);
}

static void owl_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *plane;
	bool enabled = (mode == DRM_MODE_DPMS_ON);

	DBG_KMS("crtc=%u, mode=%d", crtc->base.id, mode);

	if (enabled != owl_crtc->enabled) {
		owl_crtc->enabled = enabled;

		/* track the associate power state */
		owl_crtc->apply.enabled = enabled;
		owl_crtc_apply(crtc, &owl_crtc->apply);

		/* also enable our private plane: */
		WARN_ON(owl_plane_dpms(owl_crtc->primary, mode));
		WARN_ON(owl_plane_dpms(owl_crtc->cursor, mode));

		/* and any attached overlay planes: */
		list_for_each_entry(plane, &crtc->dev->mode_config.plane_list, head) {
			if (plane->crtc == crtc)
				WARN_ON(owl_plane_dpms(plane, mode));
		}
	}
}

static bool owl_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	DBG_KMS("crtc=%u", crtc->base.id);
	return true;
}

static int owl_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);

	DBG_KMS("crtc=%u, primary=%u, fb=%u", crtc->base.id,
			owl_crtc->primary->base.id, crtc->fb->base.id);

	mode = adjusted_mode;

	DBG_KMS("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	/*
	 * copy the mode data adjusted by mode_fixup() into crtc->mode
	 * so that hardware can be seet to proper mode.
	 */
	memcpy(&crtc->mode, adjusted_mode, sizeof(*adjusted_mode));
	owl_crtc->mode_update = true;

	return owl_plane_mode_set(owl_crtc->primary, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			NULL, NULL);
}

static void owl_crtc_prepare(struct drm_crtc *crtc)
{
	DBG_KMS("crtc=%u", crtc->base.id);
	owl_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void owl_crtc_commit(struct drm_crtc *crtc)
{
	DBG_KMS("crtc=%u", crtc->base.id);
	owl_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int owl_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_display_mode *mode = &crtc->mode;

	DBG_KMS("crtc=%u, primary=%u, fb=%u", crtc->base.id,
			owl_crtc->primary->base.id, crtc->fb->base.id);

	return owl_plane_mode_set(owl_crtc->primary, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			NULL, NULL);
}

static void owl_crtc_load_lut(struct drm_crtc *crtc)
{
	DBG_KMS("crtc=%u", crtc->base.id);
}

static void vblank_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct drm_device *dev = crtc->dev;
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	/* wakeup userspace */
	if (owl_crtc->event)
		drm_send_vblank_event(dev, owl_crtc->index, owl_crtc->event);

	owl_crtc->event = NULL;
	owl_crtc->old_fb = NULL;

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void page_flip_worker(struct work_struct *work)
{
	struct owl_crtc *owl_crtc =
			container_of(work, struct owl_crtc, page_flip_work);
	struct drm_crtc *crtc = &owl_crtc->base;
	struct drm_display_mode *mode = &crtc->mode;
	struct drm_gem_object *bo;

	mutex_lock(&crtc->mutex);
	owl_plane_mode_set(owl_crtc->primary, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			crtc->x << 16, crtc->y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			vblank_cb, crtc);
	mutex_unlock(&crtc->mutex);

	bo = owl_framebuffer_bo(crtc->fb, 0);
	drm_gem_object_unreference_unlocked(bo);
}

static void page_flip_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct owl_drm_private *priv = crtc->dev->dev_private;

	/* avoid assumptions about what ctxt we are called from: */
	queue_work(priv->wq, &owl_crtc->page_flip_work);
}

static int owl_crtc_page_flip_locked(struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_pending_vblank_event *event)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_gem_object *bo;

	DBG_KMS("crtc=%u, fb: %d -> %d (event=%p)", crtc->base.id,
			crtc->fb ? crtc->fb->base.id : -1, fb->base.id, event);

	if (owl_crtc->old_fb) {
		ERR("already a pending flip");
		return -EINVAL;
	}

	owl_crtc->event = event;
	crtc->fb = fb;

	/*
	 * Hold a reference temporarily until the crtc is updated
	 * and takes the reference to the bo.  This avoids it
	 * getting freed from under us:
	 */
	bo = owl_framebuffer_bo(fb, 0);
	drm_gem_object_reference(bo);

	/* TODO: gem obj sync */
	/* owl_gem_op_async(bo, OMAP_GEM_READ, page_flip_cb, crtc); */
	page_flip_cb(crtc);

	return 0;
}

static void owl_crtc_install_properties(struct drm_crtc *crtc, struct drm_mode_object *obj)
{
	struct drm_device *dev = crtc->dev;
	struct owl_drm_private *priv = dev->dev_private;
	struct drm_property *prop;

	prop = priv->crtc_property[CRTC_PROP_MODE];
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "mode", 0, ARRAY_SIZE(mode_names) - 1);
		if (!prop)
			return;

		priv->crtc_property[CRTC_PROP_MODE] = prop;
	}

	drm_object_attach_property(obj, prop, 0);
}

static int owl_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property, uint64_t val)
{
	struct drm_device *dev = crtc->dev;
	struct owl_drm_private *priv = dev->dev_private;

	DBG_KMS("crtc=%u, val=%llu", crtc->base.id, val);

	if (property == priv->crtc_property[CRTC_PROP_MODE]) {
		switch (val) {
		case CRTC_MODE_NORMAL:
			owl_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
			break;
		case CRTC_MODE_BLANK:
			owl_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}

	return -EINVAL;
}

static int owl_crtc_cursor_update(struct drm_crtc *crtc,
		struct drm_framebuffer *fb, int crtc_x, int crtc_y)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *cursor = owl_crtc->cursor;
	int w = fb->width, h = fb->height;
	int src_x = 0, src_y = 0;

	if (crtc_x < 0) {
		src_x += -crtc_x;
		w -= -crtc_x;
		crtc_x = 0;
	}

	if (crtc_y < 0) {
		src_y += -crtc_y;
		h -= -crtc_y;
		crtc_y = 0;
	}

	if (crtc_x + w > crtc->mode.hdisplay) {
		crtc_x = min(crtc_x, crtc->mode.hdisplay);
		w = crtc->mode.hdisplay - crtc_x;
	}

	if (crtc_y + h > crtc->mode.vdisplay) {
		crtc_y = min(crtc_y, crtc->mode.vdisplay);
		h = crtc->mode.vdisplay - crtc_y;
	}

	return cursor->funcs->update_plane(cursor, crtc, fb,
			crtc_x, crtc_y, w, h, src_x << 16, src_y << 16, w << 16, h << 16);
}

static int owl_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			  uint32_t handle, uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *cursor = owl_crtc->cursor;
	struct drm_framebuffer *fb = NULL;
	struct drm_framebuffer *old_fb = NULL;
	struct drm_mode_fb_cmd2 fbreq = {
		.width = width,
		.height = height,
		.pixel_format = DRM_FORMAT_ARGB8888,
		.handles = { handle },
		.pitches = { width * 4 },
		.offsets = { 0 },
	};
	int ret = 0;

	BUG_ON(!cursor);
	WARN_ON(cursor->crtc != crtc && cursor->crtc != NULL);

	DBG_KMS("crtc=%u, cursor=%u, handle=%u", crtc->base.id, cursor->base.id, handle);

	if (handle) {
		fb = dev->mode_config.funcs->fb_create(dev, file_priv, &fbreq);
		if (IS_ERR(fb)) {
			ERR("failed to wrap cursor buffer in drm framebuffer");
			return PTR_ERR(fb);
		}
	}

	/* No fb means shut it down */
	if (!fb) {
		old_fb = cursor->fb;
		cursor->funcs->disable_plane(cursor);
		cursor->crtc = NULL;
		cursor->fb = NULL;
		goto out;
	}

	ret = owl_crtc_cursor_update(crtc, fb, owl_crtc->cursor_x, owl_crtc->cursor_y);
	if (!ret) {
		old_fb = cursor->fb;
		cursor->crtc = crtc;
		cursor->fb = fb;
		fb = NULL;
	}

out:
	if (fb)
		drm_framebuffer_unreference(fb);
	if (old_fb)
		drm_framebuffer_unreference(old_fb);
	return ret;
}

static int owl_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *cursor = owl_crtc->cursor;
	struct drm_framebuffer *fb = cursor->fb;
	int ret = 0;

	DBG_KMS("crtc=%u, cursor=%u, x=%d, y=%d", crtc->base.id, cursor->base.id, x, y);

	BUG_ON(!cursor);
	WARN_ON(!cursor->fb);
	WARN_ON(cursor->crtc != crtc && cursor->crtc != NULL);

	if (!cursor->fb) {
		ERR("no fb attached to cursor %u", cursor->base.id);
		return -EINVAL;
	}

	ret = owl_crtc_cursor_update(crtc, fb, x, y);
	if (!ret) {
		owl_crtc->cursor_x = x;
		owl_crtc->cursor_y = y;
	}

	return ret;
}

static struct drm_crtc_funcs owl_crtc_funcs = {
	.set_config = owl_crtc_set_config,
	.destroy    = owl_crtc_destroy,
	.page_flip  = owl_crtc_page_flip_locked,
	.set_property = owl_crtc_set_property,
	.cursor_set  = owl_crtc_cursor_set,
	.cursor_move = owl_crtc_cursor_move,
};

static const struct drm_crtc_helper_funcs owl_crtc_helper_funcs = {
	.dpms       = owl_crtc_dpms,
	.mode_fixup = owl_crtc_mode_fixup,
	.mode_set   = owl_crtc_mode_set,
	.prepare    = owl_crtc_prepare,
	.commit     = owl_crtc_commit,
	.mode_set_base = owl_crtc_mode_set_base,
	.load_lut      = owl_crtc_load_lut,
};

static void apply_worker(struct work_struct *work)
{
	struct owl_crtc *owl_crtc = container_of(work, struct owl_crtc, apply_work);
	struct drm_crtc *crtc = &owl_crtc->base;
	struct owl_drm_apply *apply, *n;
	bool need_apply;

#ifdef HAVE_DRM_IRQ
	if (!drm_vblank_get(crtc->dev, owl_crtc->index)) {
		owl_crtc_set_pending(crtc, true);
		owl_crtc_wait_pending(crtc);
	}
#endif

	/*
	 * Synchronize everything on mode_config.mutex, to keep
	 * the callbacks and list modification all serialized
	 * with respect to modesetting ioctls from userspace.
	 */
	mutex_lock(&crtc->mutex);

	/* finish up previous apply's: */
	list_for_each_entry_safe(apply, n, &owl_crtc->pending_applies, pending_node) {
		apply->post_apply(apply);
		list_del(&apply->pending_node);
	}

	need_apply = !list_empty(&owl_crtc->queued_applies);

	/* then handle the next round of of queued apply's: */
	list_for_each_entry_safe(apply, n, &owl_crtc->queued_applies, queued_node) {
		apply->pre_apply(apply);
		list_del(&apply->queued_node);
		apply->queued = false;
		list_add_tail(&apply->pending_node, &owl_crtc->pending_applies);
	}

	if (need_apply) {
		struct owl_drm_private *priv = crtc->dev->dev_private;
		queue_work(priv->wq, &owl_crtc->apply_work);

		DBG_KMS("GO");
	}

	mutex_unlock(&crtc->mutex);
}

int owl_crtc_apply(struct drm_crtc *crtc, struct owl_drm_apply *apply)
{
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);

	WARN_ON(!mutex_is_locked(&crtc->mutex));

	/* no need to queue it again if it is already queued: */
	if (apply->queued) {
		DBG_KMS("crtc(%u): %s apply already queured", crtc->base.id,
				apply == &owl_crtc->apply ? "crtc" : "plane");
		apply->pending = true;
		return 0;
	}

	apply->queued = true;
	list_add_tail(&apply->queued_node, &owl_crtc->queued_applies);

	/*
	 * If there are no currently pending updates, then go ahead and
	 * kick the worker immediately, otherwise it will run again when
	 * the current update finishes.
	 */
	if (list_empty(&owl_crtc->pending_applies)) {
		struct owl_drm_private *priv = crtc->dev->dev_private;
		queue_work(priv->wq, &owl_crtc->apply_work);
	}

	return 0;
}

/* called only from apply */
static void set_enabled(struct drm_crtc *crtc, bool enable)
{
#ifdef HAVE_DRM_IRQ
	struct drm_device *dev = crtc->dev;
	struct owl_crtc *owl_crtc = to_owl_crtc(crtc);
	struct owl_irq_wait *wait;
	int ret;

	wait = owl_irq_wait_init(dev, pipe2irq(owl_crtc->index), 1);
	ret = owl_irq_wait(dev, wait, msecs_to_jiffies(100));
	if (ret)
		DEV_ERR(dev->dev, "timeout waiting for %s", enable ? "enable" : "disable");
#endif
}

static void owl_crtc_pre_apply(struct owl_drm_apply *apply)
{
	struct owl_crtc *owl_crtc = container_of(apply, struct owl_crtc, apply);
	struct drm_crtc *crtc = &owl_crtc->base;
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(crtc->dev);
	struct owl_drm_panel *panel = owl_dssdev_get_panel(dssdev, &crtc->base);
	bool enabled = apply->enabled;

	DBG_KMS("crtc=%u, enabled=%d, update=%d", crtc->base.id,
			enabled, owl_crtc->mode_update);

	if (!panel) {
		DBG_KMS("crtc(%u): panel not connected", crtc->base.id);
		return;
	}

	if (!enabled) {
		/* set_enabled(&owl_crtc->base, false); */
		owl_connector_set_enabled(panel->connector, false);
	} else {
		if (owl_crtc->mode_update) {
			owl_connector_update(panel->connector, &crtc->mode);
			owl_crtc->mode_update = false;
		}
		owl_connector_set_enabled(panel->connector, true);
		/* set_enabled(&owl_crtc->base, true); */
	}
}

static void owl_crtc_post_apply(struct owl_drm_apply *apply)
{
	struct owl_crtc *owl_crtc = container_of(apply, struct owl_crtc, apply);
	struct drm_crtc *crtc = &owl_crtc->base;

	/* queue again if any pending */
	if (apply->pending) {
		DBG_KMS("crtc=%u, enabled=%d", crtc->base.id, owl_crtc->enabled);

		apply->enabled = owl_crtc->enabled;
		owl_crtc_apply(&owl_crtc->base, apply);

		apply->pending = false;
	}
}

struct drm_crtc *owl_crtc_init(struct drm_device *dev,
		struct drm_plane *primary, struct drm_plane *cursor, unsigned int id)
{
	struct owl_crtc *owl_crtc;
	struct drm_crtc *crtc;
	int ret;

	owl_crtc = kzalloc(sizeof(*owl_crtc), GFP_KERNEL);
	if (!owl_crtc) {
		ret = -ENOMEM;
		goto fail;
	}

	crtc = &owl_crtc->base;

	INIT_WORK(&owl_crtc->page_flip_work, page_flip_worker);
	INIT_WORK(&owl_crtc->apply_work, apply_worker);

	INIT_LIST_HEAD(&owl_crtc->pending_applies);
	INIT_LIST_HEAD(&owl_crtc->queued_applies);

	owl_crtc->apply.pre_apply  = owl_crtc_pre_apply;
	owl_crtc->apply.post_apply = owl_crtc_post_apply;
	owl_crtc->primary = primary;
	owl_crtc->primary->crtc = crtc;
	owl_crtc->cursor = cursor;
	owl_crtc->cursor->crtc = crtc;
	owl_crtc->index = id;

#ifdef HAVE_DRM_IRQ
	init_waitqueue_head(&owl_crtc->pending_wait);
#endif

	drm_crtc_init(dev, crtc, &owl_crtc_funcs);
	drm_crtc_helper_add(crtc, &owl_crtc_helper_funcs);

	owl_crtc_install_properties(crtc, &crtc->base);

	DBG_KMS("crtc=%u, index=%d", crtc->base.id, id);

	return crtc;
fail:
	return ERR_PTR(ret);
}
