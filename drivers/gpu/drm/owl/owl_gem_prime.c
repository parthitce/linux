/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#include "owl_gem.h"

struct owl_prime_attachment {
	struct sg_table *sgt;
	enum dma_data_direction dir;
};

static int owl_gem_map_attach(struct dma_buf *dma_buf,
			      struct device *target_dev,
			      struct dma_buf_attachment *attach)
{
	struct owl_prime_attachment *prime_attach;
	struct drm_gem_object *obj = dma_buf->priv;

	prime_attach = kzalloc(sizeof(*prime_attach), GFP_KERNEL);
	if (!prime_attach)
		return -ENOMEM;

	prime_attach->dir = DMA_NONE;
	attach->priv = prime_attach;

	if (!obj->import_attach)
		owl_gem_get_pages(obj);

	return 0;
}

static void owl_gem_map_detach(struct dma_buf *dma_buf,
			       struct dma_buf_attachment *attach)
{
	struct owl_prime_attachment *prime_attach = attach->priv;
	struct drm_gem_object *obj = dma_buf->priv;
	struct sg_table *sgt;

	if (!obj->import_attach)
		owl_gem_put_pages(obj);

	if (!prime_attach)
		return;

	sgt = prime_attach->sgt;
	if (sgt) {
		if (prime_attach->dir != DMA_NONE)
			dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents,
					prime_attach->dir);
		sg_free_table(sgt);
	}

	kfree(sgt);
	kfree(prime_attach);
	attach->priv = NULL;
}

static struct sg_table *owl_gem_map_dma_buf(struct dma_buf_attachment *attach,
					    enum dma_data_direction dir)
{
	struct owl_prime_attachment *prime_attach = attach->priv;
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	struct sg_table *sgt;
	int npages = obj->size >> PAGE_SHIFT;

	if (WARN_ON(dir == DMA_NONE || !prime_attach))
		return ERR_PTR(-EINVAL);

	/* return the cached mapping when possible */
	if (prime_attach->dir == dir)
		return prime_attach->sgt;

	/*
	 * two mappings with different directions for the same attachment are
	 * not allowed
	 */
	if (WARN_ON(prime_attach->dir != DMA_NONE))
		return ERR_PTR(-EBUSY);

	if (WARN_ON(!owl_obj->pages))  /* should have already pinned! */
		return NULL;

	sgt = drm_prime_pages_to_sg(owl_obj->pages, npages);

	if (!IS_ERR(sgt)) {
		if (!dma_map_sg(attach->dev, sgt->sgl, sgt->nents, dir)) {
			sg_free_table(sgt);
			kfree(sgt);
			sgt = ERR_PTR(-ENOMEM);
		} else {
			prime_attach->sgt = sgt;
			prime_attach->dir = dir;
		}
	}

	return sgt;
}

static void owl_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *sgt,
				  enum dma_data_direction dir)
{
	/* nothing to be done here */
}

static void owl_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;

	/* drop the reference on the export fd holds */
	drm_gem_object_unreference_unlocked(obj);
}

static void *owm_gem_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;
	return owl_gem_get_vaddr(obj);
}

static void owl_gem_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct drm_gem_object *obj = dma_buf->priv;
	owl_gem_put_vaddr(obj);
}

static void *owl_gem_dmabuf_kmap_atomic(struct dma_buf *dma_buf,
					unsigned long page_num)
{
	return NULL;
}

static void owl_gem_dmabuf_kunmap_atomic(struct dma_buf *dma_buf,
					 unsigned long page_num, void *addr)
{
}

static void *owl_gem_dmabuf_kmap(struct dma_buf *dma_buf,
				 unsigned long page_num)
{
	return NULL;
}

static void owl_gem_dmabuf_kunmap(struct dma_buf *dma_buf,
				  unsigned long page_num, void *addr)
{
}

static int owl_gem_dmabuf_mmap(struct dma_buf *buffer,
		struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = buffer->priv;
	int ret;

	ret = _drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return owl_gem_mmap_obj(obj, vma);
}

static struct dma_buf_ops owl_gem_prime_dmabuf_ops = {
		.attach = owl_gem_map_attach,
		.detach = owl_gem_map_detach,
		.map_dma_buf = owl_gem_map_dma_buf,
		.unmap_dma_buf = owl_gem_unmap_dma_buf,
		.release = owl_gem_dmabuf_release,
		.kmap = owl_gem_dmabuf_kmap,
		.kunmap = owl_gem_dmabuf_kunmap,
		.kmap_atomic = owl_gem_dmabuf_kmap_atomic,
		.kunmap_atomic = owl_gem_dmabuf_kunmap_atomic,
		.mmap = owl_gem_dmabuf_mmap,
		.vmap = owm_gem_dmabuf_vmap,
		.vunmap = owl_gem_dmabuf_vunmap,
};

struct dma_buf *owl_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags)
{
	return dma_buf_export(obj, &owl_gem_prime_dmabuf_ops, obj->size, flags);
}

/* -----------------------------------------------------------------------------
 * DMABUF Import
 */

struct drm_gem_object *owl_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;
	struct sg_table *sgt;
	int ret;

	if (dma_buf->ops == &owl_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = owl_gem_import(dev, dma_buf, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
