/*
 * Actions OWL SoCs SPI master controller driver
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/ctype.h>

#ifdef CONFIG_DMA_ATS3605
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <dt-bindings/dma/dma-ats3605.h>
#include <mach/hardware.h>
#endif

/* SPI controller registers */
#define SPI_CTL				0x00
#define SPI_CLKDIV			0x04
#define SPI_STAT			0x08
#define SPI_RXDAT			0x0C
#define SPI_TXDAT			0x10
#define SPI_TCNT			0x14
#define SPI_SEED			0x18
#define SPI_TXCR			0x1C
#define SPI_RXCR			0x20

#ifdef CONFIG_DMA_ATS3605
#define BDMA_MODE               0x0
#define BDMA_SRC                0x4
#define BDMA_DST                0x8
#define BDMA_CNT                0xc
#define BDMA_REM                0x10
#define BDMA_CMD                0x14
#define BDMA_CACHE              0x18
#define DMA_CHANNEL             0
#endif

/* SPI_CTL */
#define SPI_CTL_SDT_MASK		(0x7 << 29)
#define SPI_CTL_SDT(x)			(((x) & 0x7) << 29)
#define SPI_CTL_BM				(0x1 << 28)
#define SPI_CTL_GM				(0x1 << 27)
#define SPI_CTL_CEB				(0x1 << 26)
#define SPI_CTL_RANEN(x)		(0x1 << 24)
#define SPI_CTL_RDIC_MASK		(0x3 << 22)
#define SPI_CTL_RDIC(x)			(((x) & 0x3) << 22)
#define SPI_CTL_TDIC_MASK		(0x3 << 20)
#define SPI_CTL_TDIC(x)			(((x) & 0x3) << 20)
#define SPI_CTL_TWME			(0x1 << 19)
#define SPI_CTL_EN				(0x1 << 18)
#define SPI_CTL_RWC_MASK		(0x3 << 16)
#define SPI_CTL_RWC(x)			(((x) & 0x3) << 16)
#define SPI_CTL_DTS			(0x1 << 15)
#define SPI_CTL_SSATEN			(0x1 << 14)
#define SPI_CTL_DM_MASK			(0x3 << 12)
#define SPI_CTL_DM(x)			(((x) & 0x3) << 12)
#define SPI_CTL_LBT			(0x1 << 11)
#define SPI_CTL_MS			(0x1 << 10)
#define SPI_CTL_DAWS_MASK		(0x3 << 8)
#define SPI_CTL_DAWS(x)			(((x) & 0x3) << 8)
#define		SPI_CTL_DAWS_8BIT		(SPI_CTL_DAWS(0))
#define		SPI_CTL_DAWS_16BIT		(SPI_CTL_DAWS(1))
#define		SPI_CTL_DAWS_32BIT		(SPI_CTL_DAWS(2))
#define SPI_CTL_CPOS_MASK		(0x3 << 6)
#define SPI_CTL_CPOS(x)			(((x) & 0x3) << 6)
#define		SPI_CTL_CPOS_CPHA		(0x1 << 7)
#define		SPI_CTL_CPOS_CPOL		(0x1 << 6)
#define SPI_CTL_LMFS				(0x1 << 5)
#define SPI_CTL_SSCO				(0x1 << 4)
#define SPI_CTL_TIEN				(0x1 << 3)
#define SPI_CTL_RIEN				(0x1 << 2)
#define SPI_CTL_TDEN				(0x1 << 1)
#define SPI_CTL_RDEN				(0x1 << 0)

/* SPI_CLKDIV */
#define SPI_CLKDIV_CLKDIV_MASK		(0x3FF << 0)
#define SPI_CLKDIV_CLKDIV(x)		(((x) & 0x3FF) << 0)
#define MAX_SPI_DMA_LEN			15*1024
#define MAX_SPI_POLL_LOOPS		5000

/******************************************************************************/
/*SPI_STAT*/
/*bit 10-31 Reserved*/
#define SPI_STAT_TFEM			(0x1 << 9)
#define SPI_STAT_RFFU			(0x1 << 8)
#define SPI_STAT_TFFU			(0x1 << 7)
#define SPI_STAT_RFEM			(0x1 << 6)
#define SPI_STAT_TFER			(0x1 << 5)
#define SPI_STAT_RFER			(0x1 << 4)
#define SPI_STAT_BEB			(0x1 << 3)
#define SPI_STAT_TCOM			(0x1 << 2)
#define SPI_STAT_TIP			(0x1 << 1)
#define SPI_STAT_PIP			(0x1 << 0)

#define msecs_to_loops(t)		(loops_per_jiffy / 1000 * HZ * t)

struct owl_spi_data {
	struct spi_master	*master;
    struct device *dev;
	struct clk		*clk;
	void __iomem		*base;
	unsigned long		phys;

	unsigned int		cur_speed;
	unsigned int		cur_mode;
	unsigned int		cur_bits_per_word;

	struct completion	xfer_completion;

#ifdef CONFIG_DMA_ATS3605
	u8 enable_dma;
	struct dma_chan			*dma_rx_channel;
	struct dma_chan			*dma_tx_channel;
	struct sg_table			sgt_rx;
	struct sg_table			sgt_tx;
	bool				dma_running;
#endif
};

#ifdef CONFIG_DMA_ATS3605
#define DMA_MODE_ST(x)			(((x) & 0x1f) << 0)
#define		DMA_MODE_ST_DDR		DMA_MODE_ST(18)
#define DMA_MODE_DT(x)			(((x) & 0x1f) << 16)
#define		DMA_MODE_DT_DDR		DMA_MODE_DT(18)
#define DMA_MODE_SAM(x)			(((x) & 0x1) << 6)
#define		DMA_MODE_SAM_CONST	DMA_MODE_SAM(1)
#define		DMA_MODE_SAM_INC	DMA_MODE_SAM(0)
#define DMA_MODE_DAM(x)			(((x) & 0x1) << 22)
#define		DMA_MODE_DAM_CONST	DMA_MODE_DAM(1)
#define		DMA_MODE_DAM_INC	DMA_MODE_DAM(0)

#define STOP_DMA(dmaNo)            \
{ \
    act_writel( act_readl(BDMA0_BASE + (0x30 * dmaNo) + BDMA_CMD) & (~0x1), BDMA0_BASE + (0x30 * dmaNo) + BDMA_CMD ); \
}

#define RESET_DMA(dmaNo)    STOP_DMA(dmaNo)

#define START_DMA(dmaNo)            \
{ \
    act_writel( 0x1, BDMA0_BASE + (0x30 * dmaNo) + BDMA_CMD ); \
}

#define SET_DMA_COUNT(dmaNo, count)        \
{ \
    act_writel(count & 0x1FFFFFF, BDMA0_BASE + (0x30 * dmaNo) + BDMA_CNT); \
}

#define SET_DMA_SRC_ADDR(dmaNo, addrForDma)        \
{ \
    act_writel(addrForDma, BDMA0_BASE + (0x30 * dmaNo) + BDMA_SRC); \
}

