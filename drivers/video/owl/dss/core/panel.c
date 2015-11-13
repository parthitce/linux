/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/8/20: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "owl_panel: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#include <video/owl_dss.h>

static LIST_HEAD(g_panel_list);
static DEFINE_MUTEX(g_panel_list_lock);

static int g_panel_nums;

static struct class owl_panel_class;

static void panel_update_path_info(struct owl_panel *panel)
{
	struct owl_de_path *path = panel->path;
	struct owl_de_path_info p_info;

	owl_de_path_get_info(path, &p_info);

	p_info.type = PANEL_TYPE(panel);

	/* panel must have a valid resolution, which is used for path size */
	owl_panel_get_resolution(panel, &p_info.width, &p_info.height);
	if (p_info.width <= 0 || p_info.height <= 0) {
		pr_debug("path size(%dx%d) invalid\n",
			 p_info.width, p_info.height);
	}

	switch (PANEL_BPP(panel)) {
	case 16:
		p_info.dither_mode = DITHER_24_TO_16;
		break;

	case 18:
		p_info.dither_mode = DITHER_24_TO_18;
		break;

	default:
		p_info.dither_mode = DITHER_DISABLE;
		break;
	}

	owl_de_path_set_info(path, &p_info);
}

int owl_panel_init(void)
{
	int ret = 0;

	pr_info("start\n");

	ret = class_register(&owl_panel_class);
	if (ret < 0) {
		pr_err("register owl panel class failed(%d)\n", ret);
		return ret;
	}

	g_panel_nums = 0;

	return 0;
}
EXPORT_SYMBOL(owl_panel_init);

struct owl_panel *owl_panel_alloc(const char *name, enum owl_display_type type)
{
	struct owl_panel *panel;

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (panel == NULL)
		return NULL;

	panel->desc.type = type;
	panel->desc.name = kzalloc(strlen(name), GFP_KERNEL);
	if (panel->desc.name)
		strcpy(panel->desc.name, name);

	return panel;
}
EXPORT_SYMBOL(owl_panel_alloc);

void owl_panel_free(struct owl_panel *panel)
{
	kzfree(panel->desc.name);
	kzfree(panel);
}
EXPORT_SYMBOL(owl_panel_free);

int owl_panel_register(struct device *parent, struct owl_panel *panel)
{
	int ret = 0;

	struct device *dev;

	pr_info("start\n");

	if (panel == NULL) {
		pr_err("panel is NULL\n");
		return -EINVAL;
	}

	if (owl_panel_get_by_type(panel->desc.type) != NULL) {
		/* TODO */
		pr_err("type(%d) was already registered!\n", panel->desc.type);
		return -EBUSY;
	}

	/*
	 * some initialization
	 */
	panel->state = OWL_DSS_STATE_OFF;

	/* TODO */
	panel->desc.is_dummy = (PANEL_TYPE(panel) == OWL_DISPLAY_TYPE_DUMMY);

	panel->desc.scale_factor_x = OWL_PANEL_SCALE_FACTOR_MAX;
	panel->desc.scale_factor_y = OWL_PANEL_SCALE_FACTOR_MAX;


	/*
	 * register to owl panel class
	 */

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	device_initialize(dev);

	dev->class = &owl_panel_class;
	dev->parent = parent;
	dev_set_drvdata(dev, panel);
	panel->dev = dev;

	ret = kobject_set_name(&dev->kobj, "%s", panel->desc.name);
	if (ret)
		goto kobject_set_name_failed;

	ret = device_add(dev);
	if (ret)
		goto device_add_failed;

	/*
	 * match with DE path
	 */
	ret = owl_de_path_add_panel(panel);
	if (ret < 0) {
		pr_err("add panel to DE path failed!\n");
		goto add_to_de_path_failed;
	}
	panel_update_path_info(panel);

	/*
	 * match with display controller
	 */
	ret = owl_ctrl_add_panel(panel);
	if (ret < 0) {
		pr_err("add panel to ctrl failed!\n");
		goto add_to_ctrl_failed;
	}

	mutex_lock(&g_panel_list_lock);
	list_add(&panel->list, &g_panel_list);
	g_panel_nums++;
	mutex_unlock(&g_panel_list_lock);

	return 0;

add_to_ctrl_failed:
add_to_de_path_failed:
	owl_ctrl_remove_panel(panel);
device_add_failed:
kobject_set_name_failed:
	put_device(dev);

	return ret;
}
EXPORT_SYMBOL(owl_panel_register);

