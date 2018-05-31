/*
 * Actions OWL SoC DMA driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include "virt-dma.h"

#define OWL_DMA_FRAME_MAX_LENGTH	0xffff0

/* global register for dma controller */

#define DMA_IRQ_EN			(0x04)
#define DMA_IRQ_PD			(0x08)
#define DMA_REG_PAUSE			(0x0C)

/* channel register */
#define DMA_CHAN_BASE(i)	(0x100 + (i) * 0x30)
#define DMAX_MODE			(0x00)
#define DMAX_SRC			(0x04)
#define DMAX_DST			(0x08)
#define DMAX_CNT			(0x0C)
#define DMAX_REM			(0x10)
#define DMAX_CMD			(0x14)
#define DMAX_CACHE			(0x18)

/* DMAX_MODE */

#define DMA_MODE_ST(x)			(((x) & 0x1f) << 0)
#define		DMA_MODE_ST_DDR			DMA_MODE_ST(18)
#define DMA_MODE_DT(x)			(((x) & 0x1f) << 16)
#define		DMA_MODE_DT_DDR			DMA_MODE_DT(18)

#define DMA_MODE_SAM(x)			(((x) & 0x1) << 6)
#define		DMA_MODE_SAM_CONST		DMA_MODE_SAM(1)
#define		DMA_MODE_SAM_INC		DMA_MODE_SAM(0)
#define DMA_MODE_DAM(x)			(((x) & 0x1) << 22)
#define		DMA_MODE_DAM_CONST		DMA_MODE_DAM(1)
#define		DMA_MODE_DAM_INC		DMA_MODE_DAM(0)

#define DMA_MODE_UART8BIT			(1 << 30)
#define DMA_MODE_DB32BIT			(0 << 30)
#define DMA_MODE_RELO			(0x1 << 31)


#define DMA_CMD_START			(0x1 << 0)


struct owl_sg {
	dma_addr_t addr;
	dma_addr_t mem_dst;
	u32 len;		/* len*/
};
/**
 * struct owl_dma_txd - wrapper for struct dma_async_tx_descriptor
 * @vd: virtual DMA descriptor
 * @lli_list: link list of children sg's
 * @done: this marks completed descriptors
 * @cyclic: indicate cyclic transfers
 * @sgidx: indicate cur sgidx transfers
 */
struct owl_dma_txd {
	struct virt_dma_desc	vd;
	dma_addr_t dev_addr;
	enum dma_transfer_direction dir;
	bool			done;
	bool			cyclic;
	unsigned sgidx; 
	unsigned sglen;
	struct owl_sg sg[0];
};

/**
 * struct owl_dma_pchan - holder for the physical channels
 * @id: physical index to this channel
 * @base: virtual memory base for the dma channel
 * @lock: a lock to use when altering an instance of this struct
 * @vchan: the virtual channel currently being served by this physical
 * channel
 * @txd_issued: issued count of txd in this physical channel
 * @txd_callback: callback count after txd completed in this physical channel
 * @ts_issued: timestamp of txd issued
 * @ts_callback: timestamp of txd callback
 */
struct owl_dma_pchan {
	u32			id;
	void __iomem		*base;
	struct owl_dma_vchan	*vchan;

	spinlock_t		lock;

	unsigned long		txd_issued;
	unsigned long		txd_callback;

	ktime_t			ts_issued;
	ktime_t			ts_callback;
};

/**
 * struct owl_dma_chan_state - holds the OWL dma specific virtual channel
 * states
 * @OWL_DMA_CHAN_IDLE: the channel is idle
 * @OWL_DMA_CHAN_RUNNING: the channel has allocated a physical transport
 * channel and is running a transfer on it
 * @OWL_DMA_CHAN_PAUSED: the channel has allocated a physical transport
 * channel, but the transfer is currently paused
 * @OWL_DMA_CHAN_WAITING: the channel is waiting for a physical transport
 * channel to become available (only pertains to memcpy channels)
 */
enum owl_dma_chan_state {
	OWL_DMA_CHAN_IDLE,
	OWL_DMA_CHAN_RUNNING,
	OWL_DMA_CHAN_PAUSED,
	OWL_DMA_CHAN_WAITING,
};

/**
 * struct owl_dma_pchan - this structure wraps a DMA ENGINE channel
 * @vc: wrappped virtual channel
 * @pchan: the physical channel utilized by this channel, if there is one
 * @cfg: dma slave config
 * @at: active transaction on this channel
 * @state: whether the channel is idle, paused, running etc
 * @drq: the physical DMA DRQ which this channel is using
 * @txd_issued: issued count of txd in this physical channel
 * @txd_callback: callback count after txd completed in this physical channel
 * @ts_issued: timestamp of txd issued
 * @ts_callback: timestamp of txd callback
 */
struct owl_dma_vchan {
	struct virt_dma_chan	vc;
	struct owl_dma_pchan	*pchan;
	struct dma_slave_config	cfg;
	struct owl_dma_txd	*at;
	enum owl_dma_chan_state state;
	int			drq;

	long			txd_issued;
	long			txd_callback;

