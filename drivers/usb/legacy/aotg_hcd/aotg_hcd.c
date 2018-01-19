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

/**
 * NOTICE: Do not support usb hub!
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
#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/mm_types.h>
#include <linux/highmem.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>

//#include <asm/irq.h>
#include <asm/system.h>

#include <linux/regulator/consumer.h>

#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
//#include <asm/prom.h>
#include <linux/kallsyms.h>

#include "aotg_hcd.h"
#include "aotg_regs.h"
#include "aotg_plat_data.h"
#include "aotg_dma.h"
#include "aotg_debug.h"
#include "aotg_hub.h"
#include "uhost_mon.h"


#define	DRIVER_DESC	"AOTG USB Host Controller Driver"

static const char driver_name[]	= "aotg_hcd";

/* do a plug-out and start detection again instead of resetting device */
#define NEW_SUSPEND_RESUME

#define	MAX_PACKET(x)	((x) & 0x7FF)


/**
 * aotg1 depends on aotg0 phy clock, we should enable phy0
 * clock always, but only disable phy0 when aotg0 and aotg1
 * don't work neither!
 */
int aotg_phy0_clk_count;
EXPORT_SYMBOL(aotg_phy0_clk_count);


/**
 * 0: enable both, 1: only enable ush2h0, 2: only enable ush2h1,
 * 3: reserved(for now, same as 0).
 */
int hcd_ports_en_ctrl;
struct mutex aotg_onoff_mutex;

/* for aotg uhost monitor */
static int aotg0_host_plug_detect;
static int aotg1_host_plug_detect;

static int hcd_suspend_en;
//static int phy1_resumed = 0;


/* for debug */
//#define MULTI_OUT_FIFO


/* printk phy set */
static int phy;
module_param(phy, int, S_IRUGO  | S_IWUSR);


#define	USB2_ECS_ID_PIN_BIT	(0x1 << 12)

typedef void (* monitor_notify_func_t)(int);
monitor_notify_func_t monitor_notify_state = NULL;

static void aotg_send_line_out_msg(struct aotg_hcd *acthcd)
{
	struct platform_device *pdev;
	static int is_first_call = 1;
	struct usb_hcd *hcd = aotg_to_hcd(acthcd);
	pdev = to_platform_device(hcd->self.controller);

	if ((pdev == NULL) || (pdev->id != 0))
		return;

	if (is_first_call) {
		is_first_call = 0;
		monitor_notify_state = (monitor_notify_func_t)
							kallsyms_lookup_name("aotg_set_idpin_state");
	}
	if (monitor_notify_state != NULL) {
		monitor_notify_state(1);
		return;
	}
	ACT_HCD_DBG
	return;
}

static ulong get_fifo_addr(struct aotg_hcd *acthcd, int size)
{
	int	i, j;
	ulong addr = 0;
	int	mul	= size / ALLOC_FIFO_UNIT;
	int	max_unit = AOTG_FIFO_NUMS;
	int	find_next =	0;

	if (mul	== 0)
		mul	= 1;

	for	(i = 2;	i <	max_unit;) {
		if (acthcd->fifo_map[i]	!= 0) {
			i++;
			continue;	 /* find the first unused addr */
		}

		for	(j = i;	j <	max_unit; j++) {
			if ((j - i + 1)	== mul)
				break;

			if (acthcd->fifo_map[j]) {
				i =	j;
				find_next =	1;
				break;
			}
		}

		if (j == 64)
			break;
		else if (find_next) {
			find_next =	0;
			continue;
		} else {
			int	k;
			for	(k = i;	k <= j;	k++)
				acthcd->fifo_map[k]	= (1 <<	31)	| (i * 64);
			addr = i * ALLOC_FIFO_UNIT;
			break;
		}
	}

	return addr;
}

static void release_fifo_addr(struct aotg_hcd *acthcd, ulong addr)
{
	int	i;

	for	(i = addr / ALLOC_FIFO_UNIT; i < AOTG_FIFO_NUMS; i++) {
		if ((acthcd->fifo_map[i] & 0x7FFFFFFF) == addr)
			acthcd->fifo_map[i]	= 0;
		else
			break;
	}
	return;
}

static int inline aotg_hcd_get_dma_buf(struct aotg_hcd *acthcd,
	struct urb *urb, struct aotg_queue *q)
{
	int i;
	int empty_idx = -1;
	struct usb_hcd *hcd = aotg_to_hcd(acthcd);

	q->dma_addr = (u32)urb->transfer_dma + urb->actual_length;
	q->dma_copy_buf = NULL;

	if (usb_pipetype(urb->pipe) == PIPE_CONTROL)
		return -EINVAL;
	if (((u32)urb->transfer_buffer & 0x3) == 0) {
		if (urb->actual_length)
			ACT_HCD_ERR
		return 0;
	}
	/* fixing dma address unaligned to 4 Bytes. */
	for (i = 0; i < AOTG_DMA_BUF_CNT; i++) {
		if ((acthcd->dma_poll[i].size != 0) && (acthcd->dma_poll[i].in_using == 0)) {
			if (acthcd->dma_poll[i].size >= urb->transfer_buffer_length) {
				q->dma_copy_buf = acthcd->dma_poll[i].buf;
				q->dma_addr = acthcd->dma_poll[i].dma;
				acthcd->dma_poll[i].in_using = 1;
				goto get_copy_buf;
			}
		} else {
			if (acthcd->dma_poll[i].size == 0) {
				if (empty_idx < 0)
					empty_idx = i;
			}
		}
	}

	if (empty_idx >= 0) {
		i = empty_idx;
		acthcd->dma_poll[i].size = urb->transfer_buffer_length + (0x1000 - 0x1);
		acthcd->dma_poll[i].size &= ~(0xfff);
		acthcd->dma_poll[i].buf = dma_alloc_coherent(hcd->self.controller,
			acthcd->dma_poll[i].size, &acthcd->dma_poll[i].dma, GFP_ATOMIC);
		q->dma_copy_buf = acthcd->dma_poll[i].buf;
		q->dma_addr = acthcd->dma_poll[i].dma;
		acthcd->dma_poll[i].in_using = 1;
	}

get_copy_buf:
	if (q->dma_copy_buf == NULL) {
		ACT_HCD_ERR
		return -ENOMEM;
	}
	if (usb_pipeout(urb->pipe)) {
		memcpy(q->dma_copy_buf, urb->transfer_buffer, urb->transfer_buffer_length);
		dma_sync_single_for_device(hcd->self.controller, q->dma_addr,
			urb->transfer_buffer_length, DMA_TO_DEVICE);
	} else
		dma_sync_single_for_device(hcd->self.controller, q->dma_addr,
			urb->transfer_buffer_length, DMA_FROM_DEVICE);

	return 0;
}
static int inline aotg_hcd_free_dma_buf(struct aotg_hcd *acthcd,
	struct aotg_queue *q)
{
	int i;
	struct usb_hcd *hcd = aotg_to_hcd(acthcd);

	if (q == NULL) {
		for (i = 0; i < AOTG_DMA_BUF_CNT; i++) {
			if (acthcd->dma_poll[i].size != 0) {
				if (acthcd->dma_poll[i].buf)
					dma_free_coherent(hcd->self.controller,
						acthcd->dma_poll[i].size, acthcd->dma_poll[i].buf,
						acthcd->dma_poll[i].dma);
				else
					ACT_HCD_ERR
			}
			acthcd->dma_poll[i].size = 0;
			acthcd->dma_poll[i].buf = NULL;
			acthcd->dma_poll[i].dma = 0;
			acthcd->dma_poll[i].in_using = 0;
		}
		return 0;
	}

	if (q->dma_copy_buf == NULL)
		return -EINVAL;

	for (i = 0; i < AOTG_DMA_BUF_CNT; i++) {
		if ((acthcd->dma_poll[i].size != 0) &&
			(acthcd->dma_poll[i].buf == q->dma_copy_buf)) {
			if (acthcd->dma_poll[i].in_using)
				acthcd->dma_poll[i].in_using = 0;
			else
				ACT_HCD_ERR
			q->dma_copy_buf = NULL;
			return 0;
		}
	}

	ACT_HCD_ERR
	return -EINVAL;
}
static struct aotg_queue * aotg_hcd_get_queue(struct aotg_hcd *acthcd,
	unsigned mem_flags)
{
	int	i;
	int	empty_idx =	-1;
	struct aotg_queue *q = NULL;

	for	(i = 0;	i <	AOTG_QUEUE_POOL_CNT; i++) {
		if (acthcd->queue_pool[i] != NULL) {
			if (acthcd->queue_pool[i]->in_using	== 0) {
				q =	acthcd->queue_pool[i];
				break;
			}
		} else {
			if (empty_idx <	0)
				empty_idx =	i;
		}
	}
	if (i == AOTG_QUEUE_POOL_CNT) {
		q =	kzalloc(sizeof(*q),	GFP_ATOMIC);
		if (unlikely(!q)) {
			dev_err(acthcd->dev, "aotg_hcd_get_queue failed\n");
			return NULL;
		}
		if ((empty_idx >= 0) &&	(empty_idx < AOTG_QUEUE_POOL_CNT))
			acthcd->queue_pool[empty_idx] =	q;
	}

	memset(q, 0, sizeof(*q));
	q->length =	-1;
	q->err_count = 0;
	q->need_zero = 0;
	INIT_LIST_HEAD(&q->dma_q_list);
	INIT_LIST_HEAD(&q->ep_q_list);
	INIT_LIST_HEAD(&q->enqueue_list);
	INIT_LIST_HEAD(&q->dequeue_list);
	INIT_LIST_HEAD(&q->finished_list);
	q->in_using	= 1;

	return q;
}

static void aotg_hcd_release_queue(struct aotg_hcd *acthcd,
	struct aotg_queue *q)
{
	int	i;

	/* release all */
	if (q == NULL) {
		for	(i = 0; i < AOTG_QUEUE_POOL_CNT; i++) {
			if (acthcd->queue_pool[i] != NULL) {
				kfree(acthcd->queue_pool[i]);
				acthcd->queue_pool[i] =	NULL;
			}
		}
		return;
	}

	for	(i = 0; i < AOTG_QUEUE_POOL_CNT; i++) {
		if (acthcd->queue_pool[i] == q)	{
			if (q->dma_copy_buf)
				aotg_hcd_free_dma_buf(acthcd, q);
			acthcd->queue_pool[i]->in_using	= 0;
			return;
		}
	}
	if (q->dma_copy_buf) {
		aotg_hcd_free_dma_buf(acthcd, q);
	}
	kfree(q);
	return;
}

static __inline__ void ep_setup(struct aotg_hcep *ep, u8 type, u8 buftype)
{
	ep->buftype	= buftype;

	usb_writeb(type	| buftype, ep->reg_hcepcon);
}

static __inline__ void aotg_hcep_reset(struct aotg_hcd *acthcd, u8 ep_mask,
	u8 type_mask)
{
	u8 val;

	usb_writeb(ep_mask, acthcd->base + ENDPRST);	/*select ep	*/
	val	= ep_mask |	type_mask;
	usb_writeb(val,	acthcd->base + ENDPRST);	/*reset	ep */
}

static __inline__ void pio_irq_disable(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_clearbitsb(1 << ep_num, acthcd->base + HCOUT07IEN);
	else
		usb_clearbitsb(1 << ep_num, acthcd->base + HCIN07IEN);
}

static __inline__ void pio_irq_enable(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_setbitsb(1 << ep_num, acthcd->base + HCOUT07IEN);
	else
		usb_setbitsb(1 << ep_num, acthcd->base + HCIN07IEN);
}

static __inline__ void pio_irq_clear(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_writeb(1 <<	ep_num,	acthcd->base + HCOUT07IRQ);
	else
		usb_writeb(1 <<	ep_num,	acthcd->base + HCIN07IRQ);
}


static __inline__ void hcerr_irq_enable(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_setbitsb(1 << ep_num, acthcd->base + HCOUT07ERRIEN);
	else
		usb_setbitsb(1 << ep_num, acthcd->base + HCIN07ERRIEN);
}

static __inline__ void hcerr_irq_disable(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_clearbitsb(1 << ep_num, acthcd->base + HCOUT07ERRIEN);
	else
		usb_clearbitsb(1 << ep_num, acthcd->base + HCIN07ERRIEN);
}

static __inline__ void hcerr_irq_clear(struct aotg_hcd *acthcd, u8 mask)
{
	u8 is_out =	mask & USB_HCD_OUT_MASK;
	u8 ep_num =	mask & 0x0f;

	if (is_out)
		usb_writeb(1 << ep_num, acthcd->base + HCOUT07ERRIRQ);
	else
		usb_writeb(1 << ep_num, acthcd->base + HCIN07ERRIRQ);
}

static __inline__ void ep_enable(struct	aotg_hcep *ep)
{
	usb_setbitsb(0x80, ep->reg_hcepcon);
}

static __inline__ void ep_disable(struct aotg_hcep *ep)
{
	usb_clearbitsb(0x80, ep->reg_hcepcon);
}

static __inline__ void __hc_in_stop(struct aotg_hcd	*acthcd, struct	aotg_hcep *ep)
{
	u8 set = 0x1 <<	ep->index;

	if (usb_readb(acthcd->base + OUTXSHORTPCKIRQ) &	set) {
		usb_writeb(set, acthcd->base + OUTXSHORTPCKIRQ);
	}
	usb_clearbitsb(set, acthcd->base + HCINXSTART);
	usb_writew(0, ep->reg_hcincount_wt);
	//usb_clearbitsb(set, acthcd->base + HCINCTRL);
}

static __inline__ void __hc_in_start(struct aotg_hcd *acthcd,
				struct aotg_hcep *ep, const u16 w_packets)
{
	u8 set = 0x1 << ep->index;

	usb_writew(w_packets, ep->reg_hcincount_wt);
	usb_setbitsb(set, acthcd->base + OUTXNAKCTRL);
	usb_setbitsb(set, acthcd->base + HCINXSTART);
}

static __inline__ void __hc_in_pause(struct aotg_hcd *acthcd,
				struct aotg_hcep *ep)
{
	u8 set = 0x1 <<	ep->index;

	if (usb_readb(acthcd->base + OUTXSHORTPCKIRQ) &	set) {
		usb_writeb(set,	acthcd->base + OUTXSHORTPCKIRQ);
	}
	usb_clearbitsb(set,	acthcd->base + HCINXSTART);
}

static __inline__ void __hc_in_resume(struct aotg_hcd *acthcd,
				struct aotg_hcep *ep)
{
	u8 set = 0x1 << ep->index;

	usb_setbitsb(set, acthcd->base + OUTXNAKCTRL);
	usb_setbitsb(set, acthcd->base + HCINXSTART);
}

static __inline__ void aotg_sofirq_on(struct aotg_hcd *acthcd)
{
	usb_setbitsb((0x1 << 1), acthcd->base + USBIEN);
}

static __inline__ void aotg_sofirq_off(struct aotg_hcd *acthcd)
{
	usb_clearbitsb(0x1 << 1, acthcd->base + USBIEN);
}

static __inline__ int get_subbuffer_count(u8 buftype)
{
	int	count =	0;

	switch (buftype) {
	case EPCON_BUF_SINGLE:
		count =	1;
		break;
	case EPCON_BUF_DOUBLE:
		count =	2;
		break;
	case EPCON_BUF_TRIPLE:
		count =	3;
		break;
	case EPCON_BUF_QUAD:
		count =	4;
		break;
	default:
		break;
	}

	return count;
}

static __inline__ void aotg_enable_irq(struct aotg_hcd *acthcd)
{
	//usb_setbitsb(USBEIRQ_USBIEN | USBEIRQ_CON_DISCONIEN, acthcd->base + USBEIRQ);
	usb_writeb(USBEIRQ_USBIEN, acthcd->base	+ USBEIRQ);
	usb_setbitsb(USBEIRQ_USBIEN, acthcd->base +	USBEIEN);
	usb_setbitsb(0x1<<2, acthcd->base +	OTGIEN);
	//printk("USBEIRQ(0x%p): 0x%02X\n", acthcd->base + USBEIRQ, usb_readb(acthcd->base + USBEIRQ));
	usb_setbitsb(OTGCTRL_BUSREQ, acthcd->base +	OTGCTRL);
}

static __inline__ void aotg_disable_irq(struct aotg_hcd *acthcd)
{
	//usb_setbitsb(USBEIRQ_USBIEN | USBEIRQ_CON_DISCONIEN, acthcd->base + USBEIRQ);
	usb_writeb(USBEIRQ_USBIEN, acthcd->base	+ USBEIRQ);
	usb_clearbitsb(USBEIRQ_USBIEN, acthcd->base	+ USBEIEN);
	usb_clearbitsb(0x1<<2, acthcd->base	+ OTGIEN);
	usb_clearbitsb(OTGCTRL_BUSREQ, acthcd->base	+ OTGCTRL);
}

static __inline__ void __clear_dma(struct aotg_hcd *acthcd,
	struct aotg_queue *q, int dma_no)
{
	struct aotg_hcep *ep = q->ep;

	aotg_hcd_dma_enable_irq(dma_no, 0);

	//printk("clear	dma: %d, inpacket_count: %d	\n", dma_no, q->inpacket_count);
	//printk("DMASRC: 0x%x ", aotg_hcd_dma_get_memaddr(dma_no));
	//printk("cnt: 0x%x	", aotg_hcd_dma_get_cnt(dma_no));
	//printk("remain: 0x%x \n",	aotg_hcd_dma_get_remain(dma_no));
	//WARN_ON(q);

	/* assure bulk-out dma is working */
	if ((dma_no	& 0x1) && (acthcd->dma_working[1] != 0)) {
		printk("%s,%d,dma_no: %d, timer_bulkout_state: %d\n",
			__func__, __LINE__, dma_no, ep->timer_bulkout_state);
		ep->timer_bulkout_state	= AOTG_BULKOUT_NULL;
	}

	list_del_init(&q->dma_q_list);
	aotg_hcd_dma_stop(dma_no);
	aotg_hcd_dma_clear_pend(dma_no);
	q->is_start_dma	= 0;

	acthcd->dma_working[dma_no&0x1]	= 0;
	pr_info("clear %d\n", (dma_no & 0x1));

	aotg_hcd_dma_sync();
	aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST |	ENDPRST_TOGRST);

	if ((acthcd->dma_working[0]	== 0) && (acthcd->dma_working[1] ==	0))	{
//		acthcd->last_bulkin_dma_len	= 1;
		//aotg_hcd_dma_reset_2(dma_no);
		//printk("dma reset! \n");
		//printk("%s,%d,dma reset!!! \n",__func__,__LINE__);
	}
	aotg_hcd_dma_enable_irq(dma_no,	1);
	return;
}

// TODO: ep limited???
//support 3 bulk & 1 interrupt eps
static int aotg_hcep_config(struct aotg_hcd *acthcd, struct aotg_hcep *ep,
				u8 type, u8 buftype, int is_out)
{
	int	index =	0;
	ulong addr = 0;
	int	get_ep = 0;
	int	subbuffer_count;
	u8 fifo_ctrl;

	if (0 == (subbuffer_count =	get_subbuffer_count(buftype))) {
		dev_err(acthcd->dev, "error	buftype: %02X, %s, %d\n",
			buftype, __func__, __LINE__);
		return -EPIPE;
	}

	if (is_out)	{
		for	(index = 1;	index <	MAX_EP_NUM;	index++) {
			if (acthcd->outep[index] ==	NULL) {
				ep->index =	index;
				ep->mask = (u8)	(USB_HCD_OUT_MASK |	index);
				acthcd->outep[index] = ep;
				get_ep = 1;

				ep->bulkout_ep_busy_cnt = 0;
				ep->timer_bulkout_state = AOTG_BULKOUT_NULL;
#ifdef SHORT_PACK_487_490
				ep->shortpack_length = 0;
				ep->is_shortpack_487_490 = false;
#endif
				break;
			}
		}
	}
	else {
		for	(index = 1; index < MAX_EP_NUM; index++) {
			if (acthcd->inep[index]	== NULL) {
				ep->index =	index;
				ep->mask = (u8)	index;
				acthcd->inep[index]	= ep;
				get_ep = 1;
				break;
			}
		}
	}

	if (!get_ep) {
		dev_err(acthcd->dev, "%s: no more available	space for ep\n", __func__);
		return -ENOSPC;
	}

	addr = get_fifo_addr(acthcd, subbuffer_count * MAX_PACKET(ep->maxpacket));
	if (addr ==	0) {
		dev_err(acthcd->dev, "buffer configuration overload!! addr:	%08X, subbuffer_count: %d, ep->maxpacket: %u\n",
				(u32)addr, subbuffer_count,	MAX_PACKET(ep->maxpacket));
		if (is_out)
			acthcd->outep[ep->index] = NULL;
		else
			acthcd->inep[ep->index]	= NULL;
		return -ENOSPC;
	} else
		ep->fifo_addr =	addr;

	ep->reg_hcepcon	= get_hcepcon_reg(is_out,
								acthcd->base + HCOUT1CON,
								acthcd->base + HCIN1CON,
								ep->index);
	ep->reg_hcepcs = get_hcepcs_reg(is_out,
								acthcd->base + HCOUT1CS,
								acthcd->base + HCIN1CS,
								ep->index);
	ep->reg_hcepbc = get_hcepbc_reg(is_out,
								acthcd->base + HCOUT1BCL,
								acthcd->base + HCIN1BCL,
								ep->index);
	ep->reg_hcepctrl = get_hcepctrl_reg(is_out,
								acthcd->base + HCOUT1CTRL,
								acthcd->base + HCIN1CTRL,
								ep->index);
	ep->reg_hcmaxpck = get_hcepmaxpck_reg(is_out,
								acthcd->base + HCOUT1MAXPCKL,
								acthcd->base + HCIN1MAXPCKL,
								ep->index);
	ep->reg_hcepaddr = get_hcepaddr_reg(is_out,
								acthcd->base + IN1STARTADDRESS,
								acthcd->base + OUT1STARTADDRESS,
								ep->index);
	ep->reg_hcfifo = get_hcfifo_reg(acthcd->base + FIFO1DATA, ep->index);
	if (!is_out) {
		/* 5202	is just	for	write, read's HCINXCOUNT address is	not	the	same with write	address. */
		ep->reg_hcincount_wt = acthcd->base	+ HCIN1_COUNTL + (ep->index	- 1) * 4;
		ep->reg_hcincount_rd = acthcd->base	+ HCIN1_COUNTL + (ep->index	- 1) * 4;
		ep->reg_hcerr =	acthcd->base + HCIN0ERR	+ ep->index	* 0x4;
	} else
		ep->reg_hcerr =	acthcd->base + HCOUT0ERR + ep->index * 0x4;

	EP_CFGINFO(acthcd->dev, "== ep->index: %d, is_out: %d, fifo addr:	%08X\n", ep->index,	is_out,	(u32)addr);
	EP_CFGINFO(acthcd->dev, "== reg_hcepcon: %08lX, reg_hcepcs: %08lX, reg_hcepbc: %08lX,	reg_hcepctrl: %08lX, reg_hcmaxpck: %08lX, ep->reg_hcepaddr:	%08lX,reg_hcfifo: %08lX\n",
			(unsigned long)ep->reg_hcepcon,
			(unsigned long)ep->reg_hcepcs,
			(unsigned long)ep->reg_hcepbc,
			(unsigned long)ep->reg_hcepctrl,
			(unsigned long)ep->reg_hcmaxpck,
			(unsigned long)ep->reg_hcepaddr,
			(unsigned long)ep->reg_hcfifo);

	pio_irq_disable(acthcd,	ep->mask);
	pio_irq_clear(acthcd, ep->mask);

	if (!is_out) {
		ep_disable(ep);
		__hc_in_stop(acthcd, ep);
	}

	/*allocate buffer address of ep	fifo */
	usb_writel(addr, ep->reg_hcepaddr);
	usb_writew(ep->maxpacket, ep->reg_hcmaxpck);
	ep_setup(ep, type, buftype);	/*ep setup */

	/*reset	this ep	*/
	usb_settoggle(ep->udev,	ep->epnum, is_out, 0);
	aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST |	ENDPRST_TOGRST);
	usb_writeb(ep->epnum, ep->reg_hcepctrl);
	fifo_ctrl =	(0<<5) | ((!!is_out) <<	4) | ep->index;
	usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);

	//pio_irq_enable(acthcd, ep->mask);
	//enable the err_irq handler
	hcerr_irq_clear(acthcd, ep->mask);
	hcerr_irq_enable(acthcd, ep->mask);

	return 0;
}

