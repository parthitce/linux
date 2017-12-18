/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "owl_drm_drv.h"
#include "owl_drm_encoder.h"
#include "owl_drm_plane.h"

#define to_owl_crtc(x)	container_of(x, struct owl_drm_crtc,\
		drm_crtc)

enum owl_crtc_mode {
	CRTC_MODE_NORMAL,	/* normal mode */
	CRTC_MODE_BLANK,	/* The private plane of crtc is blank */
};

/*
 * Owl specific crtc structure.
 *
 * @drm_crtc: crtc object.
 * @drm_plane: pointer of private plane object for this crtc
 * @pipe: a crtc index created at load() with a new crtc object creation
 *	and the crtc object would be set to private->crtc array
 *	to get a crtc object corresponding to this pipe from private->crtc
 *	array when irq interrupt occured. the reason of using this pipe is that
 *	drm framework doesn't support multiple irq yet.
 *	we can refer to the crtc to current hardware interrupt occured through
 *	this pipe value.
 * @dpms: store the crtc dpms value
 * @mode: store the crtc mode value
 */
struct owl_drm_crtc {
	struct drm_crtc			drm_crtc;
	struct drm_plane		*plane;
	unsigned int			pipe;
	unsigned int			dpms;
	enum owl_crtc_mode		mode;
	wait_queue_head_t		pending_flip_queue;
	atomic_t			pending_flip;
};

static void owl_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);

	DRM_DEBUG_KMS("crtc[%d] mode[%d]\n", crtc->base.id, mode);

	if (owl_crtc->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	if (mode > DRM_MODE_DPMS_ON) {
		/* wait for the completion of page flip. */
		wait_event(owl_crtc->pending_flip_queue,
				atomic_read(&owl_crtc->pending_flip) == 0);
		drm_vblank_off(crtc->dev, owl_crtc->pipe);
	}

	owl_drm_fn_encoder(crtc, &mode, owl_drm_encoder_crtc_dpms);
	owl_crtc->dpms = mode;
}

/* 
 * ->prepare, 
 *  Drivers use it to perform device-specific operations required before setting the new mode.
 */
static void owl_drm_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("\n");

	/* drm framework doesn't check NULL. */
}

/*
 * ->commit,
 * Commit a mode. This operation is called after setting the new mode.
 * Upon return the device must use the new mode and be fully operational. 
 */
static void owl_drm_crtc_commit(struct drm_crtc *crtc)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);

	DRM_DEBUG_KMS("crtc pipe %d\n", owl_crtc->pipe);

	owl_drm_plane_commit(owl_crtc->plane);	
}

/*
 * ->mode_fixup,
 * adjust the requested mode or reject it completely
 */
static bool owl_drm_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("\n");

	/* drm framework doesn't check NULL */
	return true;
}


/*
 * ->mode_set
 * Set a new mode, position and frame buffer.
 *
 * Depending on the device requirements, 
 * the mode can be stored internally by the driver and applied in the '->commit' operation, 
 * or programmed to the hardware immediately. 
 */
static int owl_drm_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *plane = owl_crtc->plane;
	unsigned int crtc_w;
	unsigned int crtc_h;
	int pipe = owl_crtc->pipe;
	int ret;

	DRM_DEBUG_KMS(" pipe %d\n", pipe);

	/*
	 * copy the mode data adjusted by mode_fixup() into crtc->mode
	 * so that hardware can be seet to proper mode.
	 */
	memcpy(&crtc->mode, adjusted_mode, sizeof(*adjusted_mode));

	crtc_w = crtc->fb->width - x;
	crtc_h = crtc->fb->height - y;

	ret = owl_drm_plane_mode_set(plane, crtc, crtc->fb, 0, 0, crtc_w, crtc_h,
			x, y, crtc_w, crtc_h);
	if (ret)
		return ret;

	plane->crtc = crtc;
	plane->fb = crtc->fb;

	owl_drm_fn_encoder(crtc, &pipe, owl_drm_encoder_crtc_pipe);

	return 0;
}

static int owl_drm_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_plane *plane	= owl_crtc->plane;
	unsigned int crtc_w;
	unsigned int crtc_h;
	int ret;

	DRM_DEBUG_KMS("\n");

	/* when framebuffer changing is requested, crtc's dpms should be on */
	if (owl_crtc->dpms > DRM_MODE_DPMS_ON) {
		DRM_ERROR("failed framebuffer changing request.\n");
		return -EPERM;
	}

	crtc_w = crtc->fb->width - x;
	crtc_h = crtc->fb->height - y;

	ret = owl_drm_plane_mode_set(plane, crtc, crtc->fb, 0, 0, crtc_w, crtc_h,
			x, y, crtc_w, crtc_h);
	if (ret)
		return ret;

	owl_drm_crtc_commit(crtc);

	return 0;
}

