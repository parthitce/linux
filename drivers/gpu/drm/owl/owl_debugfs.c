/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <drm/drm_mm.h>

#include "owl_drv.h"

#ifdef CONFIG_DEBUG_FS

struct drm_framebuffer;

void owl_gem_describe_objects(struct list_head *list, struct seq_file *m);
void owl_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);

static int gem_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct owl_drm_private *priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "All Objects:\n");
	owl_gem_describe_objects(&priv->obj_list, m);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	return drm_mm_dump_table(m, dev->mm_private);
}

static int fb_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct owl_drm_private *priv = dev->dev_private;
	struct drm_framebuffer *fb;

	seq_printf(m, "fbcon ");
	owl_framebuffer_describe(priv->fbdev->fb, m);

	mutex_lock(&dev->mode_config.fb_lock);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		if (fb == priv->fbdev->fb)
			continue;

		seq_printf(m, "user ");
		owl_framebuffer_describe(fb, m);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

/* list of debufs files that are applicable to all devices */
static struct drm_info_list owl_debugfs_list[] = {
	{"gem", gem_show, 0 },
	{"mm",  mm_show,  0 },
	{"fb",  fb_show,  0 },
};

int owl_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(owl_debugfs_list,
			ARRAY_SIZE(owl_debugfs_list),
			minor->debugfs_root, minor);

	if (ret)
		DEV_ERR(dev->dev, "could not install owl_debugfs_list");

	return ret;
}

void owl_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(owl_debugfs_list,
			ARRAY_SIZE(owl_debugfs_list), minor);
}

#endif
