/*
 * NAU85L40.c  --  NAU85L40 ALSA SoC Audio driver
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/regulator/consumer.h>

#include "NAU85L40-audio-regs.h"
#include "../sndrv-owl.h"

#define NAU85L40_MAX_REGISTER                     0x6E
#define I2C_NAU85L40_ADDRESS                      0x1C
#define I2C_ADAPTER_NUM                           1
//#define CFG_AUDIO_USE_CONFIG

#if CFG_AUDIO_USE_CONFIG
	struct NAU85L40_cfg_xml {
	unsigned int i2cNum;
	unsigned int i2cAddr;
};
static struct NAU85L40_cfg_xml cfg_xml;
#endif
static struct i2c_client *NAU85L40_client;

static u16 NAU85L40_reg_defaults[] = {
	0x00,     /* R0    NAU85L40_SW_RESET    */
	0x00,     /* R1    NAU85L40_POWER_MANAGEMENT*/
	0x00,     /* R2    NAU85L40_CLOCK_CTRL*/
	0x00,     /* R3    NAU85L40_CLOCK_SRC*/
	0x01,     /* R4    NAU85L40_FLL1*/
	0x3126,   /* R5    NAU85L40_FLL2*/
	0x08,     /* R6    NAU85L40_FLL3*/
	0x10,     /* R7    NAU85L40_FLL4*/
	0x0C00,   /* R8    NAU85L40_FLL5*/
	0x6000,   /* R9    NAU85L40_FLL6*/
	0xF13C,   /* R10  NAU85L40_FLL_VCO_RSV*/
	0x00,     /* R11  */
	0x00,     /* R12  */
	0x00,     /* R13  */
	0x00,     /* R14  */
	0x00,     /* R15  */
	0x0B,     /* R16  NAU85L40_PCM_CTRL0*/
	0x02,     /* R17  NAU85L40_PCM_CTRL1*/
	0x00,     /* R18  NAU85L40_PCM_CTRL2*/
	0x00,     /* R19  NAU85L40_PCM_CTRL3*/
	0x00,     /* R20  NAU85L40_PCM_CTRL4*/
	0x00,     /* R21  */
	0x00,     /* R22  */
	0x00,     /* R23  */
	0x00,     /* R24  */
	0x00,     /* R25  */
	0x00,     /* R26  */
	0x00,     /* R27  */
	0x00,     /* R28  */
	0x00,     /* R29  */
	0x00,     /* R30  */
	0x00,     /* R31  */
	0x70,     /* R32  NAU85L40_ALC_CONTROL_1*/
	0x00,     /* R33  NAU85L40_ALC_CONTROL_2*/
	0x00,     /* R34  NAU85L40_ALC_CONTROL_3*/
	0x1010,   /* R35  NAU85L40_ALC_CONTROL_4*/
	0x1010,   /* R36  NAU85L40_ALC_CONTROL_5*/
	0x00,     /* R37  */
	0x00,     /* R38  */
	0x00,     /* R39  */
	0x00,     /* R40  */
	0x00,     /* R41  */
	0x00,     /* R42  */
	0x00,     /* R43  */
	0x00,     /* R44  */
	0x00,     /* R45  NAU85L40_ALC_GAIN_CH12*/
	0x00,     /* R46  NAU85L40_ALC_GAIN_CH34*/
	0x00,     /* R47  NAU85L40_ALC_STATUS*/
	0x00,     /* R48  NAU85L40_NOTCH_FIL1_CH1*/
	0x00,     /* R49  NAU85L40_NOTCH_FIL2_CH1*/
	0x00,     /* R50  NAU85L40_NOTCH_FIL1_CH2*/
	0x00,     /* R51  NAU85L40_NOTCH_FIL2_CH2*/
	0x00,     /* R52  NAU85L40_NOTCH_FIL1_CH3*/
	0x00,     /* R53  NAU85L40_NOTCH_FIL2_CH3*/
	0x00,     /* R54  NAU85L40_NOTCH_FIL1_CH4*/
	0x00,     /* R55  NAU85L40_NOTCH_FIL2_CH4*/
	0x00,     /* R56  NAU85L40_HPF_FILTER_CH12*/
	0x00,     /* R57  NAU85L40_HPF_FILTER_CH34*/
	0x02,     /* R58  NAU85L40_ADC_SAMPLE_RATE*/
	0x00,     /* R59  */
	0x00,     /* R60  */
	0x00,     /* R61  */
	0x00,     /* R62  */
	0x00,     /* R63  */
	0x0400,   /* R64  NAU85L40_DIGITAL_GAIN_CH1*/
	0x1400,   /* R65  NAU85L40_DIGITAL_GAIN_CH2*/
	0x2400,   /* R66  NAU85L40_DIGITAL_GAIN_CH3*/
	0x0400,   /* R67  NAU85L40_DIGITAL_GAIN_CH4*/
	0xE4,     /* R68  NAU85L40_DIGITAL_MUX*/
	0x00,     /* R69  */
	0x00,     /* R70  */
	0x00,     /* R71  */
	0x00,     /* R72  NAU85L40_P2P_CH1*/
	0x00,     /* R73  NAU85L40_P2P_CH2*/
	0x00,     /* R74  NAU85L40_P2P_CH3*/
	0x00,     /* R75  NAU85L40_P2P_CH4*/
	0x00,     /* R76  NAU85L40_PEAK_CH1*/
	0x00,     /* R77  NAU85L40_PEAK_CH2*/
	0x00,     /* R78  NAU85L40_PEAK_CH3*/
	0x00,     /* R79  NAU85L40_PEAK_CH4*/
	0x00,     /* R80  NAU85L40_GPIO_CTRL*/
	0x00,     /* R81  NAU85L40_MISC_CTRL*/
	0xEFFF,   /* R82  NAU85L40_I2C_CTRL*/
	0x00,     /* R83  */
	0x00,     /* R84  */
	0x00,     /* R85  */
	0x00,     /* R86  */
	0x00,     /* R87  */
	0x00,     /* R88  NAU85L40_I2C_DEVICE_ID*/
	0x00,     /* R89  */
	0x00,     /* R90  NAU85L40_RST*/
	0x00,     /* R91  */
	0x00,     /* R92  */
	0x00,     /* R93  */
	0x00,     /* R94  */
	0x00,     /* R95  */
	0x00,     /* R96  NAU85L40_VMID_CTRL*/
	0x00,     /* R97  NAU85L40_MUTE*/
	0x00,     /* R98  */
	0x00,     /* R99  */
	0x00,     /* R100 NAU85L40_ANALOG_ADC1*/
	0x20,     /* R101 NAU85L40_ANALOG_ADC2*/
	0x00,     /* R102 NAU85L40_ANALOG_PWR*/
	0x04,     /* R103 NAU85L40_MIC_BIAS*/
	0x00,     /* R104 NAU85L40_REFERENCE*/
	0x00,     /* R105 NAU85L40_FEPGA1*/
	0x00,     /* R106 NAU85L40_FEPGA2*/
	0x0101,   /* R107 NAU85L40_FEPGA3*/
	0x0101,   /* R108 NAU85L40_FEPGA4*/
	0x00      /* R109 NAU85L40_PWR*/
};

