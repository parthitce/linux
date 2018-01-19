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
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <linux/regulator/consumer.h>
#include <mach/hardware.h>

#include "aotg_udc.h"
#include "aotg_regs.h"
#include "aotg_dma.h"


static unsigned int aotg_dma_map;
static void __iomem *aotg_dma_reg_base[4];
static void __iomem *aotg_reg_base;

int aotg_dma_reset(int dma_nr)
{
	unsigned char data;

	data = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1COM);
	data |= 0x1 << 4;		/* bit4: reset */
	data |= 0x1 << 5;		/* bit5: burst select, 1: burst8 */
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1COM);
	data &= ~(0x1 << 4);	/* reset done */
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1COM);

	return 0;
}

/**
 * 0x11: UDMA1 transfer complete IEN & UDMA1 write error IEN.
 * 0x09<<2: UDMA2 transfer complete IEN & UDMA1 write error IEN.
 */
int aotg_dma_enable_irq(int dma_nr)
{
	unsigned char data;

	data = usb_readb(aotg_reg_base + UDMAIEN);
	data |= (dma_nr == 0) ? 0x11 : (0x09 << 2);
	usb_writeb(data, aotg_reg_base + UDMAIEN);

	return 0;
}

int aotg_dma_is_irq(void)
{
	unsigned char data;
	unsigned char mask;
	int ret = 0;

	mask = usb_readb(aotg_reg_base + UDMAIEN);
	data = usb_readb(aotg_reg_base + UDMAIRQ);
	data = data & mask;

	/* UDMA1 or UDMA2 write error */
	if (data & 0x30) {
		pr_err("dma err irq! data:%x\n", (unsigned int)data);
#if 1
		pr_err("UDMAIRQ: %x\n", usb_readb(aotg_reg_base + UDMAIRQ));
		pr_err("UDMAIEN: %x\n", usb_readb(aotg_reg_base + UDMAIEN));
		pr_err("UDMA1MEMADDR: %x\n", usb_readl(aotg_reg_base + UDMA1MEMADDR));
		pr_err("UDMA1EPSEL: %x\n", usb_readb(aotg_reg_base + UDMA1EPSEL));
		pr_err("UDMA1COM: %x\n", usb_readb(aotg_reg_base + UDMA1COM));
		pr_err("UDMA1CNTL: %x\n", usb_readb(aotg_reg_base + UDMA1CNTL));
		pr_err("UDMA1CNTM: %x\n", usb_readb(aotg_reg_base + UDMA1CNTM));
		pr_err("UDMA1CNTH: %x\n", usb_readb(aotg_reg_base + UDMA1CNTH));
		pr_err("UDMA1REML: %x\n", usb_readb(aotg_reg_base + UDMA1REML));
		pr_err("UDMA1REMM: %x\n", usb_readb(aotg_reg_base + UDMA1REMM));
		pr_err("UDMA1REMH: %x\n", usb_readb(aotg_reg_base + UDMA1REMH));
		pr_err("UDMA2MEMADDR: %x\n", usb_readl(aotg_reg_base + UDMA2MEMADDR));
		pr_err("UDMA2EPSEL: %x\n", usb_readb(aotg_reg_base + UDMA2EPSEL));
		pr_err("UDMA2COM: %x\n", usb_readb(aotg_reg_base + UDMA2COM));
		pr_err("UDMA2CNTL: %x\n", usb_readb(aotg_reg_base + UDMA2CNTL));
		pr_err("UDMA2CNTM: %x\n", usb_readb(aotg_reg_base + UDMA2CNTM));
		pr_err("UDMA2CNTH: %x\n", usb_readb(aotg_reg_base + UDMA2CNTH));
		pr_err("UDMA2REML: %x\n", usb_readb(aotg_reg_base + UDMA2REML));
		pr_err("UDMA2REMM: %x\n", usb_readb(aotg_reg_base + UDMA2REMM));
		pr_err("UDMA2REMH: %x\n", usb_readb(aotg_reg_base + UDMA2REMH));
#endif
	}
	/* UDMA1 transfer complete */
	if (data & 0x1)
		ret |= 0x1;
	/* UDMA2 transfer complete */
	if (data & 0x4)
		ret |= 0x2;

	return ret;
}

