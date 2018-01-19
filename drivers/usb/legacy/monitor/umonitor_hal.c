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
#include <linux/mfd/atc260x/atc260x.h>

#include "aotg_regs.h"
#include "umonitor_hal.h"


extern unsigned int otgvbus_gpio;

extern unsigned int is_charger_adaptor_type_only_support_usb(void);

static int aotg_soft_idpin = 0;

void dwc3_set_mode(usb_hal_monitor_t * pdev, int mode)
{
	u32 reg;

	reg = act_readl(pdev->io_base + DWC3_GCTL);
	reg &= ~(DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG));
	reg |= DWC3_GCTL_PRTCAPDIR(mode);
	act_writel(reg, pdev->io_base + DWC3_GCTL);
}


int dwc3_monitor_vbus_power(usb_hal_monitor_t * pdev, int is_on)
{
  MONITOR_PRINTK("usb_monitor_vbus_power %04x\n", is_on);
	if(is_on){
		atc260x_enable_vbusotg(is_on);
		gpio_set_value_cansleep(otgvbus_gpio, is_on);
	}else{
		gpio_set_value_cansleep(otgvbus_gpio, is_on);
		atc260x_enable_vbusotg(is_on);
	}
	return 0;
}

int dwc3_get_dc5v_state(usb_hal_monitor_t * pdev)
{
	/* no use. */
	return USB_DC5V_INVALID;
}

int dwc3_get_vbus_state(usb_hal_monitor_t * pdev)
{
	int ret = USB_VBUS_INVALID;
	s32 adc_val;

	switch (pdev->config->vbus_type) {
	case UMONITOR_USB_VBUS:
		/* vbus valid. */
		ret = act_readl(pdev->usbecs) & (1 << USB3_P0_CTL_VBUS_P0);
		ret = ret ? USB_VBUS_HIGH: USB_VBUS_LOW;
		break;
	case UMONITOR_GPIO_DET_VBUS:
		break;
	case UMONITOR_DC5V_VBUS:
		ret = atc260x_ex_auxadc_read_by_name("VBUSV", &adc_val);
		if (ret < 0)
			break;
		if (is_charger_adaptor_type_only_support_usb())
			ret = (adc_val > VBUS_THRESHOLD_3800) ? USB_VBUS_HIGH: USB_VBUS_LOW;
		else
			ret = (adc_val > VBUS_THRESHOLD_3600) ? USB_VBUS_HIGH: USB_VBUS_LOW;
		break;
	default:
		break;
	}

	MONITOR_PRINTK("%s state: %u\n", __func__, ret);
	return ret;
}

int dwc3_hardware_init(usb_hal_monitor_t * pdev);
int dwc3_get_idpin_state(usb_hal_monitor_t * pdev, int phase)
{
	int ret = USB_ID_STATE_INVALID;

	switch (pdev->config->idpin_type) {
	case UMONITOR_USB_IDPIN:
		ret = (act_readl(pdev->usbecs) & (1 << USB3_P0_CTL_ID_P0)) ? USB_ID_STATE_DEVICE:USB_ID_STATE_HOST;
		break;
	case UMONITOR_SOFT_IDPIN:
	case UMONITOR_GPIO_IDPIN:
	default:
		break;
	}
	return ret;
}

unsigned int dwc3_get_linestates(usb_hal_monitor_t * pdev)
{
	unsigned int ls;

	ls = ((act_readl(pdev->usbecs) & USB3_P0_CTL_LS_P0_MASK) >> USB3_P0_CTL_LS_P0_SHIFT);
	return ls;
}

