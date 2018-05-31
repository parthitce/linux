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

static int lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dispc_manager *mgr;

	mgr = dispc_manager_create(dev, OWL_DISPLAY_TYPE_LCD);
	if (IS_ERR(mgr)) {
		DEV_ERR(dev, "dispc_manager_create failed");
		return PTR_ERR(mgr);
	}

	mgr->subdrv.load = dispc_subdrv_load;
	mgr->subdrv.unload = dispc_subdrv_unload;
	owl_subdrv_register(&mgr->subdrv);

	/* registers vsync call back */
	dispc_manager_register_vsync(mgr, dispc_panel_default_vsync);

	return 0;
}

static int lcd_remove(struct platform_device *pdev)
{
	struct dispc_manager *mgr = platform_get_drvdata(pdev);

	owl_subdrv_unregister(&mgr->subdrv);
	dispc_manager_destroy(mgr);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static bool lcd_suspend_state;

static int lcd_suspend(struct device *dev)
{
	struct dispc_manager *mgr = dev_get_drvdata(dev);

	lcd_suspend_state = mgr->enabled;
	dispc_manager_set_enabled(mgr, false);

	return 0;
}

static int lcd_resume(struct device *dev)
{
	struct dispc_manager *mgr = dev_get_drvdata(dev);

	dispc_manager_set_enabled(mgr, lcd_suspend_state);

	return 0;
}
#endif

#ifdef CONFIG_PM
static SIMPLE_DEV_PM_OPS(lcd_pm_ops, lcd_suspend, lcd_resume);
#endif

static struct platform_driver lcd_platform_driver = {
	.probe  = lcd_probe,
	.remove	= lcd_remove,
	.driver	= {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm     = &lcd_pm_ops,
#endif
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