	ktime_t			ts_issued;
	ktime_t			ts_callback;
};

/**
 * struct owl_dma - holder for the OWL DMA controller
 * @dma: dma engine for this instance
 * @base: virtual memory base for the DMA controller
 * @clk: clock for the DMA controller
 * @lock: a lock to use when change DMA controller global register
 * @lli_pool: a pool for the LLI descriptors
 * @nr_pchans: the number of physical channels
 * @pchans: array of data for the physical channels
 * @nr_vchans: the number of physical channels
 * @vchans: array of data for the physical channels
 */
struct owl_dma {
	struct dma_device	dma;
	void __iomem		*base;
	struct clk		*clk;
	spinlock_t		lock;

	/* physical dma channels */
	unsigned int		nr_pchans;
	struct owl_dma_pchan	*pchans;

	/* virtual dma channels */
	unsigned int		nr_vchans;
	struct owl_dma_vchan	*vchans;
};

/* for dma debug dump only */
static struct owl_dma *g_od;

static void pchan_writel(struct owl_dma_pchan *pchan, u32 data, u32 reg)
{
	writel(data, pchan->base + reg);
}

static u32 pchan_readl(struct owl_dma_pchan *pchan, u32 reg)
{
	return readl(pchan->base + reg);
}

static void dma_writel(struct owl_dma *od, u32 data, u32 reg)
{
	writel(data, od->base + reg);
}

static u32 dma_readl(struct owl_dma *od, u32 reg)
{
	return readl(od->base + reg);
}

static inline struct owl_dma *to_owl_dma(struct dma_device *dd)
{
	return container_of(dd, struct owl_dma, dma);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct owl_dma_vchan *to_owl_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct owl_dma_vchan, vc.chan);
}

static inline struct owl_dma_txd *
to_owl_txd(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct owl_dma_txd, vd.tx);
}

static inline void owl_dma_dump_com_regs(struct owl_dma *od)
{
	dev_info(od->dma.dev, "Common register:\n"
		"  irqpd:  0x%08x\n"
		"  irqen:  0x%08x\n"
		"  pause:  0x%08x\n",
		dma_readl(od, DMA_IRQ_PD),
		dma_readl(od, DMA_IRQ_EN), 
		dma_readl(od, DMA_REG_PAUSE));
}

static inline void owl_dma_dump_chan_regs(struct owl_dma *od,
					  struct owl_dma_pchan *pchan)
{
	phys_addr_t reg = __virt_to_phys((unsigned long)pchan->base);

	dev_info(od->dma.dev, "Chan %d reg: %pa\n"
		"   mode:  0x%08x, 	 cmd:  0x%08x\n"
		"	src:  0x%08x	 dest: 0x%08x\n"
		"   cnt:  0x%08x	 rem: 0x%08x\n"
		"  cache:  0x%08x\n",
		pchan->id, &reg,
		pchan_readl(pchan, DMAX_MODE),
		pchan_readl(pchan, DMAX_CMD),
		pchan_readl(pchan, DMAX_SRC),
		pchan_readl(pchan, DMAX_DST),
		pchan_readl(pchan, DMAX_CNT),
		pchan_readl(pchan, DMAX_REM),
		pchan_readl(pchan, DMAX_CACHE));
}

static void owl_dma_dump(struct owl_dma *od)
{
	struct owl_dma_pchan *pchan;
	struct owl_dma_vchan *vchan;
	unsigned long flags;
	int i;
		printk("%s: enter \n", __func__);
	spin_lock_irqsave(&od->lock, flags);

	owl_dma_dump_com_regs(od);

	for (i = 0; i < od->nr_pchans; i++) {
		pchan = &od->pchans[i];
		vchan = pchan->vchan;

		pr_info("[pchan %d] issued %ld, callback %ld, last(%lld-%lld=%lld ns), vchan %p(drq:%d)\n",
			i, pchan->txd_issued, pchan->txd_callback,
			ktime_to_ns(pchan->ts_callback),
			ktime_to_ns(pchan->ts_issued),
			ktime_to_ns(ktime_sub(pchan->ts_callback, pchan->ts_issued)),
			vchan, vchan ? vchan->drq : 0);

		owl_dma_dump_chan_regs(od, pchan);
	}

	for (i = 0; i < od->nr_vchans; i++) {
		vchan = &od->vchans[i];
		pchan = vchan->pchan;

		pr_info("[vchan %d] drq %d, issued %ld, callback %ld, last(%lld-%lld=%lld ns), pchan %p(%d)\n",
			i, vchan->drq,
			vchan->txd_issued,
			vchan->txd_callback,
			ktime_to_ns(vchan->ts_callback),
			ktime_to_ns(vchan->ts_issued),
			ktime_to_ns(ktime_sub(vchan->ts_callback, vchan->ts_issued)),
			pchan, pchan ? pchan->id : 0);
	}

	spin_unlock_irqrestore(&od->lock, flags);
}

/*
 * dump all dma channels information for debug
 */
void owl_dma_debug_dump(void)
{
	owl_dma_dump(g_od);
}


