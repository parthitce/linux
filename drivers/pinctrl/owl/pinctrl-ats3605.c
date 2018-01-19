/*
 * Pinctrl driver for Actions ats3605 SoC
 *
 * Copyright (C) 2014 Actions Semi Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include "pinctrl-owl.h"

/* Pinctrl registers offset */
#define MFP_CTL0		(0x0040)
#define MFP_CTL1		(0x0044)
#define MFP_CTL2		(0x0048)
#define MFP_CTL3		(0x004C)
#define PAD_PULLCTL0	(0x0060)
#define PAD_PULLCTL1	(0x0064)
#define PAD_PULLCTL2	(0x0068)
#define PAD_ST0			(0x006C)
#define PAD_ST1			(0x0070)
#define PAD_CTL			(0x0074)
#define PAD_DRV0		(0x0080)
#define PAD_DRV1		(0x0084)
#define PAD_DRV2		(0x0088)


/*
 * Most pins affected by the pinmux can also be GPIOs. Define these first.
 * These must match how the GPIO driver names/numbers its pins.
 */

#define _GPIOA(offset)		  (offset)
#define _GPIOB(offset)		  (32 + (offset))
#define _GPIOC(offset)		  (64 + (offset))
#define _GPIOD(offset)		  (96 + (offset))


/* All non-GPIO pins follow */
#define NUM_GPIOS               (_GPIOD(12) + 1)
#define _PIN(offset)            (NUM_GPIOS + (offset))




/* SIRQ */
#define P_SIRQ0                 _GPIOA(24)
#define P_SIRQ1                 _GPIOA(25)
#define P_SIRQ2                 _GPIOA(26)

/* I2S */
#define P_I2S_D0                _GPIOA(27)
#define P_I2S_BCLK0             _GPIOA(28)
#define P_I2S_LRCLK0            _GPIOA(29)
#define P_I2S_MCLK0             _GPIOA(30)
#define P_I2S_D1                _GPIOA(31)
#define P_I2S_BCLK1             _GPIOB(0)
#define P_I2S_LRCLK1            _GPIOB(1)
#define P_I2S_MCLK1             _GPIOB(2)

/* KEY */
#define P_KS_IN0                _GPIOB(3)
#define P_KS_IN1                _GPIOB(4)
#define P_KS_IN2                _GPIOB(5)
#define P_KS_IN3                _GPIOB(6)
#define P_KS_OUT0               _GPIOB(7)
#define P_KS_OUT1               _GPIOB(8)
#define P_KS_OUT2               _GPIOB(9)

/* LVDS &LCD */
#define P_LVDS_OEP              _GPIOB(10)
#define P_LVDS_OEN              _GPIOB(11)
#define P_LVDS_ODP              _GPIOB(12)
#define P_LVDS_ODN              _GPIOB(13)
#define P_LVDS_OCP              _GPIOB(14)
#define P_LVDS_OCN              _GPIOB(15)
#define P_LVDS_OBP              _GPIOB(16)
#define P_LVDS_OBN              _GPIOB(17)
#define P_LVDS_OAP              _GPIOB(18)
#define P_LVDS_OAN              _GPIOB(19)
#define P_LVDS_EEP              _GPIOB(20)
#define P_LVDS_EEN              _GPIOB(21)
#define P_LVDS_EDP              _GPIOB(22)
#define P_LVDS_EDN              _GPIOB(23)
#define P_LVDS_ECP              _GPIOB(24)
#define P_LVDS_ECN              _GPIOB(25)
#define P_LVDS_EBP              _GPIOB(26)
#define P_LVDS_EBN              _GPIOB(27)
#define P_LVDS_EAP              _GPIOB(28)
#define P_LVDS_EAN              _GPIOB(29)
#define P_LCD0_D18              _GPIOB(30)
#define P_LCD0_D17              _GPIOB(31)
#define P_LCD0_D16              _GPIOC(0)
#define P_LCD0_D9               _GPIOC(1)
#define P_LCD0_D8               _GPIOC(2)
#define P_LCD0_D2               _GPIOC(3)
#define P_LCD0_D1               _GPIOC(4)
#define P_LCD0_D0               _GPIOC(5)

/* SD */
#define P_SD0_D0                _GPIOC(10)
#define P_SD0_D1                _GPIOC(11)
#define P_SD0_D2                _GPIOC(12)
#define P_SD0_D3                _GPIOC(13)
#define P_SD0_D4                _GPIOC(14)
#define P_SD0_D5                _GPIOC(15)
#define P_SD0_D6                _GPIOC(16)
#define P_SD0_D7                _GPIOC(17)
#define P_SD0_CMD               _GPIOC(18)
#define P_SD0_CLK               _GPIOC(19)
#define P_SD1_CMD               _GPIOC(20)
#define P_SD1_CLK               _GPIOC(21)

/* SPI */
#define P_SPI0_SCLK              _GPIOC(22)
#define P_SPI0_SS               _GPIOC(23)
#define P_SPI0_MISO             _GPIOC(24)
#define P_SPI0_MOSI             _GPIOC(25)


/* UART for console */
#define P_UART0_RX              _GPIOC(26)
#define P_UART0_TX              _GPIOC(27)

/* UART for Bluetooth */
#define P_UART2_RX              _GPIOA(14)
#define P_UART2_TX              _GPIOA(15)
#define P_UART2_RTSB            _GPIOA(16)
#define P_UART2_CTSB            _GPIOA(17)

/* UART for 3G */
#define P_UART3_RX              _GPIOA(18)
#define P_UART3_TX              _GPIOA(19)
#define P_UART3_RTSB            _GPIOA(20)
#define P_UART3_CTSB            _GPIOA(21)
#define P_UART4_RX              _GPIOA(22)
#define P_UART4_TX              _GPIOA(23)

/* I2C */
#define P_I2C0_SCLK             _GPIOA(12)
#define P_I2C0_SDATA            _GPIOA(13)
#define P_I2C1_SCLK             _GPIOA(8)
#define P_I2C1_SDATA            _GPIOA(9)
#define P_I2C2_SCLK             _GPIOA(10)
#define P_I2C2_SDATA            _GPIOA(11)

/* Sensor */
#define P_SENSOR1_PCLK            _GPIOD(11)
#define P_SENSOR1_VSYNC           _GPIOD(0)
#define P_SENSOR1_HSYNC           _GPIOD(1)
#define P_SENSOR1_DATA0           _GPIOD(2)
#define P_SENSOR1_DATA1           _GPIOD(3)
#define P_SENSOR1_DATA2           _GPIOD(4)
#define P_SENSOR1_DATA3           _GPIOD(5)
#define P_SENSOR1_DATA4           _GPIOD(6)
#define P_SENSOR1_DATA5           _GPIOD(7)
#define P_SENSOR1_DATA6           _GPIOD(8)
#define P_SENSOR1_DATA7           _GPIOD(9)
#define P_SENSOR1_CKOUT           _GPIOD(10)

/* NAND (1.8v / 3.3v) */
#define P_NAND_D0              _PIN(0)
#define P_NAND_D1              _PIN(1)
#define P_NAND_D2              _PIN(2)
#define P_NAND_D3              _PIN(3)
#define P_NAND_D4              _PIN(4)
#define P_NAND_D5              _PIN(5)
#define P_NAND_D6              _PIN(6)
#define P_NAND_D7              _PIN(7)
#define P_NAND_WRB             _PIN(8)
#define P_NAND_RDB             _GPIOC(29)
#define P_NAND_DQS             _GPIOC(28)
#define P_NAND_RB              _PIN(9)
#define P_NAND_ALE             _GPIOC(31)
#define P_NAND_CLE             _GPIOC(30)
#define P_NAND_CEB0            _GPIOC(6)
#define P_NAND_CEB1            _GPIOC(7)
#define P_NAND_CEB2            _GPIOC(8)
#define P_NAND_CEB3            _GPIOC(9)

/*OTHER*/
#define P_CLKO_25M             _PIN(10)
#define P_RESERVED1            _PIN(11)
#define P_RESERVED2            _PIN(12)
#define P_RESERVED3            _PIN(13)

 
#define _FIRSTPAD		_GPIOA(0)
#define _LASTPAD		P_RESERVED3
#define NUM_PADS		(_LASTPAD - _FIRSTPAD + 1)


