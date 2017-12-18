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

#include <linux/dma-buf.h>

struct owl_drm_dmabuf_attachment {
	struct sg_table sgt;
	enum dma_data_direction dir;
	bool is_mapped;
};

static int owl_gem_attach_dma_buf(struct dma_buf *dmabuf,
					struct device *dev,
					struct dma_buf_attachment *attach)
{
	struct owl_drm_dmabuf_attachment *owl_attach;

	owl_attach = kzalloc(sizeof(*owl_attach), GFP_KERNEL);
	if (!owl_attach)
		return -ENOMEM;

	owl_attach->dir = DMA_NONE;
	attach->priv = owl_attach;

	return 0;
}

static void owl_gem_detach_dma_buf(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attach)
{
	struct owl_drm_dmabuf_attachment *owl_attach = attach->priv;
	struct sg_table *sgt;

	if (!owl_attach)
		return;

	sgt = &owl_attach->sgt;

	if (owl_attach->dir != DMA_NONE)
		dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents,
				owl_attach->dir);

	sg_free_table(sgt);
	kfree(owl_attach);
	attach->priv = NULL;
}

static struct sg_table *
		owl_gem_map_dma_buf(struct dma_buf_attachment *attach,
					enum dma_data_direction dir)
{
	struct owl_drm_dmabuf_attachment *owl_attach = attach->priv;
	struct owl_drm_gem_obj *gem_obj = attach->dmabuf->priv;
	struct drm_device *dev = gem_obj->base.dev;
	struct owl_drm_gem_buf *buf;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt = NULL;
	unsigned int i;
	int nents, ret;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/* just return current sgt if already requested. */
	if (owl_attach->dir == dir && owl_attach->is_mapped)
		return &owl_attach->sgt;

	buf = gem_obj->buffer;
	if (!buf) {
		DRM_ERROR("buffer is null.\n");
		return ERR_PTR(-ENOMEM);
	}

	sgt = &owl_attach->sgt;