/*
 * Allocate a physical channel for a virtual channel
 *
 * Try to locate a physical channel to be used for this transfer. If all
 * are taken return NULL and the requester will have to cope by using
 * some fallback PIO mode or retrying later.
 */
struct owl_dma_pchan *owl_dma_get_pchan(struct owl_dma *od,
					struct owl_dma_vchan *vchan)
{
	struct owl_dma_pchan *pchan;
	unsigned long flags;
	int i;

	for (i = 0; i < od->nr_pchans; i++) {
		pchan = &od->pchans[i];

		spin_lock_irqsave(&pchan->lock, flags);
		if (!pchan->vchan) {
			pchan->vchan = vchan;
			spin_unlock_irqrestore(&pchan->lock, flags);
			break;
		}

		spin_unlock_irqrestore(&pchan->lock, flags);
	}

	if (i == od->nr_pchans) {
		/* No physical channel available, cope with it */
		dev_dbg(od->dma.dev, "No physical channel available for vchan(drq:%d)\n",
			vchan->drq);
		return NULL;
	}

	return pchan;
}

/* Mark the physical channel as free.  Note, this write is atomic. */
static inline void owl_dma_put_pchan(struct owl_dma *od,
				     struct owl_dma_pchan *pchan)
{
	pchan->vchan = NULL;
}



/*
 * owl_dma_terminate_pchan() stops the channel and  clears any pending
 * interrupt status.  This should not be used for an on-going transfer,
 * but as a method of shutting down a channel (eg, when it's no longer used)
 * or terminating a transfer.
 */
static void owl_dma_terminate_pchan(struct owl_dma *od,
				    struct owl_dma_pchan *pchan)
{
	u32 irq_pd;

	pchan_writel(pchan, 0, DMAX_CMD);

	spin_lock(&od->lock);

	dma_writel(od, dma_readl(od, DMA_IRQ_EN) & ~(1 << (pchan->id<<1) ),
			DMA_IRQ_EN);


	irq_pd = dma_readl(od, DMA_IRQ_PD);
	if (irq_pd & (1 << (pchan->id<<1))) {
		dev_warn(od->dma.dev,
			"warning: terminate pchan%d that still "
			"has pending irq (irq_pd:0x%x)\n",
			pchan->id, irq_pd);
		dma_writel(od, (1 << (pchan->id<<1) ), DMA_IRQ_PD);
	}

	spin_unlock(&od->lock);
}

static void owl_dma_pause_pchan(struct owl_dma *od, struct owl_dma_pchan *pchan)
{
	dma_writel(od,  1 << pchan->id, DMA_REG_PAUSE);
}

static void owl_dma_resume_pchan(struct owl_dma *od, struct owl_dma_pchan *pchan)
{
	dma_writel(od,  ~(1 << pchan->id), DMA_REG_PAUSE);
}


static int owl_dma_start_next_sg(struct owl_dma_vchan *vchan)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	struct owl_dma_pchan *pchan = vchan->pchan;
	struct owl_dma_txd *txd ;
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct owl_sg *psg;
	dma_addr_t  src = 0, dst = 0, next_src = 0, next_dst = 0;
	u32  mode, r_mode,  first_tx = 1;
	unsigned long flags, next_idx;

	BUG_ON(pchan == NULL || vchan->at == NULL); 
	txd = vchan->at;
	if (txd->sgidx >= txd->sglen) {
		dev_err(od->dma.dev,"%s: tranfser finshed,sgidx=%d, sglen=%d \n", 
				__func__, txd->sgidx,   txd->sglen);
		return -EINVAL;
	}
	psg = &txd->sg[txd->sgidx];
	if(txd->cyclic) {
		r_mode = pchan_readl(pchan,  DMAX_MODE);
		next_idx = txd->sgidx +1;
		if (next_idx >= txd->sglen)
			next_idx = 0;
		if (DMA_MODE_RELO & r_mode) {
			first_tx = 0;
		}
	}
	
	switch (txd->dir) {
	case DMA_MEM_TO_MEM:
		mode = DMA_MODE_ST_DDR | DMA_MODE_DT_DDR
		| DMA_MODE_SAM_INC | DMA_MODE_DAM_INC;
		src = psg->addr;
		dst =   psg->mem_dst;
		if(txd->cyclic ) {
			next_src = txd->sg[next_idx].addr;
			next_dst =  txd->sg[next_idx].mem_dst ;
		}
		break;
	case DMA_MEM_TO_DEV:
		mode = DMA_MODE_DT(vchan->drq)| DMA_MODE_ST_DDR 
			| DMA_MODE_SAM_INC | DMA_MODE_DAM_CONST;

		/* for uart device */
		if (sconfig->dst_addr_width == DMA_SLAVE_BUSWIDTH_1_BYTE)
			mode |= DMA_MODE_UART8BIT;
		src =  psg->addr;
		dst =  txd->dev_addr;
		if(txd->cyclic ) {
			next_src = txd->sg[next_idx].addr;
			next_dst =  txd->dev_addr;
		}
		break;
	case DMA_DEV_TO_MEM:
		 mode = DMA_MODE_ST(vchan->drq) | DMA_MODE_DT_DDR
			| DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC;

		 dst=  psg->addr;
		 src=  txd->dev_addr;
		if(txd->cyclic ) {
			next_src =  txd->dev_addr;
			next_dst =  txd->sg[next_idx].addr ;
		}
		/* for uart device */
		if (sconfig->src_addr_width == DMA_SLAVE_BUSWIDTH_1_BYTE)
			mode |= DMA_MODE_UART8BIT;

		break;
	default:
		dev_err(od->dma.dev,"%s: bad direction?\n", __func__);
		return -EINVAL;
	}

	if (first_tx) {
		if(txd->cyclic)
			mode |= DMA_MODE_RELO;
		pchan_writel(pchan, mode, DMAX_MODE);
		pchan_writel(pchan, src, DMAX_SRC);
		pchan_writel(pchan, dst, DMAX_DST);
		pchan_writel(pchan, psg->len, DMAX_CNT);
	}

	spin_lock_irqsave(&od->lock, flags);
	dma_writel(od,  1 << (pchan->id<<1),  DMA_IRQ_PD);
	dma_writel(od, dma_readl(od, DMA_IRQ_EN) | (1 << (pchan->id<<1)),
		DMA_IRQ_EN);
	dev_dbg(chan2dev(&vchan->vc.chan), "start pchan%d for vchan(drq:%d)\n",
		pchan->id, vchan->drq);
	spin_unlock_irqrestore(&od->lock, flags);

	if (first_tx) {
		pchan_writel(pchan, DMA_CMD_START, DMAX_CMD);
	}

	if(txd->cyclic)  {// cyclic mode use DMA_MODE_RELO
		r_mode = pchan_readl(pchan,  DMAX_MODE); // for delay, DMA_MODE_RELO, wrtie next addr
		if (!(DMA_MODE_RELO & r_mode)) {
			dev_err(od->dma.dev,"cyclic, %s: relo mode err\n", __func__);
		}
		pchan_writel(pchan, next_src, DMAX_SRC);
		pchan_writel(pchan, next_dst, DMAX_DST);
		pchan_writel(pchan, txd->sg[next_idx].len, DMAX_CNT);
	}

	return 0;
}

