#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI)
#

ccflags-y := -Idrivers/gpu/drm/owl
owldrm-y  := \
	owl_drv.o \
	owl_core.o \
	owl_crtc.o \
	owl_encoder.o \
	owl_connector.o \
	owl_plane.o \
	owl_fb.o \
	owl_fbdev.o \
	owl_gem.o \
	owl_gem_prime.o \
	owl_gem_helper.o \
	owl_debugfs.o \
	owl_irq.o

# dss
owldrm-y                      += dss/dispc.o
owldrm-$(CONFIG_DRM_OWL_LCD)  += dss/lcd.o
owldrm-$(CONFIG_DRM_OWL_HDMI) += dss/hdmi.o

obj-$(CONFIG_DRM_OWL) += owldrm.o
