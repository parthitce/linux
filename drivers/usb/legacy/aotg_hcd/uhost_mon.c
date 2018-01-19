/*
 * Copyright (C) 2012-2014 Actions-semi Corp.
 *
 * Author: houjingkun<houjingkun@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <asm/prom.h>

#include "aotg_hcd.h"
#include "aotg_regs.h"
#include "aotg_plat_data.h"
#include "aotg_dma.h"
#include "aotg_debug.h"


/* usbecs register */
#define	USB2_ECS_VBUS_P0		10
#define	USB2_ECS_ID_P0			12
#define USB2_ECS_LS_P0_SHIFT	8
#define USB2_ECS_LS_P0_MASK		(0x3 << 8)
#define USB2_ECS_DPPUEN_P0     3
#define USB2_ECS_DMPUEN_P0     2
#define USB2_ECS_DMPDDIS_P0    1
#define USB2_ECS_DPPDDIS_P0    0
#define USB2_ECS_SOFTIDEN_P0   26
#define USB2_ECS_SOFTID_P0     27
#define USB2_ECS_SOFTVBUSEN_P0 24
#define USB2_ECS_SOFTVBUS_P0   25


struct aotg_uhost_mon_t {
	int id;
	void __iomem *usbecs;
//	u32 usbecs;

	struct timer_list hotplug_timer;

	struct workqueue_struct *aotg_dev_onoff;
	struct delayed_work aotg_dev_init;
	struct delayed_work aotg_dev_exit;

	unsigned int aotg_uhost_det;

	/* DPDM state */
	unsigned int old_state;
	unsigned int state;
};

unsigned int port0_plug_en;
unsigned int port1_plug_en;
static struct aotg_uhost_mon_t *aotg_uhost_mon0;
static struct aotg_uhost_mon_t *aotg_uhost_mon1;

static int usb2_set_dp_500k_15k(struct aotg_uhost_mon_t *umon,
				int enable_500k_up, int enable_15k_down)
{
	unsigned int val;

	val = usb_readl(umon->usbecs) &
		(~((1 << USB2_ECS_DPPUEN_P0) |
		(1 << USB2_ECS_DMPUEN_P0) |
		(1 << USB2_ECS_DMPDDIS_P0) |
		(1 << USB2_ECS_DPPDDIS_P0)));

	if (enable_500k_up != 0)
		val |= (1 << USB2_ECS_DPPUEN_P0) | (1 << USB2_ECS_DMPUEN_P0);
	if (enable_15k_down == 0)
		val |= (1 << USB2_ECS_DPPDDIS_P0) | (1 << USB2_ECS_DMPDDIS_P0);

	usb_writel(val, umon->usbecs);	/* 500k up enable, 15k down disable; */

	return 0;
}

/* get DPDM state */
static inline unsigned int usb_get_linestates(struct aotg_uhost_mon_t *umon)
{
	unsigned int state;

	state = ((usb_readl(umon->usbecs) & USB2_ECS_LS_P0_MASK) >>
				USB2_ECS_LS_P0_SHIFT);
	return state;
}

static void aotg_uhost_mon_timer(unsigned long data)
{
	static int cnt;
	struct aotg_uhost_mon_t *umon = (struct aotg_uhost_mon_t *)data;

	if ((!umon) || (!umon->aotg_uhost_det))
		return;
	umon->state = usb_get_linestates(umon);

	cnt++;
	if ((cnt % 16) == 0)
		pr_info("umon->state:%x\n", umon->state);

	if (umon->state != 0)
		if ((umon->state == umon->old_state) && (umon->state != 0x3)) {
			umon->aotg_uhost_det = 0;
			umon->old_state = 0;
			queue_delayed_work(umon->aotg_dev_onoff,
				&umon->aotg_dev_init, msecs_to_jiffies(1));
			return;
		}

	umon->old_state = umon->state;
	mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(500));
}

static void aotg_dev_register(struct work_struct *w)
{
	struct aotg_uhost_mon_t *umon = container_of(w, struct aotg_uhost_mon_t,
		aotg_dev_init.work);

	if (umon->id == 0)
		aotg0_device_init(0);
	else
		aotg1_device_init(0);
}

static void aotg_dev_unregister(struct work_struct *w)
{
	struct aotg_uhost_mon_t *umon = container_of(w, struct aotg_uhost_mon_t,
		aotg_dev_exit.work);

	if (umon->id == 0)
		aotg0_device_exit(0);
	else
		aotg1_device_exit(0);
	umon->aotg_uhost_det = 1;
	mod_timer(&umon->hotplug_timer, jiffies + msecs_to_jiffies(1000));
}

void aotg_dev_plugout_msg(int id)
{
	struct aotg_uhost_mon_t *umon = NULL;

	if ((id == 0) && aotg_uhost_mon0) {
		ACT_HCD_DBG
		umon = aotg_uhost_mon0;
	} else if ((id == 1) && aotg_uhost_mon1) {
		ACT_HCD_DBG
		umon = aotg_uhost_mon1;
	} else
		return;

	umon->old_state = 0;
	queue_delayed_work(umon->aotg_dev_onoff, &umon->aotg_dev_exit,
		msecs_to_jiffies(500));
}