static int owl_dma_start_next_txd(struct owl_dma_vchan *vchan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&vchan->vc);
	struct owl_dma_pchan *pchan = vchan->pchan;
	struct owl_dma_txd *txd = to_owl_txd(&vd->tx);

	BUG_ON(pchan == NULL);
	list_del(&vd->node);
	vchan->at = txd;
	pchan->txd_issued++;
	vchan->txd_issued++;
	pchan->ts_issued = ktime_get();
	vchan->ts_issued = ktime_get();
	pchan_writel(pchan, 0, DMAX_MODE); // for reload check
	owl_dma_start_next_sg(vchan);
	
	return 0;
}

static void owl_dma_phy_reassign_start(struct owl_dma *od,
			struct owl_dma_vchan *vchan,
			struct owl_dma_pchan *pchan)
{
	dev_dbg(chan2dev(&vchan->vc.chan), "reassigned pchan%d for vchan(drq:%d)\n",
		pchan->id, vchan->drq);

	/*
	 * We do this without taking the lock; we're really only concerned
	 * about whether this pointer is NULL or not, and we're guaranteed
	 * that this will only be called when it _already_ is non-NULL.
	 */
	pchan->vchan = vchan;
	vchan->pchan = pchan;
	vchan->state = OWL_DMA_CHAN_RUNNING;
	owl_dma_start_next_txd(vchan);
}

/*
 * Free a physical DMA channel, potentially reallocating it to another
 * virtual channel if we have any pending.
 */
static void owl_dma_phy_free(struct owl_dma *od, struct owl_dma_vchan *vchan)
{
	struct owl_dma_vchan *p, *next;

 retry:
	next = NULL;

	/* Find a waiting virtual channel for the next transfer. */
	list_for_each_entry(p, &od->dma.channels, vc.chan.device_node)
		if (p->state == OWL_DMA_CHAN_WAITING) {
			next = p;
			break;
		}

	/* Ensure that the physical channel is stopped */
	owl_dma_terminate_pchan(od, vchan->pchan);

	if (next) {
		bool success;

		/*
		 * Eww.  We know this isn't going to deadlock
		 * but lockdep probably doesn't.
		 */
		spin_lock(&next->vc.lock);
		/* Re-check the state now that we have the lock */
		success = next->state == OWL_DMA_CHAN_WAITING;
		if (success)
			owl_dma_phy_reassign_start(od, next, vchan->pchan);
		spin_unlock(&next->vc.lock);

		/* If the state changed, try to find another channel */
		if (!success)
			goto retry;
	} else {
		/* No more jobs, so free up the physical channel */
		owl_dma_put_pchan(od, vchan->pchan);
	}

	vchan->pchan = NULL;
	vchan->state = OWL_DMA_CHAN_IDLE;
}

