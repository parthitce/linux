/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#define DRM_DEBUG_CODE
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <drm/owl_drm.h>

#include "owl_drm_drv.h"
#include "owl_drm_crtc.h"
#include "owl_drm_encoder.h"
#include "owl_drm_fbdev.h"
#include "owl_drm_fb.h"
#include "owl_drm_gem.h"
#include "owl_drm_plane.h"
#include "owl_drm_dmabuf.h"
#include "owl_drm_iommu.h"

#define DRIVER_NAME	"owl"
#define DRIVER_DESC	"Owl SoC DRM"
#define DRIVER_DATE	"20170801"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define VBLANK_OFF_DELAY	50000

/* platform device pointer for owl drm device. */
static struct platform_device *owl_drm_pdev;
static struct platform_device *owl_drm_lcd_pdev;
static struct platform_device *owl_drm_hdmi_pdev;

static int owl_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct owl_drm_private *private;
	int ret;
	int nr;

	DRM_DEBUG_DRIVER("%s, start\n", __func__);

	private = kzalloc(sizeof(struct owl_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&private->pageflip_event_list);
	dev->dev_private = (void *)private;

	/*
	 * create mapping to manage iommu table and set a pointer to iommu
	 * mapping structure to iommu_mapping of private data.
	 * also this iommu_mapping can be used to check if iommu is supported
	 * or not.
	 */
	ret = drm_create_iommu_mapping(dev);
	if (ret < 0) {
		DRM_ERROR("failed to create iommu mapping.\n");
		goto err_crtc;
	}

	drm_mode_config_init(dev);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	owl_drm_mode_config_init(dev);

	/*
	 * OWL is enough to have two CRTCs and each crtc would be used
	 * without dependency of hardware.
	 */
	for (nr = 0; nr < MAX_CRTC; nr++) {
		ret = owl_drm_crtc_create(dev, nr);
		if (ret)
			goto err_release_iommu_mapping;
	}

	for (nr = 0; nr < MAX_PLANE; nr++) {
		struct drm_plane *plane;
		unsigned int possible_crtcs = (1 << MAX_CRTC) - 1;

		plane = owl_drm_plane_init(dev, possible_crtcs, false);
		if (!plane)
			goto err_release_iommu_mapping;
	}

	ret = drm_vblank_init(dev, MAX_CRTC);
	if (ret)
		goto err_release_iommu_mapping;

	/*
	 * probe sub drivers such as display controller and hdmi driver,
	 * that were registered at probe() of platform driver
	 * to the sub driver and create encoder and connector for them.
	 */
	ret = owl_drm_device_register(dev);
	if (ret)
		goto err_vblank;

	/* setup possible_clones. */
	owl_drm_encoder_setup(dev);

	/*
	 * create and configure fb helper and also owl specific
	 * fbdev object.
	 */
	ret = owl_drm_fbdev_init(dev);
	if (ret) {
		DRM_ERROR("failed to initialize drm fbdev\n");
		goto err_drm_device;
	}

	drm_vblank_offdelay = VBLANK_OFF_DELAY;

	DRM_DEBUG_DRIVER("%s, end\n", __func__);

	return 0;

err_drm_device:
	owl_drm_device_unregister(dev);
err_vblank:
	drm_vblank_cleanup(dev);
err_release_iommu_mapping:
	drm_release_iommu_mapping(dev);
err_crtc:
	drm_mode_config_cleanup(dev);
	kfree(private);

	return ret;
}

static int owl_drm_unload(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	owl_drm_fbdev_fini(dev);
	owl_drm_device_unregister(dev);
	drm_vblank_cleanup(dev);
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);

	drm_release_iommu_mapping(dev);
	kfree(dev->dev_private);

	dev->dev_private = NULL;

	return 0;
}

static int owl_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_owl_file_private *file_priv;

	DRM_DEBUG_DRIVER("%s\n", __func__);

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	return owl_drm_subdrv_open(dev, file);
}

static void owl_drm_preclose(struct drm_device *dev,
		struct drm_file *file)
{
	struct owl_drm_private *private = dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	unsigned long flags;

	DRM_DEBUG_DRIVER("%s\n", __func__);

	/* release events of current file */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_for_each_entry_safe(e, t, &private->pageflip_event_list,
			base.link) {
		if (e->base.file_priv == file) {
			list_del(&e->base.link);
			e->base.destroy(&e->base);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	owl_drm_subdrv_close(dev, file);
}

static void owl_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	if (!file->driver_priv)
		return;

	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static void owl_drm_lastclose(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	owl_drm_fbdev_restore_mode(dev);
}

static const struct vm_operations_struct owl_drm_gem_vm_ops = {
	.fault = owl_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_ioctl_desc owl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(OWL_GEM_CREATE, owl_drm_gem_create_ioctl,
			DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OWL_GEM_MAP_OFFSET,
			owl_drm_gem_map_offset_ioctl, DRM_UNLOCKED |
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OWL_GEM_MMAP,
			owl_drm_gem_mmap_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OWL_GEM_GET,
			owl_drm_gem_get_ioctl, DRM_UNLOCKED),
};

static const struct file_operations owl_drm_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= owl_drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl 	= drm_compat_ioctl,
#endif
	.release	= drm_release,
};

static struct drm_driver owl_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ |
		DRIVER_MODESET |
		DRIVER_GEM |
		DRIVER_PRIME,