#define PINCTRL_PIN(a, b) { .number = a, .name = b }
/* Pad names for the pinmux subsystem */
const struct pinctrl_pin_desc ats3605_pads[] = {
	/* SIRQ */
	PINCTRL_PIN(P_SIRQ0      ,  "P_SIRQ0"),
	PINCTRL_PIN(P_SIRQ1      ,  "P_SIRQ1"),
	PINCTRL_PIN(P_SIRQ2      ,  "P_SIRQ2"),
	/* I2S */
	PINCTRL_PIN(P_I2S_D0     ,  "P_I2S_D0"),
	PINCTRL_PIN(P_I2S_BCLK0  ,  "P_I2S_BCLK0"),
	PINCTRL_PIN(P_I2S_LRCLK0 ,  "P_I2S_LRCLK0"),
	PINCTRL_PIN(P_I2S_MCLK0  ,  "P_I2S_MCLK0"),
	PINCTRL_PIN(P_I2S_D1     ,  "P_I2S_D1"),
	PINCTRL_PIN(P_I2S_BCLK1  ,  "P_I2S_BCLK1"),
	PINCTRL_PIN(P_I2S_LRCLK1 ,  "P_I2S_LRCLK1"),
	PINCTRL_PIN(P_I2S_MCLK1  ,  "P_I2S_MCLK1"),
	/* KEY */
	PINCTRL_PIN(P_KS_IN0     ,  "P_KS_IN0"),
	PINCTRL_PIN(P_KS_IN1     ,  "P_KS_IN1"),
	PINCTRL_PIN(P_KS_IN2     ,  "P_KS_IN2"),
	PINCTRL_PIN(P_KS_IN3     ,  "P_KS_IN3"),
	PINCTRL_PIN(P_KS_OUT0    ,  "P_KS_OUT0"),
	PINCTRL_PIN(P_KS_OUT1    ,  "P_KS_OUT1"),
	PINCTRL_PIN(P_KS_OUT2    ,  "P_KS_OUT2"),
	/* LVDS &LCD */
	PINCTRL_PIN(P_LVDS_OEP   ,  "P_LVDS_OEP"),
	PINCTRL_PIN(P_LVDS_OEN   ,  "P_LVDS_OEN"),
	PINCTRL_PIN(P_LVDS_ODP   ,  "P_LVDS_ODP"),
	PINCTRL_PIN(P_LVDS_ODN   ,  "P_LVDS_ODN"),
	PINCTRL_PIN(P_LVDS_OCP   ,  "P_LVDS_OCP"),
	PINCTRL_PIN(P_LVDS_OCN   ,  "P_LVDS_OCN"),
	PINCTRL_PIN(P_LVDS_OBP   ,  "P_LVDS_OBP"),
	PINCTRL_PIN(P_LVDS_OBN   ,  "P_LVDS_OBN"),
	PINCTRL_PIN(P_LVDS_OAP   ,  "P_LVDS_OAP"),
	PINCTRL_PIN(P_LVDS_OAN   ,  "P_LVDS_OAN"),
	PINCTRL_PIN(P_LVDS_EEP   ,  "P_LVDS_EEP"),
	PINCTRL_PIN(P_LVDS_EEN   ,  "P_LVDS_EEN"),
	PINCTRL_PIN(P_LVDS_EDP   ,  "P_LVDS_EDP"),
	PINCTRL_PIN(P_LVDS_EDN   ,  "P_LVDS_EDN"),
	PINCTRL_PIN(P_LVDS_ECP   ,  "P_LVDS_ECP"),
	PINCTRL_PIN(P_LVDS_ECN   ,  "P_LVDS_ECN"),
	PINCTRL_PIN(P_LVDS_EBP   ,  "P_LVDS_EBP"),
	PINCTRL_PIN(P_LVDS_EBN   ,  "P_LVDS_EBN"),
	PINCTRL_PIN(P_LVDS_EAP   ,  "P_LVDS_EAP"),
	PINCTRL_PIN(P_LVDS_EAN   ,  "P_LVDS_EAN"),
	PINCTRL_PIN(P_LCD0_D18   ,  "P_LCD0_D18"),
	PINCTRL_PIN(P_LCD0_D17   ,  "P_LCD0_D17"),
	PINCTRL_PIN(P_LCD0_D16   ,  "P_LCD0_D16"),
	PINCTRL_PIN(P_LCD0_D9    ,  "P_LCD0_D9"),
	PINCTRL_PIN(P_LCD0_D8    ,  "P_LCD0_D8"),
	PINCTRL_PIN(P_LCD0_D2    ,  "P_LCD0_D2"),
	PINCTRL_PIN(P_LCD0_D1    ,  "P_LCD0_D1"),
	PINCTRL_PIN(P_LCD0_D0    ,  "P_LCD0_D0"),
	/* SD */
	PINCTRL_PIN(P_SD0_D0     ,  "P_SD0_D0"),
	PINCTRL_PIN(P_SD0_D1     ,  "P_SD0_D1"),
	PINCTRL_PIN(P_SD0_D2     ,  "P_SD0_D2"),
	PINCTRL_PIN(P_SD0_D3     ,  "P_SD0_D3"),
	PINCTRL_PIN(P_SD0_D4     ,  "P_SD0_D4"),
	PINCTRL_PIN(P_SD0_D5     ,  "P_SD0_D5"),
	PINCTRL_PIN(P_SD0_D6     ,  "P_SD0_D6"),
	PINCTRL_PIN(P_SD0_D7     ,  "P_SD0_D7"),
	PINCTRL_PIN(P_SD0_CMD    ,  "P_SD0_CMD"),
	PINCTRL_PIN(P_SD0_CLK    ,  "P_SD0_CLK"),
	PINCTRL_PIN(P_SD1_CMD    ,  "P_SD1_CMD"),
	PINCTRL_PIN(P_SD1_CLK    ,  "P_SD1_CLK"),
	/* SPI */
	PINCTRL_PIN(P_SPI0_SCLK  ,  "P_SPI0_SCLK"),
	PINCTRL_PIN(P_SPI0_SS    ,  "P_SPI0_SS"),
	PINCTRL_PIN(P_SPI0_MISO  ,  "P_SPI0_MISO"),
	PINCTRL_PIN(P_SPI0_MOSI  ,  "P_SPI0_MOSI"),
	/* UART for console */
	PINCTRL_PIN(P_UART0_RX   ,  "P_UART0_RX"),
	PINCTRL_PIN(P_UART0_TX   ,  "P_UART0_TX"),
	/* UART for Bluetooth */
	PINCTRL_PIN(P_UART2_RX   ,  "P_UART2_RX"),
	PINCTRL_PIN(P_UART2_TX   ,  "P_UART2_TX"),
	PINCTRL_PIN(P_UART2_RTSB ,  "P_UART2_RTSB"),
	PINCTRL_PIN(P_UART2_CTSB ,  "P_UART2_CTSB"),
	/* UART for 3G */
	PINCTRL_PIN(P_UART3_RX   ,  "P_UART3_RX"),
	PINCTRL_PIN(P_UART3_TX   ,  "P_UART3_TX"),
	PINCTRL_PIN(P_UART3_RTSB ,  "P_UART3_RTSB"),
	PINCTRL_PIN(P_UART3_CTSB ,  "P_UART3_CTSB"),
	PINCTRL_PIN(P_UART4_RX   ,  "P_UART4_RX"),
	PINCTRL_PIN(P_UART4_TX   ,  "P_UART4_TX"),
	/* I2C */
	PINCTRL_PIN(P_I2C0_SCLK  ,  "P_I2C0_SCLK"),
	PINCTRL_PIN(P_I2C0_SDATA ,  "P_I2C0_SDATA"),
	PINCTRL_PIN(P_I2C1_SCLK  ,  "P_I2C1_SCLK"),
	PINCTRL_PIN(P_I2C1_SDATA ,  "P_I2C1_SDATA"),
	PINCTRL_PIN(P_I2C2_SCLK  ,  "P_I2C2_SCLK"),
	PINCTRL_PIN(P_I2C2_SDATA ,  "P_I2C2_SDATA"),
	/* Sensor */
	PINCTRL_PIN(P_SENSOR1_PCLK ,  "P_SENSOR1_PCLK"),
	PINCTRL_PIN(P_SENSOR1_VSYNC,  "P_SENSOR1_VSYNC"),
	PINCTRL_PIN(P_SENSOR1_HSYNC,  "P_SENSOR1_HSYNC"),
	PINCTRL_PIN(P_SENSOR1_DATA0,  "P_SENSOR1_DATA0"),
	PINCTRL_PIN(P_SENSOR1_DATA1,  "P_SENSOR1_DATA1"),
	PINCTRL_PIN(P_SENSOR1_DATA2,  "P_SENSOR1_DATA2"),
	PINCTRL_PIN(P_SENSOR1_DATA3,  "P_SENSOR1_DATA3"),
	PINCTRL_PIN(P_SENSOR1_DATA4,  "P_SENSOR1_DATA4"),
	PINCTRL_PIN(P_SENSOR1_DATA5,  "P_SENSOR1_DATA5"),
	PINCTRL_PIN(P_SENSOR1_DATA6,  "P_SENSOR1_DATA6"),
	PINCTRL_PIN(P_SENSOR1_DATA7,  "P_SENSOR1_DATA7"),
	PINCTRL_PIN(P_SENSOR1_CKOUT,  "P_SENSOR1_CKOUT"),
	/* NAND (1.8v / 3.3v) */
	PINCTRL_PIN(P_NAND_D0    ,  "P_NAND_D0"),
	PINCTRL_PIN(P_NAND_D1    ,  "P_NAND_D1"),
	PINCTRL_PIN(P_NAND_D2    ,  "P_NAND_D2"),
	PINCTRL_PIN(P_NAND_D3    ,  "P_NAND_D3"),
	PINCTRL_PIN(P_NAND_D4    ,  "P_NAND_D4"),
	PINCTRL_PIN(P_NAND_D5    ,  "P_NAND_D5"),
	PINCTRL_PIN(P_NAND_D6    ,  "P_NAND_D6"),
	PINCTRL_PIN(P_NAND_D7    ,  "P_NAND_D7"),
	PINCTRL_PIN(P_NAND_WRB   ,  "P_NAND_WRB"),
	PINCTRL_PIN(P_NAND_RDB   ,  "P_NAND_RDB"),
	PINCTRL_PIN(P_NAND_DQS   ,  "P_NAND_DQS"),
	PINCTRL_PIN(P_NAND_RB    ,  "P_NAND_RB"),
	PINCTRL_PIN(P_NAND_ALE   ,  "P_NAND_ALE"),
	PINCTRL_PIN(P_NAND_CLE   ,  "P_NAND_CLE"),
	PINCTRL_PIN(P_NAND_CEB0  ,  "P_NAND_CEB0"),
	PINCTRL_PIN(P_NAND_CEB1  ,  "P_NAND_CEB1"),
	PINCTRL_PIN(P_NAND_CEB2  ,  "P_NAND_CEB2"),
	PINCTRL_PIN(P_NAND_CEB3  ,  "P_NAND_CEB3"),

	/*other*/
	PINCTRL_PIN(P_CLKO_25M  ,  "P_CLKO_25M"),
	PINCTRL_PIN(P_RESERVED1  ,  "P_RESERVED1"),
	PINCTRL_PIN(P_RESERVED2  ,  "P_RESERVED2"),
	PINCTRL_PIN(P_RESERVED3  ,  "P_RESERVED3"),

};



/*****MFP group data****************************/
enum owl_mux {
	OWL_MUX_SPI0,
	OWL_MUX_SPI1,
	OWL_MUX_SPI2,
	OWL_MUX_SPI3,
	OWL_MUX_I2C0,
	OWL_MUX_I2C1,
	OWL_MUX_I2C2,
	OWL_MUX_UART0,
	OWL_MUX_UART1,
	OWL_MUX_UART2,
	OWL_MUX_UART3,
	OWL_MUX_UART4,
	OWL_MUX_UART5,
	OWL_MUX_SD0,
	OWL_MUX_SD1,
	OWL_MUX_SD2,
	OWL_MUX_NAND,
	OWL_MUX_PWM0,
	OWL_MUX_PWM1,
	OWL_MUX_PWM2,
	OWL_MUX_PWM3,
	OWL_MUX_SIRQ0,
	OWL_MUX_SIRQ1,
	OWL_MUX_SIRQ2,
	OWL_MUX_LCD0,
	OWL_MUX_LVDS,
	OWL_MUX_NOR,
	OWL_MUX_PCM0,
	OWL_MUX_PCM1,
	OWL_MUX_I2S0,
	OWL_MUX_I2S1,
	OWL_MUX_JTAG,
	OWL_MUX_KS,
	OWL_MUX_SENS1,
	OWL_MUX_HDMI,
	OWL_MUX_CLKO_25M,
	OWL_MUX_SPDIF,
	OWL_MUX_MAX,
	OWL_MUX_RESERVED,
};