int dwc3_set_dp_500k_15k(usb_hal_monitor_t * pdev, int enable_500k_up,
			       int enable_15k_down)
{
	unsigned int val;

	val = act_readl(pdev->usbecs) & (~((1 << USB3_P0_CTL_DPPUEN_P0) |
			(1 << USB3_P0_CTL_DMPUEN_P0) |
			(1 << USB3_P0_CTL_DPPDDIS_P0) |
			(1 << USB3_P0_CTL_DMPDDIS_P0)));

	if (enable_500k_up != 0) {
		val |= (1 << USB3_P0_CTL_DPPUEN_P0)|(1 << USB3_P0_CTL_DMPUEN_P0);
	}
	if (enable_15k_down == 0) {
		val |= (1 << USB3_P0_CTL_DPPDDIS_P0)|(1 << USB3_P0_CTL_DMPDDIS_P0);
	}

	MONITOR_PRINTK("dpdm is %08x\n", val);
	act_writel(val, pdev->usbecs);	/* 500k up enable, 15k down disable; */
	return 0;

}

int dwc3_set_soft_id(usb_hal_monitor_t * pdev, int en_softid,
			   int id_state)
{
	unsigned int val;

	if (pdev->config->idpin_type == UMONITOR_USB_IDPIN) {
		/* ignore soft idpin setting. */
		en_softid = 0;
	}
	val = act_readl(pdev->usbecs);
	if (en_softid != 0) {
		val |= 0x1 << USB3_P0_CTL_SOFTIDEN_P0;	/* soft id enable. */
	} else {
		val &= ~(0x1 << USB3_P0_CTL_SOFTIDEN_P0);	/* soft id disable. */
	}

	if (id_state != 0) {
		val |= (0x1 << USB3_P0_CTL_SOFTID_P0);
	} else {
		val &= ~(0x1 << USB3_P0_CTL_SOFTID_P0);
	}
	act_writel(val, pdev->usbecs);
	return 0;
}

int dwc3_set_soft_vbus(usb_hal_monitor_t * pdev, int en_softvbus, int vbus_state)
{
	unsigned int val;

	if (pdev->config->vbus_type == UMONITOR_USB_VBUS) {
		/* ignore soft vbus setting. */
		en_softvbus = 0;
	}

	val = act_readl(pdev->usbecs);
	if (en_softvbus != 0) {
		val |= 0x1 << USB3_P0_CTL_SOFTVBUSEN_P0;	/* soft id enable. */
	} else {
		val &= ~(0x1 << USB3_P0_CTL_SOFTVBUSEN_P0);	/* soft id disable. */
	}

	if (vbus_state != 0) {
		val |= (0x1 << USB3_P0_CTL_SOFTVBUS_P0);
	}else {
		val &= ~(0x1 << USB3_P0_CTL_SOFTVBUS_P0);
	}
	act_writel(val, pdev->usbecs);

	return 0;
}

void dwc3_hal_set_cmu_usbpll(usb_hal_monitor_t *pdev, int enable)
{
	if (enable != 0) {
		act_writel(act_readl(pdev->usbpll) | (0x1f), pdev->usbpll);
	} else {
		act_writel(act_readl(pdev->usbpll) & ~(0x1f),pdev->usbpll);
	}
	mdelay(2);
}

/******************************************************************************/
/*!
 * \par  Description:
 *       对usb模块进行初始化（包括开启时钟，复位整个usb硬件模块，并确保复位在返回前完成
 * \param[in] void
 * \param[in] void
 * \return    int
 * \retval    0：usb模块初始化完成    负值：usb模块初始化失败
 * \ingroup   hal_usb
 * \par
 * \note
 *
 *******************************************************************************/
int dwc3_hardware_init(usb_hal_monitor_t * pdev)
{
	act_writel((act_readl(pdev->usbecs) | 0x0000f000), pdev->usbecs);
	//set VBUS detection threshold,VBUS_DET_THRESHOLD_LEVEL4
	act_writel((act_readl(pdev->usbecs) | USB3_P0_CTL_VBUS_THRESHOLD), pdev->usbecs);

  MONITOR_PRINTK("usbecs value is %08x------>/n", act_readl(pdev->usbecs));

	return 0;
}

