/*
 * Actions OWL SoCs phy driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * tangshaoqing <tangshaoqing@actions-semi.com>
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


#ifndef __PHY_OWL_USB_H
#define __PHY_OWL_USB_H


#include <linux/usb/phy.h>

enum {
	PHY_S700 = 0,
	PHY_S900 = 0x100
};

struct phy_owl_data {
	int ic_type;
};


struct owl_usbphy {
	struct usb_phy	phy;
	struct device	*dev;
	struct phy_owl_data *data;
	void __iomem	*regs;
};

#define phy_to_sphy(x)		container_of((x), struct owl_usbphy, phy)


#endif /* __PHY_OWL_USB_H */