static void finish_request(struct aotg_hcd *acthcd, struct aotg_queue *q,
				int status)
{
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;

	ep->q = NULL;

	if (unlikely((acthcd == NULL) || (q	== NULL) || (urb == NULL))) {
//		WARN_ON(1);
		ACT_HCD_DBG
		return;
	}

	spin_lock(&acthcd->lock);
	if (!list_empty(&q->finished_list)) {
		spin_unlock(&acthcd->lock);
		ACT_HCD_ERR
		return;
	}
	q->status =	status;
	list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
	spin_unlock(&acthcd->lock);

	tasklet_hi_schedule(&acthcd->urb_tasklet);
	return;
}

static void tasklet_finish_request(struct aotg_hcd *acthcd,
				struct aotg_queue *q, int status)
{
	struct usb_hcd *hcd	= aotg_to_hcd(acthcd);
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;

	if (unlikely((acthcd == NULL) || (q == NULL) || (urb == NULL) || (ep == NULL))) {
		WARN_ON(1);
		return;
	}
	/* maybe dequeued in urb_dequeue. */
	if (q->ep_q_list.next == LIST_POISON1) {
		ACT_HCD_DBG
		q->urb = NULL;
		q->ep =	NULL;
		return;
	}

	if (usb_pipeint(urb->pipe)) {
		acthcd->sof_kref--;
		hcd->self.bandwidth_int_reqs--;
		if (acthcd->sof_kref <=	0)
			aotg_sofirq_off(acthcd);
	}

	ep->urb_endque_cnt++;
	ep->ep_err_cnt = 0;
	q->err_count = 0;
	ep->fifo_busy_cnt = 0;
#ifdef EP_TIMEOUT_DISCONNECT
	if (status == 0) {
		ep->ep_timeout_cnt = 0;
		if ((usb_pipetype(urb->pipe) != PIPE_CONTROL) &&
			((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) == 0))
			ep->ep_reset_cnt = 0;
	}
#endif


	aotg_dbg_finish_q(q);
	list_del(&q->ep_q_list);

	if (usb_pipetype(urb->pipe)	== PIPE_CONTROL)
		acthcd->setup_processing = 0;
	else {
		if ((usb_pipein(urb->pipe)) && (status == 0)) {
			if (q->dma_copy_buf) {
				dma_sync_single_for_cpu(hcd->self.controller, q->dma_addr,
							urb->transfer_buffer_length, DMA_FROM_DEVICE);
				memcpy(urb->transfer_buffer, q->dma_copy_buf, urb->actual_length);
			}
#if 0	/* processed in unmap_urb_for_dma according ep->is_use_pio. */
			else if ((usb_pipetype(urb->pipe) == PIPE_BULK) && (ep->is_use_pio)) {
				dma_sync_single_for_device(hcd->self.controller, urb->transfer_dma,
							urb->actual_length, DMA_TO_DEVICE);
			}
#endif
		}
	}
	aotg_hcd_release_queue(acthcd, q);
	ep->q = NULL;
	if ((usb_pipetype(urb->pipe)	== PIPE_BULK) && (usb_pipeout(urb->pipe)))
		acthcd->bulkout_wait_dma[ep->index] = NULL;
	//else
		//tmp_seq_bulkout_cnt = 0;

	/* for iso transfers, it may enqueue n urbs, and ep->q_list will never be empty */
#if 0
	if (list_empty(&ep->q_list))
		return;

	ACT_HCD_ERR
#endif

	return;
}

static __inline__ void handle_status(struct	aotg_hcd *acthcd,
				struct aotg_hcep *ep, int is_out)
{
	/* status always DATA1, set 1 to ep0 toggle */
	usb_writeb(EP0CS_HCSETTOOGLE, acthcd->base + HCEP0CS);

	if (is_out)
		usb_writeb(0, acthcd->base + HCIN0BC); //recv 0 packet
	else
		usb_writeb(0, acthcd->base + HCOUT0BC);	//send 0 packet
}

static void write_hcep0_fifo(struct aotg_hcd *acthcd, struct aotg_hcep *ep,
				struct urb *urb)
{
	u32	*buf;
	int	length,	count;
	void __iomem *addr = acthcd->base +	HCEP0OUTDAT;

	if (!(usb_readb(acthcd->base + HCEP0CS)	& EP0CS_HCOUTBSY)) {
		buf	= (u32 *) (urb->transfer_buffer	+ urb->actual_length);
		prefetch(buf);

		/* how big will this packet be? */
		length = min((int)ep->maxpacket, (int)urb->transfer_buffer_length -
			(int)urb->actual_length);

		count =	length >> 2;	/*wirte in DWORD */
		if (length & 0x3) count++;

		while (likely(count--))	{
			usb_writel(*buf, addr);
			buf++;
			addr +=	4;
		}

		ep->length = length;
		usb_writeb(length, acthcd->base	+ HCOUT0BC);
		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	1);
	} else
		dev_err(acthcd->dev, "<CTRL>OUT	data is	not	ready\n");
}

static void read_hcep0_fifo(struct aotg_hcd *acthcd, struct aotg_hcep *ep,
				struct urb *urb)
{
	u8 *buf;
	unsigned overflag, is_short, shorterr, is_last;
	unsigned length, count;
	struct usb_device *udev;
	void __iomem *addr = acthcd->base +	EP0OUTDATA_W0;	//HCEP0INDAT0;
	unsigned bufferspace;

	overflag = 0;
	is_short = 0;
	shorterr = 0;
	is_last	= 0;
	udev = ep->udev;

	if (usb_readb(acthcd->base + HCEP0CS) &	EP0CS_HCINBSY) {
		dev_err(acthcd->dev, "<CTRL>IN data	is not ready\n");
		return;
	} else {
		usb_dotoggle(udev, ep->epnum, 0);
		buf	= urb->transfer_buffer + urb->actual_length;
		bufferspace	= urb->transfer_buffer_length -	urb->actual_length;
		//prefetch(buf);

		length = count = usb_readb(acthcd->base	+ HCIN0BC);
		if (length > bufferspace) {
			count =	bufferspace;
			urb->status	= -EOVERFLOW;
			overflag = 1;
		}

		urb->actual_length += count;
		while (count--)	{
			*buf++ = usb_readb(addr);
#if	0
			buf--;
			printk("ep0in:%x, cnt:%d\n", (unsigned int)*buf, count);
			buf++;
#endif
			addr++;
		}

		if (urb->actual_length >= urb->transfer_buffer_length) {
			ep->nextpid	= USB_PID_ACK;
			is_last	= 1;
			handle_status(acthcd, ep, 0);
		} else if (length <	ep->maxpacket) {
			is_short = 1;
			is_last	= 1;
			if (urb->transfer_flags	& URB_SHORT_NOT_OK)	{
				urb->status	= -EREMOTEIO;
				shorterr = 1;
			}
			ep->nextpid	= USB_PID_ACK;
			handle_status(acthcd, ep, 0);
		} else
			usb_writeb(0, acthcd->base + HCIN0BC);
	}
}

static int handle_setup_packet(struct aotg_hcd *acthcd,
				struct aotg_queue *q)
{
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;
	int	i =	0;

	u32	*buf;
	void __iomem *addr = acthcd->base +	HCEP0OUTDAT;

#ifdef DEBUG_SETUP_DATA
	u16	w_value, w_index, w_length;
	struct usb_ctrlrequest *ctrlreq;

	ctrlreq	= (struct usb_ctrlrequest *)urb->setup_packet;
	w_value	= le16_to_cpu(ctrlreq->wValue);
	w_index	= le16_to_cpu(ctrlreq->wIndex);
	w_length = le16_to_cpu(ctrlreq->wLength);
	dev_info(acthcd->dev, "<CTRL>SETUP stage %02x.%02x V%04x I%04x L%04x\n",
		  ctrlreq->bRequestType, ctrlreq->bRequest,	w_value, w_index,
		  w_length);
#endif

	if (q->is_xfer_start ||	ep->q_list.next	!= &q->ep_q_list) {
		ACT_HCD_DBG
		return 0;
	}

	if (unlikely(!HC_IS_RUNNING(aotg_to_hcd(acthcd)->state))) {
		ACT_HCD_DBG
		return -ESHUTDOWN;
	}

	acthcd->setup_processing = 1;
	q->is_xfer_start = 1;
	usb_settoggle(urb->dev,	usb_pipeendpoint(urb->pipe), 1,	1);
	ep->nextpid	= USB_PID_SETUP;
	buf	= (u32 *) urb->setup_packet;

	/*initialize the setup stage */
	usb_writeb(EP0CS_HCSET,	acthcd->base + EP0CS);
	while (usb_readb(acthcd->base +	HCEP0CS) & EP0CS_HCOUTBSY) {
		usb_writeb(EP0CS_HCSET,	acthcd->base + EP0CS);
		i++;
		if (i >	2000000) {
			printk("handle_setup timeout!\n");
			break;
		}
	}

	if (!(usb_readb(acthcd->base + HCEP0CS)	& EP0CS_HCOUTBSY)) {
		/*fill the setup data in fifo */
		usb_writel(*buf, addr);
		addr +=	4;
		buf++;
		usb_writel(*buf, addr);
		usb_writeb(8, acthcd->base + HCOUT0BC);
	} else
		dev_warn(acthcd->dev, "setup ep	busy!!!!!!!\n");

	return 0;
}

static void handle_hcep0_out(struct aotg_hcd *acthcd)
{
	struct aotg_hcep *ep;
	struct urb *urb;
	struct usb_device *udev;
	struct aotg_queue *q;

	ep = acthcd->ep0;

	if (unlikely(!ep || list_empty(&ep->q_list)))
		return;

	q =	list_entry(ep->q_list.next,	struct aotg_queue, ep_q_list);
	urb	= q->urb;
	udev = ep->udev;

	switch (ep->nextpid) {
	case USB_PID_SETUP:
		if (urb->transfer_buffer_length	== urb->actual_length) {
			ep->nextpid	= USB_PID_ACK;
			handle_status(acthcd, ep, 1);	/*no-data transfer */
		} else if (usb_pipeout(urb->pipe)) {
			usb_settoggle(udev,	0, 1, 1);
			ep->nextpid	= USB_PID_OUT;
			write_hcep0_fifo(acthcd, ep, urb);
		} else {
			usb_settoggle(udev,	0, 0, 1);
			ep->nextpid	= USB_PID_IN;
			usb_writeb(0, acthcd->base + HCIN0BC);
		}
		break;
	case USB_PID_OUT:
		urb->actual_length += ep->length;
		usb_dotoggle(udev, ep->epnum, 1);
		if (urb->actual_length >= urb->transfer_buffer_length) {
			ep->nextpid	= USB_PID_ACK;
			handle_status(acthcd, ep, 1);	/*control write	transfer */
		} else {
			ep->nextpid	= USB_PID_OUT;
			write_hcep0_fifo(acthcd, ep, urb);
		}
		break;
	case USB_PID_ACK:
		finish_request(acthcd, q, 0);
		break;
	default:
		dev_err(acthcd->dev, "<CTRL>ep0	out, odd pid %d, %s, %d\n",
			ep->nextpid, __func__, __LINE__);
	}
}

static void handle_hcep0_in(struct aotg_hcd *acthcd)
{
	struct aotg_hcep *ep;
	struct urb *urb;
	struct usb_device *udev;
	struct aotg_queue *q;

	ep = acthcd->ep0;
	if (unlikely(!ep || list_empty(&ep->q_list)))
		return;

	q =	list_entry(ep->q_list.next,	struct aotg_queue, ep_q_list);
	urb	= q->urb;
	udev = ep->udev;

	switch (ep->nextpid) {
	case USB_PID_IN:
		read_hcep0_fifo(acthcd,	ep,	urb);
		break;
	case USB_PID_ACK:
		finish_request(acthcd, q, 0);
		break;
	default:
		dev_err(acthcd->dev, "<CTRL>ep0	out	,odd pid %d\n",	ep->nextpid);
	}
}

static void handle_hcep_in(struct aotg_hcd *acthcd, int index)
{
	struct aotg_hcep *ep;
	struct urb *urb;
	struct aotg_queue *q;
	void *buf;
	int	bufferspace;
	int	count, length, fifo_cnt;
	int	remain;
	u32	word;
	//void * __iomem fifo;
	void __iomem *fifo;
	u8 *byte_buf;
	u16	*short_buf;
	u32	*word_buf;
	int	is_short = 0;
	void *kmap_addr	= NULL;
	void *virt_addr = NULL;

	ep = acthcd->inep[index];
	if (!ep	|| list_empty(&ep->q_list))	{
		ACT_HCD_ERR
		return;
	}

	q =	list_entry(ep->q_list.next,	struct aotg_queue, ep_q_list);
	urb	= q->urb;
	ep = q->ep;

	if (unlikely(ep->is_use_pio	== 0)) {
		ACT_HCD_ERR
		return;
	}

#ifdef MULTI_OUT_FIFO
	if (((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 ||
		(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2)) &&
		!(usb_readb(acthcd->base + OUTXSHORTPCKIRQ) & (0x1 << index)))
#else
	if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 &&
		!(usb_readb(acthcd->base + OUTXSHORTPCKIRQ) & (0x1 << index)))
#endif
	{
		ACT_HCD_ERR
		printk("cpu recieve data, why busy, please check it ...\n");
		WARN_ON(1);
		return;
	}

//	while ((!(usb_readb(ep->reg_hcepcs)	& EPCS_BUSY) &&	(urb->transfer_buffer_length > urb->actual_length))
//		|| (usb_readb(acthcd->base + OUTXSHORTPCKIRQ) &	(0x1 <<	index))) {
	if ((urb->transfer_buffer_length > urb->actual_length) ||
		((usb_readb(acthcd->base + OUTXSHORTPCKIRQ) & (0x1 << index)))) {

		if (q->cur_sg) {
			ACT_HCD_ERR
			if (!PageHighMem(sg_page(q->cur_sg))) {
				ACT_HCD_ERR
				kmap_addr =	sg_virt(q->cur_sg);
				buf	= kmap_addr+q->cur_sg_actual;
			} else {
				ACT_HCD_ERR
				return;
			}
			bufferspace	= q->cur_sg->length	- q->cur_sg_actual;
		} else {
			if (urb->sg != NULL) {
				if (PageHighMem(sg_page(urb->sg))) {
					ACT_HCD_ERR
					buf	= kmap_addr	+ urb->actual_length % PAGE_SIZE;
				} else {
					virt_addr=	sg_virt(urb->sg);
					buf	= virt_addr+ urb->actual_length;
				}
				bufferspace	= urb->sg->length -	urb->actual_length;
			} else if (urb->transfer_buffer	!= NULL) {
				buf	= urb->transfer_buffer + urb->actual_length;
				bufferspace	= urb->transfer_buffer_length -	urb->actual_length;
			} else {
				WARN_ON(1);
				return;
			}
		}

		fifo_cnt = (int)(usb_readw(ep->reg_hcepbc) & 0xFFF);

//		if ((fifo_cnt == 0) && (usb_readb(acthcd->base + OUTXSHORTPCKIRQ) & (0x1 <<index))) {
//			ACT_HCD_DBG
//			printk("cpu in 0 packet,check it...\n");
//		}

		if ((usb_readb(acthcd->base	+ OUTXSHORTPCKIRQ) & (0x1 << index)) &&
			(fifo_cnt == ep->maxpacket)) {
			ACT_HCD_DBG
			fifo_cnt = 0;
		}
		length = min(fifo_cnt, bufferspace);
		is_short = (fifo_cnt < (int)ep->maxpacket) ? 1 : 0;
		remain = length	& 0x3;
		count =	length >> 2;

		if ((u32)buf & 0x1)	{
			byte_buf = (u8 *)buf;
			buf	+= count * 4;
			while (likely(count--))	{
				word = usb_readl(ep->reg_hcfifo);
				*byte_buf++	= (u8)word;
				*byte_buf++	= (u8)(word	>> 8);
				*byte_buf++	= (u8)(word	>> 16);
				*byte_buf++	= (u8)(word	>> 24);

			}
		} else if ((u32)buf	& 0x2) {
			short_buf =	(u16 *)buf;
			buf	+= count * 4;
			while (likely(count--))	{
				word = usb_readl(ep->reg_hcfifo);
				*short_buf++ = (u16)word;
				*short_buf++ = (u16)(word >> 16);
			}
		} else {
			word_buf = (u32	*)buf;
			buf	+= count * 4;
			while (likely(count--))	{
				word = usb_readl(ep->reg_hcfifo);
				*word_buf++	= word;
			}
		}

		if (remain)	{
			u8 *remain_buf = (u8 *)buf;
			fifo = ep->reg_hcfifo;

			while (remain--) {
				*remain_buf++ =	usb_readb(fifo);
				fifo++;
			}
		}
		urb->actual_length += length;
		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	0);

		if (q->cur_sg) {
			ACT_HCD_ERR
			dma_sync_sg_for_device(aotg_to_hcd(acthcd)->self.controller,
				q->cur_sg, 1, DMA_TO_DEVICE);
			//dma_cache_wback((u32)(sg_virt(q->cur_sg) + q->cur_sg_actual), length);
			q->cur_sg_actual +=	length;
			if (q->cur_sg_actual >=	q->cur_sg->length) {
				q->cur_sg =	sg_next(q->cur_sg);
				q->cur_sg_actual = 0;
			}
		}


		usb_writeb(0, ep->reg_hcepcs);

	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		urb->iso_frame_desc[0].status = 0;
		urb->iso_frame_desc[0].actual_length = urb->actual_length;
		finish_request(acthcd, q, 0);
		if (usb_readw(ep->reg_hcincount_wt) <= 3) {
			usb_writew(0xffff, ep->reg_hcincount_wt);
			usb_setbitsb(0x1 <<	ep->index, acthcd->base + HCINXSTART);
		}
		pio_irq_clear(acthcd, ep->mask);
		hcerr_irq_clear(acthcd, ep->mask);
		return;
	}

	if (urb->actual_length >= urb->transfer_buffer_length) {
		ep_disable(ep);
		__hc_in_stop(acthcd, ep);
		pio_irq_disable(acthcd,	ep->mask);
		pio_irq_clear(acthcd, ep->mask);
		hcerr_irq_clear(acthcd, ep->mask);
		if (usb_pipeint(q->urb->pipe)) {
			ep->inc_intval = 0;
			hcerr_irq_disable(acthcd, ep->mask);
		}

		/* q->cur_sg maybe set to NULL in q->cur_sg	= sg_next(q->cur_sg); */
		if (!q->cur_sg && urb->actual_length > 0) {
					//if (urb->transfer_buffer != NULL)
				//dma_cache_wback((u32)urb->transfer_buffer, urb->actual_length);
		}
		finish_request(acthcd, q, 0);
	} else if (is_short) {

		ep_disable(ep);
		__hc_in_stop(acthcd, ep);
		pio_irq_disable(acthcd,	ep->mask);
		pio_irq_clear(acthcd, ep->mask);
		hcerr_irq_clear(acthcd, ep->mask);
		if (usb_pipeint(q->urb->pipe)) {
			ep->inc_intval = 0;
			hcerr_irq_disable(acthcd, ep->mask);
		}

		if (urb->transfer_flags	& URB_SHORT_NOT_OK)
			finish_request(acthcd, q, -EREMOTEIO);
		else
			finish_request(acthcd, q, 0);
	}

	return;
}

