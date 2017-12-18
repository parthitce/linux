/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_DRM_IOMMU_H_
#define _OWL_DRM_IOMMU_H_

#define OWL_DEV_ADDR_START	0x20000000
#define OWL_DEV_ADDR_SIZE	0x40000000
#define OWL_DEV_ADDR_ORDER	0x0

#ifdef CONFIG_DRM_OWL_IOMMU

int drm_create_iommu_mapping(struct drm_device *drm_dev);

void drm_release_iommu_mapping(struct drm_device *drm_dev);

int drm_iommu_attach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev);

void drm_iommu_detach_device(struct drm_device *dev_dev,
				struct device *subdrv_dev);

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct device *dev = drm_dev->dev;

	return dev->archdata.mapping ? true : false;
#else
	return false;
#endif
}

#else

struct dma_iommu_mapping;
static inline int drm_create_iommu_mapping(struct drm_device *drm_dev)
{
	return 0;
}

static inline void drm_release_iommu_mapping(struct drm_device *drm_dev)
{
}

static inline int drm_iommu_attach_device(struct drm_device *drm_dev,
						struct device *subdrv_dev)
{
	return 0;
}

static inline void drm_iommu_detach_device(struct drm_device *drm_dev,
						struct device *subdrv_dev)
{
}

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
	return false;
}

#endif
#endif
