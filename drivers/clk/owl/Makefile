#
# Actions SOC Clock specific Makefile
#
obj-y += clk.o clk-pll.o clk-factor.o


ifeq ($(CONFIG_ARCH_OWL_ATS3605_SOC), y)
obj-y +=clk-ats3605.o
else
obj-y += clk-s900.o  clk-s700.o
endif
