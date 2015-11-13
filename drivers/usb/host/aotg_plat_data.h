/*
 * Actions OWL SoCs usb2.0 controller driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * dengtaiping <dengtaiping@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __AOTG_PLAT_DATA_H__
#define __AOTG_PLAT_DATA_H__

#define CMU_USB0PLL_USBPLLEN	0x1500
#define CMU_USB1PLL_USBPLLEN  0x2a00

#define CMU_DEVRST1_USBH0	0x01
#define CMU_DEVRST1_USBH1	0x02

#define TIMER_BASE   0xE0228000
#define	USBH_0ECS	(TIMER_BASE+0x0094)
#define USBH_1ECS	(TIMER_BASE+0x0098)
#define CMU_DEVRST1 (CMU_BASE+0x00AC)
#define CMU_BASE	0xE0160000
#define CMU_USBPLL	(CMU_BASE+0x0080)

#define SPS_PG      0xE012E000
#define SPS_PG_CTL  (SPS_PG + 0)
#define SPS_PG_ACK  (SPS_PG + 0X0004)
#define USB0_PG_CTL_BITS    (0x1 << 15)
#define USB0_PG_ACK_BITS    (0x1 << 13)
#define USB1_PG_CTL_BITS    (0x1 << 16)
#define USB1_PG_ACK_BITS    (0x1 << 14)

struct aotg_plat_data {
	void __iomem *usbecs;
	void __iomem *usbpll;
	u32 usbpll_bits;
	void __iomem *devrst;
	u32 devrst_bits;
	void __iomem *sps_pg_ctl;
	u32 pg_ctl_bits;
	void __iomem *sps_pg_ack;
	u32 pg_ack_bits;
	int no_hs;

	struct clk *clk_usbh_pllen;
	struct clk *clk_usbh_phy;
	struct clk *clk_usbh_cce;
	int irq;
	struct device *dev;
	void __iomem *base;
	resource_size_t		rsrc_start;	/* memory/io resource start */
	resource_size_t		rsrc_len;	/* memory/io resource length */
};

int aotg0_device_init(int power_only);
void aotg0_device_exit(int power_only);

int aotg1_device_init(int power_only);
void aotg1_device_exit(int power_only);
extern void aotg_hub_unregister(int dev_id);
extern int aotg_hub_register(int dev_id);

#endif