static irqreturn_t owl_dma_interrupt(int irq, void *dev_id)
{
	struct owl_dma *od = dev_id;
	struct owl_dma_vchan *vchan;
	struct owl_dma_pchan *pchan;
	unsigned long pending;
	int i;


	spin_lock(&od->lock);
	pending = dma_readl(od, DMA_IRQ_PD);
	dma_writel(od, pending, DMA_IRQ_PD);

	spin_unlock(&od->lock);

	for (i = 0;  i < od->nr_pchans; i++) {
		struct owl_dma_txd *txd;
		if( !( pending & (1<< (i<<1)) ) )
			continue;
		
		pchan = &od->pchans[i];
		vchan = pchan->vchan;
		if (!vchan) {
			dev_warn(od->dma.dev, "No vchan attached on pchan%d\n",
				pchan->id);
			continue;
		}

		spin_lock(&vchan->vc.lock);

		txd = vchan->at;
		if (txd) {
			txd->sgidx++;
			if (txd->cyclic) {
				vchan_cyclic_callback(&txd->vd);
				if( txd->sgidx >= txd->sglen)
					txd->sgidx = 0;
				owl_dma_start_next_sg(vchan);
			}
			else {
				/* for debug only */
				if (pchan_readl(pchan, DMAX_REM)) {
					dev_warn(od->dma.dev,
						"%s: warning: terminate pchan%d that still "
						"busy(rlen %x)\n",
						__func__,
						pchan->id,
						pchan_readl(pchan, DMAX_REM));
					owl_dma_dump(od);
					BUG();
				}
				if( txd->sgidx >= txd->sglen) {
					vchan->at = NULL;
					txd->done = true;
					vchan_cookie_complete(&txd->vd);

					/*
					 * And start the next descriptor (if any),
					 * otherwise free this channel.
					 */
					if (vchan_next_desc(&vchan->vc))
						owl_dma_start_next_txd(vchan);
					else
						owl_dma_phy_free(od, vchan);
				} else {
					owl_dma_start_next_sg(vchan);
				}
			}
		}

		vchan->txd_callback++;
		vchan->ts_callback = ktime_get();
		spin_unlock(&vchan->vc.lock);
	}

	return IRQ_HANDLED;
}

static void owl_dma_free_txd(struct owl_dma *od, struct owl_dma_txd *txd)
{
	if (unlikely(!txd))
		return;
	kfree(txd);
}

static void owl_dma_desc_free(struct virt_dma_desc *vd)
{
	struct owl_dma *od = to_owl_dma(vd->tx.chan->device);
	struct owl_dma_txd *txd = to_owl_txd(&vd->tx);

	owl_dma_free_txd(od, txd);
}

