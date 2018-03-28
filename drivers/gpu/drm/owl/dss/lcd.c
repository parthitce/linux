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

#define DRIVER_NAME  "owldrm-lcd"

static struct owl_drm_panel_funcs lcd_panel_funcs = {
	.detect        = dispc_panel_detect,
	.prepare       = dispc_panel_prepare,
	.enable        = dispc_panel_enable,
	.disable       = dispc_panel_disable,
	.unprepare     = dispc_panel_unprepare,

	.get_modes     = dispc_panel_get_modes,
	.validate_mode = dispc_panel_validate_mode,

	.enable_vblank   = dispc_panel_enable_vblank,
	.disable_vblank  = dispc_panel_disable_vblank,
};

static int lcd_load(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_add_overlays(drm, subdrv);
	dispc_subdrv_add_panels(drm, subdrv, &lcd_panel_funcs);
	return 0;
}

static void lcd_unload(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_remove_overlays(drm, subdrv);
	dispc_subdrv_remove_panels(drm, subdrv);	
}

static int lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct owl_drm_subdrv *subdrv;
	struct dispc_manager *mgr;

	mgr = dispc_manager_create(dev, OWL_DISPLAY_TYPE_LCD);
	if (IS_ERR(mgr)) {
		DEV_ERR(dev, "dispc_manager_create failed");
		return PTR_ERR(mgr);
	}

	/* initial subdrv */
	subdrv = &mgr->subdrv;
	subdrv->display_type = OWL_DISPLAY_TYPE_LCD;
	subdrv->dev = dev;
	subdrv->load = lcd_load;
	subdrv->unload = lcd_unload;
	owl_subdrv_register(subdrv);

	/* registers vsync call back */
	dispc_panel_register_vsync(mgr, dispc_panel_default_vsync);

	return 0;
}

static int lcd_remove(struct platform_device *pdev)
{
	struct dispc_manager *mgr = platform_get_drvdata(pdev);

	owl_subdrv_unregister(&mgr->subdrv);
	dispc_manager_destroy(mgr);

	return 0;
}

static struct platform_driver lcd_platform_driver = {
	.probe  = lcd_probe,
	.remove	= lcd_remove,
	.driver	= {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static struct platform_device *lcd_platform_pdev;

int owl_lcd_register(void)
{
	int ret;

	ret = platform_driver_register(&lcd_platform_driver);
	if (ret)
		goto fail;

	lcd_platform_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(lcd_platform_pdev)) {
		ret = PTR_ERR(lcd_platform_pdev);
		goto fail_unregister_driver;
	}

	return 0;
fail_unregister_driver:
	platform_driver_unregister(&lcd_platform_driver);
fail:
	return ret;
}

void owl_lcd_unregister(void)
{
	platform_device_unregister(lcd_platform_pdev);
	platform_driver_unregister(&lcd_platform_driver);
}