static int handle_hcep_pio(struct aotg_hcd *acthcd,
				struct aotg_hcep *ep, struct aotg_queue	*q)
{
	int	count;
	int	remain;
	u16	*short_buf;
	u32	*word_buf;
	struct urb *urb	= q->urb;
	int	i =	0;
	int	length = 0;
	void *buf =	NULL;
//	void *kmap_addr	= NULL;
	void *virt_addr = NULL;

#ifdef MULTI_OUT_FIFO
	while ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0	||
		(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
	while((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0)
#endif
	{
		ACT_HCD_ERR
		printk("why still busy, err, please check it ...\n");

		i++;
		udelay(100);
		if (i > 100) {
			printk("wait 10ms, still busy, err,check it...\n");
			return 1;
		}
	}

	if (urb->actual_length >= urb->transfer_buffer_length) {
		if (q->need_zero) {
//			ACT_HCD_DBG
//			printk("cpu out 0 packet,check it...\n");
			q->need_zero = 0;
			usb_writew(0, ep->reg_hcepbc);
			usb_writeb(0, ep->reg_hcepcs);
			usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	1);
		} else
			return 1;
	} else {
		if (q->cur_sg)
			ACT_HCD_ERR
		else {
			if (urb->sg	!= NULL) {
				if (PageHighMem(sg_page(urb->sg)))
					ACT_HCD_ERR
				else {
					virt_addr=	sg_virt(urb->sg);
					buf	= virt_addr + urb->actual_length;
				}
				length = min((int)urb->sg->length -	(int)urb->actual_length,
						(int)ep->maxpacket);
			} else if (urb->transfer_buffer	!= NULL) {
				buf	= urb->transfer_buffer + urb->actual_length;
				length = min((int)urb->transfer_buffer_length -
					(int)urb->actual_length, (int)ep->maxpacket);
			} else
				WARN_ON(1);
		}

		WARN_ON(length == 0);
		count =	length >> 2;
		remain = length	& 0x3;
		prefetch(buf);

		if ((u32)buf & 0x1)	{
			u8 *byte_buf = (u8 *)buf;

			while (likely(count--))	{
				u32	word = *byte_buf | *(byte_buf +	1) << 8
						| *(byte_buf + 2) << 16	| *(byte_buf + 3) << 24;
				byte_buf +=	4;
				usb_writel(word, ep->reg_hcfifo);
			}
			buf	= byte_buf;
		} else if ((u32)buf	& 0x2) {
			short_buf =	(u16 *)buf;

			while (likely(count--))	{
				u32	word = *short_buf |	*(short_buf	+ 1) <<	16;
				short_buf += 2;
				usb_writel(word, ep->reg_hcfifo);
			}
			buf	= short_buf;
		}
		else {
			word_buf = (u32	*)buf;
			//ACT_HCD_DBG
			while (likely(count--))
				usb_writel(*word_buf++,	ep->reg_hcfifo);
			buf	= word_buf;
		}

		if (remain) {
			word_buf = (u32	*)buf;
			usb_writel(*word_buf,	ep->reg_hcfifo);
		}
		urb->actual_length += length;

		//start transfer data to phy
		usb_writew(length, ep->reg_hcepbc);
		usb_writeb(0, ep->reg_hcepcs);


		if (q->cur_sg) {
			ACT_HCD_ERR
			//dma_sync_sg_for_device(aotg_to_hcd(acthcd)->self.controller,q->cur_sg, 1, DMA_TO_DEVICE);
			q->cur_sg_actual +=	length;
			if (q->cur_sg_actual >=	q->cur_sg->length) {
				q->cur_sg =	sg_next(q->cur_sg);
				q->cur_sg_actual = 0;
			}
		}

		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	1);
	}

	pio_irq_enable(acthcd, ep->mask);
	return 0;
}

static void aotg_hcd_dma_out_finish(struct aotg_hcd *acthcd,
	struct aotg_queue *q, int pio_flag)
{
	struct urb *urb;
	struct aotg_hcep *ep;
//	u8 fifo_ctrl;

	urb = q->urb;
	ep = q->ep;

#ifdef MULTI_OUT_FIFO
	if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 ||
		(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
	if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0)
#endif
	{
		printk("err : %s, fifo busy, crazy!\n", __func__);
		printk("hcepcs:0x%X\n",usb_readb(ep->reg_hcepcs));
	}

	if ((q->need_zero) &&
		((urb->actual_length == urb->transfer_buffer_length) &&
		(urb->actual_length % ep->maxpacket == 0))) {

//		pio_irq_clear(acthcd, ep->mask);
//		fifo_ctrl = (0<<5) | (1 <<4) | ep->index;
//		usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
		q->data_done = 1;
		q->need_zero = 0;

		usb_writew(0, ep->reg_hcepbc);
		usb_writeb(0, ep->reg_hcepcs);
		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	1);
//		pio_irq_enable(acthcd, ep->mask);
//		finish_request(acthcd, q, 0);

	}
//	else {
//		pio_irq_clear(acthcd, ep->mask);
//		pio_irq_disable(acthcd, ep->mask);
//
//		if (pio_flag) {
//			ep->dma_outirq_cnt++;
//		}
//
//		finish_request(acthcd, q, 0);
//	}
	pio_irq_clear(acthcd, ep->mask);
	pio_irq_disable(acthcd, ep->mask);

	if (pio_flag)
		ep->dma_outirq_cnt++;

	finish_request(acthcd, q, 0);

	return;
}

static void handle_hcep_out(struct aotg_hcd *acthcd, int index)
{
	struct aotg_hcep *ep;
	struct aotg_queue *q;

	ep = acthcd->outep[index];
	if (!ep	|| list_empty(&ep->q_list))	{
		ACT_HCD_ERR
		return;
	}

#ifdef MULTI_OUT_FIFO
	if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 ||
		(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
	if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0)
#endif
	{
		ACT_HCD_ERR
		printk("why  busy here, err, please check it ...\n");
		return;
	}

	/*
	* add : deal dma fifo busy
	*/
	if (ep->is_use_pio == 0) {
		q =	list_entry(ep->q_list.next, struct aotg_queue, ep_q_list);
		aotg_hcd_dma_out_finish(acthcd, q,1);
		return;
	}

	q =	list_entry(ep->q_list.next,	struct aotg_queue, ep_q_list);
	if (unlikely(q->is_start_dma))	{	//avoid	hardware bug
		ACT_HCD_ERR
		return;
	}

	if (ep->is_use_pio)	{
		if (handle_hcep_pio(acthcd,	ep,	q))
			finish_request(acthcd, q, 0);
	} else {
		ACT_HCD_ERR
		if (q->data_done) {
			q->data_done = 0;
			if (q->need_zero) {
				q->need_zero = 0;
				usb_writew(0, ep->reg_hcepbc);
				usb_writeb(0, ep->reg_hcepcs);
			} else
				finish_request(acthcd, q, 0);
		}
	}
	return;
}

static int aotg_hcd_start_dma(struct aotg_hcd *acthcd, struct aotg_queue *q,
				int dmanr)
{
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;
	u32	i;
	u8 is_in = !usb_pipeout(urb->pipe);
	u8 ep_select;
	u32	dma_length = 0;
#ifdef SHORT_PACK_487_490
	u8 reg_hcoutxerr;
#endif

	if (acthcd->dma_queue[dmanr & 0x1].next != &q->dma_q_list || q->is_start_dma)
		goto done;

	if (unlikely(!HC_IS_RUNNING(aotg_to_hcd(acthcd)->state)))
		return -ESHUTDOWN;
	i =	aotg_hcd_dma_get_cmd(dmanr);
	if (i & 0x1)
		ACT_HCD_ERR

	aotg_hcd_get_dma_buf(acthcd, urb, q);
	ACT_DMA_DEBUG("expect length: %d, dir: %s, %s, %d\n",
		q->length, usb_pipeout(urb->pipe)?"out":"in",
		__func__, __LINE__);

	ep->dma_bytes = 0;
	if (q->cur_sg) {
		ACT_HCD_ERR
		goto done;
	} else {
//		if (urb->sg == NULL) {
			if (urb->actual_length != 0) {
				ACT_HCD_ERR
				printk("urb->actual_length:%d\n", urb->actual_length);
				urb->actual_length = 0;
			}
#if 0
			if (urb->sg && (!urb->transfer_dma)) {
				printk("s:[%d, %d]\n", urb->num_mapped_sgs, urb->num_sgs);
				printk("s:d:0x%lx, p:0x%lx, o:%d, l:%d\n", (unsigned
				long)urb->sg->dma_address,(unsigned long) urb->sg->page_link,
				urb->sg->offset, urb->sg->length);
				printk("s:td:0x%lx\n",(unsigned long)urb->transfer_dma);
			} else if (!urb->transfer_dma) {
				printk("td:0x%lx\n",(unsigned long)urb->transfer_dma);
			}
#endif
			ep->dma_bytes =	q->length;
			dma_length = ep->dma_bytes;
#if 0
		} else {
			//ACT_HCD_ERR
			if (PageHighMem(sg_page(urb->sg))) {
				ACT_HCD_ERR
				urb->transfer_dma &= 0xfffffff;
				urb->transfer_dma |= 0x90000000;
			}
			ep->dma_bytes =	q->length;
			dma_length = ep->dma_bytes;
		}
#endif
	}

	WARN_ON(dma_length == 0	|| ep->dma_bytes > q->length);

	if (is_in) {
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs))	== 0) {
			ACT_HCD_ERR
			printk("fifo_cnt = %d\n", (int)(usb_readw(ep->reg_hcepbc) &	0xFFF));

			aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
			i = 100000;
			while ((EPCS_BUSY &	usb_readb(ep->reg_hcepcs)) == 0 && i) {
				udelay(1);
				i--;
				if (50000 == i || i == 0)
					aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
			}

			if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) == 0 && i == 0) {
				ACT_HCD_ERR
				q->status = -EREMOTEIO;
				list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				return -1;
			}
		}
	} else {
#ifdef MULTI_OUT_FIFO
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs))	!= 0 ||\
		     (EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs))	!= 0 )
#endif
		{
			ACT_HCD_ERR
		}
#if	0
		i =	100000;
		while (((EPCS_BUSY & usb_readb(ep->reg_hcepcs))	!= 0) && i)	{ i--; udelay(1);};
		if (i == 0)	{
			printk("i:%d\n", i);

			printk("acthcd->base:%x	\n", (unsigned int)acthcd->base);
			printk("reg_hcepcs:%x \n", (unsigned int)ep->reg_hcepcs);
			printk("HCOUT1CS:%x	\n", (unsigned int)HCOUT1CS);
			printk("HCOUT2CS:%x	\n", (unsigned int)HCOUT2CS);
			printk("EPCS_BUSY:%x \n", (unsigned	int)EPCS_BUSY);
			printk("val:%x \n",	(unsigned int)usb_readb(ep->reg_hcepcs));

			aotg_dbg_proc_output_ep_state(acthcd);
			printk("\n"); printk("\n");	printk("\n");
			aotg_dbg_regs(acthcd);
			printk("\n"); printk("\n");	printk("\n");

			usb_writeb(0x1<<6 |	0x1<<5,	acthcd->base + HCPORTCTRL);	/*portrst &	55ms */
			aotg_dbg_output_info();
			printk("\n"); printk("\n");	printk("\n");
			aotg_dbg_proc_output_ep();
			WARN_ON(1);
		}
#endif
	}

	pio_irq_disable(acthcd,	ep->mask);
	acthcd->dma_working[dmanr &	0x1] = urb->pipe;

	q->is_start_dma	= dmanr	+ 1;

	ep_select =	(ep->index << 1) | is_in;
	aotg_hcd_dma_set_mode(dmanr, ep_select);

	if (is_in) {
		if (q->cur_sg)
			ACT_HCD_ERR
		else
			aotg_hcd_dma_set_memaddr(dmanr, q->dma_addr);
		aotg_hcd_dma_set_cnt(dmanr,	dma_length);
	} else {
		if (q->cur_sg)
			ACT_HCD_ERR
		else
			aotg_hcd_dma_set_memaddr(dmanr, q->dma_addr);

#ifdef SHORT_PACK_487_490
		ep->shortpack_length = dma_length % 512;

		if (ep->shortpack_length >= 487 && ep->shortpack_length <= 490 &&
			ep->shortpack_length == dma_length) {
			printk("WARNING:dma_length == short_pack_length = %u\n",
				ep->shortpack_length);
		    i = 100000;

#ifdef MULTI_OUT_FIFO
			while(((usb_readb(ep->reg_hcepcs) & EPCS_BUSY) != 0 ||
				(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2)) && i)
#else
			while((usb_readb(ep->reg_hcepcs) & EPCS_BUSY) && i)
#endif
			{
				i--;
				udelay(10);
			}

			if (!i) {
				printk("%s, wait 1s, fifo still busy! SHORT_PACK_487_490\n",
					__func__);
				aotg_hcd_show_ep_info(acthcd);
				aotg_dbg_regs(acthcd);
			}

			reg_hcoutxerr = usb_readb(ep->reg_hcerr);
			usb_writeb(reg_hcoutxerr | DO_PING, ep->reg_hcerr); //DO_PING 0x1 << 6

			ep->shortpack_length = 0;
			ep->is_shortpack_487_490 = false;
		} else if (ep->shortpack_length >= 487 &&
			ep->shortpack_length <= 490) {
			printk("WARNING:dma_length = %u, short_pack_length = %u\n",
				dma_length,ep->shortpack_length);
			ep->dma_bytes = dma_length - ep->shortpack_length;
			dma_length = ep->dma_bytes;
			ep->is_shortpack_487_490 = true;
			printk("WARNING:dma_bytes = %u\n",ep->dma_bytes);
			printk("WARNING:go to deal with SHORT_PACK_487_490--->\n");
		} else {
			ep->shortpack_length = 0;
			ep->is_shortpack_487_490 = false;
		}
#endif

		aotg_hcd_dma_set_cnt(dmanr, dma_length);
	}

	if (q->cur_sg)
		ACT_HCD_ERR

	//if (is_in) {
		//aotg_hcep_reset(acthcd, ep->mask,	ENDPRST_FIFORST	| ENDPRST_TOGRST);
	//}
	//usb_writew(0, ep->reg_hcepbc);
#if	0
	/*
	 * check dma addr right	or not.
	 * why when	we call	get_memaddr, tcp seq out of	order is decreased?
	 */
	if ((unsigned int)urb->transfer_dma	!= aotg_hcd_dma_get_memaddr(dmanr))	{
		ACT_HCD_ERR
		printk("transfer_dma:%x\n",	(unsigned int)urb->transfer_dma);
		printk("get_memaddr:%x\n", (unsigned int)aotg_hcd_dma_get_memaddr(dmanr));
	}
#endif
	aotg_hcd_dma_start(dmanr);
#if	0
	if (is_in) {
		if (aotg_hcd_dma_get_cnt(acthcd->dma_nr0) != aotg_hcd_dma_get_remain(acthcd->dma_nr0)) {
			ACT_HCD_ERR
			printk("dmanr:%d\n", dmanr);
			printk("acthcd->dma_nr0:%d\n", acthcd->dma_nr0);
			printk("cnt:%d\n", aotg_hcd_dma_get_cnt(acthcd->dma_nr0));
			printk("remain:%d\n", aotg_hcd_dma_get_remain(acthcd->dma_nr0));
		}
	}
#endif
done:
	return 0;
}

static void aotg_hcd_dma_handler(int dma_no, void *dev_id)
{
	struct aotg_hcd *acthcd = (struct aotg_hcd *)dev_id;
	struct aotg_queue *q;
	struct urb *urb;
	struct aotg_hcep *ep;
	int	length;
#ifdef SHORT_PACK_487_490
	u8 ep_select;
	u8 reg_hcoutxerr;
	u32 i;
#endif
	//unsigned int intoken_cnt = 0;

	//WARN_ON(list_empty(&acthcd->dma_queue[dma_no & 0x1]));
	if (list_empty(&acthcd->dma_queue[dma_no & 0x1])) {
		ACT_HCD_ERR
		return;
	}

	q =	list_entry(acthcd->dma_queue[dma_no	& 0x1].next,
		struct aotg_queue, dma_q_list);
	urb	= q->urb;
	ep = q->ep;

#if	0
	/*
	 * check dma addr right	or not.
	 * why when	we call	get_memaddr, tcp seq out of	order is decreased?
	 */
	if ((unsigned int)urb->transfer_dma	!= aotg_hcd_dma_get_memaddr(dma_no)) {
		ACT_HCD_ERR
		printk("transfer_dma:%x\n",	(unsigned int)urb->transfer_dma);
		printk("get_memaddr:%x\n", (unsigned int)aotg_hcd_dma_get_memaddr(dma_no));
	}
#endif

	if (q->cur_sg)
		length = ep->dma_bytes;
	else
		length = ep->dma_bytes - aotg_hcd_dma_get_remain(dma_no);

	urb->actual_length += length;

#ifdef SHORT_PACK_487_490
	if ((usb_pipeout(urb->pipe)) && ep->is_shortpack_487_490) {
//		if (ep->shortpack_length >= 487 && ep->shortpack_length <= 490)
		printk("WARNING:deal with the SHORT_PACK_487_490\n");

		i = 100000;
#ifdef MULTI_OUT_FIFO
		while(((usb_readb(ep->reg_hcepcs) & EPCS_BUSY) != 0 ||
			(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2)) && i)
#else
		while(((usb_readb(ep->reg_hcepcs) & EPCS_BUSY) != 0) && i)
#endif
		{
			i--;
			udelay(10);
		}

		if (!i) {
			printk("%s,ERR:wait 1000 ms, fifo still busy!--->sending the SHORT_PACK_487_490\n",__func__);
			aotg_hcd_show_ep_info(acthcd);
			aotg_dbg_regs(acthcd);
		}

		//ping before next dma transfer
//		printk("%s,sending PING package:[ep->reg_hcepcs = 0x%X]\n",__func__,usb_readb(ep->reg_hcepcs));
		reg_hcoutxerr = usb_readb(ep->reg_hcerr);
		usb_writeb(reg_hcoutxerr | DO_PING, ep->reg_hcerr); //DO_PING 0x1 << 6

		ep_select = (ep->index << 1) | 0x0; //0x0 means OUT
		// TODO: no defined dma ==> compile fine cause no defined SHORT_PACK_487_490
		dma = (u32)urb->transfer_dma + urb->actual_length;
		ep->dma_bytes = ep->shortpack_length;

		aotg_hcd_dma_set_mode(acthcd->dma_nr1, ep_select);
		aotg_hcd_dma_set_memaddr(acthcd->dma_nr1, dma);
		aotg_hcd_dma_set_cnt(acthcd->dma_nr1, ep->shortpack_length);

		usb_writew(0, ep->reg_hcepbc);
		aotg_hcd_dma_start(acthcd->dma_nr1);

//		if (ep->shortpack_length >= 487 && ep->shortpack_length <= 490) {
//			printk("WARNING:urb->actual_length = %u, ep->dma_bytes = %u\n",urb->actual_length,ep->dma_bytes);
//			printk("WARNING:finish SHORT_PACK_487_490\n");
//		}
		printk("WARNING:urb->actual_length = %u, ep->dma_bytes = %u\n",
			urb->actual_length, ep->dma_bytes);
		printk("WARNING:finish SHORT_PACK_487_490\n");

		ep->shortpack_length = 0;
		ep->is_shortpack_487_490 = false;
		return;
	}
#endif

	acthcd->dma_working[dma_no & 0x1] =	0;

	if (usb_pipeout(urb->pipe)) {
		if (aotg_hcd_dma_get_remain(dma_no)	!= 0)
			printk("bytes:%d, remain:%d\n",	ep->dma_bytes,
				aotg_hcd_dma_get_remain(dma_no));
	} else
		aotg_hcd_dma_sync();

	q->is_start_dma	= 0;
	list_del_init(&q->dma_q_list);
	//aotg_hcd_dma_stop(dma_no);
	//aotg_hcd_dma_reset_2(dma_no);

	if ((acthcd->dma_working[0]	== 0) && (acthcd->dma_working[1] ==	0))	{
		if ((0x1 & aotg_hcd_dma_get_cmd(acthcd->dma_nr0)) ||
			(0x1 &	aotg_hcd_dma_get_cmd(acthcd->dma_nr1)))	{
			ACT_HCD_ERR
			printk("dma0:%d\n",	aotg_hcd_dma_get_cmd(acthcd->dma_nr0));
			printk("dma1:%d\n",	aotg_hcd_dma_get_cmd(acthcd->dma_nr1));
		}//else
			//aotg_hcd_dma_reset_2(acthcd->dma_nr0);
		//printk("dma reset! \n");
	}

	if (((length + ep->maxpacket - 1)/ ep->maxpacket) &	1)
		usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	usb_pipeout(urb->pipe));

	ACT_DMA_DEBUG("actual length: %d, dir: %s, %s, %d\n",
		length,	usb_pipeout(urb->pipe) ? "out" : "in",
		__func__, __LINE__);

	if (usb_pipein(urb->pipe)) {
		if (!(usb_readb(ep->reg_hcepcs)	& EPCS_BUSY)) {
			ACT_HCD_DBG
			usb_writeb(0, ep->reg_hcepcs);
			usb_dotoggle(urb->dev, usb_pipeendpoint(urb->pipe),	0);
		}//else
			//ACT_HCD_DBG

		ep_disable(ep);
		__hc_in_stop(acthcd, ep);
		pio_irq_clear(acthcd, ep->mask);
		//pio_irq_enable(acthcd, ep->mask);

		if (urb->transfer_flags	& URB_SHORT_NOT_OK)
			finish_request(acthcd, q, urb->actual_length <
				urb->transfer_buffer_length ? -EREMOTEIO :	0);
		else
			finish_request(acthcd, q, 0);
	} else {
#if	0
		u32	mask = ep->maxpacket - 1;
		int	remain = length	& mask;

		if (remain)	{
			if (!(usb_readb(ep->reg_hcepcs)	& EPCS_BUSY)) {
				usb_writew(remain, ep->reg_hcepbc);
				usb_writeb(0, ep->reg_hcepcs);
				q->data_done = 1;
			}
		}
#endif
		if ((urb->actual_length < urb->transfer_buffer_length)) {
			ACT_HCD_ERR
			printk("act_l:%u, trns_l:%u\n", urb->actual_length,
			urb->transfer_buffer_length);
		}

		pio_irq_clear(acthcd, ep->mask);
		// if fifo busy,use hcepxout irq to finish the q
#ifdef MULTI_OUT_FIFO
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 ||
			(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0)
#endif
		{
			ep->dma_handler_cnt++;
			pio_irq_enable(acthcd, ep->mask);

		} else
			aotg_hcd_dma_out_finish(acthcd, q,0);
	}

	return;
}

static int handle_bulkout_packet(struct aotg_hcd *acthcd,
	struct aotg_queue *q)
{
	u8 fifo_ctrl;
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;
	ep->q = q;

	if (q->is_xfer_start ||	ep->q_list.next	!= &q->ep_q_list)
		return 0;

	if (!HC_IS_RUNNING(aotg_to_hcd(acthcd)->state))
		return -ESHUTDOWN;

