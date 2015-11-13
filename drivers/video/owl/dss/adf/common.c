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
 *	2015/9/22: Created by Lipeng.
 */
#include <drm/drm_fourcc.h>

#include <video/adf.h>
#include <video/adf_format.h>
#include <video/adf_client.h>

#include <video/owl_dss.h>

#include "owl-adf.h"
#include "owl-adf-ext.h"

void get_drm_mode_from_panel(struct owl_panel *panel,
			     struct drm_mode_modeinfo *mode)
{
	memset(mode, 0, sizeof(*mode));

	/* only there three is neccessary for ADF */
	owl_panel_get_draw_size(panel, (int *)&mode->hdisplay,
				(int *)&mode->vdisplay);
	mode->vrefresh = owl_panel_get_refresh_rate(panel);

	adf_modeinfo_set_name(mode);

	ADFDBG("%s: %dx%d@%d\n", __func__,
	       mode->hdisplay, mode->vdisplay, mode->vrefresh);
}

enum owl_color_mode drm_color_to_owl_color(u32 color)
{
	enum owl_color_mode owl_color;

	switch (color) {
	case DRM_FORMAT_RGB565:
		owl_color = OWL_DSS_COLOR_RGB565;
		break;

	case DRM_FORMAT_BGR565:
		owl_color = OWL_DSS_COLOR_BGR565;
		break;

	case DRM_FORMAT_BGRA8888:
		owl_color = OWL_DSS_COLOR_BGRA8888;
		break;
	case DRM_FORMAT_BGRX8888:
		owl_color = OWL_DSS_COLOR_BGRX8888;
		break;

	case DRM_FORMAT_RGBA8888:
		owl_color = OWL_DSS_COLOR_RGBA8888;
		break;
	case DRM_FORMAT_RGBX8888:
		owl_color = OWL_DSS_COLOR_RGBX8888;
		break;

	case DRM_FORMAT_ABGR8888:
		owl_color = OWL_DSS_COLOR_ABGR8888;
		break;
	case DRM_FORMAT_XBGR8888:
		owl_color = OWL_DSS_COLOR_XBGR8888;
		break;

	case DRM_FORMAT_ARGB8888:
		owl_color = OWL_DSS_COLOR_ARGB8888;
		break;
	case DRM_FORMAT_XRGB8888:
		owl_color = OWL_DSS_COLOR_XRGB8888;
		break;

	/* Y VU */
	case DRM_FORMAT_NV12:
		owl_color = OWL_DSS_COLOR_NV12;
		break;
	/* Y UV */
	case DRM_FORMAT_NV21:
		owl_color = OWL_DSS_COLOR_NV21;
		break;
	/* Y V U */
	case DRM_FORMAT_YVU420:
		owl_color = OWL_DSS_COLOR_YVU420;
		break;
	/* Y U V */
	case DRM_FORMAT_YUV420:
		owl_color = OWL_DSS_COLOR_YUV420;
		break;

	default:
		BUG();
		break;
	}

	return owl_color;
}

enum adf_interface_type dss_type_to_interface_type(enum owl_display_type type)
{
	switch (type) {
	case OWL_DISPLAY_TYPE_LCD:
	case OWL_DISPLAY_TYPE_DUMMY:
		return ADF_INTF_DPI;

	case OWL_DISPLAY_TYPE_DSI:
		return ADF_INTF_DSI;

	case OWL_DISPLAY_TYPE_EDP:
		return ADF_INTF_eDP;

	case OWL_DISPLAY_TYPE_HDMI:
		return ADF_INTF_HDMI;

	default:
		return ADF_INTF_TYPE_DEVICE_CUSTOM;
	}
}

void owleng_get_capability_ext(struct owl_adf_overlay_engine *owleng)
{
	struct owl_adf_overlay_engine_capability_ext *cap_ext;

	cap_ext = &owleng->cap_ext;

	/* TODO, cap_ext should directly get from DE */
	cap_ext->support_transform = (OWL_ADF_TRANSFORM_FLIP_H_EXT
					| OWL_ADF_TRANSFORM_FLIP_V_EXT);
	cap_ext->support_blend = (OWL_ADF_BLENDING_PREMULT_EXT
					| OWL_ADF_BLENDING_COVERAGE_EXT);
}

