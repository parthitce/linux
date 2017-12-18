/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/owl_drm.h>

#include "owl_drm_drv.h"
#include "owl_drm_gem.h"
#include "owl_drm_buf.h"
#include "owl_drm_iommu.h"

static int lowlevel_buffer_allocate(struct drm_device *dev,
		unsigned int flags, struct owl_drm_gem_buf *buf)
{
	int ret = 0;
	enum dma_attr attr;
	unsigned int nr_pages;

	DRM_DEBUG_KMS("\n");

	if (buf->dma_addr) {
		DRM_DEBUG_KMS("already allocated.\n");
		return 0;
	}

	init_dma_attrs(&buf->dma_attrs);

	/*
	 * if OWL_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(flags & OWL_BO_NONCONTIG))
		dma_set_attr(DMA_ATTR_FORCE_CONTIGUOUS, &buf->dma_attrs);

	/*
	 * if OWL_BO_WC or OWL_BO_NONCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (flags & OWL_BO_WC || !(flags & OWL_BO_CACHABLE))
		attr = DMA_ATTR_WRITE_COMBINE;
	else
		attr = DMA_ATTR_NON_CONSISTENT;

	dma_set_attr(attr, &buf->dma_attrs);
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &buf->dma_attrs);

	nr_pages = buf->size >> PAGE_SHIFT;

	if (!is_drm_iommu_supported(dev)) {
		dma_addr_t start_addr;
		unsigned int i = 0;

		buf->pages = kzalloc(sizeof(struct page) * nr_pages,
				GFP_KERNEL);
		if (!buf->pages) {
			DRM_ERROR("failed to allocate pages.\n");
			return -ENOMEM;
		}

		buf->kvaddr = dma_alloc_attrs(dev->dev, buf->size,
				&buf->dma_addr, GFP_KERNEL,
				&buf->dma_attrs);
		if (!buf->kvaddr) {
			DRM_ERROR("failed to allocate buffer.\n");
			kfree(buf->pages);
			return -ENOMEM;
		}

		start_addr = buf->dma_addr;
		while (i < nr_pages) {
			buf->pages[i] = phys_to_page(start_addr);
			start_addr += PAGE_SIZE;
			i++;
		}
	} else {

		buf->pages = dma_alloc_attrs(dev->dev, buf->size,
				&buf->dma_addr, GFP_KERNEL,
				&buf->dma_attrs);
		if (!buf->pages) {
			DRM_ERROR("failed to allocate buffer.\n");
			return -ENOMEM;
		}
	}

	buf->sgt = drm_prime_pages_to_sg(buf->pages, nr_pages);
	if (!buf->sgt) {
		DRM_ERROR("failed to get sg table.\n");
		ret = -ENOMEM;
		goto err_free_attrs;
	}

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)buf->dma_addr,
			buf->size);

	return ret;

err_free_attrs:
	dma_free_attrs(dev->dev, buf->size, buf->pages,
			(dma_addr_t)buf->dma_addr, &buf->dma_attrs);
	buf->dma_addr = (dma_addr_t)NULL;

	if (!is_drm_iommu_supported(dev))
		kfree(buf->pages);

	return ret;
}

static void lowlevel_buffer_deallocate(struct drm_device *dev,
		unsigned int flags, struct owl_drm_gem_buf *buf)
{
	DRM_DEBUG_KMS("%s.\n", __func__);

	if (!buf->dma_addr) {
		DRM_DEBUG_KMS("dma_addr is invalid.\n");
		return;
	}

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)buf->dma_addr,
			buf->size);

	sg_free_table(buf->sgt);

	kfree(buf->sgt);
	buf->sgt = NULL;

	if (!is_drm_iommu_supported(dev)) {
		dma_free_attrs(dev->dev, buf->size, buf->kvaddr,
				(dma_addr_t)buf->dma_addr, &buf->dma_attrs);
		kfree(buf->pages);
	} else
		dma_free_attrs(dev->dev, buf->size, buf->pages,
				(dma_addr_t)buf->dma_addr, &buf->dma_attrs);

	buf->dma_addr = (dma_addr_t)NULL;
}

struct owl_drm_gem_buf *owl_drm_init_buf(struct drm_device *dev,
		unsigned int size)
{
	struct owl_drm_gem_buf *buffer;

	DRM_DEBUG_KMS("desired size = 0x%x\n", size);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DRM_ERROR("failed to allocate owl_drm_gem_buf.\n");
		return NULL;
	}

	buffer->size = size;
	return buffer;
}

void owl_drm_fini_buf(struct drm_device *dev,
		struct owl_drm_gem_buf *buffer)
{
	DRM_DEBUG_KMS("%s.\n", __func__);

	if (!buffer) {
		DRM_DEBUG_KMS("buffer is null.\n");
		return;
	}

	kfree(buffer);
	buffer = NULL;
}

int owl_drm_alloc_buf(struct drm_device *dev,
		struct owl_drm_gem_buf *buf, unsigned int flags)
{

	/*
	 * allocate memory region and set the memory information
	 * to vaddr and dma_addr of a buffer object.
	 */
	if (lowlevel_buffer_allocate(dev, flags, buf) < 0)
		return -ENOMEM;

	return 0;
}

void owl_drm_free_buf(struct drm_device *dev,
		unsigned int flags, struct owl_drm_gem_buf *buffer)
{

	lowlevel_buffer_deallocate(dev, flags, buffer);
}
