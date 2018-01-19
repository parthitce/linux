/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "owl_drv.h"

#ifdef HAVE_DRM_IRQ

struct owl_irq_wait {
	struct list_head node;
	wait_queue_head_t wq;
	uint32_t irqmask;
	int count;
};

/* call with wait_lock and dispc runtime held */
static void owl_irq_update(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_irq_wait *wait;
	uint32_t irqmask = priv->irq_mask;

	assert_spin_locked(&priv->wait_lock);

	list_for_each_entry(wait, &priv->wait_list, node)
		irqmask |= wait->irqmask;

	DBG("irqmask=%08x", irqmask);
}

static void owl_irq_wait_handler(struct owl_irq_wait *wait)
{
	wait->count--;
	wake_up(&wait->wq);
}

struct owl_irq_wait *owl_irq_wait_init(struct drm_device *dev,
		uint32_t irqmask, int count)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_irq_wait *wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	unsigned long flags;

	init_waitqueue_head(&wait->wq);
	wait->irqmask = irqmask;
	wait->count = count;

	spin_lock_irqsave(&priv->wait_lock, flags);
	list_add(&wait->node, &priv->wait_list);
	owl_irq_update(dev);
	spin_unlock_irqrestore(&priv->wait_lock, flags);

	return wait;
}

int owl_irq_wait(struct drm_device *dev, struct owl_irq_wait *wait,
		unsigned long timeout)
{
	struct owl_drm_private *priv = dev->dev_private;
	unsigned long flags;
	int ret;

	ret = wait_event_timeout(wait->wq, (wait->count <= 0), timeout);

	spin_lock_irqsave(&priv->wait_lock, flags);
	list_del(&wait->node);
	owl_irq_update(dev);
	spin_unlock_irqrestore(&priv->wait_lock, flags);

	kfree(wait);

	return ret == 0 ? -1 : 0;
}

/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int owl_irq_enable_vblank(struct drm_device *dev, int crtc_id)
{
	struct owl_drm_private *priv = dev->dev_private;
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc_id);

	spin_lock_irqsave(&priv->wait_lock, flags);
	priv->irq_mask |= pipe2irq(crtc_id);
	owl_irq_update(dev);
	spin_unlock_irqrestore(&priv->wait_lock, flags);

	owl_crtc_enable_vblank(dev, crtc_id);

	return -EINVAL;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void owl_irq_disable_vblank(struct drm_device *dev, int crtc_id)
{
	struct owl_drm_private *priv = dev->dev_private;
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc_id);

	spin_lock_irqsave(&priv->wait_lock, flags);
	priv->irq_mask &= ~pipe2irq(crtc_id);
	owl_irq_update(dev);
	spin_unlock_irqrestore(&priv->wait_lock, flags);

	owl_crtc_disable_vblank(dev, crtc_id);
}

/*
 * We need a special version, instead of just using drm_irq_install(),
 * because we need to register the irq via owldss.  Once owldss and
 * owldrm are merged together we can assign the disc hwmod data to
 * ourselves and drop these and just use drm_irq_{install,uninstall}()
 */

static int owl_irq_handler(struct owl_drm_panel *panel)
{
	struct owl_drm_private *priv = panel->drm->dev_private;
	struct owl_irq_wait *wait, *n;
	unsigned long flags;
	u32 irqstatus = 0xffffffff; /* ignore irqmask at present */

	owl_crtc_handle_vblank(panel);

	spin_lock_irqsave(&priv->wait_lock, flags);
	list_for_each_entry_safe(wait, n, &priv->wait_list, node) {
		if (wait->irqmask & irqstatus)
			owl_irq_wait_handler(wait);
	}
	spin_unlock_irqrestore(&priv->wait_lock, flags);

	return 0;
}

static int owl_irq_request(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_drm_dssdev *dssdev = priv->dssdev;
	struct owl_drm_panel_callback_funcs callback = {
		.vsync = owl_irq_handler,
		.hotplug = owl_crtc_handle_hotplug,
	};

	INIT_LIST_HEAD(&priv->wait_list);
	spin_lock_init(&priv->wait_lock);
	priv->irq_mask = 0;

	return owl_dssdev_register_callback(dssdev, &callback);
}

static void owl_irq_free(struct drm_device *dev)
{
	struct owl_drm_dssdev *dssdev = owl_dssdev_get(dev);
	struct owl_drm_panel_callback_funcs callback = { 0 };

	owl_dssdev_register_callback(dssdev, &callback);
}

int owl_irq_install(struct drm_device *dev)
{
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return 0;

	mutex_lock(&dev->struct_mutex);

	/* Driver must have been initialized */
	if (!dev->dev_private) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	if (dev->irq_enabled) {
		mutex_unlock(&dev->struct_mutex);
		return -EBUSY;
	}
	dev->irq_enabled = 1;
	mutex_unlock(&dev->struct_mutex);

	/* Before installing handler */
	if (dev->driver->irq_preinstall)
		dev->driver->irq_preinstall(dev);

	ret = owl_irq_request(dev);
	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* After installing handler */
	if (dev->driver->irq_postinstall)
		ret = dev->driver->irq_postinstall(dev);

	if (ret < 0) {
		mutex_lock(&dev->struct_mutex);
		dev->irq_enabled = 0;
		mutex_unlock(&dev->struct_mutex);

		owl_irq_free(dev);
	}

	return ret;
}

int owl_irq_uninstall(struct drm_device *dev)
{
	unsigned long irqflags;
	int irq_enabled, i;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return 0;

	mutex_lock(&dev->struct_mutex);
	irq_enabled = dev->irq_enabled;
	dev->irq_enabled = 0;
	mutex_unlock(&dev->struct_mutex);

	/*
	 * Wake up any waiters so they don't hang.
	 */
	if (dev->num_crtcs) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		for (i = 0; i < dev->num_crtcs; i++) {
			DRM_WAKEUP(&dev->vbl_queue[i]);
			dev->vblank_enabled[i] = 0;
			dev->last_vblank[i] =
				dev->driver->get_vblank_counter(dev, i);
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}

	if (!irq_enabled)
		return -EINVAL;

	if (dev->driver->irq_uninstall)
		dev->driver->irq_uninstall(dev);

	owl_irq_free(dev);

	return 0;
}

#endif /* HAVE_DRM_IRQ */