#define SET_DMA_DST_ADDR(dmaNo, addrForDma)        \
{ \
    act_writel(addrForDma, BDMA0_BASE + (0x30 * dmaNo) + BDMA_DST); \
}

#define SET_DMA_MODE(dmaNo, mode)        \
{ \
    act_writel(mode, BDMA0_BASE + (0x30 * dmaNo) + BDMA_MODE); \
}

//Dma Read optimize switch
//#define DMA_DIRECT
//#define DMA_SINGLE
#define DMA_SG

struct owl_dma_slave {
	struct device		*dma_dev;
	u32			mode;
};
#endif

static inline unsigned int owl_spi_readl(struct owl_spi_data *aspi,
			unsigned int reg)
{
	return __raw_readl(aspi->base + reg);
}

static inline void owl_spi_writel(struct owl_spi_data *aspi,
		unsigned int val, unsigned int reg)
{
	__raw_writel(val, aspi->base + reg);
}

static void owl_spi_dump_regs(struct owl_spi_data *aspi)
{
	pr_debug("dump phys %08x regs:\n"
		"  ctl:      %.8x  clkdiv: %.8x  stat:    %.8x\n",
		(u32)aspi->phys,
		owl_spi_readl(aspi, SPI_CTL),
		owl_spi_readl(aspi, SPI_CLKDIV),
		owl_spi_readl(aspi, SPI_STAT));
}

//#define DUMP_MEM
#ifdef DUMP_MEM
static void owl_spi_dump_mem(char *label, const void *base, int len)
{
	int i, j;
	char *data = base;
	char buf[10], line[80];

	pr_debug("%s: dump of %d bytes of data at 0x%p\n",
		label, len, data);

	for (i = 0; i < len; i += 16) {
		sprintf(line, "%.8x: ", i);
		for (j = 0; j < 16; j++) {
			if ((i + j < len))
				sprintf(buf, "%02x ", data[i + j]);
			else
				sprintf(buf, "   ");
			strcat(line, buf);
		}
		strcat(line, " ");
		buf[1] = 0;
		for (j = 0; (j < 16) && (i + j < len); j++) {
			buf[0] = isprint(data[i + j]) ? data[i + j] : '.';
			strcat(line, buf);
		}
		pr_debug("%s\n", line);
	}
}
#else
static void owl_spi_dump_mem(char *label, const void *base, int len)
{
}
#endif

static void enable_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;

	if (gpio_is_valid(spi->cs_gpio))
		gpio_direction_output(spi->cs_gpio, 0);

	/* enable spi controller */
	val = owl_spi_readl(aspi, SPI_CTL);
	val &= ~SPI_CTL_SSCO;
    //Notice:It should not set SPI_CTL_EN here.
	//val |= SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);
}

static void disable_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;

	if (gpio_is_valid(spi->cs_gpio))
		gpio_direction_output(spi->cs_gpio, 1);

	/* disable spi controller */
	val = owl_spi_readl(aspi, SPI_CTL);
	val |= SPI_CTL_SSCO;
    //Notice:It should not set SPI_CTL_EN here.
	//val &= ~SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);
}

static int owl_spi_setup_cs(struct spi_device *spi)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(spi->master);
	unsigned int val;
	int ret;

	if (gpio_is_valid(spi->cs_gpio)) {
		ret = gpio_request_one(spi->cs_gpio, GPIOF_OUT_INIT_HIGH,
				dev_name(&spi->dev));
		if (ret) {
			dev_err(&spi->dev,
				"Failed to get /CS gpio [%d]: %d\n",
				spi->cs_gpio, ret);
			return ret;
		}
	}

	val = owl_spi_readl(aspi, SPI_CTL);
	val |= SPI_CTL_SSCO;
    //Notice:It should not set SPI_CTL_EN here.
	//val &= ~SPI_CTL_EN;
	owl_spi_writel(aspi, val, SPI_CTL);

	return 0;
}

static void owl_spi_cleanup_cs(struct spi_device *spi)
{
	if (gpio_is_valid(spi->cs_gpio))
		gpio_free(spi->cs_gpio);
}

static int owl_spi_baudrate_set(struct owl_spi_data *aspi, unsigned int speed)
{
	u32 spi_source_clk_hz;
	u32 clk_div;

	spi_source_clk_hz = clk_get_rate(aspi->clk);

	/* setup SPI clock register */
	clk_div = (spi_source_clk_hz + (2 * speed) - 1) / (speed) / 2;
	if (clk_div == 0)
		clk_div = 1;

	pr_debug("owl_spi: required speed = %d\n", speed);
	pr_debug("owl_spi: spi clock = %d KHz(hclk = %d,clk_div = %d)\n",
		spi_source_clk_hz / (clk_div * 2) / 1000,
		spi_source_clk_hz, clk_div);

	owl_spi_writel(aspi, SPI_CLKDIV_CLKDIV(clk_div), SPI_CLKDIV);

	return 0;
}

static inline int owl_spi_config(struct owl_spi_data *aspi)
{
	unsigned int val, mode;

	mode = aspi->cur_mode;
	val = owl_spi_readl(aspi, SPI_CTL);

	val &= ~(SPI_CTL_CPOS_MASK | SPI_CTL_LMFS | SPI_CTL_LBT | \
			SPI_CTL_DAWS_MASK | SPI_CTL_CEB);

	if (mode & SPI_CPOL)
		val |= SPI_CTL_CPOS_CPOL;

	if (mode & SPI_CPHA)
		val |= SPI_CTL_CPOS_CPHA;

	if (mode & SPI_LSB_FIRST)
		val |= SPI_CTL_LMFS;
	else
		val |= SPI_CTL_CEB;

	if (mode & SPI_LOOP)
		val |= SPI_CTL_LBT;

	switch (aspi->cur_bits_per_word) {
	case 16:
		val |= SPI_CTL_DAWS_16BIT;
		break;
	case 32:
		val |= SPI_CTL_DAWS_32BIT;
		break;
	default:
		val |= SPI_CTL_DAWS_8BIT;
		break;
	}

#ifdef CONFIG_DMA_ATS3605
    val |= SPI_CTL_CPOS_CPOL;
    val |= SPI_CTL_CPOS_CPHA;
#endif
	owl_spi_writel(aspi, val, SPI_CTL);

	owl_spi_baudrate_set(aspi, aspi->cur_speed);

	return 0;
}

static inline void spi_clear_stat(struct owl_spi_data *aspi)
{
	owl_spi_writel(aspi,
		SPI_STAT_TFER	/* clear the rx FIFO */
		| SPI_STAT_RFER	/* clear the tx FIFO */
		| SPI_STAT_BEB	/* clear the Bus error bit */
		| SPI_STAT_TCOM	/* clear the transfer complete bit */
		| SPI_STAT_TIP	/* clear the tx IRQ pending bit */
		| SPI_STAT_PIP,	/* clear the rx IRQ pending bit */
		SPI_STAT);
}