static int owl_dma_terminate_all(struct owl_dma_vchan *vchan)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	unsigned long flags;
	LIST_HEAD(head);

	dev_dbg(chan2dev(&vchan->vc.chan), "%s: vchan(drq:%d), pchan(id:%d)\n",
		__func__,
		vchan->drq,
		vchan->pchan ? vchan->pchan->id : -1);

	spin_lock_irqsave(&vchan->vc.lock, flags);
	if (!vchan->pchan && !vchan->at) {
		spin_unlock_irqrestore(&vchan->vc.lock, flags);
		return 0;
	}

	vchan->state = OWL_DMA_CHAN_IDLE;

	if (vchan->pchan) {
		/* Mark physical channel as free */
		owl_dma_phy_free(od, vchan);
	}

	/* Dequeue jobs and free LLIs */
	if (vchan->at) {
		owl_dma_desc_free(&vchan->at->vd);
		vchan->at = NULL;
	}

	/* Dequeue jobs not yet fired as well */
	vchan_get_all_descriptors(&vchan->vc, &head);
	vchan_dma_desc_free_list(&vchan->vc, &head);

	/*
	 * clear cyclic dma descriptor to avoid using invalid cyclic pointer
	 * in vchan_complete()
	 */
	vchan->vc.cyclic = NULL;

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int owl_dma_pause(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	/*
	 * Anything succeeds on channels with no physical allocation and
	 * no queued transfers.
	 */
	spin_lock_irqsave(&vchan->vc.lock, flags);
	if (!vchan->pchan && !vchan->at) {
		spin_unlock_irqrestore(&vchan->vc.lock, flags);
		return 0;
	}

	owl_dma_pause_pchan(to_owl_dma(vchan->vc.chan.device), vchan->pchan);
	vchan->state = OWL_DMA_CHAN_PAUSED;

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int owl_dma_resume(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	dev_dbg(chan2dev(chan), "vchan %p: resume\n", &vchan->vc);

	/*
	 * Anything succeeds on channels with no physical allocation and
	 * no queued transfers.
	 */
	spin_lock_irqsave(&vchan->vc.lock, flags);
	if (!vchan->pchan && !vchan->at) {
		spin_unlock_irqrestore(&vchan->vc.lock, flags);
		return 0;
	}

	owl_dma_resume_pchan(to_owl_dma(vchan->vc.chan.device), vchan->pchan);
	vchan->state = OWL_DMA_CHAN_RUNNING;

	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int owl_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			   unsigned long arg)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	int ret = 0;

	dev_dbg(chan2dev(chan), "%s: cmd %d\n", __func__, cmd);

	switch (cmd) {
	case DMA_RESUME:
		owl_dma_resume(chan);
		break;

	case DMA_PAUSE:
		owl_dma_pause(chan);
		break;

	case DMA_TERMINATE_ALL:
		ret = owl_dma_terminate_all(vchan);
		break;
	case DMA_SLAVE_CONFIG:
		memcpy(&vchan->cfg, (void *)arg,
			sizeof(struct dma_slave_config));
		break;
	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static enum dma_status owl_dma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie,
		struct dma_tx_state *state)
{

	struct owl_dma_vchan *vchan = to_owl_vchan(chan);

	struct virt_dma_desc *vd;
	struct owl_dma_txd *txd;
	enum dma_status ret;
	unsigned long flags;
	size_t bytes = 0;
	int i;

	ret = dma_cookie_status(chan, cookie, state);
	if (ret == DMA_SUCCESS)
		return ret;

	/*
	 * There's no point calculating the residue if there's
	 * no txstate to store the value.
	 */
	if (!state) {
		if (vchan->state == OWL_DMA_CHAN_PAUSED)
			ret = DMA_PAUSED;
		return ret;
	}

	spin_lock_irqsave(&vchan->vc.lock, flags);
	ret = dma_cookie_status(chan, cookie, state);
	if (ret != DMA_SUCCESS) {
		vd = vchan_find_desc(&vchan->vc, cookie);
		if (vd) {
			/* On the issued list, so hasn't been processed yet */
			txd = to_owl_txd(&vd->tx);			
			for (i = 0; i < txd->sglen; i++) 
				bytes += txd->sg[i].len;

		} else {
			txd = vchan->at;
			if (!vchan->pchan || !txd) {
				bytes = 0;
			} else {
				bytes = pchan_readl(vchan->pchan, DMAX_REM);
				for (i = txd->sgidx+1; i < txd->sglen; i++) 
					bytes += txd->sg[i].len;
			}
		}
	}
	spin_unlock_irqrestore(&vchan->vc.lock, flags);
	/*
	 * This cookie not complete yet
	 * Get number of bytes left in the active transactions and queue
	 */
	dma_set_residue(state, bytes);

	if (vchan->state == OWL_DMA_CHAN_PAUSED && ret == DMA_IN_PROGRESS)
		ret = DMA_PAUSED;

	/* Whether waiting or running, we're in progress */
	return ret;
}

/*
 * Try to allocate a physical channel.  When successful, assign it to
 * this virtual channel, and initiate the next descriptor.  The
 * virtual channel lock must be held at this point.
 */
static void owl_dma_phy_alloc_and_start(struct owl_dma_vchan *vchan)
{
	struct owl_dma *od = to_owl_dma(vchan->vc.chan.device);
	struct owl_dma_pchan *pchan;

	pchan = owl_dma_get_pchan(od, vchan);
	if (!pchan) {
		dev_warn(od->dma.dev, "no physical channel available for xfer on vchan(%d)\n",
			vchan->drq);
		vchan->state = OWL_DMA_CHAN_WAITING;
		return;
	}

	dev_dbg(od->dma.dev, "allocated pchan%d for vchan(drq:%d)\n",
		pchan->id, vchan->drq);

	vchan->pchan = pchan;
	vchan->state = OWL_DMA_CHAN_RUNNING;
	owl_dma_start_next_txd(vchan);
}

/*
 * Slave transactions callback to the slave device to allow
 * synchronization of slave DMA signals with the DMAC enable
 */
static void owl_dma_issue_pending(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);
	if (vchan_issue_pending(&vchan->vc)) {
		if (!vchan->pchan && vchan->state != OWL_DMA_CHAN_WAITING)
			owl_dma_phy_alloc_and_start(vchan);
	}
	spin_unlock_irqrestore(&vchan->vc.lock, flags);
}

static struct dma_async_tx_descriptor *owl_dma_prep_memcpy(
		struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct owl_dma_txd *txd;
	size_t offset, bytes;
	int i;
	unsigned int sg_len;

	if (!len)
		return NULL;
	sg_len = (len+OWL_DMA_FRAME_MAX_LENGTH-1)/OWL_DMA_FRAME_MAX_LENGTH;

	txd = kzalloc(sizeof(*txd)+ sg_len * sizeof(txd->sg[0]), GFP_NOWAIT);
	if (!txd)
		return NULL;	

	txd->dir = DMA_MEM_TO_MEM;
	txd->sglen = sg_len;
	i = 0;
	for (offset = 0; offset < len; offset += bytes) {
		bytes = min_t(size_t, len - offset, OWL_DMA_FRAME_MAX_LENGTH);
		txd->sg[i].addr = offset + src;
		txd->sg[i].mem_dst = offset + dst;
		txd->sg[i].len = bytes;
		i++;
	}
	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);
}

