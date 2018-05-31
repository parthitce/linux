/*
 * Actions ATS3605 clock driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
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

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/clk-ats3605.h>
#include "clk.h"

#define CMU_COREPLL		(0x0000)
#define CMU_DEVPLL		(0x0004)
#define CMU_DDRPLL		(0x0008)
#define CMU_NANDPLL	(0x000C)
#define CMU_DISPLAYPLL	(0x0010)
#define CMU_AUDIOPLL	(0x0014)
#define CMU_TVOUTPLL	(0x0018)
#define CMU_BUSCLK		(0x001C)
#define CMU_SENSORCLK	(0x0020)
#define CMU_LCDCLK		(0x0024)
#define CMU_DECLK		(0x0030)
#define CMU_SICLK		(0x0034)
#define CMU_VDECLK		(0x0040)
#define CMU_VCECLK		(0x0044)
#define CMU_GPUCLK		(0x0048)
#define CMU_NANDCCLK	(0x004C)
#define CMU_SD0CLK		(0x0050)
#define CMU_SD1CLK		(0x0054)
#define CMU_SD2CLK		(0x0058)
#define CMU_UART0CLK	(0x005C)
#define CMU_UART1CLK	(0x0060)
#define CMU_UART2CLK	(0x0064)
#define CMU_DMACLK		(0x006C)
#define CMU_PWM0CLK	(0x0070)
#define CMU_PWM1CLK	(0x0074)
#define CMU_PWM2CLK	(0x0078)
#define CMU_PWM3CLK	(0x007C)
#define CMU_USBCLK		(0x0080)
#define CMU_120MPLL		(0x0084)
#define CMU_TLSCLK		(0x0088)
#define CMU_DEVCLKEN0	(0x00A0)
#define CMU_DEVCLKEN1	(0x00A4)
#define CMU_DEVRST0	(0x00A8)
#define CMU_DEVRST1	(0x00AC)
#define CMU_UART3CLK	(0x00B0)
#define CMU_UART4CLK	(0x00B4)
#define CMU_UART5CLK	(0x00B8)




/* fixed rate clocks */
static struct owl_fixed_rate_clock ats3605_fixed_rate_clks[] __initdata = {
	{ CLK_LOSC,     "losc",     NULL, CLK_IS_ROOT, 32768, },
	{ CLK_HOSC,     "hosc",     NULL, CLK_IS_ROOT, 24000000, },
	{ CLK_120M,     "120m",     NULL, CLK_IS_ROOT, 120000000, },
};

static struct clk_pll_table clk_audio_pll_table[] = {
	{0, 45158400}, {1, 49152000},
	{0, 0},
};


static struct clk_pll_table clk_tvout0_pll_table[] = {
	{0, 100800000}, {1, 297000000}, {3, 296000000},
	{0, 0},
};

static struct clk_pll_table clk_clk_tvout1_pll_table[] = {
	{0, 80000000}, {1, 130000000},  {2, 171000000},
	{3, 294000000}, {4, 216000000},  {0, 0},
};

/* pll clocks */
static struct owl_pll_clock ats3605_pll_clks[] __initdata = {
	{ CLK_CORE_PLL,   "core_pll", NULL, CLK_IS_ROOT, CMU_COREPLL, 12000000, 9, 0, 7,  4, 127, 0, NULL},
	{ CLK_DEV_PLL,    "dev_pll", NULL, CLK_IS_ROOT, CMU_DEVPLL,  6000000, 8, 0, 7, 8, 126, 0, NULL},
	{ CLK_DDR_PLL,    "ddr_pll",  NULL, CLK_IS_ROOT, CMU_DDRPLL, 12000000, 8, 0, 7,  1,  120, 0, NULL},
	{ CLK_NAND_PLL,   "nand_pll", NULL, CLK_IS_ROOT, CMU_NANDPLL,  6000000, 8, 0, 7,  2, 86, 0, NULL},
	{ CLK_DISPLAY_PLL, "display_pll", NULL, CLK_IS_ROOT, CMU_DISPLAYPLL, 6000000, 8, 0, 7, 2, 126, 0, NULL},
	{ CLK_AUDIO_PLL,  "audio_pll", NULL, CLK_IS_ROOT, CMU_AUDIOPLL, 0, 4, 0, 1, 0, 0, 0, clk_audio_pll_table},
	{ CLK_TVOUT0_PLL,  "tvout0_pll", NULL, CLK_IS_ROOT, CMU_TVOUTPLL, 0, 3, 0, 2, 0, 0, 0, clk_tvout0_pll_table},
	{ CLK_TVOUT1_PLL,  "tvout1_pll", NULL, CLK_IS_ROOT, CMU_TVOUTPLL, 0, 11, 8, 3, 0, 0, 0, clk_clk_tvout1_pll_table},
};

