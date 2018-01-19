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
 * \file   umonitor_config.h
 * \brief
 *      usb monitor configure headfile.
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
#ifndef _UMONITOR_CONFIG_H_
#define _UMONITOR_CONFIG_H_

//#ifdef ATM7029_EVB
//#define GPIO_VBUS_SUPPLY    0x28      //KS_OUT1 GPIOB8   001  01000
//#endif
//
//#ifdef ATM7029_DEMO
//#define GPIO_VBUS_SUPPLY    0x11      //RMII_RXER GPIPA17 010 10001
//#endif

#define CHECK_TIMER_INTERVAL  1000


#define __GPIO_GROUP(x)     ((x) >> 5)
#define __GPIO_NUM(x)       ((x) & 0x1f)

typedef struct umon_port_config {
    #define UMONITOR_DISABLE   		   0
    #define UMONITOR_DEVICE_ONLY       	   1
    #define UMONITOR_HOST_ONLY	           2
    #define UMONITOR_HOST_AND_DEVICE       3
    unsigned char detect_type;	/* usb port detect request. */
    /* if detect_type == UMONITOR_DISABLE, below is no use. */

    unsigned char port_type;
	#define PORT_DWC3   		   0
	#define PORT_USB2       	   1


    #define UMONITOR_USB_IDPIN   	   0
    #define UMONITOR_SOFT_IDPIN       	   1
    #define UMONITOR_GPIO_IDPIN	           2  /* gpio detect idpin. */
	#define UMONITOR_ADC_IDPIN	           3
    unsigned char idpin_type;
    /*
     * if idpin_type set to UMONITOR_USB_IDPIN or UMONITOR_SOFT_IDPIN,
     * below is no use.
     */
    unsigned char idpin_gpio_group;
    unsigned char idpin_gpio_no;

    #define UMONITOR_DC5V_VBUS             20  /* use dc5v to detect vbus. */

#define UMONITOR_USB_VBUS          0
#define UMONITOR_GPIO_DET_VBUS             1  /* gpio detect vbus. */
#define UMONITOR_GPIO_CTL_VBUS             2  /* gpio controll vbus. */
#define UMONITOR_GPIO_DET_CTL_VBUS      3
#define UMONITOR_SOFT_VBUS             10


    unsigned char vbus_type;
    /*
     * if vbus_type set to UMONITOR_USB_VBUS, below is no use.
     */
    unsigned char vbus_gpio_group;
    unsigned char vbus_gpio_no;

	 unsigned int vbus_det_gpio;
    unsigned int vbus_ctl_gpio;
    unsigned int idpin_gpio;
	unsigned int idpin_adc_threshold_min;
	unsigned int idpin_adc_threshold_max;
	unsigned int idpin_adc_threshold_min_2;
	unsigned int idpin_adc_threshold_max_2;
    unsigned int vbus_active_level;

    /* in host state, if vbus power on use gpio, set it. */
    unsigned char poweron_gpio_group;
    unsigned char poweron_gpio_no;
    unsigned char poweron_active_level;
    /* in host state, if vbus power switch onoff use gpio, set it. */
    unsigned char power_switch_gpio_group;
    unsigned char power_switch_gpio_no;
    unsigned char power_switch_active_level;
} umon_port_config_t;


/*debug flag*/
//#define 	DEBUG_MONITOR
#define   ERR_MONITOR

#ifdef DEBUG_MONITOR
#define MONITOR_DEBUG(fmt, args...)    printk(KERN_DEBUG "<MON %s:%d>"fmt, __func__, __LINE__,## args)
#else
#define MONITOR_DEBUG(fmt, args...)    /*not printk*/
#endif

#define MONITOR_NOTICE(fmt, args...)    printk(KERN_NOTICE "<MON %s:%d>"fmt, __func__, __LINE__,## args)


#define MONITOR_PRINTK(fmt, args...)    /*not printk*/


#ifdef ERR_MONITOR
#define MONITOR_ERR(fmt, args...)    printk(KERN_ERR fmt, ## args)
#else
#define MONITOR_ERR(fmt, args...)    /*not printk*/
#endif

#endif  /* _UMONITOR_CONFIG_H_ */
/*! \endcond*/

