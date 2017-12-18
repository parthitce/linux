/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>

#include <linux/shmem_fs.h>
#include <drm/owl_drm.h>

#include "owl_drm_drv.h"
#include "owl_drm_gem.h"
#include "owl_drm_buf.h"

static unsigned int convert_to_vm_err_msg(int msg)
{
	unsigned int out_msg;

	switch (msg) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		out_msg = VM_FAULT_NOPAGE;
		break;

	case -ENOMEM:
		out_msg = VM_FAULT_OOM;
		break;

	default:
		out_msg = VM_FAULT_SIGBUS;
		break;
	}

	return out_msg;
}

static int check_gem_flags(unsigned int flags)
{
	if (flags & ~(OWL_BO_MASK)) {
		DRM_ERROR("invalid flags.\n");
		return -EINVAL;
	}

	return 0;
}

void update_vm_cache_attr(struct owl_drm_gem_obj *obj,
					struct vm_area_struct *vma)
{
	DRM_DEBUG_KMS("flags = 0x%x\n", obj->flags);

	/* non-cachable as default. */
	if (obj->flags & OWL_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	else if (obj->flags & OWL_BO_WC)
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	else
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));
}

static unsigned long roundup_gem_size(unsigned long size, unsigned int flags)
{
	/* TODO */

	return roundup(size, PAGE_SIZE);
}

static int owl_drm_gem_map_buf(struct drm_gem_object *obj,
					struct vm_area_struct *vma,
					unsigned long f_vaddr,
					pgoff_t page_offset)
{
	struct owl_drm_gem_obj *owl_gem_obj = to_owl_gem_obj(obj);
	struct owl_drm_gem_buf *buf = owl_gem_obj->buffer;
	struct scatterlist *sgl;
	unsigned long pfn;
	int i;

	if (!buf->sgt)
		return -EINTR;

	if (page_offset >= (buf->size >> PAGE_SHIFT)) {
		DRM_ERROR("invalid page offset\n");
		return -EINVAL;
	}

	sgl = buf->sgt->sgl;
	for_each_sg(buf->sgt->sgl, sgl, buf->sgt->nents, i) {
		if (page_offset < (sgl->length >> PAGE_SHIFT))
			break;
		page_offset -=	(sgl->length >> PAGE_SHIFT);
	}

	pfn = __phys_to_pfn(sg_phys(sgl)) + page_offset;

	return vm_insert_mixed(vma, f_vaddr, pfn);
}

static int owl_drm_gem_handle_create(struct drm_gem_object *obj,
					struct drm_file *file_priv,
					unsigned int *handle)
{
	int ret;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		return ret;

	DRM_DEBUG_KMS("gem handle = 0x%x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

void owl_drm_gem_destroy(struct owl_drm_gem_obj *owl_gem_obj)
{
	struct drm_gem_object *obj;
	struct owl_drm_gem_buf *buf;

	DRM_DEBUG_KMS("%s\n", __func__);

	obj = &owl_gem_obj->base;
	buf = owl_gem_obj->buffer;

	DRM_DEBUG_KMS("handle count = %d\n", atomic_read(&obj->handle_count));

	/*
	 * do not release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach)
		goto out;

	owl_drm_free_buf(obj->dev, owl_gem_obj->flags, buf);

out:
	owl_drm_fini_buf(obj->dev, buf);
	owl_gem_obj->buffer = NULL;

	if (obj->map_list.map)
		drm_gem_free_mmap_offset(obj);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(owl_gem_obj);
	owl_gem_obj = NULL;
}

unsigned long owl_drm_gem_get_size(struct drm_device *dev,
						unsigned int gem_handle,
						struct drm_file *file_priv)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, file_priv, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return 0;
	}

	owl_gem_obj = to_owl_gem_obj(obj);

	drm_gem_object_unreference_unlocked(obj);

	return owl_gem_obj->buffer->size;
}


struct owl_drm_gem_obj *owl_drm_gem_init(struct drm_device *dev,
						      unsigned long size)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_gem_object *obj;
	int ret;

	owl_gem_obj = kzalloc(sizeof(*owl_gem_obj), GFP_KERNEL);
	if (!owl_gem_obj) {
		DRM_ERROR("failed to allocate owl gem object\n");
		return NULL;
	}

	owl_gem_obj->size = size;
	obj = &owl_gem_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_ERROR("failed to initialize gem object\n");
		kfree(owl_gem_obj);
		return NULL;
	}

	DRM_DEBUG_KMS("created file object = 0x%x\n", (unsigned int)obj->filp);

	return owl_gem_obj;
}

struct owl_drm_gem_obj *owl_drm_gem_create(struct drm_device *dev,
						unsigned int flags,
						unsigned long size)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct owl_drm_gem_buf *buf;
	int ret;
	
	DRM_DEBUG_KMS("size %d\n", size);

	if (!size) {
		DRM_ERROR("invalid size.\n");
		return ERR_PTR(-EINVAL);
	}

	size = roundup_gem_size(size, flags);

	ret = check_gem_flags(flags);
	if (ret)
		return ERR_PTR(ret);

	buf = owl_drm_init_buf(dev, size);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	owl_gem_obj = owl_drm_gem_init(dev, size);
	if (!owl_gem_obj) {
		ret = -ENOMEM;
		goto err_fini_buf;
	}

	owl_gem_obj->buffer = buf;

	/* set memory type and cache attribute from user side. */
	owl_gem_obj->flags = flags;

	ret = owl_drm_alloc_buf(dev, buf, flags);
	if (ret < 0) {
		drm_gem_object_release(&owl_gem_obj->base);
		goto err_fini_buf;
	}

	return owl_gem_obj;

err_fini_buf:
	owl_drm_fini_buf(dev, buf);
	return ERR_PTR(ret);
}

int owl_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_owl_gem_create *args = data;
	struct owl_drm_gem_obj *owl_gem_obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __func__);

	owl_gem_obj = owl_drm_gem_create(dev, args->flags, args->size);
	if (IS_ERR(owl_gem_obj))
		return PTR_ERR(owl_gem_obj);

	ret = owl_drm_gem_handle_create(&owl_gem_obj->base, file_priv,
			&args->handle);
	if (ret) {
		owl_drm_gem_destroy(owl_gem_obj);
		return ret;
	}

	return 0;
}