	.load			= owl_drm_load,
	.unload			= owl_drm_unload,
	.open			= owl_drm_open,
	.preclose		= owl_drm_preclose,
	.lastclose		= owl_drm_lastclose,
	.postclose		= owl_drm_postclose,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= owl_drm_crtc_enable_vblank,
	.disable_vblank		= owl_drm_crtc_disable_vblank,
	.gem_init_object	= owl_drm_gem_init_object,
	.gem_free_object	= owl_drm_gem_free_object,
	.gem_vm_ops		= &owl_drm_gem_vm_ops,
	.dumb_create		= owl_drm_gem_dumb_create,
	.dumb_map_offset	= owl_drm_gem_dumb_map_offset,
	.dumb_destroy		= owl_drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= owl_dmabuf_prime_export,
	.gem_prime_import	= owl_dmabuf_prime_import,
	.ioctls			= owl_ioctls,
	.fops			= &owl_drm_driver_fops,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

static int owl_drm_platform_probe(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	owl_drm_driver.num_ioctls = DRM_ARRAY_SIZE(owl_ioctls);

	return drm_platform_init(&owl_drm_driver, pdev);
}

static int owl_drm_platform_remove(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	drm_platform_exit(&owl_drm_driver, pdev);

	return 0;
}

static struct platform_driver owl_drm_platform_driver = {
	.probe		= owl_drm_platform_probe,
	.remove		= owl_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "owl-drm",
	},
};

static int __init owl_drm_init(void)
{
	int ret;

	/* for debug.
	   DRM_UT_CORE 		0x01
	   DRM_UT_DRIVER	0x02
	   DRM_UT_KMS		0x04
	   DRM_UT_PRIME		0x08
	   */
	/* drm_debug = DRM_UT_CORE | DRM_UT_DRIVER | DRM_UT_KMS | DRM_UT_PRIME; */

	DRM_DEBUG_DRIVER("%s, start\n", __func__);

	/*
	 * Init lcd platform driver/device.
	 * */
#ifdef CONFIG_DRM_OWL_LCD
	ret = platform_driver_register(&lcd_platform_driver);
	if (ret < 0)
		goto out_lcd;

	owl_drm_lcd_pdev = platform_device_register_simple("owl-drm-lcd", -1,
			NULL, 0);
	if (IS_ERR(owl_drm_lcd_pdev)) {
		ret = PTR_ERR(owl_drm_lcd_pdev);
		goto out_lcd_dev;
	}
#endif

	/*
	 * Init hdmi platform driver/device
	 * */
#ifdef CONFIG_DRM_OWL_HDMI
	ret = platform_driver_register(&hdmi_platform_driver);
	if (ret < 0)
		goto out_hdmi;

	owl_drm_hdmi_pdev = platform_device_register_simple("owl-drm-hdmi", -1,
			NULL, 0);
	if (IS_ERR(owl_drm_hdmi_pdev)) {
		ret = PTR_ERR(owl_drm_hdmi_pdev);
		goto out_hdmi_dev;
	}
#endif

	/*
	 * Init drm platform driver/device.
	 * */
	ret = platform_driver_register(&owl_drm_platform_driver);
	if (ret < 0)
		goto out_drm;

	owl_drm_pdev = platform_device_register_simple("owl-drm", -1,
			NULL, 0);
	if (IS_ERR(owl_drm_pdev)) {
		ret = PTR_ERR(owl_drm_pdev);
		goto out;
	}

	return 0;

out:
	platform_driver_unregister(&owl_drm_platform_driver);

out_drm:
#ifdef CONFIG_DRM_OWL_HDMI
	platform_device_unregister(owl_drm_hdmi_pdev);
out_hdmi_dev:
	platform_driver_unregister(&hdmi_platform_driver);
out_hdmi:
#endif

#ifdef CONFIG_DRM_OWL_LCD
	platform_device_unregister(owl_drm_lcd_pdev);
out_lcd_dev:
	platform_driver_unregister(&lcd_platform_driver);
out_lcd:
#endif

	DRM_DEBUG_DRIVER("%s, end\n", __func__);

	return ret;
}

static void __exit owl_drm_exit(void)
{
	DRM_DEBUG_DRIVER("%s\n", __func__);

	platform_device_unregister(owl_drm_pdev);
	platform_driver_unregister(&owl_drm_platform_driver);

#ifdef CONFIG_DRM_OWL_LCD
	platform_device_unregister(owl_drm_lcd_pdev);
	platform_driver_unregister(&lcd_platform_driver);
#endif

#ifdef CONFIG_DRM_OWL_HDMI
	platform_device_unregister(owl_drm_hdmi_pdev);
	platform_driver_unregister(&hdmi_platform_driver);
#endif
}

module_init(owl_drm_init);
module_exit(owl_drm_exit);

MODULE_DESCRIPTION("Owl SoC DRM Driver");
MODULE_LICENSE("GPL");