	q->is_xfer_start = 1;

#if 1
	if (acthcd->use_dma) {
		if ((acthcd->dma_working[1] == 0) &&
			(urb->transfer_buffer_length >= AOTG_MIN_DMA_SIZE)) {
			//(urb->pipe == acthcd->dma_working[1]))
			q->length =	urb->transfer_buffer_length;
			list_add_tail(&q->dma_q_list, &acthcd->dma_queue[1]);
			ep->is_use_pio = 0;
			fifo_ctrl =	(1<<5) | (1 <<4) | ep->index;
			usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
			aotg_hcd_start_dma(acthcd, q, acthcd->dma_nr1);
			return 0;
		}
	}
#endif

	ep->is_use_pio = 1;
	fifo_ctrl = (0<<5) | (1 <<4) | ep->index;
	usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
	aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
	pio_irq_clear(acthcd, ep->mask);
	//pio_irq_enable(acthcd, ep->mask);

	handle_hcep_pio(acthcd,	ep,	q);
	return 0;
}

static int handle_bulkin_packet(struct aotg_hcd *acthcd,
	struct aotg_queue *q)
{
	u8 fifo_ctrl;
	struct urb *urb	= q->urb;
	struct aotg_hcep *ep = q->ep;
	u16	w_packets;
	int	retval = 0;

	if (q->is_xfer_start ||	ep->q_list.next	!= &q->ep_q_list) {
		ACT_HCD_ERR
		return 0;
	}

	if (!HC_IS_RUNNING(aotg_to_hcd(acthcd)->state)) {
		ACT_HCD_ERR
		return -ESHUTDOWN;
	}
	q->is_xfer_start = 1;
	w_packets =	urb->transfer_buffer_length	/ ep->maxpacket;
	if (urb->transfer_buffer_length	& (ep->maxpacket - 1))
		w_packets++;
#if 1
	if (acthcd->use_dma) {
		if (((acthcd->dma_working[0] ==	0) &&
			(urb->transfer_buffer_length >= AOTG_MIN_DMA_SIZE)) ||
			(acthcd->dma_working[0]	== urb->pipe)) {
			q->length =	urb->transfer_buffer_length;
			list_add_tail(&q->dma_q_list, &acthcd->dma_queue[0]);
			//usb dma, set auto fifo bit
			ep->is_use_pio = 0;
			fifo_ctrl = 	(1<<5) | (0 <<4) | ep->index;
			usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
			if (aotg_hcd_start_dma(acthcd, q, acthcd->dma_nr0)) {
				ACT_HCD_ERR
				return 0;
			}
			goto xfer_start;
		}
	}
#endif

	//use cpu, not set auto fifo bit
	ep->is_use_pio = 1;
	fifo_ctrl = (0<<5) | (0 <<4) | ep->index;
	usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
xfer_start:
	/* do not worry	about re enable	again, for at the func's start position,
	 * it already check	(ep->q_list.next !=	&q->ep_q_list).
	 */
	ep_enable(ep);
#if	1
	if (ep->is_use_pio)	{
		aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
		pio_irq_clear(acthcd, ep->mask);
		pio_irq_enable(acthcd, ep->mask);
	}
#endif

	__hc_in_start(acthcd, ep, w_packets);
	ep->nextpid	= USB_PID_IN;
	q->inpacket_count =	w_packets;

	return retval;
}

static int handle_intr_packet(struct aotg_hcd *acthcd, struct aotg_queue *q)
{
	struct aotg_hcep *ep = q->ep;

	q->is_xfer_start = 1;
	aotg_to_hcd(acthcd)->self.bandwidth_allocated += ep->load /	ep->period;
	aotg_to_hcd(acthcd)->self.bandwidth_int_reqs++;

	hrtimer_start(&ep->intr_timer,
		ktime_set(ep->interval_s, ep->interval_ns), HRTIMER_MODE_REL);

//	queue_delayed_work(acthcd->periodic_wq,	&ep->dwork,	msecs_to_jiffies(ep->period));
//	queue_work(acthcd->periodic_wq,	&ep->dwork.work);
	return 0;
}

static void handle_iso_packet(struct aotg_hcd *acthcd, struct aotg_queue *q)
{
	struct aotg_hcep *ep = q->ep;
	struct urb *urb = NULL;
	int pipe;
	//u16 w_packets;
	u8 fifo_ctrl;

	q->is_xfer_start = 1;
	aotg_to_hcd(acthcd)->self.bandwidth_allocated += ep->load /	ep->period;
	aotg_to_hcd(acthcd)->self.bandwidth_int_reqs++;
	if (!list_empty(&ep->q_list)) {
		q =	list_first_entry(&ep->q_list, struct aotg_queue, ep_q_list);
		urb	= q->urb;
		pipe = urb->pipe;

		if (ep->iso_start == 0) {
		/*    if (((hcd_suspend_en == 0) && usb_pipein(pipe))||(hcd_suspend_en == 1)) {
			w_packets =	urb->transfer_buffer_length	/ MAX_PACKET(ep->maxpacket);
			if (urb->transfer_buffer_length	& (MAX_PACKET(ep->maxpacket) - 1)) {
				w_packets++;
			}*/
			ep->iso_start = 1;
			//reset	fifo, skip the undo	data
			aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
			ep_enable(ep);
			pio_irq_clear(acthcd, ep->mask);
			pio_irq_enable(acthcd, ep->mask);
			hcerr_irq_clear(acthcd, ep->mask);

			//use cpu, not set auto fifo bit
			ep->is_use_pio = 1;
			fifo_ctrl = (0<<5) | (0 <<4) | ep->index;
			usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
			//u8 set = 0x1 <<	ep->index;

			usb_writew(0xffff, ep->reg_hcincount_wt);
			//printk(" reg_hcincount_wt:%x,HCIN1_COUNT(0x%p) : 0x%X\n",ep->reg_hcincount_wt,
		  //          acthcd->base + HCIN1_COUNTL, usb_readw(acthcd->base + HCIN1_COUNTL));
			//usb_setbitsb(set, acthcd->base + OUTXNAKCTRL);
			usb_setbitsb(0x1 <<	ep->index, acthcd->base + HCINXSTART);
			/*if (hcd_suspend_en == 0)
				__hc_in_start(acthcd, ep, w_packets);
			else {
				if (usb_pipein(pipe))
					__hc_in_start(acthcd, ep, w_packets);
				else
					handle_hcep_pio(acthcd,	ep,	q);
				}
			}*/
		}/* else {	// never happed, enqueue filter	it
			retval = -ENODEV;
			dev_err(acthcd->dev, "<Intr>xfers wrong	type, ignore it, %s()\n", __func__);
		}*/
	}
	return;
}

#ifdef EP_TIMEOUT_DISCONNECT
static void aotg_hcd_do_disconnect(struct aotg_hcd *acthcd,
	struct aotg_hcep *ep)
{
	printk("WARNNING-->%s, ep[index:%d,num:%d], cnt : %d\n",
		__func__, ep->index,ep->epnum, ep->ep_timeout_cnt);
	usb_writeb(0x3f, acthcd->base +	HCIN07ERRIRQ);
	usb_writeb(0x3f, acthcd->base +	HCOUT07ERRIRQ);
	usb_writeb(0x3f, acthcd->base +	HCIN07IRQ);
	usb_writeb(0x3f, acthcd->base +	HCOUT07IRQ);
	usb_clearbitsb(0x3f,	acthcd->base + HCIN07ERRIEN);
	usb_clearbitsb(0x3f,	acthcd->base + HCOUT07ERRIEN);

	acthcd->put_aout_msg = 0;
	acthcd->discon_happened = 1;
	mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(1));
}

static void aotg_hcd_bulk_timeout_disconnect(struct aotg_hcd *acthcd,
	struct aotg_hcep *ep, u8 error)
{
	u8 err_type = 0;

	err_type =  (error >> 2) & 0x7;

	//printk("ep->type : 0x%X, err_type : 0x%X\n", ep->type, err_type);

	if (ep->type == PIPE_BULK && err_type == NO_HANDSHAKE) {
	//if (err_type == NO_HANDSHAKE) {
		ep->ep_timeout_cnt++;
		DBG_TIMEOUT("%s, ep[index:%d,num:%d], cnt: %d\n",
			__func__, ep->index,ep->epnum, ep->ep_timeout_cnt);

		if (ep->ep_timeout_cnt > TIMEOUT_MAX_CNT) {
			ep->ep_timeout_cnt = 0;
			aotg_hcd_do_disconnect(acthcd, ep);
		}

	} else
		ep->ep_timeout_cnt = 0;

	return;
}
#else
#define aotg_hcd_bulk_timeout_disconnect NULL
#define aotg_hcd_do_disconnect NULL
#endif

static void aotg_hcd_do_error(struct aotg_hcd *acthcd,
				struct aotg_hcep *ep, u8 error)
{
	struct aotg_queue *q;
	struct urb *urb;
	u8 reset = 0;
	int	status;

	//special handling: not get disconnect signal
	aotg_hcd_bulk_timeout_disconnect(acthcd, ep, error);

	// error type
	switch (error &	(7<<2))	{
	case 0:
		status = 0;
		break;

	case 3<<2:
		status = -EPIPE;
		reset =	ENDPRST_FIFORST	| ENDPRST_TOGRST;
		break;

	case 4<<2:
		status = -ETIMEDOUT;
		reset =	ENDPRST_FIFORST	| ENDPRST_TOGRST;
		break;

	default:
		status = -EIO;
		reset =	ENDPRST_FIFORST	| ENDPRST_TOGRST;
		break;
	}

	//the transaction still not finish,
	if (ep && !list_empty(&ep->q_list))	{
		q =	list_entry(ep->q_list.next, struct aotg_queue, ep_q_list);
		urb	= q->urb;

		if ((ep->type == PIPE_INTERRUPT) || (ep->type == PIPE_ISOCHRONOUS)) {
			//ep_disable(ep);
			if (hcd_suspend_en == 0)
				__hc_in_stop(acthcd, ep);
			else if (usb_pipein(urb->pipe))
				__hc_in_stop(acthcd, ep);
			pio_irq_disable(acthcd,	ep->mask);
			pio_irq_clear(acthcd, ep->mask);
			hcerr_irq_clear(acthcd, ep->mask);
			hcerr_irq_disable(acthcd, ep->mask);

			if (!list_empty(&q->finished_list)) {
				return;
				ACT_HCD_ERR
				//bug : todo
			} else if (status != -EPIPE) {
				ep->intr_recall_cnt++;
				hrtimer_start(&ep->intr_timer, ktime_set(ep->interval_s, ep->interval_ns), HRTIMER_MODE_REL);
			} else {
				if (q->is_start_dma) {
					ACT_HCD_DBG
					__clear_dma(acthcd,	q, q->is_start_dma-1);
				}

				finish_request(acthcd, q, status);

				dev_info(acthcd->dev, "%s ep %d,  num = %d , type = %d,pio=%d,err[0x%02X],err_type [0x%02X], finish intr queue\n",
					(ep->mask & USB_HCD_OUT_MASK) ?"HC OUT":"HC IN", ep->index,ep->epnum,ep->type, ep->is_use_pio,error, (error>>2)&0x7);
			}

			return;
		}

		//type: RESERVED, NO_HANDSHAKE, PID_ERR, < 10 times (resend)
		if (ep->index >	0 && status != -EPIPE) {
			if (q->err_count < MAX_ERROR_COUNT)	{
				EP_ERRINFO(acthcd->dev, "%s ep %d,  num = %d ,type = %d, pio=%d,err[0x%02X],err_ type [0x%02X], rescend it\n",
					usb_pipeout(urb->pipe)?"HC OUT":"HC IN", ep->index,	ep->epnum,ep->type,ep->is_use_pio,error, (error>>2)&0x7);
				q->err_count++;
				usb_writeb(0x1 << 5, ep->reg_hcerr);
				return;
			} else {
				EP_ERRINFO(acthcd->dev,"%d dbg!\n", __LINE__);
				q->err_count = 0;
			}
		}

		//type:STALL, > 10 times  (reset & finish)
		if (ep->index >	0) {
			if (usb_pipeout(urb->pipe))	{
				aotg_hcep_reset(acthcd,	ep->mask | USB_HCD_OUT_MASK, reset);
			} else {
				if (ep->index >	0) {
					ep_disable(ep);
					__hc_in_stop(acthcd, ep);
				}
					aotg_hcep_reset(acthcd,	ep->mask, reset);
			}
			dev_info(acthcd->dev, "%s ep %d,  num = %d , pio=%d,error [0x%02X],	err_ype [0x%02X],	reset it...\n",
					usb_pipeout(urb->pipe)?"HC OUT":"HC	IN", ep->index,	ep->epnum,ep->is_use_pio,error, (error>>2)&0x7);
		}

		if (q->is_start_dma) {
			ACT_HCD_DBG
			__clear_dma(acthcd,	q, q->is_start_dma-1);
		}

		finish_request(acthcd, q, status);
	} else
		ACT_HCD_ERR

	/* else{
	//the last data still in fifo, but queue has finish, mostly in OUT data
		if (!ep->is_out) {
			ACT_HCD_ERR
			EP_ERRINFO(acthcd->dev, "%s ep %d, num = %d , pio=%d,error [0x%02X],err_type [0x%02X],IN err,check it ... \n",
					ep->is_out ? "HC OUT":"HC	IN", ep->index,	ep->epnum,ep->is_use_pio,error, (error>>2)&0x7);
		}

#ifdef MULTI_OUT_FIFO
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) == 0	&&
			(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) == (0x01 << 2))
#else
		if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) == 0)
#endif
		{
			ACT_HCD_ERR
			EP_ERRINFO(acthcd->dev, "%s ep %d,  num = %d , pio=%d,error[0x%02X],err_type [0x%02X],should busy now \n",
					ep->is_out ? "HC OUT":"HC	IN", ep->index,	ep->epnum,ep->is_use_pio,error, (error>>2)&0x7);
		}

		//type: RESERVED, NO_HANDSHAKE, PID_ERR, < 10 times (resend)
		if (ep->index >	0 && status != -EPIPE) {
			if (ep->ep_err_cnt < MAX_EP_ERR_CNT)	{
				EP_ERRINFO(acthcd->dev, "%s ep %d,  num = %d , pio=%d,err[0x%02X], err_ type [0x%02X],fifo busy,so rescend it\n",
					ep->is_out ? "HC OUT":"HC	IN", ep->index,	ep->epnum,ep->is_use_pio,error, (error>>2)&0x7);
				ep->ep_err_cnt++;
				usb_writeb(0x1 << 5, ep->reg_hcerr);
				return;
			} else {
				EP_ERRINFO(acthcd->dev,"%d dbg!\n", __LINE__);
				ep->ep_err_cnt = 0;
			}
		}

		//type:STALL, > 10 times  (reset & finish)
		if (ep->index >	0) {
			if (ep->is_out)	{
				aotg_hcep_reset(acthcd,	ep->mask | USB_HCD_OUT_MASK, reset);
			} else {
				if (ep->index >	0) {
					ep_disable(ep);
					__hc_in_stop(acthcd, ep);
				}
				aotg_hcep_reset(acthcd,	ep->mask, reset);
			}
			dev_info(acthcd->dev, "%s ep %d,  num = %d , pio=%d,err [0x%02X],err_type [0x%02X],fifo busy or stall,so reset it...\n",
					ep->is_out ? "HC OUT":"HC IN", ep->index,	ep->epnum,ep->is_use_pio,error, (error>>2)&0x7);

		}
	} */

	return;
}