	ret = sg_alloc_table(sgt, buf->sgt->orig_nents, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("failed to alloc sgt.\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&dev->struct_mutex);

	rd = buf->sgt->sgl;
	wr = sgt->sgl;
	for (i = 0; i < sgt->orig_nents; ++i) {
		sg_set_page(wr, sg_page(rd), rd->length, rd->offset);
		rd = sg_next(rd);
		wr = sg_next(wr);
	}

	if (dir != DMA_NONE) {
		nents = dma_map_sg(attach->dev, sgt->sgl, sgt->orig_nents, dir);
		if (!nents) {
			DRM_ERROR("failed to map sgl with iommu.\n");
			sg_free_table(sgt);
			sgt = ERR_PTR(-EIO);
			goto err_unlock;
		}
	}

	owl_attach->is_mapped = true;
	owl_attach->dir = dir;
	attach->priv = owl_attach;

	DRM_DEBUG_PRIME("buffer size = 0x%lx\n", buf->size);

err_unlock:
	mutex_unlock(&dev->struct_mutex);
	return sgt;
}

static void owl_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
						struct sg_table *sgt,
						enum dma_data_direction dir)
{
	/* Nothing to do. */
}

static void owl_dmabuf_release(struct dma_buf *dmabuf)
{
	struct owl_drm_gem_obj *owl_gem_obj = dmabuf->priv;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/*
	 * owl_dmabuf_release() call means that file object's
	 * f_count is 0 and it calls drm_gem_object_handle_unreference()
	 * to drop the references that these values had been increased
	 * at drm_prime_handle_to_fd()
	 */
	if (owl_gem_obj->base.export_dma_buf == dmabuf) {
		owl_gem_obj->base.export_dma_buf = NULL;

		/*
		 * drop this gem object refcount to release allocated buffer
		 * and resources.
		 */
		drm_gem_object_unreference_unlocked(&owl_gem_obj->base);
	}
}

static void *owl_gem_dmabuf_kmap_atomic(struct dma_buf *dma_buf,
						unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void owl_gem_dmabuf_kunmap_atomic(struct dma_buf *dma_buf,
						unsigned long page_num,
						void *addr)
{
	/* TODO */
}

static void *owl_gem_dmabuf_kmap(struct dma_buf *dma_buf,
					unsigned long page_num)
{
	/* TODO */

	return NULL;
}

static void owl_gem_dmabuf_kunmap(struct dma_buf *dma_buf,
					unsigned long page_num, void *addr)
{
	/* TODO */
}

void update_vm_cache_attr(struct owl_drm_gem_obj *obj,
					struct vm_area_struct *vma);

static int owl_gem_dmabuf_mmap(struct dma_buf *dma_buf,
	struct vm_area_struct *vma)
{
	struct owl_drm_gem_obj *owl_gem_obj = dma_buf->priv;
	struct drm_gem_object *obj = &owl_gem_obj->base;
	struct drm_device *drm_dev = obj->dev;
	struct owl_drm_gem_buf *buffer = owl_gem_obj->buffer;
	int ret;

	/* Check for valid size. */
	if (buffer->size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = drm_dev->driver->gem_vm_ops;
	vma->vm_private_data = obj;

	update_vm_cache_attr(owl_gem_obj, vma);

	ret = dma_mmap_attrs(drm_dev->dev, vma, buffer->pages,
				buffer->dma_addr, buffer->size,
				&buffer->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	/*
	 * take a reference to this mapping of the object. And this reference
	 * is unreferenced by the corresponding vm_close call.
	 */
	drm_gem_object_reference(obj);

	drm_vm_open_locked(drm_dev, vma);

	return 0;
}

static struct dma_buf_ops owl_dmabuf_ops = {
	.attach			= owl_gem_attach_dma_buf,
	.detach			= owl_gem_detach_dma_buf,
	.map_dma_buf		= owl_gem_map_dma_buf,
	.unmap_dma_buf		= owl_gem_unmap_dma_buf,
	.kmap			= owl_gem_dmabuf_kmap,
	.kmap_atomic		= owl_gem_dmabuf_kmap_atomic,
	.kunmap			= owl_gem_dmabuf_kunmap,
	.kunmap_atomic		= owl_gem_dmabuf_kunmap_atomic,
	.mmap			= owl_gem_dmabuf_mmap,
	.release		= owl_dmabuf_release,
};

struct dma_buf *owl_dmabuf_prime_export(struct drm_device *drm_dev,
				struct drm_gem_object *obj, int flags)
{
	struct owl_drm_gem_obj *owl_gem_obj = to_owl_gem_obj(obj);

	return dma_buf_export(owl_gem_obj, &owl_dmabuf_ops,
				owl_gem_obj->base.size, flags);
}

struct drm_gem_object *owl_dmabuf_prime_import(struct drm_device *drm_dev,
				struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	struct owl_drm_gem_obj *owl_gem_obj;
	struct owl_drm_gem_buf *buffer;
	int ret;

	DRM_DEBUG_PRIME("%s\n", __FILE__);

	/* is this one of own objects? */
	if (dma_buf->ops == &owl_dmabuf_ops) {
		struct drm_gem_object *obj;

		owl_gem_obj = dma_buf->priv;
		obj = &owl_gem_obj->base;

		/* is it from our device? */
		if (obj->dev == drm_dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(dma_buf, drm_dev->dev);
	if (IS_ERR(attach))
		return ERR_PTR(-EINVAL);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_buf_detach;
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DRM_ERROR("failed to allocate owl_drm_gem_buf.\n");
		ret = -ENOMEM;
		goto err_unmap_attach;
	}

	owl_gem_obj = owl_drm_gem_init(drm_dev, dma_buf->size);
	if (!owl_gem_obj) {
		ret = -ENOMEM;
		goto err_free_buffer;
	}

	sgl = sgt->sgl;

	buffer->size = dma_buf->size;
	buffer->dma_addr = sg_dma_address(sgl);

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		owl_gem_obj->flags |= OWL_BO_CONTIG;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		owl_gem_obj->flags |= OWL_BO_NONCONTIG;
	}

	owl_gem_obj->buffer = buffer;
	buffer->sgt = sgt;
	owl_gem_obj->base.import_attach = attach;

	DRM_DEBUG_PRIME("dma_addr = 0x%x, size = 0x%lx\n", buffer->dma_addr,
								buffer->size);

	return &owl_gem_obj->base;

err_free_buffer:
	kfree(buffer);
	buffer = NULL;
err_unmap_attach:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
err_buf_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

MODULE_DESCRIPTION("Owl SoC DRM DMABUF Module");
MODULE_LICENSE("GPL");
