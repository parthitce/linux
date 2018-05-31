#include "dispc.h"

static enum owl_color_mode drm_format_to_dss_format(u32 format)
{
	enum owl_color_mode dss_format = OWL_DSS_COLOR_BGRX8888;

	switch (format) {
	case DRM_FORMAT_RGB565:
		dss_format = OWL_DSS_COLOR_RGB565;
		break;
	case DRM_FORMAT_BGR565:
		dss_format = OWL_DSS_COLOR_BGR565;
		break;
	case DRM_FORMAT_BGRA8888:
		dss_format = OWL_DSS_COLOR_ARGB8888;
		break;
	case DRM_FORMAT_BGRX8888:
		dss_format = OWL_DSS_COLOR_XRGB8888;
		break;
	case DRM_FORMAT_RGBA8888:
		dss_format = OWL_DSS_COLOR_ABGR8888;
		break;
	case DRM_FORMAT_RGBX8888:
		dss_format = OWL_DSS_COLOR_XBGR8888;
		break;
	case DRM_FORMAT_ABGR8888:
		dss_format = OWL_DSS_COLOR_RGBA8888;
		break;
	case DRM_FORMAT_XBGR8888:
		dss_format = OWL_DSS_COLOR_RGBX8888;
		break;
	case DRM_FORMAT_ARGB8888:
		dss_format = OWL_DSS_COLOR_BGRA8888;
		break;
	case DRM_FORMAT_XRGB8888:
		dss_format = OWL_DSS_COLOR_BGRX8888;
		break;
	case DRM_FORMAT_NV12: /* Y VU */
		dss_format = OWL_DSS_COLOR_NV12;
		break;
	case DRM_FORMAT_NV21: /* Y UV */
		dss_format = OWL_DSS_COLOR_NV21;
		break;
	case DRM_FORMAT_YVU420: /* Y V U */
		dss_format = OWL_DSS_COLOR_YVU420;
		break;
	case DRM_FORMAT_YUV420: /* Y U V */
		dss_format = OWL_DSS_COLOR_YUV420;
		break;
	default:
		ERR("unrecognized pixel format %x, fallback to DRM_FORMAT_BGRX8888", format);
		break;
	}

	return dss_format;
}

/*******************************************************************************
 * struct owl_drm_panel: common implemention of struct owl_drm_panel_funcs
 ******************************************************************************/
bool dispc_panel_detect(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	bool connected = true;

	if (mgr->type == OWL_DRM_DISPLAY_EXTERNAL)
		connected = owl_panel_hpd_is_connected(mgr->owl_panel);

	DSS_DBG(mgr, "connected=%d", connected);

	return connected;
}

/* delay the power enble actions until some videos are on */
int dispc_panel_enable(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	int ret;

	DSS_DBG(mgr, "panel=%p", panel);

	ret = dispc_manager_set_enabled(mgr, true);
	if (ret) {
		DSS_ERR(mgr, "panel=%p, enable failed", panel);
		goto out;
	}

	panel->enabled = true;
out:
	return ret;
}

int dispc_panel_disable(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);

	DSS_DBG(mgr, "panel=%p", panel);

	dispc_manager_set_enabled(mgr, false);
	panel->enabled = false;

	return 0;
}

int dispc_panel_prepare(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	DSS_DBG(mgr, "panel=%p", panel);
	return 0;
}

int dispc_panel_unprepare(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	DSS_DBG(mgr, "panel=%p", panel);
	return 0;
}

int dispc_panel_get_modes(struct owl_drm_panel *panel,
		struct owl_videomode *modes, int num_modes)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	return owl_panel_get_mode_list(mgr->owl_panel, modes, num_modes);
}

bool dispc_panel_validate_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	struct owl_panel *owl_panel = mgr->owl_panel;
	struct owl_videomode *mode_list;
	int count, i;

	if (owl_panel->n_modes) { /* possibly multi-resolution panel */
		mode_list = owl_panel->mode_list;
		count = owl_panel->n_modes;
	} else {                  /* possibly fix resolution panel */
		mode_list = &owl_panel->mode;
		count = 1;
	}

	for (i = 0; i < count; i++) {
		struct owl_videomode *m = &mode_list[i];
		/* FIXME: Should validate the timing too ? */
		if (mode->xres == m->xres &&
			mode->yres == m->yres &&
			(!mode->refresh || mode->refresh == m->refresh)) {
			return true;
		}
	}

	return false;
}

