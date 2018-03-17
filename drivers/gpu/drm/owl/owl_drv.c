/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"
#include "owl_gem.h"

#define DRIVER_NAME	 "owl"
#define DRIVER_DESC	 "OWL DRM"
#define DRIVER_DATE	 "20170801"
#define DRIVER_MAJOR	   1
#define DRIVER_MINOR	   0
#define DRIVER_PATCHLEVEL  0

/* platform device pointer for owl drm device. */
static struct platform_device *owl_platform_pdev;

static void owl_fb_output_poll_changed(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	if (priv->fbdev)
		drm_fb_helper_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs owl_mode_config_funcs = {
	.fb_create = owl_framebuffer_create,
	.output_poll_changed = owl_fb_output_poll_changed,
};

static void owl_modeset_free(struct drm_device *dev)
{
#ifdef HAVE_DRM_IRQ
	owl_irq_uninstall(dev);
#endif

	owl_subdrv_list_unload(dev);
	drm_mode_config_cleanup(dev);
}

static int owl_modeset_init(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	int num_crtcs, num_planes;
	int i, ret;

	drm_mode_config_init(dev);

	priv->possible_crtcs = (1u << MAX_CRTCS) - 1;
	priv->possible_clones = 0; /* (1u << MAX_CRTCS) - 1; */

	/* register subdrv encoders, connectors, panels and overlays */
	ret = owl_subdrv_list_load(dev);
	if (ret)
		goto fail;

#ifdef HAVE_DRM_IRQ
	ret = owl_irq_install(dev);
	if (ret)
		goto fail;
#else
	/* enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 */
	dev->irq_enabled = 1;
	/* with vblank_disable_allowed = 1, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	dev->vblank_disable_allowed = 1;

	/* register event callback from subdrv */
	{
		struct owl_drm_panel_callback_funcs callback = {
			.vsync = owl_crtc_handle_vblank,
			.hotplug = owl_crtc_handle_vblank,
		};

		owl_dssdev_register_callback(priv->dssdev, &callback);
	}
#endif

	num_planes = min(MAX_PLANES, priv->dssdev->num_overlay);
	num_crtcs = min3(MAX_CRTCS, num_planes, priv->dssdev->num_panel);

	/* planes are sorted by zpos from lowest to highest by default */
	for (i = 0; i < num_planes; i++) {
		struct drm_plane *plane;
		bool private_plane = (i < num_crtcs) || (i >= (num_planes - num_crtcs));
		plane = owl_plane_init(dev, private_plane);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			goto fail;
		}

		BUG_ON(priv->num_planes >= ARRAY_SIZE(priv->planes));
		priv->planes[priv->num_planes++] = plane;
	}

	for (i = 0; i < num_crtcs; i++) {
		struct drm_crtc *crtc;
		crtc = owl_crtc_init(dev, priv->planes[i], priv->planes[num_planes - 1 - i], i);
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			goto fail;
		}

		BUG_ON(priv->num_crtcs >= ARRAY_SIZE(priv->crtcs));
		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	ERR("registered %d planes, %d crtcs, %d encoders and %d connectors",
		priv->num_planes, priv->num_crtcs, priv->num_encoders,
		priv->num_connectors);

	dev->mode_config.min_width  = 32;
	dev->mode_config.min_height = 32;
	dev->mode_config.max_width  = 4096;
	dev->mode_config.max_height = 4096;
	dev->mode_config.funcs = &owl_mode_config_funcs;
	return 0;
fail:
	owl_modeset_free(dev);
	return ret;
}

static int owl_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct owl_drm_private *priv;
	int ret;

#if 0
	{
		/* For debug: DRM_UT_CORE, DRM_UT_DRIVER, DRM_UT_KMS, DRM_UT_PRIME */
		extern unsigned int drm_debug;
		drm_debug = DRM_UT_DRIVER | DRM_UT_KMS;
	}
