#
# Makefile for ppm_owl
#
ifeq ($(CONFIG_ARCH_OWL_ATS3605_SOC), y)
obj-y			+= ppm_owl_sysfs.o
obj-y			+= ppm_owl_cpufreq_opt.o
else
obj-y			+= ppm_owl_input.o
obj-y			+= ppm_owl_driver.o
obj-y			+= ppm_owl_sysfs.o
obj-y			+= ppm_owl_fsm.o
obj-y			+= ppm_owl_base.o
obj-y			+= ppm_owl_stats.o
obj-y			+= ppm_owl_cpufreq_opt.o
endif