void owl_panel_unregister(struct owl_panel *panel)
{
	owl_de_path_remove_panel(panel);
	owl_ctrl_remove_panel(panel);
	put_device(panel->dev);

	g_panel_nums--;
}
EXPORT_SYMBOL(owl_panel_unregister);

int owl_panel_get_panel_num(void)
{
	return g_panel_nums;
}
EXPORT_SYMBOL(owl_panel_get_panel_num);

struct owl_panel *owl_panel_get_by_name(const char *name)
{
	struct owl_panel *p;

	mutex_lock(&g_panel_list_lock);

	list_for_each_entry(p, &g_panel_list, list) {
		if (strcmp(p->desc.name, name) == 0) {
			mutex_unlock(&g_panel_list_lock);
			return p;
		}
	}
	mutex_unlock(&g_panel_list_lock);

	return NULL;
}
EXPORT_SYMBOL(owl_panel_get_by_name);

struct owl_panel *owl_panel_get_by_type(enum owl_display_type type)
{
	struct owl_panel *p;

	mutex_lock(&g_panel_list_lock);

	list_for_each_entry(p, &g_panel_list, list) {
		if (p->desc.type == type) {
			mutex_unlock(&g_panel_list_lock);
			return p;
		}
	}

	mutex_unlock(&g_panel_list_lock);

	return NULL;
}
EXPORT_SYMBOL(owl_panel_get_by_type);

struct owl_panel *owl_panel_get_next_panel(struct owl_panel *from)
{
	if (from == NULL) {
		return list_first_entry_or_null(&g_panel_list,
						struct owl_panel, list);
	} else {
		if (list_is_last(&from->list, &g_panel_list))
			return NULL;
		else
			return list_next_entry(from, list);
	}
}
EXPORT_SYMBOL(owl_panel_get_next_panel);