/******************************************************************************/
/*!
* \par  Description:
*     enable or disable the usb controller
* \param[in]    aotg  contains the controller info
* \param[in]    enable  enable(1) or disable(0) the controller
* \return       the result
* \retval           0 sucess
* \retval           1 failed
* \ingroup      USB_UOC
*******************************************************************************/
int dwc3_hal_aotg_enable(usb_hal_monitor_t * pdev, int enable)
{
	if (enable != 0) {
		MONITOR_PRINTK("aotg mon enable\n");
		if (dwc3_hardware_init(pdev) != 0) {
			return -1;
		}
	} else {
		MONITOR_PRINTK("aotg mon disable\n");
	}
	return 0;
}

int dwc3_hal_set_mode(usb_hal_monitor_t * pdev, int mode)
{
	if (mode == USB_IN_DEVICE_MOD) {
		act_writel(act_readl(pdev->usbecs) | (0xf << 12), pdev->usbecs);
	}
	if (mode == USB_IN_HOST_MOD) {
		act_writel(act_readl(pdev->usbecs) & ((~0xf) << 12), pdev->usbecs);
	}
	return 0;
}

void dwc3_hal_dp_up(usb_hal_monitor_t * pdev)
{
	return;
}

void dwc3_hal_dp_down(usb_hal_monitor_t * pdev)
{
	return;
}

int dwc3_hal_is_sof(usb_hal_monitor_t * pdev)
{
  return 0;
}

int dwc3_hal_enable_irq(struct usb_hal_monitor *pdev, int enable)
{
	return 0;
}

void dwc3_hal_debug(usb_hal_monitor_t * pdev)
{
	printk("%s:%d\n", __FILE__, __LINE__);
	printk("pdev->usbecs addr: 0x%x\n\n", pdev->usbecs);
	return;
}

int dwc3_suspend_or_resume(usb_hal_monitor_t *pdev, int is_suspend)
{
	/* save/reload usbecs when suspend/resume */
	if (is_suspend) {
		pdev->usbecs_val = act_readl(pdev->usbecs);
	}else{
		dwc3_hardware_init(pdev);
		//act_writel(pdev->usbecs_val, pdev->usbecs);
	}
	return 0;
}

void dwc3_controllor_init(usb_hal_monitor_t * pdev)
{
  u32 reg;

	/*USB3 PLL enable*/
	act_writel(act_readl(pdev->usbpll) | (0x1f), pdev->usbpll);
	udelay(1000);

  /*USB3 Cmu Reset */
  reg = act_readl(pdev->devrst);
	reg &= ~(USB3_MOD_RST);
	act_writel(reg, pdev->devrst);
	udelay(100);

  reg = act_readl(pdev->devrst);
	reg |= (USB3_MOD_RST);
	act_writel(reg, pdev->devrst);
	udelay(100);

	/*PLL1 enable*/
	reg = act_readl(pdev->io_base + DWC3_CMU_DEBUG_LDO);
	reg |= CMU_BIAS_EN;
	act_writel(reg, pdev->io_base + DWC3_CMU_DEBUG_LDO);

	/*PLL2 enable*/
	reg = (BIST_QINIT(0x3) | EYE_HEIGHT(0x4) | PLL2_LOCK |
			PLL2_RS(0x2) | PLL2_ICP(0x1) | CMU_SEL_PREDIV |
			CMU_DIVX2 | PLL2_DIV(0x17) |PLL2_POSTDIV(0x3) |
			PLL2_PU);
	act_writel(reg, pdev->io_base + DWC3_CMU_PLL2_BISTDEBUG);

	/*USB2 LDO enable*/
	reg = act_readl(pdev->usbecs );
	reg |= (1 << USB3_P0_CTL_PLLLDOEN )|(2 << USB3_P0_CTL_LDOVREFSEL_SHIFT);
	act_writel(reg, pdev->usbecs );

	/*dwc3 core reset*/
  reg = act_readl(pdev->io_base + DWC3_GCTL);
	reg |= DWC3_GCTL_CORESOFTRESET;
	act_writel(reg, pdev->io_base + DWC3_GCTL);
	udelay(100);
}

