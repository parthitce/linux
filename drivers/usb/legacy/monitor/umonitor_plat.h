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
#ifndef _UMONITOR_PLAT_H_
#define _UMONITOR_PLAT_H_

#include "umonitor_config.h"

#define	DWC3_PLATFORM 0   /*for some serials IC*/
#define	USB2_PLATFORM 1


typedef struct usb_hal_monitor {
    char * name;
    unsigned int register_base_addr;
    unsigned int usbecs_val;
    unsigned int io_base;
    unsigned int usbecs;
    unsigned int usbpll;
    unsigned int usbpll_bits;
    unsigned int devrst;
    unsigned int devrst_bits;
    unsigned int devclk;
    unsigned int devclk_bits;

    umon_port_config_t * config;

    int (* vbus_power_onoff)(struct usb_hal_monitor *pdev, int is_on);

    #define USB_DC5V_LOW               0
    #define USB_DC5V_HIGH              1
    #define USB_DC5V_INVALID           2
    int (* get_dc5v_state)(struct usb_hal_monitor *pdev);

    #define USB_VBUS_LOW               0
    #define USB_VBUS_HIGH              1
    #define USB_VBUS_INVALID           2
    int (* get_vbus_state)(struct usb_hal_monitor *pdev);

    /* return state of linestate[1:0]. */
    unsigned int (* get_linestates)(struct usb_hal_monitor *pdev);

    /*
    * ���id pin��״̬,��Ҫ������gpioȥ��idpin�������,
    * ����������ô˺���ʱ����Ϊ0 (��ʾ�����Ч).
    * retval:
    * USB_ID_STATE_INVALID -- �����Ч,��ʱ���ü��idpin��״̬;
    * USB_ID_STATE_DEVICE -- ��⵽idpinΪ��,����device���׶�;
    * USB_ID_STATE_HOST -- ��⵽idpinΪ��,����host���׶�;
    */
    #define USB_ID_STATE_INVALID    0
    #define USB_ID_STATE_DEVICE     1
    #define USB_ID_STATE_HOST       2
    int (* get_idpin_state)(struct usb_hal_monitor *pdev, int phase);

    int (* set_dp_500k_15k)(struct usb_hal_monitor *pdev, int enable_500k_up, int enable_15k_down);
    int (* set_soft_id)(struct usb_hal_monitor *pdev, int en_softid, int id_state);
    int (* set_soft_vbus)(struct usb_hal_monitor *pdev, int en_softvbus, int vbus_state);

    int (* aotg_enable)(struct usb_hal_monitor *pdev, int enable);

    /* used for enter certain otg states. */
    #define USB_IN_DEVICE_MOD   1
    #define USB_IN_HOST_MOD     0
    int (* set_mode)(struct usb_hal_monitor *pdev, int mode);
    void (* dwc_set_mode)(struct usb_hal_monitor *pdev, int mode);
    void (* set_cmu_usbpll)(struct usb_hal_monitor *pdev, int enable);
    void (* dp_up)(struct usb_hal_monitor *pdev);
    void (* dp_down)(struct usb_hal_monitor *pdev);
    int (* is_sof)(struct usb_hal_monitor *pdev);
    int (* enable_irq)(struct usb_hal_monitor *pdev, int enable);
    int (* suspend_or_resume)(struct usb_hal_monitor *pdev, int is_suspend);
    void (* dwc3_otg_mode_cfg)(struct usb_hal_monitor *pdev);
    void (* dwc3_otg_init)(struct usb_hal_monitor *pdev);
    void (* dwc3_otg_exit)(struct usb_hal_monitor *pdev);
    void (* debug)(struct usb_hal_monitor *pdev);
} usb_hal_monitor_t;

int usb_hal_init_monitor_hw_ops(usb_hal_monitor_t * pdev, umon_port_config_t * config, unsigned int plat);

/* todo */

#endif  /* _UMONITOR_PLAT_H_ */
/*! \endcond*/