void owl_panel_get_default_mode(struct owl_panel *panel,
				struct owl_videomode *mode)
{
	memcpy(mode, &panel->default_mode, sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_get_default_mode);

void owl_panel_get_mode(struct owl_panel *panel, struct owl_videomode *mode)
{
	memcpy(mode, &panel->mode, sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_get_mode);

int owl_panel_set_mode(struct owl_panel *panel, struct owl_videomode *mode)
{
	memcpy(&panel->mode, mode, sizeof(struct owl_videomode));
	panel_update_path_info(panel);

	return 0;
}
EXPORT_SYMBOL(owl_panel_set_mode);

void owl_panel_set_mode_list(struct owl_panel *panel,
			     struct owl_videomode *modes, int n_modes)
{
	int i;

	if (panel == NULL || modes == NULL || n_modes <= 0)
		return;

	if (n_modes > OWL_PANEL_MAX_VIDEOMODES)
		n_modes = OWL_PANEL_MAX_VIDEOMODES;

	panel->n_modes = n_modes;
	for (i = 0; i < n_modes; i++)
		memcpy(&panel->mode_list[i], &modes[i],
		       sizeof(struct owl_videomode));
}
EXPORT_SYMBOL(owl_panel_set_mode_list);

int owl_panel_enable(struct owl_panel *panel)
{
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("state %d, power_on_delay %d, enable_delay %d\n",
		panel->state, desc->power_on_delay, desc->enable_delay);

	if (panel->state == OWL_DSS_STATE_ON)
		return 0;

	if (desc->ops && desc->ops->power_on) {
		desc->ops->power_on(panel);
		if (desc->power_on_delay > 0)
			mdelay(desc->power_on_delay);
	}

	if (desc->ops && desc->ops->enable)
		desc->ops->enable(panel);

	owl_ctrl_enable(panel->ctrl);

	if (desc->enable_delay > 0)
		mdelay(desc->enable_delay);

	panel->state = OWL_DSS_STATE_ON;

	return 0;
}
EXPORT_SYMBOL(owl_panel_enable);

void owl_panel_disable(struct owl_panel *panel)
{
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("state %d, power_off_delay %d, disable_delay %d\n",
		panel->state, desc->power_off_delay, desc->disable_delay);

	if (panel->state == OWL_DSS_STATE_OFF)
		return;

	owl_ctrl_disable(panel->ctrl);

	if (desc->disable_delay > 0)
		mdelay(desc->disable_delay);

	if (desc->ops && desc->ops->disable)
		desc->ops->disable(panel);

	if (desc->ops && desc->ops->power_off) {
		desc->ops->power_off(panel);
		if (desc->power_off_delay > 0)
			mdelay(desc->power_off_delay);
	}

	panel->state = OWL_DSS_STATE_OFF;
}
EXPORT_SYMBOL(owl_panel_disable);

void owl_panel_get_resolution(struct owl_panel *panel, int *xres, int *yres)
{
	if (panel == NULL)
		return;

	*xres = panel->mode.xres;
	*yres = panel->mode.yres;
}
EXPORT_SYMBOL(owl_panel_get_resolution);

int owl_panel_get_refresh_rate(struct owl_panel *panel)
{
	if (panel == NULL)
		return 0;

	return panel->mode.refresh;
}
EXPORT_SYMBOL(owl_panel_get_refresh_rate);

void owl_panel_get_draw_size(struct owl_panel *panel, int *width, int *height)
{
	if (panel == NULL)
		return;

	if (panel->draw_width == 0 || panel->draw_height == 0) {
		if (panel->mode.xres != 0 && panel->mode.yres != 0) {
			pr_debug("use 'mode' as draw size\n");
			*width = panel->mode.xres;
			*height = panel->mode.yres;
		} else {
			pr_debug("use default size as draw size\n");
			*width = 1920;
			*height = 1080;
		}
	} else {
		*width = panel->draw_width;
		*height = panel->draw_height;
	}

	/* limit draw size */
	if (*width > 2560)
		*width /= 2;
	if (*height > 2048)
		*height /= 2;
}
EXPORT_SYMBOL(owl_panel_get_draw_size);

int owl_panel_get_bpp(struct owl_panel *panel)
{
	return panel == NULL ? 0 : panel->desc.bpp;
}
EXPORT_SYMBOL(owl_panel_get_bpp);

char *owl_panel_get_name(struct owl_panel *panel)
{
	return panel == NULL ? NULL : panel->desc.name;
}
EXPORT_SYMBOL(owl_panel_get_name);

enum owl_display_type owl_panel_get_type(struct owl_panel *panel)
{
	return panel == NULL ? OWL_DISPLAY_TYPE_NONE : panel->desc.type;
}
EXPORT_SYMBOL(owl_panel_get_type);

int owl_panel_parse_panel_info(struct device_node *of_node,
			       struct owl_panel *panel)
{
	uint32_t val;

	struct device_node *entry = of_node;

	struct owl_videomode *mode = &panel->mode;
	struct owl_dss_panel_desc *desc = &panel->desc;

	pr_info("start\n");

	#define OF_READ_U32(name, p) \
		of_property_read_u32(entry, (name), (p))

	/*
	 * parse standard panel descriptors, can be zero
	 */
	OF_READ_U32("width_mm", &desc->width_mm);
	OF_READ_U32("height_mm", &desc->height_mm);
	OF_READ_U32("bpp", &desc->bpp);

	OF_READ_U32("power_on_delay", &desc->power_on_delay);
	OF_READ_U32("power_off_delay", &desc->power_off_delay);
	OF_READ_U32("enable_delay", &desc->enable_delay);
	OF_READ_U32("disable_delay", &desc->disable_delay);

	if (OF_READ_U32("is_primary", &val))
		desc->is_primary = false;
	else
		desc->is_primary = (val == 1 ? true : false);

	if (OF_READ_U32("hotplug_always_on", &val))
		desc->hotplug_always_on = false;
	else
		desc->hotplug_always_on = (val == 1 ? true : false);

	pr_debug("%dmm x %dmm, bpp %d\n", desc->width_mm,
		 desc->height_mm, desc->bpp);

	pr_debug("power_on_delay %d, power_off_delay %d\n",
		 desc->power_on_delay, desc->power_off_delay);
	pr_debug("enable_delay %d, disable_delay %d\n",
		 desc->enable_delay, desc->disable_delay);

	pr_debug("hotplug_always_on %d\n", desc->hotplug_always_on);

	OF_READ_U32("draw_width", &panel->draw_width);
	OF_READ_U32("draw_height", &panel->draw_height);
	pr_debug("draw size %dx%d\n", panel->draw_width, panel->draw_height);

	/*
	 * parse default video mode, some can be zero
	 */
	entry = of_parse_phandle(of_node, "videomode-0", 0);
	if (!entry) {
		pr_info("no entry for 'videomode-0'\n");
	} else {
		OF_READ_U32("xres", &mode->xres);
		OF_READ_U32("yres", &mode->yres);
		OF_READ_U32("refresh_rate", &mode->refresh);
		OF_READ_U32("pixel_clock", &mode->pixclock);
		OF_READ_U32("left_margin", &mode->hfp);
		OF_READ_U32("right_margin", &mode->hbp);
		OF_READ_U32("upper_margin", &mode->vfp);
		OF_READ_U32("lower_margin", &mode->vbp);
		OF_READ_U32("hsync_len", &mode->hsw);
		OF_READ_U32("vsync_len", &mode->vsw);
	}

	pr_debug("%d x %d, refresh %d\n", mode->xres, mode->yres,
		 mode->refresh);

	pr_debug("pixclock %d\n", mode->pixclock);
	pr_debug("hfp %d, hbp %d, vfp %d, vbp %d, hsw %d, vsw %d\n",
		 mode->hfp, mode->hbp, mode->vfp, mode->vbp,
		 mode->hsw, mode->vsw);

	/* set default_mode */
	if (mode->xres != 0 && mode->yres != 0 && mode->refresh != 0) {
		panel->default_mode.xres = mode->xres;
		panel->default_mode.yres = mode->yres;
		panel->default_mode.refresh = mode->refresh;
	} else {
		panel->default_mode.xres = 1280;
		panel->default_mode.yres = 720;
		panel->default_mode.refresh = 50;
	}

	#undef OF_READ_U32

	return 0;
}
EXPORT_SYMBOL(owl_panel_parse_panel_info);

void owl_panel_hotplug_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			      void *data)
{
	panel->hotplug_cb = cb;
	panel->hotplug_data = data;
}

void owl_panel_vsync_cb_set(struct owl_panel *panel, owl_panel_cb_t cb,
			    void *data)
{
	panel->vsync_cb = cb;
	panel->vsync_data = data;
}

void owl_panel_hotplug_notify(struct owl_panel *panel, bool is_connected)
{
	if (panel->hotplug_cb)
		panel->hotplug_cb(panel, panel->vsync_data, is_connected);
}
EXPORT_SYMBOL(owl_panel_hotplug_notify);

void owl_panel_vsync_notify(struct owl_panel *panel)
{
	if (panel->vsync_cb)
		panel->vsync_cb(panel, panel->vsync_data, 0);
}
EXPORT_SYMBOL(owl_panel_vsync_notify);

void owl_panel_get_scale_factor(struct owl_panel *panel,
				uint16_t *x, uint16_t *y)
{
	*x = panel->desc.scale_factor_x;
	*y = panel->desc.scale_factor_y;
}
EXPORT_SYMBOL(owl_panel_get_scale_factor);

void owl_panel_set_scale_factor(struct owl_panel *panel,
				uint16_t x, uint16_t y)
{
	if (x > OWL_PANEL_SCALE_FACTOR_MAX)
		x = OWL_PANEL_SCALE_FACTOR_MAX;
	if (x < OWL_PANEL_SCALE_FACTOR_MIN)
		x = OWL_PANEL_SCALE_FACTOR_MIN;
	panel->desc.scale_factor_x = x;

	if (y > OWL_PANEL_SCALE_FACTOR_MAX)
		y = OWL_PANEL_SCALE_FACTOR_MAX;
	if (y < OWL_PANEL_SCALE_FACTOR_MIN)
		y = OWL_PANEL_SCALE_FACTOR_MIN;
	panel->desc.scale_factor_y = y;
}
EXPORT_SYMBOL(owl_panel_set_scale_factor);

/*
 * check panel's connect status, the rule is:
 *	if panel's .hpd_is_connected is not NULL, using it;
 *	or, if its controller's .hpd_is_panel_connected is not NULL, using it;
 *	or, return TRUE.
 */
bool owl_panel_hpd_is_connected(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (PANEL_IS_PRIMARY(panel))
		return true;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_is_connected)
		return panel->desc.ops->hpd_is_connected(panel);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_is_panel_connected)
		return ctrl->ops->hpd_is_panel_connected(ctrl);
	else
		return true;
}
EXPORT_SYMBOL(owl_panel_hpd_is_connected);