void dwc3_controllor_exit(usb_hal_monitor_t * pdev)
{
	u32 reg;

	/*USB3 PLL disable*/
	act_writel(act_readl(pdev->usbpll) & ~(0x1f),pdev->usbpll);

	/*PLL1 disable*/
	reg = act_readl(pdev->io_base + DWC3_CMU_DEBUG_LDO);
	reg &= ~CMU_BIAS_EN;
	act_writel(reg, pdev->io_base + DWC3_CMU_DEBUG_LDO);

	/*PLL2 disable*/
	reg = act_readl(pdev->io_base + DWC3_CMU_PLL2_BISTDEBUG);
	reg &= ~(BIST_QINIT(0x3) | EYE_HEIGHT(0x4) | PLL2_LOCK |
			PLL2_RS(0x2) | PLL2_ICP(0x1) | CMU_SEL_PREDIV |
			CMU_DIVX2 | PLL2_DIV(0x17) |PLL2_POSTDIV(0x3) |
			PLL2_PU);
	act_writel(reg, pdev->io_base + DWC3_CMU_PLL2_BISTDEBUG);

	/*USB2 LDO disable*/
	reg = act_readl(pdev->usbecs );
	reg &= ~((1 << USB3_P0_CTL_PLLLDOEN )|(2 << USB3_P0_CTL_LDOVREFSEL_SHIFT));
	act_writel(reg, pdev->usbecs );

	udelay(100);
}

/*dwc3_controllor_otg_cfg函数调用有以下3种情况;主要目的是为了设置主控器处于otg模式;
以防止id pin的状态在其它模式下检测不准.调用的情况如下,resume函数,检测到usb拔出,检测
到u盘拔出.*/
/*dwc3_controllor_exit函数会将usbpll关掉,目前发现有风险,不能在这里调用.因拔线情况下,
dwc3(usb)驱动可能还未卸载,而仍然可能访问usb主控制器,而这时已经关掉了pll,会导致问题.*/
void dwc3_controllor_mode_cfg(usb_hal_monitor_t *pdev)
{
  u32 value;
  /*不为0,表示usb3之前的频率打开,驱动存在;故这边不需要打开频率;退出时亦不需要关闭频率*/
  value = act_readl(pdev->usbpll) & 0x1f;
  if(value == 0)
    dwc3_controllor_init(pdev);

  dwc3_set_mode(pdev, DWC3_GCTL_PRTCAP_OTG);

  if(value == 0)
    dwc3_controllor_exit(pdev);
}



void usb2_set_mode(usb_hal_monitor_t * pdev, int mode)
{
}

// TODO: high-active or low-active: pdev->config->power_switch_active_level
int usb2_monitor_vbus_power(usb_hal_monitor_t * pdev, int is_on)
{
  MONITOR_PRINTK("usb_monitor_vbus_power %04x\n", is_on);
	if(is_on){
		atc260x_enable_vbusotg(is_on);
		gpio_set_value_cansleep(otgvbus_gpio, is_on);
	}else{
		gpio_set_value_cansleep(otgvbus_gpio, is_on);
		atc260x_enable_vbusotg(is_on);
	}
	return 0;
}

int usb2_get_dc5v_state(usb_hal_monitor_t * pdev)
{
	/* no use. */
	return USB_DC5V_INVALID;
}