static const char *cpu_clk_mux_p[] __initdata = {"losc", "hosc", "core_pll", "vce"};
static const char *dev_clk_p[] __initdata = { "hosc", "dev_pll"};
static const char *nic_dcu_clk_mux_p[] __initdata = { "dev_clk", "ddr"};
static const char *ddr_clk_mux_p[] __initdata = { "ddr_pll", "tvout0_pll"};

static const char *audio_pll_mux_p[] __initdata = { "audio_pll"};

static const char *nand_clk_mux_p[] __initdata = { "nand_pll", "display_pll", "dev_clk", "ddr_pll"};
static const char *uart_clk_mux_p[] __initdata = { "hosc", "dev_pll"};
static const char *sd_clk_mux_p[] __initdata = { "dev_clk", "120m_pll", "nand_pll", };
static const char *pwm_clk_mux_p[] __initdata = { "losc", "hosc"};
static const char *de_clk_mux_p[] __initdata = { "display_pll", "dev_clk"};
static const char *lcd_clk_mux_p[] __initdata = { "display_pll", "dev_clk", "tvout0_pll" };
static const char *sensor_clk_mux_p[] __initdata = { "hosc", "si"};
static const char *vde_clk_mux_p[] __initdata = { "dev_clk", "display_pll", "nand_pll", "ddr_pll"};
static const char *vce_clk_mux_p[] __initdata = { "dev_clk", "display_pll", "nand_pll", "ddr_pll", "120m_pll"};


/* mux clocks */
static struct owl_mux_clock ats3605_mux_clks[] __initdata = {
	{ CLK_CPU,  "cpu_clk", cpu_clk_mux_p, ARRAY_SIZE(cpu_clk_mux_p), CLK_SET_RATE_PARENT, CMU_BUSCLK, 0, 2, 0, "cpu_clk" },
	{ CLK_DEV,  "dev_clk", dev_clk_p, ARRAY_SIZE(dev_clk_p), CLK_SET_RATE_PARENT, CMU_DEVPLL, 12, 1, 0, "dev_clk" },
	{ CLK_NIC_DCU,  "nic_dcu_clk", nic_dcu_clk_mux_p, ARRAY_SIZE(nic_dcu_clk_mux_p), CLK_SET_RATE_PARENT, CMU_BUSCLK, 16, 1, 0,  NULL},
	{ CLK_HP_CLK_MUX, "hp_clk_mux", nic_dcu_clk_mux_p, ARRAY_SIZE(nic_dcu_clk_mux_p), CLK_SET_RATE_PARENT, CMU_BUSCLK, 7, 1, 0,  NULL},

};


static struct clk_factor_table sd_factor_table[] = {
	/* bit0 ~ 4 */
	{0, 1, 1}, {1, 1, 2}, {2, 1, 3}, {3, 1, 4},
	{4, 1, 5}, {5, 1, 6}, {6, 1, 7}, {7, 1, 8},
	{8, 1, 9}, {9, 1, 10}, {10, 1, 11}, {11, 1, 12},
	{12, 1, 13}, {13, 1, 14}, {14, 1, 15}, {15, 1, 16},
	{16, 1, 17}, {17, 1, 18}, {18, 1, 19}, {19, 1, 20},
	{20, 1, 21}, {21, 1, 22}, {22, 1, 23}, {23, 1, 24},
	{24, 1, 25}, {25, 1, 26},

	/* bit8: /128 */
	{256, 1, 1 * 128}, {257, 1, 2 * 128}, {258, 1, 3 * 128}, {259, 1, 4 * 128},
	{260, 1, 5 * 128}, {261, 1, 6 * 128}, {262, 1, 7 * 128}, {263, 1, 8 * 128},
	{264, 1, 9 * 128}, {265, 1, 10 * 128}, {266, 1, 11 * 128}, {267, 1, 12 * 128},
	{268, 1, 13 * 128}, {269, 1, 14 * 128}, {270, 1, 15 * 128}, {271, 1, 16 * 128},
	{272, 1, 17 * 128}, {273, 1, 18 * 128}, {274, 1, 19 * 128}, {275, 1, 20 * 128},
	{276, 1, 21 * 128}, {277, 1, 22 * 128}, {278, 1, 23 * 128}, {279, 1, 24 * 128},
	{280, 1, 25 * 128}, {281, 1, 26 * 128},
	{0, 0},
};

