
obj-y += pd.o 

ifeq ($(CONFIG_ARCH_OWL_ATS3605_SOC), y)
obj-y += ats3605_pd.o
else
obj-$(CONFIG_ARCH_OWL) += s700_pd.o s900_pd.o
endif