struct NAU85L40_priv {
	enum snd_soc_control_type control_type;
	int sysclk;
	struct regulator *IDO1;
	struct regulator *IDO7;
};

static int NAU85L40_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	return 0;
}

static int NAU85L40_readable_register(struct snd_soc_codec *codec, unsigned int reg)
{
	return 1;
}

static int NAU85L40_writable_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case NAU85L40_SW_RESET:
	case NAU85L40_POWER_MANAGEMENT:
	case NAU85L40_CLOCK_CTRL:
	case NAU85L40_CLOCK_SRC:
	case NAU85L40_FLL1:
	case NAU85L40_FLL2:
	case NAU85L40_FLL3:
	case NAU85L40_FLL4:
	case NAU85L40_FLL5:
	case NAU85L40_FLL6:
	case NAU85L40_FLL_VCO_RSV:
	case NAU85L40_PCM_CTRL0:
	case NAU85L40_PCM_CTRL1:
	case NAU85L40_PCM_CTRL2:
	case NAU85L40_PCM_CTRL3:
	case NAU85L40_PCM_CTRL4:
	case NAU85L40_ALC_CONTROL_1:
	case NAU85L40_ALC_CONTROL_2:
	case NAU85L40_ALC_CONTROL_3:
	case NAU85L40_ALC_CONTROL_4:
	case NAU85L40_ALC_CONTROL_5:
	case NAU85L40_NOTCH_FIL1_CH1:
	case NAU85L40_NOTCH_FIL2_CH1:
	case NAU85L40_NOTCH_FIL1_CH2:
	case NAU85L40_NOTCH_FIL2_CH2:
	case NAU85L40_NOTCH_FIL1_CH3:
	case NAU85L40_NOTCH_FIL2_CH3:
	case NAU85L40_NOTCH_FIL1_CH4:
	case NAU85L40_NOTCH_FIL2_CH4:
	case NAU85L40_HPF_FILTER_CH12:
	case NAU85L40_HPF_FILTER_CH34:
	case NAU85L40_ADC_SAMPLE_RATE:
	case NAU85L40_DIGITAL_GAIN_CH1:
	case NAU85L40_DIGITAL_GAIN_CH2:
	case NAU85L40_DIGITAL_GAIN_CH3:
	case NAU85L40_DIGITAL_GAIN_CH4:
	case NAU85L40_DIGITAL_MUX:
	case NAU85L40_GPIO_CTRL:
	case NAU85L40_MISC_CTRL:
	case NAU85L40_I2C_CTRL:
	case NAU85L40_RST:
	case NAU85L40_VMID_CTRL:
	case NAU85L40_MUTE:
	case NAU85L40_ANALOG_ADC1:
	case NAU85L40_ANALOG_ADC2:
	case NAU85L40_ANALOG_PWR:
	case NAU85L40_MIC_BIAS:
	case NAU85L40_REFERENCE:
	case NAU85L40_FEPGA1:
	case NAU85L40_FEPGA2:
	case NAU85L40_FEPGA3:
	case NAU85L40_FEPGA4:
	case NAU85L40_PWR:
		return 1;
	default:
		return 0;
	}
}