int aotg_dma_clear_pend(int dma_nr)
{
	unsigned char data, bit_mask;

	//data = usb_readb(aotg_reg_base + UDMAIRQ);

	/* 0x04: UDMA2; 0x01: UDMA1 */
	bit_mask = (dma_nr) ? 0x04 : 0x01;
	do {
		usb_writeb(bit_mask, aotg_reg_base + UDMAIRQ);
		data = usb_readb(aotg_reg_base + UDMAIRQ);
	} while (data & bit_mask);

	return 0;
}

// TODO: not used!
unsigned int aotg_dma_get_cnt(int dma_nr)
{
	unsigned int data_u32;
#if 0
	unsigned char data_u8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1CNTL);
	data_u32 = (unsigned int)data_u8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1CNTM);
	data_u32 |= (unsigned int)data_u8 << 8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1CNTH);
	data_u32 |= (unsigned int)data_u8 << 16;
#endif
	data_u32 = usb_readl(aotg_dma_reg_base[dma_nr] + UDMA1CNTL);
	data_u32 &= 0x00ffffff;

	return data_u32;
}

// TODO: not used!
unsigned int aotg_dma_get_memaddr(int dma_nr)
{
	unsigned int data;

	data = usb_readl(aotg_dma_reg_base[dma_nr] + UDMA1MEMADDR);
	return data;
}

int aotg_dma_stop(int dma_nr)
{
	unsigned char data;

	data = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1COM);
	data &= ~0x1; //clear 0 to cause finish compulsively.
	data |= 0x1 << 5;
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1COM);

	return 0;
}

unsigned int aotg_dma_get_remain(int dma_nr)
{
	unsigned int data_u32;
#if 0
	unsigned char data_u8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1REML);
	data_u32 = (unsigned int)data_u8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1REMM);
	data_u32 |= (unsigned int)data_u8 << 8;
	data_u8 = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1REMH);
	data_u32 |= (unsigned int)data_u8 << 16;
#endif
	data_u32 = usb_readl(aotg_dma_reg_base[dma_nr] + UDMA1REML);
	data_u32 &= 0x00ffffff;

	return data_u32;
}

int aotg_dma_set_mode(int dma_nr, unsigned char ep_select)
{
	int i = 0;

	usb_writeb(ep_select, aotg_dma_reg_base[dma_nr] + UDMA1EPSEL);
	do {
		i++;
		if (i > 1000) {
			UDC_DMA_DEBUG("%s timeout!\n", __func__);
			break;
		}
		usb_writeb(ep_select, aotg_dma_reg_base[dma_nr] + UDMA1EPSEL);
	} while (usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1EPSEL) != ep_select);

	return 0;
}

int aotg_dma_set_memaddr(int dma_nr, unsigned long addr)
{
	usb_writel(addr, aotg_dma_reg_base[dma_nr] + UDMA1MEMADDR);
	return 0;
}

int aotg_dma_set_cnt(int dma_nr, unsigned long dma_length)
{
	unsigned char data;

	data = (unsigned char)(dma_length & 0xff);
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1CNTL);

	data = (unsigned char)((dma_length >> 8) & 0xff);
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1CNTM);

	data = (unsigned char)((dma_length >> 16) & 0xff);
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1CNTH);

	return 0;
}

int aotg_dma_start(int dma_nr)
{
	unsigned char data;

	data = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1COM);
	data |= 0x1;
	data |= 0x1 << 5;
	usb_writeb(data, aotg_dma_reg_base[dma_nr] + UDMA1COM);

	return 0;
}

unsigned int aotg_dma_get_cmd(unsigned int dma_nr)
{
	unsigned char data;

	data = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1COM);
	return (unsigned int)data;
}