static struct dma_async_tx_descriptor *owl_dma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction dir,
		unsigned long flags, void *context)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct owl_dma_txd *txd;
	int i;
	struct scatterlist *sg;

	if (unlikely(!sgl || !sg_len))
		return NULL;

	txd = kzalloc(sizeof(*txd)+ sg_len * sizeof(txd->sg[0]), GFP_NOWAIT);
	if (!txd)
		return NULL;

	if (dir == DMA_MEM_TO_DEV) {
		txd->dev_addr = sconfig->dst_addr;
	} else if (dir == DMA_DEV_TO_MEM) {
		txd->dev_addr = sconfig->src_addr;
	}else {
		dev_err(od->dma.dev,"%s: bad direction?\n", __func__);
		goto err_txd_free;
	}
	txd->dir = dir;
	for_each_sg(sgl, sg, sg_len, i) {
		txd->sg[i].addr = sg_dma_address(sg);
		txd->sg[i].len = sg_dma_len(sg);
	}
	txd->sglen = sg_len;

	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	owl_dma_free_txd(od, txd);
	return NULL;
}

/**
 * owl_prep_dma_cyclic - prepare the cyclic DMA transfer
 * @chan: the DMA channel to prepare
 * @buf_addr: physical DMA address where the buffer starts
 * @buf_len: total number of bytes for the entire buffer
 * @period_len: number of bytes for each period
 * dir: transfer direction, to or from device
 * @context: transfer context (ignored)
 */
static struct dma_async_tx_descriptor *
owl_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction dir,
		unsigned long flags, void *context)
{
	struct owl_dma *od = to_owl_dma(chan->device);
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct owl_dma_txd *txd;
	int  i;
	unsigned int periods = buf_len / period_len;

	txd = kzalloc(sizeof(*txd)+ periods * sizeof(txd->sg[0]), GFP_NOWAIT);
	if (!txd)
		return NULL;
	if (dir == DMA_MEM_TO_DEV) {
		txd->dev_addr = sconfig->dst_addr;
	} else if (dir == DMA_DEV_TO_MEM) {
		txd->dev_addr = sconfig->src_addr;
	}else {
		dev_err(od->dma.dev,"%s: bad direction?\n", __func__);
		goto err_txd_free;
	}
	txd->dir = dir;
	txd->cyclic = true;
	txd->sglen = periods;
	for (i = 0; i < periods; i++) {
		txd->sg[i].addr = buf_addr + (period_len * i);
		txd->sg[i].len = period_len;
	}
	return vchan_tx_prep(&vchan->vc, &txd->vd, flags);

err_txd_free:
	owl_dma_free_txd(od, txd);
	return NULL;
}

static int owl_dma_alloc_chan_resources(struct dma_chan *chan)
{
	return 0;
}

static void owl_dma_free_chan_resources(struct dma_chan *chan)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);

	/* Ensure all queued descriptors are freed */
	vchan_free_chan_resources(&vchan->vc);
}

static inline void owl_dma_free(struct owl_dma *od)
{
	struct owl_dma_vchan *vchan = NULL;
	struct owl_dma_vchan *next;

	list_for_each_entry_safe(vchan,
				 next, &od->dma.channels, vc.chan.device_node) {
		list_del(&vchan->vc.chan.device_node);
		tasklet_kill(&vchan->vc.task);
	}
}

struct owl_dma_of_filter_args {
	struct owl_dma *od;
	unsigned int drq;
};

static bool owl_dma_of_filter(struct dma_chan *chan, void *param)
{
	struct owl_dma_vchan *vchan = to_owl_vchan(chan);
	struct owl_dma_of_filter_args *fargs = param;

	/* Ensure the device matches our channel */
	if (chan->device != &fargs->od->dma)
			return false;

	vchan->drq = fargs->drq;

	return true;
}

static struct dma_chan *
owl_of_dma_simple_xlate(struct of_phandle_args *dma_spec,
		struct of_dma *ofdma)
{
	struct owl_dma *od = ofdma->of_dma_data;
	unsigned int drq = dma_spec->args[0];
	struct owl_dma_of_filter_args fargs = {
		.od = od,
	};
	dma_cap_mask_t cap;
	printk("owl_of_dma_simple_xlate 1\n");
	//if (drq > od->nr_vchans)
		//return NULL;

	fargs.drq = drq;
	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);

	/*
	 * for linux3.14+
	 * dma_get_slave_channel(&(od->vchans[request].vc.chan));
	 */
	return dma_request_channel(cap, owl_dma_of_filter, &fargs);
}

static int owl_dma_debugfs_show(struct seq_file *s, void *unused)
{
	struct owl_dma *od = s->private;

	owl_dma_dump(od);

	return 0;
}

static int owl_dma_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, owl_dma_debugfs_show, inode->i_private);
}