/*****MFP group data****************************/

/*
** mfp0_31_29
*/
static unsigned int  owl_mfp0_31_29_pads[] = {
	P_LVDS_OEP,
	P_LVDS_OEN,
};

static unsigned int  owl_mfp0_31_29_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_I2C2,
	OWL_MUX_NOR,
	OWL_MUX_SPI2,
	OWL_MUX_UART2,
};

/*
** mfp0_28_26
*/
static unsigned int  owl_mfp0_28_26_pads[] = {
	P_LVDS_ODP,
};

static unsigned int  owl_mfp0_28_26_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_NOR,
	OWL_MUX_SPI2,
	OWL_MUX_UART2,
};

/*
** mfp0_25_23
*/
static unsigned int  owl_mfp0_25_23_pads[] = {
	P_LVDS_ODN,
};

static unsigned int  owl_mfp0_25_23_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_NOR,
	OWL_MUX_SPI2,
	OWL_MUX_UART2,
	OWL_MUX_PWM2,
};

/*
** mfp0_22_20_sirq0
*/
static unsigned int  owl_mfp0_22_20_sirq0_pads[] = {
	P_LVDS_OCP,
};

static unsigned int  owl_mfp0_22_20_sirq0_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_SIRQ0,
	OWL_MUX_NOR,
	OWL_MUX_SPI3,
	OWL_MUX_PCM0,
};

/*
** mfp0_22_20_sirq1
*/
static unsigned int  owl_mfp0_22_20_sirq1_pads[] = {
	P_LVDS_OCN,
};

static unsigned int  owl_mfp0_22_20_sirq1_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_SIRQ1,
	OWL_MUX_NOR,
	OWL_MUX_SPI3,
	OWL_MUX_PCM0,
};

/*
** mfp0_22_20_sirq2
*/
static unsigned int  owl_mfp0_22_20_sirq2_pads[] = {
	P_LVDS_OBP,
};

static unsigned int  owl_mfp0_22_20_sirq2_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_SIRQ2,
	OWL_MUX_NOR,
	OWL_MUX_SPI3,
	OWL_MUX_PCM0,
};


/*
** mfp0_19_17
*/
static unsigned int  owl_mfp0_19_17_pads[] = {
	P_LVDS_OBN,
};

static unsigned int  owl_mfp0_19_17_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_NOR,
	OWL_MUX_SPI3,
	OWL_MUX_PCM0,
};


/*
** mfp0_16_14
*/
static unsigned int  owl_mfp0_16_14_pads[] = {
	P_LVDS_OAP,
	P_LVDS_OAN,
};

static unsigned int  owl_mfp0_16_14_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_LCD0,
	OWL_MUX_I2C1,
	OWL_MUX_NOR,
	OWL_MUX_UART4,
};

/*
** mfp0_5
*/
static unsigned int  owl_mfp0_5_pads[] = {
	P_I2S_LRCLK0,
	P_I2S_MCLK0,
};

static unsigned int  owl_mfp0_5_funcs[] = {
	OWL_MUX_I2S0,
	OWL_MUX_PCM1,
};

/*
** mfp0_2_i2s0
*/
static unsigned int  owl_mfp0_2_i2s0_pads[] = {
	P_I2S_LRCLK0,
};

static unsigned int  owl_mfp0_2_i2s0_funcs[] = {
	OWL_MUX_I2S0,
	OWL_MUX_PCM0,
};

/*
** mfp0_2_i2s1
*/
static unsigned int  owl_mfp0_2_i2s1_pads[] = {
	P_I2S_LRCLK1,
	P_I2S_LRCLK1,
	P_I2S_MCLK1,
};

static unsigned int  owl_mfp0_2_i2s1_funcs[] = {
	OWL_MUX_I2S1,
	OWL_MUX_PCM0,
};


/*
** mfp1_31_29_ks_in0
*/
static unsigned int  owl_mfp1_31_29_ks_in0_pads[] = {
	P_KS_IN0,
	P_KS_IN2,
};

static unsigned int  owl_mfp1_31_29_ks_in0_funcs[] = {
	OWL_MUX_KS,
	OWL_MUX_JTAG,
	OWL_MUX_NOR,
	OWL_MUX_PWM0,
};

/*
** mfp1_31_29_ks_in1
*/
static unsigned int  owl_mfp1_31_29_ks_in1_pads[] = {
	P_KS_IN1,
};

static unsigned int  owl_mfp1_31_29_ks_in1_funcs[] = {
	OWL_MUX_KS,
	OWL_MUX_JTAG,
	OWL_MUX_NOR,
	OWL_MUX_PWM1,
};


/*
** mfp1_28_26_ks_in3
*/
static unsigned int  owl_mfp1_28_26_ks_in3_pads[] = {
	P_KS_IN3,
};

static unsigned int  owl_mfp1_28_26_ks_in3_funcs[] = {
	OWL_MUX_KS,
	OWL_MUX_JTAG,
	OWL_MUX_NOR,
	OWL_MUX_PWM1,
	OWL_MUX_SD1,
};

/*
** mfp1_28_26_ks_out0
*/
static unsigned int  owl_mfp1_28_26_ks_out0_pads[] = {
	P_KS_OUT0,
};

static unsigned int  owl_mfp1_28_26_ks_out0_funcs[] = {
	OWL_MUX_KS,
	OWL_MUX_UART5,
	OWL_MUX_NOR,
	OWL_MUX_PWM2,
	OWL_MUX_SD0,
};

/*
** mfp1_28_26_ks_out1
*/
static unsigned int  owl_mfp1_28_26_ks_out1_pads[] = {
	P_KS_OUT1,
};

static unsigned int  owl_mfp1_28_26_ks_out1_funcs[] = {
	OWL_MUX_KS,
	OWL_MUX_JTAG,
	OWL_MUX_NOR,
	OWL_MUX_PWM3,
	OWL_MUX_SD0,
};


/*
** mfp1_25_23
*/
static unsigned int  owl_mfp1_25_23_pads[] = {
	P_KS_OUT2,
};

static unsigned int  owl_mfp1_25_23_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_KS,
	OWL_MUX_RESERVED,
	OWL_MUX_PWM2,
	OWL_MUX_UART5,
};

/*
** mfp1_22
*/
static unsigned int  owl_mfp1_22_pads[] = {
	P_RESERVED1,

};

static unsigned int  owl_mfp1_22_funcs[] = {
	OWL_MUX_LVDS,
	OWL_MUX_LCD0,
};

/*
** mfp1_21
*/
static unsigned int  owl_mfp1_21_pads[] = {
	P_LVDS_EEP,
	P_LVDS_EEN,
	P_LVDS_EDP,
	P_LVDS_EDN,
	P_LVDS_ECP,
	P_LVDS_ECN,
	P_LVDS_EBP,
	P_LVDS_EBN,
	P_LVDS_EAP,
	P_LVDS_EAN,
};

static unsigned int  owl_mfp1_21_funcs[] = {
	OWL_MUX_LVDS,
	OWL_MUX_LCD0,
};

/*
** mfp1_16_15
*/
static unsigned int  owl_mfp1_16_15_pads[] = {
	P_LCD0_D17,
};

static unsigned int  owl_mfp1_16_15_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_SD0,
	OWL_MUX_SD1,
	OWL_MUX_NOR,
};

/*
** mfp1_14_13_sd1_clkb
*/
static unsigned int  owl_mfp1_14_13_sd1_clkb_pads[] = {
	P_LCD0_D16,
};

static unsigned int  owl_mfp1_14_13_sd1_clkb_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_SD1,
	OWL_MUX_RESERVED,
	OWL_MUX_NOR,
};
/*
** mfp1_14_13_sd1_clk
*/
static unsigned int  owl_mfp1_14_13_sd1_clk_pads[] = {
	P_LCD0_D16,
};

static unsigned int  owl_mfp1_14_13_sd1_clk_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_RESERVED,
	OWL_MUX_SD1,
	OWL_MUX_NOR,
};


/*
** mfp1_12_11
*/
static unsigned int  owl_mfp1_12_11_pads[] = {
	P_LCD0_D9,
};

static unsigned int  owl_mfp1_12_11_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_SD0,
	OWL_MUX_SD1,
};


/*
** mfp1_10
*/
static unsigned int  owl_mfp1_10_pads[] = {
	P_LCD0_D8,
	P_LCD0_D0,
	P_LCD0_D1,
};

static unsigned int  owl_mfp1_10_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_SD1,
};

/*
** mfp1_9
*/
static unsigned int  owl_mfp1_9_pads[] = {
	P_LCD0_D18,
	P_LCD0_D2,
};

static unsigned int  owl_mfp1_9_funcs[] = {
	OWL_MUX_LCD0,
	OWL_MUX_I2C1,
};
/*
** mfp1_8_0 reserved
*/


/*
** mfp2_31_27 reserved
*/


/*
** mfp2_26
*/
static unsigned int  owl_mfp2_26_pads[] = {
	P_UART2_RX,
	P_UART2_TX,
};

static unsigned int  owl_mfp2_26_funcs[] = {
	OWL_MUX_UART2,
	OWL_MUX_SENS1,
};
/*
** mfp2_25_24
*/
static unsigned int  owl_mfp2_25_24_pads[] = {
	P_UART2_CTSB,
	P_UART2_RTSB,
};

static unsigned int  owl_mfp2_25_24_funcs[] = {
	OWL_MUX_UART2,
	OWL_MUX_UART0,
	OWL_MUX_SENS1,
};

/*
** mfp2_23_irq0
*/
static unsigned int  owl_mfp2_23_irq0_pads[] = {
	P_UART3_RX,
};