static void owl_drm_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("\n");
	/* drm framework doesn't check NULL */
}

static void owl_drm_crtc_disable(struct drm_crtc *crtc)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);

	DRM_DEBUG_KMS("\n");

	owl_drm_plane_dpms(owl_crtc->plane, DRM_MODE_DPMS_OFF);
	owl_drm_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static struct drm_crtc_helper_funcs owl_crtc_helper_funcs = {
	.dpms		= owl_drm_crtc_dpms,
	.prepare	= owl_drm_crtc_prepare,
	.commit		= owl_drm_crtc_commit,
	.mode_fixup	= owl_drm_crtc_mode_fixup,
	.mode_set	= owl_drm_crtc_mode_set,
	.mode_set_base	= owl_drm_crtc_mode_set_base,
	.load_lut	= owl_drm_crtc_load_lut,
	.disable	= owl_drm_crtc_disable,
};

static int owl_drm_crtc_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct owl_drm_private *dev_priv = dev->dev_private;
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);
	struct drm_framebuffer *old_fb = crtc->fb;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("\n");

	/* when the page flip is requested, crtc's dpms should be on */
	if (owl_crtc->dpms > DRM_MODE_DPMS_ON) {
		DRM_ERROR("failed page flip request.\n");
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);

	if (event) {
		/*
		 * the pipe from user always is 0 so we can set pipe number
		 * of current owner to event.
		 */
		event->pipe = owl_crtc->pipe;

		/*
		 * To synchronize page flip to vertical blanking
		 * the driver will likely need to enable vertical blanking interrupts.
		 * It should call 'drm_vblank_get' for that purpose,
		 * and call drm_vblank_put after the page flip completes. 
		 */
		ret = drm_vblank_get(dev, owl_crtc->pipe);
		if (ret) {
			DRM_DEBUG("failed to acquire vblank counter\n");

			goto out;
		}

		spin_lock_irq(&dev->event_lock);
		list_add_tail(&event->base.link,
				&dev_priv->pageflip_event_list);
		atomic_set(&owl_crtc->pending_flip, 1);
		spin_unlock_irq(&dev->event_lock);

		/*
		 * If a page flip can be successfully scheduled, 
		 * the driver must set the drm_crtc->fb field 
		 * to the new framebuffer pointed to by fb. 
		 * This is important so that the reference counting on framebuffers stays balanced
		 */
		crtc->fb = fb;
		ret = owl_drm_crtc_mode_set_base(crtc, crtc->x, crtc->y,
				NULL);
		if (ret) {
			crtc->fb = old_fb;

			spin_lock_irq(&dev->event_lock);
			drm_vblank_put(dev, owl_crtc->pipe);
			list_del(&event->base.link);
			spin_unlock_irq(&dev->event_lock);

			goto out;
		}
	}
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static void owl_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(crtc);
	struct owl_drm_private *private = crtc->dev->dev_private;

	DRM_DEBUG_KMS("\n");

	private->crtc[owl_crtc->pipe] = NULL;

	drm_crtc_cleanup(crtc);
	kfree(owl_crtc);
}

static int owl_drm_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file_priv,
		uint32_t handle,
		uint32_t width,
		uint32_t height)
{
	struct drm_device *dev		 = crtc->dev;
	struct owl_drm_private *dev_priv = dev->dev_private;
	struct owl_drm_crtc *owl_crtc 	 = to_owl_crtc(crtc);

	DRM_DEBUG_KMS("crtc id %d, plane id %d\n",
			crtc->base.id, owl_crtc->plane->base.id);
	DRM_DEBUG_KMS("width %d, height %d, handle 0x%x\n",
			width, height, handle);
	return 0;
}

static int owl_drm_crtc_cursor_move(struct drm_crtc *crtc,
		int x, int y)
{
	struct drm_device *dev			= crtc->dev;
	struct owl_drm_private *dev_priv	= dev->dev_private;
	struct owl_drm_crtc *owl_crtc		= to_owl_crtc(crtc);
	int ret;

	DRM_DEBUG_KMS("x, y(%d, %d)\n", x, y);


	return 0;
}	

static int owl_drm_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev 		 = crtc->dev;
	struct owl_drm_private *dev_priv = dev->dev_private;
	struct owl_drm_crtc *owl_crtc 	 = to_owl_crtc(crtc);

	DRM_DEBUG_KMS("\n");

	if (property == dev_priv->crtc_mode_property) {
		enum owl_crtc_mode mode = val;

		if (mode == owl_crtc->mode)
			return 0;

		owl_crtc->mode = mode;

		switch (mode) {
			case CRTC_MODE_NORMAL:
				owl_drm_crtc_commit(crtc);
				break;
			case CRTC_MODE_BLANK:
				owl_drm_plane_dpms(owl_crtc->plane,
						DRM_MODE_DPMS_OFF);
				break;
			default:
				break;
		}

		return 0;
	}

	return -EINVAL;
}