static inline int owl_spi_wait_tcom(struct owl_spi_data *aspi)
{
	unsigned int stat;
	int timeout;

	/* transfer timeout: 500ms, 8 cycles per loop */
	timeout = msecs_to_loops(500 / 8);

	do {
		stat = owl_spi_readl(aspi, SPI_STAT);
	} while (!(stat & SPI_STAT_TCOM) && timeout--);

	if (timeout < 0) {
		pr_err("Error: spi transfer wait complete timeout\n");
		owl_spi_dump_regs(aspi);
		return -1;
	}

	/* clear transfer complete flag */
	owl_spi_writel(aspi, stat | SPI_STAT_TCOM, SPI_STAT);

	return 0;
}

static int owl_spi_write_read_8bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u8 *tx_buf = xfer->tx_buf;
	u8 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_DAWS_MASK | SPI_CTL_TIEN |
			SPI_CTL_RIEN | SPI_CTL_TDEN | SPI_CTL_RDEN | SPI_CTL_DM_MASK);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(0);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT) & 0xff;

		count -= 1;
	} while (count);

	return 0;
}

static int owl_spi_write_read_16bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u16 *tx_buf = xfer->tx_buf;
	u16 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_DAWS_MASK | SPI_CTL_TIEN |
			SPI_CTL_RIEN | SPI_CTL_TDEN | SPI_CTL_RDEN | SPI_CTL_DM_MASK);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(1);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT) & 0xffff;

		count -= 2;
	} while (count);

	return 0;
}

static int owl_spi_write_read_32bit(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	unsigned int ctl;
	unsigned int count = xfer->len;
	const u32 *tx_buf = xfer->tx_buf;
	u32 *rx_buf = xfer->rx_buf;

	ctl = owl_spi_readl(aspi, SPI_CTL);
	ctl &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK |
			SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN |
			SPI_CTL_TDEN | SPI_CTL_RDEN | SPI_CTL_DM_MASK);
	ctl |= SPI_CTL_RWC(0) | SPI_CTL_DAWS(2);

	if (aspi->cur_speed > 20000000)
		ctl |= SPI_CTL_SDT(1);

	owl_spi_writel(aspi, ctl, SPI_CTL);

	do {
		if (tx_buf)
			owl_spi_writel(aspi, *tx_buf++, SPI_TXDAT);
		else
			owl_spi_writel(aspi, 0, SPI_TXDAT);

		if (owl_spi_wait_tcom(aspi) < 0) {
			pr_err("SPI: TXS timed out\n");
			return -ETIMEDOUT;
		}

		if (rx_buf)
			*rx_buf++ = owl_spi_readl(aspi, SPI_RXDAT);

		count -= 4;
	} while (count);

	return 0;
}

#ifdef CONFIG_DMA_ATS3605
static int owl_spi_get_channel_no(unsigned int base)
{
	switch (base) {
		case SPI0_BASE:
			return 0;
		case SPI1_BASE:
			return 1;
		case SPI2_BASE:
			return 2;
		case SPI3_BASE:
			return 3;
	}
	return -1;
}

static unsigned int owl_spi_get_dma_trig(unsigned int base)
{
	static unsigned int trigs[] = {DMA_DRQ_SPI0, DMA_DRQ_SPI1, DMA_DRQ_SPI2, DMA_DRQ_SPI3};
	
	int spi_no = owl_spi_get_channel_no(base);
	if(spi_no < 0) {
		pr_err("error: 0x%x.spi do not support\n", base);
		return -1;
	}
	
	return trigs[spi_no];
}

static struct page *owl_spi_virt_to_page(const void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	else
		return virt_to_page(addr);
}

static void owl_spi_setup_dma_scatter(void *buffer,
			      unsigned int length,
			      struct sg_table *sgtab)
{
	struct scatterlist *sg;
	int bytesleft = length;
	void *bufp = buffer;
	int mapbytes;
	int i;

	if (buffer) {
		for_each_sg(sgtab->sgl, sg, sgtab->nents, i) {
			if(bytesleft == 0) {
				sg_mark_end(sg);
				sgtab->nents = i;
				break;
			}
			/*
			 * If there are less bytes left than what fits
			 * in the current page (plus page alignment offset)
			 * we just feed in this, else we stuff in as much
			 * as we can.
			 */
			if (bytesleft < (PAGE_SIZE - offset_in_page(bufp)))
				mapbytes = bytesleft;
			else
				mapbytes = PAGE_SIZE - offset_in_page(bufp);
			sg_set_page(sg, owl_spi_virt_to_page(bufp),
				    mapbytes, offset_in_page(bufp));
			bufp += mapbytes;
			bytesleft -= mapbytes;
			//pr_info("set RX/TX target page @ %p, %d bytes, %d left\n",
			//	bufp, mapbytes, bytesleft);
		}
	}
	
	BUG_ON(bytesleft);
}

static void spi_callback(void *completion)
{
	complete(completion);
}

static inline int owl_dma_dump_all(struct dma_chan *chan)
{
	return chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
}

static inline void dump_spi_registers(struct owl_spi_data *aspi)
{
	pr_info("aspi: SPI0_CTL(0x%x) = 0x%x\n", (unsigned int)(aspi->base + SPI_CTL),
	       owl_spi_readl(aspi, SPI_CTL));
	pr_info("aspi: SPI0_STAT(0x%x) = 0x%x\n", (unsigned int)(aspi->base + SPI_STAT),
	       owl_spi_readl(aspi, SPI_STAT));
	pr_info("aspi: SPI0_CLKDIV(0x%x) = 0x%x\n", (unsigned int)(aspi->base + SPI_CLKDIV),
	       owl_spi_readl(aspi, SPI_CLKDIV));
}

int owl_spi_wait_till_ready(struct owl_spi_data *aspi)
{
	int i;

	for (i = 0; i < MAX_SPI_POLL_LOOPS; i++) {
		if (owl_spi_readl(aspi, SPI_STAT) & SPI_STAT_TCOM) {
			owl_spi_writel(aspi, owl_spi_readl(aspi, SPI_STAT) | SPI_STAT_TCOM, SPI_STAT);
			//dump_spi_registers(aspi);
			pr_debug("wait num = %d\n", i);
			return 1;
		}
	}

	dump_spi_registers(aspi);

	return -1;
}

static inline u32 spi_reg(struct owl_spi_data *aspi, u32 reg)
{
	return (unsigned int)(SPI0_BASE + reg);
}

static void print_spi_regs(unsigned int base)
{
    pr_err("SPI0_CTL(0x%x) = 0x%x \n", base+SPI_CTL, act_readl(base+SPI_CTL));
    pr_err("SPI0_CLKDIV(0x%x) = 0x%x \n", base+SPI_CLKDIV, act_readl(base+SPI_CLKDIV));
    pr_err("SPI0_STAT(0x%x) = 0x%x \n", base+SPI_STAT, act_readl(base+SPI_STAT));
    pr_err("SPI0_TCNT(0x%x) = 0x%x \n", base+SPI_TCNT, act_readl(base+SPI_TCNT));
    pr_err("SPI0_SEED(0x%x) = 0x%x \n", base+SPI_SEED, act_readl(base+SPI_SEED));
    pr_err("SPI0_TXCR(0x%x) = 0x%x \n", base+SPI_TXCR, act_readl(base+SPI_TXCR));
    pr_err("SPI0_RXCR(0x%x) = 0x%x \n", base+SPI_RXCR, act_readl(base+SPI_RXCR));
}

