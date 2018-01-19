/*
 * Device Tree binding constants for Actions ATS3605 SoC clock controller
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

#ifndef __DT_BINDINGS_CLOCK_ATS3605_H
#define __DT_BINDINGS_CLOCK_ATS3605_H

#define CLK_NONE			0

/* fixed rate clocks */
#define CLK_LOSC			1
#define CLK_HOSC			2
#define CLK_120M			3

/* pll clocks */
#define CLK_CORE_PLL		4
#define CLK_DEV_PLL			5
#define CLK_DDR_PLL			6
#define CLK_NAND_PLL		7
#define CLK_DISPLAY_PLL		8
#define CLK_TVOUT0_PLL		9
#define CLK_TVOUT1_PLL		10
#define CLK_AUDIO_PLL		11
#define CLK_120M_PLL		12

/* system clock */
#define CLK_SYS_BASE			13
#define CLK_CPU				CLK_SYS_BASE
#define CLK_DEV				(CLK_SYS_BASE+1)
#define CLK_AHB				(CLK_SYS_BASE+2)
#define CLK_APB				(CLK_SYS_BASE+3)
#define CLK_ACP				(CLK_SYS_BASE+4)
#define CLK_NIC_DCU			(CLK_SYS_BASE+5)
#define CLK_DDR				(CLK_SYS_BASE+6)
#define CLK_DMAC			(CLK_SYS_BASE+7)
#define CLK_SRAMI			(CLK_SYS_BASE+8)
#define CLK_HP_CLK_MUX		(CLK_SYS_BASE+9)
#define CLK_TWD				(CLK_SYS_BASE+10)

/* peripheral device clock */
#define CLK_PERIP_BASE			25
#define CLK_GPIO			(CLK_PERIP_BASE)
#define CLK_TIMER			(CLK_PERIP_BASE+1)
#define CLK_NAND			(CLK_PERIP_BASE+2)
#define CLK_NAND_NIC		(CLK_PERIP_BASE+3)
#define CLK_KEY				(CLK_PERIP_BASE+4)
#define CLK_VCE				(CLK_PERIP_BASE+5)
#define CLK_VDE				(CLK_PERIP_BASE+6)
#define CLK_SD0_NIC			(CLK_PERIP_BASE+7)
#define CLK_SD1_NIC			(CLK_PERIP_BASE+8)
#define CLK_SD2_NIC			(CLK_PERIP_BASE+9)
#define CLK_SD0				(CLK_PERIP_BASE+10)
#define CLK_SD1				(CLK_PERIP_BASE+11)
#define CLK_SD2				(CLK_PERIP_BASE+12)
#define CLK_UART0			(CLK_PERIP_BASE+13)
#define CLK_UART1			(CLK_PERIP_BASE+14)
#define CLK_UART2			(CLK_PERIP_BASE+15)
#define CLK_UART3			(CLK_PERIP_BASE+16)
#define CLK_UART4			(CLK_PERIP_BASE+17)
#define CLK_UART5			(CLK_PERIP_BASE+18)
//#define CLK_GPU				(CLK_PERIP_BASE+19)
#define CLK_PWM0			(CLK_PERIP_BASE+20)
#define CLK_PWM1			(CLK_PERIP_BASE+21)
#define CLK_PWM2			(CLK_PERIP_BASE+22)
#define CLK_PWM3			(CLK_PERIP_BASE+23)

#define CLK_I2C0			(CLK_PERIP_BASE+27)
#define CLK_I2C1			(CLK_PERIP_BASE+28)
#define CLK_I2C2			(CLK_PERIP_BASE+29)
#define CLK_I2C3			(CLK_PERIP_BASE+30)
#define CLK_SPI0			(CLK_PERIP_BASE+31)
#define CLK_SPI1			(CLK_PERIP_BASE+32)
#define CLK_SPI2			(CLK_PERIP_BASE+33)
#define CLK_SPI3			(CLK_PERIP_BASE+34)

#define CLK_DE				(CLK_PERIP_BASE+35)
#define CLK_DE1				(CLK_PERIP_BASE+36)
#define CLK_DE2				(CLK_PERIP_BASE+37)
#define CLK_DE3				(CLK_PERIP_BASE+38)
#define CLK_DE_WB			(CLK_PERIP_BASE+39)
#define CLK_LCD				(CLK_PERIP_BASE+40)
#define CLK_LCD0			(CLK_PERIP_BASE+41)
#define CLK_SI				(CLK_PERIP_BASE+42)
#define CLK_SENSOR_OUT		(CLK_PERIP_BASE+43)

#define CLK_HDMI_AUDIO		(CLK_PERIP_BASE+44)
#define CLK_I2SRX			(CLK_PERIP_BASE+45)
#define CLK_I2STX			(CLK_PERIP_BASE+46)
#define CLK_PCM0			(CLK_PERIP_BASE+47)
#define CLK_PCM1			(CLK_PERIP_BASE+48)
#define CLK_SPDIF			(CLK_PERIP_BASE+49)

#define CLK_USB2H0_PLLEN		(CLK_PERIP_BASE+50)
#define CLK_USB2H0_PHY			(CLK_PERIP_BASE+51)
#define CLK_USB2H0_CCE			(CLK_PERIP_BASE+52)
#define CLK_USB2H1_PLLEN		(CLK_PERIP_BASE+53)
#define CLK_USB2H1_PHY			(CLK_PERIP_BASE+54)
#define CLK_USB2H1_CCE			(CLK_PERIP_BASE+55)
#define CLK_USB2H_CCE			(CLK_PERIP_BASE+56)


#define CLK_NR_CLKS				85

#endif /* __DT_BINDINGS_CLOCK_ATS3605_H */