static void aotg_hcd_error(struct aotg_hcd *acthcd,	u32	irqvector)
{
	struct aotg_hcep *ep = NULL;
	u8 error = 0;

	switch (irqvector) {
	case UIV_HCOUT0ERR:
		usb_writeb(1 <<	0, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->ep0;
		error =	usb_readb(acthcd->base + HCOUT0ERR);
		break;

	case UIV_HCIN0ERR:
		usb_writeb(1 <<	0, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->ep0;
		error =	usb_readb(acthcd->base + HCIN0ERR);
		break;

	case UIV_HCOUT1ERR:
		usb_writeb(1 <<	1, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->outep[1];
		error =	usb_readb(acthcd->base + HCOUT1ERR);
		break;

	case UIV_HCIN1ERR:
		usb_writeb(1 <<	1, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->inep[1];
		error =	usb_readb(acthcd->base + HCIN1ERR);
		break;

	case UIV_HCOUT2ERR:
		usb_writeb(1 <<	2, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->outep[2];
		error =	usb_readb(acthcd->base + HCOUT2ERR);
		break;

	case UIV_HCIN2ERR:
		usb_writeb(1 <<	2, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->inep[2];
		error =	usb_readb(acthcd->base + HCIN2ERR);
		break;

	case UIV_HCOUT3ERR:
		usb_writeb(1 <<	3, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->outep[3];
		error =	usb_readb(acthcd->base + HCOUT3ERR);
		break;

	case UIV_HCIN3ERR:
		usb_writeb(1 <<	3, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->inep[3];
		error =	usb_readb(acthcd->base + HCIN3ERR);
		break;

	case UIV_HCOUT4ERR:
		usb_writeb(1 <<	4, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->outep[4];
		error =	usb_readb(acthcd->base + HCOUT4ERR);
		break;

	case UIV_HCIN4ERR:
		usb_writeb(1 <<	4, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->inep[4];
		error =	usb_readb(acthcd->base + HCIN4ERR);
		break;

	case UIV_HCOUT5ERR:
		usb_writeb(1 <<	5, acthcd->base	+ HCOUT07ERRIRQ);
		ep = acthcd->outep[5];
		error =	usb_readb(acthcd->base + HCOUT5ERR);
		break;

	case UIV_HCIN5ERR:
		usb_writeb(1 <<	5, acthcd->base	+ HCIN07ERRIRQ);
		ep = acthcd->inep[5];
		error =	usb_readb(acthcd->base + HCIN5ERR);
		break;

	default:
		dev_err(acthcd->dev, "no such irq! %s %d\n", __func__, __LINE__);
		WARN_ON(1);
		break;
	}

	aotg_hcd_do_error(acthcd, ep, error);

	tasklet_hi_schedule(&acthcd->urb_tasklet);

	return;
}

static int inline aotg_hcd_handle_dma_pend(struct aotg_hcd *acthcd)
{
	unsigned int dma_pend, dma_cmd;

	dma_pend = (unsigned int)aotg_hcd_dma_is_irq_2(acthcd->dma_nr0);
	if (dma_pend ==	0)
		return 0;

do_dma_pending:
	if (dma_pend & 0x1)	{
		aotg_hcd_dma_clear_pend(acthcd->dma_nr0);
		dma_cmd	= 0x1 &	aotg_hcd_dma_get_cmd(acthcd->dma_nr0);
		if (dma_cmd)
			ACT_HCD_ERR
		else if (acthcd->dma_working[0] == 0)
			ACT_HCD_ERR
		else
			aotg_hcd_dma_handler(acthcd->dma_nr0, acthcd);
	}
	if (dma_pend & 0x2)	{
		aotg_hcd_dma_clear_pend(acthcd->dma_nr1);
		dma_cmd	= 0x1 &	aotg_hcd_dma_get_cmd(acthcd->dma_nr1);
		if (dma_cmd)
			ACT_HCD_ERR
		else if (acthcd->dma_working[1] == 0)
			ACT_HCD_ERR
		else
			aotg_hcd_dma_handler(acthcd->dma_nr1, acthcd);
	}
	dma_pend = (unsigned int)aotg_hcd_dma_is_irq_2(acthcd->dma_nr0);
	if (dma_pend)
		goto do_dma_pending;

	/* deal with some wrong state that dma0 and dma1 is stopped but no dma irq pending */
	dma_cmd	= 0x1 &	aotg_hcd_dma_get_cmd(acthcd->dma_nr0);
	if ((dma_cmd ==	0) && (acthcd->dma_working[0] != 0)) {
		dma_pend = (unsigned int)aotg_hcd_dma_is_irq_2(acthcd->dma_nr0);
		if (dma_pend)
			goto do_dma_pending;
		ACT_HCD_ERR
		aotg_hcd_dma_handler(acthcd->dma_nr0, acthcd);
	}
	dma_cmd	= 0x1 &	aotg_hcd_dma_get_cmd(acthcd->dma_nr1);
	if ((dma_cmd ==	0) && (acthcd->dma_working[1] != 0)) {
		dma_pend = (unsigned int)aotg_hcd_dma_is_irq_2(acthcd->dma_nr1);
		if (dma_pend)
			goto do_dma_pending;
		ACT_HCD_ERR
		aotg_hcd_dma_handler(acthcd->dma_nr1, acthcd);
	}

	dma_pend = (unsigned int)aotg_hcd_dma_is_irq_2(acthcd->dma_nr0);
	if (dma_pend)
		goto do_dma_pending;

	return 0;
}

static inline void handle_usbreset_isr(struct aotg_hcd *acthcd)
{
	struct aotg_queue *q = NULL, *next;
	struct aotg_hcep *ep;

	usb_writeb(USBIRQ_URES,	acthcd->base + USBIRQ);	/* clear usb reset irq */

	if (acthcd->port & (USB_PORT_STAT_POWER	| USB_PORT_STAT_CONNECTION)) {
		acthcd->speed =	USB_SPEED_FULL;	/*FS is	the	default	*/
		acthcd->port |=	(USB_PORT_STAT_C_RESET << 16);
		acthcd->port &=	~USB_PORT_STAT_RESET;

		ep = acthcd->ep0;
		if (ep)	{
			list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
				if (list_empty(&q->finished_list)) {
					q->status = -EIO;
					list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				}
			}
		}

		/* for all eps: fifo & toggle reset */
		aotg_hcep_reset(acthcd, USB_HCD_IN_MASK, ENDPRST_FIFORST | ENDPRST_TOGRST);
		aotg_hcep_reset(acthcd, USB_HCD_OUT_MASK, ENDPRST_FIFORST | ENDPRST_TOGRST);

		acthcd->port |=	USB_PORT_STAT_ENABLE;
		//acthcd->rhstate	= AOTG_RH_ENABLE;

		/* now root hub port has been enabled */
		if (usb_readb(acthcd->base + USBCS)	& USBCS_HFMODE)	{
			acthcd->speed =	USB_SPEED_HIGH;
			acthcd->port |=	USB_PORT_STAT_HIGH_SPEED;
			usb_writeb(USBIRQ_HS, acthcd->base + USBIRQ);
			HCD_DEBUG("%s: USB device is high-speed\n", __func__);
		} else if (usb_readb(acthcd->base + USBCS) & USBCS_LSMODE) {
			acthcd->speed =	USB_SPEED_LOW;
			acthcd->port |=	USB_PORT_STAT_LOW_SPEED;
			HCD_DEBUG("%s: USB device is low-speed\n", __func__);
		} else {
			acthcd->speed =	USB_SPEED_FULL;
			HCD_DEBUG("%s: USB device is full-speed\n", __func__);
		}

		/* usb_clearbitsb(USBIEN_URES,USBIEN); */	/* disable reset irq */
		/* khu del for must enable USBIEN_URES again */

		//usb_clearbitsb(0x3e, acthcd->base	+ OUTXSHORTPCKIEN);
		//usb_writeb(0x3e, acthcd->base	+ OUTXSHORTPCKIRQ);
		usb_writeb(0x3f, acthcd->base +	HCIN07ERRIRQ);
		usb_writeb(0x3f, acthcd->base +	HCOUT07ERRIRQ);
		usb_writeb(0x3f, acthcd->base +	HCIN07IRQ);
		usb_writeb(0x3f, acthcd->base +	HCOUT07IRQ);

		usb_setbitsb(0x3f,	acthcd->base + HCIN07ERRIEN);
		usb_setbitsb(0x3f,	acthcd->base + HCOUT07ERRIEN);

		HCD_DEBUG("%s: USB reset end\n", __func__);
	}

	return;
}

static irqreturn_t aotg_hcd_irq(struct usb_hcd *hcd)
{
	struct platform_device *pdev;
	unsigned int port_no;
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	u8 eirq_mask = usb_readb(acthcd->base +	USBEIEN);
	u8 eirq_pending	= usb_readb(acthcd->base + USBEIRQ);
	u8 otg_state = 0;

	/*
	 * Be careful to use spin lock, because urb->complete() may
	 * enqueue urb again which will result in dead lock.
	 */
	//spin_lock(&acthcd->lock);

	pdev = to_platform_device(hcd->self.controller);
	port_no	= pdev->id & 0xff;

	if (eirq_pending & USBEIRQ_USBIRQ) {
		u32 irqvector = (u32)usb_readb(acthcd->base + IVECT);
		usb_writeb(eirq_mask | USBEIRQ_USBIRQ, acthcd->base + USBEIRQ);
		//printk("irqvector:%d,	%x\n", irqvector, irqvector);

		switch (irqvector) {
		case UIV_OTGIRQ:
			if (usb_readb(acthcd->base + OTGIRQ) & (0x1<<2)) {
				usb_writeb(0x1<<2, acthcd->base	+ OTGIRQ);
				otg_state = usb_readb(acthcd->base + OTGSTATE);
				printk("otg_port_no:%d OTG IRQ,	OTGSTATE: 0x%02X, USBIRQ:0x%02X\n",
					port_no, usb_readb(acthcd->base	+ OTGSTATE),
					usb_readb(acthcd->base + USBIRQ));
				if (otg_state == 0x4)
					return IRQ_HANDLED;

				acthcd->put_aout_msg = 0;
				if (otg_state == AOTG_STATE_A_HOST) {
                   if (hcd_suspend_en) {
                       if (acthcd->phy_resumed /*&& port_no*/) {
                            acthcd->discon_happened = 1;
                            acthcd->phy_resumed = 0;
                            printk("\n=====discon_happened = 1============\n");
                        }
                    }

					if (acthcd->discon_happened == 1)
						mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(500));
					else
						mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(1));
				} else {
					acthcd->discon_happened = 1;
					mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(1));
				}
				//usb_writeb(0x02, acthcd->base	+ USBIRQ);
			} else
				printk("port_no:%d error OTG irq! OTGIRQ: 0x%02X\n",
					port_no, usb_readb(acthcd->base	+ OTGIRQ));

			break;
		case UIV_SOF:
			usb_writeb(1 <<	1, acthcd->base	+ USBIRQ);
			break;
		case UIV_USBRESET:
			handle_usbreset_isr(acthcd);
			break;
		case UIV_EP0IN:
			usb_writeb(1, acthcd->base + HCOUT07IRQ);	/* clear hcep0out irq */
			handle_hcep0_out(acthcd);
			break;
		case UIV_EP0OUT:
			usb_writeb(1, acthcd->base + HCIN07IRQ);	/* clear hcep0in irq */
			handle_hcep0_in(acthcd);
			break;
		case UIV_EP1IN:
			usb_writeb(1<<1, acthcd->base +	HCOUT07IRQ);/* clear hcep1out irq */
			handle_hcep_out(acthcd,	1);
			break;
		case UIV_EP1OUT:
			usb_writeb(1<<1, acthcd->base +	HCIN07IRQ);	/* clear hcep1in irq */
			handle_hcep_in(acthcd, 1);
			break;
		case UIV_EP2IN:
			usb_writeb(1<<2, acthcd->base +	HCOUT07IRQ);/* clear hcep2out irq */
			handle_hcep_out(acthcd,	2);
			break;
		case UIV_EP2OUT:
			usb_writeb(1<<2, acthcd->base +	HCIN07IRQ);	/* clear hcep2in irq */
			handle_hcep_in(acthcd, 2);
			break;
		case UIV_EP3IN:
			usb_writeb(1<<3, acthcd->base +	HCOUT07IRQ);/* clear hcep3out irq */
			handle_hcep_out(acthcd,	3);
			break;
		case UIV_EP3OUT:
			usb_writeb(1<<3, acthcd->base +	HCIN07IRQ);	/* clear hcep3in irq */
			handle_hcep_in(acthcd,	3);
			break;
		case UIV_EP4IN:
			usb_writeb(1<<4, acthcd->base +	HCOUT07IRQ);/* clear hcep4out irq */
			handle_hcep_out(acthcd,	4);
			break;
		case UIV_EP4OUT:
			usb_writeb(1<<4, acthcd->base +	HCIN07IRQ);	/* clear hcep4in irq */
			handle_hcep_in(acthcd, 4);
			break;
		case UIV_EP5IN:
			usb_writeb(1<<5, acthcd->base +	HCOUT07IRQ);/* clear hcep5out irq */
			handle_hcep_out(acthcd,	5);
			break;
		case UIV_EP5OUT:
			usb_writeb(1<<5, acthcd->base +	HCIN07IRQ);	/* clear hcep5in irq */
			handle_hcep_in(acthcd, 5);
			break;
		case UIV_HCOUT0ERR:
		case UIV_HCIN0ERR:
		case UIV_HCOUT1ERR:
		case UIV_HCIN1ERR:
		case UIV_HCOUT2ERR:
		case UIV_HCIN2ERR:
		case UIV_HCOUT3ERR:
		case UIV_HCIN3ERR:
		case UIV_HCOUT4ERR:
		case UIV_HCIN4ERR:
		case UIV_HCOUT5ERR:
		case UIV_HCIN5ERR:
//			ACT_HCD_DBG
			aotg_hcd_error(acthcd, irqvector);
			break;
		default:
			dev_err(acthcd->dev, "error	interrupt, pls check it! irqvector:	0x%02X\n", (u8)irqvector);
			//spin_unlock(&acthcd->lock);
			return IRQ_NONE;
		}
	}

	if (acthcd->use_dma)
		aotg_hcd_handle_dma_pend(acthcd);

	//spin_unlock(&acthcd->lock);
	return IRQ_HANDLED;
}

static void aotg_hcd_hotplug_timer(unsigned long data)
{
	struct aotg_hcd	*acthcd	= (struct aotg_hcd *)data;
	struct usb_hcd *hcd	= aotg_to_hcd(acthcd);
	struct platform_device *pdev;
	unsigned int port_no;
	unsigned long flags;
	int	connect_changed = 0;

	if (unlikely(IS_ERR_OR_NULL((void *)data))) {
		ACT_HCD_DBG
		return;
	}
	if (acthcd->hcd_exiting	!= 0) {
		ACT_HCD_DBG
		return;
	}

	//disable_irq_nosync(acthcd->uhc_irq);
	disable_irq(acthcd->uhc_irq);
	spin_lock_irqsave(&acthcd->lock, flags);

	if (acthcd->put_aout_msg != 0) {
		pdev = to_platform_device(hcd->self.controller);
		port_no	= pdev->id & 0xff;

		if (port_no == 0) {
			/* if idpin == 1, send A_OUT message quickly. */
			if (usb_readl(acthcd->usbecs) & USB2_ECS_ID_PIN_BIT) {
				ACT_HCD_DBG
				aotg_send_line_out_msg(acthcd);
				acthcd->put_aout_msg = 0;
			} else {
				acthcd->put_aout_msg++;
				if (acthcd->put_aout_msg > 22) {
					ACT_HCD_DBG
					aotg_send_line_out_msg(acthcd);
					acthcd->put_aout_msg = 0;
				} else
					mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(1000));
			}
		} else {
			ACT_HCD_DBG
			acthcd->put_aout_msg = 0;
		}
		spin_unlock_irqrestore(&acthcd->lock, flags);
		enable_irq(acthcd->uhc_irq);
		return;
	}

	if ((usb_readb(acthcd->base + OTGSTATE) == AOTG_STATE_A_HOST) && (acthcd->discon_happened == 0)) {
		if (!acthcd->inserted) {
			acthcd->port |=	(USB_PORT_STAT_C_CONNECTION	<< 16);
			/* indicate the device is present */
			acthcd->port |=	USB_PORT_STAT_CONNECTION;
			//acthcd->rhstate	= AOTG_RH_ATTACHED;
			acthcd->inserted = 1;
			connect_changed	= 1;
		}
	} else {
		if (acthcd->inserted) {
			acthcd->port &=	~(USB_PORT_STAT_CONNECTION |
							USB_PORT_STAT_ENABLE |
							USB_PORT_STAT_LOW_SPEED |
							USB_PORT_STAT_HIGH_SPEED |
							USB_PORT_STAT_SUSPEND);
			acthcd->port |=	(USB_PORT_STAT_C_CONNECTION	<< 16);
			//acthcd->rhstate	= AOTG_RH_NOATTACHED;
			acthcd->inserted = 0;
			connect_changed	= 1;
		}
		if (acthcd->discon_happened == 1) {
			acthcd->discon_happened = 0;
			if (usb_readb(acthcd->base + OTGSTATE) == AOTG_STATE_A_HOST)
				mod_timer(&acthcd->hotplug_timer, jiffies + msecs_to_jiffies(300));
		}
	}

	dev_info(acthcd->dev, "%s changed: %d, inserted: %d\n",
		dev_name(hcd->self.controller),	connect_changed, acthcd->inserted);

	if (connect_changed) {
		if (HC_IS_SUSPENDED(hcd->state))
			usb_hcd_resume_root_hub(hcd);
		pr_info("port status change\n");
		usb_hcd_poll_rh_status(hcd);
	}

	if ((acthcd->inserted == 0) && (connect_changed	== 1) &&
	    (usb_readb(acthcd->base + OTGSTATE) != AOTG_STATE_A_HOST)) {
		acthcd->put_aout_msg = 1;
		mod_timer(&acthcd->hotplug_timer, jiffies +	msecs_to_jiffies(6000));
	}
	acthcd->suspend_request_pend = 0;

	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);

	return;
}

static enum hrtimer_restart aotg_hcd_trans_wait_timer(struct hrtimer *hrtimer)
{
	u8 error;
	char ht_flag = 0;
	int i;
	struct aotg_hcep *ep;
	struct aotg_hcd *acthcd;
	struct aotg_queue *q;
	unsigned long flags;
	unsigned int ht_s;
	unsigned int ht_ns;

	if (unlikely(IS_ERR_OR_NULL((void *)hrtimer))) {
		ACT_HCD_DBG
		return HRTIMER_NORESTART;
	}
	acthcd = container_of(hrtimer, struct aotg_hcd, trans_wait_timer);
	if (acthcd->hcd_exiting	!= 0) {
		ACT_HCD_DBG
		return HRTIMER_NORESTART;
	}

	//disable_irq_nosync(acthcd->uhc_irq);
	disable_irq(acthcd->uhc_irq);
	spin_lock_irqsave(&acthcd->lock, flags);

	for (i = 1; i < MAX_EP_NUM; i++) {
		//dma transfer finish, check the fifo
		if (acthcd->bulkout_wait_ep[i] != NULL) {
			ep = acthcd->bulkout_wait_ep[i];
#ifdef MULTI_OUT_FIFO
			if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) == 0 &&
				(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) == (0x01 << 2))
#else
			if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) ==	0)
#endif
			{
				acthcd->bulkout_wait_ep[i] = NULL;
				ep->fifo_timeout = 0;
			} else if (time_after(jiffies, ep->fifo_timeout)) {
				ht_flag = 1;
				error = usb_readb(ep->reg_hcerr);
				ep->fifo_timeout =  jiffies + FIFO_BUSY_MS*HZ/1000;
				ep->fifo_busy_cnt ++;
#ifdef EP_TIMEOUT_DISCONNECT
				if (((error & 0x3) == 0x3) && (ep->fifo_busy_cnt < MAX_FIFO_BUSY)) {
					ACT_HCD_DBG
					aotg_hcd_bulk_timeout_disconnect(acthcd, ep, error);

				} else {
					if (ep->fifo_busy_cnt > MAX_FIFO_BUSY) {
						ep->fifo_busy_cnt = 0;
						aotg_hcep_reset(acthcd,	ep->mask | USB_HCD_OUT_MASK, ENDPRST_FIFORST);

						ep->ep_reset_cnt++;
						DBG_TIMEOUT("ep_reset_cnt:%d\n", ep->ep_reset_cnt);
						if (ep->ep_reset_cnt > MAX_EP_RESET) {
							ep->ep_reset_cnt = 0;
							aotg_hcd_do_disconnect(acthcd,  ep);

						}

					}
				}
#else
				if (ep->fifo_busy_cnt > MAX_FIFO_BUSY) {
					ACT_HCD_DBG
					ep->fifo_busy_cnt = 0;
					aotg_hcep_reset(acthcd,	ep->mask | USB_HCD_OUT_MASK, ENDPRST_FIFORST);
				}
#endif
			} else
				ht_flag = 1;
		}

		if (acthcd->bulkout_wait_dma[i] != NULL) {
			ep = acthcd->bulkout_wait_dma[i];
			if (ep->q != NULL) {
				if (time_after(jiffies, ep->q->timeout)) {
					spin_unlock_irqrestore(&acthcd->lock, flags);
					q = ep->q;
					if (ep->is_use_pio == 0) {
						ep->dma_timeout_cnt++;
						printk("dma_wait:ep->index:%x ep->mask:%x, finish this q & reset ep\n", ep->index, ep->mask);
						printk("h_cnt:%u, i_cnt:%u\n", ep->dma_handler_cnt, ep->dma_outirq_cnt);
						printk("act_l:%u, trans_l:%u\n", q->urb->actual_length, q->urb->transfer_buffer_length);
						printk("ep:%d, cs_reg:0x%X\n", ep->index, usb_readb(ep->reg_hcepcs));
#ifdef DMA_PIO_MSG
						printk("DEBUG MSG:\n");
						aotg_hcd_show_single_queue_info(acthcd,  ep);
						aotg_hcd_show_single_ep_reg(acthcd, ep);
#endif
					}
					acthcd->bulkout_wait_dma[ep->index] = NULL;
					finish_request(acthcd, ep->q, -EREMOTEIO);
					aotg_hcep_reset(acthcd,	ep->mask | USB_HCD_OUT_MASK, ENDPRST_FIFORST);
					//ep->q->timeout = jiffies + 5*HZ;
					spin_lock_irqsave(&acthcd->lock, flags);
				}
			}
		}
	}

	if (ht_flag) {
		ht_s = 0;
		ht_ns = 2 * 1000 * 1000;
	} else {
		ht_s = 0;
		ht_ns = 500 * 1000 * 1000;
	}
	hrtimer_start(&acthcd->trans_wait_timer, ktime_set(ht_s, ht_ns),
		HRTIMER_MODE_REL);
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);
	tasklet_hi_schedule(&acthcd->urb_tasklet);

	return HRTIMER_NORESTART;
}

static inline int start_transfer(struct aotg_hcd *acthcd, struct aotg_queue *q,
				struct aotg_hcep *ep)
{
	struct urb *urb	= q->urb;
	int	retval = 0;

	ep->urb_enque_cnt++;
	ep->ep_err_cnt = 0;
	ep->fifo_busy_cnt = 0;

	list_add_tail(&q->ep_q_list, &ep->q_list);

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		q->timeout = jiffies + HZ;
		retval = handle_setup_packet(acthcd, q);
		break;

	case PIPE_BULK:
		q->timeout = jiffies + DMA_TIEMOUT_S * HZ;
		aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);

		if (usb_pipeout(urb->pipe))
			handle_bulkout_packet(acthcd, q);
		else
			handle_bulkin_packet(acthcd, q);
		break;

	case PIPE_INTERRUPT:
		retval = handle_intr_packet(acthcd,	q);
		break;

	case PIPE_ISOCHRONOUS:
		handle_iso_packet(acthcd, q);
		break;

	default:
		retval = -ENODEV;
		break;
	}

	return retval;
}
static void intr_check_ep_timer(unsigned long data)
{
	struct aotg_hcd	*acthcd	= (struct aotg_hcd *)data;
	struct aotg_hcep *ep;
	int i;

	for	(i = 1;	i <	MAX_EP_NUM;	i++) {
		if (acthcd->inep[i] != NULL) {
			ep = acthcd->inep[i];
			if (ep->type == PIPE_INTERRUPT) {
				ep->inc_intval++;
				if (( ep->inc_intval > 10) && (!list_empty(&ep->q_list))) {
					pr_info("ep%d has stop\n", i);
					hrtimer_start(&ep->intr_timer,
						ktime_set(ep->interval_s, ep->interval_ns),
						HRTIMER_MODE_REL);
				}
			}
		}
	}
	mod_timer(&acthcd->intr_check_timer, jiffies +msecs_to_jiffies(100));
}

static enum hrtimer_restart intr_timeout_handler(struct hrtimer *hrtimer)
{
	struct aotg_hcep *ep = container_of(hrtimer, struct aotg_hcep, intr_timer);

	tasklet_schedule(&ep->intr_tasklet);

	return HRTIMER_NORESTART;
}

static void intr_tasklet_func(unsigned long data)
{
	struct aotg_hcep *ep = (struct aotg_hcep *)data;
	struct aotg_hcd	*acthcd	= ep->dev;
	struct aotg_queue *q = NULL;
	struct urb *urb	= NULL;
	int	pipe;
	unsigned long flags;
	u16	w_packets;
	u8 fifo_ctrl;

	spin_lock_irqsave(&acthcd->lock, flags);
	if (!list_empty(&ep->q_list)) {
		q =	list_first_entry(&ep->q_list, struct aotg_queue, ep_q_list);
		urb	= q->urb;
		pipe = urb->pipe;

		if (usb_pipeint(pipe)) {
		    if (((hcd_suspend_en == 0) && usb_pipein(pipe)) || (hcd_suspend_en == 1)) {
				w_packets =	urb->transfer_buffer_length	/ MAX_PACKET(ep->maxpacket);
				if (urb->transfer_buffer_length	& (MAX_PACKET(ep->maxpacket) - 1))
					w_packets++;
				//reset	fifo, skip the undo	data
				aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
				ep_enable(ep);
				pio_irq_clear(acthcd, ep->mask);
				pio_irq_enable(acthcd, ep->mask);
				hcerr_irq_clear(acthcd, ep->mask);
				ep->inc_intval = 0;
				hcerr_irq_enable(acthcd, ep->mask);

				//use cpu, not set auto fifo bit
				ep->is_use_pio = 1;
				fifo_ctrl = (0<<5) | (0 <<4) | ep->index;
				usb_writeb(fifo_ctrl, acthcd->base + FIFOCTRL);
				if (hcd_suspend_en == 0)
					__hc_in_start(acthcd, ep, w_packets);
				else {
					if (usb_pipein(pipe))
						__hc_in_start(acthcd, ep, w_packets);
					else
						handle_hcep_pio(acthcd,	ep,	q);
				}
			}
		} else
			dev_err(acthcd->dev, "<Intr>xfers wrong	type, ignore it, %s()\n", __func__);

	}
	spin_unlock_irqrestore(&acthcd->lock, flags);
	return;
}

static struct aotg_hcep *aotg_hcep_alloc(struct aotg_hcd *acthcd,
					struct urb *urb)
{
	struct aotg_hcep *ep = NULL;
	int	pipe = urb->pipe;
	int	is_out = usb_pipeout(pipe);
	int	type = usb_pipetype(pipe);
	int	retval = 0;

	ep = kzalloc(sizeof(*ep), GFP_ATOMIC);
	if (NULL ==	ep)	{
		dev_err(acthcd->dev, "alloc	ep failed\n");
		retval = -ENOMEM;
		goto exit;
	}

	ep->hep	= urb->ep;
	ep->udev = usb_get_dev(urb->dev);
	ep->dev	= acthcd;
	INIT_LIST_HEAD(&ep->q_list);
	ep->maxpacket =	usb_maxpacket(ep->udev,	pipe, is_out);
	ep->epnum =	usb_pipeendpoint(pipe);
	ep->length = 0;
	ep->type = type;
	ep->is_out = is_out;
	ep->urb_enque_cnt =	0;
	ep->urb_endque_cnt = 0;
	ep->inc_intval = 0;
	ep->urb_stop_stran_cnt = 0;
	ep->urb_unlinked_cnt = 0;
	ep->intr_recall_cnt = 0;
	ep->ep_err_cnt = 0;
	ep->dma_handler_cnt = 0;
	ep->dma_outirq_cnt = 0;
	ep->dma_timeout_cnt = 0;
	ep->iso_start = 0;

	ep->fifo_busy_cnt = 0;
#ifdef EP_TIMEOUT_DISCONNECT
	ep->ep_timeout_cnt = 0;
	ep->ep_reset_cnt = 0;
#endif

	ep->q = NULL;
	EP_CFGINFO(acthcd->dev, "ep->epnum: %d, ep->maxpacket: %d, ep->type : %d\n",
		ep->epnum, ep->maxpacket, ep->type);
	ep->bulkout_zero_cnt = 0;
	usb_settoggle(urb->dev,	usb_pipeendpoint(urb->pipe), is_out, 0);

