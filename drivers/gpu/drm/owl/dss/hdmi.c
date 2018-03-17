/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include "dispc.h"

#define HDMI_DRIVER_NAME  "owl-drm-hdmi"

static bool hdmi_panel_detect(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
 	bool is_connected = owl_panel_hpd_is_connected(mgr->owl_panel);

	DBG("is connected %d", is_connected);
	return is_connected;
}

static struct owl_drm_panel_funcs hdmi_panel_funcs = {
	.detect        = hdmi_panel_detect,

	.prepare       = dispc_panel_prepare,
	.enable        = dispc_panel_enable,
	.disable       = dispc_panel_disable,
	.unprepare     = dispc_panel_unprepare,

	.get_modes     = dispc_panel_get_modes,
	.validate_mode = dispc_panel_validate_mode,

	.enable_vblank   = dispc_panel_enable_vblank,
	.disable_vblank  = dispc_panel_disable_vblank,
};

static int hdmi_load(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_add_overlays(drm, subdrv);
	dispc_subdrv_add_panels(drm, subdrv, &hdmi_panel_funcs);
	return 0;
}

static void hdmi_unload(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_remove_overlays(drm, subdrv);
	dispc_subdrv_remove_panels(drm, subdrv);
}

static int hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct owl_drm_subdrv *subdrv;
	struct dispc_manager *mgr;

	mgr = dispc_manager_init(dev, OWL_DISPLAY_TYPE_HDMI);
	if (IS_ERR(mgr)) {
		DEV_ERR(dev, "dispc_manager_init failed");
		return PTR_ERR(mgr);
	}

	/* initial subdrv */
	subdrv = &mgr->subdrv;
	subdrv->display_type = OWL_DISPLAY_TYPE_HDMI;
	subdrv->dev = dev;
	subdrv->load = hdmi_load;
	subdrv->unload = hdmi_unload;
	owl_subdrv_register(subdrv);

	/* registers vsync and hotplug call back */
	owl_panel_vsync_cb_set(mgr->owl_panel, dispc_panel_vsync_cb, mgr);
	owl_panel_hotplug_cb_set(mgr->owl_panel, dispc_panel_hotplug_cb, mgr);

	/* enable hpd detect */
	owl_panel_hpd_enable(mgr->owl_panel, 0);

	return 0;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct dispc_manager *mgr = platform_get_drvdata(pdev);
	owl_subdrv_unregister(&mgr->subdrv);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hdmi_suspend(struct device *dev)
{
	DEV_DBG(dev, "");
	return 0;
}

static int hdmi_resume(struct device *dev)
{
	DEV_DBG(dev, "");
	return 0;
}
#endif

static const struct dev_pm_ops hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hdmi_suspend, hdmi_resume)
};

struct platform_driver hdmi_platform_driver = {
	.probe  = hdmi_probe,
	.remove	= hdmi_remove,
	.driver = {
		.name = HDMI_DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &hdmi_pm_ops,
#endif
	},
};

static struct platform_device *hdmi_platform_pdev;

int owl_hdmi_register(void)
{
	int ret;

	ret = platform_driver_register(&hdmi_platform_driver);
	if (ret)
		goto fail;

	hdmi_platform_pdev = platform_device_register_simple(HDMI_DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(hdmi_platform_pdev)) {
		ret = PTR_ERR(hdmi_platform_pdev);
		goto fail_unregister_driver;
	}

	return 0;
fail_unregister_driver:
	platform_driver_unregister(&hdmi_platform_driver);
fail:
	return ret;
}

void owl_hdmi_unregister(void)
{
	platform_device_unregister(hdmi_platform_pdev);
	platform_driver_unregister(&hdmi_platform_driver);
}
