/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"
#include "dss/dispc.h"

static LIST_HEAD(owldrm_subdrv_list);

struct owl_drm_dssdev *owl_dssdev_init(struct drm_device *drm)
{
	struct owl_drm_dssdev *dssdev;

	dssdev = devm_kzalloc(drm->dev, sizeof(*dssdev), GFP_KERNEL);
	if (!dssdev)
		return ERR_PTR(-ENOMEM);

	dssdev->drm = drm;
	INIT_LIST_HEAD(&dssdev->panel_list);
	INIT_LIST_HEAD(&dssdev->overlay_list);

	return dssdev;
}

int owl_dssdev_register_callback(struct owl_drm_dssdev *dssdev,
		struct owl_drm_panel_callback_funcs *cbs)
{
	struct owl_drm_panel *panel;

	list_for_each_entry(panel, &dssdev->panel_list, list) {
		memcpy(&panel->callbacks, cbs, sizeof(*cbs));
	}

	return 0;
}

struct owl_drm_panel *owl_dssdev_get_panel(struct owl_drm_dssdev *dssdev,
		struct drm_mode_object *obj)
{
	struct owl_drm_panel *panel;

	if (obj->type == DRM_MODE_OBJECT_CONNECTOR) {
		struct drm_connector *connector = obj_to_connector(obj);
		list_for_each_entry(panel, &dssdev->panel_list, list) {
			if (panel->connector == connector)
				return panel;
		}

		return NULL;
	}

	if (obj->type == DRM_MODE_OBJECT_ENCODER) {
		struct drm_encoder *encoder = obj_to_encoder(obj);
		list_for_each_entry(panel, &dssdev->panel_list, list) {
			if (panel->connector &&
				panel->connector->encoder == encoder)
				return panel;
		}

		return NULL;
	}

	if (obj->type == DRM_MODE_OBJECT_CRTC) {
		struct drm_crtc *crtc = obj_to_crtc(obj);
		list_for_each_entry(panel, &dssdev->panel_list, list) {
			if (panel->connector &&
				panel->connector->encoder &&
				panel->connector->encoder->crtc == crtc)
				return panel;
		}

		return NULL;
	}

	return NULL;
}

struct owl_drm_overlay *owl_dssdev_alloc_overlay(struct owl_drm_dssdev *dssdev,
		struct drm_plane *plane)
{
	struct owl_drm_overlay *overlay;

	list_for_each_entry(overlay, &dssdev->overlay_list, list) {
		if (!overlay->plane) {
			overlay->plane = plane;
			return overlay;
		}
	}

	return NULL;
}

int owl_dssdev_free_overlay(struct owl_drm_overlay *overlay)
{
	if (overlay->panel) {
		DEV_WARN(overlay->drm->dev, "free overlay attached to panel %p", overlay->panel);
		owl_dssdev_detach_overlay(overlay, overlay->panel);
	}

	overlay->plane = NULL;
	return 0;
}

int owl_dssdev_attach_overlay(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel)
{
	struct owl_drm_overlay *ovr = NULL;

	if (!overlay || !panel)
		return -EINVAL;

	if (overlay->panel && overlay->panel != panel) {
		DEV_WARN(overlay->drm->dev, 
			"overlay %p already attached to another panel %p, will detach first",
			overlay, overlay->panel);
		owl_dssdev_detach_overlay(overlay, overlay->panel);
	}

	if (overlay->funcs->attach) {
		int ret = overlay->funcs->attach(overlay, panel);
		if (ret) {
			DEV_ERR(overlay->drm->dev, "overlay %p fail to attach to panel %p",
					overlay, panel);
			return ret;
		}
	}

	/* sort by zpos from lowest to highest */
	list_for_each_entry(ovr, &panel->attach_list, attach_list) {
		if (ovr->zpos >= overlay->zpos)
			break;
	}

	list_add_tail(&overlay->attach_list, &ovr->attach_list);

	overlay->panel = panel;
	panel->num_attach++;

	return 0;
}

int owl_dssdev_detach_overlay(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel)
{
	if (!panel || !overlay || overlay->panel != panel)
		return -EINVAL;

	if (overlay->funcs->detach)
		overlay->funcs->detach(overlay, panel);

	list_del(&overlay->attach_list);
	INIT_LIST_HEAD(&overlay->attach_list);

	overlay->panel = NULL;
	BUG_ON(--panel->num_attach < 0);

	return 0;
}

