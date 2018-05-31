# AT91 Platform Support
snd-soc-atc2603c-objs                       :=      asoc-codec/atc2603c-audio-codec.o
snd-soc-nau85l40-objs                       :=      asoc-codec/NAU85L40-audio-codec.o
snd-soc-dai-owl-i2s-objs                    :=      asoc-platform/dai-owl-i2s.o
snd-soc-dai-owl-spi-objs                    :=      asoc-platform/dai-owl-spi.o
snd-soc-dai-owl-hdmi-objs                   :=      asoc-platform/dai-owl-hdmi.o
snd-soc-dai-owl-i2s-6wire-objs              :=      asoc-platform/dai-owl-i2s-6wire.o
snd-soc-pcm-owl-objs                        :=      asoc-platform/pcm-owl.o asoc-platform/dmaengine-pcm-owl.o
snd-soc-link-owl-objs                       :=      asoc-machine/link-owl.o
snd-soc-link-owl-3605-spi-nau85l40-objs     :=      asoc-machine/link-owl-3605-spi-nau85l40.o 
snd-soc-link-owl-hdmi-objs                  :=      asoc-machine/link-owl-hdmi.o
snd-soc-link-owl-6wirei2s-atc2603c-objs     :=      asoc-machine/link-owl-6wirei2s-atc2603c.o

obj-$(CONFIG_SND_SOC_ACT_2603C) += snd-soc-atc2603c.o
obj-$(CONFIG_SND_SOC_ACT_NAU85L40) += snd-soc-nau85l40.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_I2S) += snd-soc-dai-owl-i2s.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_SPI) += snd-soc-dai-owl-spi.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_HDMI) += snd-soc-dai-owl-hdmi.o
obj-$(CONFIG_SND_SOC_ACT_DAI_OWL_I2S_6WIRE) += snd-soc-dai-owl-i2s-6wire.o
obj-$(CONFIG_SND_SOC_ACT_OWL) += snd-soc-pcm-owl.o

# ATC2603A PMU Machine Support
obj-$(CONFIG_SND_SOC_ACT_ALL_PMU_OWL) += snd-soc-link-owl.o
obj-$(CONFIG_SND_SOC_ACT_ALL_PMU_OWL_6WIRE) += snd-soc-link-owl-6wirei2s-atc2603c.o
obj-$(CONFIG_SND_SOC_ACT_ALL_PMU_OWL_3605_NAU85L40) += snd-soc-link-owl-3605-spi-nau85l40.o
obj-$(CONFIG_SND_SOC_ACT_OWL_HDMI) += snd-soc-link-owl-hdmi.o