int usb2_get_vbus_state(usb_hal_monitor_t * pdev)
{
	int ret = USB_VBUS_INVALID;
	s32 adc_val;

	switch (pdev->config->vbus_type) {
	case UMONITOR_USB_VBUS:
		/* vbus valid. */
		  ret = act_readl(pdev->usbecs) & (1 << USB2_ECS_VBUS_P0);
		  ret = ret ? USB_VBUS_HIGH: USB_VBUS_LOW;
		  break;
	case UMONITOR_GPIO_DET_VBUS:
		break;
	case UMONITOR_DC5V_VBUS:
		ret = atc260x_ex_auxadc_read_by_name("VBUSV", &adc_val);
		if (ret < 0)
			break;
		if (is_charger_adaptor_type_only_support_usb())
			ret = (adc_val > VBUS_THRESHOLD_3800) ? USB_VBUS_HIGH: USB_VBUS_LOW;
		else
			ret = (adc_val > VBUS_THRESHOLD_3600) ? USB_VBUS_HIGH: USB_VBUS_LOW;
		  break;
	  default:
		  break;
	}

	MONITOR_PRINTK("%s state: %u\n", __func__, ret);
	return ret;
}

int usb2_hardware_init(usb_hal_monitor_t * pdev);
int usb2_get_idpin_state(usb_hal_monitor_t * pdev, int phase)
{
	int ret = USB_ID_STATE_INVALID;
	s32 idpin_adc_val;

	switch (pdev->config->idpin_type) {
	case UMONITOR_USB_IDPIN:
		if (aotg_soft_idpin) {
			ret = USB_ID_STATE_DEVICE;	/* force to send A_OUT message. */
			if (phase) {
				aotg_soft_idpin = 0;
			}
			break;
		}

		if(phase) {
			if (act_readl(pdev->usbecs) & (0x1 << 31)) {
				phase = 0;
			}
		}
		/*id state ok;need firstly set USB2_0ECS[31]=1 (bug)*/
		if(phase) {
			act_writel(act_readl(pdev->usbecs) | (1 << 31), pdev->usbecs);
			udelay(2);
		}
		ret = (act_readl(pdev->usbecs) & (1 << USB2_ECS_ID_P0)) ? USB_ID_STATE_DEVICE: USB_ID_STATE_HOST;

		if(phase)
			act_writel(act_readl(pdev->usbecs) & ~(1 << 31), pdev->usbecs);
		break;
	case UMONITOR_SOFT_IDPIN:
		break;
	case UMONITOR_GPIO_IDPIN:
		 if(0==gpio_get_value_cansleep(pdev->config->idpin_gpio))
              ret=USB_ID_STATE_HOST;
         else
              ret = USB_ID_STATE_DEVICE;
         break;
	case UMONITOR_ADC_IDPIN:
		 if (atc260x_ex_auxadc_read_by_name("AUX0", &idpin_adc_val) < 0)
			break;
		 if ((idpin_adc_val >= pdev->config->idpin_adc_threshold_min) &&
		 	(idpin_adc_val <= pdev->config->idpin_adc_threshold_max)){
	          ret = USB_ID_STATE_HOST;
			//MONITOR_ERR("USB_ID_STATE_HOST_1");
		 }
		 else if ((pdev->config->idpin_adc_threshold_min_2 != 0) //如果这些参数有配置的话
				|| (pdev->config->idpin_adc_threshold_max_2 !=0))
		 {
			if((idpin_adc_val >= pdev->config->idpin_adc_threshold_min_2) &&
				(idpin_adc_val <= pdev->config->idpin_adc_threshold_max_2)){
	          ret = USB_ID_STATE_HOST;
			//MONITOR_ERR("USB_ID_STATE_HOST_2");
			} else {
	          ret = USB_ID_STATE_DEVICE;
			 //MONITOR_ERR("USB_ID_STATE_DEVICE");
			}
		 } else {
	          ret = USB_ID_STATE_DEVICE;
			 // MONITOR_ERR("USB_ID_STATE_DEVICE");
		 }
	     break;
	default:
		break;
	}
	return ret;
}