dma_addr_t *owl_drm_gem_get_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, filp, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return ERR_PTR(-EINVAL);
	}

	owl_gem_obj = to_owl_gem_obj(obj);

	return &owl_gem_obj->buffer->dma_addr;
}

void owl_drm_gem_put_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, filp, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return;
	}

	owl_gem_obj = to_owl_gem_obj(obj);

	drm_gem_object_unreference_unlocked(obj);

	/*
	 * decrease obj->refcount one more time because we has already
	 * increased it at owl_drm_gem_get_dma_addr().
	 */
	drm_gem_object_unreference_unlocked(obj);
}

int owl_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_owl_gem_map_off *args = data;

	DRM_DEBUG_KMS("%s\n", __func__);

	DRM_DEBUG_KMS("handle = 0x%x, offset = 0x%lx\n",
			args->handle, (unsigned long)args->offset);

	if (!(dev->driver->driver_features & DRIVER_GEM)) {
		DRM_ERROR("does not support GEM.\n");
		return -ENODEV;
	}

	return owl_drm_gem_dumb_map_offset(file_priv, dev, args->handle,
			&args->offset);
}

static struct drm_file *owl_drm_find_drm_file(struct drm_device *drm_dev,
							struct file *filp)
{
	struct drm_file *file_priv;

	/* find current process's drm_file from filelist. */
	list_for_each_entry(file_priv, &drm_dev->filelist, lhead)
		if (file_priv->filp == filp)
			return file_priv;

	WARN_ON(1);

	return ERR_PTR(-EFAULT);
}

