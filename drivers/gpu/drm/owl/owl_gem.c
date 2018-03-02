/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/shmem_fs.h>

#include "owl_drv.h"
#include "owl_gem.h"

/*
 * GEM buffer object implementation.
 */

/* note: we use upper 8 bits of flags for driver-internal flags: */
#define OWL_BO_MEM_MASK     0xff000000	/* memory allocation type mask */
#define OWL_BO_MEM_DMA_API  0x01000000	/* memory allocated with the dma_alloc_* API */
#define OWL_BO_MEM_SHMEM    0x02000000	/* memory allocated through shmem backing */
#define OWL_BO_MEM_DMABUF   0x04000000	/* memory imported from a dmabuf */

/* -----------------------------------------------------------------------------
 * Helpers
 */
static bool is_contiguous(struct owl_gem_object *owl_obj)
{
	if (owl_obj->flags & OWL_BO_MEM_DMA_API)
		return true;

	if ((owl_obj->flags & OWL_BO_MEM_DMABUF) && owl_obj->sgt->nents == 1)
		return true;

	return false;
}

static bool use_pages(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	return !!(owl_obj->flags & OWL_BO_MEM_SHMEM);
}

static bool use_dmalloc(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	return !!(owl_obj->flags & OWL_BO_MEM_DMA_API);
}

/* -----------------------------------------------------------------------------
 * Page Management
 */

/* acquire pages when needed (for example, for DMA where physically
 * contiguous buffer is not required
 */
static struct page **get_pages_contig(struct drm_gem_object *obj, int npages)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	struct page **pages;
	phys_addr_t paddr = dma_to_phys(obj->dev->dev, owl_obj->dma_addr);
	int i;

	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < npages; i++) {
		pages[i] = phys_to_page(paddr);
		paddr += PAGE_SIZE;
	}

	return pages;
}

static struct page **get_pages(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	if (!owl_obj->pages) {
		struct drm_device *dev = obj->dev;
		struct sg_table *sgt;
		struct page **pages;
		int npages = obj->size >> PAGE_SHIFT;
		void *ret = ERR_PTR(-ENOMEM);

		if (use_pages(obj))
			pages = _drm_gem_get_pages(obj, GFP_KERNEL);
		else
			pages = get_pages_contig(obj, npages);

		if (IS_ERR(pages)) {
			DEV_ERR(dev->dev, "could not get pages: %ld", PTR_ERR(pages));
			return pages;
		}

		sgt = drm_prime_pages_to_sg(pages, npages);
		if (IS_ERR(sgt)) {
			DEV_ERR(dev->dev, "failed to allocate sgt");
			ret = ERR_CAST(sgt);
			goto fail_free_pages;
		}

		/* for non-cached buffers, ensure the new pages are clean because
		* DSS, GPU, etc. are not cache coherent:
		*/
		/* For non-cached buffers, ensure the new pages are clean
		 * because display controller, GPU, etc. are not coherent:
		 */
		if (owl_obj->flags & (OWL_BO_WC|OWL_BO_UNCACHED))
			dma_map_sg(dev->dev, sgt->sgl, sgt->nents, DMA_BIDIRECTIONAL);

		owl_obj->sgt = sgt;
		owl_obj->pages = pages;
		return pages;
fail_free_pages:
		if (owl_obj->flags & OWL_BO_MEM_SHMEM)
			_drm_gem_put_pages(obj, pages, true, false);
		else
			kfree(pages);
		return ret;
	}

	return owl_obj->pages;
}

static void put_pages(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	if (owl_obj->pages) {
		/* For non-cached buffers, ensure the new pages are clean
		 * because display controller, GPU, etc. are not coherent:
		 */
		if (owl_obj->flags & (OWL_BO_WC|OWL_BO_UNCACHED))
			dma_unmap_sg(obj->dev->dev, owl_obj->sgt->sgl,
					owl_obj->sgt->nents, DMA_BIDIRECTIONAL);
		sg_free_table(owl_obj->sgt);
		kfree(owl_obj->sgt);

		if (use_pages(obj))
			_drm_gem_put_pages(obj, owl_obj->pages, true, false);
		else
			kfree(owl_obj->pages);

		owl_obj->pages = NULL;
	}
}

