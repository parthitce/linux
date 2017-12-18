/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _OWL_DRM_CONNECTOR_H_
#define _OWL_DRM_CONNECTOR_H_

struct drm_connector *owl_drm_connector_create(struct drm_device *dev,
						   struct drm_encoder *encoder);

struct drm_encoder *owl_drm_best_encoder(struct drm_connector *connector);

void owl_drm_display_power(struct drm_connector *connector, int mode);

#endif