int aotg_dma_request(void *data)
{
	int dma_nr = -1;
	struct aotg_udc *udc = (struct aotg_udc *)data;
	unsigned int base = (unsigned int)udc->regs;

	aotg_reg_base = udc->regs;

	if ((base & 0x03ffffff) == (AOTG0_BASE & 0x03ffffff)) {
		UDC_DMA_DEBUG("AOTG base is 0!\n");
		if ((aotg_dma_map & 0x1) == 0) {
			dma_nr = 0;
			aotg_dma_map |= 0x1;
			/* UDMA1 */
			aotg_dma_reg_base[0] = udc->regs;
			aotg_dma_map |= 0x2;
			/* UDMA2 */
			aotg_dma_reg_base[1] = udc->regs + (UDMA2MEMADDR - UDMA1MEMADDR);
		} else {
			UDC_DMA_DEBUG("%s:%d, err!\n", __func__, __LINE__);
			return -1;
		}
	} else if ((base & 0x03ffffff) == (AOTG1_BASE & 0x03ffffff)) {
		UDC_DMA_DEBUG("AOTG base is 1!\n");
		if ((aotg_dma_map & 0x4) == 0) {
			dma_nr = 2;
			aotg_dma_map |= 0x4;
			/* UDMA1 */
			aotg_dma_reg_base[2] = udc->regs;
			aotg_dma_map |= 0x8;
			/* UDMA2 */
			aotg_dma_reg_base[3] = udc->regs + (UDMA2MEMADDR - UDMA1MEMADDR);
		} else {
			UDC_DMA_DEBUG("%s:%d, err!\n", __func__, __LINE__);
			return -1;
		}
	} else {
		UDC_DMA_DEBUG("%s:%d, err!\n", __func__, __LINE__);
		return -1;
	}

	return dma_nr;
}

void aotg_dma_free(int dma_nr)
{
	if ((dma_nr >= 0) && (dma_nr <= 3)) {
		aotg_dma_map &= ~(0x1 << dma_nr);
	} else {
		UDC_DMA_DEBUG("%s:%d, err!\n", __func__, __LINE__);
	}
	return;
}

/* bit0: start bit of dma; 1: dma start; 0: dma complete. */
unsigned char aotg_dma_is_finish(int dma_nr)
{
	unsigned char data;

	data = aotg_dma_get_cmd(dma_nr);
	data &= 0x01;

	return data;
}

static inline u8 aotg_udma_epsel(int dma_nr)
{
	return usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1EPSEL);
}

static inline u8 aotg_udma_still_on(int dma_nr)
{
	return (usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1COM) & 0x01);
}

void aotg_dma_wait_finish(int dma_nr)
{
	unsigned char data;
	unsigned int cnt = 0;

	data = aotg_dma_is_finish(dma_nr);
	while (data) {
		udelay(1);
		cnt++;
		data = aotg_dma_is_finish(dma_nr);
		if (cnt > 5) {
			unsigned char ep_sel = usb_readb(aotg_dma_reg_base[dma_nr] + UDMA1EPSEL);
			// FIXME: check OUTXZEROPCKIEN first!
			unsigned char irq = usb_readb(aotg_reg_base + OUTXZEROPCKIRQ);
			// count - remain
			int actual = usb_readl(aotg_dma_reg_base[dma_nr] + UDMA1CNTL) - usb_readl(aotg_dma_reg_base[dma_nr] + UDMA1REML);
			// check for ep-out
			unsigned char offset = ((ep_sel - 1) % 2) ? 0 : ((ep_sel - 1) / 2);
			if ((actual == 0) && (irq & (1 << offset))) {
				irq |= (1 << offset);
				usb_writeb(irq, aotg_reg_base + OUTXZEROPCKIRQ);
				//UDC_NOTICE("it is in the zero packet block, get out now\n");
				return;
			}
		}
	}

	if (cnt >= 2)
		printk("udc dma udelay: %d\n", cnt);

	return;
}


void aotg_dma_wait_irq_pending(void)
{
	int data;

	do {
		udelay(1);
		data = aotg_dma_is_irq();
	} while(!data);

	return;
}

MODULE_LICENSE("GPL");
