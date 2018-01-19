/*! \cond USBMONITOR*/
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
 * \file   umonitor_hal.h
 * \brief
 *      usb monitor hardware opration api.
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
#ifndef _UMONITOR_HAL_H_
#define _UMONITOR_HAL_H_

#include "umonitor_plat.h"


void dwc3_set_mode(usb_hal_monitor_t * pdev, int mode);
int dwc3_monitor_vbus_power(usb_hal_monitor_t * pdev, int is_on);
int dwc3_get_dc5v_state(usb_hal_monitor_t * pdev);
int dwc3_get_vbus_state(usb_hal_monitor_t * pdev);
int dwc3_get_idpin_state(usb_hal_monitor_t * pdev, int phase);
unsigned int dwc3_get_linestates(usb_hal_monitor_t * pdev);
int dwc3_set_dp_500k_15k(usb_hal_monitor_t * pdev, int enable_500k_up,
			       int enable_15k_down);
int dwc3_set_soft_id(usb_hal_monitor_t * pdev, int en_softid,
			   int id_state);
int dwc3_set_soft_vbus(usb_hal_monitor_t * pdev, int en_softvbus, int vbus_state);
void dwc3_hal_set_cmu_usbpll(usb_hal_monitor_t *pdev, int enable);
int dwc3_hal_aotg_enable(usb_hal_monitor_t * pdev, int enable);
int dwc3_hal_set_mode(usb_hal_monitor_t * pdev, int mode);
void dwc3_hal_dp_up(usb_hal_monitor_t * pdev);
void dwc3_hal_dp_down(usb_hal_monitor_t * pdev);
int dwc3_hal_is_sof(usb_hal_monitor_t * pdev);
int dwc3_hal_enable_irq(struct usb_hal_monitor *pdev, int enable);
void dwc3_hal_debug(usb_hal_monitor_t * pdev);
int dwc3_suspend_or_resume(usb_hal_monitor_t *pdev, int is_suspend);
void dwc3_controllor_init(usb_hal_monitor_t * pdev);
void dwc3_controllor_exit(usb_hal_monitor_t * pdev);
void dwc3_controllor_mode_cfg(usb_hal_monitor_t *pdev);



void usb2_set_mode(usb_hal_monitor_t * pdev, int mode);
int usb2_monitor_vbus_power(usb_hal_monitor_t * pdev, int is_on);
int usb2_get_dc5v_state(usb_hal_monitor_t * pdev);
int usb2_get_vbus_state(usb_hal_monitor_t * pdev);
int usb2_get_idpin_state(usb_hal_monitor_t * pdev, int phase);
unsigned int usb2_get_linestates(usb_hal_monitor_t * pdev);
int usb2_set_dp_500k_15k(usb_hal_monitor_t * pdev, int enable_500k_up,
			       int enable_15k_down);
int usb2_set_soft_id(usb_hal_monitor_t * pdev, int en_softid,
			   int id_state);
int usb2_set_soft_vbus(usb_hal_monitor_t * pdev, int en_softvbus, int vbus_state);
void usb2_hal_set_cmu_usbpll(usb_hal_monitor_t *pdev, int enable);
int usb2_hal_aotg_enable(usb_hal_monitor_t * pdev, int enable);
int usb2_hal_set_mode(usb_hal_monitor_t * pdev, int mode);
void usb2_hal_dp_up(usb_hal_monitor_t * pdev);
void usb2_hal_dp_down(usb_hal_monitor_t * pdev);
int usb2_hal_is_sof(usb_hal_monitor_t * pdev);
int usb2_hal_enable_irq(struct usb_hal_monitor *pdev, int enable);
void usb2_hal_debug(usb_hal_monitor_t * pdev);
int usb2_suspend_or_resume(usb_hal_monitor_t *pdev, int is_suspend);
void usb2_controllor_init(usb_hal_monitor_t * pdev);
void usb2_controllor_exit(usb_hal_monitor_t * pdev);
void usb2_controllor_mode_cfg(usb_hal_monitor_t *pdev);

#endif  /* _UMONITOR_HAL_H_ */
/*! \endcond*/
