#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/owl
owldrm-y  := owl_drm_drv.o owl_drm_encoder.o owl_drm_connector.o \
             owl_drm_crtc.o owl_drm_fbdev.o owl_drm_fb.o \
             owl_drm_buf.o owl_drm_gem.o owl_drm_core.o \
             owl_drm_plane.o 

owldrm-$(CONFIG_DRM_OWL_IOMMU)  += owl_drm_iommu.o
owldrm-$(CONFIG_DRM_OWL_DMABUF) += owl_drm_dmabuf.o
owldrm-$(CONFIG_DRM_OWL_LCD)    += owl_drm_lcd.o
owldrm-$(CONFIG_DRM_OWL_HDMI)	+= owl_drm_hdmi.o

obj-$(CONFIG_DRM_OWL)           += owldrm.o