static void print_dma_regs()
{
    pr_err("BDMA0_MODE(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_MODE, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_MODE));
    pr_err("BDMA0_SRC(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_SRC, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_SRC));
    pr_err("BDMA0_DST(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_DST, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_DST));
    pr_err("BDMA0_CNT(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CNT, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CNT));
    pr_err("BDMA0_REM(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_REM, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_REM));
    pr_err("BDMA0_CMD(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CMD, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CMD));
    pr_err("BDMA0_CACHE(0x%x) = 0x%x \n", BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CACHE, \
            act_readl(BDMA0_BASE + (0x30 * DMA_CHANNEL) + BDMA_CACHE));
}

static int owl_spi_write_by_dma(struct owl_spi_data *aspi,
	    struct spi_transfer *xfer)
{
    int trig = owl_spi_get_dma_trig((u32)aspi->phys);
	struct dma_slave_config tx_conf = {
		.dst_addr = spi_reg(aspi, SPI_TXDAT),
		.direction = DMA_MEM_TO_DEV,
	};
	struct owl_dma_slave tx_atslave = {
        .mode = DMA_MODE_DT(trig) | DMA_MODE_ST_DDR | DMA_MODE_SAM_INC | DMA_MODE_DAM_CONST,
		.dma_dev = aspi->dma_tx_channel->device->dev,
	};

	struct dma_chan *txchan = aspi->dma_tx_channel;
	unsigned int pages;
	int len, left;
	void *tx_buf;
	int tx_sglen;
	struct dma_async_tx_descriptor *txdesc;
	u32 val;
	int retval;

	struct completion tx_cmp;
	dma_cookie_t		cookie;
	enum dma_status		status;

	/* Create sglists for the transfers */
	left = xfer->len;
	tx_buf = (void*)xfer->tx_buf;
	
	pages = DIV_ROUND_UP(xfer->len, PAGE_SIZE);
	retval = sg_alloc_table(&aspi->sgt_tx, pages, GFP_ATOMIC);
	if (retval)
		goto err_slave;

    txchan->private = (void *)&tx_atslave;
    retval = dmaengine_slave_config(txchan, &tx_conf);
    if (retval) {
    	pr_err("call the write slave config error\n");
    	goto err_slave;
    }

    val = owl_spi_readl(aspi, SPI_CTL);
    val &= (~(SPI_CTL_RWC(3) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(7) | SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | \
    	SPI_CTL_TDEN | SPI_CTL_RDEN) | SPI_CTL_DAWS(3));
    val |= (SPI_CTL_RWC(1) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_TDEN | SPI_CTL_DAWS(2));
    if (xfer->tx_nbits == SPI_NBITS_DUAL)
        val |= (0x1 << 12);
    else
        val &= ~(0x1 << 12);
    owl_spi_writel(aspi, val, SPI_CTL);

	while(left > 0) {
		len = left > MAX_SPI_DMA_LEN ? MAX_SPI_DMA_LEN : left;
		left -= len;
		
        //stat must be clear first, stop the last dma xfer.
        owl_spi_writel(aspi, 0x30 | SPI_STAT_TCOM, SPI_STAT);
	
		owl_spi_writel(aspi, len/4, SPI_TXCR);

		/* Fill in the scatterlists for the TX buffers */
		owl_spi_setup_dma_scatter(tx_buf, len, &aspi->sgt_tx);
		tx_sglen = dma_map_sg(txchan->device->dev, aspi->sgt_tx.sgl,
				   aspi->sgt_tx.nents, DMA_TO_DEVICE);
		if (!tx_sglen)
			goto err_sgmap;

		tx_buf += len;

		/* Send scatterlists */
		txdesc = dmaengine_prep_slave_sg(txchan,
					      aspi->sgt_tx.sgl,
					      tx_sglen,
					      DMA_MEM_TO_DEV,
					      0);
		if (!txdesc)
			goto err_desc;

		init_completion(&tx_cmp);
	
		txdesc->callback = spi_callback;
		txdesc->callback_param = &tx_cmp;

		cookie = dmaengine_submit(txdesc);
		if (dma_submit_error(cookie)) {
			pr_err("submit write error!\n");
			goto err_desc;
		}

		dma_async_issue_pending(txchan);

		if (!wait_for_completion_timeout(&tx_cmp, msecs_to_jiffies(100))) {
			pr_err("wait_for_completion timeout while send by dma\n");
            dmaengine_terminate_all(txchan);
			//owl_dma_dump_all(txchan);
			goto err_desc;
		}

		status = dma_async_is_tx_complete(txchan, cookie, NULL, NULL);
		if (status != DMA_SUCCESS) {
			pr_err("transfer not succeed\n");
			goto err_desc;
		}

		dma_unmap_sg(txchan->device->dev, aspi->sgt_tx.sgl,
			     aspi->sgt_tx.nents, DMA_TO_DEVICE);
	}
	sg_free_table(&aspi->sgt_tx);
	return 0;

err_desc:
	dmaengine_terminate_all(txchan);
err_sgmap:
	sg_free_table(&aspi->sgt_tx);
err_slave:
	return -EINVAL;
}

#ifdef DMA_DIRECT
//Test success, about 11M/s.
static int owl_spi_read_by_dma(struct owl_spi_data *aspi,
			   struct spi_transfer *xfer)
{
	void *rx_buf;
    int len, left, devid, trig;
	unsigned int val, mode;
    dma_addr_t rx_addr;

	left = xfer->len;
    rx_buf = xfer->rx_buf;

    val = owl_spi_readl(aspi, SPI_CTL);
    val &= (~(SPI_CTL_RWC(3) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(7) | SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | \
    	SPI_CTL_TDEN | SPI_CTL_RDEN) | SPI_CTL_DAWS(3));
    val |= (SPI_CTL_RWC(2) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(1) | SPI_CTL_DTS | SPI_CTL_RDEN | \
        SPI_CTL_DAWS(2));

    //Support for dual wire read.
    if (xfer->rx_nbits == SPI_NBITS_DUAL)
        val |= (0x1 << 12);
    else
        val &= ~(0x1 << 12);
    owl_spi_writel(aspi, val, SPI_CTL);

    devid = aspi->dma_rx_channel->dev->dev_id;
    trig = owl_spi_get_dma_trig((u32)aspi->phys);

    if (rx_buf) {
        while (left > 0) {
		    len = left > MAX_SPI_DMA_LEN ? MAX_SPI_DMA_LEN : left;
            mode = DMA_MODE_ST(trig) | DMA_MODE_DT_DDR | DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC;
            rx_addr = dma_map_single(aspi->dma_rx_channel->device->dev, xfer->rx_buf + (xfer->len - left), len, DMA_DEV_TO_MEM);
            
            //stat must be clear first, stop the last dma xfer.
            owl_spi_writel(aspi, 0x30 | SPI_STAT_TCOM, SPI_STAT);

            owl_spi_writel(aspi, len/4, SPI_TCNT);
            owl_spi_writel(aspi, len/4, SPI_RXCR);
            
            SET_DMA_SRC_ADDR(devid, SPI0_RXDAT);
            SET_DMA_DST_ADDR(devid, rx_addr);
            SET_DMA_MODE(devid, mode);
            SET_DMA_COUNT(devid, len);
            
            START_DMA(devid);
            
            //wait util dma xfer finish.
            while(act_readl(BDMA0_BASE + (0x30 * devid) + BDMA_CMD));
            STOP_DMA(devid);
            dma_unmap_single(aspi->dma_rx_channel->device->dev, rx_addr, len, DMA_DEV_TO_MEM);
		    left -= len;
        }
    }

	return 0;
}
#endif

#ifdef DMA_SINGLE
//Test success, about 8.5M/s.
static int owl_spi_read_by_dma(struct owl_spi_data *aspi,
			   struct spi_transfer *xfer)
{
    int trig;
    trig = owl_spi_get_dma_trig((u32)aspi->phys);

	struct dma_slave_config rx_conf = {
		.src_addr = spi_reg(aspi, SPI_RXDAT),
		.direction = DMA_DEV_TO_MEM,
	};
	struct owl_dma_slave rx_atslave = {
        .mode = DMA_MODE_ST(trig) | DMA_MODE_DT_DDR | DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC,
		.dma_dev = aspi->dma_rx_channel->device->dev,
	};
	struct dma_chan *rxchan = aspi->dma_rx_channel;
	unsigned int pages;
	int len, left;
	void *rx_buf;
	int rx_sglen;
	struct dma_async_tx_descriptor *rxdesc;
	
	u32 val;
	int retval;

	struct completion rx_cmp;
	dma_cookie_t		cookie;
	enum dma_status		status;
    dma_addr_t rx_addr;
	
	/* Create sglists for the transfers */
	left = xfer->len;
	rx_buf = xfer->rx_buf;
	
    rxchan->private = (void *)&rx_atslave;
    retval = dmaengine_slave_config(rxchan, &rx_conf);
    if (retval) {
    	pr_err("call the read slave config error\n");
    	goto err_slave;
    }

    val = owl_spi_readl(aspi, SPI_CTL);
    val &= (~(SPI_CTL_RWC(3) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(7) | SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | \
    	SPI_CTL_TDEN | SPI_CTL_RDEN) | SPI_CTL_DAWS(3));
    val |= (SPI_CTL_RWC(2) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(1) | SPI_CTL_DTS | SPI_CTL_RDEN | \
        SPI_CTL_DAWS(2));

    //Support for dual wire read.
    if (xfer->rx_nbits == SPI_NBITS_DUAL)
        val |= (0x1 << 12);
    else
        val &= ~(0x1 << 12);
    owl_spi_writel(aspi, val, SPI_CTL);


	while(left > 0) {
		len = left > MAX_SPI_DMA_LEN ? MAX_SPI_DMA_LEN : left;

        //stat must be clear first, stop the last dma xfer.
        owl_spi_writel(aspi, 0x30 | SPI_STAT_TCOM, SPI_STAT);

		owl_spi_writel(aspi, len/4, SPI_TCNT);
		owl_spi_writel(aspi, len/4, SPI_RXCR);

        rx_addr = dma_map_single(aspi->dma_rx_channel->device->dev, xfer->rx_buf + (xfer->len - left), len, DMA_DEV_TO_MEM);
		rxdesc = dmaengine_prep_slave_single(rxchan, rx_addr, len, DMA_DEV_TO_MEM, 0);
		if (!rxdesc)
			goto err_desc;

		init_completion(&rx_cmp);
	
		rxdesc->callback = spi_callback;
		rxdesc->callback_param = &rx_cmp;
	
		cookie = dmaengine_submit(rxdesc);
		if (dma_submit_error(cookie)) {
			pr_err("submit read error!\n");
			goto err_desc;
		}

		dma_async_issue_pending(rxchan);

		if (!wait_for_completion_timeout(&rx_cmp, msecs_to_jiffies(100))) {
			pr_err("read wait_for_completion timeout while receive by dma in line %d \n", __LINE__);
			//owl_dma_dump_all(rxchan);
			goto err_desc;
		}
	
		status = dma_async_is_tx_complete(rxchan, cookie, NULL, NULL);
		if (status != DMA_SUCCESS) {
			pr_err("transfer not succeed\n");
			goto err_desc;
		}
        dma_unmap_single(aspi->dma_rx_channel->device->dev, rx_addr, len, DMA_DEV_TO_MEM);
		left -= len;
	}
	return 0;
	
err_desc:
	dmaengine_terminate_all(rxchan);
err_slave:
	return -EINVAL;
}
#endif

#ifdef DMA_SG
//Test success, about 9.5M/s.
static int owl_spi_read_by_dma(struct owl_spi_data *aspi,
			   struct spi_transfer *xfer)
{
    int trig;
    trig = owl_spi_get_dma_trig((u32)aspi->phys);

	struct dma_slave_config rx_conf = {
		.src_addr = spi_reg(aspi, SPI_RXDAT),
		.direction = DMA_DEV_TO_MEM,
	};
	struct owl_dma_slave rx_atslave = {
        .mode = DMA_MODE_ST(trig) | DMA_MODE_DT_DDR | DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC,
		.dma_dev = aspi->dma_rx_channel->device->dev,
	};
	struct dma_chan *rxchan = aspi->dma_rx_channel;
	unsigned int pages;
	int len, left;
	void *rx_buf;
	int rx_sglen;
	struct dma_async_tx_descriptor *rxdesc;
	
	u32 val;
	int retval;

	struct completion rx_cmp;
	dma_cookie_t		cookie;
	enum dma_status		status;
	
	/* Create sglists for the transfers */
	left = xfer->len;
	rx_buf = xfer->rx_buf;
	
	owl_spi_dump_regs(aspi);
	pages = DIV_ROUND_UP(xfer->len, PAGE_SIZE);
	retval = sg_alloc_table(&aspi->sgt_rx, pages, GFP_ATOMIC);
	if (retval)
		goto err_slave;

    rxchan->private = (void *)&rx_atslave;
    retval = dmaengine_slave_config(rxchan, &rx_conf);
    if (retval) {
    	pr_err("call the read slave config error\n");
    	goto err_slave;
    }

    val = owl_spi_readl(aspi, SPI_CTL);
    val &= (~(SPI_CTL_RWC(3) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(7) | SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | \
    	SPI_CTL_TDEN | SPI_CTL_RDEN) | SPI_CTL_DAWS(3));
    val |= (SPI_CTL_RWC(2) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
    	SPI_CTL_SDT(1) | SPI_CTL_DTS | SPI_CTL_RDEN | \
        SPI_CTL_DAWS(2));

    //Support for dual wire read.
    if (xfer->rx_nbits == SPI_NBITS_DUAL)
        val |= (0x1 << 12);
    else
        val &= ~(0x1 << 12);
    owl_spi_writel(aspi, val, SPI_CTL);

	while(left > 0) {
		len = left > MAX_SPI_DMA_LEN ? MAX_SPI_DMA_LEN : left;
		left -= len;

        //stat must be clear first, stop the last dma xfer.
        owl_spi_writel(aspi, 0x30 | SPI_STAT_TCOM, SPI_STAT);

		owl_spi_writel(aspi, len / 4, SPI_TCNT);
		owl_spi_writel(aspi, len / 4, SPI_RXCR);
        //print_spi_regs(SPI0_BASE);
        //print_dma_regs();
		
		/* Fill in the scatterlists for the RX buffers */
		owl_spi_setup_dma_scatter(rx_buf, len, &aspi->sgt_rx);
		rx_sglen = dma_map_sg(rxchan->device->dev, aspi->sgt_rx.sgl,
				   aspi->sgt_rx.nents, DMA_FROM_DEVICE);
		if (!rx_sglen)
			goto err_sgmap;

		rx_buf += len;
		
		/* Send scatterlists */
		rxdesc = dmaengine_prep_slave_sg(rxchan,
					      aspi->sgt_rx.sgl,
					      rx_sglen,
					      DMA_DEV_TO_MEM,
					      0);
		if (!rxdesc)
			goto err_desc;

		init_completion(&rx_cmp);
	
		rxdesc->callback = spi_callback;
		rxdesc->callback_param = &rx_cmp;
	
		cookie = dmaengine_submit(rxdesc);
		if (dma_submit_error(cookie)) {
			pr_err("submit read error!\n");
			goto err_desc;
		}

		dma_async_issue_pending(rxchan);

		//pr_debug("read start dma\n");
		if (!wait_for_completion_timeout(&rx_cmp, msecs_to_jiffies(100))) {
			pr_err("read wait_for_completion timeout while receive by dma in line %d \n", __LINE__);
			//owl_dma_dump_all(rxchan);
			goto err_desc;
		}
	
		status = dma_async_is_tx_complete(rxchan, cookie, NULL, NULL);
		if (status != DMA_SUCCESS) {
			pr_err("transfer not succeed\n");
			goto err_desc;
		}
	
		dma_unmap_sg(rxchan->device->dev, aspi->sgt_rx.sgl,
			     aspi->sgt_rx.nents, DMA_FROM_DEVICE);
	}
	sg_free_table(&aspi->sgt_rx);
	return 0;
	
err_desc:
	dmaengine_terminate_all(rxchan);
err_sgmap:
	sg_free_table(&aspi->sgt_rx);
err_slave:
	return -EINVAL;
}
#endif

static int owl_spi_write_read_by_dma(struct owl_spi_data *aspi,
			   struct spi_transfer *xfer)
{
    int trig = owl_spi_get_dma_trig((u32)aspi->phys);

	struct dma_slave_config tx_conf = {
		.dst_addr = spi_reg(aspi, SPI_TXDAT),
		.direction = DMA_MEM_TO_DEV,
	};
	struct owl_dma_slave tx_atslave = {
        .mode = DMA_MODE_DT(trig) | DMA_MODE_ST_DDR | DMA_MODE_SAM_INC | DMA_MODE_DAM_CONST,
		.dma_dev = aspi->dma_tx_channel->device->dev,
	};
	struct dma_slave_config rx_conf = {
		.src_addr = spi_reg(aspi, SPI_RXDAT),
		.direction = DMA_DEV_TO_MEM,
	};
	struct owl_dma_slave rx_atslave = {
        .mode = DMA_MODE_ST(trig) | DMA_MODE_DT_DDR | DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC,
		.dma_dev = aspi->dma_rx_channel->device->dev,
	};

	struct dma_chan *txchan = aspi->dma_tx_channel;
	struct dma_chan *rxchan = aspi->dma_rx_channel;
	unsigned int pages;
	int len, left;
	void *tx_buf, *rx_buf;
	int rx_sglen, tx_sglen;
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_async_tx_descriptor *txdesc;
	
	u32 val;
	int retval;

	struct completion rx_cmp, tx_cmp;
	dma_cookie_t		cookie;
	enum dma_status		status;

	/* Create sglists for the transfers */
	left = xfer->len;
	tx_buf = (void*)xfer->tx_buf;
	rx_buf = xfer->rx_buf;

	pages = DIV_ROUND_UP(xfer->len, PAGE_SIZE);
	retval = sg_alloc_table(&aspi->sgt_tx, pages, GFP_ATOMIC);
	if (retval)
		goto err_slave;
	retval = sg_alloc_table(&aspi->sgt_rx, pages, GFP_ATOMIC);
	if (retval)
		goto err_slave;

	while(left > 0) {
		len = left > MAX_SPI_DMA_LEN ? MAX_SPI_DMA_LEN : left;
		left -= len;

        //stat must be clear first, stop the last dma xfer.
        owl_spi_writel(aspi, 0x30 | SPI_STAT_TCOM, SPI_STAT);
		val = owl_spi_readl(aspi, SPI_CTL);
		val &= (~(SPI_CTL_RWC(3) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
			SPI_CTL_SDT(7) | SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | \
			SPI_CTL_TDEN | SPI_CTL_RDEN) | SPI_CTL_DAWS(3));
		val |= (SPI_CTL_RWC(0) | SPI_CTL_RDIC(3) | SPI_CTL_TDIC(3) | \
			SPI_CTL_SDT(1) | SPI_CTL_DTS | SPI_CTL_RDEN | \
			SPI_CTL_TDEN | SPI_CTL_DAWS(2));
		owl_spi_writel(aspi, val, SPI_CTL);
	
		owl_spi_writel(aspi, len/4, SPI_TXCR);
		owl_spi_writel(aspi, len/4, SPI_RXCR);

		txchan->private = (void *)&tx_atslave;
		retval = dmaengine_slave_config(txchan, &tx_conf);
		if (retval) {
			pr_err("call the write slave config error\n");
			goto err_slave;
		}
		rxchan->private = (void *)&rx_atslave;
		retval = dmaengine_slave_config(rxchan, &rx_conf);
		if (retval) {
			pr_err("call the read slave config error\n");
			goto err_slave;
		}

		/* Fill in the scatterlists for the TX buffers */
		owl_spi_setup_dma_scatter(tx_buf, len, &aspi->sgt_tx);
		tx_sglen = dma_map_sg(txchan->device->dev, aspi->sgt_tx.sgl,
				   aspi->sgt_tx.nents, DMA_TO_DEVICE);
		if (!tx_sglen)
			goto err_sgmap;
		owl_spi_setup_dma_scatter(rx_buf, len, &aspi->sgt_rx);
		rx_sglen = dma_map_sg(rxchan->device->dev, aspi->sgt_rx.sgl,
				   aspi->sgt_rx.nents, DMA_FROM_DEVICE);
		if (!rx_sglen)
			goto err_sgmap;

		tx_buf += len;
		rx_buf += len;

		/* Send scatterlists */
		txdesc = dmaengine_prep_slave_sg(txchan,
					      aspi->sgt_tx.sgl,
					      tx_sglen,
					      DMA_MEM_TO_DEV,
					      0);
		if (!txdesc)
			goto err_desc;	
		rxdesc = dmaengine_prep_slave_sg(rxchan,
					      aspi->sgt_rx.sgl,
					      rx_sglen,
					      DMA_DEV_TO_MEM,
					      0);
		if (!rxdesc)
			goto err_desc;

		init_completion(&tx_cmp);
		txdesc->callback = spi_callback;
		txdesc->callback_param = &tx_cmp;
		cookie = dmaengine_submit(txdesc);
		if (dma_submit_error(cookie)) {
			pr_err("submit write error!\n");
			goto err_desc;
		}

		init_completion(&rx_cmp);
		rxdesc->callback = spi_callback;
		rxdesc->callback_param = &rx_cmp;
		cookie = dmaengine_submit(rxdesc);
		if (dma_submit_error(cookie)) {
			pr_err("submit read error!\n");
			goto err_desc;
		}

		dma_async_issue_pending(txchan);
		dma_async_issue_pending(rxchan);

		pr_debug("write&read start dma\n");
		if (!wait_for_completion_timeout(&tx_cmp, 5 * HZ)) {
			pr_err("write wait_for_completion timeout while send by dma\n");
			owl_dma_dump_all(txchan);
			goto err_desc;
		}
		if (!wait_for_completion_timeout(&rx_cmp, 1 * HZ)) {
			pr_err("read wait_for_completion timeout while receive by dma in line %d \n", __LINE__);
			owl_dma_dump_all(rxchan);
			goto err_desc;
		}
	
		status = dma_async_is_tx_complete(txchan, cookie, NULL, NULL);
		if (status != DMA_SUCCESS) {
			pr_err("transfer not succeed\n");
			goto err_desc;
		}
		status = dma_async_is_tx_complete(rxchan, cookie, NULL, NULL);
		if (status != DMA_SUCCESS) {
			pr_err("transfer not succeed\n");
			goto err_desc;
		}
	
		if (owl_spi_readl(aspi, SPI_STAT) &
		    (SPI_STAT_RFER | SPI_STAT_TFER | SPI_STAT_BEB)) {
			pr_err("spi state error while send by dma\n");
			dump_spi_registers(aspi);
			goto err_desc;
		}

		dma_unmap_sg(txchan->device->dev, aspi->sgt_tx.sgl,
			     aspi->sgt_tx.nents, DMA_TO_DEVICE);
		dma_unmap_sg(rxchan->device->dev, aspi->sgt_rx.sgl,
			     aspi->sgt_rx.nents, DMA_FROM_DEVICE);
	}
	sg_free_table(&aspi->sgt_tx);
	sg_free_table(&aspi->sgt_rx);
	return 0;
	
err_desc:
	dmaengine_terminate_all(rxchan);
	dmaengine_terminate_all(txchan);
err_sgmap:
	sg_free_table(&aspi->sgt_rx);
	sg_free_table(&aspi->sgt_tx);
err_slave:
	return -EINVAL;
}
#endif

static int owl_spi_write_read(struct owl_spi_data *aspi,
		struct spi_transfer *xfer)
{
	int word_len = 0, len = 0, count = 0, retval = 0;

	word_len = aspi->cur_bits_per_word;
	len = xfer->len;

#ifdef CONFIG_DMA_ATS3605
    //if read len larger than 64 bytes and len align 4, use dma read.
    if ((len > 64) && (len % 4 == 0) && aspi->enable_dma) {
		if (xfer->tx_buf && (!xfer->rx_buf)) {
			retval = owl_spi_write_by_dma(aspi, xfer);
        } else if ((!xfer->tx_buf) && xfer->rx_buf) {
			retval = owl_spi_read_by_dma(aspi, xfer);
		} else if((xfer->tx_buf) && (xfer->rx_buf)) {
			retval = owl_spi_write_read_by_dma(aspi, xfer);
  		} else {
			pr_err("cannot find valid xfer buffer\n");
			return 0;
		}

		if (retval)
			return 0;
		else
			return len;
    } else {
	    spi_clear_stat(aspi);
        if (word_len == 8)
		    count = owl_spi_write_read_8bit(aspi, xfer);
        else if (word_len == 16)
		    count = owl_spi_write_read_16bit(aspi, xfer);
        else
		    count = owl_spi_write_read_32bit(aspi, xfer);

	    if (count < 0)
	    	return count;

	    return len - count;
    }
#else
   spi_clear_stat(aspi);
   if (word_len == 8)
       count = owl_spi_write_read_8bit(aspi, xfer);
   else if (word_len == 16)
       count = owl_spi_write_read_16bit(aspi, xfer);
   else
       count = owl_spi_write_read_32bit(aspi, xfer);

   if (count < 0)
   	return count;

   return len - count;
#endif
}

static int owl_spi_handle_msg(struct owl_spi_data *aspi,
		struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer = NULL;
	int ret, status = 0;
	u32 speed;
	u8 bits_per_word;

	msg->status = -EINPROGRESS;

	aspi->cur_bits_per_word = spi->bits_per_word;
	aspi->cur_speed = spi->max_speed_hz;
	aspi->cur_mode = spi->mode;
	owl_spi_config(aspi);

	enable_cs(spi);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* Only BPW and Speed may change across transfers */
		bits_per_word = xfer->bits_per_word ? : spi->bits_per_word;
		speed = xfer->speed_hz ? : spi->max_speed_hz;

		//dev_dbg(&spi->dev, "%d: bits_per_word %d, len %d\n", __LINE__,
		//	bits_per_word, xfer->len);

		if ((bits_per_word != 8) && (bits_per_word != 16) &&
			(bits_per_word != 32)) {
			dev_err(&spi->dev, "invalid bits per word - %d!\n",
					bits_per_word);
			status = -EINVAL;
			goto out;
		}

		/*transfer length should be alignd according to bits_per_word*/
		if (xfer->len & ((bits_per_word >> 3) - 1)) {
			dev_err(&spi->dev, "bad transfer length - %d!\n",
					xfer->len);
			status = -EINVAL;
			goto out;
		}

		if (bits_per_word != aspi->cur_bits_per_word ||
				speed != aspi->cur_speed) {
			aspi->cur_bits_per_word = bits_per_word;
			aspi->cur_speed = speed;
			owl_spi_config(aspi);
		}

		//if (xfer->tx_buf)
		//	owl_spi_dump_mem("[msg] tx buf",
		//			xfer->tx_buf, xfer->len);

		if (xfer->cs_change)
			enable_cs(spi);

		ret = owl_spi_write_read(aspi, xfer);
		if (unlikely(ret < 0)) {
			status = ret;
			goto out;
		}

		//if (xfer->rx_buf)
		//	owl_spi_dump_mem("[msg] rx buf", xfer->rx_buf,
		//			xfer->len);

		msg->actual_length += ret;

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change)
			disable_cs(spi);
	}

out:
	disable_cs(spi);

	msg->status = status;
	if (status < 0)
		dev_warn(&spi->dev, "spi transfer failed with %d\n", status);

	spi_finalize_current_message(aspi->master);

	return status;
}

static int owl_spi_transfer_one_message(struct spi_master *master,
				      struct spi_message *msg)
{
	struct owl_spi_data *aspi = spi_master_get_devdata(master);

	return owl_spi_handle_msg(aspi, msg);
}

static int owl_spi_setup(struct spi_device *spi)
{
	int ret = 0;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	ret = owl_spi_setup_cs(spi);
	if (ret)
		return ret;

	return ret;
}

static void owl_spi_cleanup(struct spi_device *spi)
{
	owl_spi_cleanup_cs(spi);
}

/* init hardware by default value */
static int owl_spi_init_hw(struct owl_spi_data *aspi)
{
	unsigned int val;

	val = owl_spi_readl(aspi, SPI_CTL);
	val &= ~(SPI_CTL_RWC_MASK | SPI_CTL_TDIC_MASK | SPI_CTL_SDT_MASK | \
		SPI_CTL_DTS | SPI_CTL_TIEN | SPI_CTL_RIEN | SPI_CTL_TDEN | \
		SPI_CTL_RDEN);
	val |= SPI_CTL_EN | SPI_CTL_SSCO;
	owl_spi_writel(aspi, val, SPI_CTL);

	spi_clear_stat(aspi);

	/* 120MHz by default */
	owl_spi_baudrate_set(aspi, 120000000);

	return 0;
}

#ifdef CONFIG_DMA_ATS3605
static int owl_spi_dma_probe(struct owl_spi_data *aspi)
{
	dma_cap_mask_t mask;

	if (!aspi->enable_dma) {
		pr_debug("spi dma is disabled\n");
		return 0;
	}

	/* Try to acquire a generic DMA engine slave channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	/*
	 * We need both RX and TX channels to do DMA, else do none
	 * of them.
	 */
	aspi->dma_rx_channel = dma_request_slave_channel(aspi->dev, "rx");
	if (!aspi->dma_rx_channel) {
		pr_err("no RX DMA channel!\n");
		goto err_no_rxchan;
	}

	aspi->dma_tx_channel = dma_request_slave_channel(aspi->dev, "tx");
	if (!aspi->dma_tx_channel) {
		pr_err("no TX DMA channel!\n");
		goto err_no_txchan;
	}

	pr_debug("setup for DMA on RX %s, TX %s\n",
		 dma_chan_name(aspi->dma_rx_channel),
		 dma_chan_name(aspi->dma_tx_channel));

	return 0;

err_no_txchan:
	dma_release_channel(aspi->dma_rx_channel);
	aspi->dma_rx_channel = NULL;
err_no_rxchan:
	pr_err("Failed to work in dma mode, work without dma!\n");
	return -ENODEV;
}

static void owl_spi_unmap_free_dma_scatter(struct owl_spi_data *aspi)
{
	/* Unmap and free the SG tables */
    if (&aspi->sgt_rx) {
	    dma_unmap_sg(aspi->dma_rx_channel->device->dev, aspi->sgt_rx.sgl,
	    	     aspi->sgt_rx.nents, DMA_FROM_DEVICE);
	    sg_free_table(&aspi->sgt_rx);
    }
    if (&aspi->sgt_tx) {
	    dma_unmap_sg(aspi->dma_tx_channel->device->dev, aspi->sgt_tx.sgl,
	    	     aspi->sgt_tx.nents, DMA_TO_DEVICE);
	    sg_free_table(&aspi->sgt_tx);
    }
}

static void owl_spi_terminate_dma(struct owl_spi_data *aspi)
{
	struct dma_chan *rxchan = aspi->dma_rx_channel;
	struct dma_chan *txchan = aspi->dma_tx_channel;

    if (rxchan)
	    dmaengine_terminate_all(rxchan);
    if (txchan)
	    dmaengine_terminate_all(txchan);
	owl_spi_unmap_free_dma_scatter(aspi);
	aspi->dma_running = false;
}

static void owl_spi_dma_remove(struct owl_spi_data *aspi)
{
	if (aspi->dma_running)
		owl_spi_terminate_dma(aspi);
	if (aspi->dma_tx_channel)
		dma_release_channel(aspi->dma_tx_channel);
	if (aspi->dma_rx_channel)
		dma_release_channel(aspi->dma_rx_channel);
}
#endif

static int owl_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = dev_get_drvdata(&pdev->dev);
	struct owl_spi_data *aspi = spi_master_get_devdata(master);

#ifdef CONFIG_DMA_ATS3605
	owl_spi_dma_remove(aspi);
#endif
	clk_disable_unprepare(aspi->clk);
	spi_unregister_master(master);

	return 0;
}

static int owl_spi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct owl_spi_data *aspi;
	struct resource *res;
	int ret, num_cs;

	master = spi_alloc_master(&pdev->dev, sizeof(*aspi));
	if (!master)
		return -ENOMEM;

	aspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto free_master;
	}
	aspi->phys = res->start;

	aspi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(aspi->base)) {
		ret = PTR_ERR(aspi->base);
		goto free_master;
	}

	aspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(aspi->clk)) {
		dev_err(&pdev->dev, "spi clock not found.\n");
		ret = PTR_ERR(aspi->clk);
		goto free_master;
	}

	ret = clk_prepare_enable(aspi->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto free_master;
	}

    aspi->dev = &pdev->dev;

	/* SPI controller initializations */
	owl_spi_init_hw(aspi);

	ret = of_property_read_u32(np, "num-cs", &num_cs);
	if (ret < 0)
		master->num_chipselect = 1;
	else
		master->num_chipselect = num_cs;

	master->setup = owl_spi_setup;
	master->cleanup = owl_spi_cleanup;
	master->transfer_one_message = owl_spi_transfer_one_message;
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->flags = 0;
	master->bits_per_word_mask = BIT(32 - 1) | BIT(16 - 1) | BIT(8 - 1);
	master->dev.of_node = np;

	aspi->master = master;