static unsigned int  owl_mfp2_23_irq0_funcs[] = {
	OWL_MUX_UART3,
	OWL_MUX_SIRQ0,
};
/*
** mfp2_23_irq1
*/
static unsigned int  owl_mfp2_23_irq1_pads[] = {
	P_UART3_TX,
};

static unsigned int  owl_mfp2_23_irq1_funcs[] = {
	OWL_MUX_UART3,
	OWL_MUX_SIRQ1,
};


/*
** mfp2_22
*/
static unsigned int  owl_mfp2_22_pads[] = {
	P_UART3_RTSB,
};

static unsigned int  owl_mfp2_22_funcs[] = {
	OWL_MUX_UART3,
	OWL_MUX_UART5,
};

/*
** mfp2_21_20
*/
static unsigned int  owl_mfp2_21_20_pads[] = {
	P_UART3_CTSB,
};

static unsigned int  owl_mfp2_21_20_funcs[] = {
	OWL_MUX_UART3,
	OWL_MUX_UART5,
	OWL_MUX_PWM2,
};

/*
** mfp2_19_17
*/
static unsigned int  owl_mfp2_19_17_pads[] = {
	P_SD0_D0,
};

static unsigned int  owl_mfp2_19_17_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_NOR,
	OWL_MUX_RESERVED,
	OWL_MUX_JTAG,
	OWL_MUX_UART2,
	OWL_MUX_UART5,
};

/*
** mfp2_16_14
*/
static unsigned int  owl_mfp2_16_14_pads[] = {
	P_SD0_D1,
};

static unsigned int  owl_mfp2_16_14_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_NOR,
	OWL_MUX_RESERVED,
	OWL_MUX_RESERVED,
	OWL_MUX_UART2,
	OWL_MUX_UART5,
};

/*
** mfp2_13_11
*/
static unsigned int  owl_mfp2_13_11_pads[] = {
	P_SD0_D2,
	P_SD0_D3,
};

static unsigned int  owl_mfp2_13_11_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_NOR,
	OWL_MUX_RESERVED,
	OWL_MUX_JTAG,
	OWL_MUX_UART2,
	OWL_MUX_UART1,
};
/*
** mfp2_10_9
*/
static unsigned int  owl_mfp2_10_9_pads[] = {
	P_SD0_D4,
	P_SD0_D5,
	P_SD0_D6,
	P_SD0_D7,
};

static unsigned int  owl_mfp2_10_9_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_RESERVED,
	OWL_MUX_RESERVED,
	OWL_MUX_SD1,
};

/*
** mfp2_8_7
*/
static unsigned int  owl_mfp2_8_7_pads[] = {
	P_SD0_CMD,
};

static unsigned int  owl_mfp2_8_7_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_NOR,
	OWL_MUX_RESERVED,
	OWL_MUX_JTAG,
};

/*
** mfp2_6_5
*/
static unsigned int  owl_mfp2_6_5_pads[] = {
	P_SD0_CLK,
};

static unsigned int  owl_mfp2_6_5_funcs[] = {
	OWL_MUX_SD0,
	OWL_MUX_RESERVED,
	OWL_MUX_JTAG,
};

/*
** mfp2_2
*/
static unsigned int  owl_mfp2_2_pads[] = {
	P_SPI0_SCLK,
};

static unsigned int  owl_mfp2_2_funcs[] = {
	OWL_MUX_SPI0,
	OWL_MUX_NOR,
};

/*
** mfp2_1_0
*/
static unsigned int  owl_mfp2_1_0_pads[] = {
	P_SPI0_SS,
};

static unsigned int  owl_mfp2_1_0_funcs[] = {
	OWL_MUX_SPI0,
	OWL_MUX_NOR,
	OWL_MUX_SIRQ2,
};



/*
** mfp3_31
*/
static unsigned int  owl_mfp3_31_pads[] = {
	P_CLKO_25M,
};

static unsigned int  owl_mfp3_31_funcs[] = {
	OWL_MUX_RESERVED,
	OWL_MUX_CLKO_25M,
};

/*
** mfp3_30_28
*/
static unsigned int  owl_mfp3_30_28_pads[] = {
	P_UART0_RX,
};

static unsigned int  owl_mfp3_30_28_funcs[] = {
	OWL_MUX_UART0,
	OWL_MUX_UART2,
	OWL_MUX_SPI1,
	OWL_MUX_I2C0,
	OWL_MUX_I2S1,
	OWL_MUX_PCM1,
};

/*
** mfp3_27_26
*/
static unsigned int  owl_mfp3_27_26_pads[] = {
	P_NAND_CEB2,
	P_NAND_CEB3,
};

static unsigned int  owl_mfp3_27_26_funcs[] = {
	OWL_MUX_NAND,
	OWL_MUX_RESERVED,
	OWL_MUX_I2C1,
};

/*
** mfp3_25 reserved
*/

/*
** mfp3_24_23
*/
static unsigned int  owl_mfp3_24_23_pads[] = {
	P_NAND_D0,
	P_NAND_D1,
	P_NAND_D2,
	P_NAND_D3,
	P_NAND_D4,
	P_NAND_D5,
	P_NAND_D6,
	P_NAND_D7,
};

static unsigned int  owl_mfp3_24_23_funcs[] = {
	OWL_MUX_NAND,
	OWL_MUX_SD2,
	OWL_MUX_NOR,
};

/*
** mfp3_22
*/
static unsigned int  owl_mfp3_22_pads[] = {
	P_RESERVED3,
};

static unsigned int  owl_mfp3_22_funcs[] = {
	OWL_MUX_HDMI,
	OWL_MUX_SENS1,
};


/*
** mfp3_21_19
*/
static unsigned int  owl_mfp3_21_19_pads[] = {
	P_UART0_TX,
};

static unsigned int  owl_mfp3_21_19_funcs[] = {
	OWL_MUX_UART0,
	OWL_MUX_UART2,
	OWL_MUX_SPI1,
	OWL_MUX_I2C0,
	OWL_MUX_SPDIF,
	OWL_MUX_PCM1,
	OWL_MUX_I2S1,
};

/*
** mfp3_18_16
*/
static unsigned int  owl_mfp3_18_16_pads[] = {
	P_I2C0_SCLK,
	P_I2C0_SDATA,
};

static unsigned int  owl_mfp3_18_16_funcs[] = {
	OWL_MUX_I2C0,
	OWL_MUX_UART2,
	OWL_MUX_I2C1,
	OWL_MUX_UART1,
	OWL_MUX_SPI1,
	OWL_MUX_NOR,
};

/*
** mfp3_15_13 reserved
*/

/*
** mfp3_12
*/
static unsigned int  owl_mfp3_12_pads[] = {
	P_SENSOR1_PCLK,
	P_SENSOR1_VSYNC,
	P_SENSOR1_HSYNC,
	P_SENSOR1_CKOUT,
};

static unsigned int  owl_mfp3_12_funcs[] = {
	OWL_MUX_SENS1,
	OWL_MUX_NOR,
};

/*
** mfp3_11_9
*/
static unsigned int  owl_mfp3_11_9_pads[] = {
	P_SENSOR1_DATA1,
	P_SENSOR1_DATA0,
};

static unsigned int  owl_mfp3_11_9_funcs[] = {
	OWL_MUX_SENS1,
	OWL_MUX_SD1,
	OWL_MUX_SPI0,
	OWL_MUX_NOR,
	OWL_MUX_UART3,
};

/*
** mfp3_8_6
*/
static unsigned int  owl_mfp3_8_6_pads[] = {
	P_SENSOR1_DATA3,
	P_SENSOR1_DATA2,
};

static unsigned int  owl_mfp3_8_6_funcs[] = {
	OWL_MUX_SENS1,
	OWL_MUX_SD1,
	OWL_MUX_SPI0,
	OWL_MUX_NOR,
	OWL_MUX_UART3,
};

/*
** mfp3_5_3
*/
static unsigned int  owl_mfp3_5_3_pads[] = {
	P_SENSOR1_DATA5,
	P_SENSOR1_DATA4,
};

static unsigned int  owl_mfp3_5_3_funcs[] = {
	OWL_MUX_SENS1,
	OWL_MUX_SD1,
	OWL_MUX_PCM0,
	OWL_MUX_NOR,
	OWL_MUX_UART1,
};

/*
** mfp3_2_0
*/
static unsigned int  owl_mfp3_2_0_pads[] = {
	P_SENSOR1_DATA7,
	P_SENSOR1_DATA6,
};

static unsigned int  owl_mfp3_2_0_funcs[] = {
	OWL_MUX_SENS1,
	OWL_MUX_I2C1,
	OWL_MUX_PCM0,
	OWL_MUX_NOR,
	OWL_MUX_UART4,
};


/*sirq0 dummy group*/
static unsigned int  owl_sirq0_dummy_pads[] = {
	P_SIRQ0,
};

static unsigned int  owl_sirq0_dummy_funcs[] = {
	OWL_MUX_SIRQ0,
};

/*sirq1 dummy group*/
static unsigned int  owl_sirq1_dummy_pads[] = {
	P_SIRQ1,
};

static unsigned int  owl_sirq1_dummy_funcs[] = {
	OWL_MUX_SIRQ1,
};

/*sirq2 dummy group*/
static unsigned int  owl_sirq2_dummy_pads[] = {
	P_SIRQ2,
};

static unsigned int  owl_sirq2_dummy_funcs[] = {
	OWL_MUX_SIRQ2,
};

/*i2s0 dummy group*/
static unsigned int  owl_i2s0_dummy_pads[] = {
	P_I2S_D0,
};

static unsigned int  owl_i2s0_dummy_funcs[] = {
	OWL_MUX_I2S0,
};

/*i2s1 dummy group*/
static unsigned int  owl_i2s1_dummy_pads[] = {
	P_I2S_D1,
};

static unsigned int  owl_i2s1_dummy_funcs[] = {
	OWL_MUX_I2S1,
};

/*sd1 dummy group*/
static unsigned int  owl_sd1_dummy_pads[] = {
	P_SD1_CLK,
	P_SD1_CMD,
};

static unsigned int  owl_sd1_dummy_funcs[] = {
	OWL_MUX_SD1,
};


/*spi0 dummy group*/
static unsigned int  owl_spi0_dummy_pads[] = {
	P_SPI0_MISO,
	P_SPI0_MOSI,
};

static unsigned int  owl_spi0_dummy_funcs[] = {
	OWL_MUX_SPI0,
};