#endif

	priv = devm_kzalloc(dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dssdev = owl_dssdev_init(dev);
	if (IS_ERR(priv->dssdev))
		return -ENOMEM;

	priv->wq = alloc_ordered_workqueue("owldrm", 0);
	if (!priv->wq)
		return -ENOMEM;

	dev->dev_private = (void *)priv;

	ret = owl_gem_init(dev);
	if (ret) {
		DEV_ERR(dev->dev, "owl_gem_init failed: ret=%d", ret);
		dev->dev_private = NULL;
		return ret;
	}

	ret = owl_modeset_init(dev);
	if (ret) {
		DEV_ERR(dev->dev, "owl_modeset_init failed: ret=%d", ret);
		dev->dev_private = NULL;
		return ret;
	}

	ret = drm_vblank_init(dev, priv->num_crtcs);
	if (ret)
		DEV_WARN(dev->dev, "could not init vblank");

	priv->fbdev = owl_fbdev_init(dev);
	if (!priv->fbdev) {
		DEV_WARN(dev->dev, "owl_fbdev_init failed");
		/* well, limp along without an fbdev.. maybe X11 will work? */
	}

	/* store off drm_device for use in pm ops */
	dev_set_drvdata(dev->dev, dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}

static int owl_drm_unload(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;

	DBG("unload: dev=%p", dev);

	drm_kms_helper_poll_fini(dev);
	drm_vblank_cleanup(dev);

	owl_fbdev_free(dev);
	owl_modeset_free(dev);
	owl_gem_deinit(dev);

	flush_workqueue(priv->wq);
	destroy_workqueue(priv->wq);

	dev->dev_private = NULL;
	dev_set_drvdata(dev->dev, NULL);

	return 0;
}

static int owl_drm_open(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;
	DBG("open: dev=%p, file=%p", dev, file);
	return 0;
}

static int owl_drm_firstopen(struct drm_device *dev)
{
	DBG("firstopen: dev=%p", dev);
	return 0;
}

static void owl_drm_lastclose(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	int ret = 0;

	DBG("lastclose: dev=%p", dev);

	if (!priv->fbdev)
		return;

	drm_modeset_lock_all(dev);
	ret = drm_fb_helper_restore_fbdev_mode(priv->fbdev);
	drm_modeset_unlock_all(dev);
	if (ret)
		DBG("failed to restore crtc mode");
}

static void owl_drm_preclose(struct drm_device *dev, struct drm_file *file)
{
	DBG("preclose: dev=%p, file=%p", dev, file);
}

static void owl_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	DBG("postclose: dev=%p, file=%p", dev, file);
}

static int owl_ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_owl_gem_new *args = data;

	if (args->flags & ~OWL_BO_FLAGS) {
		DEV_ERR(dev->dev, "invalid flags: %08x", args->flags);
		return -EINVAL;
	}

	return owl_gem_new_handle(dev, file_priv, args->size,
			args->flags, &args->handle);
}

static int owl_ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_owl_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret = 0;

	VERB("%p:%p: handle=%d", dev, file_priv, args->handle);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	args->offset = owl_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static struct drm_ioctl_desc owldrm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(OWL_GEM_NEW,  owl_ioctl_gem_new,  DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(OWL_GEM_INFO, owl_ioctl_gem_info, DRM_AUTH|DRM_UNLOCKED),
};