static struct drm_crtc_funcs owl_crtc_funcs = {
	.cursor_set 	= owl_drm_crtc_cursor_set,
	.cursor_move 	= owl_drm_crtc_cursor_move,
	.set_config	= drm_crtc_helper_set_config,
	.page_flip	= owl_drm_crtc_page_flip,
	.destroy	= owl_drm_crtc_destroy,
	.set_property	= owl_drm_crtc_set_property,
};

static const struct drm_prop_enum_list mode_names[] = {
	{ CRTC_MODE_NORMAL, "normal" },
	{ CRTC_MODE_BLANK, "blank" },
};

static void owl_drm_crtc_attach_mode_property(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct owl_drm_private *dev_priv = dev->dev_private;
	struct drm_property *prop;

	DRM_DEBUG_KMS("\n");

	prop = dev_priv->crtc_mode_property;
	if (!prop) {
		prop = drm_property_create_enum(dev, 0, "mode", mode_names,
				ARRAY_SIZE(mode_names));
		if (!prop)
			return;

		dev_priv->crtc_mode_property = prop;
	}

	drm_object_attach_property(&crtc->base, prop, 0);
}

int owl_drm_crtc_create(struct drm_device *dev, unsigned int nr)
{
	struct owl_drm_private *private = dev->dev_private;
	struct owl_drm_crtc *owl_crtc;
	struct drm_crtc *crtc;

	DRM_DEBUG_KMS("start\n");

	owl_crtc = kzalloc(sizeof(*owl_crtc), GFP_KERNEL);
	if (!owl_crtc) {
		DRM_ERROR("failed to allocate rockchip crtc\n");
		return -ENOMEM;
	}

	owl_crtc->pipe = nr;
	owl_crtc->dpms = DRM_MODE_DPMS_OFF;
	init_waitqueue_head(&owl_crtc->pending_flip_queue);
	atomic_set(&owl_crtc->pending_flip, 0);

	owl_crtc->plane = owl_drm_plane_init(dev, 1 << nr, true);
	if (!owl_crtc->plane) {
		kfree(owl_crtc);
		return -ENOMEM;
	}

	crtc = &owl_crtc->drm_crtc;
	private->crtc[nr] = crtc;

	drm_crtc_init(dev, crtc, &owl_crtc_funcs);
	drm_crtc_helper_add(crtc, &owl_crtc_helper_funcs);

	owl_drm_crtc_attach_mode_property(crtc);

	DRM_DEBUG_KMS("end\n");
	return 0;
}

int owl_drm_crtc_enable_vblank(struct drm_device *dev, int crtc)
{
	struct owl_drm_private *private = dev->dev_private;
	struct owl_drm_crtc *owl_crtc =
		to_owl_crtc(private->crtc[crtc]);

	DRM_DEBUG_KMS("crtc dpms %d\n", owl_crtc->dpms);

	if (owl_crtc->dpms != DRM_MODE_DPMS_ON)
		return -EPERM;

	owl_drm_fn_encoder(private->crtc[crtc], &crtc,
			owl_drm_enable_vblank);

	return 0;
}

void owl_drm_crtc_disable_vblank(struct drm_device *dev, int crtc)
{
	struct owl_drm_private *private = dev->dev_private;
	struct owl_drm_crtc *owl_crtc =
		to_owl_crtc(private->crtc[crtc]);

	DRM_DEBUG_KMS("start\n");

	if (owl_crtc->dpms != DRM_MODE_DPMS_ON)
		return;

	owl_drm_fn_encoder(private->crtc[crtc], &crtc,
			owl_drm_disable_vblank);
}

void owl_drm_crtc_finish_pageflip(struct drm_device *dev, int crtc)
{
	struct owl_drm_private *dev_priv = dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct drm_crtc *drm_crtc = dev_priv->crtc[crtc];
	struct owl_drm_crtc *owl_crtc = to_owl_crtc(drm_crtc);
	unsigned long flags;

	//DRM_DEBUG_KMS("start\n");

	spin_lock_irqsave(&dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev_priv->pageflip_event_list,
			base.link) {
		/* if event's pipe isn't same as crtc then ignore it. */
		if (crtc != e->pipe)
			continue;

		list_del(&e->base.link);
		drm_send_vblank_event(dev, -1, e);
		drm_vblank_put(dev, crtc);
		atomic_set(&owl_crtc->pending_flip, 0);
		wake_up(&owl_crtc->pending_flip_queue);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}