/*
** uart4 dummy group
*/
static unsigned int  owl_uart4_dummy_pads[] = {
	P_UART4_RX,
	P_UART4_TX,
};

static unsigned int  owl_uart4_dummy_funcs[] = {
	OWL_MUX_UART4,
};

/*
** i2c1 dummy group
*/
static unsigned int  owl_i2c1_dummy_pads[] = {
	P_I2C1_SCLK,
	P_I2C1_SDATA,
};

static unsigned int  owl_i2c1_dummy_funcs[] = {
	OWL_MUX_I2C1,
};

/*
** i2c2 dummy group
*/
static unsigned int  owl_i2c2_dummy_pads[] = {
	P_I2C2_SCLK,
	P_I2C2_SDATA,
};

static unsigned int  owl_i2c2_dummy_funcs[] = {
	OWL_MUX_I2C2,
};

/*
** nand dummy group
*/
static unsigned int  owl_nand_dummy_pads[] = {
	P_NAND_WRB,
	P_NAND_RDB,
	P_NAND_DQS,
	P_NAND_RB,
	P_NAND_ALE,
	P_NAND_CLE,
	P_NAND_CEB0,
	P_NAND_CEB1,
	P_NAND_CEB2,
	P_NAND_CEB3,
};

static unsigned int  owl_nand_dummy_funcs[] = {
	OWL_MUX_NAND,
};


/*****End MFP group data****************************/

/*****PADDRV group data****************************/

/*PAD_DRV0*/

static unsigned int  owl_paddrv0_25_24_pads[] = {
	P_LVDS_OEP,
	P_LVDS_OEN,
};

static unsigned int  owl_paddrv0_23_22_pads[] = {
	P_LVDS_ODP,
};

static unsigned int  owl_paddrv0_21_20_pads[] = {
	P_LVDS_ODN,
};

static unsigned int  owl_paddrv0_19_18_pads[] = {
	P_LVDS_OCP,
	P_LVDS_OCN,
	P_LVDS_OBP,
};

static unsigned int  owl_paddrv0_17_16_pads[] = {
	P_LVDS_OBN,
};

static unsigned int  owl_paddrv0_15_14_pads[] = {
	P_LVDS_OAP,
	P_LVDS_OAN,
};

static unsigned int  owl_paddrv0_13_12_pads[] = {
	P_SIRQ0,
	P_SIRQ1,
	P_SIRQ2,
};

static unsigned int  owl_paddrv0_11_10_pads[] = {
	P_I2S_D0,
};

static unsigned int  owl_paddrv0_9_8_pads[] = {
	P_I2S_BCLK0,
};

static unsigned int  owl_paddrv0_7_6_pads[] = {
	P_I2S_LRCLK0,
	P_I2S_MCLK0,
	P_I2S_D1,
};

static unsigned int  owl_paddrv0_5_4_pads[] = {
	P_I2S_BCLK1,
	P_I2S_LRCLK1,
	P_I2S_MCLK1,
};


static unsigned int  owl_paddrv0_1_0_pads[] = {
	P_KS_IN0,
	P_KS_IN1,
	P_KS_IN2,
	P_KS_IN3,
};

/*PAD_DRV1*/
static unsigned int  owl_paddrv1_31_30_pads[] = {
	P_KS_OUT0,
	P_KS_OUT1,
	P_KS_OUT2,
};

static unsigned int  owl_paddrv1_29_28_pads[] = {
	P_LVDS_EEP,
	P_LVDS_EEN,
	P_LVDS_EDP,
	P_LVDS_EDN,
	P_LVDS_ECP,
	P_LVDS_ECN,
	P_LVDS_EBP,
	P_LVDS_EBN,
	P_LVDS_EAP,
	P_LVDS_EAN,
};

static unsigned int  owl_paddrv1_27_26_pads[] = {
	P_LCD0_D18,
	P_LCD0_D17,
	P_LCD0_D16,
	P_LCD0_D9,
	P_LCD0_D8,
	P_LCD0_D2,
	P_LCD0_D1,
	P_LCD0_D0,
};

static unsigned int  owl_paddrv1_23_22_pads[] = {
	P_SD0_D0,
	P_SD0_D1,
	P_SD0_D2,
	P_SD0_D3,
};

static unsigned int  owl_paddrv1_21_20_pads[] = {
	P_SD0_D4,
	P_SD0_D5,
	P_SD0_D6,
	P_SD0_D7,
};

static unsigned int  owl_paddrv1_19_18_pads[] = {
	P_SD0_CMD,
};

static unsigned int  owl_paddrv1_17_16_pads[] = {
	P_SD0_CLK,
};

static unsigned int  owl_paddrv1_15_14_pads[] = {
	P_SD1_CMD,
};

static unsigned int  owl_paddrv1_13_12_pads[] = {
	P_SD1_CLK,
};

static unsigned int  owl_paddrv1_11_10_pads[] = {
	P_SPI0_SS,
	P_SPI0_MISO,
	P_SPI0_MOSI,
	P_SPI0_SCLK
};

/*PAD_DRV2*/
static unsigned int  owl_paddrv2_31_30_pads[] = {
	P_UART0_RX,
};

static unsigned int  owl_paddrv2_29_28_pads[] = {
	P_UART0_TX,
};

static unsigned int  owl_paddrv2_27_pads[] = {
	P_UART2_RX,
	P_UART2_TX,
	P_UART2_RTSB,
	P_UART2_CTSB,
};

static unsigned int  owl_paddrv2_26_pads[] = {
	P_UART3_RX,
	P_UART3_TX,
	P_UART3_RTSB,
	P_UART3_CTSB,
};

static unsigned int  owl_paddrv2_25_pads[] = {
	P_UART4_RX,
	P_UART4_TX,
};

static unsigned int  owl_paddrv2_24_23_pads[] = {
	P_I2C0_SCLK,
	P_I2C0_SDATA,
};

static unsigned int  owl_paddrv2_22_21_pads[] = {
	P_I2C1_SCLK,
	P_I2C1_SDATA,
	P_I2C2_SCLK,
	P_I2C2_SDATA,
};

static unsigned int  owl_paddrv2_19_18_pads[] = {
	P_SENSOR1_PCLK,
};

static unsigned int  owl_paddrv2_17_16_pads[] = {
	P_SENSOR1_VSYNC,
	P_SENSOR1_HSYNC,
};

static unsigned int  owl_paddrv2_15_14_pads[] = {
	P_SENSOR1_DATA0,
	P_SENSOR1_DATA1,
	P_SENSOR1_DATA2,
	P_SENSOR1_DATA3,
};

static unsigned int  owl_paddrv2_13_12_pads[] = {
	P_SENSOR1_DATA4,
	P_SENSOR1_DATA5,
	P_SENSOR1_DATA6,
	P_SENSOR1_DATA7,
};

static unsigned int  owl_paddrv2_11_10_pads[] = {
	P_SENSOR1_CKOUT,

};

static unsigned int  owl_paddrv2_9_8_pads[] = {
	P_NAND_D0,
	P_NAND_D1,      
	P_NAND_D2,   
	P_NAND_D3,    
	P_NAND_D4,        
	P_NAND_D5,             
	P_NAND_D6,             
	P_NAND_D7,              
	P_NAND_WRB,           
	P_NAND_RDB,             
	P_NAND_DQS,           
	P_NAND_RB,             
	P_NAND_ALE,            
	P_NAND_CLE,            
	P_NAND_CEB0,           
	P_NAND_CEB1,          
	P_NAND_CEB2,      
	P_NAND_CEB3, 
};


/*****End PADDRV group data****************************/