struct page **owl_gem_get_pages(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	struct page **pages;

	mutex_lock(&owl_obj->lock);
	pages = get_pages(obj);
	mutex_unlock(&owl_obj->lock);
	return pages;
}

void owl_gem_put_pages(struct drm_gem_object *obj)
{
	/* when we start tracking the pin count, then do something here */
}

uint32_t owl_gem_flags(struct drm_gem_object *obj)
{
	return to_owl_bo(obj)->flags & ~OWL_BO_MEM_MASK;
}

int owl_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	if (owl_obj->flags & OWL_BO_WC) {
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else if (owl_obj->flags & OWL_BO_UNCACHED) {
		vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	} else {
		/*
		 * We do have some private objects, at least for scanout buffers
		 * on hardware without DMM/TILER.  But these are allocated write-
		 * combine
		 */
		if (WARN_ON(!obj->filp))
			return -EINVAL;

		/*
		 * Shunt off cached objs to shmem file so they have their own
		 * address_space (so unmap_mapping_range does what we want,
		 * in particular in the case of mmap'd dmabufs)
		 */
		fput(vma->vm_file);
		vma->vm_pgoff = 0;
		vma->vm_file  = get_file(obj->filp);

		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	}

	return 0;
}

/** We override mainly to fix up some of the vm mapping flags.. */
int owl_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		DBG("mmap failed: %d", ret);
		return ret;
	}

	return owl_gem_mmap_obj(vma->vm_private_data, vma);
}

/* -----------------------------------------------------------------------------
 * Fault Handling
 */

/**
 * owl_gem_fault		-	pagefault handler for GEM objects
 * @vma: the VMA of the GEM object
 * @vmf: fault detail
 *
 * Invoked when a fault occurs on an mmap of a GEM managed area. GEM
 * does most of the work for us including the actual map/unmap calls
 * but we need to do the actual page work.
 *
 * The VMA was set up by GEM. In doing so it also ensured that the
 * vma->vm_private_data points to the GEM object that is backing this
 * mapping.
 */
int owl_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	struct page **pages;
	unsigned long pfn;
	pgoff_t pgoff;
	int ret;

	/*
	 * vm_ops.open/drm_gem_mmap_obj and close get and put
	 * a reference on obj. So, we dont need to hold one here.
	 */
	ret = mutex_lock_interruptible(&owl_obj->lock);
	if (ret)
		goto out;

	/* make sure we have pages attached now */
	pages = get_pages(obj);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out_unlock;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address - vma->vm_start) >> PAGE_SHIFT;

	pfn = page_to_pfn(pages[pgoff]);

	VERB("Inserting %p pfn %lx, pa %lx", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

out_unlock:
	mutex_unlock(&owl_obj->lock);
out:
	switch (ret) {
	case -EAGAIN:
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

static uint64_t mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	WARN_ON(!mutex_is_locked(&owl_obj->lock));

	if (!obj->map_list.map) {
		/* Make it mmapable */
		int ret = _drm_gem_create_mmap_offset_size(obj, obj->size);
		if (ret) {
			DEV_ERR(dev->dev, "could not allocate mmap offset");
			return 0;
		}
	}

	return (uint64_t)obj->map_list.hash.key << PAGE_SHIFT;
}

uint64_t owl_gem_mmap_offset(struct drm_gem_object *obj)
{
	uint64_t offset;
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	mutex_lock(&owl_obj->lock);
	offset = mmap_offset(obj);
	mutex_unlock(&owl_obj->lock);
	return offset;
}

/* -----------------------------------------------------------------------------
 * Dumb Buffers
 */

/**
 * owl_gem_dumb_create	-	create a dumb buffer
 * @drm_file: our client file
 * @dev: our device
 * @args: the requested arguments copied from userspace
 *
 * Allocate a buffer suitable for use for a frame buffer of the
 * form described by user space. Give userspace a handle by which
 * to reference it.
 */
int owl_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args)
{
	if (args->size == 0) {
		uint32_t min_pitch = args->pitch ? args->pitch :
				args->width * ((args->bpp + 7) / 8);

		/* align to 64 bytes since Mali requires it. */
		args->pitch = ALIGN(min_pitch, 64);
		args->size = args->pitch * args->height;
	}

	args->size  = PAGE_ALIGN(args->size);

	return owl_gem_new_handle(dev, file, args->size,
			OWL_BO_SCANOUT | OWL_BO_WC, &args->handle);
}

