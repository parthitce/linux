/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRV_H_
#define _OWL_DRV_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/owl_drm.h>
#include <video/owl_dss.h>

#define HAVE_DRM_IRQ

#define MAX_PLANES	   4
#define MAX_CRTCS	   2
#define MAX_ENCODERS   2
#define MAX_CONNECTORS 2

#define ERR(fmt, ...) DRM_ERROR(fmt"\n", ##__VA_ARGS__)
#define DBG(fmt, ...) DRM_DEBUG_DRIVER(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG_DRIVER(fmt"\n", ##__VA_ARGS__)
#define DBG_KMS(fmt, ...) DRM_DEBUG_KMS(fmt"\n", ##__VA_ARGS__)

#define DEV_DBG(dev, fmt, ...)  dev_dbg(dev,  "[%s] " fmt"\n",  __func__, ##__VA_ARGS__)
#define DEV_WARN(dev, fmt, ...) dev_warn(dev, "[%s] " fmt"\n",  __func__, ##__VA_ARGS__)
#define DEV_ERR(dev, fmt, ...)  dev_err(dev,  "[%s] " fmt"\n",  __func__, ##__VA_ARGS__)

struct owl_drm_panel;
struct owl_drm_overlay;

/* plane property */
enum owl_plane_property {
	PLANE_PROP_ZPOS,
	PLANE_PROP_SCALING,
	PLANE_PROP_MAX_NUM,
};

/* crtc property */
enum owl_crtc_property {
	CRTC_PROP_MODE,
	CRTC_PROP_MAX_NUM,
};

/* overlay capability */
enum owl_overlay_capability {
	OVERLAY_CAP_SCALING,
};

/*
 * Owl drm common overlay structure.
 *
 * @src_x: offset x on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @src_y: offset y on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @src_width: width of a partial image to be displayed from framebuffer.
 * @src_height: height of a partial image to be displayed from framebuffer.
 * @fb_width: width of a framebuffer.
 * @fb_height: height of a framebuffer.
 * @crtc_x: offset x on hardware screen.
 * @crtc_y: offset y on hardware screen.
 * @crtc_width: window width to be displayed (hardware screen).
 * @crtc_height: window height to be displayed (hardware screen).
 * @mode_width: width of screen mode.
 * @mode_height: height of screen mode.
 * @pixel_format: fourcc pixel format of this overlay
 * @dma_addr: array of bus(accessed by dma) address to the memory region
 *	      allocated for a overlay.
 */
struct owl_overlay_info {
	unsigned int src_x;
	unsigned int src_y;
	unsigned int src_width;
	unsigned int src_height;
	unsigned int fb_width;
	unsigned int fb_height;

	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_width;
	unsigned int crtc_height;
	unsigned int mode_width;
	unsigned int mode_height;

	uint32_t pixel_format;
	dma_addr_t dma_addr[4];
	unsigned int pitches[4];
	unsigned int offsets[4];
};

/**
 * struct owl_drm_overlay_funcs - perform operations on a given overlay
 * @apply: apply the overlayinfo
 * @enable: enable overlay output
 * @disable: disable overlay output
 * @attach: attach to a given panel
 * @detach: detach to a given panel
 * @query: query the capability/property
 */
struct owl_drm_overlay_funcs {
	/* apply drm overlay info to registers. */
	int (*apply)(struct owl_drm_overlay *overlay, struct owl_overlay_info *info);
	/* enable hardware specific overlay. */
	int (*enable)(struct owl_drm_overlay *overlay);
	/* disable hardware specific overlay. */
	int (*disable)(struct owl_drm_overlay *overlay);
	/* attach to a given panel. */
	int (*attach)(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel);
	/* detach to a given panel. */
	int (*detach)(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel);
	/* query the capability/property */
	int (*query)(struct owl_drm_overlay *overlay, int what, int *value);
};

/**
 * struct owl_drm_overlay - OWL DRM overlay object
 * @drm: DRM device owning the overlay
 * @plane: DRM plane that the overlay is bound to
 * @panel: OWL DRM panel that the overlay is attached to
 * @zpos: overlay zpos
 * @private: owl-display private data
 * @funcs: operations that can be performed on the overlay
 * @attach_list: overlay attach entry in panel
 * @list: overlay entry in registry
 */
struct owl_drm_overlay {
	struct drm_device *drm;
	struct drm_plane *plane;
	struct owl_drm_panel *panel;

	int zpos;
	void *private;

	const struct owl_drm_overlay_funcs *funcs;

	struct list_head attach_list;
	struct list_head list;
};