void owl_panel_hpd_enable(struct owl_panel *panel, bool enable)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (!enable && PANEL_HOTPLUG_ALWAYS_ON(panel))
		return;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_enable)
		panel->desc.ops->hpd_enable(panel, enable);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_enable)
		ctrl->ops->hpd_enable(ctrl, enable);
	else
		return;
}
EXPORT_SYMBOL(owl_panel_hpd_enable);

bool owl_panel_hpd_is_enabled(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (PANEL_HOTPLUG_ALWAYS_ON(panel))
		return true;

	if (panel && panel->desc.ops && panel->desc.ops->hpd_is_enabled)
		return panel->desc.ops->hpd_is_enabled(panel);
	if (ctrl && ctrl->ops && ctrl->ops->hpd_is_enabled)
		return ctrl->ops->hpd_is_enabled(ctrl);
	else
		return true;
}
EXPORT_SYMBOL(owl_panel_hpd_is_enabled);

/*
 * Calculate preline numbers according to the recommended preline time
 * and panel's timings. Calculation formula is:
 *	(recommended preline time(us) * 1000000)
	/ (x_res + hfp + hbp + hsw) / pixel_clock)
 *
 * maybe should be calculated static, but has a little sticky for HDMI,
 * because HDMI's timing is static defined, I have no entry point to
 * calculate it. FIXME pls!
 */