static void modeset_hotplug(struct owl_panel *owl_panel, void *data, u32 status)
{
	struct dispc_manager *mgr = data;
	complete(&mgr->modeset_completion);
}

int dispc_panel_set_mode(struct owl_drm_panel *panel, struct owl_videomode *mode)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	struct owl_panel *owl_panel = mgr->owl_panel;

	DSS_DBG(mgr, "panel=%p, mode=%dx%d-%dHZ", panel, mode->xres, mode->yres, mode->refresh);

	if (mode->xres == owl_panel->mode.xres &&
		mode->yres == owl_panel->mode.yres &&
		mode->refresh == owl_panel->mode.refresh)
		return 0;

	if (!owl_panel_hpd_is_connected(mgr->owl_panel)) {
		owl_panel_set_default_mode(owl_panel, mode);
		return 0;
	}

	/* reinitialize in case of accident */
	INIT_COMPLETION(mgr->modeset_completion);

	/* fake the modeset hotplug handler */
	owl_panel_hotplug_cb_set(mgr->owl_panel, modeset_hotplug, mgr);

	/* simulate hotplug in */
	owl_panel_hpd_enable(owl_panel, false);
	wait_for_completion_interruptible_timeout(&mgr->modeset_completion, msecs_to_jiffies(3000));

	/* disable panel */
	dispc_manager_set_enabled(mgr, false);

	/* config the target mode */
	owl_panel_set_default_mode(owl_panel, mode);

	/* simulate hotplug out */
	owl_panel_hpd_enable(owl_panel, true);
	wait_for_completion_interruptible_timeout(&mgr->modeset_completion, msecs_to_jiffies(3000));

	/* enable panel */
	dispc_manager_set_enabled(mgr, true);

	/* restore hotplug handler */
	owl_panel_hotplug_cb_set(mgr->owl_panel, mgr->hotplug, mgr);

	return 0;
}

int dispc_panel_enable_vblank(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);

	DSS_DBG(mgr, "panel=%p", panel);
	return owl_de_path_enable_vsync(mgr->owl_path);
}

void dispc_panel_disable_vblank(struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);

	DSS_DBG(mgr, "panel=%p", panel);
	owl_de_path_disable_vsync(mgr->owl_path);
}

void dispc_panel_default_vsync(struct owl_panel *owl_panel, void *data, u32 status)
{
	struct dispc_manager *mgr = data;
	struct owl_drm_panel *panel = mgr->subdrv.panel;

	if (!panel) {
		DSS_DBG(mgr, "drm panel not connected");
		goto out;
	}

	if (panel->callbacks.vsync)
		panel->callbacks.vsync(panel);
out:
	atomic_inc(&mgr->vsync_counter);
}

void dispc_panel_default_hotplug(struct owl_panel *owl_panel, void *data, u32 status)
{
	struct dispc_manager *mgr = data;
	struct owl_drm_panel *panel = mgr->subdrv.panel;

	DSS_DBG(mgr, "hotplug=%u", status);

	if (!panel) {
		DSS_DBG(mgr, "drm panel not connected");
		goto out;
	}

	/* FIXME: Should we do the dpms here ? */
	if (!status)
		dispc_manager_set_enabled(mgr, false);
	else if (panel->enabled)
		dispc_manager_set_enabled(mgr, true);

	if (panel->callbacks.hotplug)
		panel->callbacks.hotplug(panel);
out:
	atomic_inc(&mgr->hotplug_counter);
}

/*******************************************************************************
 * struct owl_drm_overlay: common implemention of struct owl_drm_overlay_funcs
 ******************************************************************************/
static int drm_overlay_to_dss_videoinfo(struct dispc_manager *mgr,
		struct owl_overlay_info *overlay, struct owl_de_video_info *info)
{
	struct owl_panel *owl_panel = mgr->owl_panel;
	int disp_x, disp_y, disp_width, disp_height; /* real display area */
	int draw_width, draw_height;
	int plane;

	owl_panel_get_draw_size(owl_panel, &draw_width, &draw_height);
	owl_panel_get_disp_area(owl_panel, &disp_x, &disp_y, &disp_width, &disp_height);

	DSS_DBG(mgr, "disp area(%d,%d %dx%d); draw area(0,0 %dx%d)",
			disp_x, disp_y, disp_width, disp_height, draw_width, draw_height);

