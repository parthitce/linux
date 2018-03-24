/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#include "owl_drv.h"

#define to_owl_encoder(x)	container_of(x, struct owl_encoder, base)

/*
 * owl specific encoder structure.
 */
struct owl_encoder {
	struct drm_encoder base;
};

static void owl_encoder_destroy(struct drm_encoder *encoder)
{
	struct owl_encoder *owl_encoder = to_owl_encoder(encoder);

	DBG_KMS("encoder=%u", encoder->base.id);

	drm_encoder_cleanup(encoder);
	kfree(owl_encoder);
}

static struct drm_encoder_funcs owl_encoder_funcs = {
	.destroy = owl_encoder_destroy,
};

/*
 * The CRTC drm_crtc_helper_set_mode() doesn't really give us the right
 * order.. the easiest way to work around this for now is to make all
 * the encoder-helper's no-op's and have the owl_crtc code take care
 * of the sequencing and call us in the right points.
 *
 * Eventually to handle connecting CRTCs to different encoders properly,
 * either the CRTC helpers need to change or we need to replace
 * drm_crtc_helper_set_mode(), but lets wait until atomic-modeset for
 * that.
 */

static void owl_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	DBG_KMS("encoder=%u, mode=%d", encoder->base.id, mode);
}

static bool owl_encoder_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	DBG_KMS("encoder=%u", encoder->base.id);
	return true;
}

static void owl_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DBG_KMS("encoder=%u", encoder->base.id);
}

static void owl_encoder_prepare(struct drm_encoder *encoder)
{
	DBG_KMS("encoder=%u", encoder->base.id);
}

static void owl_encoder_commit(struct drm_encoder *encoder)
{
	DBG_KMS("encoder=%u", encoder->base.id);
}

static void owl_encoder_disable(struct drm_encoder *encoder)
{
	DBG_KMS("encoder=%u", encoder->base.id);
}

static struct drm_encoder_helper_funcs owl_drm_encoder_helper_funcs = {
	.dpms       = owl_encoder_dpms,
	.mode_fixup	= owl_encoder_mode_fixup,
	.mode_set   = owl_encoder_mode_set,
	.prepare    = owl_encoder_prepare,
	.commit	    = owl_encoder_commit,
	.disable    = owl_encoder_disable,
};

struct drm_encoder *owl_encoder_init(struct drm_device *dev,
		unsigned int possible_crtcs, unsigned int possible_clones)
{
	struct owl_encoder *owl_encoder;
	struct drm_encoder *encoder;
	int ret;

	owl_encoder = kzalloc(sizeof(*owl_encoder), GFP_KERNEL);
	if (!owl_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	encoder = &owl_encoder->base;
	encoder->possible_crtcs = possible_crtcs;
	encoder->possible_clones = possible_clones;

	drm_encoder_init(dev, encoder, &owl_encoder_funcs, DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &owl_drm_encoder_helper_funcs);

	DBG_KMS("encoder=%u", encoder->base.id);
	return encoder;
fail:
	return ERR_PTR(ret);
}