/**
 * owl_gem_dumb_destroy	-	destroy a dumb buffer
 * @file: client file
 * @dev: our DRM device
 * @handle: the object handle
 *
 * Destroy a handle that was created via owl_gem_dumb_create.
 */
int owl_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		uint32_t handle)
{
	/* No special work needed, drop the reference and see what falls out */
	return drm_gem_handle_delete(file, handle);
}

/**
 * owl_gem_dumb_map	-	buffer mapping for dumb interface
 * @file: our drm client file
 * @dev: drm device
 * @handle: GEM handle to the object (from dumb_create)
 *
 * Do the necessary setup to allow the mapping of the frame buffer
 * into user memory. We don't have to do much here at the moment.
 */
int owl_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto fail;
	}

	*offset = owl_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

fail:
	return ret;
}

/* -----------------------------------------------------------------------------
 * Memory Management & DMA Sync
 */

int owl_gem_pin(struct drm_gem_object *obj, dma_addr_t *paddr)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	int ret = 0;

	mutex_lock(&owl_obj->lock);

	if (owl_obj->pin_addr == DMA_ERROR_CODE) {
		if (owl_de_mmu_is_present()) {
			struct page **pages = get_pages(obj);
			if (IS_ERR_OR_NULL(pages)) {
				ret = -ENOMEM;
				goto fail;
			}

			/* map to display mmu */
			ret = owl_de_mmu_map_sg(owl_obj->sgt, &owl_obj->pin_addr);
			if (ret)
				goto fail;
		} else if (is_contiguous(owl_obj)) {
			owl_obj->pin_addr = owl_obj->dma_addr;
		}  else {
			ret = -EINVAL;
			goto fail;
		}
	}

	*paddr = owl_obj->pin_addr;
	owl_obj->pin_count++;

fail:
	mutex_unlock(&owl_obj->lock);
	return ret;
}

void owl_gem_unpin(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	mutex_lock(&owl_obj->lock);
	WARN_ON(owl_obj->pin_count < 1);
	owl_obj->pin_count--;
	mutex_unlock(&owl_obj->lock);
}

void *owl_gem_get_vaddr(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	int ret = 0;

	mutex_lock(&owl_obj->lock);

	owl_obj->vmap_count++;

	if (!owl_obj->vaddr) {
		if (obj->import_attach) {
			owl_obj->vaddr = dma_buf_vmap(obj->import_attach->dmabuf);
		} else {
			struct page **pages = get_pages(obj);
			if (IS_ERR(pages)) {
				ret = PTR_ERR(pages);
				goto fail;
			}

			owl_obj->vaddr = vmap(pages, obj->size >> PAGE_SHIFT,
					VM_MAP, pgprot_writecombine(PAGE_KERNEL));
		}

		if (owl_obj->vaddr == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	mutex_unlock(&owl_obj->lock);
	return owl_obj->vaddr;

fail:
	owl_obj->vmap_count--;
	mutex_unlock(&owl_obj->lock);
	return ERR_PTR(ret);
}

void owl_gem_put_vaddr(struct drm_gem_object *obj)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	mutex_lock(&owl_obj->lock);
	WARN_ON(owl_obj->vmap_count < 1);
	owl_obj->vmap_count--;
	mutex_unlock(&owl_obj->lock);
}

/* -----------------------------------------------------------------------------
 * DebugFS
 */

#ifdef CONFIG_DEBUG_FS
void owl_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct owl_gem_object *owl_obj = to_owl_bo(obj);
	uint64_t off = 0;

	mutex_lock(&owl_obj->lock);

	if (obj->map_list.map)
		off = (uint64_t)obj->map_list.hash.key;

	seq_printf(m, "%08x: %2d (%2d) %08llx %pad (%2d) %p",
			owl_obj->flags, obj->name, obj->refcount.refcount.counter,
			off, &owl_obj->pin_addr, owl_obj->pin_count, owl_obj->vaddr);

	seq_printf(m, " %zu\n", obj->size);

	mutex_unlock(&owl_obj->lock);
}

