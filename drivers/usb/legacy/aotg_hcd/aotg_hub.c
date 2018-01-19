/**
 * In order to support usb hub.
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
#include <linux/kallsyms.h>

#include "aotg_hcd.h"
#include "aotg_regs.h"
#include "aotg_plat_data.h"
#include "aotg_dma.h"
#include "aotg_debug.h"
#include "aotg_hub.h"


static struct delayed_work aotg_switch_hub_wk;  	/* process switch with hub controller. */
static struct delayed_work aotg_switch_hcd_wk;
static struct workqueue_struct *aotg_switch_wq = NULL;

static aotg_hub_symbol_func_t aotg_hub_init = NULL;
static aotg_hub_symbol_func_t aotg_hub_exit = NULL;
static int aotg0_working = 0;

static void aotg_switch_with_hub_drv(int is_hub)
{
	static int is_first_call = 1;

	if (is_first_call) {
		is_first_call = 0;
		aotg_hub_init = (aotg_hub_symbol_func_t)kallsyms_lookup_name("aotg_hub_register");
		aotg_hub_exit = (aotg_hub_symbol_func_t)kallsyms_lookup_name("aotg_hub_unregister");
	}
	if (is_hub) {
		if (aotg_hub_init) {
			aotg0_device_exit(0);
			msleep(300);
			aotg_hub_init(0);
		} else {
			ACT_HCD_ERR
		}
		return;
	} else {
		if (aotg_hub_exit) {
			aotg_hub_exit(0);
			msleep(300);
			aotg0_device_init(0);
		} else {
			ACT_HCD_ERR
		}
		return;
	}
	return;
}

static void aotg_switch_to_hub(struct work_struct *w)
{
	if (aotg0_working)
		aotg_switch_with_hub_drv(1);
}

static void aotg_switch_to_hcd(struct work_struct *w)
{
	if (aotg0_working)
		aotg_switch_with_hub_drv(0);
}

void aotg_force_init_hub(int port_num)
{
	aotg0_working = 1;
	return;
}

void aotg_force_exit_hub(int port_num)
{
	aotg0_working = 0;

	if (aotg_hub_exit) {
		aotg_hub_exit(port_num);
	}
	return;
}

void aotg_hub_notify_enter(int state)
{
	if (aotg0_working)
		queue_delayed_work(aotg_switch_wq, &aotg_switch_hub_wk, msecs_to_jiffies(1));
}

void aotg_hub_notify_exit(int state)
{
	if (aotg0_working)
		queue_delayed_work(aotg_switch_wq, &aotg_switch_hcd_wk, msecs_to_jiffies(10));
}
EXPORT_SYMBOL(aotg_hub_notify_exit);

int ahcd_hub_init(void)
{
	aotg_switch_wq = create_singlethread_workqueue("aotg_switch_wq");
	INIT_DELAYED_WORK(&aotg_switch_hub_wk, aotg_switch_to_hub);
	INIT_DELAYED_WORK(&aotg_switch_hcd_wk, aotg_switch_to_hcd);
	return 0;
}

int ahcd_hub_exit(void)
{
	if (aotg_switch_wq) {
		cancel_delayed_work_sync(&aotg_switch_hub_wk);
		cancel_delayed_work_sync(&aotg_switch_hcd_wk);
		flush_workqueue(aotg_switch_wq);
		destroy_workqueue(aotg_switch_wq);
	}
	return 0;
}

