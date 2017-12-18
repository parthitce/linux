/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _OWL_DRM_HDMI_H_
#define _OWL_DRM_HDMI_H_

#define DRM_DEBUG_ENABLE
#include <video/owl_dss.h>

#define WINDOWS_NR	4

#define get_hdmi_context(dev)	platform_get_drvdata(to_platform_device(dev))

struct hdmi_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	dma_addr_t		dma_addr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
	bool			enabled;
	bool			resume;
};

struct hdmi_context {
	struct owl_drm_subdrv		subdrv;
	int 				vblank_en;
	struct drm_crtc			*crtc;

	unsigned int			default_win;
	bool				suspended;
	struct mutex			lock;

	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;

	/* for debug */
	ktime_t				pre_vsync_stamp;
	u32				pre_vsync_duration_us;

	/* owl number */
	struct owl_panel		*panel;
	struct owl_de_path		*path;
	struct owl_de_video		*video[WINDOWS_NR];
	enum owl_dss_state		state;

};
#endif
