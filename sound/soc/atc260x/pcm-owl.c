/*
 * s900-pcm.c  --  ALSA PCM interface for the OMAP SoC
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/dma-mapping.h>

#include "sndrv-owl.h"
#include "common-regs-owl.h"

static struct snd_pcm_hardware s900_playback_hw_info = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 2,
	.channels_max		= 8,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= 32*1024,
	.periods_min		= 2,
	.periods_max		= 16,
};

static struct snd_pcm_hardware s900_capture_hw_info = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.rate_min		= 8000,
	.rate_max		= 96000,
	.channels_min		= 1,
	.channels_max		= 4,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= 32*1024,
	.periods_min		= 2,
	.periods_max		= PAGE_SIZE / 16,
};

/* get phys_addr of i2s/pcm register from dts file */
extern phys_addr_t get_dai_i2s_reg_base(void);
extern phys_addr_t get_dai_pcm_reg_base(void);

static ssize_t error_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", error_switch);
	return cnt;
}

static ssize_t error_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		error_switch = tmp;
		break;
	default:
		printk(KERN_ERR"invalid input\n");
		break;
	}
	return count;
}


static ssize_t debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", debug_switch);
	return cnt;
}

static ssize_t debug_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		debug_switch = tmp;
		break;
	default:
		printk(KERN_INFO"invalid input\n");
		break;
	}
	return count;
}

static struct device_attribute pcm_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

static const char *const audio_output_mode[]
	= {"i2s", "hdmi", "spdif", "pcm"};
/* no use "hdmi" and "spdif" in input mode */
static const char *const audio_input_mode[]
	= {"i2s", "hdmi", "spdif", "pcm"};

static const SOC_ENUM_SINGLE_DECL(audio_output_enum,
	0, 0, audio_output_mode);

static int audio_output_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform =
		snd_kcontrol_chip(kcontrol);
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);
	ucontrol->value.integer.value[0] = pcm_priv->output_mode;
	return 0;
}

static int audio_output_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform =
		snd_kcontrol_chip(kcontrol);
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	pcm_priv->output_mode = ucontrol->value.integer.value[0];
	return 0;
}

static const SOC_ENUM_SINGLE_DECL(audio_input_enum,
	0, 0, audio_input_mode);

static int audio_input_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform =
		snd_kcontrol_chip(kcontrol);
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);
	ucontrol->value.integer.value[0] = pcm_priv->input_mode;
	return 0;
}

static int audio_input_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_platform *platform =
		snd_kcontrol_chip(kcontrol);
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	pcm_priv->input_mode = ucontrol->value.integer.value[0];
	return 0;
}
static const struct snd_kcontrol_new s900_pcm_controls[] = {
	SOC_ENUM_EXT("audio output mode switch", audio_output_enum,
			audio_output_mode_get, audio_output_mode_put),
	SOC_ENUM_EXT("audio input mode switch", audio_input_enum,
			audio_input_mode_get, audio_input_mode_put),
};

/* this may get called several times by oss emulation */
static int s900_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct dma_slave_config slave_config;
	int ret;
	dma_addr_t dst_addr, src_addr;
	enum dma_slave_buswidth dst_addr_width;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
			snd_soc_platform_get_drvdata(platform);

	/* direction, dst_addr_width or src_addr_width */
	ret = snd_hwparams_to_dma_slave_config(substream, params, &slave_config);
	if (ret)
		return ret;

	slave_config.device_fc = false;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		switch (pcm_priv->output_mode) {
		case O_MODE_PCM:
			dst_addr = get_dai_pcm_reg_base();
			dst_addr += PCM1_TXDAT;
			break;
		case O_MODE_SPDIF:
			dst_addr = get_dai_i2s_reg_base();
			dst_addr += SPDIF_DAT;
			break;
		case O_MODE_HDMI:
			dst_addr = get_dai_i2s_reg_base();
			dst_addr += HDMI_DAT;
			break;
		case O_MODE_I2S:
		default:
			dst_addr = get_dai_i2s_reg_base();
			dst_addr += I2STX_DAT;
			break;
		}

		slave_config.dst_addr = dst_addr;

		dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		slave_config.dst_addr_width = dst_addr_width;

	} else {
		switch (pcm_priv->input_mode) {
		case O_MODE_PCM:
			src_addr = get_dai_pcm_reg_base();
			slave_config.src_addr = src_addr + PCM1_RXDAT;
			break;
		case O_MODE_I2S:
		default:
			src_addr = get_dai_i2s_reg_base();
			slave_config.src_addr = src_addr + I2SRX_DAT;
			break;
		}
		slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	}

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret)
		return ret;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	/*
	 * Set DMA transfer frame size equal to ALSA period size and frame
	 * count as no. of ALSA periods. Then with DMA frame interrupt enabled,
	 * we can transfer the whole ALSA buffer with single DMA transfer but
	 * still can get an interrupt at each period bounary
	 */

	return 0;
}