/*******************************************************************************
 * subdrv ops
 ******************************************************************************/
int owl_subdrv_register(struct owl_drm_subdrv *subdrv)
{
	list_add_tail(&subdrv->list, &owldrm_subdrv_list);
	return 0;
}

void owl_subdrv_unregister(struct owl_drm_subdrv *subdrv)
{
	list_del(&subdrv->list);
}

int owl_subdrv_register_overlay(struct owl_drm_subdrv *subdrv,
		struct owl_drm_overlay *overlay)
{
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(overlay->drm);

	INIT_LIST_HEAD(&overlay->attach_list);
	list_add_tail(&overlay->list, &dssdev->overlay_list);
	dssdev->num_overlay++;
	return 0;
}

void owl_subdrv_unregister_overlay(struct owl_drm_subdrv *subdrv,
		struct owl_drm_overlay *overlay)
{
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(overlay->drm);

	if (overlay->panel)
		owl_dssdev_detach_overlay(overlay, overlay->panel);

	list_del(&overlay->list);
	BUG_ON(--dssdev->num_overlay < 0);
}

int owl_subdrv_register_panel(struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel *panel)
{
	struct device *dev = panel->drm->dev;
	struct owl_drm_private *priv = panel->drm->dev_private;
	struct owl_drm_dssdev *dssdev = priv->dssdev;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int connector_type = DRM_MODE_CONNECTOR_Unknown;
	int ret = 0;

	switch (subdrv->display_type) {
	case OWL_DISPLAY_TYPE_LCD:
		connector_type = DRM_MODE_CONNECTOR_LVDS;
		break;
	case OWL_DISPLAY_TYPE_HDMI:
		connector_type = DRM_MODE_CONNECTOR_HDMIA;
		break;
	default:
		DEV_ERR(dev, "unrecognized display type %x", subdrv->display_type);
		return -EINVAL;
	}

	/* create and initialize a encoder for this sub driver. */
	encoder = owl_encoder_init(dssdev->drm, (1u << OWL_DRM_NUM_DISPLAY_TYPES) - 1, 0);
	if (IS_ERR(encoder)) {
		DEV_ERR(dev, "panel %x: fail to create encoder", subdrv->display_type);
		return PTR_ERR(encoder);
	}

	/*
	 * create and initialize a connector for this sub driver and
	 * attach the encoder created above to the connector.
	 */
	connector = owl_connector_init(dssdev->drm, connector_type, encoder, panel);
	if (IS_ERR(connector)) {
		DEV_ERR(dev, "panel %x: fail to create connector", subdrv->display_type);
		ret = PTR_ERR(connector);
		goto err_destroy_encoder;
	}

	panel->connector = connector;

	INIT_LIST_HEAD(&panel->attach_list);
	list_add_tail(&panel->list, &dssdev->panel_list);
	dssdev->num_panel++;

	BUG_ON(priv->num_encoders >= ARRAY_SIZE(priv->encoders));
	priv->encoders[priv->num_encoders++] = encoder;
	BUG_ON(priv->num_connectors >= ARRAY_SIZE(priv->connectors));
	priv->connectors[priv->num_connectors++] = connector;

	return 0;
err_destroy_encoder:
	encoder->funcs->destroy(encoder);
	return ret;
}

void owl_subdrv_unregister_panel(struct owl_drm_subdrv *subdrv, struct owl_drm_panel *panel)
{
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(panel->drm);
	struct owl_drm_overlay *overlay, *n;

	/* FIXME: should we destroy the encoder and connector here ? */
	list_for_each_entry_safe(overlay, n, &panel->attach_list, attach_list) {
		owl_dssdev_detach_overlay(overlay, panel);
	}

	list_del(&panel->list);
	BUG_ON(--dssdev->num_panel < 0);
}

int owl_subdrv_list_load(struct drm_device *drm)
{
	struct owl_drm_subdrv *subdrv, *n;
	int fine_cnt = 0;

	list_for_each_entry_safe(subdrv, n, &owldrm_subdrv_list, list) {
		if (subdrv->load && !subdrv->load(drm, subdrv))
			fine_cnt++;
	}

	return fine_cnt ? 0 : -EINVAL;
}

void owl_subdrv_list_unload(struct drm_device *drm)
{
	struct owl_drm_subdrv *subdrv;

	list_for_each_entry(subdrv, &owldrm_subdrv_list, list) {
		if (subdrv->unload)
			subdrv->unload(drm, subdrv);
	}
}
