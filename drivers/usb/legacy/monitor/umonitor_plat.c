/*********************************************************************************
*                            Module: usb monitor driver
*                (c) Copyright 2003 - 2008, Actions Co,Ld.
*                        All Right Reserved
*
* History:
*      <author>      <time>       <version >    <desc>
*       houjingkun   2011/07/08   1.0         build this file
********************************************************************************/

/*!
 * \file   umonitor_hal.c
 * \brief
 *      usb monitor hardware opration api code.
 * \author houjingkun
 * \par GENERAL DESCRIPTION:
 * \par EXTERNALIZED FUNCTIONS:
 *       null
 *
 *  Copyright(c) 2008-2012 Actions Semiconductor, All Rights Reserved.
 *
 * \version 1.0
 * \date  2011/07/08
 *******************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <mach/hardware.h>

#include "aotg_regs.h"
//#include "umonitor_plat.h"
#include "umonitor_hal.h"


int usb_hal_init_monitor_hw_ops(usb_hal_monitor_t * pdev,
	umon_port_config_t * config, unsigned int plat)
{
	int ret = 0;

	pdev->name = "usb controller";

	switch(plat) {
		case DWC3_PLATFORM:
			pdev->register_base_addr = USB3_REGISTER_BASE;
			pdev->usbecs = USB3_P0_CTL;
			pdev->usbpll = CMU_USBCLK;
			pdev->devrst = CMU_DEVRST1;
			pdev->vbus_power_onoff = dwc3_monitor_vbus_power;
			pdev->get_dc5v_state = dwc3_get_dc5v_state;
			pdev->get_vbus_state = dwc3_get_vbus_state;
			pdev->get_linestates = dwc3_get_linestates;
			pdev->get_idpin_state = dwc3_get_idpin_state;
			pdev->set_dp_500k_15k = dwc3_set_dp_500k_15k;
			pdev->set_soft_id = dwc3_set_soft_id;
			pdev->set_soft_vbus = dwc3_set_soft_vbus;
			pdev->aotg_enable = dwc3_hal_aotg_enable;
			pdev->set_mode = dwc3_hal_set_mode;
			pdev->set_cmu_usbpll = dwc3_hal_set_cmu_usbpll;
			pdev->dp_up = dwc3_hal_dp_up;
			pdev->dp_down = dwc3_hal_dp_down;
			pdev->is_sof = dwc3_hal_is_sof;
			pdev->enable_irq = dwc3_hal_enable_irq;
			pdev->debug = dwc3_hal_debug;
			pdev->suspend_or_resume = dwc3_suspend_or_resume;
			pdev->dwc3_otg_mode_cfg = dwc3_controllor_mode_cfg;
			pdev->dwc3_otg_exit = dwc3_controllor_exit;
			pdev->dwc3_otg_init = dwc3_controllor_init;
			break;
		case USB2_PLATFORM:
			pdev->register_base_addr = USB0_BASE;
			pdev->usbecs = USB2_0ECS;
			pdev->usbpll = CMU_USBCLK;
			pdev->devrst = CMU_DEVRST1;
			pdev->vbus_power_onoff = usb2_monitor_vbus_power;
			pdev->get_dc5v_state = usb2_get_dc5v_state;
			pdev->get_vbus_state = usb2_get_vbus_state;
			pdev->get_linestates = usb2_get_linestates;
			pdev->get_idpin_state = usb2_get_idpin_state;
			pdev->set_dp_500k_15k = usb2_set_dp_500k_15k;
			pdev->set_soft_id = usb2_set_soft_id;
			pdev->set_soft_vbus = usb2_set_soft_vbus;
			pdev->aotg_enable = usb2_hal_aotg_enable;
			pdev->set_mode = usb2_hal_set_mode;
			pdev->set_cmu_usbpll = usb2_hal_set_cmu_usbpll;
			pdev->dp_up = usb2_hal_dp_up;
			pdev->dp_down = usb2_hal_dp_down;
			pdev->is_sof = usb2_hal_is_sof;
			pdev->enable_irq = usb2_hal_enable_irq;
			pdev->debug = usb2_hal_debug;
			pdev->suspend_or_resume = usb2_suspend_or_resume;
			pdev->dwc3_otg_mode_cfg = usb2_controllor_mode_cfg;
			pdev->dwc3_otg_exit = usb2_controllor_exit;
			pdev->dwc3_otg_init = usb2_controllor_init;
			break;
		default:
			break;
	}

	pdev->io_base = (unsigned int)(void __iomem *)IO_ADDRESS(pdev->register_base_addr);
	if (!pdev->io_base) {
		MONITOR_ERR("ioremap failed\n");
		return -ENOMEM;
	}
	MONITOR_PRINTK("pdev->io_base is %08x\n",(unsigned int )pdev->io_base);

	pdev->config = config;

	return ret;
}