static struct clk_div_table ahb_div_table[] = {
	{0, 1},   {1, 2},   {2, 3},   {3, 4},
	{4, 6},  {0, 0},
};

static struct clk_div_table apb_div_table[] = {
	{0, 2},   {1, 4},   {2, 6},   {3, 8},
	{4, 12},   {5, 16},   {0, 0},
};
static struct clk_div_table acp_div_table[] = {
	{0, 2},   {1, 4},   {2, 8},   {0, 0},
};
static struct clk_div_table lcd_div_table[] = {
	{0, 1},   {1, 7},
};

static struct clk_div_table nand_div_table[] = {
	{0, 1},   {1, 2},   {2, 4},   {3, 6},
	{4, 8},   {5, 10},   {6, 12},  {7, 14},
	{8, 16}, {9, 18},   {10, 20},  {11, 22},
	{12, 24}, {13, 26},   {14, 28},  {15, 30},
	{0, 0},
};

static struct clk_div_table hdmia_div_table[] = {
	{0, 1},   {1, 2},   {2, 3},   {3, 4},
	{4, 6},   {5, 8},   {6, 12},  {7, 16},
	{8, 24},
	{0, 0},
};

/* divider clocks */

static struct owl_divider_clock ats3605_div_clks[] __initdata = {
	{ CLK_AHB, "ahb_clk", "hp_clk_mux", 0, CMU_BUSCLK, 4, 3, 0, ahb_div_table,"ahb_clk"},
	{ CLK_APB, "apb_clk", "ahb_clk", 0, CMU_BUSCLK, 8, 3, 0,  apb_div_table, "apb_clk"},
	{ CLK_ACP, "acp_clk", "cpu_clk", 0, CMU_DMACLK, 0, 2, 0,  acp_div_table, "acp_clk"},
	{ CLK_DE1, "de1", "de_clk", 0, CMU_DECLK, 0, 4, 0,  NULL, "de1"},
	{ CLK_DE2, "de2", "de_clk", 0, CMU_DECLK, 4, 4, 0,  NULL, "de2"},
	{ CLK_DE3, "de3", "de_clk", 0, CMU_DECLK, 8, 4, 0,  NULL, "de3"},
	{ CLK_DE_WB, "de_wb", "de_clk", 0, CMU_DECLK, 20, 4, 0,  NULL, "de_wb"},
	{ CLK_LCD0, "lcd0", "lcd_clk", 0, CMU_LCDCLK, 0, 4, 0,  NULL, "lcd0"},

};

/* fixed factor clocks */

static struct owl_fixed_factor_clock ats3605_fixed_factor_clks[] __initdata = {
	{CLK_TWD, "smp_twd", "cpu_clk", 0, 1, 2},

};


static struct clk_factor_table vde_factor_table[] = {
	{0, 1, 1}, {1, 2, 3}, {2, 1, 2}, {3, 2, 5},
	{4, 1, 3}, {5, 1, 4}, {6, 1, 6}, {7, 1, 8},
	{0, 0, 0},
};


/* gate clocks */
static struct owl_gate_clock ats3605_gate_clks[] __initdata = {
	{ CLK_GPIO,  "gpio", "apb_clk", 0, CMU_DEVCLKEN0, 18, 0, "gpio"},
	{ CLK_DMAC,  "dmac", "acp_clk", 0, CMU_DEVCLKEN0, 1, 0, "dmac"},
	{ CLK_SRAMI,  "srami", "ahb_clk", 0, CMU_DEVCLKEN0, 2, 0, "srami"},
	{ CLK_120M_PLL,  "120m_pll", "120m", 0, CMU_120MPLL, 0, 0, "120m_pll"},

	{ CLK_I2C0, "i2c0",	"hosc", 0, CMU_DEVCLKEN1, 14, 0, "i2c0"},
	{ CLK_I2C1, "i2c1",	"hosc", 0, CMU_DEVCLKEN1, 15, 0, "i2c1"},
	{ CLK_I2C2, "i2c2",	"hosc", 0, CMU_DEVCLKEN1, 30, 0, "i2c2"},