unsigned int usb2_get_linestates(usb_hal_monitor_t * pdev)
{
	unsigned int ls;

	ls = ((act_readl(pdev->usbecs) & USB2_ECS_LS_P0_MASK) >> USB2_ECS_LS_P0_SHIFT);
	return ls;
}

int usb2_set_dp_500k_15k(usb_hal_monitor_t * pdev, int enable_500k_up,
			       int enable_15k_down)
{
	unsigned int val;

	val = act_readl(pdev->usbecs) & (~((1 << USB2_ECS_DPPUEN_P0) |
			(1 << USB2_ECS_DMPUEN_P0) |
			(1 << USB2_ECS_DMPDDIS_P0) |
			(1 << USB2_ECS_DPPDDIS_P0)));

	if (enable_500k_up != 0) {
		val |= (1 << USB2_ECS_DPPUEN_P0)|(1 << USB2_ECS_DMPUEN_P0);
	}
	if (enable_15k_down == 0) {
		val |= (1 << USB2_ECS_DPPDDIS_P0)|(1 << USB2_ECS_DMPDDIS_P0);
	}

	MONITOR_PRINTK("dpdm is %08x\n", val);
	act_writel(val, pdev->usbecs);	/* 500k up enable, 15k down disable; */
	return 0;

}

int usb2_set_soft_id(usb_hal_monitor_t * pdev, int en_softid,
			   int id_state)
{
	unsigned int val;

	if (pdev->config->idpin_type == UMONITOR_USB_IDPIN) {
		/* ignore soft idpin setting. */
		en_softid = 0;
	}
	val = act_readl(pdev->usbecs);
	if (en_softid != 0) {
		val |= 0x1 << USB2_ECS_SOFTIDEN_P0;	/* soft id enable. */
	} else {
		val &= ~(0x1 << USB2_ECS_SOFTIDEN_P0);	/* soft id disable. */
	}

	if (id_state != 0) {
		val |= (0x1 << USB2_ECS_SOFTID_P0);
	} else {
		val &= ~(0x1 << USB2_ECS_SOFTID_P0);
	}
	act_writel(val, pdev->usbecs);
	return 0;
}

int usb2_set_soft_vbus(usb_hal_monitor_t * pdev, int en_softvbus, int vbus_state)
{
	unsigned int val;

	if (pdev->config->vbus_type == UMONITOR_USB_VBUS) {
		/* ignore soft vbus setting. */
		en_softvbus = 0;
	}

	val = act_readl(pdev->usbecs);
	if (en_softvbus != 0) {
		val |= 0x1 << USB2_ECS_SOFTVBUSEN_P0;	/* soft id enable. */
	} else {
		val &= ~(0x1 << USB2_ECS_SOFTVBUSEN_P0);	/* soft id disable. */
	}

	if (vbus_state != 0) {
		val |= (0x1 << USB2_ECS_SOFTVBUS_P0);
	}else {
		val &= ~(0x1 << USB2_ECS_SOFTVBUS_P0);
	}
	act_writel(val, pdev->usbecs);

	return 0;
}

void usb2_hal_set_cmu_usbpll(usb_hal_monitor_t *pdev, int enable)
{
	if (enable != 0) {
		act_writel(act_readl(pdev->usbpll) | (0x3f00), pdev->usbpll);
	} else {
		act_writel(act_readl(pdev->usbpll) & ~(0x3f00),pdev->usbpll);
	}
	mdelay(2);
}

/******************************************************************************/
/*!
 * \par  Description:
 *       对usb模块进行初始化（包括开启时钟，复位整个usb硬件模块，并确保复位在返回前完成
 * \param[in] void
 * \param[in] void
 * \return    int
 * \retval    0：usb模块初始化完成    负值：usb模块初始化失败
 * \ingroup   hal_usb
 * \par
 * \note
 *
 *******************************************************************************/
