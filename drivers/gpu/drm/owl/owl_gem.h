/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _OWL_GEM_H_
#define _OWL_GEM_H_

#include <drm/drmP.h>
#include <drm/owl_drm.h>
#include <linux/dma-buf.h>
#include <linux/seq_file.h>

#define to_owl_bo(x) container_of(x, struct owl_gem_object, base)

struct owl_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/*
	 * pin_addr contains the buffer address for display engine
	 *
	 * 1) if display engine has mmu, pin_addr is the mapped address by the mmu
	 * 2) if display engine doesn't have mmu, and the dma_addr is valid, pin_addr
	 *    is equal to dma_addr
	 * 3) otherwise, pin_addr is invalid.
	 */
	dma_addr_t pin_addr;
	uint32_t pin_count;

	/* Virtual address, if mapped. */
	void *vaddr;
	uint32_t vmap_count;

	/* Backing sgt and backing pages array, if allocated. */
	struct sg_table *sgt;
	struct page **pages;

	/*
	 * The buffer DMA address. It is only valid for
	 * 1) buffers allocated through the DMA mapping API (with the
	 *   OWL_BO_MEM_DMA_API flag set)
	 *
	 * 2) buffers imported from dmabuf (with the OWL_BO_MEM_DMABUF flag set)
	 *   if they are physically contiguous (when sgt->orig_nents == 1)
	 */
	dma_addr_t dma_addr;
	/* Used by dma_alloc_attrs and dma_free_attrs. */
	struct dma_attrs dma_attrs;

	/* gem obj list */
	struct list_head mm_list;

	/* Protects resources associated with bo */
	struct mutex lock;
};

int owl_gem_init(struct drm_device *dev);
void owl_gem_deinit(struct drm_device *dev);

int owl_gem_init_object(struct drm_gem_object *obj);
void owl_gem_free_object(struct drm_gem_object *obj);
struct drm_gem_object *owl_gem_new(struct drm_device *dev,
		uint64_t size, uint32_t flags);
int owl_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint64_t size, uint32_t flags, uint32_t *handle);
struct drm_gem_object *owl_gem_import(struct drm_device *dev,
		struct dma_buf *dmabuf, struct sg_table *sgt);

uint32_t owl_gem_flags(struct drm_gem_object *obj);

void *owl_gem_get_vaddr(struct drm_gem_object *obj);
void owl_gem_put_vaddr(struct drm_gem_object *obj);

int owl_gem_pin(struct drm_gem_object *obj, dma_addr_t *paddr);
void owl_gem_unpin(struct drm_gem_object *obj);

int owl_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int owl_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		uint32_t handle);
int owl_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);

uint64_t owl_gem_mmap_offset(struct drm_gem_object *obj);
int owl_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int owl_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma);
int owl_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

struct page **owl_gem_get_pages(struct drm_gem_object *obj);
void owl_gem_put_pages(struct drm_gem_object *obj);

struct dma_buf *owl_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags);
struct drm_gem_object *owl_gem_prime_import(struct drm_device *dev,
		struct dma_buf *buffer);

#ifdef CONFIG_DEBUG_FS
void owl_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void owl_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

/* These are helper functions defined in owl_gem_helper.c, which should be
 * took place by official drm symbols in higher version linux.
 */
struct page **_drm_gem_get_pages(struct drm_gem_object *obj, gfp_t gfpmask);
void _drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed);
int _drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size);
int _drm_gem_mmap_obj(struct drm_gem_object *obj, unsigned long obj_size,
		     struct vm_area_struct *vma);
#endif