static int owl_drm_gem_mmap_buffer(struct file *filp,
				      struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = filp->private_data;
	struct owl_drm_gem_obj *owl_gem_obj = to_owl_gem_obj(obj);
	struct drm_device *drm_dev = obj->dev;
	struct owl_drm_gem_buf *buffer;
	struct drm_file *file_priv;
	unsigned long vm_size;
	int ret;

	DRM_DEBUG_KMS("%s\n", __func__);

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data = obj;
	vma->vm_ops = drm_dev->driver->gem_vm_ops;

	/* restore it to driver's fops. */
	filp->f_op = fops_get(drm_dev->driver->fops);

	file_priv = owl_drm_find_drm_file(drm_dev, filp);
	if (IS_ERR(file_priv))
		return PTR_ERR(file_priv);

	/* restore it to drm_file. */
	filp->private_data = file_priv;

	update_vm_cache_attr(owl_gem_obj, vma);

	vm_size = vma->vm_end - vma->vm_start;

	/*
	 * a buffer contains information to physically continuous memory
	 * allocated by user request or at framebuffer creation.
	 */
	buffer = owl_gem_obj->buffer;

	/* check if user-requested size is valid. */
	if (vm_size > buffer->size)
		return -EINVAL;

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

static const struct file_operations owl_drm_gem_fops = {
	.mmap = owl_drm_gem_mmap_buffer,
};

int owl_drm_gem_mmap_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_owl_gem_mmap *args = data;
	struct drm_gem_object *obj;
	unsigned int addr;

	DRM_DEBUG_KMS("%s\n", __func__);

	if (!(dev->driver->driver_features & DRIVER_GEM)) {
		DRM_ERROR("does not support GEM.\n");
		return -ENODEV;
	}

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	/*
	 * We have to use gem object and its fops for specific mmaper,
	 * but vm_mmap() can deliver only filp. So we have to change
	 * filp->f_op and filp->private_data temporarily, then restore
	 * again. So it is important to keep lock until restoration the
	 * settings to prevent others from misuse of filp->f_op or
	 * filp->private_data.
	 */
	mutex_lock(&dev->struct_mutex);

	/*
	 * Set specific mmper's fops. And it will be restored by
	 * owl_drm_gem_mmap_buffer to dev->driver->fops.
	 * This is used to call specific mapper temporarily.
	 */
	file_priv->filp->f_op = &owl_drm_gem_fops;

	/*
	 * Set gem object to private_data so that specific mmaper
	 * can get the gem object. And it will be restored by
	 * owl_drm_gem_mmap_buffer to drm_file.
	 */
	file_priv->filp->private_data = obj;

	addr = vm_mmap(file_priv->filp, 0, args->size,
			PROT_READ | PROT_WRITE, MAP_SHARED, 0);

	drm_gem_object_unreference(obj);

	if (IS_ERR((void *)addr)) {
		/* check filp->f_op, filp->private_data are restored */
		if (file_priv->filp->f_op == &owl_drm_gem_fops) {
			file_priv->filp->f_op = fops_get(dev->driver->fops);
			file_priv->filp->private_data = file_priv;
		}
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR((void *)addr);
	}

	mutex_unlock(&dev->struct_mutex);

	args->mapped = addr;

	DRM_DEBUG_KMS("mapped = 0x%lx\n", (unsigned long)args->mapped);

	return 0;
}

int owl_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_owl_gem_info *args = data;
	struct drm_gem_object *obj;

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	owl_gem_obj = to_owl_gem_obj(obj);

	args->flags = owl_gem_obj->flags;
	args->size = owl_gem_obj->size;

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

struct vm_area_struct *owl_gem_get_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *vma_copy;

	vma_copy = kmalloc(sizeof(*vma_copy), GFP_KERNEL);
	if (!vma_copy)
		return NULL;

	if (vma->vm_ops && vma->vm_ops->open)
		vma->vm_ops->open(vma);

	if (vma->vm_file)
		get_file(vma->vm_file);

	memcpy(vma_copy, vma, sizeof(*vma));

	vma_copy->vm_mm = NULL;
	vma_copy->vm_next = NULL;
	vma_copy->vm_prev = NULL;

	return vma_copy;
}

void owl_gem_put_vma(struct vm_area_struct *vma)
{
	if (!vma)
		return;

	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);

	if (vma->vm_file)
		fput(vma->vm_file);

	kfree(vma);
}

int owl_gem_get_pages_from_userptr(unsigned long start,
						unsigned int npages,
						struct page **pages,
						struct vm_area_struct *vma)
{
	int get_npages;

	/* the memory region mmaped with VM_PFNMAP. */
	if (vma_is_io(vma)) {
		unsigned int i;

		for (i = 0; i < npages; ++i, start += PAGE_SIZE) {
			unsigned long pfn;
			int ret = follow_pfn(vma, start, &pfn);
			if (ret)
				return ret;

			pages[i] = pfn_to_page(pfn);
		}

		if (i != npages) {
			DRM_ERROR("failed to get user_pages.\n");
			return -EINVAL;
		}

		return 0;
	}

	get_npages = get_user_pages(current, current->mm, start,
					npages, 1, 1, pages, NULL);
	get_npages = max(get_npages, 0);
	if (get_npages != npages) {
		DRM_ERROR("failed to get user_pages.\n");
		while (get_npages)
			put_page(pages[--get_npages]);
		return -EFAULT;
	}

	return 0;
}

void owl_gem_put_pages_to_userptr(struct page **pages,
					unsigned int npages,
					struct vm_area_struct *vma)
{
	if (!vma_is_io(vma)) {
		unsigned int i;

		for (i = 0; i < npages; i++) {
			set_page_dirty_lock(pages[i]);

			/*
			 * undo the reference we took when populating
			 * the table.
			 */
			put_page(pages[i]);
		}
	}
}

