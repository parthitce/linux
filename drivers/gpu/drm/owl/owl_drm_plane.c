/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <drm/owl_drm.h>
#include "owl_drm_drv.h"
#include "owl_drm_encoder.h"
#include "owl_drm_fb.h"
#include "owl_drm_gem.h"

#define to_owl_drm_plane(x)	container_of(x, struct owl_drm_plane, drm_plane)

struct owl_drm_plane {
	struct drm_plane		drm_plane;
	struct owl_drm_overlay		overlay;
	bool				enabled;
};

static const uint32_t formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV12MT,
};

/*
 * This function is to get X or Y size shown via screen. This needs length and
 * start position of CRTC.
 *
 *      <--- length --->
 * CRTC ----------------
 *      ^ start        ^ end
 *
 * There are six cases from a to f.
 *
 *             <----- SCREEN ----->
 *             0                 last
 *   ----------|------------------|----------
 * CRTCs
 * a -------
 *        b -------
 *        c --------------------------
 *                 d --------
 *                           e -------
 *                                  f -------
 */
static int owl_drm_plane_get_size(int start, unsigned length, unsigned last)
{
	int end = start + length;
	int size = 0;

	if (start <= 0) {
		if (end > 0)
			size = min_t(unsigned, end, last);
	} else if (start <= last) {
		size = min_t(unsigned, last - start, length);
	}

	return size;
}
/*		fb				crtc
		---------------------------------		
		|	|			|
		|	src_x			|
		|	|			|		
		|	------src_w------	|  (0, 0)------crtc_w----
		|-src_y-|		|	|	|		|
		|	|		|	|	|		|
		|	src_h		|	|	crtc_h		|			
		|	|		|	|------>|		|
		|	|		|	|	|		|
		|	-----------------	|	-----------------
		|				|
		|				|
		---------------------------------
		*/
int owl_drm_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y, unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	struct owl_drm_plane *owl_drm_plane = to_owl_drm_plane(plane);
	struct owl_drm_overlay *overlay     = &owl_drm_plane->overlay;
	unsigned int actual_w;
	unsigned int actual_h;
	int nr;
	int i;

	nr = owl_drm_fb_get_buf_cnt(fb);
	DRM_DEBUG_KMS("buf cnt: %d\n", nr);

	for (i = 0; i < nr; i++) {
		struct owl_drm_gem_buf *buffer = owl_drm_fb_buffer(fb, i);

		if (!buffer) {
			DRM_LOG_KMS("buffer is null\n");
			return -EFAULT;
		}

		overlay->dma_addr[i] = buffer->dma_addr;
		overlay->offsets[i] = fb->offsets[i];
		overlay->pitches[i] = fb->pitches[i];

		DRM_DEBUG_KMS("buffer: %d, dma_addr 0x%lx, offsets %d, pitches %d\n",
				i, (unsigned long)overlay->dma_addr[i],
				overlay->offsets[i], overlay->pitches[i]);
	}

	actual_w = owl_drm_plane_get_size(crtc_x, crtc_w, crtc->mode.hdisplay);
	actual_h = owl_drm_plane_get_size(crtc_y, crtc_h, crtc->mode.vdisplay);
	DRM_DEBUG_KMS("actual_w %d, actual_h %d\n", actual_w, actual_h);

	if (crtc_x < 0) {
		if (actual_w)
			src_x -= crtc_x;
		crtc_x = 0;
	}

	if (crtc_y < 0) {
		if (actual_h)
			src_y -= crtc_y;
		crtc_y = 0;
	}

	/* set drm framebuffer data. */
	overlay->fb_width 	= fb->width;
	overlay->fb_height 	= fb->height;
	overlay->bpp 		= fb->bits_per_pixel;
	overlay->pitch 		= fb->pitches[0];
	overlay->pixel_format 	= fb->pixel_format;

	/* set overlay range of source display */
	overlay->fb_x 		= src_x;
	overlay->fb_y 		= src_y;
	overlay->src_width 	= src_w;
	overlay->src_height 	= src_h;

	/* set overlay range to be displayed. */
	overlay->crtc_x 	= crtc_x;
	overlay->crtc_y 	= crtc_y;
	overlay->crtc_width 	= actual_w;
	overlay->crtc_height 	= actual_h;

	/* set drm mode data. */
	overlay->mode_width 	= crtc->mode.hdisplay;
	overlay->mode_height 	= crtc->mode.vdisplay;
	overlay->refresh 	= crtc->mode.vrefresh;
	overlay->pixclock 	= crtc->mode.clock*1000;
	overlay->scan_flag 	= crtc->mode.flags;

	DRM_DEBUG_KMS("overlay bpp %d, pitch %d, pixel_format %d\n",
			overlay->bpp, overlay->pitch, overlay->pixel_format);

	DRM_DEBUG_KMS("overlay fb width %d, height %d\n",
			overlay->fb_width, overlay->fb_height);

	DRM_DEBUG_KMS("overlay display frame: offset_x/y(%d, %d), width/height(%d, %d)\n",
			overlay->crtc_x, overlay->crtc_y,
			overlay->crtc_width, overlay->crtc_height);

	DRM_DEBUG_KMS("overlay source crop: offset_x/y(%d, %d), width/height(%d, %d)\n",
			overlay->fb_x, overlay->fb_y,
			overlay->src_width, overlay->src_height);

	owl_drm_fn_encoder(crtc, overlay, owl_drm_encoder_plane_mode_set);

	return 0;
}

