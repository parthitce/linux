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
static struct owl_videomode fake_mode;

static int hdmi_init_fakemode(void)
{
	struct dispc_manager *mgr = dispc_manager_get(OWL_DRM_DISPLAY_PRIMARY);
	if (!mgr)
		return -ENOENT;

	memcpy(&fake_mode, &mgr->owl_panel->mode, sizeof(fake_mode));
	return 0;
}

static int hdmi_get_modes(struct owl_drm_panel *panel,
		struct owl_videomode *modes, int num_modes)
{
	int count = dispc_panel_get_modes(panel, modes, num_modes);

	if (!count || !fake_mode.xres || !fake_mode.yres)
		return count;

	/* try to get the number of supported modes */
	if (!modes || !num_modes)
		return count + 1;

	if (count < num_modes) {
		memcpy(&modes[count], &fake_mode, sizeof(fake_mode));
		return count + 1;
	}

	return count;
}

static bool hdmi_validate_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	if (dispc_panel_validate_mode(panel, mode))
		return true;

	/* only xres and yres are valid in fake mode */
	if (fake_mode.xres && fake_mode.xres == mode->xres  &&
		fake_mode.yres && fake_mode.yres == mode->yres)
		return true;

	return false;
}

static int hdmi_set_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	struct owl_videomode *vmodes;
	int ret, cnt, best = 0, weight = INT_MAX;

	/* 1) if not the fake mode, just set it */
	if (mode->xres != fake_mode.xres || mode->yres != fake_mode.yres)
		return dispc_panel_set_mode(panel, mode);

	/* 2) list the supported modes by panel */
	cnt = dispc_panel_get_modes(panel, NULL, 0);
	if (!cnt)
		return -ENOTTY;

	vmodes = kcalloc(cnt, sizeof(*vmodes), GFP_KERNEL);
	if (!vmodes)
		return -ENOMEM;

	cnt = dispc_panel_get_modes(panel, vmodes, cnt);

	/* 3) find the best mode */
	while (--cnt >= 0) {
		int w = abs(vmodes[cnt].xres - mode->xres) * abs(vmodes[cnt].yres - mode->yres);
		if (w < weight) {
			weight = w;
			best = cnt;
		}

		if (w == 0)
			break;
	}

	DSS_DBG(mgr, "match fake-mode=%dx%d -> real-mode=%dx%d",
			fake_mode.xres, fake_mode.yres, vmodes[best].xres, vmodes[best].yres);

	/* 4) set the matched mode */
	ret = dispc_panel_set_mode(panel, &vmodes[best]);

	kfree(vmodes);
	return ret;
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

	/* hotplug detection */
	owl_panel_hpd_enable(mgr->owl_panel, false);

#ifdef CONFIG_DRM_OWL_HDMI_FAKE_LCD_MODE
	hdmi_init_fakemode();
#endif

	return 0;
}

static int hdmi_remove(struct platform_device *pdev)
{
	struct dispc_manager *mgr = platform_get_drvdata(pdev);
	owl_subdrv_unregister(&mgr->subdrv);
	return 0;
}

static struct platform_driver hdmi_platform_driver = {
	.probe  = hdmi_probe,
	.remove	= hdmi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
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