	info->xoff = overlay->src_x;
	info->yoff = overlay->src_y;
	info->width = overlay->src_width;
	info->height = overlay->src_height;

	if (overlay->mode_width != owl_panel->mode.xres) {
		info->pos_x = DIV_ROUND_CLOSEST(overlay->crtc_x * owl_panel->mode.xres, overlay->mode_width);
		info->out_width = DIV_ROUND_CLOSEST(overlay->crtc_width * owl_panel->mode.xres, overlay->mode_width);
	} else {
		info->pos_x = overlay->crtc_x;
		info->out_width = overlay->crtc_width;
	}

	if (overlay->mode_height != owl_panel->mode.yres) {
		info->pos_y = DIV_ROUND_CLOSEST(overlay->crtc_y * owl_panel->mode.yres, overlay->mode_height);
		info->out_height = DIV_ROUND_CLOSEST(overlay->crtc_height * owl_panel->mode.yres, overlay->mode_height);
	} else {
		info->pos_y = overlay->crtc_y;
		info->out_height = overlay->crtc_height;
	}

	info->is_original_scaled = (info->width != info->out_width || info->height != info->out_height);

	DSS_DBG(mgr, "crop: (%d,%d, %dx%d) --> display: (%d,%d, %dx%d)",
			info->xoff, info->yoff, info->width, info->height,
			info->pos_x, info->pos_y, info->out_width, info->out_height);

	/*
	 * caculate real position without scaling
	 */
	info->real_pos_x = (info->pos_x == 0 ? 0 : (info->pos_x - (info->width - info->out_width) / 2));
	info->real_pos_x = (info->real_pos_x < 0 ? 0 : info->real_pos_x);

	info->real_pos_y = (info->pos_y == 0 ? 0 : (info->pos_y - (info->height - info->out_height) / 2));
	info->real_pos_y = (info->real_pos_y < 0 ? 0 : info->real_pos_y);

	DSS_DBG(mgr, "original area-->");
	DSS_DBG(mgr, "(xoff,yoff, WidthxHeight) --> (pos_x,pos_y/real_pos_x,real_pos_y WidthxHeight)");
	DSS_DBG(mgr, "(%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)",
			info->xoff, info->yoff, info->width, info->height,
			info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
			info->out_width, info->out_height);

	/*
	 * Adjust with draw size and display area size.
	 *
	 * There are 2 rectangles in the coordinate system, one is
	 * DRAW rectangle, denoted by (0,0 draw_width,draw_height),
	 * another is DISPLAY rectangle, denoted by
	 * (disp_x,disp_y disp_width,disp_height).
	 *
	 * Now, we need convert the target rectangle
	 * (pos_x,pos_y out_width,out_height) from DRAW rectangle to
	 * DISPLAY rectangle, the algorithm is:
	 * 	out_width / draw_width = out_width_new / disp_width,
	 * 	out_height / draw_height = out_height_new / disp_height,
	 *	(pos_x - 0) / draw_width = (pso_x_new - disp_x) / disp_width
	 *	(pos_y - 0) / draw_height = (pso_y_new - disp_y) / disp_height
	 */
	info->pos_x = DIV_ROUND_CLOSEST(info->pos_x * disp_width, draw_width) + disp_x;
	info->pos_y = DIV_ROUND_CLOSEST(info->pos_y * disp_height, draw_height) + disp_y;

	info->out_width = DIV_ROUND_CLOSEST(info->out_width * disp_width, draw_width);
	info->out_height = DIV_ROUND_CLOSEST(info->out_height * disp_height, draw_height);

	DSS_DBG(mgr, "area after adjusting-->");
	DSS_DBG(mgr, "(xoff,yoff, WidthxHeight) --> (pos_x,pos_y/real_pos_x,real_pos_y WidthxHeight)");
	DSS_DBG(mgr, "(%d,%d %dx%d) --> (%d,%d/%d,%d %dx%d)",
			info->xoff, info->yoff, info->width, info->height,
			info->pos_x, info->pos_y, info->real_pos_x, info->real_pos_y,
			info->out_width, info->out_height);

	info->color_mode = drm_format_to_dss_format(overlay->pixel_format);

	/* FIXME: OWL_BLENDING_PREMULT, OWL_BLENDING_COVERAGE, OWL_BLENDING_NONE ? */
	info->blending = OWL_BLENDING_PREMULT;
	info->alpha = 255;