int owl_gem_map_sgt_with_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	int nents;

	mutex_lock(&drm_dev->struct_mutex);

	nents = dma_map_sg(drm_dev->dev, sgt->sgl, sgt->nents, dir);
	if (!nents) {
		DRM_ERROR("failed to map sgl with dma.\n");
		mutex_unlock(&drm_dev->struct_mutex);
		return nents;
	}

	mutex_unlock(&drm_dev->struct_mutex);
	return 0;
}

void owl_gem_unmap_sgt_from_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	dma_unmap_sg(drm_dev->dev, sgt->sgl, sgt->nents, dir);
}

int owl_drm_gem_init_object(struct drm_gem_object *obj)
{
	DRM_DEBUG_KMS("%s\n", __func__);

	return 0;
}

void owl_drm_gem_free_object(struct drm_gem_object *obj)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct owl_drm_gem_buf *buf;

	DRM_DEBUG_KMS("%s\n", __func__);

	owl_gem_obj = to_owl_gem_obj(obj);
	buf = owl_gem_obj->buffer;

	if (obj->import_attach)
		drm_prime_gem_destroy(obj, buf->sgt);

	owl_drm_gem_destroy(to_owl_gem_obj(obj));
}

int owl_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __func__);

	if (args->size == 0) {
		uint32_t min_pitch = args->pitch ? args->pitch :
				args->width * ((args->bpp + 7) / 8);

		/* align to 64 bytes since Mali requires it. */
		args->pitch = ALIGN(min_pitch, 64);
		args->size = args->pitch * args->height;
	}

	owl_gem_obj = owl_drm_gem_create(dev, OWL_BO_CONTIG |
						OWL_BO_WC, args->size);
	if (IS_ERR(owl_gem_obj))
		return PTR_ERR(owl_gem_obj);

	ret = owl_drm_gem_handle_create(&owl_gem_obj->base, file_priv,
			&args->handle);
	if (ret) {
		owl_drm_gem_destroy(owl_gem_obj);
		return ret;
	}

	return 0;
}

int owl_drm_gem_dumb_map_offset(struct drm_file *file_priv,
				   struct drm_device *dev, uint32_t handle,
				   uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;

	DRM_DEBUG_KMS("%s\n", __func__);

	mutex_lock(&dev->struct_mutex);

	/*
	 * get offset of memory allocated for drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_MAP_DUMB command.
	 */

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		ret = -EINVAL;
		goto unlock;
	}

	if (!obj->map_list.map) {
		ret = drm_gem_create_mmap_offset(obj);
		if (ret)
			goto out;
	}

	*offset = (u64)obj->map_list.hash.key << PAGE_SHIFT;
	DRM_DEBUG_KMS("offset = 0x%lx\n", (unsigned long)*offset);

out:
	drm_gem_object_unreference(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int owl_drm_gem_dumb_destroy(struct drm_file *file_priv,
				struct drm_device *dev,
				unsigned int handle)
{
	int ret;

	DRM_DEBUG_KMS("%s\n", __func__);

	/*
	 * obj->refcount and obj->handle_count are decreased and
	 * if both them are 0 then owl_drm_gem_free_object()
	 * would be called by callback to release resources.
	 */
	ret = drm_gem_handle_delete(file_priv, handle);
	if (ret < 0) {
		DRM_ERROR("failed to delete drm_gem_handle.\n");
		return ret;
	}

	return 0;
}

int owl_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_device *dev = obj->dev;
	unsigned long f_vaddr;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;
	f_vaddr = (unsigned long)vmf->virtual_address;

	mutex_lock(&dev->struct_mutex);

	ret = owl_drm_gem_map_buf(obj, vma, f_vaddr, page_offset);
	if (ret < 0)
		DRM_ERROR("failed to map a buffer with user.\n");

	mutex_unlock(&dev->struct_mutex);

	return convert_to_vm_err_msg(ret);
}

int owl_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct owl_drm_gem_obj *owl_gem_obj;
	struct drm_gem_object *obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __func__);

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	obj = vma->vm_private_data;
	owl_gem_obj = to_owl_gem_obj(obj);

	ret = check_gem_flags(owl_gem_obj->flags);
	if (ret) {
		drm_gem_vm_close(vma);
		drm_gem_free_mmap_offset(obj);
		return ret;
	}

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	update_vm_cache_attr(owl_gem_obj, vma);

	return ret;
}
