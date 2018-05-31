
obj-y += owl_dbg.o
obj-y += powergate/
obj-y += owl_bootmode.o
obj-y += ppm-owl/

ifeq ($(CONFIG_ARCH_OWL_ATS3605_SOC), y)
	obj-$(CONFIG_PM_SLEEP) += ats3605_pm.o
	obj-y += ats3605_dvfs.o
	obj-y += ats3605_ota.o
else
obj-$(CONFIG_PM_SLEEP) += owl_pm.o
obj-$(CONFIG_OWL_CPU_AUTO_HOTPLUG) += hotplug-owl.o
obj-y += owl_dmm_pm.o
obj-y += dvfs.o
endif