	switch (type) {
	case PIPE_CONTROL:
		acthcd->ep0	= ep;
		ep->index =	0;
		ep->mask = 0;
		usb_writeb(usb_pipedevice(urb->pipe), acthcd->base + FNADDR);
		EP_CFGINFO(acthcd->dev, "device addr: 0x%08x\n",
			usb_readb(acthcd->base + FNADDR));
		usb_writeb(ep->epnum, acthcd->base + HCEP0CTRL);
		usb_writeb((u8)ep->maxpacket, acthcd->base + HCIN0MAXPCK);
		usb_writeb((u8)ep->maxpacket, acthcd->base + HCOUT0MAXPCK);

		usb_setbitsb(1,	acthcd->base + HCOUT07IEN);
		usb_setbitsb(1,	acthcd->base + HCIN07IEN);
		usb_writeb(1, acthcd->base + HCOUT07IRQ);
		usb_writeb(1, acthcd->base + HCIN07IRQ);
		//usb_setbitsb(1, acthcd->base + HCIN07ERRIEN);
		//usb_setbitsb(1, acthcd->base + HCOUT07ERRIEN);
		usb_settoggle(urb->dev,	usb_pipeendpoint(urb->pipe), 1,	0);
		usb_settoggle(urb->dev,	usb_pipeendpoint(urb->pipe), 0,	0);
		acthcd->setup_processing = 0;
		break;

	case PIPE_BULK:
		if (is_out)
#ifdef MULTI_OUT_FIFO
			retval = aotg_hcep_config(acthcd, ep, EPCON_TYPE_BULK,
							EPCON_BUF_DOUBLE, is_out);
#else
			retval = aotg_hcep_config(acthcd, ep, EPCON_TYPE_BULK,
							EPCON_BUF_SINGLE, is_out);
#endif
		else
			retval = aotg_hcep_config(acthcd, ep, EPCON_TYPE_BULK,
							EPCON_BUF_SINGLE, is_out);

		if (retval < 0)	{
			dev_err(acthcd->dev, "PIPE_BULK, retval: %d\n",	retval);
			goto free_ep;
		}
		break;

	case PIPE_INTERRUPT:
		retval = aotg_hcep_config(acthcd, ep, EPCON_TYPE_INT,
							EPCON_BUF_SINGLE, is_out);
		if (retval < 0)	{
			dev_err(acthcd->dev, "PIPE_INTERRUPT, retval: %d\n", retval);
			goto free_ep;
		}
//		INIT_DELAYED_WORK(&ep->dwork, perioidc_work_func);
		ep->load = NS_TO_US(usb_calc_bus_time(ep->udev->speed, !is_out,
						(type == PIPE_ISOCHRONOUS),	ep->maxpacket));

		switch (ep->udev->speed) {
		case USB_SPEED_HIGH:
			ep->period = urb->interval >> 3;
			break;
		case USB_SPEED_FULL:
			ep->period = urb->interval;
			break;
		case USB_SPEED_LOW:
			ep->period = urb->interval;
			break;
		default:
			dev_err(acthcd->dev, "error	speed, speed: %d, %s, %d\n",
				ep->udev->speed, __func__, __LINE__);
			goto free_ep;
		}

		if (ep->period == 0) {
			ACT_HCD_DBG
			ep->period = 1;
		}

		tasklet_init(&ep->intr_tasklet, intr_tasklet_func, (unsigned long)ep);
		hrtimer_init(&ep->intr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ep->intr_timer.function = intr_timeout_handler;
		ep->interval_s = ep->period / 1000;
		ep->interval_ns = (ep->period % 1000) * 1000000 - 400*1000;

		printk("urb->interval: %d\n", urb->interval);
		break;
	case PIPE_ISOCHRONOUS:
		retval = aotg_hcep_config(acthcd, ep, EPCON_TYPE_ISO,
							EPCON_BUF_SINGLE, is_out);
		if (retval < 0)	{
			dev_err(acthcd->dev, "PIPE_INTERRUPT, retval: %d\n", retval);
			goto free_ep;
		}
		ep->load = NS_TO_US(usb_calc_bus_time(ep->udev->speed,
			!is_out, (type == PIPE_ISOCHRONOUS), ep->maxpacket));

		switch (ep->udev->speed) {
		case USB_SPEED_HIGH:
			ep->period = urb->interval >> 3;
			break;
		case USB_SPEED_FULL:
			ep->period = urb->interval;
			break;
		case USB_SPEED_LOW:
			ep->period = urb->interval;
			break;
		default:
			dev_err(acthcd->dev, "error	speed, speed: %d, %s, %d\n",
				ep->udev->speed, __func__, __LINE__);
			goto free_ep;
		}

		if (ep->period == 0) {
			ACT_HCD_DBG
			ep->period = 1;
		}
		break;
	default:
		dev_err(acthcd->dev, "not support type,	type: %d\n", type);
		goto free_ep;
	}

	return ep;

free_ep:
	usb_put_dev(urb->dev);
	kfree(ep);
exit:
	return NULL;
}

static int aotg_hcd_urb_enqueue(struct usb_hcd *hcd,
				struct urb *urb, unsigned mem_flags)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	struct aotg_hcep *ep = NULL;
	struct aotg_queue *q;
	unsigned long flags;
	int	pipe = urb->pipe;
	int	type = usb_pipetype(pipe);
	//int is_out = !usb_pipein(pipe);
	//int epnum	= usb_pipeendpoint(pipe);
	int	retval = 0;

	if (type ==	PIPE_ISOCHRONOUS) {
		//dev_err(acthcd->dev, "<QUEUE> not support type: %d\n", type);
		// TODO: support number_of_packets
		if (urb->number_of_packets != 1)
			printk("numb: %d\n", urb->number_of_packets);
		//return -ENOSPC;
	}
	if (acthcd->hcd_exiting	!= 0)
		//dev_err(acthcd->dev, "aotg hcd exiting!	type:%d\n",	type);
		return -EIO;

	/* avoid call disable_irq in irq context */
	if (in_irq())
		disable_irq_nosync(acthcd->uhc_irq);
	else
		disable_irq(acthcd->uhc_irq);

	spin_lock_irqsave(&acthcd->lock, flags);

	q =	aotg_hcd_get_queue(acthcd, mem_flags);
	if (unlikely(!q)) {
		dev_err(acthcd->dev, "<QUEUE> alloc dma queue failed\n");
		spin_unlock_irqrestore(&acthcd->lock, flags);
		enable_irq(acthcd->uhc_irq);
		return -ENOMEM;
	}

	if (urb->num_sgs > 0) {
		ACT_HCD_DBG
		q->cur_sg =	urb->sg;
	} else
		q->cur_sg =	NULL;

	if (!(acthcd->port & USB_PORT_STAT_ENABLE) ||
		!HC_IS_RUNNING(hcd->state)) {
		dev_err(acthcd->dev, "<QUEUE> port is dead or disable\n");
		retval = -ENODEV;
		goto exit;
	}

#ifdef CONFIG_USB_LEGACY_AOTG_HUB_SUPPORT
	/*
	 * switch to aotg_hcd_hub controller which supports usb hub.
	 *
	 * Scheme: switch only when device(hub or normal device)
	 * connected to usb hub(except root hub).
	 * 1. I'm not root hub;
	 * 2. My father is not root hub;
	 * ==> I'm level 2 device.
	 */
	if (urb->dev->parent &&
		(urb->dev->parent != hcd->self.root_hub) &&
		(acthcd->id == 0)) {
		//if ((urb->dev->parent->parent) &&
			//(urb->dev->parent != hcd->self.root_hub)) {
		ACT_HCD_DBG
		printk("find usb hub!\n");
		retval = -ENODEV;
		aotg_hub_notify_enter(1);
		goto exit;
	}
#endif

	if (likely(urb->ep->hcpriv))
		ep = (struct aotg_hcep *)urb->ep->hcpriv;
	else {
		ep = aotg_hcep_alloc(acthcd, urb);
		if (NULL ==	ep)	{
			dev_err(acthcd->dev, "<QUEUE> alloc	ep failed\n");
			retval = -ENOMEM;
			goto exit;
		}
		urb->ep->hcpriv	= ep;
	}

	urb->hcpriv	= hcd;
	q->ep =	ep;
	q->urb = urb;

	if ((usb_pipeout(urb->pipe)) &&
		(urb->transfer_buffer_length % ep->maxpacket ==	0))	{
		q->need_zero = urb->transfer_flags & URB_ZERO_PACKET;
	}

	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval)
		goto exit;

	list_add_tail(&q->enqueue_list,	&acthcd->hcd_enqueue_list);
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);
	tasklet_hi_schedule(&acthcd->urb_tasklet);
	return retval;
exit:
	if (unlikely(retval	< 0)) {
		if (!list_empty(&q->ep_q_list))
			list_del(&q->ep_q_list);
		if (!list_empty(&q->dma_q_list))
			list_del_init(&q->dma_q_list);
		aotg_hcd_release_queue(acthcd, q);
	}
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);
	return retval;
}

void urb_tasklet_func(unsigned long data)
{
	struct aotg_hcd *acthcd = (struct aotg_hcd *)data;
	struct aotg_queue *q, *next;
	struct aotg_hcep *ep;
	struct urb *urb;
	struct usb_hcd *hcd;
	unsigned long flags;
	int status;
	int cnt = 0;
	int delay_cnt = 0;
	unsigned int ht_s;
	unsigned int ht_ns;

	//spin_lock(&acthcd->tasklet_lock);

	do {
		status = (int)spin_is_locked(&acthcd->tasklet_lock);
		if (status)	{
			acthcd->tasklet_retry =	1;
			printk("locked,	urb	retry later!\n");
			return;
		}
		cnt++;
		/* sometimes tasklet_lock is unlocked, but spin_trylock	still will be failed,
		 * maybe caused	by the instruction of strexeq in spin_trylock, it will return failed
		 * if other	cpu	is accessing the nearby	address	of &acthcd->tasklet_lock.
		 */
		status = spin_trylock(&acthcd->tasklet_lock);
		if ((!status) && (cnt >	10))  {
			acthcd->tasklet_retry =	1;
			printk("urb	retry later!\n");
			return;
		}
	} while	(status	== 0);

	//disable_irq_nosync(acthcd->uhc_irq);
	disable_irq(acthcd->uhc_irq);
	spin_lock_irqsave(&acthcd->lock, flags);

	/* do dequeue task.	*/
DO_DEQUEUE_TASK:
	urb	= NULL;
	list_for_each_entry_safe(q,	next, &acthcd->hcd_dequeue_list, dequeue_list) {
		if (q->status <	0) {
			urb	= q->urb;
			ep = q->ep;
			if (ep)
				ep->urb_unlinked_cnt++;
			if (q->dequeue_list.next == LIST_POISON1) {
				ACT_HCD_ERR
				urb = NULL;
				continue;
			}
			list_del(&q->dequeue_list);
			status = q->status;
			aotg_hcd_release_queue(acthcd, q);
			hcd	= bus_to_hcd(urb->dev->bus);
			break;
		} else
			ACT_HCD_ERR
	}
	if (urb	!= NULL) {
		usb_hcd_unlink_urb_from_ep(hcd,	urb);

		spin_unlock_irqrestore(&acthcd->lock, flags);

		/* in usb_hcd_giveback_urb,	complete function may call new urb_enqueue.	*/
		usb_hcd_giveback_urb(hcd, urb, status);

		spin_lock_irqsave(&acthcd->lock, flags);
		goto DO_DEQUEUE_TASK;
	}

	/* do finished task. */
DO_FINISH_TASK:
	urb	= NULL;
	list_for_each_entry_safe(q,	next, &acthcd->hcd_finished_list, finished_list) {
		if (q->finished_list.next == LIST_POISON1) {
			ACT_HCD_ERR
			urb = NULL;
			continue;
		}
		list_del(&q->finished_list);
		status = q->status;
		tasklet_finish_request(acthcd, q, status);

		hcd	= aotg_to_hcd(acthcd);
		urb	= q->urb;
		ep = q->ep;
		//tasklet_finish_request(acthcd, q, status);
		if (urb	!= NULL)
			break;
	}
	if (urb	!= NULL) {
		usb_hcd_unlink_urb_from_ep(hcd,	urb);

		spin_unlock_irqrestore(&acthcd->lock, flags);

		/* in usb_hcd_giveback_urb,	complete function may call new urb_enqueue.	*/
		usb_hcd_giveback_urb(hcd, urb, status);

		spin_lock_irqsave(&acthcd->lock, flags);
		goto DO_FINISH_TASK;
	}

//DO_ENQUEUE_TASK:
	/* do enqueue task.	*/
	/* start transfer directly,	don't care setup appearing in bulkout. */
	list_for_each_entry_safe(q,	next, &acthcd->hcd_enqueue_list, enqueue_list) {
		urb	= q->urb;
		ep = q->ep;

		/* deal	with controll request. */
		if (usb_pipetype(urb->pipe)	== PIPE_CONTROL) {
			if (acthcd->setup_processing !=	0) {
				if (list_empty(&ep->q_list)) {
					ACT_HCD_ERR
					acthcd->setup_processing = 0;
					goto BEGIN_START_TANSFER;
				}
				//ACT_HCD_DBG
				continue;
			} else {
				if (!list_empty(&ep->q_list)) {
					ACT_HCD_ERR
					continue;
				}
				goto BEGIN_START_TANSFER;
			}
		}

		/* deal	with new bulk in request. */
		if ((usb_pipetype(urb->pipe) ==	PIPE_BULK) && (usb_pipein(urb->pipe))) {
			if (list_empty(&ep->q_list))
				goto BEGIN_START_TANSFER;
			continue;
		}

		/* deal	with bulk out request. */
		if ((usb_pipetype(urb->pipe) ==	PIPE_BULK) && (usb_pipeout(urb->pipe)))	{

			if (!list_empty(&ep->q_list))
				continue;

			if (acthcd->bulkout_wait_ep[ep->index] != NULL) {
				delay_cnt = DELAY_BUSY_US;
#ifdef MULTI_OUT_FIFO
				while(((EPCS_NPAK & usb_readb(ep->reg_hcepcs)) !=
					(0x01 << 2)) && delay_cnt--)
#else
				while(((usb_readb(ep->reg_hcepcs) & EPCS_BUSY) != 0) &&
					delay_cnt--)
#endif
					udelay(1);

				if (!delay_cnt) {
					ht_s = 0;
					ht_ns = BUSY_TIMER_US * 1000;
					hrtimer_start(&acthcd->trans_wait_timer,
						ktime_set(ht_s, ht_ns), HRTIMER_MODE_REL);
					continue;
				}

				goto BEGIN_START_TANSFER;
			}
#ifdef MULTI_OUT_FIFO
			if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0 ||
				(EPCS_NPAK & usb_readb(ep->reg_hcepcs)) != (0x01 << 2))
#else
			if ((EPCS_BUSY & usb_readb(ep->reg_hcepcs)) != 0)
#endif
			{
				acthcd->bulkout_wait_ep[ep->index] = ep;
				ep->busy_jiffies = jiffies;
				ep->fifo_timeout = ep->busy_jiffies + FIFO_BUSY_MS * HZ / 1000;
				ht_s = 0;
				ht_ns = BUSY_TIMER_US * 1000;
				hrtimer_start(&acthcd->trans_wait_timer,
					ktime_set(ht_s, ht_ns), HRTIMER_MODE_REL);

				continue;
			}

			acthcd->bulkout_wait_ep[ep->index] = NULL;
			acthcd->bulkout_wait_dma[ep->index] = ep;
			ep->fifo_timeout = 0;

			goto BEGIN_START_TANSFER;
		}

BEGIN_START_TANSFER:
		list_del(&q->enqueue_list);
		aotg_dbg_put_q(q, usb_pipeendpoint(q->urb->pipe),
			usb_pipein(q->urb->pipe), q->urb->transfer_buffer_length);

		status = start_transfer(acthcd,	q, ep);

		if (unlikely(status	< 0)) {
			ACT_HCD_ERR
			hcd	= bus_to_hcd(urb->dev->bus);
			aotg_hcd_release_queue(acthcd, q);

			usb_hcd_unlink_urb_from_ep(hcd,	urb);
			spin_unlock_irqrestore(&acthcd->lock, flags);
			usb_hcd_giveback_urb(hcd, urb, status);
			spin_lock_irqsave(&acthcd->lock, flags);
		}
		//spin_unlock_irqrestore(&acthcd->lock,	flags);
		//enable_irq(acthcd->uhc_irq);
		//spin_unlock(&acthcd->tasklet_lock);
		//return;
	}

	if (acthcd->tasklet_retry != 0)	{
		acthcd->tasklet_retry =	0;
		goto DO_DEQUEUE_TASK;
	}
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);
	spin_unlock(&acthcd->tasklet_lock);
	return;
}

static int aotg_hcd_urb_dequeue(struct usb_hcd *hcd,
				struct urb *urb, int status)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	struct aotg_hcep *ep;
	struct aotg_queue *q = NULL, *next,	*tmp;
	unsigned long flags;
	int	retval = 0;

	if (in_irq())
		disable_irq_nosync(acthcd->uhc_irq);
	else
		disable_irq(acthcd->uhc_irq);

	spin_lock_irqsave(&acthcd->lock, flags);

	ep = (struct aotg_hcep *)urb->ep->hcpriv;

	retval = usb_hcd_check_unlink_urb(hcd, urb,	status);
	if (retval)	{
		dev_err(acthcd->dev, "can't unlink urb to ep, retval: %d\n", retval);
		goto dequeue_fail;
	}

	if (acthcd->hcd_exiting != 0 ) {
		ACT_HCD_ERR
		goto dequeue_fail;
	}
	retval = -EINVAL;

	if (ep && !list_empty(&ep->q_list))	{
		retval = 0;

		list_for_each_entry_safe(tmp, next,	&ep->q_list, ep_q_list)	{
			if (tmp->urb ==	urb) {
				if (!list_empty(&tmp->dma_q_list))

					list_del_init(&tmp->dma_q_list);

				/* maybe finished in tasklet_finish_request. */
				if (!list_empty(&tmp->finished_list)) {
					//ACT_HCD_DBG
					if (tmp->finished_list.next	!= LIST_POISON1)
						list_del(&tmp->finished_list);
				}
				if (tmp->ep_q_list.next	== LIST_POISON1) {
					ACT_HCD_ERR
					goto dequeue_fail;
				}
				list_del(&tmp->ep_q_list);
				q =	tmp;
				break;
			}
		}

		if (likely(q)) {
			if (q->is_xfer_start ||	q->is_start_dma) {
				ep->urb_stop_stran_cnt++;

				if (q->is_xfer_start) {
					//ACT_HCD_DBG
					if (ep->index >	0) {
						if (usb_pipein(urb->pipe)) {
							//ACT_HCD_DBG
							__hc_in_stop(acthcd, ep);
							ep_disable(ep);
						}
						aotg_hcep_reset(acthcd,	ep->mask, ENDPRST_FIFORST);
						pio_irq_clear(acthcd, ep->mask);
					}
				}
				if (q->is_start_dma) {
					__clear_dma(acthcd,	q, q->is_start_dma-1);
				}
				if (usb_pipeint(q->urb->pipe) && q->is_xfer_start) {
					ACT_HCD_DBG
					hcerr_irq_clear(acthcd, ep->mask);
					hcerr_irq_disable(acthcd, ep->mask);
					acthcd->sof_kref--;
					hcd->self.bandwidth_int_reqs--;
					if (acthcd->sof_kref <=	0)
						aotg_sofirq_off(acthcd);
				}

				if (usb_pipetype(urb->pipe)	== PIPE_CONTROL)
					acthcd->setup_processing = 0;
				q->is_xfer_start = 0;
				/* processed in	__clear_dma. */
				//q->is_start_dma =	0;
				//dma[0] = 0; dma[1] = 0;
			}
			goto dequeued;
		}
	}

	list_for_each_entry_safe(tmp, next,	&acthcd->hcd_enqueue_list, enqueue_list) {
		if (tmp->urb ==	urb) {
			list_del(&tmp->enqueue_list);
			q =	tmp;
			//ACT_HCD_DBG
			/* there maybe some err if setup_processing != 0 */
			if (usb_pipetype(urb->pipe)	== PIPE_CONTROL)
				if (acthcd->setup_processing !=	0)
					//ACT_HCD_DBG
					acthcd->setup_processing = 0;
			break;
		}
	}
dequeued:
	ep->iso_start = 0;
	if (likely(q)) {
		q->status =	status;
		if (!list_empty(&q->dequeue_list))
			ACT_HCD_ERR
		else
			list_add_tail(&q->dequeue_list,	&acthcd->hcd_dequeue_list);

		spin_unlock_irqrestore(&acthcd->lock, flags);
		enable_irq(acthcd->uhc_irq);
		tasklet_schedule(&acthcd->urb_tasklet);
		return retval;
	}

dequeue_fail:
	ACT_HCD_ERR
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);
	return retval;
}

static void aotg_hcd_endpoint_disable(struct usb_hcd *hcd,
				struct usb_host_endpoint *hep)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);
	struct aotg_hcep *ep = hep->hcpriv;
	unsigned long flags;
	int	index;
	int	i;

	if (!ep)
		return;

	/* assume we'd just wait for the irq */
	for	(i = 0;	i <	100	&& !list_empty(&hep->urb_list);	i++)
		msleep(3);

	/*
	 * usually, before disable ep, all urb are dequeued.
	 * otherwise, there is something	wrong in upper driver
	 */
	if (!list_empty(&ep->q_list))
		dev_warn(acthcd->dev, "<ep disable>	error: ep%d's urb list not empty\n", ep->index);

	if (ep->type ==	PIPE_INTERRUPT)	{
		hcd->self.bandwidth_allocated -= ep->load /	ep->period;
		hrtimer_cancel(&ep->intr_timer);
		tasklet_kill(&ep->intr_tasklet);
	}

	local_irq_save(flags);

	usb_put_dev(ep->udev);
	index =	ep->index;
	if (index == 0)	{
		acthcd->ep0	= NULL;
		acthcd->setup_processing = 0;
		usb_clearbitsb(1, acthcd->base + HCOUT07IEN);
		usb_clearbitsb(1, acthcd->base + HCIN07IEN);
		usb_writeb(1, acthcd->base + HCOUT07IRQ);
		usb_writeb(1, acthcd->base + HCIN07IRQ);
	}
	else {
		if (ep->mask & USB_HCD_OUT_MASK) {
			acthcd->outep[index] = NULL;
			acthcd->bulkout_wait_ep[index] = NULL;
			acthcd->bulkout_wait_dma[index] = NULL;
		} else {
			acthcd->inep[index]	= NULL;
			ep_disable(ep);
			__hc_in_stop(acthcd, ep);
			//hcin_spkt_irq_disable(acthcd,	ep);
		}
		pio_irq_disable(acthcd,	ep->mask);
		pio_irq_clear(acthcd, ep->mask);
		release_fifo_addr(acthcd, ep->fifo_addr);

		//clear err_irq handler
		hcerr_irq_disable(acthcd, ep->mask);
		//acthcd->dma_working[dmanr	& 0x1] = urb->pipe;
	}

	hep->hcpriv	= NULL;

	local_irq_restore(flags);
	kfree(ep);
}

