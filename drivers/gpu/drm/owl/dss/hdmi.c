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

#define DRIVER_NAME  "owldrm-hdmi"

#ifdef CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE
/* fake lcd mode:
 * fake_stat == 0 : fake_mode is uninitialized
 * fake_stat < 0  : fake_mode is really fake
 * fake_stat > 0  : fake_mode is corresponding to some real mode
 */
static struct owl_videomode fake_mode;
static int fake_stat = 0;

static int hdmi_init_fakemode(void)
{
	struct dispc_manager *mgr = dispc_manager_get(OWL_DRM_DISPLAY_PRIMARY);
	if (!mgr)
		return -ENOENT;

	/* duplicate lcd mode */
	memcpy(&fake_mode, &mgr->owl_panel->mode, sizeof(fake_mode));

	/* initialized as fake */
	fake_stat = -1;

	return 0;
}

static int hdmi_get_modes(struct owl_drm_panel *panel,
		struct owl_videomode *modes, int num_modes)
{
	int count = dispc_panel_get_modes(panel, modes, num_modes);

	/* fake_mode uninitialized or real */
	if (!count || fake_stat >= 0)
		return count;

	/* try to get the number of supported modes */
	if (!modes || !num_modes)
		return count + 1;

	/* duplicate the fake_mode */
	if (count < num_modes) {
		memcpy(&modes[count], &fake_mode, sizeof(fake_mode));
		return count + 1;
	}

	return count;
}

static bool hdmi_validate_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	/* only xres and yres are valid when fake_mode is really fake */
	if (fake_stat < 0 && fake_mode.xres == mode->xres && fake_mode.yres == mode->yres)
		return true;

	return dispc_panel_validate_mode(panel, mode);
}

static int hdmi_set_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	/* just skip modeset when fake_mode is really fake, since display engine
	 * will scale it to match the current mode
	 */
	if (fake_stat < 0 && fake_mode.xres == mode->xres && fake_mode.yres == mode->yres)
		return 0;

	return dispc_panel_set_mode(panel, mode);
}

static void hdmi_hotplug_cb(struct owl_panel *owl_panel, void *data, u32 status)
{
	struct dispc_manager *mgr = data;
	struct owl_videomode *modes;
	int num;

	/* 1. handle hotplug event */
	dispc_panel_default_hotplug(owl_panel, data, status);

	/* 2. take care of the fake mode */

	/* 1) panel disconnected, or fake_mode uninitialized */
	if (!status || !fake_stat)
		return;

	/* 2) get the mode list */
	if (owl_panel->n_modes) {
		modes = owl_panel->mode_list;
		num = owl_panel->n_modes;
	} else {
		modes = &owl_panel->mode;
		num = 1;
	}

	/* 3) find whether there is a corresponding real mode, if exist, copy it */
	while (--num >= 0) {
		if (fake_mode.xres == modes[num].xres &&
			fake_mode.yres == modes[num].yres) {
			memcpy(&fake_mode, &modes[num], sizeof(fake_mode));
			fake_stat = 1;
			break;
		}
	}

	DSS_DBG(mgr, "fake_mode(real=%d): %dx%d-%dHZ",
			fake_stat > 0, fake_mode.xres, fake_mode.yres, fake_mode.refresh);
}
#endif /* CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE */

static struct owl_drm_panel_funcs hdmi_panel_funcs = {
	.detect        = dispc_panel_detect,
	.prepare       = dispc_panel_prepare,
	.enable        = dispc_panel_enable,
	.disable       = dispc_panel_disable,
	.unprepare     = dispc_panel_unprepare,

#ifdef CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE
	.get_modes     = hdmi_get_modes,
	.validate_mode = hdmi_validate_mode,
	.set_mode      = hdmi_set_mode,
#else
	.get_modes     = dispc_panel_get_modes,
	.validate_mode = dispc_panel_validate_mode,
	.set_mode      = dispc_panel_set_mode,
#endif

	.enable_vblank   = dispc_panel_enable_vblank,
	.disable_vblank  = dispc_panel_disable_vblank,
};

static int hdmi_load(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_add_overlays(drm, subdrv);
	dispc_subdrv_add_panel(drm, subdrv, &hdmi_panel_funcs);
	return 0;
}

static int hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dispc_manager *mgr;

	mgr = dispc_manager_create(dev, OWL_DISPLAY_TYPE_HDMI);
	if (IS_ERR(mgr)) {
		DEV_ERR(dev, "dispc_manager_create failed");
		return PTR_ERR(mgr);
	}

	mgr->subdrv.load = hdmi_load;
	mgr->subdrv.unload = dispc_subdrv_unload;
	owl_subdrv_register(&mgr->subdrv);

#ifdef CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE
	hdmi_init_fakemode();
#endif

	/* registers vsync and hotplug call back */
	dispc_manager_register_vsync(mgr, dispc_panel_default_vsync);

#ifdef CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE
	dispc_manager_register_hotplug(mgr, hdmi_hotplug_cb);
#else
	dispc_manager_register_hotplug(mgr, dispc_panel_default_hotplug);
#endif

	/* hotplug detection */
	owl_panel_hpd_enable(mgr->owl_panel, true);

	return 0;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct dispc_manager *mgr = platform_get_drvdata(pdev);

	owl_subdrv_unregister(&mgr->subdrv);
	dispc_manager_destroy(mgr);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static bool hdmi_suspend_state;

static int hdmi_suspend(struct device *dev)
{
	struct dispc_manager *mgr = dev_get_drvdata(dev);

	hdmi_suspend_state = mgr->enabled;
	dispc_manager_set_enabled(mgr, false);

	return 0;
}

static int hdmi_resume(struct device *dev)
{
	struct dispc_manager *mgr = dev_get_drvdata(dev);

	dispc_manager_set_enabled(mgr, hdmi_suspend_state);

	return 0;
}
#endif

#ifdef CONFIG_PM
static SIMPLE_DEV_PM_OPS(hdmi_pm_ops, hdmi_suspend, hdmi_resume);
#endif

static struct platform_driver hdmi_platform_driver = {
	.probe  = hdmi_probe,
	.remove	= hdmi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm     = &hdmi_pm_ops,
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

	hdmi_platform_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
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
