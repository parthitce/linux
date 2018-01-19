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

/* Should use drm_fb_helper_alloc_fbi after kernel upgrade */
static struct fb_info *_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper);
/* Should use drm_fb_helper_unregister_fbi after kernel upgrade */
static void _drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper);
/* Should use drm_fb_helper_fini directly after kernel upgrade */
static void _drm_fb_helper_fini(struct drm_fb_helper *fb_helper);

/*
 * fbdev funcs, to implement legacy fbdev interface on top of drm driver
 */

#define to_owl_fbdev(x)	container_of(x, struct owl_fbdev, base)

struct owl_fbdev {
	struct drm_fb_helper base;
	struct drm_framebuffer *fb;
};

static int owl_fbdev_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = (struct drm_fb_helper *)info->par;
	struct owl_fbdev *fbdev = to_owl_fbdev(helper);
	struct drm_gem_object *bo = owl_framebuffer_bo(fbdev->fb, 0);
	int ret = 0;

	ret = _drm_gem_mmap_obj(bo, bo->size, vma);
	if (ret) {
		pr_err("%s:drm_gem_mmap_obj fail\n", __func__);
		return ret;
	}

	return owl_gem_mmap_obj(bo, vma);
}

static struct fb_ops owl_fb_ops = {
	.owner		    = THIS_MODULE,

	/* Note: to properly handle manual update displays, we wrap the
	 * basic fbdev ops which write to the framebuffer
	 */
	.fb_read = fb_sys_read,
	.fb_write = fb_sys_write,
	.fb_mmap        = owl_fbdev_mmap,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,

	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	    = drm_fb_helper_set_par,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_blank	    = drm_fb_helper_blank,
	.fb_setcmap	    = drm_fb_helper_setcmap,
};

static int owl_fbdev_create(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct owl_fbdev *fbdev = to_owl_fbdev(helper);
	struct drm_device *dev = helper->dev;
	struct drm_framebuffer *fb = NULL;
	struct drm_gem_object *bo;
	struct fb_info *fbi = NULL;
	dma_addr_t paddr;
	uint32_t format;
	int ret, pitch;

	DBG("create fbdev: %dx%d@%d (%dx%d)", sizes->surface_width,
			sizes->surface_height, sizes->surface_bpp,
			sizes->fb_width, sizes->fb_height);

	format = drm_mode_legacy_fb_format(sizes->surface_bpp, sizes->surface_depth);
	pitch = align_pitch(sizes->surface_width, sizes->surface_bpp);
	fb = owl_alloc_fb(dev, sizes->surface_width,
			sizes->surface_height, pitch, format);
	if (IS_ERR(fb)) {
		DEV_ERR(dev->dev, "failed to allocate fb");
		ret = PTR_ERR(fb);
		goto fail;
	}

	bo = owl_framebuffer_bo(fb, 0);

	mutex_lock(&dev->struct_mutex);
	fbi = _drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(fbi)) {
		DEV_ERR(dev->dev, "failed to allocate fb info");
		ret = PTR_ERR(fbi);
		goto fail_remove_fb;
	}

	fbdev->fb = fb;
	helper->fb = fb;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &owl_fb_ops;

	strcpy(fbi->fix.id, "owl");

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	ret = owl_gem_pin(bo, &paddr);
	if (ret)
		goto fail_free_fbi;

	dev->mode_config.fb_base = paddr;

	fbi->screen_base = owl_gem_get_vaddr(bo);
	if (IS_ERR(fbi->screen_base)) {
		ret = PTR_ERR(fbi->screen_base);
		goto fail_unpin_bo;
	}
	fbi->screen_size = bo->size;
	fbi->fix.smem_start = paddr;
	fbi->fix.smem_len = bo->size;

	DBG("par=%p, %dx%d", fbi->par, fbi->var.xres, fbi->var.yres);
	DBG("allocated %dx%d fb", fbdev->fb->width, fbdev->fb->height);

	mutex_unlock(&dev->struct_mutex);
	return 0;

fail_unpin_bo:
	owl_gem_unpin(bo);
fail_free_fbi:
	fb_dealloc_cmap(&fbi->cmap);
	_drm_fb_helper_unregister_fbi(helper);
fail_remove_fb:
	drm_framebuffer_unregister_private(fb);
	drm_framebuffer_remove(fb);
	mutex_unlock(&dev->struct_mutex);
fail:
	return ret;
}

static struct drm_fb_helper_funcs owl_fb_helper_funcs = {
	.fb_probe =	owl_fbdev_create,
};

struct drm_fb_helper *owl_fbdev_init(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_fbdev *fbdev = NULL;
	struct drm_fb_helper *helper;
	int ret;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return NULL;

	helper = &fbdev->base;
	helper->funcs = &owl_fb_helper_funcs;

	ret = drm_fb_helper_init(dev, helper, priv->num_crtcs, priv->num_connectors);
	if (ret < 0) {
		DEV_ERR(dev->dev, "failed to initialize drm fb helper.");
		goto fail;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DEV_ERR(dev->dev, "failed to register drm_fb_helper_connector.");
		goto fail_fini;
	}

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(helper, 32);
	if (ret < 0) {
		DEV_ERR(dev->dev, "failed to set up hw configuration.");
		goto fail_fini;
	}

	priv->fbdev = helper;
	return helper;
fail_fini:
	_drm_fb_helper_fini(helper);
fail:
	kfree(fbdev);
	return NULL;
}

void owl_fbdev_free(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct drm_fb_helper *helper = priv->fbdev;
	struct owl_fbdev *fbdev;

	DBG();

	if (!helper)
		return;

	_drm_fb_helper_unregister_fbi(helper);
	_drm_fb_helper_fini(helper);

	fbdev = to_owl_fbdev(priv->fbdev);

	/* this will free the backing object */
	if (fbdev->fb) {
		struct drm_gem_object *bo =
			owl_framebuffer_bo(fbdev->fb, 0);
		owl_gem_put_vaddr(bo);
		drm_framebuffer_unregister_private(fbdev->fb);
		drm_framebuffer_remove(fbdev->fb);
	}

	kfree(fbdev);
	priv->fbdev = NULL;
}

/* Should use drm_fb_helper_alloc_fbi after kernel upgrade */
static struct fb_info *_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
	struct device *dev = fb_helper->dev->dev;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(0, dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_release;

	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_free_cmap;
	}

	fb_helper->fbdev = info;

	return info;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_release:
	framebuffer_release(info);
	return ERR_PTR(ret);
}

/* Should use drm_fb_helper_unregister_fbi after kernel upgrade */
static void _drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}

/* Should use drm_fb_helper_fini directly after kernel upgrade */
static void _drm_fb_helper_fini(struct drm_fb_helper *fb_helper)
{
	struct fb_info *info;

	if (!fb_helper)
		return;

	info = fb_helper->fbdev;
	if (info) {
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	fb_helper->fbdev = NULL;

	drm_fb_helper_fini(fb_helper);
}