void owl_gem_describe_objects(struct list_head *list, struct seq_file *m)
{
	struct owl_gem_object *owl_obj;
	int count = 0;
	size_t size = 0;

	list_for_each_entry(owl_obj, list, mm_list) {
		struct drm_gem_object *obj = &owl_obj->base;
		seq_printf(m, "   ");
		owl_gem_describe(obj, m);
		count++;
		size += obj->size;
	}

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}
#endif

/* -----------------------------------------------------------------------------
 * Constructor & Destructor
 */

int owl_gem_init_object(struct drm_gem_object *obj)
{
	return -EINVAL;          /* unused */
}

/* don't call directly.. called from GEM core when it is time to actually
 * free the object..
 */
void owl_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_gem_object *owl_obj = to_owl_bo(obj);

	spin_lock(&priv->list_lock);
	list_del(&owl_obj->mm_list);
	spin_unlock(&priv->list_lock);

	mutex_lock(&owl_obj->lock);

	if (obj->map_list.map)
		drm_gem_free_mmap_offset(obj);

	/* ummap from display mmu */
	if (owl_de_mmu_is_present() && owl_obj->pin_addr != DMA_ERROR_CODE)
		owl_de_mmu_unmap_sg(owl_obj->pin_addr);

	if (obj->import_attach) {
		if (owl_obj->vaddr)
			dma_buf_vunmap(obj->import_attach->dmabuf, owl_obj->vaddr);

		/* Don't drop the pages for imported dmabuf, as they are not
		 * ours, just free the array we allocated:
		 */
		if (owl_obj->pages)
			kfree(owl_obj->pages);

		drm_prime_gem_destroy(obj, owl_obj->sgt);
	} else {
		if (owl_obj->vaddr) {
			if (use_dmalloc(obj))
				dma_free_attrs(dev->dev, obj->size, owl_obj->vaddr,
						owl_obj->dma_addr, &owl_obj->dma_attrs);
			else
				vunmap(owl_obj->vaddr);
		}
		put_pages(obj);
	}

	drm_gem_object_release(obj);

	mutex_unlock(&owl_obj->lock);
	kfree(owl_obj);
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int owl_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint64_t size, uint32_t flags, uint32_t *handle)
{
	struct drm_gem_object *obj;
	int ret;

	obj = owl_gem_new(dev, size, flags);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, obj, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int owl_gem_new_impl(struct drm_device *dev,
		uint64_t size, uint32_t flags,
		struct drm_gem_object **obj)
{
	struct owl_drm_private *priv = dev->dev_private;
	struct owl_gem_object *owl_obj;
	int ret = 0;

	if (flags & OWL_BO_MEM_DMABUF) {
		/* nothing to do */
	} else if ((flags & OWL_BO_CONTIG) ||
					((flags & OWL_BO_SCANOUT) && !owl_de_mmu_is_present())) {
		flags |= OWL_BO_MEM_DMA_API;
	} else {
		flags |= OWL_BO_MEM_SHMEM;
	}

	owl_obj = kzalloc(sizeof(*owl_obj), GFP_KERNEL);
	if (!owl_obj)
		return -ENOMEM;

	/* Initialize the GEM object. */
	if (flags & OWL_BO_MEM_SHMEM)
		ret = drm_gem_object_init(dev, &owl_obj->base, size);
	else
		ret = drm_gem_private_object_init(dev, &owl_obj->base, size);

	if (ret)
		goto fail_free;

	mutex_init(&owl_obj->lock);

	owl_obj->flags = flags;
	owl_obj->pin_addr = DMA_ERROR_CODE;

	spin_lock(&priv->list_lock);
	list_add_tail(&owl_obj->mm_list, &priv->obj_list);
	spin_unlock(&priv->list_lock);

	*obj = &owl_obj->base;
	return 0;
fail_free:
	kfree(owl_obj);
	return ret;
}

/* GEM buffer object constructor */
struct drm_gem_object *owl_gem_new(struct drm_device *dev,
		uint64_t size, uint32_t flags)
{
	struct owl_gem_object *owl_obj;
	struct drm_gem_object *obj = NULL;
	int ret;

	/* Disallow zero sized objects as they make the underlying
	 * infrastructure grumpy
	 */
	if (size == 0)
		return ERR_PTR(-EINVAL);

	size = PAGE_ALIGN(size);

	ret = owl_gem_new_impl(dev, size, flags, &obj);
	if (ret)
		return ERR_PTR(ret);

	owl_obj = to_owl_bo(obj);

	/* Allocate memory if needed. */
	if (use_dmalloc(obj)) {
		init_dma_attrs(&owl_obj->dma_attrs);
		if (flags & OWL_BO_CONTIG)
			dma_set_attr(DMA_ATTR_FORCE_CONTIGUOUS, &owl_obj->dma_attrs);
		if ((flags & OWL_BO_WC) || (flags & OWL_BO_UNCACHED))
			dma_set_attr(DMA_ATTR_WRITE_COMBINE, &owl_obj->dma_attrs);
		else
			dma_set_attr(DMA_ATTR_NON_CONSISTENT, &owl_obj->dma_attrs);

		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &owl_obj->dma_attrs);

		owl_obj->vaddr = dma_alloc_attrs(dev->dev, size,
							&owl_obj->dma_addr, GFP_KERNEL,
							&owl_obj->dma_attrs);
		if (!owl_obj->vaddr) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	return obj;
fail:
	drm_gem_object_unreference_unlocked(obj);
	return ERR_PTR(ret);
}

struct drm_gem_object *owl_gem_import(struct drm_device *dev,
		struct dma_buf *dmabuf, struct sg_table *sgt)
{
	struct owl_gem_object *owl_obj;
	struct drm_gem_object *obj;
	uint64_t size;
	struct page **pages;
	int ret, npages;

	/* Without a DMM only physically contiguous buffers can be supported. */
	if (sgt->orig_nents != 1 && !owl_de_mmu_is_present())
		return ERR_PTR(-EINVAL);

	size = dmabuf->size;
	npages = DIV_ROUND_UP(size, PAGE_SIZE);

	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	ret = drm_prime_sg_to_page_addr_arrays(sgt, pages, NULL, npages);
	if (ret) {
		DEV_ERR(dev->dev, "drm_prime_sg_to_page_addr_arrays failed");
		goto fail;
	}

	ret = owl_gem_new_impl(dev, size, OWL_BO_MEM_DMABUF | OWL_BO_WC, &obj);
	if (ret)
		goto fail;

	owl_obj = to_owl_bo(obj);

	mutex_lock(&owl_obj->lock);
	owl_obj->sgt = sgt;
	owl_obj->pages = pages;
	if (sgt->orig_nents == 1)
		owl_obj->dma_addr = sg_dma_address(sgt->sgl);
	mutex_unlock(&owl_obj->lock);
	return obj;
fail:
	kfree(pages);
	return ERR_PTR(ret);
}

/* -----------------------------------------------------------------------------
 * Init & Cleanup
 */

int owl_gem_init(struct drm_device *dev)
{
	struct owl_drm_private *priv = dev->dev_private;

	spin_lock_init(&priv->list_lock);
	INIT_LIST_HEAD(&priv->obj_list);

	return 0;
}

void owl_gem_deinit(struct drm_device *dev)
{
}