static int NAU85L40_reset(struct snd_soc_codec *codec)
{
	snd_soc_write(codec, NAU85L40_SW_RESET, 0x0000);
	return snd_soc_write(codec, NAU85L40_SW_RESET, 0xFFFF);
}

static int NAU85L40_start(struct snd_soc_codec *codec)
{
	//Enable VREF
	snd_soc_write(codec, NAU85L40_REFERENCE, 0x7000);
	snd_soc_write(codec, NAU85L40_VMID_CTRL, 0x0060);
	snd_soc_write(codec, NAU85L40_CLOCK_CTRL, 0x800a);
	//clk src   ADCclk=mclk/1   mclk=clkin/3
	snd_soc_write(codec, NAU85L40_CLOCK_SRC, 0x0007);
	//pcm ctrl  32bit pcma ;MSB on 2st bclk
	snd_soc_write(codec, NAU85L40_PCM_CTRL0, 0x000f);
	//pcm ctrl1 master mode  256fs
	snd_soc_write(codec, NAU85L40_PCM_CTRL1, 0x0008);
	snd_soc_write(codec, NAU85L40_PCM_CTRL2, 0x4800);
	snd_soc_write(codec, NAU85L40_PCM_CTRL4, 0x800f);
	snd_soc_write(codec, NAU85L40_HPF_FILTER_CH12, 0x1f1f);
	snd_soc_write(codec, NAU85L40_HPF_FILTER_CH34, 0x1f1f);
	//OSR   256
	snd_soc_write(codec, NAU85L40_ADC_SAMPLE_RATE, 0x0063);
	snd_soc_write(codec, NAU85L40_ANALOG_PWR, 0x000f);
	snd_soc_write(codec, NAU85L40_MIC_BIAS, 0x0d07);
	snd_soc_write(codec, NAU85L40_FEPGA1, 0x0088);
	snd_soc_write(codec, NAU85L40_FEPGA2, 0xaa88);
	snd_soc_write(codec, NAU85L40_FEPGA3, 0x1515);
	snd_soc_write(codec, NAU85L40_FEPGA4, 0x1515);
	snd_soc_write(codec, NAU85L40_DIGITAL_GAIN_CH1, 0x04a0);
	snd_soc_write(codec, NAU85L40_DIGITAL_GAIN_CH2, 0x14a0);
	snd_soc_write(codec, NAU85L40_DIGITAL_GAIN_CH3, 0x24a0);
	snd_soc_write(codec, NAU85L40_DIGITAL_GAIN_CH4, 0x34a0);

	snd_soc_write(codec, NAU85L40_PWR, 0xf000);
	snd_soc_write(codec, NAU85L40_POWER_MANAGEMENT, 0x000f);
	codec->cache_bypass = 1;
	snd_err("read reg NAU85L40_FEPGA1 val 0x%x",snd_soc_read(codec, NAU85L40_FEPGA1));
	codec->cache_bypass = 0;

	return 0;
}