#define MUX_PG(group_name, mfpctl_regn, mfpctl_sft, mfpctl_w)		\
	{								\
		.name = #group_name,					\
		.pads = owl_##group_name##_pads,			\
		.padcnt = ARRAY_SIZE(owl_##group_name##_pads),		\
		.funcs = owl_##group_name##_funcs,			\
		.nfuncs = ARRAY_SIZE(owl_##group_name##_funcs),		\
		.mfpctl_reg = MFP_CTL##mfpctl_regn,			\
		.mfpctl_shift = mfpctl_sft,				\
		.mfpctl_width = mfpctl_w,				\
		.paddrv_reg = -1,					\
	}

#define MUX_PG_DUMMY(group_name)					\
	{								\
		.name = #group_name,					\
		.pads = owl_##group_name##_pads,			\
		.padcnt = ARRAY_SIZE(owl_##group_name##_pads),		\
		.funcs = owl_##group_name##_funcs,			\
		.nfuncs = ARRAY_SIZE(owl_##group_name##_funcs),		\
		.mfpctl_reg = -1,					\
		.paddrv_reg = -1,					\
	}

#define PADDRV_PG(group_name, paddrv_regn, paddrv_sft)			\
	{								\
		.name = #group_name,					\
		.pads = owl_##group_name##_pads,			\
		.padcnt = ARRAY_SIZE(owl_##group_name##_pads),		\
		.paddrv_reg = PAD_DRV##paddrv_regn,			\
		.paddrv_shift = paddrv_sft,				\
		.paddrv_width = 2,					\
		.mfpctl_reg = -1,					\
	}

#define PADDRV_PG1(group_name, paddrv_regn, paddrv_sft)			\
	{								\
		.name = #group_name,					\
		.pads = owl_##group_name##_pads,			\
		.padcnt = ARRAY_SIZE(owl_##group_name##_pads),		\
		.paddrv_reg = PAD_DRV##paddrv_regn,			\
		.paddrv_shift = paddrv_sft,				\
		.paddrv_width = 1,					\
		.mfpctl_reg = -1,					\
	}

/*
**
** all pinctrl groups of atm7059 board
**
*/
const struct owl_group ats3605_groups[] = {
	MUX_PG(mfp0_31_29, 0, 29, 3),
	MUX_PG(mfp0_28_26, 0, 26, 3),
	MUX_PG(mfp0_25_23, 0, 23, 3),
	MUX_PG(mfp0_22_20_sirq0, 0, 20, 3),
	MUX_PG(mfp0_22_20_sirq1, 0, 20, 3),
	MUX_PG(mfp0_22_20_sirq2, 0, 20, 3),
	MUX_PG(mfp0_19_17, 0, 17, 3),
	MUX_PG(mfp0_16_14, 0, 14, 3),
	MUX_PG(mfp0_5, 0, 5, 1),
	MUX_PG(mfp0_2_i2s0, 0, 2, 1),
	MUX_PG(mfp0_2_i2s1, 0, 2, 1),

	MUX_PG(mfp1_31_29_ks_in0, 1, 29, 3),
	MUX_PG(mfp1_31_29_ks_in1, 1, 29, 3),
	MUX_PG(mfp1_28_26_ks_in3, 1, 26, 3),
	MUX_PG(mfp1_28_26_ks_out0, 1, 26, 3),
	MUX_PG(mfp1_28_26_ks_out1, 1, 26, 3),
	MUX_PG(mfp1_25_23, 1, 23, 3),
	MUX_PG(mfp1_22, 1, 22, 1),
	MUX_PG(mfp1_21, 1, 21, 1),
	MUX_PG(mfp1_16_15, 1, 15, 2),
	MUX_PG(mfp1_14_13_sd1_clkb, 1, 13, 2),
	MUX_PG(mfp1_14_13_sd1_clk, 1, 13, 3),
	MUX_PG(mfp1_12_11, 1, 11, 2),
	MUX_PG(mfp1_10, 1, 10, 1),
	MUX_PG(mfp1_9, 1, 9, 1),

	MUX_PG(mfp2_26, 2, 26, 1),
	MUX_PG(mfp2_25_24, 2, 24, 2),
	MUX_PG(mfp2_23_irq0, 2, 23, 1),
	MUX_PG(mfp2_23_irq1, 2, 23, 1),
	MUX_PG(mfp2_22, 2, 22, 1),
	MUX_PG(mfp2_21_20, 2, 20, 2),
	MUX_PG(mfp2_19_17, 2, 17, 3),
	MUX_PG(mfp2_16_14, 2, 14, 3),
	MUX_PG(mfp2_13_11, 2, 11, 3),
	MUX_PG(mfp2_10_9, 2, 9, 2),
	MUX_PG(mfp2_8_7, 2, 7, 2),
	MUX_PG(mfp2_6_5, 2, 5, 2),
	MUX_PG(mfp2_2, 2, 2, 1),
	MUX_PG(mfp2_1_0, 2, 0, 2),

	MUX_PG(mfp3_31, 3, 31, 1),
	MUX_PG(mfp3_30_28, 3, 28, 3),
	MUX_PG(mfp3_27_26, 3, 26, 2),
	MUX_PG(mfp3_24_23, 3, 23, 2),
	MUX_PG(mfp3_22, 3, 22, 1),
	MUX_PG(mfp3_21_19, 3, 19, 3),
	MUX_PG(mfp3_18_16, 3, 16, 3),
	MUX_PG(mfp3_12, 3, 12, 1),
	MUX_PG(mfp3_11_9, 3, 9, 3),
	MUX_PG(mfp3_8_6, 3, 6, 3),
	MUX_PG(mfp3_5_3, 3, 3, 3),
	MUX_PG(mfp3_2_0, 3, 0, 3),

	MUX_PG_DUMMY(sirq0_dummy),
	MUX_PG_DUMMY(sirq1_dummy),
	MUX_PG_DUMMY(sirq2_dummy),
	MUX_PG_DUMMY(i2s0_dummy),
	MUX_PG_DUMMY(i2s1_dummy),
	MUX_PG_DUMMY(sd1_dummy),
	MUX_PG_DUMMY(spi0_dummy),
	MUX_PG_DUMMY(uart4_dummy),
	MUX_PG_DUMMY(nand_dummy),
	MUX_PG_DUMMY(i2c1_dummy),
	MUX_PG_DUMMY(i2c2_dummy),
	
	PADDRV_PG(paddrv0_25_24, 0, 28),
	PADDRV_PG(paddrv0_23_22, 0, 22),
	PADDRV_PG(paddrv0_21_20, 0, 20),
	PADDRV_PG(paddrv0_19_18, 0, 18),
	PADDRV_PG(paddrv0_17_16, 0, 16),
	PADDRV_PG(paddrv0_15_14, 0, 14),
	PADDRV_PG(paddrv0_13_12, 0, 12),
	PADDRV_PG(paddrv0_11_10, 0, 10),
	PADDRV_PG(paddrv0_9_8, 0, 8),
	PADDRV_PG(paddrv0_7_6, 0, 6),
	PADDRV_PG(paddrv0_5_4, 0, 4),
	PADDRV_PG(paddrv0_1_0, 0, 0),

	PADDRV_PG(paddrv1_31_30, 1, 30),
	PADDRV_PG(paddrv1_29_28, 1, 28),
	PADDRV_PG(paddrv1_27_26, 1, 26),
	PADDRV_PG(paddrv1_23_22, 1, 22),
	PADDRV_PG(paddrv1_21_20, 1, 20),
	PADDRV_PG(paddrv1_19_18, 1, 18),
	PADDRV_PG(paddrv1_17_16, 1, 16),
	PADDRV_PG(paddrv1_15_14, 1, 14),
	PADDRV_PG(paddrv1_13_12, 1, 12),
	PADDRV_PG(paddrv1_11_10, 1, 10),

	PADDRV_PG(paddrv2_31_30, 2, 30),
	PADDRV_PG(paddrv2_29_28, 2, 28),
	PADDRV_PG1(paddrv2_27, 2, 27),
	PADDRV_PG1(paddrv2_26, 2, 26),
	PADDRV_PG1(paddrv2_25, 2, 25),
	PADDRV_PG(paddrv2_24_23, 2, 23),
	PADDRV_PG(paddrv2_22_21, 2, 21),
	PADDRV_PG(paddrv2_19_18, 2, 18),
	PADDRV_PG(paddrv2_17_16, 2, 16),
	PADDRV_PG(paddrv2_15_14, 2, 14),
	PADDRV_PG(paddrv2_13_12, 2, 12),
	PADDRV_PG(paddrv2_11_10, 2, 10),
	PADDRV_PG(paddrv2_9_8, 2, 8),

};

static const char * const spi0_groups[] = {
	"mfp2_2",
	"mfp2_1_0",
	"mfp3_11_9",
	"mfp3_8_6",
	"spi0_dummy",
};

static const char * const spi1_groups[] = {
	"mfp3_30_28",
	"mfp3_21_19",
	"mfp3_18_16",
};

static const char * const spi2_groups[] = {
	"mfp0_31_29",
	"mfp0_28_26",
	"mfp0_25_23",
};

static const char * const spi3_groups[] = {
	"mfp0_22_20_sirq0",
	"mfp0_22_20_sirq1",
	"mfp0_22_20_sirq2",
	"mfp0_19_17",
};

static const char * const i2c0_groups[] = {
	"mfp3_30_28",
	"mfp3_21_19",
	"mfp3_18_16",
};

static const char * const i2c1_groups[] = {
	"mfp0_16_14",
	"mfp1_9",
	"mfp3_27_26",
	"mfp3_18_16",
	"mfp3_2_0",
	"i2c1_dummy",
};

static const char * const i2c2_groups[] = {
	"mfp0_31_29",
	"i2c2_dummy",
};


static const char * const uart0_groups[] = {
	"mfp2_25_24",
	"mfp3_30_28",
	"mfp3_21_19",
};

static const char * const uart1_groups[] = {
	"mfp2_13_11",
	"mfp3_18_16",
	"mfp3_5_3",
};

static const char * const uart2_groups[] = {
	"mfp0_31_29",
	"mfp0_28_26",
	"mfp0_25_23",
	"mfp2_26",
	"mfp2_25_24",
	"mfp2_19_17",
	"mfp2_16_14",
	"mfp2_13_11",
	"mfp3_30_28",
	"mfp3_21_19",
	"mfp3_18_16",
};

static const char * const uart3_groups[] = {
	"mfp2_23_irq0",
	"mfp2_23_irq1",
	"mfp2_22",
	"mfp2_21_20",
	"mfp3_11_9",
	"mfp3_8_6"
};

static const char * const uart4_groups[] = {
	"mfp0_16_14",
	"mfp3_2_0",
	"uart4_dummy",
};

static const char * const uart5_groups[] = {
	"mfp1_28_26_ks_out0",
	"mfp1_25_23",
	"mfp2_22",
	"mfp2_21_20",
	"mfp2_19_17",
	"mfp2_16_14",
};

static const char * const sd0_groups[] = {
	"mfp1_28_26_ks_out0",
	"mfp1_28_26_ks_out1",
	"mfp1_25_23",
	"mfp1_16_15",
	"mfp1_12_11",
	"mfp2_19_17",
	"mfp2_16_14",
	"mfp2_13_11",
	"mfp2_10_9",
	"mfp2_8_7",
	"mfp2_6_5",
};

static const char * const sd1_groups[] = {
	"mfp1_28_26_ks_in3",
	"mfp1_16_15",
	"mfp1_14_13_sd1_clkb",
	"mfp1_14_13_sd1_clk",
	"mfp1_12_11",
	"mfp1_10",
	"mfp2_10_9",
	"mfp3_11_9",
	"mfp3_8_6",
	"mfp3_5_3",
	"sd1_dummy",
};

static const char * const sd2_groups[] = {
	"mfp3_24_23",
};

static const char * const nand_groups[] = {
	"mfp3_27_26",
	"mfp3_24_23",
	"nand_dummy"
};

static const char * const pwm0_groups[] = {
	"mfp1_31_29_ks_in0",
};

static const char * const pwm1_groups[] = {
	"mfp1_31_29_ks_in1",
	"mfp1_28_26_ks_in3",
};

static const char * const pwm2_groups[] = {
	"mfp0_25_23",
	"mfp1_28_26_ks_out0",
	"mfp1_25_23",
	"mfp2_21_20",
};

static const char * const pwm3_groups[] = {
	"mfp1_28_26_ks_out1",
};


static const char * const sirq0_groups[] = {
	"mfp0_22_20_sirq0",
	"sirq0_dummy",
};

static const char * const sirq1_groups[] = {
	"mfp0_22_20_sirq1",
	"sirq1_dummy",
};

static const char * const sirq2_groups[] = {
	"mfp0_22_20_sirq2",
	"mfp2_1_0",
	"sirq2_dummy",
};

static const char * const lcd0_groups[] = {
	"mfp0_31_29",
	"mfp0_28_26",
	"mfp0_25_23",
	"mfp0_22_20_sirq0",
	"mfp0_22_20_sirq1",
	"mfp0_22_20_sirq2",
	"mfp0_19_17",
	"mfp0_16_14",
	"mfp1_22",
	"mfp1_21",
	"mfp1_16_15",
	"mfp1_14_13_sd1_clkb",
	"mfp1_14_13_sd1_clk",
	"mfp1_12_11",
	"mfp1_10",
	"mfp1_9",
};

static const char * const lvds_groups[] = {
	"mfp1_22",
	"mfp1_21",
};

static const char * const nor_groups[] = {
	"mfp0_31_29",
	"mfp0_28_26",
	"mfp0_25_23",
	"mfp0_22_20_sirq0",
	"mfp0_22_20_sirq1",
	"mfp0_22_20_sirq2",
	"mfp0_19_17",
	"mfp0_16_14",
	"mfp1_31_29_ks_in0",
	"mfp1_31_29_ks_in1",
	"mfp1_28_26_ks_in3",
	"mfp1_28_26_ks_out0",
	"mfp1_28_26_ks_out1",
	"mfp1_16_15",
	"mfp1_14_13_sd1_clkb",
	"mfp1_14_13_sd1_clk",
	"mfp2_19_17",
	"mfp2_16_14",
	"mfp2_13_11",
	"mfp2_8_7",
	"mfp2_2",
	"mfp2_1_0",
	"mfp3_24_23",
	"mfp3_18_16",
	"mfp3_12",
	"mfp3_11_9",
	"mfp3_8_6",
	"mfp3_5_3",
	"mfp3_2_0",
};

static const char * const pcm0_groups[] = {
	"mfp0_22_20_sirq0",
	"mfp0_22_20_sirq1",
	"mfp0_22_20_sirq2",
	"mfp0_19_17",
	"mfp0_2_i2s0",
	"mfp0_2_i2s1",
	"mfp3_5_3",
	"mfp3_2_0",
};

static const char * const pcm1_groups[] = {
	"mfp0_5",
	"mfp3_30_28",
	"mfp3_21_19",
};

static const char * const i2s0_groups[] = {
	"mfp0_5",
	"mfp0_2_i2s0",
	"i2s0_dummy",
};

static const char * const i2s1_groups[] = {
	"mfp0_2_i2s1",
	"mfp3_30_28",
	"mfp3_21_19",
	"i2s1_dummy",
};

static const char * const jtag_groups[] = {
	"mfp1_31_29_ks_in0",
	"mfp1_31_29_ks_in1",
	"mfp1_28_26_ks_in3",
	"mfp1_28_26_ks_out1",
	"mfp2_19_17",
	"mfp2_13_11",
	"mfp2_8_7",
	"mfp2_6_5",
};

static const char * const ks_groups[] = {
	"mfp1_31_29_ks_in0",
	"mfp1_31_29_ks_in1",
	"mfp1_31_29_ks_in3",
	"mfp1_28_26_ks_out0",
	"mfp1_28_26_ks_out1",
	"mfp1_25_23",
};

static const char * const sens1_groups[] = {
	"mfp2_26",
	"mfp2_25_24",
	"mfp3_22",
	"mfp3_12",
	"mfp3_11_9",
	"mfp3_8_6",
	"mfp3_5_3",
	"mfp3_2_0",
};

static const char * const hdmi_groups[] = {
	"mfp3_22",
};
static const char * const clko_25m_groups[] = {
	"mfp3_31",
};

static const char * const spdif_groups[] = {
	"mfp3_21_19",
};

#define FUNCTION(fname)					\
	{						\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

const struct owl_pinmux_func ats3605_functions[] = {
	[OWL_MUX_SPI0] = FUNCTION(spi0),
	[OWL_MUX_SPI1] = FUNCTION(spi1),
	[OWL_MUX_SPI2] = FUNCTION(spi2),
	[OWL_MUX_SPI3] = FUNCTION(spi3),
	[OWL_MUX_I2C0] = FUNCTION(i2c0),
	[OWL_MUX_I2C1] = FUNCTION(i2c1),
	[OWL_MUX_I2C2] = FUNCTION(i2c2),
	[OWL_MUX_UART0] = FUNCTION(uart0),
	[OWL_MUX_UART1] = FUNCTION(uart1),
	[OWL_MUX_UART2] = FUNCTION(uart2),
	[OWL_MUX_UART3] = FUNCTION(uart3),
	[OWL_MUX_UART4] = FUNCTION(uart4),
	[OWL_MUX_UART5] = FUNCTION(uart5),
	[OWL_MUX_SD0] = FUNCTION(sd0),
	[OWL_MUX_SD1] = FUNCTION(sd1),
	[OWL_MUX_SD2] = FUNCTION(sd2),
	[OWL_MUX_NAND] = FUNCTION(nand),
	[OWL_MUX_PWM0] = FUNCTION(pwm0),
	[OWL_MUX_PWM1] = FUNCTION(pwm1),
	[OWL_MUX_PWM2] = FUNCTION(pwm2),
	[OWL_MUX_PWM3] = FUNCTION(pwm3),
	[OWL_MUX_SIRQ0] = FUNCTION(sirq0),
	[OWL_MUX_SIRQ1] = FUNCTION(sirq1),
	[OWL_MUX_SIRQ2] = FUNCTION(sirq2),
	[OWL_MUX_LCD0] = FUNCTION(lcd0),
	[OWL_MUX_LVDS] = FUNCTION(lvds),
	[OWL_MUX_NOR] = FUNCTION(nor),
	[OWL_MUX_PCM0] = FUNCTION(pcm0),
	[OWL_MUX_PCM1] = FUNCTION(pcm1),
	[OWL_MUX_I2S0] = FUNCTION(i2s0),
	[OWL_MUX_I2S1] = FUNCTION(i2s1),
	[OWL_MUX_JTAG] = FUNCTION(jtag),
	[OWL_MUX_KS] = FUNCTION(ks),
	[OWL_MUX_SENS1] = FUNCTION(sens1),
	[OWL_MUX_HDMI] = FUNCTION(hdmi),
	[OWL_MUX_CLKO_25M] = FUNCTION(clko_25m),
	[OWL_MUX_SPDIF] = FUNCTION(spdif),
};


/******PAD SCHIMTT CONFIGURES*************************/

#define SCHIMMT_CONF(pad_name, reg_n, sft)		\
	{	\
		.schimtt_funcs = pad_name##_schimtt_funcs,		\
		.num_schimtt_funcs =					\
			ARRAY_SIZE(pad_name##_schimtt_funcs),		\
		.reg = PAD_ST##reg_n,					\
		.shift = sft,						\
	}

#define PAD_SCHIMMT_CONF(pad_name, reg_n, sft)				\
	struct owl_pinconf_schimtt pad_name##_schimmt_conf = {		\
		.schimtt_funcs = pad_name##_schimtt_funcs,		\
		.num_schimtt_funcs =					\
			ARRAY_SIZE(pad_name##_schimtt_funcs),		\
		.reg = PAD_ST##reg_n,					\
		.shift = sft,						\
	}


/******PAD PULL UP/DOWN CONFIGURES*************************/
#define PULL_CONF(reg_n, sft, w, pup, pdn)		\
	{	\
		.reg = PAD_PULLCTL##reg_n,				\
		.shift = sft,	\
		.width = w,	\
		.pullup = pup,		\
		.pulldown = pdn,	\
	}

#define PAD_PULL_CONF(pad_name, reg_num,	\
			shift, width, pull_up, pull_down)		\
	struct owl_pinconf_reg_pull pad_name##_pull_conf	\
		= PULL_CONF(reg_num, shift, width, pull_up, pull_down)

/*PAD_PULLCTL0*/
static PAD_PULL_CONF(P_LVDS_OCP, 0, 29, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_KS_OUT2, 0, 28, 1, 0x1, 0);
static PAD_PULL_CONF(P_LCD0_D17, 0, 27, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LCD0_D9, 0, 26, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LVDS_OCN, 0, 24, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_LVDS_OBP, 0, 22, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_LCD0_D18, 0, 21, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LCD0_D2, 0, 20, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LVDS_OAP, 0, 19, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LVDS_OAN, 0, 18, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LVDS_OEP, 0, 17, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_LVDS_OEN, 0, 16, 1, 0x1, 0x0);
static PAD_PULL_CONF(P_SIRQ0, 0, 14, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_SIRQ1, 0, 12, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_SIRQ2, 0, 10, 2, 0x1, 0x2);

static PAD_PULL_CONF(P_I2C0_SDATA, 0, 9, 1, 0x1, 0);
static PAD_PULL_CONF(P_I2C0_SCLK, 0, 8, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_IN0, 0, 7, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_IN1, 0, 6, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_IN2, 0, 5, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_IN3, 0, 4, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_OUT0, 0, 2, 1, 0x1, 0);
static PAD_PULL_CONF(P_KS_OUT1, 0, 1, 1, 0x1, 0);
static PAD_PULL_CONF(P_LCD0_D8, 0, 0, 1, 0x1, 0);

/*PAD_PULLCTL1*/
static PAD_PULL_CONF(P_LCD0_D1, 1, 31, 1, 0x1, 0);
static PAD_PULL_CONF(P_LCD0_D0, 1, 30, 1, 0x1, 0);
static PAD_PULL_CONF(P_SPI0_SS, 1, 28, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_NAND_CEB2, 1, 23, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_CEB3, 1, 22, 1, 0x1, 0);
static PAD_PULL_CONF(P_UART3_RX, 1, 20, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_UART3_TX, 1, 18, 2, 0x1, 0x2);
static PAD_PULL_CONF(P_SD0_D0, 1, 17, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D1, 1, 16, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D2, 1, 15, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D3, 1, 14, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_CMD, 1, 13, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD1_CMD, 1, 11, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D4, 1, 6, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D5, 1, 5, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D6, 1, 4, 1, 0x1, 0);
static PAD_PULL_CONF(P_SD0_D7, 1, 3, 1, 0x1, 0);
static PAD_PULL_CONF(P_UART0_RX, 1, 2, 1, 0x1, 0);
static PAD_PULL_CONF(P_UART0_TX, 1, 1, 1, 0x1, 0);
static PAD_PULL_CONF(P_CLKO_25M, 1, 0, 1, 0, 0x1);

/*PAD_PULLCTL2*/
static PAD_PULL_CONF(P_SENSOR1_DATA6, 2, 22, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA7, 2, 21, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA0, 2, 20, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA1, 2, 19, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA2, 2, 18, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA3, 2, 17, 1, 0x1, 0);
static PAD_PULL_CONF(P_SENSOR1_DATA4, 2, 16, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D0, 2, 15, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D3, 2, 14, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D5, 2, 13, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D6, 2, 12, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D7, 2, 11, 1, 0x1, 0);
static PAD_PULL_CONF(P_I2C1_SDATA, 2, 10, 1, 0x1, 0);
static PAD_PULL_CONF(P_I2C1_SCLK, 2, 9, 1, 0x1, 0);
static PAD_PULL_CONF(P_I2C2_SDATA, 2, 8, 1, 0x1, 0);
static PAD_PULL_CONF(P_I2C2_SCLK, 2, 7, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_DQS, 2, 6, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_RB, 2, 5, 1, 0x1, 0);
static PAD_PULL_CONF(P_NAND_D1, 2, 0, 1, 0x1, 0);


/********PAD INFOS*****************************/
#define PAD_TO_GPIO(padnum)		\
		((padnum < NUM_GPIOS) ? padnum : -1)

#define PAD_INFO(name)		\
	{		\
		.pad = name,	\
		.gpio = PAD_TO_GPIO(name),		\
		.schimtt = NULL,		\
		.pull = NULL,	\
	}

#define PAD_INFO_SCHIMTT(name)	\
	{		\
		.pad = name,	\
		.gpio = PAD_TO_GPIO(name),		\
		.schimtt = &name##_schimmt_conf,		\
		.pull = NULL,	\
	}

#define PAD_INFO_PULL(name)	\
	{		\
		.pad = name,	\
		.gpio = PAD_TO_GPIO(name),		\
		.schimtt = NULL,		\
		.pull = &name##_pull_conf,	\
	}

#define PAD_INFO_SCHIMTT_PULL(name)	\
	{		\
		.pad = name,	\
		.gpio = PAD_TO_GPIO(name),		\
		.schimtt = &name##_schimmt_conf,		\
		.pull = &name##_pull_conf,	\
	}

/* Pad info table for the pinmux subsystem */
struct owl_pinconf_pad_info ats3605_pad_tab[NUM_PADS] = {

	/*PAD_PULLCTL0*/
	[P_LVDS_OCP] = PAD_INFO_PULL(P_LVDS_OCP),
	[P_KS_OUT2] =  PAD_INFO_PULL(P_KS_OUT2),
	[P_LCD0_D17] = PAD_INFO_PULL(P_LCD0_D17),
	[P_LCD0_D9] =  PAD_INFO_PULL(P_LCD0_D9),
	[P_LVDS_OCN] =  PAD_INFO_PULL(P_LVDS_OCN),
	[P_LVDS_OBP] =  PAD_INFO_PULL(P_LVDS_OBP),
	[P_LCD0_D18] =  PAD_INFO_PULL(P_LCD0_D18),
	[P_LCD0_D2] =  PAD_INFO_PULL(P_LCD0_D2),
	[P_LVDS_OAP] =  PAD_INFO_PULL(P_LVDS_OAP),
	[P_LVDS_OAN] =  PAD_INFO_PULL(P_LVDS_OAN),
	[P_LVDS_OEP] =  PAD_INFO_PULL(P_LVDS_OEP),
	[P_LVDS_OEN] =  PAD_INFO_PULL(P_LVDS_OEN),
	[P_SIRQ0] = PAD_INFO_PULL(P_SIRQ0),
	[P_SIRQ1] = PAD_INFO_PULL(P_SIRQ1),
	[P_SIRQ2] = PAD_INFO_PULL(P_SIRQ2),

	[P_I2C0_SDATA] = PAD_INFO_PULL(P_I2C0_SDATA),
	[P_I2C0_SCLK] = PAD_INFO_PULL(P_I2C0_SCLK),
	[P_KS_IN0] = PAD_INFO_PULL(P_KS_IN0),
	[P_KS_IN1] = PAD_INFO_PULL(P_KS_IN1),
	[P_KS_IN2] = PAD_INFO_PULL(P_KS_IN2),
	[P_KS_IN3] = PAD_INFO_PULL(P_KS_IN3),
	[P_KS_OUT0] = PAD_INFO_PULL(P_KS_OUT0),
	[P_KS_OUT1] = PAD_INFO_PULL(P_KS_OUT1),
	[P_LCD0_D8] = PAD_INFO_PULL(P_LCD0_D8),

	/*PAD_PULLCTL1*/
	[P_LCD0_D1] = PAD_INFO_PULL(P_LCD0_D1),
	[P_LCD0_D0] = PAD_INFO_PULL(P_LCD0_D0),
	[P_SPI0_SS] = PAD_INFO_PULL(P_SPI0_SS),
	[P_NAND_CEB2] = PAD_INFO_PULL(P_NAND_CEB2),
	[P_NAND_CEB3] = PAD_INFO_PULL(P_NAND_CEB3),
	[P_UART3_RX] = PAD_INFO_PULL(P_UART3_RX),
	[P_UART3_TX] = PAD_INFO_PULL(P_UART3_TX),
	[P_SD0_D0] = PAD_INFO_PULL(P_SD0_D0),
	[P_SD0_D1] = PAD_INFO_PULL(P_SD0_D1),
	[P_SD0_D2] = PAD_INFO_PULL(P_SD0_D2),
	[P_SD0_D3] = PAD_INFO_PULL(P_SD0_D3),
	[P_SD0_CMD] = PAD_INFO_PULL(P_SD0_CMD),
	[P_SD1_CMD] = PAD_INFO_PULL(P_SD1_CMD),
	[P_SD0_D4] = PAD_INFO_PULL(P_SD0_D4),
	[P_SD0_D5] = PAD_INFO_PULL(P_SD0_D5),
	[P_SD0_D6] = PAD_INFO_PULL(P_SD0_D6),
	[P_SD0_D7] = PAD_INFO_PULL(P_SD0_D7),
	[P_UART0_RX] = PAD_INFO_PULL(P_UART0_RX),
	[P_UART0_TX] = PAD_INFO_PULL(P_UART0_TX),
	[P_CLKO_25M] = PAD_INFO_PULL(P_CLKO_25M),

	/*PAD_PULLCTL2*/
	[P_SENSOR1_DATA6] = PAD_INFO_PULL(P_SENSOR1_DATA6),
	[P_SENSOR1_DATA7] = PAD_INFO_PULL(P_SENSOR1_DATA7),
	[P_SENSOR1_DATA0] = PAD_INFO_PULL(P_SENSOR1_DATA0),
	[P_SENSOR1_DATA1] = PAD_INFO_PULL(P_SENSOR1_DATA1),
	[P_SENSOR1_DATA2] = PAD_INFO_PULL(P_SENSOR1_DATA2),
	[P_SENSOR1_DATA3] = PAD_INFO_PULL(P_SENSOR1_DATA3),
	[P_SENSOR1_DATA4] = PAD_INFO_PULL(P_SENSOR1_DATA4),
	[P_NAND_D0] = PAD_INFO_PULL(P_NAND_D0),
	[P_NAND_D3] = PAD_INFO_PULL(P_NAND_D3),
	[P_NAND_D5] = PAD_INFO_PULL(P_NAND_D5),
	[P_NAND_D6] = PAD_INFO_PULL(P_NAND_D6),
	[P_NAND_D7] = PAD_INFO_PULL(P_NAND_D7),
	[P_I2C1_SDATA] = PAD_INFO_PULL(P_I2C1_SDATA),
	[P_I2C1_SCLK] = PAD_INFO_PULL(P_I2C1_SCLK),
	[P_I2C2_SDATA] = PAD_INFO_PULL(P_I2C2_SDATA),
	[P_I2C2_SCLK] = PAD_INFO_PULL(P_I2C2_SCLK),
	[P_NAND_DQS] = PAD_INFO_PULL(P_NAND_DQS),
	[P_NAND_RB] = PAD_INFO_PULL(P_NAND_RB),
	[P_NAND_D1] = PAD_INFO_PULL(P_NAND_D1),
};

static struct pinctrl_gpio_range ats3605_gpio_ranges[] = {
	{
		.name = "ats3605-pinctrl-gpio",
		.id = 0,
		.base = 0,
		.pin_base = 0,
		.npins = NUM_GPIOS,
	},
};

static struct owl_pinctrl_soc_info ats3605_pinctrl_info = {
	.gpio_ranges = ats3605_gpio_ranges,
	.gpio_num_ranges = ARRAY_SIZE(ats3605_gpio_ranges),
	.padinfo = ats3605_pad_tab,
	.pins = (const struct pinctrl_pin_desc *)ats3605_pads,
	.npins = ARRAY_SIZE(ats3605_pads),
	.functions = ats3605_functions,
	.nfunctions = ARRAY_SIZE(ats3605_functions),
	.groups = ats3605_groups,
	.ngroups = ARRAY_SIZE(ats3605_groups),

};

static int ats3605_pinctrl_probe(struct platform_device *pdev)
{
	pr_info("[OWL] pinctrl ats3605 probe\n");
	return owl_pinctrl_probe(pdev, &ats3605_pinctrl_info);
}

static int ats3605_pinctrl_remove(struct platform_device *pdev)
{
	return owl_pinctrl_remove(pdev);
}

static struct of_device_id ats3605_pinctrl_of_match[] = {
	{ .compatible = "actions,ats3605-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, ats3605_pinctrl_of_match);

static struct platform_driver ats3605_pinctrl_driver = {
	.probe = ats3605_pinctrl_probe,
	.remove = ats3605_pinctrl_remove,
	.driver = {
		.name = "pinctrl-ats3605",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ats3605_pinctrl_of_match),
	},
};

static int __init ats3605_pinctrl_init(void)
{
	return platform_driver_register(&ats3605_pinctrl_driver);
}
arch_initcall(ats3605_pinctrl_init);

static void __exit ats3605_pinctrl_exit(void)
{
	platform_driver_unregister(&ats3605_pinctrl_driver);
}

module_exit(ats3605_pinctrl_exit);
MODULE_AUTHOR("Actions Semi Inc.");
MODULE_DESCRIPTION("Pin control driver for Actions ATS3605 SoC");
MODULE_LICENSE("GPL");