	{ CLK_SPI0, "spi0",	"ahb_clk", 0, CMU_DEVCLKEN1, 10, 0, "spi0"},
	{ CLK_SPI1, "spi1",	"ahb_clk", 0, CMU_DEVCLKEN1, 11, 0, "spi1"},
	{ CLK_SPI2, "spi2",	"ahb_clk", 0, CMU_DEVCLKEN1, 12, 0, "spi2"},
	{ CLK_SPI3, "spi3",	"ahb_clk", 0, CMU_DEVCLKEN1, 13, 0, "spi3"},

	{ CLK_KEY,  "key", "apb_clk", 0, CMU_DEVCLKEN0, 17, 0, "key"},
	{ CLK_TIMER,  "timer", "hosc", 0, CMU_DEVCLKEN1, 27, 0, "timer"},

	{ CLK_USB2H0_PLLEN,	"usb2h0_pllen",	NULL, 0, CMU_USBCLK, 12, 0, "usb2h0_pllen"},
	{ CLK_USB2H0_PHY,	"usb2h0_phy",	NULL, 0, CMU_USBCLK, 10, 0, "usb2h0_phy"},
	{ CLK_USB2H0_CCE,	"usb2h0_cce",	NULL, 0, CMU_USBCLK, 8, 0, "usb2h0_cce"},

	{ CLK_USB2H1_PLLEN,	"usb2h1_pllen",	NULL, 0, CMU_USBCLK, 13, 0, "usb2h1_pllen"},
	{ CLK_USB2H1_PHY,	"usb2h1_phy",	NULL, 0, CMU_USBCLK, 11, 0, "usb2h1_phy"},
	{ CLK_USB2H1_CCE,	"usb2h1_cce",	NULL, 0, CMU_USBCLK, 9, 0, "usb2h1_cce"},

	{ CLK_USB2H_CCE,	"usb2h_cce",	NULL, 0, CMU_USBCLK, 14, 0, "usb2h_cce"},

#if 0
	
	{ CLK_DSI,  "dsi_clk", NULL, 0, CMU_DEVCLKEN0, 2, 0, "dsi"},
	{ CLK_TVOUT,  "tvout_clk", NULL, 0, CMU_DEVCLKEN0, 3, 0, "tvout"},
	{ CLK_HDMI_DEV,  "hdmi_dev", NULL, 0, CMU_DEVCLKEN0, 5, 0, "hdmi_dev"},
	{ CLK_USB3_480MPLL0,	"usb3_480mpll0",	NULL, 0, CMU_USBPLL, 3, 0, "usb3_480mpll0"},
	{ CLK_USB3_480MPHY0,	"usb3_480mphy0",	NULL, 0, CMU_USBPLL, 2, 0, "usb3_480mphy0"},
	{ CLK_USB3_5GPHY,	"usb3_5gphy",		NULL, 0, CMU_USBPLL, 1, 0, "usb3_5gphy"},
	{ CLK_USB3_CCE,		"usb3_cce",		NULL, 0, CMU_DEVCLKEN0, 25, 0, "usb3_cce"},




	{ CLK_USB2H0_PLLEN,	"usbh0_pllen",	NULL, 0, CMU_USBPLL, 12, 0, "usbh0_pllen"},
	{ CLK_USB2H0_PHY,	"usbh0_phy",	NULL, 0, CMU_USBPLL, 10, 0, "usbh0_phy"},
	{ CLK_USB2H0_CCE,	"usbh0_cce",	NULL, 0, CMU_DEVCLKEN0, 26, 0, "usbh0_cce"},

	{ CLK_USB2H1_PLLEN,	"usbh1_pllen",	NULL, 0, CMU_USBPLL, 13, 0, "usbh1_pllen"},
	{ CLK_USB2H1_PHY,	"usbh1_phy",	NULL, 0, CMU_USBPLL, 11, 0, "usbh1_phy"},
	{ CLK_USB2H1_CCE,	"usbh1_cce",	NULL, 0, CMU_DEVCLKEN0, 27, 0, "usbh1_cce"},
	{ CLK_IRC_SWITCH,	"irc_switch",	NULL, 0, CMU_DEVCLKEN1, 15, 0, "irc_switch"},

#endif
};