	info->rotation = 0;
	info->mmu_enable = owl_de_mmu_is_present();
	info->n_planes = drm_format_num_planes(overlay->pixel_format);

	for (plane = 0; plane < info->n_planes; plane++) {
		dma_addr_t addr;

		info->offset[plane] = overlay->offsets[plane];
		info->pitch[plane] = overlay->pitches[plane];

		if (info->mmu_enable) {
			if (owl_de_mmu_handle_to_addr(overlay->dma_addr[plane], &addr)) {
				DSS_ERR(mgr, "owl_de_mmu_handle_to_addr failed (hnd=%p)",
						(void*)overlay->dma_addr[plane]);
				return -EFAULT;
			}
			info->addr[plane] = addr + info->offset[plane];
		} else {
			info->addr[plane] = overlay->dma_addr[plane] + info->offset[plane];
		}

		DSS_DBG(mgr, "plane %d, Pitch %d, Offset %d, Addr %ld",
				plane, info->pitch[plane], info->offset[plane],
				info->addr[plane]);
	}

	/* choose any values as you like */
	info->brightness = owl_panel_brightness_get(owl_panel);
	info->contrast = owl_panel_contrast_get(owl_panel);
	info->saturation = owl_panel_saturation_get(owl_panel);

	return 0;
}

int dispc_ovr_apply(struct owl_drm_overlay *overlay, struct owl_overlay_info *info)
{
	struct dispc_manager *mgr = overlay_to_mgr(overlay);
	struct owl_de_path *owl_path = mgr->owl_path;
	struct owl_de_video *owl_video = overlay->private;
	struct owl_de_video_info owl_info;
	int ret = 0;

	DSS_DBG(mgr, "overlay=%p, zpos=%d", overlay, overlay->zpos);

	if (!dispc_manager_is_enabled(mgr)) {
		DSS_DBG(mgr, "panel not enabled yet");
		return -EINVAL;
	}

	owl_de_video_get_info(owl_video, &owl_info);

	ret = drm_overlay_to_dss_videoinfo(mgr, info, &owl_info);
	if (ret < 0) {
		DSS_ERR(mgr, "overlay info to video info error");
		return -EINVAL;
	}

	ret = owl_de_video_info_validate(owl_video, &owl_info);
	if (ret < 0) {
		DSS_ERR(mgr, "video info validate (err=%d)", ret);
		return -EINVAL;
	}

	owl_de_video_set_info(owl_video, &owl_info);
	owl_de_path_attach(owl_path, owl_video);
	owl_de_path_apply(owl_path);

	/* Do not wait for vsync */
	/*
	 * owl_de_path_wait_for_go(owl_path);
	 */

	return ret;
}

int dispc_ovr_enable(struct owl_drm_overlay *overlay)
{
	struct dispc_manager *mgr = overlay_to_mgr(overlay);
	DSS_DBG(mgr, "overlay=%p, zpos=%d", overlay, overlay->zpos);
	return 0;
}

int dispc_ovr_disable(struct owl_drm_overlay *overlay)
{
	struct dispc_manager *mgr = overlay_to_mgr(overlay);
	struct owl_de_path *owl_path = mgr->owl_path;
	struct owl_de_video *owl_video = overlay->private;

	DSS_DBG(mgr, "overlay=%p, zpos=%d", overlay, overlay->zpos);

	mutex_lock(&mgr->mutex);

	if (owl_path->ops && owl_path->ops->detach) {
		owl_path->ops->detach(owl_path, owl_video);
		owl_path->ops->set_fcr(owl_path);
 	}

	owl_de_path_detach(owl_path, owl_video);

	mutex_unlock(&mgr->mutex);

	return 0;
}

int dispc_ovr_attach(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);

	DSS_DBG(mgr, "panel=%p, overlay=%p, zpos=%d", panel, overlay, overlay->zpos);

	if (overlay->zpos >= mgr->num_videos ||
		overlay->private != mgr->owl_videos[overlay->zpos]) {
		DSS_ERR(mgr, "Not support overlay dynamic zpos yet");
		return -EINVAL;
	}

	return 0;
}

int dispc_ovr_detach(struct owl_drm_overlay *overlay, struct owl_drm_panel *panel)
{
	struct dispc_manager *mgr = panel_to_mgr(panel);
	DSS_DBG(mgr, "panel=%p, overlay=%p, zpos=%d", panel, overlay, overlay->zpos);
	return 0;
}

