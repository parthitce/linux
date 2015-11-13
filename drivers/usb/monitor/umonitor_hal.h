/*
 * Actions OWL SoCs usb monitor driver
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

#ifndef _UMONITOR_HAL_H_
#define _UMONITOR_HAL_H_

#include "umonitor_config.h"

#define ECS_DPPDDIS     (1<<0)
#define ECS_DMPDDIS    (1<<1)
#define ECS_DPPUEN      (1<<2)
#define ECS_DMPUEN     (1<<3)
/*VBUS detection threshold control. reg USB3_P0_CTL [2:0]*/
#define VBUS_DET_THRESHOLD_MASK     (7<<4)    /*100 4.75v*/
#define VBUS_DET_THRESHOLD_LEVEL0     (0x00<<4)    /*100 4.75v*/
#define VBUS_DET_THRESHOLD_LEVEL1     (0x01<<4)    /*4.45v*/
#define VBUS_DET_THRESHOLD_LEVEL2     (0x02<<4)    /*4.00v*/
#define VBUS_DET_THRESHOLD_LEVEL3     (0x03<<4)    /*3.65v*/
#define VBUS_DET_THRESHOLD_LEVEL4     (0x04<<4)    /*3.00v*/

typedef struct usb_hal_monitor {
	char *name;
	unsigned int usbecs_val;
	void __iomem *io_base;
	void __iomem *usbecs;
	void __iomem *usbpll;
	unsigned int usbpll_bits;
	void __iomem *devrst;
	unsigned int devrst_bits;
	unsigned int devclk;
	unsigned int devclk_bits;

	umon_port_config_t *config;

	int ic_type;
	int (*vbus_power_onoff)(struct usb_hal_monitor *pdev, int is_on);

#define USB_DC5V_LOW               0
#define USB_DC5V_HIGH              1
#define USB_DC5V_INVALID           2
	int (*get_dc5v_state)(struct usb_hal_monitor *pdev);

#define USB_VBUS_LOW               0
#define USB_VBUS_HIGH              1
#define USB_VBUS_INVALID           2
	int (*get_vbus_state)(struct usb_hal_monitor *pdev);

	/* return state of linestate[1:0]. */
	unsigned int (*get_linestates)(struct usb_hal_monitor *pdev);

#define USB_ID_STATE_INVALID    0
#define USB_ID_STATE_DEVICE     1
#define USB_ID_STATE_HOST       2
	int (*get_idpin_state)(struct usb_hal_monitor *pdev);

	int (*set_dp_500k_15k)(struct usb_hal_monitor *pdev, int enable_500k_up, int enable_15k_down);
	int (*set_soft_id)(struct usb_hal_monitor *pdev, int en_softid, int id_state);
	int (*set_soft_vbus)(struct usb_hal_monitor *pdev, int en_softvbus, int vbus_state);

	int (*aotg_enable)(struct usb_hal_monitor *pdev, int enable);

	/* used for enter certain otg states. */
#define USB_IN_DEVICE_MOD   1
#define USB_IN_HOST_MOD     0
	int (*set_mode)(struct usb_hal_monitor *pdev, int mode);
	void (*dwc_set_mode)(struct usb_hal_monitor *pdev, int mode);
	void (*set_cmu_usbpll)(struct usb_hal_monitor *pdev, int enable);
	void (*dp_up)(struct usb_hal_monitor *pdev);
	void (*dp_down)(struct usb_hal_monitor *pdev);
	int (*is_sof)(struct usb_hal_monitor *pdev);
	int (*enable_irq)(struct usb_hal_monitor *pdev, int enable);
	int (*suspend_or_resume)(struct usb_hal_monitor *pdev, int is_suspend);
	void (*dwc3_otg_mode_cfg)(struct usb_hal_monitor *pdev);
	int (*monitor_get_usb_plug_type)(struct usb_hal_monitor *pdev);
	void (*dwc3_otg_init)(struct usb_hal_monitor *pdev);
	void (*dwc3_otg_exit)(struct usb_hal_monitor *pdev);
	void (*debug)(struct usb_hal_monitor *pdev);
} usb_hal_monitor_t;

int usb_hal_init_monitor_hw_ops(usb_hal_monitor_t *pdev, umon_port_config_t *config, void __iomem *base);

/* todo */

#endif  /* _UMONITOR_HAL_H_ */
/*! \endcond*/