int owl_panel_calc_prelines(struct owl_panel *panel)
{
	int preline_num;

	struct owl_videomode *mode;

	if (panel == NULL)
		return 0;

	mode = &panel->mode;

	/* caculate preline number if we can */
	preline_num = (mode->xres + mode->hfp + mode->hbp + mode->hsw)
			* mode->pixclock;
	if (preline_num != 0)
		preline_num = (DSS_RECOMMENDED_PRELINE_TIME * 1000000
			+ preline_num / 2) / preline_num; /* round */

	return preline_num;
}
EXPORT_SYMBOL(owl_panel_calc_prelines);

int owl_panel_3d_mode_set(struct owl_panel *panel, enum owl_3d_mode mode_3d)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->mode_3d_set)
		return panel->desc.ops->mode_3d_set(panel, mode_3d);
	if (ctrl && ctrl->ops && ctrl->ops->mode_3d_set)
		return ctrl->ops->mode_3d_set(ctrl, mode_3d);

	return 0;
}
EXPORT_SYMBOL(owl_panel_3d_mode_set);

enum owl_3d_mode owl_panel_3d_mode_get(struct owl_panel *panel)
{
	struct owl_display_ctrl *ctrl = panel->ctrl;

	if (panel && panel->desc.ops && panel->desc.ops->mode_3d_get)
		return panel->desc.ops->mode_3d_get(panel);
	if (ctrl && ctrl->ops && ctrl->ops->mode_3d_get)
		return ctrl->ops->mode_3d_get(ctrl);

	return MODE_3D_DISABLE;
}
EXPORT_SYMBOL(owl_panel_3d_mode_get);


/*=========================================================
 *		owl panel class
 *=======================================================*/

static void panel_dev_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

static ssize_t panel_name_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", panel->desc.name);
}

static ssize_t panel_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return sprintf(buf, "%ux%umm\n", panel->desc.width_mm,
		       panel->desc.height_mm);
}

static ssize_t panel_default_mode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->default_mode;

	return snprintf(buf, PAGE_SIZE, "%ux%u@%u,%u,%u/%u/%u,%u/%u/%u\n",
			mode->xres, mode->yres, mode->refresh, mode->pixclock,
			mode->hfp, mode->hbp, mode->hsw, mode->vfp,
			mode->vbp, mode->vsw);
}

static ssize_t panel_default_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->default_mode;

	sscanf(buf, "%ux%u@%u,%u,%u/%u/%u,%u/%u/%u\n",
	       &mode->xres, &mode->yres, &mode->refresh, &mode->pixclock,
	       &mode->hfp, &mode->hbp, &mode->hsw,
	       &mode->vfp, &mode->vbp, &mode->vsw);

	return count;
}

static ssize_t panel_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->mode;

	return snprintf(buf, PAGE_SIZE, "%ux%u@%u,%u,%u/%u/%u,%u/%u/%u\n",
			mode->xres, mode->yres, mode->refresh, mode->pixclock,
			mode->hfp, mode->hbp, mode->hsw, mode->vfp,
			mode->vbp, mode->vsw);
}