int usb2_hardware_init(usb_hal_monitor_t * pdev)
{
	unsigned int umask;

	act_writel((act_readl(pdev->usbecs) | 0x0000000f), pdev->usbecs);
	//set VBUS detection threshold,VBUS_DET_THRESHOLD_LEVEL4
	umask = (act_readl(pdev->usbecs)) & (~USB2_ECS_VBUS_THRESHOLD_MASK);
	act_writel(umask | USB2_ECS_VBUS_THRESHOLD, pdev->usbecs);

	MONITOR_PRINTK("usbecs value is %08x------>/n", act_readl(pdev->usbecs));

	return 0;
}

/******************************************************************************/
/*!
* \par  Description:
*     enable or disable the usb controller
* \param[in]    aotg  contains the controller info
* \param[in]    enable  enable(1) or disable(0) the controller
* \return       the result
* \retval           0 sucess
* \retval           1 failed
* \ingroup      USB_UOC
*******************************************************************************/
int usb2_hal_aotg_enable(usb_hal_monitor_t * pdev, int enable)
{
	if (enable != 0) {
		MONITOR_PRINTK("aotg mon enable\n");
		if (usb2_hardware_init(pdev) != 0) {
			return -1;
		}
	} else {
		MONITOR_PRINTK("aotg mon disable\n");
	}
	return 0;
}

int usb2_hal_set_mode(usb_hal_monitor_t * pdev, int mode)
{
	if (mode == USB_IN_DEVICE_MOD) {
		act_writel(act_readl(pdev->usbecs) | (0xf ), pdev->usbecs);
	}
	if (mode == USB_IN_HOST_MOD) {
		act_writel(act_readl(pdev->usbecs) & (~0xf), pdev->usbecs);
	}
	return 0;
}

void usb2_hal_dp_up(usb_hal_monitor_t * pdev)
{
	return;
}

void usb2_hal_dp_down(usb_hal_monitor_t * pdev)
{
	return;
}

int usb2_hal_is_sof(usb_hal_monitor_t * pdev)
{
  return 0;
}

int usb2_hal_enable_irq(struct usb_hal_monitor *pdev, int enable)
{
	return 0;
}

void usb2_hal_debug(usb_hal_monitor_t * pdev)
{
	printk("%s:%d\n", __FILE__, __LINE__);
	printk("pdev->usbecs addr: 0x%x\n\n", pdev->usbecs);
	return;
}

int usb2_suspend_or_resume(usb_hal_monitor_t *pdev, int is_suspend)
{
	/* save/reload usbecs when suspend/resume */
	if (is_suspend) {
		pdev->usbecs_val = act_readl(pdev->usbecs);
	}else{
		usb2_hardware_init(pdev);
		//act_writel(pdev->usbecs_val, pdev->usbecs);
	}
	return 0;
}

void usb2_controllor_init(usb_hal_monitor_t * pdev)
{
	u32 reg;

	/*USB2 PLL enable*/
	act_writel(act_readl(pdev->usbpll) | (0x3f00), pdev->usbpll);
	udelay(1000);

	/*USB2 Cmu Reset */
	reg = act_readl(pdev->devrst);
	reg &= ~(USB2_MOD_RST);
	act_writel(reg, pdev->devrst);
	udelay(100);

	reg = act_readl(pdev->devrst);
	reg |= (USB2_MOD_RST);
	act_writel(reg, pdev->devrst);
	udelay(100);

}

void usb2_controllor_exit(usb_hal_monitor_t * pdev)
{
	/*USB3 PLL disable*/
	act_writel(act_readl(pdev->usbpll) & ~(0x3f00),pdev->usbpll);
	udelay(100);
}

void usb2_controllor_mode_cfg(usb_hal_monitor_t *pdev)
{
}

void aotg_set_idpin_state(int value)
{
	aotg_soft_idpin = 1;
}
EXPORT_SYMBOL_GPL(aotg_set_idpin_state);