/**
 * struct owl_drm_panel_funcs - perform operations on a given panel
 * @detect: detect panel is conneted or not
 * @disable: disable panel (turn off back light, etc.)
 * @unprepare: turn off panel
 * @prepare: turn on panel and perform set up
 * @enable: enable panel (turn on back light, etc.)
 * @get_modes: get modes of the panel
 * return the number of modes present
 * @validate_mode: check the given mode is supported or not
 * @set_mode: set the given mode
 * @enable_vblank: enable vblank
 * @disable_vblank: disable vblank
 */
struct owl_drm_panel_funcs {
	/* connected or not */
	bool (*detect)(struct owl_drm_panel *panel);
	/* disable panel (turn off back light, etc.) */
	int (*disable)(struct owl_drm_panel *panel);
	/* turn off panel */
	int (*unprepare)(struct owl_drm_panel *panel);
	/* turn on panel and perform set up */
	int (*prepare)(struct owl_drm_panel *panel);
	/* enable panel (turn on back light, etc.) */
	int (*enable)(struct owl_drm_panel *panel);
	/* get modes */
	int (*get_modes)(struct owl_drm_panel *panel, struct owl_videomode *modes, int num_modes);
	/* check if mode is valid or not */
	bool (*validate_mode)(struct owl_drm_panel *panel, struct owl_videomode *mode);
	/* set mode */
	int (*set_mode)(struct owl_drm_panel *panel, struct owl_videomode *mode);
	/* enable device vblank. */
	int (*enable_vblank)(struct owl_drm_panel *panel);
	/* disable device vblank. */
	void (*disable_vblank)(struct owl_drm_panel *panel);
};

/**
 * struct owl_drm_panel_callback_funcs - callback operations on a given panel
 * @vsync: vsyn callback for drm system
 * @hotplug: hotplug callback for drm system
 */
struct owl_drm_panel_callback_funcs {
	/* vsync callback */
	int (*vsync)(struct owl_drm_panel *panel);
	/* hotplug callback */
	int (*hotplug)(struct owl_drm_panel *panel);
};

/**
 * struct owl_drm_panel - OWL DRM panel object
 * @drm: DRM device owning the panel
 * @connector: DRM connector that the panel is attached to
 * @dev: parent device of the panel
 * @funcs: operations that can be performed on the panel
 * @callbacks: callbacks that can be performed on the panel
 * @num_attach: number of attached overlays in attach_list
 * @attach_list: list of attached overlays objects
 * @list: panel entry in registry
 */
struct owl_drm_panel {
	struct drm_device *drm;
	struct drm_connector *connector;
	struct device *dev;

	const struct owl_drm_panel_funcs *funcs;
	struct owl_drm_panel_callback_funcs callbacks;

	int num_attach;
	struct list_head attach_list;

	struct list_head list;
};

/**
 * struct owl_drm_dssdev - Owl DRM DSS Device.
 * @drm: DRM device
 * @num_panel: number of panels in panel_list
 * @panel_list: list of panel objects
 * @num_overlay: number of overlays in overlay_list
 * @overlay_list: list of overlay objects
 */
struct owl_drm_dssdev {
	struct drm_device *drm;

	int num_panel;
	struct list_head panel_list;

	int num_overlay;
	struct list_head overlay_list;
};

/*
 * Owl drm sub driver structure.
 * @dev: pointer to device object for subdrv device driver.
 * @panel: panel defined in subdrv.
 * @display_type: owl display type
 * @load: this callback would be called by owl drm driver after
 *	subdrv is registered to it.
 * @unload: this callback is used to release resources created
 * @list: subdrv entry in registry
 */
struct owl_drm_subdrv {
	struct device *dev;
	struct owl_drm_panel *panel;

	/* OWL_DISPLAY_TYPE_x */
	int display_type;

	/* subdrv ops */
	int (*load)(struct drm_device *drm, struct owl_drm_subdrv *subdrv);
	void (*unload)(struct drm_device *drm, struct owl_drm_subdrv *subdrv);

	struct list_head list;
};

/* parameters which describe (unrotated) coordinates of scanout within a fb: */
struct owl_drm_window {
	uint32_t zpos;
	int32_t  crtc_x, crtc_y;		/* signed because can be offscreen */
	uint32_t crtc_w, crtc_h;
	uint32_t src_x, src_y;
	uint32_t src_w, src_h;
};

/* Once GO bit is set, we can't make further updates to shadowed registers
 * until the GO bit is cleared.  So various parts in the kms code that need
 * to update shadowed registers queue up a pair of callbacks, pre_apply
 * which is called before setting GO bit, and post_apply that is called
 * after GO bit is cleared.  The crtc manages the queuing, and everyone
 * else goes thru owl_crtc_apply() using these callbacks so that the
 * code which has to deal w/ GO bit state is centralized.
 */
