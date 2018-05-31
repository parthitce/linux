/*
 * module att3008 regs
 *
 */

#ifndef __MODULE_ATT3008_H__
#define __MODULE_ATT3008_H__

#include "vuiregs.h"

#define ATT3008_CHIPID_REG          0xe0
#define ATT3008_CHIPID_VAL          0xac15

struct regval_list {
	unsigned int reg;
	unsigned int value;
};
#define ENDMARKER       {0xff, 0xffff}

static const struct regval_list module_adcclk_26m_source[] = {
    {CMU_CLKIN_CTL,      0x5},

	ENDMARKER,
};

static const struct regval_list module_adcclk_24m_source[] = {
    {CMU_CLKIN_CTL,      0x4},

	ENDMARKER,
};

static const struct regval_list module_adcclk_8khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1816},    //mclk from pll,bclk from mclk divisor,8k
    {CMU_CLK_CTL1,       0x003d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_12khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1815},    //mclk from pll,bclk from mclk divisor,12k
    {CMU_CLK_CTL1,       0x103d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_16khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1814},    //mclk from pll,bclk from mclk divisor,16k
    {CMU_CLK_CTL1,       0x203d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_24khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1814},    //mclk from pll,bclk from mclk divisor,24k
    {CMU_CLK_CTL1,       0x203d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_32khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1813},    //mclk from pll,bclk from mclk divisor,32k
    {CMU_CLK_CTL1,       0x303d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_48khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x13},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1811},    //mclk from pll,bclk from mclk divisor,48k
    {CMU_CLK_CTL1,       0x503d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_11khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x03},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1815},    //mclk from pll,bclk from mclk divisor,11.025khz
    {CMU_CLK_CTL1,       0x603d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_22khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x03},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1813},    //mclk from pll,bclk from mclk divisor,22.05khz
    {CMU_CLK_CTL1,       0x703d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_adcclk_44khz_init[] = {
    //{CMU_CLKIN_CTL,      0x5},
    {CMU_AVDDLDO_CTL,   0x0005},    //before enable audiopll, avdd enable should be first
    {CMU_AUDIO_PLL,      0x03},      //p_bclk as pll input
    {CMU_CLK_CTL0,       0x1811},    //mclk from pll,bclk from mclk divisor,44.1khz
    {CMU_CLK_CTL1,       0x803d},
    {CMU_CLK_EN0,        0xff00},
    {CMU_DEV_RST,        0x0},
    {CMU_DEV_RST,        0x1},

	ENDMARKER,
};

static const struct regval_list module_gpio_cfg[] = {
    {AUDIOIF_CTL,       0x160c},    //before setting mfp, set mode first
    {GPIO_CFG0,         0x0},
    {GPIO_CFG1,         0x5552},    //select gpio function as spi if
    {GPIO_CFG2,         0x2222},

	ENDMARKER,
};

static const struct regval_list module_audio_if_start[] = {
    {CMU_CLK_EN1,        0x0001},
    {AUDIOIF_CTL,       0x060e},    //1d 8ch master
    {AUDIOIF_CTL,       0x060f},    //1d 8ch master

	ENDMARKER,
};

static const struct regval_list module_adc_cfg[] = {
	//####### ADC Vref cfg #######
	{ADC_VREF_CTL,      0xd5ce},    //bandgap filter enable,bandgap pd res enable

	//####### MIC BIAS cfg  #######
	{MICBIAS_CTL0,      0x0d0d},
	{MICBIAS_CTL1,      0x0d0d},
	{MICBIAS_CTL2,      0x0d0d},
	{MICBIAS_CTL3,      0x0d0d},

	{ADC0_ANA_CTL1,     0x10c2},
	{ADC1_ANA_CTL1,     0x10c2},
	{ADC2_ANA_CTL1,     0x10c2},
	{ADC3_ANA_CTL1,     0x10c2},
	{ADC4_ANA_CTL1,     0x10c2},
	{ADC5_ANA_CTL1,     0x10c2},
	{ADC6_ANA_CTL1,     0x10c2},
	{ADC7_ANA_CTL1,     0x10c2},

	ENDMARKER,
};

static const struct regval_list module_adc_init[] = {
	{ADC0_ANA_CTL1,     0x1042},
	{ADC1_ANA_CTL1,     0x1042},
	{ADC2_ANA_CTL1,     0x1042},
	{ADC3_ANA_CTL1,     0x1042},
	{ADC4_ANA_CTL1,     0x1042},
	{ADC5_ANA_CTL1,     0x1042},
	{ADC6_ANA_CTL1,     0x1042},
	{ADC7_ANA_CTL1,     0x1042},

	ENDMARKER,
};

static const struct regval_list module_ch_init[] = {
	//adc0~7 was control by adc0 register en,so we should config others adc first,and then enable adc0
	//####### ch1 cfg  #######
	{CH1_DIG_CTL,       0x2201},
	{ADC1_ANA_CTL0,     0x2aaa},
	{ADC1_ANA_CTL1,     0xdf5e},

	//####### ch2 cfg  #######
	{CH2_DIG_CTL,       0x2201},
	{ADC2_ANA_CTL0,     0x2aaa},
	{ADC2_ANA_CTL1,     0xdf5e},

	//####### ch3 cfg  #######
	{CH3_DIG_CTL,       0x2201},
	{ADC3_ANA_CTL0,     0x2aaa},
	{ADC3_ANA_CTL1,     0xdf5e},

	//####### ch4 cfg  #######
	{CH4_DIG_CTL,       0x2201},
	{ADC4_ANA_CTL0,     0x2aaa},
	{ADC4_ANA_CTL1,     0xdf5e},

	//####### ch5 cfg  #######
	{CH5_DIG_CTL,       0x2201},
	{ADC5_ANA_CTL0,     0x2aaa},
	{ADC5_ANA_CTL1,     0xdf5e},

	//####### ch6 cfg  #######
	{CH6_DIG_CTL,       0x2001},
	{ADC6_ANA_CTL0,     0x2aaa},
	{ADC6_ANA_CTL1,     0xdf42},

	//####### ch7 cfg  #######
	{CH7_DIG_CTL,       0x2001},
	{ADC7_ANA_CTL0,     0x2aaa},
	{ADC7_ANA_CTL1,     0xdf42},

	//####### ch0 cfg  #######
	{CH0_DIG_CTL,       0x2201},
	{ADC0_ANA_CTL0,     0x2aaa},
	{ADC0_ANA_CTL1,     0xdf5e},

    ENDMARKER,
};

#endif /* __MODULE_ATT3008_H__ */