int dispc_ovr_query(struct owl_drm_overlay *overlay, int what, int *value)
{
	DBG("overlay(%d)=%p, what=%d", overlay->zpos, overlay, what);

	switch (what) {
	case OVERLAY_CAP_SCALING:
		*value = owl_de_video_has_scaler(overlay->private);
		break;
	default:
		/* overlay may not have attached to any panel */
		ERR("invalid query %d", what);
		return -EINVAL;
	}

	return 0;
}

/*******************************************************************************
 * struct owl_drm_subdrv: panel/overlay register/unregister
 ******************************************************************************/
static struct owl_drm_panel_funcs owldrm_panel_funcs = {
	.detect        = dispc_panel_detect,
	.prepare       = dispc_panel_prepare,
	.enable        = dispc_panel_enable,
	.disable       = dispc_panel_disable,
	.unprepare     = dispc_panel_unprepare,

	.get_modes     = dispc_panel_get_modes,
	.validate_mode = dispc_panel_validate_mode,

	.enable_vblank   = dispc_panel_enable_vblank,
	.disable_vblank  = dispc_panel_disable_vblank,
};

static const struct owl_drm_overlay_funcs owldrm_overlay_funcs = {
	.apply   = dispc_ovr_apply,
	.enable  = dispc_ovr_enable,
	.disable = dispc_ovr_disable,
	.attach  = dispc_ovr_attach,
	.detach  = dispc_ovr_detach,
	.query   = dispc_ovr_query,
};

static struct owl_drm_overlay *owldrm_overlays;

int dispc_subdrv_add_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	struct dispc_manager *mgr = subdrv_to_mgr(subdrv);
	int i;

	/* Ignore duplicate register */
	if (owldrm_overlays)
		return 0;

	owldrm_overlays = kcalloc(mgr->num_videos, sizeof(*owldrm_overlays), GFP_KERNEL);
	if (!owldrm_overlays)
		return -ENOMEM;

	for (i = 0; i < mgr->num_videos; i++) {
		owldrm_overlays[i].drm = drm;
		owldrm_overlays[i].zpos = i;
		owldrm_overlays[i].private = mgr->owl_videos[i];
		owldrm_overlays[i].funcs = &owldrm_overlay_funcs;
		INIT_LIST_HEAD(&owldrm_overlays[i].attach_list);
		INIT_LIST_HEAD(&owldrm_overlays[i].list);
		owl_subdrv_register_overlay(subdrv, &owldrm_overlays[i]);
	}

	return 0;
}

void dispc_subdrv_remove_overlays(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	struct dispc_manager *mgr = subdrv_to_mgr(subdrv);
	int i;

	if (!owldrm_overlays)
		return;

	for (i = 0; i < mgr->num_videos; i++)
		owl_subdrv_unregister_overlay(subdrv, &owldrm_overlays[i]);

	kfree(owldrm_overlays);
	owldrm_overlays = NULL;
}

int dispc_subdrv_add_panel(struct drm_device *drm, struct owl_drm_subdrv *subdrv,
		struct owl_drm_panel_funcs *funcs)
{
	struct owl_drm_panel *panel = devm_kzalloc(drm->dev, sizeof(*panel), GFP_KERNEL);

	if (!panel)
		return -ENOMEM;

	panel->drm = drm;
	panel->dev = subdrv->dev;
	panel->funcs = funcs;
	INIT_LIST_HEAD(&panel->attach_list);
	INIT_LIST_HEAD(&panel->list);

	subdrv->panel = panel;

	return owl_subdrv_register_panel(subdrv, panel);
}

void dispc_subdrv_remove_panel(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	owl_subdrv_unregister_panel(subdrv, subdrv->panel);
}

int dispc_subdrv_load(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_add_overlays(drm, subdrv);
	dispc_subdrv_add_panel(drm, subdrv, &owldrm_panel_funcs);
	return 0;
}

void dispc_subdrv_unload(struct drm_device *drm, struct owl_drm_subdrv *subdrv)
{
	dispc_subdrv_remove_overlays(drm, subdrv);
	dispc_subdrv_remove_panel(drm, subdrv);
}

/*******************************************************************************
 * struct dispc_manager
 ******************************************************************************/