static ssize_t panel_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode = &panel->mode;

	sscanf(buf, "%ux%u@%u,%u,%u/%u/%u,%u/%u/%u\n",
	       &mode->xres, &mode->yres, &mode->refresh, &mode->pixclock,
	       &mode->hfp, &mode->hbp, &mode->hsw,
	       &mode->vfp, &mode->vbp, &mode->vsw);

	return count;
}

static ssize_t panel_mode_list_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int i, ret = 0;

	struct owl_panel *panel = dev_get_drvdata(dev);
	struct owl_videomode *mode;

	for (i = 0; i < panel->n_modes; i++) {
		mode = &panel->mode_list[i];

		ret += snprintf(&buf[ret], PAGE_SIZE,
				"%ux%u@%u,%u,%u/%u/%u,%u/%u/%u\n",
				mode->xres, mode->yres, mode->refresh,
				mode->pixclock, mode->hfp, mode->hbp,
				mode->hsw, mode->vfp, mode->vbp, mode->vsw);
	}

	return ret;
}

static ssize_t panel_prelines_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", owl_panel_calc_prelines(panel));
}

static ssize_t panel_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			panel->state == OWL_DSS_STATE_ON);
}

static ssize_t panel_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (!!val)
		owl_panel_enable(panel);
	else
		owl_panel_disable(panel);

	return count;
}

static ssize_t panel_is_primary_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.is_primary);
}

static ssize_t panel_hotplug_always_on_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->desc.hotplug_always_on);
}

static ssize_t panel_hpd_enable_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			owl_panel_hpd_is_enabled(panel));
}

static ssize_t panel_hpd_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	owl_panel_hpd_enable(panel, !!val);

	return count;
}

static ssize_t panel_connected_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			owl_panel_hpd_is_connected(panel));
}

static ssize_t panel_scale_factor_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	uint16_t xfactor, yfactor;
	struct owl_panel *panel = dev_get_drvdata(dev);

	owl_panel_get_scale_factor(panel, &xfactor, &yfactor);

	return snprintf(buf, PAGE_SIZE, "%d %d\n", xfactor, yfactor);
}

static ssize_t panel_scale_factor_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int xfactor, yfactor;
	struct owl_panel *panel = dev_get_drvdata(dev);

	sscanf(buf, "%d %d\n", &xfactor, &yfactor);

	owl_panel_set_scale_factor(panel, (uint16_t)xfactor, (uint16_t)yfactor);

	return count;
}

static ssize_t panel_mode_3d_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct owl_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", owl_panel_3d_mode_get(panel));
}

static ssize_t panel_mode_3d_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int val;
	int ret = 0;
	struct owl_panel *panel = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	owl_panel_3d_mode_set(panel, val);

	return count;
}

/*
 * These are the only attributes are present for all panel.
 */
static struct device_attribute panel_dev_attrs[] = {
	__ATTR(name, S_IRUGO, panel_name_show, NULL),
	__ATTR(size, S_IRUGO, panel_size_show, NULL),
	__ATTR(default_mode, S_IRUGO | S_IWUSR, panel_default_mode_show,
	       panel_default_mode_store),
	__ATTR(mode, S_IRUGO | S_IWUSR, panel_mode_show, panel_mode_store),
	__ATTR(mode_list, S_IRUGO, panel_mode_list_show, NULL),
	__ATTR(prelines, S_IRUGO, panel_prelines_show, NULL),
	__ATTR(enable, S_IRUGO | S_IWUSR, panel_enable_show,
	       panel_enable_store),
	__ATTR(is_primary, S_IRUGO, panel_is_primary_show, NULL),
	__ATTR(hotplug_always_on, S_IRUGO, panel_hotplug_always_on_show, NULL),
	__ATTR(hpd_enable, S_IRUGO | S_IWUSR, panel_hpd_enable_show,
	       panel_hpd_enable_store),
	__ATTR(connected, S_IRUGO, panel_connected_show, NULL),
	__ATTR(scale_factor, S_IRUGO | S_IWUSR, panel_scale_factor_show,
	       panel_scale_factor_store),
	__ATTR(mode_3d, S_IRUGO | S_IWUSR, panel_mode_3d_show,
	       panel_mode_3d_store),
	__ATTR_NULL,
};

static struct class owl_panel_class = {
	.name = "owl_panel",
	.dev_release = panel_dev_release,
	.dev_attrs = panel_dev_attrs,
};