static struct aotg_uhost_mon_t *aotg_uhost_mon_alloc(void)
{
	struct aotg_uhost_mon_t *umon = NULL;

	umon = kzalloc(sizeof(*umon), GFP_KERNEL);
	if (!umon)
		return NULL;

	init_timer(&umon->hotplug_timer);
	umon->hotplug_timer.function = aotg_uhost_mon_timer;
	umon->hotplug_timer.data = (unsigned long)umon;

	INIT_DELAYED_WORK(&umon->aotg_dev_init, aotg_dev_register);
	INIT_DELAYED_WORK(&umon->aotg_dev_exit, aotg_dev_unregister);

	umon->aotg_uhost_det = 1;

	return umon;
}

static void __iomem *usb_get_ces0(void)
{
#if 1
	return (void __iomem *)USB2H0_ECS;
#else
	int dvfslevel;
	void __iomem *usbecs;

	dvfslevel = ASOC_GET_IC(asoc_get_dvfslevel());
	switch (dvfslevel) {
	case 0x7029:
		usbecs = (void __iomem *)USB2_0ECS_ATM7029;
		break;
	case 0x7021:
	case 0x7023:
		usbecs = (void __iomem *)USB2_0ECS_ATM7023;
		break;
	default:
		pr_info("wrong dvfslevel: %d\n", dvfslevel);
		BUG();
	}

	return usbecs;
#endif
}

static void __iomem *usb_get_ces1(void)
{
#if 1
	return (void __iomem *)USB2H1_ECS;
#else
	int dvfslevel;
	void __iomem *usbecs;

	dvfslevel = ASOC_GET_IC(asoc_get_dvfslevel());
	switch (dvfslevel) {
	case 0x7029:
		usbecs = (void __iomem *)USB2_1ECS_ATM7029;
		break;
	case 0x7021:
	case 0x7023:
		usbecs = (void __iomem *)USB2_1ECS_ATM7023;
		break;
	default:
		pr_info("wrong dvfslevel: %d\n", dvfslevel);
		BUG();
	}

	return usbecs;
#endif
}

void aotg_uhost_mon_init(int aotg0_config, int aotg1_config)
{
	port0_plug_en = aotg0_config;
	port1_plug_en = aotg1_config;

	pr_info("%s, port0_plug_en: %d, port1_plug_en: %d\n", __func__,
		port0_plug_en, port1_plug_en);

	if (port0_plug_en) {
		aotg_uhost_mon0 = aotg_uhost_mon_alloc();
		aotg_uhost_mon0->id = 0;
		aotg_uhost_mon0->aotg_dev_onoff =
			create_singlethread_workqueue("aotg_dev0_onoff");
		aotg_uhost_mon0->usbecs = usb_get_ces0();

		if (port0_plug_en != 2)
			aotg0_device_init(1);
		usb2_set_dp_500k_15k(aotg_uhost_mon0, 0, 1);
		mod_timer(&aotg_uhost_mon0->hotplug_timer,
			jiffies + msecs_to_jiffies(8000));
	}
	if (port1_plug_en) {
		aotg_uhost_mon1 = aotg_uhost_mon_alloc();
		aotg_uhost_mon1->id = 1;
		aotg_uhost_mon1->aotg_dev_onoff =
			create_singlethread_workqueue("aotg_dev1_onoff");
		aotg_uhost_mon1->usbecs = usb_get_ces1();

		if (port1_plug_en != 2)
			aotg1_device_init(1);
		usb2_set_dp_500k_15k(aotg_uhost_mon1, 0, 1);
		mod_timer(&aotg_uhost_mon1->hotplug_timer,
			jiffies + msecs_to_jiffies(8000));
	}

	return;
}

static inline int aotg_uhost_mon_free(struct aotg_uhost_mon_t *umon)
{
	if (!umon)
		return -1;

	if (umon->aotg_dev_onoff) {
		cancel_delayed_work_sync(&umon->aotg_dev_init);
		cancel_delayed_work_sync(&umon->aotg_dev_exit);
		flush_workqueue(umon->aotg_dev_onoff);
		destroy_workqueue(umon->aotg_dev_onoff);
	}
	del_timer_sync(&umon->hotplug_timer);
	kfree(umon);

	return 0;
}

void aotg_uhost_mon_exit(void)
{
	if (port0_plug_en && (port0_plug_en != 2))
		aotg0_device_exit(1);
	if (port1_plug_en && (port1_plug_en != 2))
		aotg1_device_exit(1);

	aotg_uhost_mon_free(aotg_uhost_mon0);
	aotg_uhost_mon_free(aotg_uhost_mon1);
	aotg_uhost_mon0 = NULL;
	aotg_uhost_mon1 = NULL;

	return;
}