struct owl_drm_apply {
	struct list_head pending_node, queued_node;
	bool queued;
	void (*pre_apply)(struct owl_drm_apply *apply);
	void (*post_apply)(struct owl_drm_apply *apply);
};

#ifdef HAVE_DRM_IRQ
/* For transiently registering for different DSS irqs that various parts
 * of the KMS code need during setup/configuration.  These are not
 * necessarily the same as what drm_vblank_get/put() are requesting, and
 * the hysteresis in drm_vblank_put() is not necessarily desirable for
 * internal housekeeping related irq usage.
 */
struct owl_drm_irq {
	struct list_head node;
	uint32_t irqmask;
	bool registered;
	void (*irq)(struct owl_drm_irq *irq, uint32_t irqstatus);
};
#endif /* HAVE_DRM_IRQ */

/*
 * Owl drm private structure.
 */
struct owl_drm_private {
	struct owl_drm_dssdev *dssdev;

	int num_planes;
	struct drm_plane *planes[MAX_PLANES];

	int num_crtcs;
	struct drm_crtc *crtcs[MAX_CRTCS];

	int num_encoders;
	struct drm_encoder *encoders[MAX_ENCODERS];

	int num_connectors;
	struct drm_connector *connectors[MAX_CONNECTORS];

	/* for encoders */
	unsigned int possible_crtcs;
	unsigned int possible_clones;

	/* Properties */
	struct drm_property *plane_property[PLANE_PROP_MAX_NUM];
	struct drm_property *crtc_property[CRTC_PROP_MAX_NUM];

	struct drm_fb_helper *fbdev;

	struct workqueue_struct *wq;

	/* lock for obj_list below */
	spinlock_t list_lock;

	/* list of GEM objects: */
	struct list_head obj_list;

#ifdef HAVE_DRM_IRQ
	/* irq handling: */
	spinlock_t wait_lock;       /* protects the wait_list */
	struct list_head wait_list;	/* list of owl_irq_wait */
	uint32_t irq_mask;		    /* enabled irqs in addition to wait_list */
#endif
};

/*******************************************************************************
 * fb
 ******************************************************************************/
struct drm_framebuffer *owl_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
struct drm_framebuffer *owl_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd);
int owl_framebuffer_get_planes(struct drm_framebuffer *fb);
struct drm_gem_object *owl_framebuffer_bo(struct drm_framebuffer *fb, int plane);
struct drm_framebuffer *owl_alloc_fb(struct drm_device *dev, int w, int h,
		int p, uint32_t format);

/*******************************************************************************
 * fbdev
 ******************************************************************************/
struct drm_fb_helper *owl_fbdev_init(struct drm_device *dev);
void owl_fbdev_free(struct drm_device *dev);

/*******************************************************************************
 * plane
 ******************************************************************************/
struct drm_plane *owl_plane_init(struct drm_device *dev, bool private_plane);
int owl_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h,
		void (*fxn)(void *), void *arg);
int owl_plane_dpms(struct drm_plane *plane, int mode);

/*******************************************************************************
 * crtc
 ******************************************************************************/
struct drm_crtc *owl_crtc_init(struct drm_device *dev,
		struct drm_plane *primary, struct drm_plane *cursor, unsigned int id);
int owl_crtc_apply(struct drm_crtc *crtc, struct owl_drm_apply *apply);
int owl_crtc_enable_vblank(struct drm_device *dev, int crtc_id);
void owl_crtc_disable_vblank(struct drm_device *dev, int crtc_id);
int owl_crtc_handle_vblank(struct owl_drm_panel *panel);
int owl_crtc_handle_hotplug(struct owl_drm_panel *panel);
unsigned int owl_crtc_index(const struct drm_crtc *crtc);
uint32_t owl_crtc_mask(const struct drm_crtc *crtc);

/*******************************************************************************
 * encoder
 ******************************************************************************/
struct drm_encoder *owl_encoder_init(struct drm_device *dev);

/*******************************************************************************
 * connector
 ******************************************************************************/
struct drm_connector *owl_connector_init(struct drm_device *dev, int type,
		struct drm_encoder *encoder, struct owl_drm_panel *panel);
int owl_connector_set_enabled(struct drm_connector *connector, bool enabled);
int owl_connector_update(struct drm_connector *connector, struct drm_display_mode *mode);
void copy_mode_owl_to_drm(struct drm_display_mode *mode, struct owl_videomode *owl_mode);
void copy_mode_drm_to_owl(struct owl_videomode *owl_mode, struct drm_display_mode *mode);

/*******************************************************************************
 * drm-irq
 ******************************************************************************/
#ifdef HAVE_DRM_IRQ
int owl_irq_enable_vblank(struct drm_device *dev, int crtc_id);
void owl_irq_disable_vblank(struct drm_device *dev, int crtc_id);
int owl_irq_uninstall(struct drm_device *dev);
int owl_irq_install(struct drm_device *dev);