static int NAU85L40_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct NAU85L40_priv *NAU85L40 = snd_soc_codec_get_drvdata(codec);
	int i, best, target, fs;
	u16 reg;
	
	NAU85L40_reset(codec);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		return -EINVAL;
	}

	fs = params_rate(params);
	NAU85L40_start(codec);

	return 0;
}

static int NAU85L40_hw_free(
		struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	snd_dbg("NAU85L40_audio_hw_free\n");
	/* disable the atc2603c i2s input function */
	NAU85L40_reset(codec);
	
	return 0;
}

#define NAU85L40_RATES SNDRV_PCM_RATE_16000

#define NAU85L40_FORMATS SNDRV_PCM_FMTBIT_S32_LE

static const struct snd_soc_dai_ops NAU85L40_dai_ops = {
	.hw_params = NAU85L40_hw_params,
	.hw_free = NAU85L40_hw_free,
};

static struct snd_soc_dai_driver NAU85L40_dai = {
	.name = "NAU85L40-codec-dai",
	.capture = {
		.stream_name = "NAU85L40 Capture",
		.channels_min = 4,
		.channels_max = 4,
		.rates = NAU85L40_RATES,
		.formats = NAU85L40_FORMATS,},
	.ops = &NAU85L40_dai_ops,
};

static int NAU85L40_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret = 0;
	u16 reg;
	struct NAU85L40_priv *NAU85L40;

	ret = snd_soc_codec_set_cache_io(codec, 16, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	snd_err("NAU85L40_probe");
	//ret = NAU85L40_reset(codec);
	//if (ret < 0) {
	//	dev_err(codec->dev, "Failed to issue reset\n");
	//	return ret;
	//}

	//snd_soc_add_codec_controls(codec, NAU85L40_snd_controls,
				//ARRAY_SIZE(NAU85L40_snd_controls));
	NAU85L40 = devm_kzalloc(codec->dev, sizeof(struct NAU85L40_priv),
			      GFP_KERNEL);
	if (NAU85L40 == NULL)
		return -ENOMEM;
	NAU85L40->IDO1= regulator_get(codec->dev, "ldo1");
	if (IS_ERR(NAU85L40->IDO1))
		dev_warn(codec->dev, "failed to get the regulator ldo1");

	NAU85L40->IDO7= regulator_get(codec->dev, "ldo7");
	if (IS_ERR(NAU85L40->IDO7))
		dev_warn(codec->dev, "failed to get the regulator ldo7");

	regulator_set_voltage(NAU85L40->IDO1, 3300000, 3300000);
	regulator_enable(NAU85L40->IDO1);

	regulator_set_voltage(NAU85L40->IDO7, 1800000, 1800000);
	regulator_enable(NAU85L40->IDO7);
	
	snd_soc_codec_set_drvdata(codec, NAU85L40);

	return 0;
}

static int NAU85L40_remove(struct snd_soc_codec *codec)
{
	struct NAU85L40_priv *NAU85L40 = snd_soc_codec_get_drvdata(codec);

	if (!IS_ERR(NAU85L40->IDO1)) {
		regulator_disable(NAU85L40->IDO1);
		regulator_put(NAU85L40->IDO1);
	}

	if (!IS_ERR(NAU85L40->IDO7)) {
		regulator_disable(NAU85L40->IDO7);
		regulator_put(NAU85L40->IDO7);
	}

	NAU85L40_reset(codec);
	return 0;
}

#ifdef CONFIG_PM
static int NAU85L40_suspend(struct snd_soc_codec *codec)
{
	struct NAU85L40_priv *NAU85L40 = snd_soc_codec_get_drvdata(codec);

	NAU85L40_reset(codec);
	regulator_disable(NAU85L40->IDO1);
	regulator_disable(NAU85L40->IDO7);

	return 0;
}

static int NAU85L40_resume(struct snd_soc_codec *codec)
{
	struct NAU85L40_priv *NAU85L40 = snd_soc_codec_get_drvdata(codec);
	
	regulator_set_voltage(NAU85L40->IDO1, 3300000, 3300000);
	regulator_enable(NAU85L40->IDO1);
	
	regulator_set_voltage(NAU85L40->IDO7, 1800000, 1800000);
	regulator_enable(NAU85L40->IDO7);

	snd_soc_cache_sync(codec);
	NAU85L40_start(codec);
	return 0;
}
#else
#define NAU85L40_suspend NULL
#define NAU85L40_resume NULL
#endif