static int aotg_hcd_get_frame(struct usb_hcd *hcd)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	int	frame =	0;

	if (acthcd->speed == USB_SPEED_UNKNOWN)
		frame =	0;
	else if (acthcd->speed == USB_SPEED_HIGH)
		frame =	(int)(usb_readw(acthcd->base + HCFRMNRL) >> 3);
	else
		frame =	(int)usb_readw(acthcd->base + HCFRMNRL);

	return frame;
}

static int aotg_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct aotg_hcd	*acthcd;
	unsigned long flags;
	int	retval = 0;

	acthcd = hcd_to_aotg(hcd);
	local_irq_save(flags);
	if (!HC_IS_RUNNING(hcd->state))
		goto done;

	if ((acthcd->port &	AOTG_PORT_C_MASK) != 0)	{
		*buf = (1 << 1);
		HUB_DEBUG("<HUB	STATUS>port	status %08x	has	changes\n",	acthcd->port);
		retval = 1;
	}
done:
	local_irq_restore(flags);
	return retval;
}

static __inline__ void port_reset(struct aotg_hcd *acthcd)
{
	HCD_DEBUG("<USB> port reset\n");
	/* bit5: force port reset; bit6: reset signal period: 55ms */
	usb_writeb(0x1<<6 |	0x1<<5,	acthcd->base + HCPORTCTRL);
}

static void port_power(struct aotg_hcd *acthcd, int is_on)
{
	struct usb_hcd *hcd	= aotg_to_hcd(acthcd);

	/* hub is inactive unless the port is powered */
	if (is_on) {
		hcd->self.controller->power.power_state	= PMSG_ON;
		dev_dbg(acthcd->dev, "<USB>	power on\n");
	} else {
		hcd->self.controller->power.power_state	= PMSG_SUSPEND;
		dev_dbg(acthcd->dev, "<USB>	power off\n");
	}
}

static void port_suspend(struct aotg_hcd *acthcd)
{
	usb_clearbitsb(OTGCTRL_BUSREQ, acthcd->base	+ OTGCTRL);
}

static void port_resume(struct aotg_hcd *acthcd)
{
	usb_setbitsb(OTGCTRL_BUSREQ, acthcd->base +	OTGCTRL);
}

static int aotg_hcd_start(struct usb_hcd *hcd)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	//struct device *dev = hcd->self.controller;
	int	retval = 0;
	unsigned long flags;

	dev_info(acthcd->dev, "<HCD> start\n");

	local_irq_save(flags);
	hcd->state = HC_STATE_RUNNING;
	hcd->uses_new_polling =	1;
	local_irq_restore(flags);

	return retval;
}

static void aotg_hcd_stop(struct usb_hcd *hcd)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);
	unsigned long flags;

	dev_info(acthcd->dev, "<HCD> stop\n");

	local_irq_save(flags);
	hcd->state = HC_STATE_HALT;
	acthcd->port = 0;
	//acthcd->rhstate	= AOTG_RH_POWEROFF;
	local_irq_restore(flags);

	return;
}

#ifdef	CONFIG_PM

static int aotg_hub_suspend(struct usb_hcd *hcd)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);

	if ((hcd ==	NULL) || (acthcd ==	NULL)) {
		ACT_HCD_ERR
		return 0;
	}
	acthcd->suspend_request_pend = 1;
	port_suspend(acthcd);

	return 0;
}

static int
aotg_hub_resume(struct usb_hcd *hcd)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);

	port_resume(acthcd);

	return 0;
}

#else

#define	aotg_hub_suspend	NULL
#define	aotg_hub_resume	NULL

#endif

static __inline__ void aotg_hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0,	sizeof(*desc));
	desc->bDescriptorType = 0x29;
	desc->bDescLength =	9;
	desc->wHubCharacteristics = (__force __u16)(__constant_cpu_to_le16(0x0001));
	desc->bNbrPorts	= 1;
	//desc->bitmap[0] = 1 << 1;
	//desc->bitmap[1] = 0xff;
}

static int aotg_hub_control(struct usb_hcd *hcd, u16 typeReq,
				u16 wValue, u16	wIndex, char *buf, u16 wLength)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);
	unsigned long flags;
	int port_reset_flag = 0;
	int i = 0;
	int	retval = 0;

	if (in_irq())
		disable_irq_nosync(acthcd->uhc_irq);
	else
		disable_irq(acthcd->uhc_irq);

	spin_lock_irqsave(&acthcd->lock, flags);

	if (!HC_IS_RUNNING(hcd->state))	{
		dev_err(acthcd->dev, "<HUB_CONTROL>	hc state is	not	HC_STATE_RUNNING\n");
		spin_unlock_irqrestore(&acthcd->lock, flags);
		enable_irq(acthcd->uhc_irq);
		return -EPERM;
	}

	switch (typeReq) {
	case ClearHubFeature:
		HUB_DEBUG("ClearHubFeature wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);
		break;
	case ClearPortFeature:
		HUB_DEBUG("ClearPortFeature wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);

		if (wIndex != 1	|| wLength != 0)
			goto hub_error;

		switch (wValue)	{
		case USB_PORT_FEAT_ENABLE:
			acthcd->port &=	~(USB_PORT_STAT_ENABLE |
							USB_PORT_STAT_LOW_SPEED |
							USB_PORT_STAT_HIGH_SPEED);
			//acthcd->rhstate	= AOTG_RH_DISABLE;
			if (acthcd->port & USB_PORT_STAT_POWER)
				port_power(acthcd, 0);
			break;
		case USB_PORT_FEAT_SUSPEND:
			HUB_DEBUG("clear suspend feathure\n");
			//port_resume(acthcd);
			acthcd->port &=	~(1	<< wValue);
			break;
		case USB_PORT_FEAT_POWER:
			acthcd->port = 0;
			//acthcd->rhstate	= AOTG_RH_POWEROFF;
			port_power(acthcd, 0);
			break;
		case USB_PORT_FEAT_C_ENABLE:
		case USB_PORT_FEAT_C_SUSPEND:
		case USB_PORT_FEAT_C_CONNECTION:
		case USB_PORT_FEAT_C_OVER_CURRENT:
		case USB_PORT_FEAT_C_RESET:
			acthcd->port &=	~(1	<< wValue);
			break;
		default:
			goto hub_error;
		}
		break;
	case GetHubDescriptor:
		HUB_DEBUG("GetHubDescriptor wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);
		aotg_hub_descriptor((struct	usb_hub_descriptor *)buf);
		break;
	case GetHubStatus:
		HUB_DEBUG("GetHubStatus wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);

		*(__le32 *)	buf	= __constant_cpu_to_le32(0);
		break;
	case GetPortStatus:
		HUB_DEBUG("GetPortStatus wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);

		if (wIndex != 1)
			goto hub_error;
		*(__le32 *)	buf	= cpu_to_le32(acthcd->port);
		HUB_DEBUG("the port status is %08x\n ",
			acthcd->port);
		break;
	case SetHubFeature:
		HUB_DEBUG("SetHubFeature wValue: %04x,wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);
		goto hub_error;
		break;
	case SetPortFeature:
		HUB_DEBUG("SetPortFeature wValue:%04x, wIndex: %04x, wLength: %04x\n",
			wValue, wIndex, wLength);

		switch (wValue)	{
		case USB_PORT_FEAT_POWER:
			if (unlikely(acthcd->port &	USB_PORT_STAT_POWER))
				break;
			acthcd->port |=	(1 << wValue);
			//acthcd->rhstate	= AOTG_RH_POWERED;
			port_power(acthcd, 1);
			break;
		case USB_PORT_FEAT_RESET:
			port_reset(acthcd);

			usb_setbitsb(USBIRQ_SOF, acthcd->base + USBIRQ);

			/* if it's already enabled, disable */
			acthcd->port &=	~(USB_PORT_STAT_ENABLE |
							USB_PORT_STAT_LOW_SPEED |
							USB_PORT_STAT_HIGH_SPEED);
			acthcd->port |=	(1 << wValue);
			//acthcd->rhstate	= AOTG_RH_RESET;
			usb_setbitsb(USBIEN_URES, acthcd->base + USBIEN);
			/* enable reset irq */

			port_reset_flag = 1;
			i = 6;

			break;
		case USB_PORT_FEAT_SUSPEND:
			/* acthcd->port |= USB_PORT_FEAT_SUSPEND; */
			acthcd->port |=	(1 << wValue);
			//acthcd->rhstate	= AOTG_RH_SUSPEND;
			//port_suspend(acthcd);
			break;
		default:
			if (acthcd->port & USB_PORT_STAT_POWER)
				acthcd->port |=	(1 << wValue);
		}
		break;
	default:
hub_error:
		retval = -EPIPE;
		dev_err(acthcd->dev, "hub control error\n");
		break;

	}
	spin_unlock_irqrestore(&acthcd->lock, flags);
	enable_irq(acthcd->uhc_irq);

	while ((USB_PORT_FEAT_RESET == wValue) &&
		!(usb_readb(acthcd->base + USBIRQ) & USBIRQ_SOF) && i--)
		msleep(10);

	if (port_reset_flag)
		pr_debug("%s, i: %d, USBIRQ: 0x%x\n", __func__,
			i, usb_readb(acthcd->base + USBIRQ));

	return retval;
}

static void aotg_DD_set_phy(void __iomem *base, u8 reg, u8 value)
{
	u8 addrlow, addrhigh;
	int time = 1;

	addrlow = reg & 0x0f;
	addrhigh = (reg >> 4) & 0x0f;

	/*write	vstatus: */
	usb_writeb(value, base + VDSTATE);

	mb();

	/*write	vcontrol: */
	usb_writeb(addrlow | 0x10, base + VDCTRL);
	udelay(time); //the	vload period should > 33.3ns
	usb_writeb(addrlow & 0xf, base + VDCTRL);

	udelay(time);
	mb();

	usb_writeb(addrhigh | 0x10, base + VDCTRL);
	udelay(time);
	usb_writeb(addrhigh & 0x0f, base + VDCTRL);
	udelay(time);
	usb_writeb(addrhigh | 0x10, base + VDCTRL);
	udelay(time);
	if (phy == 1)
		pr_info("set_phy(0x%02X, 0x%02X)\n", reg, value);

	return;
}

static void aotg_hcd_config_phy(struct	aotg_hcd *acthcd,
				struct platform_device *pdev)
{
	if (acthcd->id) {
		if (phy == 1)
			pr_info("usb2.0-1: phy version %s\n", PORT1_PHY_CFG);

		aotg_DD_set_phy(acthcd->base, 0xe2, 0x34);
		aotg_DD_set_phy(acthcd->base, 0xe7, 0x0b);
		aotg_DD_set_phy(acthcd->base, 0xe7, 0x0f);
		udelay(1);
		aotg_DD_set_phy(acthcd->base, 0xe2,0x76);	/* voltage of threshold */
		udelay(1);
		if (acthcd->usbecs == (void __iomem *)USB2_1ECS_ATM7023)
			aotg_DD_set_phy(acthcd->base, 0xe4, 0x04);
		else
			aotg_DD_set_phy(acthcd->base, 0xe4, 0x0b);
		aotg_DD_set_phy(acthcd->base, 0x84, 0x1a);
	} else {
		if (phy == 1)
			pr_info("usb2.0-0 : phy version %s\n", PORT0_PHY_CFG);

		aotg_DD_set_phy(acthcd->base, 0xe2, 0x34);
		aotg_DD_set_phy(acthcd->base, 0xe7, 0x0b);
		aotg_DD_set_phy(acthcd->base, 0xe7, 0x0f);
		udelay(1);
		aotg_DD_set_phy(acthcd->base, 0xe2, 0x86);
		aotg_DD_set_phy(acthcd->base, 0xe4, 0x0b);
		aotg_DD_set_phy(acthcd->base, 0x84, 0x1a);
	}
}

static void aotg_DD_set_phy_init(struct aotg_hcd *acthcd)
{
	int time = 600;

	/* write vstatus: */
	usb_writeb(0x0a, acthcd->base + VDSTATE);
	mb();
	udelay(time);
	/* write vcontrol: */
	usb_writeb(0x17, acthcd->base + VDCTRL);
	udelay(time); //the vload period should > 33.3ns
	usb_writeb(0x07, acthcd->base + VDCTRL);
	udelay(time);
	usb_writeb(0x17, acthcd->base + VDCTRL);
	udelay(time);
	mb();
	usb_writeb(0x1e, acthcd->base + VDCTRL);
	udelay(time);
	usb_writeb(0x0e, acthcd->base + VDCTRL);
	udelay(time);
	usb_writeb(0x1e, acthcd->base + VDCTRL);
	udelay(time);
	aotg_hcd_config_phy(acthcd, to_platform_device(acthcd->dev));

	if (phy == 1)
		pr_info("%s,acthcd->id:%d\n", __func__, acthcd->id);

	return;
}

/**
 * clk enable sequence: phy -> pllen -> cce.
 * clk disable sequence: cce -> pllen -> phy.
 */
static void aotg_hcd_clk_enable(struct aotg_hcd *acthcd, int enable)
{
	if (enable) {
		aotg_phy0_clk_count++;
		clk_prepare_enable(acthcd->clk_phy);
		if (acthcd->id == 1)
			clk_prepare_enable(acthcd->clk_phy1);
		clk_prepare_enable(acthcd->clk_pllen);
		clk_prepare_enable(acthcd->clk_cce);
	} else {
		aotg_phy0_clk_count--;
		clk_disable_unprepare(acthcd->clk_cce);
		clk_disable_unprepare(acthcd->clk_pllen);
		if (acthcd->id == 1)
			clk_disable_unprepare(acthcd->clk_phy1);
		if (aotg_phy0_clk_count == 0)
			clk_disable_unprepare(acthcd->clk_phy);
	}
}


/**
 * check and wait for reset done.
 * FIXME: what if reset timeout?
 */
static int aotg_hcd_wait_for_reset_done(struct aotg_hcd *ctrl)
{
	int i = 0;

	if ((usb_readb(ctrl->base + USBERESET) & 0x01) != 0)
		pr_debug("aotg begin to reset...\n");
	while ((usb_readb(ctrl->base + USBERESET) & 0x01) != 0 && (i < 500000)) {
		/* waiting for reset */
		i++;
		udelay(1);
	}

	if ((usb_readb(ctrl->base + USBERESET) & 0x01) != 0) {
		pr_err("aotg reset not complete!\n");
		return 0;
	} else {
		pr_debug("aotg reset complete.\n");
		return -1;
	}
}

static void aotg_hcd_hardware_init(struct aotg_hcd *acthcd)
{
	unsigned long flags;

	local_irq_save(flags);

	aotg_DD_set_phy_init(acthcd);

	usb_writel(0, acthcd->usbecs);
	udelay(1);
	//set soft vbus and id, vbus threshold 3.43~3.57
	usb_writel(0x1 << 26 | 0x1 << 25 | 0x1 << 24 | 0x3 << 4, acthcd->usbecs);

	usb_writeb(0xff, acthcd->base + TAAIDLBDIS);
	/* set TA_AIDL_BDIS timeout never generate */
	usb_writeb(0xff, acthcd->base + TAWAITBCON);
	/* set TA_WAIT_BCON timeout never generate */
	usb_writeb(0x28, acthcd->base + TBVBUSDISCHPLS);
	usb_setbitsb(1 << 7, acthcd->base + TAWAITBCON);
	//set soft vbus and id, vbus threshold 3.43~3.57
	//usb_writel(0x1<<26|0x1<<25|0x1<<24|0x3<<4, data->usbecs);
	//dev_info(acthcd->dev, "usbecs set to 0x%08X.\n", usb_readl(data->usbecs));
	usb_writew(0x1000, acthcd->base + VBUSDBCTIMERL);
	usb_writeb(0x40, acthcd->base + USBCS);

	/* Enable high-speed default: no need to disable HS mode */
	//usb_setbitsb(1 << 7, acthcd->base + BKDOOR);

	//aotg_hcd_config_phy(acthcd->base, to_platform_device(acthcd->dev));
	local_irq_restore(flags);

	return;
}

static int aotg_hcd_init(struct usb_hcd *hcd, struct platform_device *pdev)
{
	struct aotg_hcd *acthcd = hcd_to_aotg(hcd);
	int	retval = 0;
	int	i;

	/*init software state */
	spin_lock_init(&acthcd->lock);
	spin_lock_init(&acthcd->tasklet_lock);
	acthcd->tasklet_retry =	0;
	//acthcd->dev = &pdev->dev;
	acthcd->port = 0;
	//acthcd->rhstate	= AOTG_RH_POWEROFF;
	acthcd->inserted = 0;

	acthcd->frame =	0;//NO_FRAME;
	//acthcd->periodic_count = 0;
	acthcd->sof_kref = 0;
	INIT_LIST_HEAD(&acthcd->hcd_enqueue_list);
	INIT_LIST_HEAD(&acthcd->hcd_dequeue_list);
	INIT_LIST_HEAD(&acthcd->hcd_finished_list);
	tasklet_init(&acthcd->urb_tasklet, urb_tasklet_func, (unsigned long)acthcd);

	acthcd->ep0	= NULL;
	for	(i = 0;	i <	MAX_EP_NUM;	i++) {
		acthcd->inep[i]	= NULL;
		acthcd->outep[i] = NULL;

		acthcd->bulkout_wait_ep[i] = NULL;
		acthcd->bulkout_wait_dma[i] = NULL;
	}

	acthcd->fifo_map[0]	= 1<<31;
	acthcd->fifo_map[1]	= 1<<31	| 64;
	for	(i = 2;	i <	64;	i++)
		acthcd->fifo_map[i]	= 0;

	INIT_LIST_HEAD(&acthcd->dma_queue[0]);
	INIT_LIST_HEAD(&acthcd->dma_queue[1]);

	acthcd->put_aout_msg = 0;
	acthcd->discon_happened = 0;
	acthcd->uhc_irq	= 0;
	for	(i = 0;	i <	AOTG_QUEUE_POOL_CNT; i++)
		acthcd->queue_pool[i] =	NULL;

	return retval;
}

#define AOTG_BUF_NEED_MAP(x, y)	((x != NULL) && (((unsigned long)x & 0x3) == 0))

/* control & interrupt using pio only.
 * it's called before urb_enqueue, so is_use_pio is not assigned.
 */
static int aotg_map_urb_for_dma(struct usb_hcd *hcd, struct	urb	*urb, gfp_t	mem_flags)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	int	ret	= 0;

	if (((acthcd->use_dma &&	usb_pipebulk(urb->pipe)) &&
		AOTG_BUF_NEED_MAP(urb->transfer_buffer, urb->transfer_dma) &&
		(urb->transfer_buffer_length >=	AOTG_MIN_DMA_SIZE)) ||
		((urb->sg) && (!urb->transfer_buffer)))
		ret = usb_hcd_map_urb_for_dma(hcd, urb,	mem_flags);

	return ret;
}

static void	aotg_unmap_urb_for_dma(struct usb_hcd *hcd,	struct urb *urb)
{
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);
	struct aotg_hcep * ep =	NULL;

	if (urb->ep->hcpriv) {
		ep = (struct aotg_hcep *)urb->ep->hcpriv;
		if (unlikely(ep->is_use_pio)) {
			if (usb_pipebulk(urb->pipe) && (usb_pipeout(urb->pipe))) {
				// do nothing;
			} else
				return;
		}
	}
	if (((acthcd->use_dma &&	usb_pipebulk(urb->pipe)) &&
		AOTG_BUF_NEED_MAP(urb->transfer_buffer, urb->transfer_dma) &&
		(urb->transfer_buffer_length >=	AOTG_MIN_DMA_SIZE)) ||
		(urb->sg))
		usb_hcd_unmap_urb_for_dma(hcd, urb);

	return;
}

static struct hc_driver	act_hc_driver =	{
	.description = driver_name,
	.hcd_priv_size = sizeof(struct aotg_hcd),
	.product_desc =	DRIVER_DESC,

	/*
	 * generic hardware linkage
	 */
	.irq = aotg_hcd_irq,
	.flags = HCD_USB2 |	HCD_MEMORY,

	/* Basic lifecycle operations */
	.start = aotg_hcd_start,
	.stop =	aotg_hcd_stop,

	.urb_enqueue = aotg_hcd_urb_enqueue,
	.urb_dequeue = aotg_hcd_urb_dequeue,

	.map_urb_for_dma	= aotg_map_urb_for_dma,
	.unmap_urb_for_dma	= aotg_unmap_urb_for_dma,

	.endpoint_disable =	aotg_hcd_endpoint_disable,

	/*
	 * periodic schedule support
	 */
	.get_frame_number =	aotg_hcd_get_frame,

	.hub_status_data = aotg_hub_status_data,
	.hub_control = aotg_hub_control,

	.bus_suspend =		  aotg_hub_suspend,
	.bus_resume	=		  aotg_hub_resume,
};

const struct of_device_id aotg_hcd_of_match[];
static int aotg_hcd_probe(struct platform_device *pdev)
{
	unsigned int ht_s;
	unsigned int ht_ns;
	int	ret;
	int	irq;
	struct usb_hcd *hcd;
	struct resource *res;
	struct aotg_hcd *acthcd;
	const struct of_device_id *of_id;

	of_id = of_match_device(aotg_hcd_of_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "of_device not found\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	/* Initialize dma_mask and coherent_dma_mask to 32-bits */
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	else
		dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));

	hcd	= usb_create_hcd(&act_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs))
		return PTR_ERR(hcd->regs);

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	ret = aotg_hcd_init(hcd, pdev);
	if (ret) {
		dev_err(&pdev->dev,	"aotg hcd init failed\n");
		return ret;
	}

	acthcd = hcd_to_aotg(hcd);

	acthcd->dev	= &pdev->dev;
	acthcd->base = hcd->regs;

	acthcd->hcd_exiting	= 0;
	acthcd->uhc_irq	= irq;

	acthcd->id = pdev->id;

	/* ioremap for usbecs */
	acthcd->usbecs = devm_ioremap_nocache(&pdev->dev,
		*(unsigned int *)of_id->data, 4);

	if (acthcd->id == 0) {
		acthcd->clk_phy = devm_clk_get(&pdev->dev, "usb2h0_phy");
		if (IS_ERR(acthcd->clk_phy)) {
			dev_err(&pdev->dev, "unable to get usb2h0_phy\n");
			return PTR_ERR(acthcd->clk_phy);
		}
		acthcd->clk_pllen = devm_clk_get(&pdev->dev, "usb2h0_pllen");
		if (IS_ERR(acthcd->clk_pllen)) {
			dev_err(&pdev->dev, "unable to get usb2h0_pllen\n");
			return PTR_ERR(acthcd->clk_pllen);
		}
		acthcd->clk_cce = devm_clk_get(&pdev->dev, "usb2h0_cce");
		if (IS_ERR(acthcd->clk_cce)) {
			dev_err(&pdev->dev, "unable to get usb2h0_cce\n");
			return PTR_ERR(acthcd->clk_cce);
		}
	} else {
		/* phy0 clock */
		acthcd->clk_phy = devm_clk_get(&pdev->dev, "usb2h0_phy");
		if (IS_ERR(acthcd->clk_phy)) {
			dev_err(&pdev->dev, "unable to get usb2h0_phy\n");
			return PTR_ERR(acthcd->clk_phy);
		}
		/* phy1 clock */
		acthcd->clk_phy1 = devm_clk_get(&pdev->dev, "usb2h1_phy");
		if (IS_ERR(acthcd->clk_phy1)) {
			dev_err(&pdev->dev, "unable to get usb2h1_phy\n");
			return PTR_ERR(acthcd->clk_phy1);
		}
		acthcd->clk_pllen = devm_clk_get(&pdev->dev, "usb2h1_pllen");
		if (IS_ERR(acthcd->clk_pllen)) {
			dev_err(&pdev->dev, "unable to get usb2h1_pllen\n");
			return PTR_ERR(acthcd->clk_pllen);
		}
		acthcd->clk_cce = devm_clk_get(&pdev->dev, "usb2h1_cce");
		if (IS_ERR(acthcd->clk_cce)) {
			dev_err(&pdev->dev, "unable to get usb2h1_cce\n");
			return PTR_ERR(acthcd->clk_cce);
		}
	}

	dev_dbg(&pdev->dev, "clk_phy: %p\n", acthcd->clk_phy);
	dev_dbg(&pdev->dev, "clk_pllen: %p\n", acthcd->clk_pllen);
	dev_dbg(&pdev->dev, "clk_cce: %p\n", acthcd->clk_cce);

	aotg_hcd_clk_enable(acthcd, 1);

	/* usb controller reset */
	device_reset(&pdev->dev);
	aotg_hcd_wait_for_reset_done(acthcd);

	aotg_hcd_hardware_init(acthcd);

	hcd->has_tt	= 1;
	hcd->self.uses_pio_for_control = 1;	// for ep0,	using CPU mode only

	if (pdev->dev.dma_mask)	{
		ret = aotg_hcd_dma_request_2(aotg_hcd_dma_handler, acthcd);
		//printk("id:%d, usb_dma:	no:%d\n", pdev->id, retval);

		if (ret >= 0) {
			/* FIXME: no sg support */
			//hcd->self.sg_tablesize = MAX_SG_TABLE - 1;
			hcd->self.sg_tablesize = 0;
			acthcd->dma_nr0	= ret;
			acthcd->dma_nr1	= ret + 1;
			acthcd->use_dma	= 1;
			acthcd->dma_working[0] = 0;
			acthcd->dma_working[1] = 0;
			aotg_hcd_dma_sync();
			aotg_hcd_dma_reset_2(acthcd->dma_nr0);
			aotg_hcd_dma_enable_irq_2(acthcd->dma_nr0);
			aotg_hcd_dma_clear_pend_2(acthcd->dma_nr0);
		} else {
			hcd->self.sg_tablesize = 0;
			acthcd->dma_nr0	= ret;
			acthcd->dma_nr1	= -1;
			acthcd->use_dma	= 0;
			dev_err(&pdev->dev, "Can't register IRQ for DMA, ret: %d\n", ret);
			goto error;
		}
	} else {
		acthcd->use_dma	= 0;
		acthcd->dma_working[0] = 0;
		acthcd->dma_working[1] = 0;
		acthcd->dma_nr0	= -1;
		acthcd->dma_nr1	= -1;
		dev_dbg(&pdev->dev,	"<hcd_probe>: hcd use pio\n");
	}

	init_timer(&acthcd->hotplug_timer);
	acthcd->hotplug_timer.function = aotg_hcd_hotplug_timer;
	acthcd->hotplug_timer.data = (unsigned long)acthcd;

	hrtimer_init(&acthcd->trans_wait_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	acthcd->trans_wait_timer.function = aotg_hcd_trans_wait_timer;
	ht_s = 0;
	ht_ns = 500 * 1000 * 1000;

	init_timer(&acthcd->intr_check_timer);
	acthcd->intr_check_timer.function = intr_check_ep_timer;
	acthcd->intr_check_timer.data =(unsigned long)acthcd;
	mod_timer(&acthcd->intr_check_timer, jiffies +msecs_to_jiffies(100));

	ret = usb_add_hcd(hcd, irq, 0);
	if (likely(ret == 0)) {
		//dev_info(&pdev->dev, " ==> open vbus.\n");
		aotg_enable_irq(acthcd);
		create_debug_file(acthcd);
		dev_info(&pdev->dev,
			"aotg hcd initialized. OTGIRQ: 0x%02X, OTGSTATE: 0x%02X\n",
			usb_readb(acthcd->base + OTGIRQ),
			usb_readb(acthcd->base + OTGSTATE));

		if (usb_readb(acthcd->base + OTGSTATE) == AOTG_STATE_A_HOST) {
			acthcd->put_aout_msg = 0;
			mod_timer(&acthcd->hotplug_timer, jiffies +	msecs_to_jiffies(3));
		} else
			mod_timer(&acthcd->hotplug_timer, jiffies +	msecs_to_jiffies(1000));

		hrtimer_start(&acthcd->trans_wait_timer, ktime_set(ht_s, ht_ns), HRTIMER_MODE_REL);

		return 0;
	} else
		dev_err(&pdev->dev, "usb add hcd failed\n");

	del_timer_sync(&acthcd->hotplug_timer);
	del_timer_sync(&acthcd->intr_check_timer);
	hrtimer_cancel(&acthcd->trans_wait_timer);
	aotg_hcd_dma_free_2(acthcd->dma_nr0);

	aotg_send_line_out_msg(acthcd);

	usb_put_hcd(hcd);

	return ret;

error:
	aotg_hcd_clk_enable(acthcd, 0);

	return ret;
}

static void aotg_hcd_flush_queue(struct aotg_hcd	*acthcd)
{
	struct aotg_queue *q = NULL, *next;
	struct usb_hcd *hcd;
	struct urb *urb;
	struct aotg_hcep *ep;
	int status;
	unsigned long flags;

	spin_lock_irqsave(&acthcd->lock, flags);

ENQUEUE_FLUSH:
	urb = NULL;
	list_for_each_entry_safe(q,	next, &acthcd->hcd_enqueue_list, enqueue_list) {
		urb	= q->urb;
		ep = q->ep;
		if (ep)
			ep->urb_unlinked_cnt++;
		list_del(&q->enqueue_list);
		hcd	= bus_to_hcd(urb->dev->bus);
		break;
	}
	if (urb != NULL) {
		usb_hcd_unlink_urb_from_ep(hcd,urb);
		spin_unlock_irqrestore(&acthcd->lock, flags);
		/* in usb_hcd_giveback_urb,	complete function may call new urb_enqueue.	*/
		usb_hcd_giveback_urb(hcd, urb, -EIO);

		spin_lock_irqsave(&acthcd->lock, flags);
		goto ENQUEUE_FLUSH;
	}
DEQUEUE_FLUSH:
	urb = NULL;
	list_for_each_entry_safe(q,	next, &acthcd->hcd_dequeue_list, dequeue_list) {
		urb	= q->urb;
		ep = q->ep;
		if (ep)
			ep->urb_unlinked_cnt++;
		list_del(&q->dequeue_list);
		status = q->status;
		hcd	= bus_to_hcd(urb->dev->bus);
		break;
	}
	if (urb != NULL) {
		usb_hcd_unlink_urb_from_ep(hcd,urb);
		spin_unlock_irqrestore(&acthcd->lock, flags);
		/* in usb_hcd_giveback_urb,	complete function may call new urb_enqueue.	*/
		usb_hcd_giveback_urb(hcd, urb, status);

		spin_lock_irqsave(&acthcd->lock, flags);
		goto DEQUEUE_FLUSH;
	}
FINISHED_FLUSH:
	urb = NULL;
	list_for_each_entry_safe(q,	next, &acthcd->hcd_finished_list, finished_list) {
		urb	= q->urb;
		ep = q->ep;
		list_del(&q->finished_list);
		status = q->status;
		tasklet_finish_request(acthcd, q, status);
		hcd	= aotg_to_hcd(acthcd);
		break;
	}
	if (urb != NULL) {
		usb_hcd_unlink_urb_from_ep(hcd,	urb);

		spin_unlock_irqrestore(&acthcd->lock, flags);

		/* in usb_hcd_giveback_urb,	complete function may call new urb_enqueue.	*/
		usb_hcd_giveback_urb(hcd, urb, status);

		spin_lock_irqsave(&acthcd->lock, flags);
		goto FINISHED_FLUSH;
	}

	spin_unlock_irqrestore(&acthcd->lock, flags);
	return;
}

/* NOTICE: Not Used */
#if 0
static void test_flush_queue(struct aotg_hcd *acthcd)
{
	struct aotg_queue *q = NULL, *next;
	struct aotg_hcep *ep;
	int	i;
	unsigned long flags;

	spin_lock_irqsave(&acthcd->lock, flags);
	acthcd->hcd_exiting	= 1;

	ep = acthcd->ep0;
	if (ep)	{
		list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
			if (list_empty(&q->finished_list)) {
				q->status = -EIO;
				list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
			}
		}
	}
	for	(i = 1;	i <	MAX_EP_NUM;	i++) {
		ep = acthcd->inep[i];
		if (ep)	{
			list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
				if (list_empty(&q->finished_list)) {
					q->status = -EIO;
					list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				}
			}
		}
	}
	for	(i = 1;	i <	MAX_EP_NUM;	i++) {
		ep = acthcd->outep[i];
		if (ep)	{
			list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
				if (list_empty(&q->finished_list)) {
					q->status = -EIO;
					list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				}
			}
		}
	}
	spin_unlock_irqrestore(&acthcd->lock, flags);

	aotg_hcd_flush_queue(acthcd);
}
#endif

