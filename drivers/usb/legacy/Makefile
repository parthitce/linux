# obj-$(CONFIG_USB_LEGACY_AOTG_UDC)		+= aotg_udc/
# obj-$(CONFIG_USB_LEGACY_AOTG_ADFU)		+= aotg_udc/

ifneq ($(filter m y, $(CONFIG_USB_LEGACY_AOTG_UDC) $(CONFIG_USB_LEGACY_AOTG_ADFU)), )
	obj-y += aotg_udc/
endif

obj-$(CONFIG_USB_LEGACY_AOTG_HCD) += aotg_hcd/
obj-$(CONFIG_USB_LEGACY_MONITOR) += monitor/