static const struct file_operations owl_dma_debugfs_operations = {
	.open = owl_dma_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int owl_dma_register_dbgfs(struct owl_dma *od)
{
	(void) debugfs_create_file(dev_name(od->dma.dev),
			S_IFREG | S_IRUGO, NULL, od,
			&owl_dma_debugfs_operations);

	return 0;
}


static int owl_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct owl_dma *od;
	struct resource *res;
	int ret, i, irq, nr_channels, nr_requests;

	pr_info("[ATS3605 dma] initialize controller\n");

	od = devm_kzalloc(&pdev->dev, sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	od->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(od->base))
		return PTR_ERR(od->base);

	ret = of_property_read_u32(np, "dma-channels", &nr_channels);
	if (ret) {
		dev_err(&pdev->dev, "Can't get dma-channels.\n");
		return ret;
	}

	ret = of_property_read_u32(np, "dma-requests", &nr_requests);
	if (ret) {
		dev_err(&pdev->dev, "Can't get dma-requests.\n");
		return ret;
	}
	dev_info(&pdev->dev, "dma-channels %d, dma-requests %d\n",
		nr_channels, nr_requests);

	od->nr_pchans = nr_channels;
	od->nr_vchans = nr_requests;

	if (!pdev->dev.dma_mask) {
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	}

	platform_set_drvdata(pdev, od);
	spin_lock_init(&od->lock);

	dma_cap_set(DMA_MEMCPY, od->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, od->dma.cap_mask);
	dma_cap_set(DMA_CYCLIC, od->dma.cap_mask);
	od->dma.dev = &pdev->dev;
	od->dma.device_alloc_chan_resources = owl_dma_alloc_chan_resources;
	od->dma.device_free_chan_resources = owl_dma_free_chan_resources;
	od->dma.device_tx_status = owl_dma_tx_status;
	od->dma.device_issue_pending = owl_dma_issue_pending;
	od->dma.device_prep_dma_memcpy = owl_dma_prep_memcpy;
	od->dma.device_prep_slave_sg = owl_dma_prep_slave_sg;
	od->dma.device_prep_dma_cyclic = owl_prep_dma_cyclic;
	od->dma.device_control = owl_dma_control;
	INIT_LIST_HEAD(&od->dma.channels);

	od->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(od->clk)) {
		dev_err(&pdev->dev, "Can't get clock");
		return PTR_ERR(od->clk);
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, owl_dma_interrupt, 0,
			dev_name(&pdev->dev), od);
	if (ret) {
		dev_err(&pdev->dev, "Can't request IRQ\n");
		return ret;
	}

	/* init physical channel */
	od->pchans = devm_kzalloc(&pdev->dev,
		od->nr_pchans * sizeof(struct owl_dma_pchan), GFP_KERNEL);
	if (od->pchans == NULL)
		return -ENOMEM;

	for (i = 0; i < od->nr_pchans; i++) {
		struct owl_dma_pchan *pchan = &od->pchans[i];

		pchan->id = i;
		pchan->base = od->base + DMA_CHAN_BASE(i);

		spin_lock_init(&pchan->lock);
	}

	/* init virtual channel */
	od->vchans = devm_kzalloc(&pdev->dev,
		od->nr_vchans * sizeof(struct owl_dma_vchan), GFP_KERNEL);
	if (od->vchans == NULL)
		return -ENOMEM;

	for (i = 0; i < od->nr_vchans; i++) {
		struct owl_dma_vchan *vchan = &od->vchans[i];

		vchan->vc.desc_free = owl_dma_desc_free;
		vchan_init(&vchan->vc, &od->dma);
		vchan->drq = -1;
		vchan->state = OWL_DMA_CHAN_IDLE;
	}

	clk_prepare_enable(od->clk);

	ret = dma_async_device_register(&od->dma);
	if (ret) {
		dev_err(&pdev->dev, "failed to register DMA engine device\n");
		goto err_pool_free;
	}

	/* Device-tree DMA controller registration */
	ret = of_dma_controller_register(pdev->dev.of_node,
			owl_of_dma_simple_xlate, od);
	if (ret) {
		dev_err(&pdev->dev, "of_dma_controller_register failed\n");
		goto err_dma_unregister;
	}

	owl_dma_register_dbgfs(od);

	g_od = od;

	return 0;

err_dma_unregister:
	dma_async_device_unregister(&od->dma);
err_pool_free:
	clk_disable_unprepare(od->clk);
	return ret;
}

static int owl_dma_remove(struct platform_device *pdev)
{
	struct owl_dma *od = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&od->dma);

	/* mask all interrupts for this execution environment */
	dma_writel(od, 0x0, DMA_IRQ_EN);
	owl_dma_free(od);

	clk_disable_unprepare(od->clk);

	return 0;
}

static const struct of_device_id owl_dma_match[] = {
	{ .compatible = "actions,ats3605-dma",},
	{},
};
MODULE_DEVICE_TABLE(of, owl_dma_match);

static struct platform_driver owl_dma_driver = {
	.probe	= owl_dma_probe,
	.remove	= owl_dma_remove,
	.driver = {
		.name = "dma-owl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(owl_dma_match),
	},
};

static int owl_dma_init(void)
{
	return platform_driver_register(&owl_dma_driver);
}
subsys_initcall(owl_dma_init);

static void __exit owl_dma_exit(void)
{
	platform_driver_unregister(&owl_dma_driver);
}
module_exit(owl_dma_exit);

MODULE_AUTHOR("liaotianyang <liaotianyang@actions-semi.com>");
MODULE_DESCRIPTION("Actions ATS3605 SoC DMA driver");
MODULE_LICENSE("GPL");
