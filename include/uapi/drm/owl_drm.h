/*
 * Copyright (c) 2017 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _UAPI_OWL_DRM_H_
#define _UAPI_OWL_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints:
 *  1) Do not use pointers, use __u64 instead for 32 bit / 64 bit
 *     user/kernel compatibility
 *  2) Keep fields aligned to their size
 *  3) Because of how drm_ioctl() works, we can add new fields at
 *     the end of an ioctl if some care is taken: drm_ioctl() will
 *     zero out the new fields at the tail of the ioctl, so a zero
 *     value should have a backwards compatible meaning.  And for
 *     output params, userspace won't see the newly added output
 *     fields.. so that has to be somehow ok.
 */

struct drm_owl_param {
	__u64 param;          /* in, OWL_PARAM_x */
	__u64 value;          /* out (get_param) or in (set_param) */
};


/* gem memory type definitions. */
#define OWL_BO_SCANOUT      0x00000001  /* scanout capable */
#define OWL_BO_CONTIG_MASK  0x00000100  /* physically continuous modes */
#define OWL_BO_CACHE_MASK   0x00030000  /* cache modes */
#define OWL_BO_FLAGS        (OWL_BO_SCANOUT     | \
                             OWL_BO_CONTIG_MASK | \
                             OWL_BO_CACHE_MASK)

/* continuous modes */
#define OWL_BO_NONCONTIG    0x00000000  /* physically non-continuous and used as default */
#define OWL_BO_CONTIG       0x00000100  /* physically continuous */

/* cache modes */
#define OWL_BO_CACHED       0x00000000  /* cached mapping and used as default */
#define OWL_BO_WC           0x00010000  /* write-combine mapping */
#define OWL_BO_UNCACHED     0x00020000  /* strongly-ordered (uncached) mapping */

struct drm_owl_gem_new {
	__u64 size;	   /* in */
	__u32 flags;   /* in, mask of OWL_BO_x */
	__u32 handle;  /* out */
};

struct drm_owl_gem_info {
	__u32 handle;         /* in */
	__u32 pad;
	__u64 offset;         /* out, offset to pass to mmap() */
};


#define OWL_PREP_READ        0x01
#define OWL_PREP_WRITE       0x02
#define OWL_PREP_NOSYNC      0x04
#define OWL_PREP_FLAGS       (OWL_PREP_READ | OWL_PREP_WRITE | OWL_PREP_NOSYNC)

/* timeouts are specified in clock-monotonic absolute times (to simplify
 * restarting interrupted ioctls).  The following struct is logically the
 * same as 'struct timespec' but 32/64b ABI safe.
 */
struct drm_owl_timespec {
	__s64 tv_sec;          /* seconds */
	__s64 tv_nsec;         /* nanoseconds */
};

struct drm_owl_gem_cpu_prep {
	__u32 handle;         /* in */
	__u32 op;             /* in, mask of OWL_PREP_x */
	struct drm_owl_timespec timeout;   /* in */
};

struct drm_owl_gem_cpu_fini {
	__u32 handle;         /* in */
	__u32 op;             /* in, mask of OWL_PREP_x */
};


#define DRM_OWL_GEM_NEW                0x00
#define DRM_OWL_GEM_INFO               0x01
/* placeholder:
#define DRM_OWL_GET_PARAM              0x02
#define DRM_OWL_SET_PARAM              0x03
#define DRM_OWL_GEM_CPU_PREP           0x04
#define DRM_OWL_GEM_CPU_FINI           0x05
 */
#define DRM_OWL_NUM_IOCTLS             0x02

#define DRM_IOCTL_OWL_GEM_NEW          DRM_IOWR(DRM_COMMAND_BASE + DRM_OWL_GEM_NEW, struct drm_owl_gem_new)
#define DRM_IOCTL_OWL_GEM_INFO         DRM_IOWR(DRM_COMMAND_BASE + DRM_OWL_GEM_INFO, struct drm_owl_gem_info)
/* placeholder:
#define DRM_IOCTL_OWL_GET_PARAM        DRM_IOWR(DRM_COMMAND_BASE + DRM_OWL_GET_PARAM, struct drm_owl_param)
#define DRM_IOCTL_OWL_SET_PARAM	       DRM_IOW (DRM_COMMAND_BASE + DRM_OWL_SET_PARAM, struct drm_owl_param)
#define DRM_IOCTL_OWL_GEM_CPU_PREP     DRM_IOW (DRM_COMMAND_BASE + DRM_OWL_GEM_CPU_PREP, struct drm_owl_gem_cpu_prep)
#define DRM_IOCTL_OWL_GEM_CPU_FINI     DRM_IOW (DRM_COMMAND_BASE + DRM_OWL_GEM_CPU_FINI, struct drm_owl_gem_cpu_fini)
*/

#if defined(__cplusplus)
}
#endif

#endif /* _UAPI_OWL_DRM_H_ */