static const struct vm_operations_struct owl_gem_vm_ops = {
	.fault = owl_gem_fault,
	.open  = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations owldrm_driver_fops = {
	.owner   = THIS_MODULE,
	.open    = drm_open,
	.release = drm_release,
	.mmap    = owl_gem_mmap,
	.poll    = drm_poll,
	.fasync  = drm_fasync,
	.read    = drm_read,
	.llseek  = noop_llseek,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = drm_compat_ioctl,
#endif
};

static struct drm_driver owldrm_driver = {
	.driver_features    = DRIVER_HAVE_IRQ | DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load               = owl_drm_load,
	.unload             = owl_drm_unload,
	.open               = owl_drm_open,
	.firstopen          = owl_drm_firstopen,
	.lastclose          = owl_drm_lastclose,
	.preclose           = owl_drm_preclose,
	.postclose          = owl_drm_postclose,
	.get_vblank_counter	= drm_vblank_count,
#ifdef HAVE_DRM_IRQ
	.enable_vblank      = owl_irq_enable_vblank,
	.disable_vblank     = owl_irq_disable_vblank,
#else
	.enable_vblank		= owl_crtc_enable_vblank,
	.disable_vblank		= owl_crtc_disable_vblank,
#endif
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = owl_debugfs_init,
	.debugfs_cleanup    = owl_debugfs_cleanup,
#endif
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export   = owl_gem_prime_export,
	.gem_prime_import   = owl_gem_prime_import,
	.gem_init_object    = owl_gem_init_object,
	.gem_free_object    = owl_gem_free_object,
	.gem_vm_ops		    = &owl_gem_vm_ops,
	.dumb_create        = owl_gem_dumb_create,
	.dumb_map_offset    = owl_gem_dumb_map_offset,
	.dumb_destroy       = owl_gem_dumb_destroy,
	.ioctls	    = owldrm_ioctls,
	.num_ioctls = DRM_OWL_NUM_IOCTLS,
	.fops = &owldrm_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

#ifdef CONFIG_PM_SLEEP
static int owl_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(ddev);

	return 0;
}

static int owl_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_enable(ddev);

	return 0;
}
#endif

#ifdef CONFIG_PM
static int owl_runtime_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DBG("dev=%p", ddev);

	return 0;
}

static int owl_runtime_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DBG("dev=%p", ddev);

	return 0;
}
#endif

static const struct dev_pm_ops owl_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(owl_pm_suspend, owl_pm_resume)
	SET_RUNTIME_PM_OPS(owl_runtime_suspend, owl_runtime_resume, NULL)
};

/*
 * Platform driver:
 */
static int owl_pdev_probe(struct platform_device *pdev)
{
	DBG("%s", pdev->name);
	return drm_platform_init(&owldrm_driver, pdev);
}

static int owl_pdev_remove(struct platform_device *pdev)
{
	DBG("%s", pdev->name);
	drm_platform_exit(&owldrm_driver, pdev);
	return 0;
}

static struct platform_driver owl_platform_driver = {
	.probe		= owl_pdev_probe,
	.remove		= owl_pdev_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm     = &owl_pm_ops,
#endif
	},
};

static int __init owl_drm_init(void)
{
	int ret;
	struct platform_device_info pdev_info = {
		.name = DRIVER_NAME,
		.dma_mask = DMA_BIT_MASK(32),
	};

	ret = owl_lcd_register();
	if (ret) {
		pr_warn("owl_lcd_register failed (err=%d)\n", ret);
	}

	ret = owl_hdmi_register();
	if (ret) {
		pr_warn("owl_hdmi_register failed (err=%d)\n", ret);
	}

	ret = platform_driver_register(&owl_platform_driver);
	if (ret < 0) {
		pr_err("platform_driver_register: %s failed (err=%d)\n", DRIVER_NAME, ret);
		goto fail_unregister_subdrv;
	}

	owl_platform_pdev = platform_device_register_full(&pdev_info);
	if (IS_ERR(owl_platform_pdev)) {
		ret = PTR_ERR(owl_platform_pdev);
		pr_err("platform_device_register_simple: %s failed (err=%d)\n", DRIVER_NAME, ret);
		goto fail_unregister_driver;
	}

	return 0;
fail_unregister_driver:
	platform_driver_unregister(&owl_platform_driver);
fail_unregister_subdrv:
	owl_hdmi_unregister();
	owl_lcd_unregister();
	return ret;
}

static void __exit owl_drm_fini(void)
{
	DBG("fini");

	platform_device_unregister(owl_platform_pdev);
	platform_driver_unregister(&owl_platform_driver);

	owl_lcd_unregister();
	owl_hdmi_unregister();
}

/* need late_initcall() so we load after dss_driver's are loaded */
late_initcall(owl_drm_init);
module_exit(owl_drm_fini);

MODULE_DESCRIPTION("OWL DRM Display Driver");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL v2");
