dtb-$(CONFIG_ARCH_OWL) += s900_evb.dtb s900_evb_cts.dtb \
			 s900_ces.dtb s900_ksd.dtb \
			 s900_evb_216ball.dtb s900_vr.dtb s900_qcb.dtb s900_96board.dtb \
			 gt9_ebox.dtb s900_upgrade.dtb

dtb-$(CONFIG_ARCH_VEXPRESS) += rtsm_ve-aemv8a.dtb foundation-v8.dtb \
				fvp-base-gicv2-psci.dtb
dtb-$(CONFIG_ARCH_VEXPRESS) += juno.dtb
dtb-$(CONFIG_ARCH_XGENE) += apm-mustang.dtb

targets += dtbs
DTC_FLAGS ?= -R -4 -p 0x1000
DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := dts/*.dtb *.dtb