/*
 * convert adf buffer and buffer config ext to overlay info,
 * it will do a simple check for the parameters,
 * if there is some invalid, return error.
 */
int adf_buffer_to_video_info(struct owl_adf_device *owladf,
			     struct adf_buffer *buf,
			     struct adf_buffer_mapping *mappings,
			     struct owl_adf_buffer_config_ext *config_ext,
			     struct owl_de_video_info *info)
{
	u16 xfactor = 100, yfactor = 100;

	u32 paddr;

	int plane, ret = 0;
	int center_x, center_y;	/* used for scaling caculation */

	int draw_width, draw_height, disp_width, disp_height;

	struct device *dev = owladf->adfdev.dev;
	struct owl_adf_interface *owlintf;

	if (config_ext->aintf_id >= owladf->n_intfs) {
		dev_err(dev, "aintf_id %d invalid\n", config_ext->aintf_id);
		return -EINVAL;
	}
	owlintf = &owladf->intfs[config_ext->aintf_id];

	owl_panel_get_scale_factor(owlintf->panel, &xfactor, &yfactor);

	owl_panel_get_resolution(owlintf->panel, &disp_width, &disp_height);
	owl_panel_get_draw_size(owlintf->panel, &draw_width, &draw_height);

	ADFDBG("%s: factor(%dx%d), disp size(%dx%d), draw size(%dx%d)\n",
	       __func__, xfactor, yfactor, disp_width, disp_height,
	       draw_width, draw_height);

	/* get central point of the outout screen */
	center_x = disp_width / 2;
	center_y = disp_height / 2;

	ADFDBG("%s: %d,%d~%d,%d --> %d,%d~%d,%d\n", __func__,
		config_ext->crop.x1, config_ext->crop.y1,
		config_ext->crop.x2, config_ext->crop.y2,
		config_ext->display.x1, config_ext->display.y1,
		config_ext->display.x2, config_ext->display.y2);

	/*
	 * detect 3D mode according to buffer flag
	 * NOTE: do not support frame 3D, TODO
	 */
	if ((config_ext->flag & OWL_ADF_BUFFER_FLAG_MODE3D_LR) != 0)
		info->mode_3d = MODE_3D_LR_HALF;
	else if ((config_ext->flag & OWL_ADF_BUFFER_FLAG_MODE3D_TB) != 0)
		info->mode_3d = MODE_3D_TB_HALF;
	else
		info->mode_3d = MODE_3D_DISABLE;
	ADFDBG("%s: mode_3d %d\n", __func__, info->mode_3d);

	info->xoff = config_ext->crop.x1;
	info->yoff = config_ext->crop.y1;
	info->width = config_ext->crop.x2 - info->xoff;
	info->height = config_ext->crop.y2 - info->yoff;

	#define ROUND100(x) (((x) + 50) / 100)

	/*
	 * adjust with draw size and display size
	 */
	info->pos_x = ROUND100(config_ext->display.x1 * disp_width * 100
			       / draw_width);
	info->pos_y = ROUND100(config_ext->display.y1 * disp_height * 100
			       / draw_height);

	info->out_width = ROUND100(config_ext->display.x2 * disp_width * 100
			       / draw_width) - info->pos_x;
	info->out_height = ROUND100(config_ext->display.y2 * disp_height * 100
			       / draw_height) - info->pos_y;

	/*
	 * Consider and calculate the scale factor.
	 *
	 * Step1, get new position's coordinates using the difference
	 * between its coordinates and the center coordinates, formulas are:
	 * | pos_x_new - center_x | = | pos_x - center_x | * xfactor / 100
	 * | pos_y_new - center_y | = | pos_y - center_y | * yfactor / 100
	 *
	 * Step2, get new out_width & out_height using the following formulas:
	 *	out_width_new = out_width * xfactor / 100
	 *	out_height_new = out_height * yfactor / 100
	 */
	if (info->pos_x < center_x)
		info->pos_x = center_x - ROUND100((center_x - info->pos_x)
						  * xfactor);
	else
		info->pos_x = center_x + ROUND100((info->pos_x - center_x)
						  * xfactor);