#ifdef CONFIG_DMA_ATS3605
	aspi->enable_dma = 1;
	if(owl_spi_dma_probe(aspi) < 0)
		goto remove_dma;
#endif

	init_completion(&aspi->xfer_completion);

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	clk_disable_unprepare(aspi->clk);
#ifdef CONFIG_DMA_ATS3605
remove_dma:
	owl_spi_dma_remove(aspi);
#endif
free_master:
	spi_master_put(master);
	return ret;
}

static const struct of_device_id owl_spi_dt_ids[] = {
	{ .compatible = "actions,s900-spi" },
	{ .compatible = "actions,s700-spi" },
	{ .compatible = "actions,ats3605-spi" },
};

static struct platform_driver owl_spi_driver = {
	.probe = owl_spi_probe,
	.remove = owl_spi_remove,
	.driver = {
		.name = "spi-owl",
		.of_match_table = of_match_ptr(owl_spi_dt_ids),
	},
};

static int __init owl_spi_init(void)
{
	pr_info("[OWL] SPI controller initialization\n");

	return platform_driver_register(&owl_spi_driver);
}

static void __exit owl_spi_exit(void)
{
	platform_driver_unregister(&owl_spi_driver);
}

subsys_initcall(owl_spi_init);
module_exit(owl_spi_exit);

MODULE_AUTHOR("David Liu <liuwei@actions-semi.com>");
MODULE_DESCRIPTION("SPI controller driver for Actions SOC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spi-owl");