void owl_drm_plane_commit(struct drm_plane *plane)
{
	struct owl_drm_plane *owl_drm_plane 	= to_owl_drm_plane(plane);
	struct owl_drm_overlay *overlay 	= &owl_drm_plane->overlay;

	DRM_DEBUG_KMS("zpos %d\n", overlay->zpos);

	owl_drm_fn_encoder(plane->crtc, &overlay->zpos,
			owl_drm_encoder_plane_commit);
}

void owl_drm_plane_dpms(struct drm_plane *plane, int mode)
{
	struct owl_drm_plane *owl_drm_plane = to_owl_drm_plane(plane);
	struct owl_drm_overlay *overlay = &owl_drm_plane->overlay;

	DRM_DEBUG_KMS(", mode %d\n", mode);

	if (mode == DRM_MODE_DPMS_ON) {
		if (owl_drm_plane->enabled)
			return;

		owl_drm_fn_encoder(plane->crtc, &overlay->zpos,
				owl_drm_encoder_plane_enable);

		owl_drm_plane->enabled = true;
	} else {
		if (!owl_drm_plane->enabled)
			return;

		owl_drm_fn_encoder(plane->crtc, &overlay->zpos,
				owl_drm_encoder_plane_disable);

		owl_drm_plane->enabled = false;
	}
}

static int owl_update_plane(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	int ret;

	DRM_DEBUG_KMS("\n");
	ret = owl_drm_plane_mode_set(plane, crtc, fb, crtc_x, crtc_y,
			crtc_w, crtc_h, src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (ret < 0)
		return ret;
	plane->crtc = crtc;

	owl_drm_plane_commit(plane);
	owl_drm_plane_dpms(plane, DRM_MODE_DPMS_ON);
	return 0;
}

static int owl_disable_plane(struct drm_plane *plane)
{
	DRM_DEBUG_KMS("\n");

	owl_drm_plane_dpms(plane, DRM_MODE_DPMS_OFF);

	return 0;
}

static void owl_drm_plane_destroy(struct drm_plane *plane)
{
	struct owl_drm_plane *owl_drm_plane = to_owl_drm_plane(plane);

	DRM_DEBUG_KMS("\n");

	owl_disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(owl_drm_plane);
}

static int owl_drm_plane_set_property(struct drm_plane *plane,
		struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct owl_drm_plane *owl_drm_plane = to_owl_drm_plane(plane);
	struct owl_drm_private *dev_priv = dev->dev_private;

	DRM_DEBUG_KMS("id %d, val %d\n", plane->base.id, val);

	if (property == dev_priv->plane_zpos_property) {
		owl_drm_plane->overlay.zpos = val;
		return 0;
	}

	return -EINVAL;
}

static struct drm_plane_funcs owl_drm_plane_funcs = {
	.update_plane	= owl_update_plane,
	.disable_plane	= owl_disable_plane,
	.destroy	= owl_drm_plane_destroy,
	.set_property	= owl_drm_plane_set_property,
};

static void owl_drm_plane_attach_zpos_property(struct drm_plane *plane)
{
	struct drm_device *dev			= plane->dev;
	struct owl_drm_private *dev_priv	= dev->dev_private;
	struct drm_property *prop;

	DRM_DEBUG_KMS("\n");

	prop = dev_priv->plane_zpos_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zpos", 0,
				MAX_PLANE - 1);
		if (!prop)
			return;

		dev_priv->plane_zpos_property = prop;
	}

	drm_object_attach_property(&plane->base, prop, 0);
}

struct drm_plane *owl_drm_plane_init(struct drm_device *dev,
		unsigned int possible_crtcs, bool priv)
{
	struct owl_drm_plane *owl_drm_plane;
	int err;

	DRM_DEBUG_KMS("possible_crtcs %d, priv %d\n",
			possible_crtcs, priv);

	owl_drm_plane = kzalloc(sizeof(struct owl_drm_plane), GFP_KERNEL);
	if (!owl_drm_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return NULL;
	}

	err = drm_plane_init(dev, &owl_drm_plane->drm_plane, possible_crtcs,
			&owl_drm_plane_funcs, formats, ARRAY_SIZE(formats),
			priv);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(owl_drm_plane);
		return NULL;
	}

	if (priv)
		owl_drm_plane->overlay.zpos = DEFAULT_ZPOS;
	else
		owl_drm_plane_attach_zpos_property(&owl_drm_plane->drm_plane);

	return &owl_drm_plane->drm_plane;
}