	if (info->pos_y < center_y)
		info->pos_y = center_y - ROUND100((center_y - info->pos_y)
						  * yfactor);
	else
		info->pos_y = center_y + ROUND100((info->pos_y - center_y)
						  * yfactor);

	info->out_width = ROUND100(info->out_width * xfactor);
	info->out_height = ROUND100(info->out_height * yfactor);
	#undef ROUND100

	info->color_mode = drm_color_to_owl_color(buf->format);

	ADFDBG("%s: blend %d, alpha %d\n", __func__,
		config_ext->blend_type, config_ext->plane_alpha);
	if (config_ext->blend_type == OWL_ADF_BLENDING_NONE_EXT) {
		info->global_alpha_en = true;
		info->global_alpha = 0xff;
	} else {
		info->global_alpha_en = true;
		if (config_ext->blend_type == OWL_ADF_BLENDING_PREMULT_EXT)
			info->pre_mult_alpha_en = true;
		else
			info->pre_mult_alpha_en = false;
	}
	info->global_alpha = config_ext->plane_alpha;

	if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_H_EXT)
		info->rotation = 2;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_FLIP_V_EXT)
		info->rotation = 1;
	else if (config_ext->transform == OWL_ADF_TRANSFORM_ROT_180_EXT)
		info->rotation = 3;
	else
		info->rotation = 0;

	info->mmu_enable = false;
	if (mappings == NULL) {
		/* maybe a validate buffer, paddr canbe zero */
		ADFDBG("#mappings is NULL)\n");
		paddr = 0;
	} else if (config_ext->stamp == OWL_ADF_FBDEV_BUF_ID) {
		ADFDBG("###NO MMU(maybe ADF FBDEV)\n");
		paddr = sg_phys(mappings->sg_tables[0]->sgl);
	} else {
		ret = owl_de_mmu_sg_table_to_da(mappings->sg_tables[0],
						config_ext->stamp, &paddr);
		if (ret < 0) {
			dev_err(dev, "MMU ERROR %d\n", ret);
			goto err_out;
		}
		ADFDBG("paddr %d\n", paddr);
		info->mmu_enable = true;
	}

	if (adf_format_is_rgb(buf->format)) {
		info->n_planes = 1;

		info->offset[0] = buf->offset[0];
		info->pitch[0] = buf->pitch[0];

		info->addr[0] = paddr + info->offset[0]
			+ info->yoff * info->pitch[0]
			+ info->xoff * adf_format_plane_cpp(buf->format, 0);

		ADFDBG("%s: RGB, P %d, O %d, A %ld\n",
			__func__, info->pitch[0],
			info->offset[0], info->addr[0]);
	} else {
		info->n_planes = buf->n_planes;
		for (plane = 0; plane < info->n_planes; plane++) {
			info->offset[plane] = buf->offset[plane];
			info->pitch[plane] = buf->pitch[plane];

			info->addr[plane] = paddr + info->offset[plane]
				+ info->yoff * info->pitch[plane]
				+ info->xoff / adf_format_plane_cpp(buf->format,
								    plane);

			ADFDBG("%s: YUV, plane %d, P %d, O %d, A %ld\n",
			       __func__, plane, info->pitch[plane],
			       info->offset[plane], info->addr[plane]);
		}
	}

	return 0;

err_out:
	return ret;
}

int buffer_and_ext_validate(struct owl_adf_device *owladf,
			    struct adf_buffer *buf,
			    struct owl_adf_buffer_config_ext *config_ext)
{
	int ret = 0;

	struct owl_adf_overlay_engine *owleng;
	struct owl_de_video_info info;

	owleng = eng_to_owl_eng(buf->overlay_engine);
	if (!owleng || !owleng->attached) {
		dev_err(owladf->adfdev.dev, "not attched\n");
		ret = -ENODEV;
		goto out;
	}

	if (adf_buffer_to_video_info(owladf, buf, NULL,
				     config_ext, &info) < 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = owl_de_video_info_validate(owleng->video, &info);
out:
	ADFDBG("%s: ret = %d\n", __func__, ret);
	return ret;
}
