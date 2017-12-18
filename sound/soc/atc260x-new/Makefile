# AT91 Platform Support
snd-soc-atc2603c-objs                                  :=      asoc-codec/atc2603c-audio-codec.o
snd-soc-dai-owl-i2s-objs                               :=      asoc-platform/dai-owl-i2s.o
snd-soc-dai-owl-hdmi-objs                              :=      asoc-platform/dai-owl-hdmi.o
snd-soc-pcm-owl-objs                                   :=      asoc-platform/pcm-owl.o asoc-platform/dmaengine-pcm-owl.o
snd-soc-link-owl-objs                                  :=      asoc-machine/link-owl.o

obj-$(CONFIG_SND_SOC_ACT_2603C) += snd-soc-atc2603c.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_I2S) += snd-soc-dai-owl-i2s.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_HDMI) += snd-soc-dai-owl-hdmi.o
obj-$(CONFIG_SND_SOC_ACT_OWL) += snd-soc-pcm-owl.o

# ATC2603A PMU Machine Support
obj-$(CONFIG_SND_SOC_ACT_ALL_PMU_OWL) += snd-soc-link-owl.o