static struct dispc_manager *dispc_mgrs[OWL_DRM_NUM_DISPLAY_TYPES];

struct dispc_manager *dispc_manager_get(int type)
{
	return dispc_mgrs[type];
}

bool dispc_manager_is_enabled(struct dispc_manager *mgr)
{
	bool enabled;

	mutex_lock(&mgr->mutex);
	enabled = mgr->enabled;
	mutex_unlock(&mgr->mutex);

	return enabled;
}

int dispc_manager_set_enabled(struct dispc_manager *mgr, bool enabled)
{
	DSS_DBG(mgr, "enabled=%d", enabled);

	mutex_lock(&mgr->mutex);

	if (mgr->enabled == enabled)
		goto out_unlock;

	if (enabled) {
		if (!owl_de_path_is_enabled(mgr->owl_path))
			owl_de_path_enable(mgr->owl_path);
		if (!owl_panel_is_enabled(mgr->owl_panel))
			owl_panel_enable(mgr->owl_panel);
	} else {
		if (owl_panel_is_enabled(mgr->owl_panel))
			owl_panel_disable(mgr->owl_panel);
		if (owl_de_path_is_enabled(mgr->owl_path))
			owl_de_path_disable(mgr->owl_path);
	}

	mgr->enabled = enabled;

out_unlock:
	mutex_unlock(&mgr->mutex);
	return 0;
}

struct dispc_manager *dispc_manager_create(struct device *dev, int display_type)
{
	struct dispc_manager *mgr = NULL;
	int type, i, ret = 0;

	switch (display_type) {
	case OWL_DISPLAY_TYPE_LCD:
		type = OWL_DRM_DISPLAY_PRIMARY;
		break;
	case OWL_DISPLAY_TYPE_HDMI:
		type = OWL_DRM_DISPLAY_EXTERNAL;
		break;
	default:
		DEV_ERR(dev, "unsupported display type=0x%x)", display_type);
		return ERR_PTR(-EINVAL);
	};

	if (dispc_mgrs[type]) {
		DEV_ERR(dev, "drm display type 0x%x already initialized)", type);
		return ERR_PTR(-EINVAL);
	}

	mgr = devm_kzalloc(dev, sizeof(*mgr), GFP_KERNEL);
	if (!mgr) {
		ret = -ENOMEM;
		goto fail;
	}

	/* get path */
	mgr->owl_path = owl_de_path_get_by_type(display_type);
	if (!mgr->owl_path || !mgr->owl_path->current_panel) {
		DEV_ERR(dev, "fail to get path (display type=0x%x)", display_type);
		ret = -ENODEV;
		goto fail_free;
	}

	/* get panel */
	mgr->owl_panel = mgr->owl_path->current_panel;

	/* get videos */
	mgr->num_videos = owl_de_get_video_num();
#ifdef CONFIG_VIDEO_OWL_DE_S700
	mgr->num_videos /= 4;
#endif

	if (mgr->num_videos > MAX_VIDEOS) {
		DEV_WARN(dev, "increase MAX_VIDEOS to support more videos (up to %d)",
				mgr->num_videos);
		mgr->num_videos = MAX_VIDEOS;
	}

	for (i = 0; i < mgr->num_videos; i++) {
#ifdef CONFIG_VIDEO_OWL_DE_S700
		mgr->owl_videos[i] = owl_de_video_get_by_index(4 * i);
#else
		mgr->owl_videos[i] = owl_de_video_get_by_index(i);
#endif
		if (!mgr->owl_videos[i]) {
			DEV_ERR(dev, "fail to get video %d", i);
			ret = -ENODEV;
			goto fail_free;
		}
	}

	/* cancel the mmu skipping introduced for android */
	owl_de_path_set_mmuskip(mgr->owl_path, 0);

	mutex_init(&mgr->mutex);
	init_completion(&mgr->modeset_completion);

	mgr->subdrv.display_type = display_type;
	mgr->subdrv.dev = dev;
	mgr->type = type;
	dispc_mgrs[type] = mgr;

	dev_set_drvdata(dev, mgr);
	return mgr;
fail_free:
	devm_kfree(dev, mgr);
fail:
	return ERR_PTR(ret);
}

void dispc_manager_destroy(struct dispc_manager *mgr)
{
	complete_all(&mgr->modeset_completion);
	dispc_manager_set_enabled(mgr, false);
	dispc_mgrs[mgr->type] = NULL;
}