static struct snd_soc_codec_driver soc_codec_dev_NAU85L40 = {
	.probe =	NAU85L40_probe,
	.remove =	NAU85L40_remove,
	.suspend =	NAU85L40_suspend,
	.resume =	NAU85L40_resume,
	//.set_bias_level = NAU85L40_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(NAU85L40_reg_defaults),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = NAU85L40_reg_defaults,
	.volatile_register = NAU85L40_volatile_register,
	.readable_register = NAU85L40_readable_register,
	.writable_register = NAU85L40_writable_register,
};

static int regaddr = 0;
static int regval = 0;
static ssize_t reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "reg %x regval %x\n", regaddr,regval);
	return cnt;
}

static ssize_t reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int aregaddr =0, val=0;
	int ret;
	
	char *found = strstr(buf, "read");
	if(found){
		sscanf(buf, "read:%x", &aregaddr);
		snd_err("read reg 0x%x ",aregaddr);
		uint8_t data[2] = { 0 , aregaddr & 0xFF};
		struct i2c_msg msg_set[2] = {
			{ NAU85L40_client->addr, I2C_M_NOSTART, 2, (uint8_t *)data },
			{ NAU85L40_client->addr, I2C_M_RD, 2, (uint8_t *)data }
		};

		ret = i2c_transfer(NAU85L40_client->adapter, msg_set, 2);
		if (ret == 2) {
			ret = 0;
			regaddr = aregaddr;
			regval = (data[0]<<8)| data[1];
			snd_err("read sucess reg %d regval %x",regaddr,regval);
		} else {
			snd_err("i2c read failed=%d reg=%d addr = %d", ret, aregaddr,NAU85L40_client->addr);
			ret = -EREMOTEIO;
		}
	}else{
		sscanf(buf, "write:%x-%x", &aregaddr, &val);
		snd_err("write reg 0x%x val 0x%x",aregaddr,val);
		uint8_t data[2] = { 0 , aregaddr & 0xFF};
		uint8_t value[2] = { val >> 8,val & 0xFF};
		struct i2c_msg msg[2] ={
			{ NAU85L40_client->addr, I2C_M_IGNORE_NAK, 2, (uint8_t *)data },
			{ NAU85L40_client->addr, I2C_M_IGNORE_NAK, 2, (uint8_t *)value }
		};

		ret = i2c_transfer(NAU85L40_client->adapter, &msg, 2);
		if (ret == 2) {
			ret = 0;
		} else {
			snd_err("i2c write failed=%d reg=%d addr = %d", ret, aregaddr,NAU85L40_client->addr);
			ret = -EREMOTEIO;
		}

		
	}

	
	return count;
}

static struct device_attribute NAU85L40_attr = __ATTR(reg, S_IRUSR | S_IWUSR, reg_show, reg_store);

static int  NAU85L40_i2c_probe(struct i2c_client *i2c,const struct i2c_device_id *id)
{
	int ret;
	
	printk(KERN_ERR "NAU85L40_i2c_probe\n");
	ret = device_create_file(&i2c->dev, &NAU85L40_attr);
	NAU85L40_client = i2c;
	if (ret) {
		snd_err("Add device file failed");
	}
	
	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_NAU85L40, &NAU85L40_dai, 1);

	return ret;
}

static int  NAU85L40_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	device_remove_file(&client->dev, &NAU85L40_attr);
	return 0;
}

static const struct i2c_device_id NAU85L40_i2c_id[] = {
	{ "nau85l40", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, NAU85L40_i2c_id);

static struct i2c_driver NAU85L40_i2c_driver = {
	.driver = {
		.name = "NAU85L40",
		.owner = THIS_MODULE,
	},
	.probe = NAU85L40_i2c_probe,
	.remove = NAU85L40_i2c_remove,
	.id_table = NAU85L40_i2c_id,
};

static int __init NAU85L40_modinit(void)
{
	int ret = 0;
	
	ret = i2c_add_driver(&NAU85L40_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register NAU85L40 I2C driver: %d\n",
		       ret);
	}
	return ret;
}
module_init(NAU85L40_modinit);

static void __exit NAU85L40_exit(void)
{
	i2c_del_driver(&NAU85L40_i2c_driver);
}
module_exit(NAU85L40_exit);

MODULE_DESCRIPTION("ASoC NAU85L40 driver");
MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_LICENSE("GPL");