static inline u32 pipe2irq(int crtc_id)
{
	return 1u << crtc_id;
}

/* For KMS code that needs to wait for a certain # of IRQs:
 */
struct owl_irq_wait;
struct owl_irq_wait * owl_irq_wait_init(struct drm_device *dev,
		uint32_t irqmask, int count);
int owl_irq_wait(struct drm_device *dev, struct owl_irq_wait *wait,
		unsigned long timeout);

#endif /* HAVE_DRM_IRQ */

/*******************************************************************************
 * debugfs
 ******************************************************************************/
#ifdef CONFIG_DEBUG_FS
int owl_debugfs_init(struct drm_minor *minor);
void owl_debugfs_cleanup(struct drm_minor *minor);
void owl_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
void owl_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void owl_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

/*******************************************************************************
 * dssdev ops
 ******************************************************************************/
/* Return dssdev from drm device */
static inline struct owl_drm_dssdev *owl_dssdev_get(struct drm_device *drm)
{
	struct owl_drm_private *priv = drm->dev_private;
	return priv->dssdev;
}

/* Initialize dssdev */
struct owl_drm_dssdev *owl_dssdev_init(struct drm_device *drm);

/* Allocate overlay */
struct owl_drm_overlay *owl_dssdev_alloc_overlay(struct owl_drm_dssdev *dssdev,
		struct drm_plane *plane);
/* Free overlay */
int owl_dssdev_free_overlay(struct owl_drm_overlay *overlay);

/* Attach an overlay to panel */
int owl_dssdev_attach_overlay(struct owl_drm_overlay *overlay,
		struct owl_drm_panel *panel);
/* Detach an overlay to panel */
int owl_dssdev_detach_overlay(struct owl_drm_overlay *overlay,
		struct owl_drm_panel *panel);

/* Find attached panel */
struct owl_drm_panel *owl_dssdev_get_panel(struct owl_drm_dssdev *dssdev,
		struct drm_mode_object *obj);

int owl_dssdev_register_callback(struct owl_drm_dssdev *dssdev,
		struct owl_drm_panel_callback_funcs *cbs);

/*******************************************************************************
 * subdrv ops
 ******************************************************************************/
/*
 * this function would be called by sub drivers such as display controller
 * or hdmi driver to register this sub driver object to owl drm driver
 * and when a sub driver is registered to owl drm driver a probe callback
 * of the sub driver is called and creates its own encoder and connector.
 */
int owl_subdrv_register(struct owl_drm_subdrv *subdrv);

/* this function removes subdrv list from owl drm driver */
void owl_subdrv_unregister(struct owl_drm_subdrv *subdrv);

/* called in func subdrv->load() */
int owl_subdrv_register_panel(struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel *panel);
int owl_subdrv_register_overlay(struct owl_drm_subdrv *subdrv,
		struct owl_drm_overlay *overlay);

/* called in func subdrv->unload() */
void owl_subdrv_unregister_panel(struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel *panel);
void owl_subdrv_unregister_overlay(struct owl_drm_subdrv *subdrv,
		struct owl_drm_overlay *overlay);

/*
 * this function calls a load callback registered to sub driver list and
 * create its own encoder and connector and then set drm_device object
 * to global one.
 */
int owl_subdrv_list_load(struct drm_device *drm);

/*
 * this function calls a unload callback registered to sub driver list and
 * destroy its own encoder and connetor.
 */
void owl_subdrv_list_unload(struct drm_device *drm);

/*******************************************************************************
 * subdrv declaration
 ******************************************************************************/
/* lcd and hdmi subdrivers declarations */
#ifdef CONFIG_DRM_OWL_LCD
int owl_lcd_register(void);
void owl_lcd_unregister(void);
#else
static inline int owl_lcd_register(void)
{
	return 0;
}
static inline void owl_lcd_register(void)
{
}
#endif /* CONFIG_DRM_OWL_LCD */

#ifdef CONFIG_DRM_OWL_HDMI
int owl_hdmi_register(void);
void owl_hdmi_unregister(void);
#else
static inline int __init owl_hdmi_register(void)
{
	return 0;
}
static inline void __exit owl_hdmi_unregister(void)
{
}
#endif /* CONFIG_DRM_OWL_HDMI */

/*******************************************************************************
 * utils
 ******************************************************************************/
static inline int align_pitch(int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/*
	* For 32bpp mali 450GPU needs pitch 8 bytes alignment.
	* Further more, make pitch a multiple of 64 bytes for best performance.
	*/
	return bytespp * ALIGN(width, 64);
}

#endif /* _OWL_DRV_H_ */