static int s900_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int s900_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int s900_pcm_open(struct snd_pcm_substream *substream)
{
	int ret;
	struct dma_chan *chan;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_set_runtime_hwparams(substream, &s900_playback_hw_info);
	} else {
		snd_soc_set_runtime_hwparams(substream, &s900_capture_hw_info);
	}

	/* snd_pcm_hw_constraint_integer&&��������ʱ&&����ͨ��*/
	ret = snd_dmaengine_pcm_open_request_chan(substream, NULL, NULL);
	if (ret) {
		return 0;
	}

	chan = snd_dmaengine_pcm_get_chan(substream);
	return 0;
}

static int s900_pcm_close(struct snd_pcm_substream *substream)
{

	/* �ͷ�ͨ�����ͷ�����ʱ*/
	snd_dmaengine_pcm_close_release_chan(substream);

	return 0;
}

static int s900_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = dma_mmap_coherent(substream->pcm->card->dev, vma,
								runtime->dma_area,
								runtime->dma_addr,
								runtime->dma_bytes);
	return ret;
}

static struct snd_pcm_ops s900_pcm_ops = {
	.open		= s900_pcm_open,
	.close		= s900_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= s900_pcm_hw_params,
	.hw_free	= s900_pcm_hw_free,
	.prepare	= s900_pcm_prepare,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer_no_residue,
	.mmap		= s900_pcm_mmap,
};

static u64 s900_pcm_dmamask = DMA_BIT_MASK(32);

static int s900_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		size = s900_playback_hw_info.buffer_bytes_max;
	else
		size = s900_capture_hw_info.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	memset(buf->area, 0, size);

	buf->bytes = size;

	return 0;
}

static void s900_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_coherent(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);

		buf->area = NULL;
	}
}

static int s900_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &s900_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = s900_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = s900_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	/* free preallocated buffers in case of error */
	if (ret)
		s900_pcm_free_dma_buffers(pcm);

	return ret;
}

static struct snd_soc_platform_driver s900_soc_platform = {
	.ops		= &s900_pcm_ops,
	.pcm_new	= s900_pcm_new,
	.pcm_free	= s900_pcm_free_dma_buffers,

	.controls = s900_pcm_controls,
	.num_controls = ARRAY_SIZE(s900_pcm_controls),
};

static int s900_pcm_probe(struct platform_device *pdev)
{
	dev_err(&pdev->dev,
			"s900_pcm_probe!!\n");
	pdev->dev.init_name = "s900-pcm-audio";
	return snd_soc_register_platform(&pdev->dev,
			&s900_soc_platform);
}

static int s900_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver s900_pcm_driver = {
	.driver = {
			.name = "s900-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = s900_pcm_probe,
	.remove = s900_pcm_remove,
};

static struct platform_device *s900_pcm_device;
static int __init s900_pcm_init(void)
{
	int ret;
	int i = 0;
	struct s900_pcm_priv *pcm_priv;

	printk(KERN_INFO"s900_pcm_init\n");
	s900_pcm_device = platform_device_alloc("s900-pcm-audio", -1);
	if (!s900_pcm_device) {
		snd_dbg(
				"ASoC: Platform device s900-pcm-audio allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = platform_device_add(s900_pcm_device);
	if (ret) {
		snd_dbg(
				"ASoC: Platform device s900-pcm-audio add failed\n");
		goto err_device_add;
	}

	pcm_priv = kzalloc(sizeof(struct s900_pcm_priv), GFP_KERNEL);
	if (NULL == pcm_priv)
		return -ENOMEM;
	pcm_priv->output_mode = O_MODE_I2S;
	pcm_priv->input_mode = O_MODE_I2S;
	platform_set_drvdata(s900_pcm_device, pcm_priv);

	ret = platform_driver_register(&s900_pcm_driver);
	if (ret) {
		snd_dbg(
				"ASoC: Platform driver s900-pcm-audio register failed\n");
		goto err_driver_register;
	}

	for (i = 0; i < ARRAY_SIZE(pcm_attr); i++) {
		ret = device_create_file(
			&s900_pcm_device->dev, &pcm_attr[i]);
		if (ret) {
			snd_err("Add device file failed");
			goto device_create_file_failed;
		}
	}

	return 0;

device_create_file_failed:
err_driver_register:
	platform_device_unregister(s900_pcm_device);
err_device_add:
	platform_device_put(s900_pcm_device);
err:
	return ret;
}
static void __exit s900_pcm_exit(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(pcm_attr); i++) {
		device_remove_file(&s900_pcm_device->dev, &pcm_attr[i]);
	}

	platform_driver_unregister(&s900_pcm_driver);
	platform_device_unregister(s900_pcm_device);
	s900_pcm_device = NULL;
}

module_init(s900_pcm_init);
module_exit(s900_pcm_exit);

MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 PCM module");
MODULE_LICENSE("GPL");