static struct owl_composite_clock ats3605_composite_clks[] __initdata = {

	COMP_FIXED_FACTOR_CLK(CLK_DDR, "ddr", CLK_IGNORE_UNUSED,
			C_MUX(ddr_clk_mux_p, CMU_DDRPLL, 9, 1, 0),
			C_GATE(CMU_DEVCLKEN0, 3, 0),
			C_FIXED_FACTOR(1, 2)),

	COMP_DIV_CLK(CLK_NAND, "nand", 0,
			C_MUX(nand_clk_mux_p, CMU_NANDCCLK, 4, 2, 0),
			C_GATE(CMU_DEVCLKEN0, 4,  0),
			C_DIVIDER(CMU_NANDCCLK, 0, 4, nand_div_table,  0)),

	COMP_DIV_CLK(CLK_NAND_NIC, "nand_nic", 0,
			C_MUX(nand_clk_mux_p, CMU_NANDCCLK, 4, 2, 0),
			C_NULL,
			C_DIVIDER(CMU_NANDCCLK, 8, 2, NULL,  CLK_DIVIDER_POWER_OF_TWO)),


	COMP_DIV_CLK(CLK_UART0, "uart0", 0,
			C_MUX(uart_clk_mux_p, CMU_UART0CLK, 16, 1, 0),
			C_GATE(CMU_DEVCLKEN1, 6, 0),
			C_DIVIDER(CMU_UART0CLK, 0, 9, NULL, CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_DIV_CLK(CLK_UART1, "uart1", 0,
			C_MUX(uart_clk_mux_p, CMU_UART1CLK, 16, 1, 0),
			C_GATE(CMU_DEVCLKEN1, 7, 0),
			C_DIVIDER(CMU_UART1CLK, 0, 9, NULL,  CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_DIV_CLK(CLK_UART2, "uart2", 0,
			C_MUX(uart_clk_mux_p, CMU_UART2CLK, 16, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 8,  0),
			C_DIVIDER(CMU_UART2CLK, 0, 9, NULL,  CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_DIV_CLK(CLK_UART3, "uart3", 0,
			C_MUX(uart_clk_mux_p, CMU_UART3CLK, 16, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 19,  0),
			C_DIVIDER(CMU_UART3CLK, 0, 9, NULL,  CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_DIV_CLK(CLK_UART4, "uart4", 0,
			C_MUX(uart_clk_mux_p, CMU_UART4CLK, 16, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 20,  0),
			C_DIVIDER(CMU_UART4CLK, 0, 9, NULL,  CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_DIV_CLK(CLK_UART5, "uart5", 0,
			C_MUX(uart_clk_mux_p, CMU_UART5CLK, 16, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 21,  0),
			C_DIVIDER(CMU_UART5CLK, 0, 9, NULL,  CLK_DIVIDER_ROUND_CLOSEST)),

	COMP_FACTOR_CLK(CLK_SD0, "sd0", 0,
			C_MUX(sd_clk_mux_p, CMU_SD0CLK, 9, 2,  0),
			C_GATE(CMU_DEVCLKEN0, 5,  0),
			C_FACTOR(CMU_SD0CLK, 0, 9, sd_factor_table, 0)),

	COMP_DIV_CLK(CLK_SD0_NIC, "sd0_nic", 0,
			C_MUX(sd_clk_mux_p, CMU_SD0CLK, 9, 2,  0),
			C_NULL,
			C_DIVIDER(CMU_SD0CLK, 16, 2, NULL, CLK_DIVIDER_POWER_OF_TWO)),

	COMP_FACTOR_CLK(CLK_SD1, "sd1", 0,
			C_MUX(sd_clk_mux_p, CMU_SD1CLK, 9, 2, 0),
			C_GATE(CMU_DEVCLKEN0, 6,  0),
			C_FACTOR(CMU_SD1CLK, 0, 9, sd_factor_table,  0)),

	COMP_DIV_CLK(CLK_SD1_NIC, "sd1_nic", 0,
			C_MUX(sd_clk_mux_p, CMU_SD1CLK, 9, 2,  0),
			C_NULL,
			C_DIVIDER(CMU_SD1CLK, 16, 2, NULL, CLK_DIVIDER_POWER_OF_TWO)),

#ifdef CONFIG_MMC_OWL_CLK_NANDPLL
	COMP_FACTOR_CLK(CLK_SD2, "sd2", CLK_SET_RATE_PARENT,
#else
	COMP_FACTOR_CLK(CLK_SD2, "sd2", 0,
#endif
			C_MUX(sd_clk_mux_p, CMU_SD2CLK, 9, 2,  0),
			C_GATE(CMU_DEVCLKEN0, 7,  0),
			C_FACTOR(CMU_SD2CLK, 0, 9, sd_factor_table, 0)),

	COMP_DIV_CLK(CLK_SD2_NIC, "sd2_nic", 0,
			C_MUX(sd_clk_mux_p, CMU_SD2CLK, 9, 2,  0),
			C_NULL,
			C_DIVIDER(CMU_SD2CLK, 16, 2, NULL, CLK_DIVIDER_POWER_OF_TWO)),


	COMP_DIV_CLK(CLK_PWM0, "pwm0", 0,
			C_MUX(pwm_clk_mux_p, CMU_PWM0CLK, 12, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 23,  0),
			C_DIVIDER(CMU_PWM0CLK, 0, 6, NULL,  0)),

	COMP_DIV_CLK(CLK_PWM1, "pwm1", 0,
			C_MUX(pwm_clk_mux_p, CMU_PWM1CLK, 12, 1,  0),
			C_GATE(CMU_DEVCLKEN1, 24,  0),
			C_DIVIDER(CMU_PWM1CLK, 0, 6, NULL,  0)),

	COMP_DIV_CLK(CLK_PWM2, "pwm2", 0,
			C_MUX(pwm_clk_mux_p, CMU_PWM2CLK, 12, 1, 0),
			C_GATE(CMU_DEVCLKEN1, 25, 0),
			C_DIVIDER(CMU_PWM2CLK, 0, 6, NULL, 0)),

	COMP_DIV_CLK(CLK_PWM3, "pwm3", 0,
			C_MUX(pwm_clk_mux_p, CMU_PWM3CLK, 12, 1, 0),
			C_GATE(CMU_DEVCLKEN1, 26,  0),
			C_DIVIDER(CMU_PWM3CLK, 0, 6, NULL, 0)),


	COMP_FACTOR_CLK(CLK_DE, "de_clk", 0,
			C_MUX(de_clk_mux_p, CMU_DECLK, 12, 1,  0),
			C_GATE(CMU_DEVCLKEN0, 8,  0),
			C_NULL),

	COMP_DIV_CLK(CLK_LCD, "lcd_clk", 0,
			C_MUX(lcd_clk_mux_p, CMU_LCDCLK, 12, 2, 0),
			C_GATE(CMU_DEVCLKEN0, 9,  0),
			C_DIVIDER(CMU_LCDCLK, 8, 1, lcd_div_table, 0)),

	COMP_DIV_CLK(CLK_SI, "si", 0,
			C_MUX(de_clk_mux_p, CMU_SICLK, 4, 1, 0),
			C_GATE(CMU_DEVCLKEN0, 14,  0),
			C_DIVIDER(CMU_SICLK, 0, 4, NULL, 0)),


	COMP_DIV_CLK(CLK_SENSOR_OUT, "sensor_out", 0,
			C_MUX(sensor_clk_mux_p, CMU_SENSORCLK, 4, 1, 0),
			C_NULL,
			C_DIVIDER(CMU_SENSORCLK, 8, 4, NULL, 0)),

	COMP_DIV_CLK(CLK_SPDIF, "spdif", 0,
			C_MUX_F(audio_pll_mux_p, 0),
			C_GATE(CMU_DEVCLKEN0, 23,  0),
			C_DIVIDER(CMU_AUDIOPLL, 28, 4, hdmia_div_table, 0)),

	COMP_DIV_CLK(CLK_HDMI_AUDIO, "hdmia", 0,
			C_MUX_F(audio_pll_mux_p, 0),
			C_GATE(CMU_DEVCLKEN0, 22, 0),
			C_DIVIDER(CMU_AUDIOPLL, 24, 4, hdmia_div_table, 0)),

	COMP_DIV_CLK(CLK_I2SRX, "i2srx", 0,
			C_MUX_F(audio_pll_mux_p, 0),
			C_GATE(CMU_DEVCLKEN0, 21, 0),
			C_DIVIDER(CMU_AUDIOPLL, 20, 4, hdmia_div_table, 0)),

	COMP_DIV_CLK(CLK_I2STX, "i2stx", 0,
			C_MUX_F(audio_pll_mux_p , 0),
			C_GATE(CMU_DEVCLKEN0, 20,  0),
			C_DIVIDER(CMU_AUDIOPLL, 16, 4, hdmia_div_table, 0)),

	COMP_FIXED_FACTOR_CLK(CLK_PCM0, "pcm0", 0,
			C_MUX_F(audio_pll_mux_p , 0),
			C_GATE(CMU_DEVCLKEN0, 24, 0),
			C_FIXED_FACTOR(1, 2)),

	COMP_FIXED_FACTOR_CLK(CLK_PCM1, "pcm1", 0,
			C_MUX_F(audio_pll_mux_p , 0),
			C_GATE(CMU_DEVCLKEN1, 16, 0),
			C_FIXED_FACTOR(1, 2)),

	COMP_FACTOR_CLK(CLK_VDE, "vde", 0,
			C_MUX(vde_clk_mux_p, CMU_VDECLK, 4, 2,  0),
			C_GATE(CMU_DEVCLKEN0, 25,  0),
			C_FACTOR(CMU_VDECLK, 0, 3, vde_factor_table, 0)),

	COMP_FACTOR_CLK(CLK_VCE, "vce", 0,
			C_MUX(vce_clk_mux_p, CMU_VCECLK, 4, 3, 0),
			C_GATE(CMU_DEVCLKEN0, 26, 0),
			C_FACTOR(CMU_VCECLK, 0, 3, vde_factor_table, 0)),
#if 0
	COMP_FACTOR_CLK(CLK_GPU3D, "gpu3d", 0,
			C_MUX(gpu_clk_mux_p, CMU_GPU3DCLK, 4, 3, 0),
			C_GATE(CMU_DEVCLKEN0, 8, 0),
			C_FACTOR(CMU_GPU3DCLK, 0, 3, vde_factor_table, 0)),

	COMP_FACTOR_CLK(CLK_HDE, "hde", 0,
			C_MUX(hde_clk_mux_p, CMU_HDECLK, 4, 2, 0),
			C_GATE(CMU_DEVCLKEN0, 9, 0),
			C_FACTOR(CMU_HDECLK, 0, 3, vde_factor_table, 0)),

	COMP_FIXED_FACTOR_CLK(CLK_ETHERNET, "ethernet", 0,
			C_MUX_F(ethernet_clk_mux_p, 0),
			C_GATE(CMU_DEVCLKEN1, 23, 0),
			C_FIXED_FACTOR(1, 20)),
	COMP_DIV_CLK(CLK_THERMAL_SENSOR, "thermal_sensor", 0,
			C_MUX_F(speed_sensor_clk_mux_p, 0),
			C_GATE(CMU_DEVCLKEN0, 31, 0),
			C_DIVIDER(CMU_SSTSCLK, 20, 10, NULL, 0)),
#endif


};


void __init ats3605_clk_init(struct device_node *np)
{
	struct owl_clk_provider *ctx;
	void __iomem *base;

	pr_info("[OWL] ats3605 clock initialization\n");

	base = of_iomap(np, 0);
	if (!base)
		return;

	ctx = owl_clk_init(np, base, CLK_NR_CLKS);
	if (!ctx)
		panic("%s: unable to allocate context.\n", __func__);

	owl_clk_register_fixed_rate(ctx, ats3605_fixed_rate_clks,
			ARRAY_SIZE(ats3605_fixed_rate_clks));

	owl_clk_register_pll(ctx, ats3605_pll_clks,
			ARRAY_SIZE(ats3605_pll_clks));

	owl_clk_register_divider(ctx, ats3605_div_clks,
			ARRAY_SIZE(ats3605_div_clks));

	owl_clk_register_fixed_factor(ctx, ats3605_fixed_factor_clks,
			ARRAY_SIZE(ats3605_fixed_factor_clks));


	owl_clk_register_mux(ctx, ats3605_mux_clks,
			ARRAY_SIZE(ats3605_mux_clks));

	owl_clk_register_gate(ctx, ats3605_gate_clks,
			ARRAY_SIZE(ats3605_gate_clks));

	owl_clk_register_composite(ctx, ats3605_composite_clks,
			ARRAY_SIZE(ats3605_composite_clks));
}
CLK_OF_DECLARE(ats3605_clk, "actions,ats3605-clock", ats3605_clk_init);