static int aotg_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd	= platform_get_drvdata(pdev);
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);

	/* make sure all urb request is exited */
	struct aotg_queue *q = NULL, *next;
	struct aotg_hcep *ep;
	int	i;
	unsigned long flags;

	tasklet_kill(&acthcd->urb_tasklet);

	disable_irq(acthcd->uhc_irq);
	spin_lock_irqsave(&acthcd->lock, flags);
	acthcd->hcd_exiting	= 1;

	ep = acthcd->ep0;
	if (ep)	{
		list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
			if (list_empty(&q->finished_list)) {
				q->status = -EIO;
				list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
			}
		}
	}
	for	(i = 1;	i <	MAX_EP_NUM;	i++) {
		ep = acthcd->inep[i];
		if (ep)	{
			list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
				if (list_empty(&q->finished_list)) {
					q->status = -EIO;
					list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				}
			}
		}
	}
	for	(i = 1;	i <	MAX_EP_NUM;	i++) {
		ep = acthcd->outep[i];
		if (ep)	{
			list_for_each_entry_safe(q,	next, &ep->q_list, ep_q_list) {
				if (list_empty(&q->finished_list)) {
					q->status = -EIO;
					list_add_tail(&q->finished_list, &acthcd->hcd_finished_list);
				}
			}
		}
	}
	spin_unlock_irqrestore(&acthcd->lock, flags);

	aotg_hcd_flush_queue(acthcd);

	hrtimer_cancel(&acthcd->trans_wait_timer);
	del_timer_sync(&acthcd->hotplug_timer);
	del_timer_sync(&acthcd->intr_check_timer);
	if (acthcd->put_aout_msg != 0) {
		aotg_send_line_out_msg(acthcd);
	}

	remove_debug_file(acthcd);
	aotg_hcd_free_dma_buf(acthcd, NULL);
	usb_remove_hcd(hcd);
//	iounmap(hcd->regs);
//	release_mem_region(hcd->rsrc_start,	hcd->rsrc_len);

	aotg_hcd_release_queue(acthcd, NULL);

	if (acthcd->use_dma)
		aotg_hcd_dma_free_2(acthcd->dma_nr0);

	aotg_disable_irq(acthcd);

	aotg_hcd_clk_enable(acthcd, 0);

	usb_put_hcd(hcd);

	pr_info("pdev->id remove:%d\n", pdev->id);

	return 0;
}

/* FIXME: Do we need to shutdown aotg? */
void aotg_hcd_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd	= platform_get_drvdata(pdev);
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);

	printk("usb2-%d shutdown\n", acthcd->id);

	if (acthcd->id == 0)
		aotg0_device_exit(1);
	else if (acthcd->id == 1)
		aotg1_device_exit(1);
	else
		ACT_HCD_ERR

	return;
}

static inline int aotg_device_calc_id(int dev_id)
{
	int	id;

	if (hcd_ports_en_ctrl == 1)
		id = 0;
	else if (hcd_ports_en_ctrl == 2)
		id = 1;
	else if (hcd_ports_en_ctrl == 3) {
		if (dev_id)
			id = 0;
		else
			id = 1;
	} else
		id = dev_id;

	return id;
}

/* avoid registering duplicately. */
static unsigned int aotg_registered_map = 0;
int aotg_device_register(int dev_id)
{
	if (aotg_registered_map & (0x1 << dev_id)) {
		printk("aotg-%d allready registered.\n", dev_id);
		return 0;
	}
	aotg_registered_map |= (0x1 << dev_id);
	dev_id = aotg_device_calc_id(dev_id);

	if (dev_id)
		return aotg1_device_init(0);
	else
		return aotg0_device_init(0);
}
EXPORT_SYMBOL(aotg_device_register);

void aotg_device_unregister(int dev_id)
{
	if ((aotg_registered_map & (0x1 << dev_id)) == 0) {
		printk("aotg-%d allready unregistered.\n", dev_id);
		return;
	}
	aotg_registered_map &= ~(0x1 << dev_id);
	dev_id = aotg_device_calc_id(dev_id);

	if (dev_id)
		return aotg1_device_exit(0);
	else
		return aotg0_device_exit(0);
}
EXPORT_SYMBOL(aotg_device_unregister);

/* In order to keep compatibility */
int aotg_hub_register(int dev_id)
{
	return aotg_device_register(dev_id);
}
EXPORT_SYMBOL(aotg_hub_register);

void aotg_hub_unregister(int dev_id)
{
	aotg_device_unregister(dev_id);
}
EXPORT_SYMBOL(aotg_hub_unregister);


#ifdef CONFIG_PM
static int aotg_hcd_suspend(struct device *dev)
{
	//struct usb_hcd *hcd	= platform_get_drvdata(pdev);
	struct usb_hcd *hcd	= dev_get_drvdata(dev);
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);

	if ((hcd ==	NULL) || (acthcd ==	NULL)) {
		ACT_HCD_ERR
		return 0;
	}

	printk(KERN_DEBUG"usb 2.0-%d suspend\n", acthcd->id);

	aotg_disable_irq(acthcd);
	//usb_clearbitsb(OTGCTRL_BUSREQ, acthcd->base + OTGCTRL);
	aotg_hcd_clk_enable(acthcd, 0);

	/* power off */
	if (acthcd->id == 0)
		aotg0_device_exit(1);
	else
		aotg1_device_exit(1);

	//acthcd->lr_flag = 1;

	return 0;
}

static int aotg_hcd_resume(struct device *dev)
{
	//struct usb_hcd *hcd	= platform_get_drvdata(pdev);
	struct usb_hcd *hcd	= dev_get_drvdata(dev);
	struct aotg_hcd	*acthcd	= hcd_to_aotg(hcd);

	if ((hcd ==	NULL) || (acthcd ==	NULL)) {
		ACT_HCD_ERR
		return 0;
	}
	if (hcd_suspend_en)
		acthcd->phy_resumed = 1;

#ifdef NEW_SUSPEND_RESUME
	if (acthcd->id == 0)
		aotg_dev_plugout_msg(0);
	else
		aotg_dev_plugout_msg(1);
#else
	/* power on */
	if (acthcd->id == 0)
		aotg0_device_init(1);
	else
		aotg1_device_init(1);

	printk(KERN_DEBUG"usb 2.0-%d suspend\n", acthcd->id);
	aotg_hcd_hardware_init(acthcd);

	if (acthcd->use_dma	!= 0) {
		acthcd->dma_working[0] = 0;
		acthcd->dma_working[1] = 0;
		aotg_hcd_dma_sync();
		aotg_hcd_dma_reset_2(acthcd->dma_nr0);
		aotg_hcd_dma_enable_irq_2(acthcd->dma_nr0);
		aotg_hcd_dma_clear_pend_2(acthcd->dma_nr0);
	}
	//INIT_LIST_HEAD(&acthcd->dma_queue[0]);
	//INIT_LIST_HEAD(&acthcd->dma_queue[1]);
	//INIT_LIST_HEAD(&acthcd->hcd_enqueue_list);

	for (i = 1; i < MAX_EP_NUM; i++) {
		acthcd->bulkout_wait_ep[i] = NULL;
		acthcd->bulkout_wait_dma[i] = NULL;
	}

	aotg_enable_irq(acthcd);
#endif

	return 0;
}

static const struct dev_pm_ops aotg_hcd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aotg_hcd_suspend, aotg_hcd_resume)
};
#define DEV_PM_OPS	(&aotg_hcd_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static struct aotg_private_data usb2h0_usbces = {
	.usbecs = USB2H0_ECS,
};

static struct aotg_private_data usb2h1_usbces = {
	.usbecs = USB2H1_ECS,
};

//static const struct of_device_id aotg_hcd_of_match[] = {
const struct of_device_id aotg_hcd_of_match[] = {
	{ .compatible = "actions,ats3605-usb2.0-0", .data = &usb2h0_usbces, },
	{ .compatible = "actions,ats3605-usb2.0-1", .data = &usb2h1_usbces, },
	{ },
};
MODULE_DEVICE_TABLE(of, aotg_hcd_of_match);

static struct platform_driver aotg_hcd_driver = {
	.probe	  = aotg_hcd_probe,
	.remove	  = aotg_hcd_remove,
	.shutdown = aotg_hcd_shutdown,
	.driver	  = {
		.name = driver_name,
		.pm = DEV_PM_OPS,
		//.of_match_table = of_match_ptr(aotg_hcd_of_match),
	},
};

static const struct of_device_id aotg_hcd_config_of_match[] = {
	{ .compatible = "actions,ats3605-usb2-config", },
	{ },
};
MODULE_DEVICE_TABLE(of, aotg_hcd_config_of_match);

/* aotg usb2 related configs */
static int aotg_hcd_get_dts(void)
{
	struct device_node *fdt_node;
	const __be32 *prop;

	fdt_node = of_find_compatible_node(NULL, NULL,
		"actions,ats3605-usb2-config");
	if (fdt_node == NULL) {
		pr_warn("%s couldn't find device node!\n", __func__);
		return -EINVAL;
	}

	prop = of_get_property(fdt_node, "ports_config", NULL);
	if (prop == NULL)
		pr_warn("%s couldn't find ports_config!\n", __func__);
	else {
		hcd_ports_en_ctrl = be32_to_cpup(prop);
		pr_info("%s hcd_ports_en_ctrl: %d\n", __func__, hcd_ports_en_ctrl);
	}

	prop = of_get_property(fdt_node, "hcd_suspend_en", NULL);
	if (prop == NULL)
		pr_warn("%s couldn't find hcd_suspend_en!\n", __func__);
	else {
		hcd_suspend_en = be32_to_cpup(prop);
		pr_info("%s hcd_suspend_en: %d\n", __func__, hcd_suspend_en);
	}

	prop = of_get_property(fdt_node, "aotg0_host_plug_detect", NULL);
	if (prop == NULL)
		pr_warn("%s couldn't find aotg0_host_plug_detect!\n", __func__);
	else {
		aotg0_host_plug_detect = be32_to_cpup(prop);
		pr_info("%s aotg0_host_plug_detect: %d\n", __func__,
			aotg0_host_plug_detect);
	}

	prop = of_get_property(fdt_node, "aotg1_host_plug_detect", NULL);
	if (prop == NULL)
		pr_warn("%s couldn't find aotg1_host_plug_detect!\n", __func__);
	else {
		aotg1_host_plug_detect = be32_to_cpup(prop);
		pr_info("%s aotg1_host_plug_detect: %d\n", __func__,
			aotg1_host_plug_detect);
	}

	return 0;
}

static int __init aotg_init(void)
{
	/* default values works fine, go on even errors happened! */
	aotg_hcd_get_dts();

	mutex_init(&aotg_onoff_mutex);
	aotg0_device_mod_init();
	aotg1_device_mod_init();
	create_acts_hcd_proc();
#ifdef CONFIG_USB_LEGACY_AOTG_HUB_SUPPORT
	ahcd_hub_init();
	pr_info("aotg support usb hub!\n");
#endif
	aotg_uhost_mon_init(aotg0_host_plug_detect, aotg1_host_plug_detect);

	return platform_driver_register(&aotg_hcd_driver);
}
module_init(aotg_init);

static void	__exit aotg_exit(void)
{
#ifdef CONFIG_USB_LEGACY_AOTG_HUB_SUPPORT
	ahcd_hub_exit();
#endif
	remove_acts_hcd_proc();
	platform_driver_unregister(&aotg_hcd_driver);
	//aotg1_wakelock_init(0);
	aotg0_device_mod_exit();
	aotg1_device_mod_exit();
	aotg_uhost_mon_exit();
}
module_exit(aotg_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
