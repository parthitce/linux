/*
 * Actions Devices VUI board(ATT3008/ATT3006) I2C-SPI driver
 *
 * Copyright 2017 Actions-semi Inc.
 * Author: Yiguang <liuyiguang@actions-semi.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <mach/hardware.h>
#include <mach/actions_reg_leopard.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <dt-bindings/dma/dma-ats3605.h>

#include "module_att3008.c"
#include "vui_ioctl.h"

#define DEFAULT_SAMPLE_RATE     16000           //16khz, 32kh, 48khz for choise!
#define DEFAULT_DATAWIDTH       16              //16bit, 32bit for choise!
#define MAX_CH                  8
#define DEFAULT_ADC_CLK         26              //26m, 24m for choise!

/* SPI controller registers */
#define SPI_CTL				    0x00
#define SPI_CLKDIV			    0x04
#define SPI_STAT			    0x08
#define SPI_RXDAT			    0x0C
#define SPI_TXDAT			    0x10
#define SPI_TCNT			    0x14
#define SPI_SEED			    0x18
#define SPI_TXCR			    0x1C
#define SPI_RXCR			    0x20

/* SPI_CTL */
#define SPI_CTL_SDT_MASK		(0x7 << 29)
#define SPI_CTL_SDT(x)			(((x) & 0x7) << 29)
#define SPI_CTL_BM				(0x1 << 28)
#define SPI_CTL_GM				(0x1 << 27)
#define SPI_CTL_CEB				(0x1 << 26)
#define SPI_CTL_RANEN   		(0x1 << 24)
#define SPI_CTL_RDIC_MASK		(0x3 << 22)
#define SPI_CTL_RDIC(x)			(((x) & 0x3) << 22)
#define SPI_CTL_TDIC_MASK		(0x3 << 20)
#define SPI_CTL_TDIC(x)			(((x) & 0x3) << 20)
#define SPI_CTL_TWME			(0x1 << 19)
#define SPI_CTL_EN				(0x1 << 18)
#define SPI_CTL_RWC_MASK		(0x3 << 16)
#define SPI_CTL_RWC(x)			(((x) & 0x3) << 16)
#define SPI_CTL_DTS			    (0x1 << 15)
#define SPI_CTL_SSATEN			(0x1 << 14)
#define SPI_CTL_DM_MASK			(0x3 << 12)
#define SPI_CTL_DM(x)			(((x) & 0x3) << 12)
#define SPI_CTL_LBT			    (0x1 << 11)
#define SPI_CTL_MS			    (0x1 << 10)
#define SPI_CTL_DAWS_MASK		(0x3 << 8)
#define SPI_CTL_DAWS(x)			(((x) & 0x3) << 8)
#define		SPI_CTL_DAWS_8BIT	(SPI_CTL_DAWS(0))
#define		SPI_CTL_DAWS_16BIT	(SPI_CTL_DAWS(1))
#define		SPI_CTL_DAWS_32BIT	(SPI_CTL_DAWS(2))
#define SPI_CTL_CPOS_MASK		(0x3 << 6)
#define SPI_CTL_CPOS(x)			(((x) & 0x3) << 6)
#define		SPI_CTL_CPOS_CPHA	(0x1 << 7)
#define		SPI_CTL_CPOS_CPOL	(0x1 << 6)
#define SPI_CTL_LMFS			(0x1 << 5)
#define SPI_CTL_SSCO			(0x1 << 4)
#define SPI_CTL_TIEN			(0x1 << 3)
#define SPI_CTL_RIEN			(0x1 << 2)
#define SPI_CTL_TDEN			(0x1 << 1)
#define SPI_CTL_RDEN			(0x1 << 0)

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

/* SPI_CLKDIV */
#define SPI_CLKDIV_CLKDIV_MASK	(0x3FF << 0)
#define SPI_CLKDIV_CLKDIV(x)	(((x) & 0x3FF) << 0)

#define MAX_SPI_POLL_LOOPS		5000
#define SPI_DMA_DISPOSABLE_LEN	(8 * 1024)
#define LOOP_BUFFER_SIZE		(SPI_DMA_DISPOSABLE_LEN * 32)

/***********************************************************/
#define BDMA_MODE               0x0
#define BDMA_SRC                0x4
#define BDMA_DST                0x8
#define BDMA_CNT                0xc
#define BDMA_REM                0x10
#define BDMA_CMD                0x14
#define BDMA_CACHE              0x18

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
/***********************************************************/

struct owl_dma_slave {
	u32	mode;
	struct device *dma_dev;
};

typedef struct {
    void *start;
	void *end;
	void *in;
	void *out;
} loop_buf_t;

struct spivui_data {
    struct clk *spi_clk;
    struct clk *i2c_clk;
    struct i2c_client *client;
	struct dma_chan	*dma_rx_channel;
    struct device *dev;
    dma_addr_t rx_addr;
	dma_cookie_t cookie;
	loop_buf_t loop_buf;
    bool block;
    int spi_bus;
    int chn_num;
    int adc_clk;
    int dw;
    int pos;
    struct resource spi_res;
	u32 vui_en_gpios;
	u32 vui_reset_gpios;
};

static int major = 0;
static dev_t devno;
static bool loop_back = false;
static struct class *spivui_class;
static struct device *spivui_device;
static struct spivui_data *g_aspi;

static DECLARE_WAIT_QUEUE_HEAD(vui_waitq);
static bool vui_buffer_input = false;

static int set_spi_res(struct spivui_data *aspi, int id)
{
    aspi->spi_res.start = SPI0_BASE + id * 0x4000;
    aspi->spi_res.end = SPI0_BASE + (id+1) * 0x4000;
    aspi->spi_res.flags = IORESOURCE_MEM;

    return 0;
}

static inline void check_and_set_gpio(u32 gpio, int value)
{
	if (gpio_cansleep(gpio)) {
		gpio_set_value_cansleep(gpio, value);
	} else {
		gpio_set_value(gpio, value);
	}
}

static int of_data_get(struct spivui_data *aspi)
{
	struct device_node *of_node;
	int ret = -1;
	const char *vui_src = NULL;

	of_node = of_find_compatible_node(NULL, NULL, "spivui");
	if (of_node == NULL) {
		pr_err("%s,%d,get spivui compatible fail!\n", __func__, __LINE__);
		return ret;
	}

	ret = of_property_read_u32(of_node, "spi_bus", &aspi->spi_bus);
    if (ret) {
        pr_err("get spi bus error!\n");
        return ret;
    }

    set_spi_res(aspi, aspi->spi_bus);

	ret = of_property_read_u32(of_node, "adc_clk", &aspi->adc_clk);
    if (ret) {
        pr_err("get adc clk value error! set adc clk as default 26m \n");
        aspi->adc_clk = DEFAULT_ADC_CLK;
    } else {
        if (aspi->adc_clk != 24 && aspi->adc_clk != 26) {
            pr_err("get adc clk error, value should be 24m or 26m \n");
            return -1;
        }
    }

    if (aspi->adc_clk == 24) {
        act_writel(0x0, DEBUG_OEN0); 
        act_writel(0x0, DEBUG_SEL); 
        act_writel(0x10000000, DEBUG_OEN0); 
        act_writel(0x80000038, CMU_DIGITALDEBUG); 
    }

    //Read vui power control setting for dts
	if (!of_property_read_string(of_node, "vui-src", &vui_src)) {
	    if (!strcmp(vui_src, "regulator")) {
        }else if(!strcmp(vui_src, "gpio")){
	        if (of_find_property(of_node, "vui-reset-gpios", NULL)) {
	        	aspi->vui_reset_gpios = of_get_named_gpio(of_node, "vui-reset-gpios", 0);
	        	if (gpio_is_valid(aspi->vui_reset_gpios)) {
	        		ret = gpio_request(aspi->vui_reset_gpios, "vui-reset-gpios");
	        		if (ret < 0) {
	        			pr_err("couldn't claim vui_reset_gpios pin\n");
	        			return ret;
	        		}
	        		gpio_direction_output(aspi->vui_reset_gpios, 0);
	        		check_and_set_gpio(aspi->vui_reset_gpios, 1);
	        		msleep(2);
	        	} else {
	        		pr_err("gpio for vui_reset_gpios invalid.\n");
	        	}
	        }

	        if (of_find_property(of_node, "vui-enable-gpios", NULL)) {
	        	aspi->vui_en_gpios = of_get_named_gpio(of_node, "vui-enable-gpios", 0);
	        	if (gpio_is_valid(aspi->vui_en_gpios)) {
	        		ret = gpio_request(aspi->vui_en_gpios, "vui-enable-gpios");
	        		if (ret < 0) {
	        			pr_err("couldn't claim vui_en_gpios pin\n");
	        			return ret;
	        		}
	        		gpio_direction_output(aspi->vui_en_gpios, 0);
	        		check_and_set_gpio(aspi->vui_en_gpios, 1);
	        		msleep(2);
	        	} else {
	        		pr_err("gpio for vui_en_gpios invalid.\n");
	        	}
	        }
        }
    }

	return 0;
}

static inline void spi_writel(u32 val, u32 reg)
{
    u32 spi_base = SPI0_BASE + g_aspi->spi_bus * 0x4000;
    act_writel(val, spi_base + reg);
}

static inline u32 spi_readl(u32 reg)
{
    u32 spi_base = SPI0_BASE + g_aspi->spi_bus * 0x4000;
    return act_readl(spi_base + reg);
}

static int spi_init()
{
    u32 val;
    spi_writel(0x0, SPI_CTL);
    spi_writel(0x1, SPI_CLKDIV);

    val = spi_readl(SPI_CTL);
    val &= ~(SPI_CTL_SDT_MASK | SPI_CTL_BM | SPI_CTL_GM | SPI_CTL_CEB |
            SPI_CTL_RANEN | SPI_CTL_RDIC_MASK | SPI_CTL_TDIC_MASK | 
            SPI_CTL_TWME | SPI_CTL_EN | SPI_CTL_RWC_MASK | SPI_CTL_DTS |
            SPI_CTL_SSATEN | SPI_CTL_DM_MASK | SPI_CTL_LBT | SPI_CTL_MS | 
            SPI_CTL_DAWS_MASK | SPI_CTL_CPOS_MASK | SPI_CTL_LMFS | 
            SPI_CTL_SSCO | SPI_CTL_TIEN | SPI_CTL_RIEN | SPI_CTL_TDEN | 
            SPI_CTL_RDEN);
    val |= (SPI_CTL_SDT(0x0) | SPI_CTL_RDIC(0x3) | SPI_CTL_MS | 
            SPI_CTL_TDIC(0x3) | SPI_CTL_RWC(0x2) | SPI_CTL_EN | SPI_CTL_DTS | 
            SPI_CTL_DAWS_32BIT | SPI_CTL_CPOS(0x3) | SPI_CTL_RDEN);
    spi_writel(val, SPI_CTL);

    return 0;
}

static void print_spi_regs(unsigned int base)
{
    pr_err("SPI2_CTL(0x%x) = 0x%x \n", base+SPI_CTL, act_readl(base+SPI_CTL));
    pr_err("SPI2_CLKDIV(0x%x) = 0x%x \n", base+SPI_CLKDIV, act_readl(base+SPI_CLKDIV));
    pr_err("SPI2_STAT(0x%x) = 0x%x \n", base+SPI_STAT, act_readl(base+SPI_STAT));
    pr_err("SPI2_TCNT(0x%x) = 0x%x \n", base+SPI_TCNT, act_readl(base+SPI_TCNT));
    pr_err("SPI2_SEED(0x%x) = 0x%x \n", base+SPI_SEED, act_readl(base+SPI_SEED));
    pr_err("SPI2_TXCR(0x%x) = 0x%x \n", base+SPI_TXCR, act_readl(base+SPI_TXCR));
    pr_err("SPI2_RXCR(0x%x) = 0x%x \n", base+SPI_RXCR, act_readl(base+SPI_RXCR));
}

static void print_dma_regs()
{
    int i = 0;
    for (i = 0; i < 1; i++) {
        pr_err("BDMA%d_MODE(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_MODE, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_MODE));
        pr_err("BDMA%d_SRC(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_SRC, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_SRC));
        pr_err("BDMA%d_DST(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_DST, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_DST));
        pr_err("BDMA%d_CNT(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_CNT, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_CNT));
        pr_err("BDMA%d_REM(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_REM, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_REM));
        pr_err("BDMA%d_CMD(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_CMD, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_CMD));
        pr_err("BDMA%d_CACHE(0x%x) = 0x%x \n", i, BDMA0_BASE + (0x30 * i) + BDMA_CACHE, \
                act_readl(BDMA0_BASE + (0x30 * i) + BDMA_CACHE));
    }
}

static void loop_buf_init(loop_buf_t *p, void *start, int size)
{
	p->start = start;
	p->end = start + size;
	p->in = p->out = start;
    pr_info("start = 0x%x; end = 0x%x; in = 0x%x; out = 0x%x; size = 0x%x \n", \
            start, p->start, p->end, p->in, p->out, size);
}

static int loop_buf_input_remain(loop_buf_t *p)
{
	return ((p->in < p->out) ? (p->out - p->in) : ((p->end - p->in) + (p->out - p->start)));
}

static int loop_buf_output_remain(loop_buf_t *p)
{
	return ((p->in >= p->out) ? (p->in - p->out) : ((p->end - p->out) + (p->in - p->start)));
}

static int loop_buf_producer(loop_buf_t *p, int size)
{
	int len = loop_buf_input_remain(p);

	size = len > size ? size : len;
	if(size == 0)
		return 0;

	if(p->in >= p->out) {
		len = p->end - p->in;
		len = size > len  ? len  : size;

        if (loop_back)
            loop_back = false;

		size -= len;

        p->in += len;
        if(p->in == p->end) {
        	p->in = p->start;
            loop_back = true;
        }

		if(size > 0) {
			p->in = p->start + size;
            //pr_err("in = 0x%x line %d \n", p->in, __LINE__);
			return (size + len);
		} else {
            //pr_err("in = 0x%x line %d \n", p->in, __LINE__);
			return len;
        }
	} else {
		p->in += size;
        if (loop_back && p->in >= p->out) {
            loop_back = false;
            pr_err("Buffer loop back and crash err!!! in %d \n", __LINE__);
        }
        //pr_err("in = 0x%x in line %d \n", p->in, __LINE__);
		return size;
	}
}

static int loop_buf_consumer(loop_buf_t *p, void *dst, int size)
{
	int len = loop_buf_output_remain(p);
	
	size = len > size ? size : len;
	if(size == 0)
		return 0;
	
	if(p->out > p->in) {
		len = p->end - p->out;
		len = size > len ? len : size;
		if(copy_to_user(dst, p->out, len))
			return 0;

		size -= len;

        p->out += len;
        if(p->out == p->end)
        	p->out = p->start;

		if(size > 0) {
			dst += len;
			if(copy_to_user(dst, p->out, size))
				return 0;
			p->out = p->start + size;
            //pr_err("consumer change p->out = 0x%x ; p->in = 0x%x in line %d \n", p->out, p->in, __LINE__);
			return (size + len);
		} else {
            //pr_err("consumer change p->out = 0x%x ; p->in = 0x%x in line %d \n", p->out, p->in, __LINE__);
			return len;
        }
	} else if(p->out < p->in) {
		if(copy_to_user(dst, p->out, size))
			return 0;
		p->out += size;
        //pr_err("consumer change p->out = 0x%x ; p->in = 0x%x in line %d \n", p->out, p->in, __LINE__);
		return size;
	} else {
        pr_err("Producer buffer equals to Consumer buffer, Buffer crash in line %d ====\n", __LINE__);
        if (g_aspi->block)
            return -EIO;
        else
            return -EAGAIN;
    }
}

static void read_callback(void *arg)
{
    int count;
    int size;
    u32 rxcr;
    int curpos;
    struct spivui_data *aspi = arg;
	struct dma_tx_state state;

    if (aspi == NULL || aspi->dma_rx_channel == NULL)
        return;

	dma_sync_single_for_cpu(aspi->dev, aspi->rx_addr,
			LOOP_BUFFER_SIZE, DMA_FROM_DEVICE);
	dmaengine_tx_status(aspi->dma_rx_channel, aspi->cookie, &state);

    curpos = (LOOP_BUFFER_SIZE - state.residue) / SPI_DMA_DISPOSABLE_LEN;
    if (aspi->pos == curpos) {
        spi_writel(0xFFF0, SPI_RXCR);
        pr_err("curpos = pos, reset SPI2_RXCR = 0xFFF0 and return \n");
        return ;
    }

    count = (aspi->pos < curpos) ? (curpos - aspi->pos) : (curpos + LOOP_BUFFER_SIZE / SPI_DMA_DISPOSABLE_LEN - aspi->pos);
    if (count > 10) {
        pr_err("vui dma lost irq num %d \n", count);
        pr_info("rxcr = 0x%x; state.residue = %d; pos = %d; curpos = %d \n", spi_readl(SPI_RXCR), state.residue, aspi->pos, curpos);
    }
    aspi->pos = curpos;

	if (loop_buf_producer(&aspi->loop_buf, count * SPI_DMA_DISPOSABLE_LEN)) {
        if (aspi->block) {
            vui_buffer_input = true;
	        wake_up_interruptible(&vui_waitq);
        }
    }
    //change len to word.
    //act_writel(SPI_DMA_DISPOSABLE_LEN / 4, SPI2_RXCR);

    spi_writel(0xFFF0, SPI_RXCR);
}

static int get_spi_dma_trig(int id)
{
	static int trigs[] = {DMA_DRQ_SPI0, DMA_DRQ_SPI1, DMA_DRQ_SPI2, DMA_DRQ_SPI3};
	
	int spi_no = id;
	if(spi_no < 0) {
		pr_err("error: get spi_no = %d \n", id);
		return -1;
	}
	
	return trigs[spi_no];
}

static int spivui_dma_start(struct spivui_data *aspi)
{
    int trig = get_spi_dma_trig(aspi->spi_bus);
	struct dma_slave_config rx_conf = {
		.src_addr = SPI0_BASE + aspi->spi_bus * 0x4000 + SPI_RXDAT,
		.direction = DMA_DEV_TO_MEM,
	};
	struct owl_dma_slave rx_atslave = {
        .mode = DMA_MODE_ST(trig) | DMA_MODE_DT_DDR | DMA_MODE_SAM_CONST | DMA_MODE_DAM_INC,
		.dma_dev = aspi->dma_rx_channel->device->dev,
	};
	struct dma_chan *rxchan = aspi->dma_rx_channel;
	struct dma_async_tx_descriptor *rxdesc;
	enum dma_status		status;
    rxchan->private = (void *)&rx_atslave;
	int retval;
    retval = dmaengine_slave_config(rxchan, &rx_conf);
    if (retval) {
    	pr_err("call the read slave config error\n");
    	return -1;
    }

    aspi->pos = 0;
    //Clear stat
    spi_writel(0x30 | SPI_STAT_TCOM, SPI_STAT);
    //change len to word.
    //act_writel(SPI_DMA_DISPOSABLE_LEN / 4, SPI2_RXCR);
    spi_writel(0xFFF0, SPI_RXCR);

    aspi->rx_addr = dma_map_single(aspi->dev, aspi->loop_buf.start, \
            LOOP_BUFFER_SIZE, DMA_DEV_TO_MEM);
    if (dma_mapping_error(aspi->dev, aspi->rx_addr)) {
        pr_err("dma_map_single error in line %d \n", __LINE__);
    }

	loop_buf_init(&aspi->loop_buf, aspi->loop_buf.start, LOOP_BUFFER_SIZE);

	dma_sync_single_for_device(aspi->dev, \
            aspi->rx_addr, LOOP_BUFFER_SIZE, DMA_FROM_DEVICE);

    rxdesc = dmaengine_prep_dma_cyclic(rxchan, aspi->rx_addr, LOOP_BUFFER_SIZE, SPI_DMA_DISPOSABLE_LEN, \
            DMA_DEV_TO_MEM, 0);
    if (!rxdesc)
    	goto err_desc;
    
    rxdesc->callback = read_callback;
    rxdesc->callback_param = aspi;
    
    aspi->cookie = dmaengine_submit(rxdesc);
    if (dma_submit_error(aspi->cookie)) {
    	pr_err("submit read error!\n");
    	goto err_desc;
    }
    
    dma_async_issue_pending(rxchan);
    vui_audio_start(aspi->client);

    return 0;

err_desc:
	dmaengine_terminate_all(rxchan);
    return -1;
}

static int owl_spi_dma_probe(struct spivui_data *aspi)
{
	dma_cap_mask_t mask;

	/* Try to acquire a generic DMA engine slave channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	aspi->dma_rx_channel = dma_request_slave_channel(aspi->dev, "rx");
	if (!aspi->dma_rx_channel) {
		pr_err("no RX DMA channel!\n");
		goto err_no_rxchan;
	}

	pr_info("setup for DMA on RX %s\n",
		 dma_chan_name(aspi->dma_rx_channel));

	aspi->loop_buf.start = kmalloc(LOOP_BUFFER_SIZE, GFP_KERNEL);
	if (!aspi->loop_buf.start) {
		pr_err("Unable to allocate dma buffer for RX\n");
		dma_release_channel(aspi->dma_rx_channel);
		return -ENOMEM;
	}

	return 0;

err_no_rxchan:
	pr_err("Failed to work in dma mode, work without dma!\n");
	return -ENODEV;
}

static void owl_spi_terminate_dma(struct spivui_data *aspi)
{
	struct dma_chan *rxchan = aspi->dma_rx_channel;

    if (rxchan)
	    dmaengine_terminate_all(rxchan);
}

static void owl_spi_dma_remove(struct spivui_data *aspi)
{
    owl_spi_terminate_dma(aspi);
	if (aspi->dma_rx_channel)
		dma_release_channel(aspi->dma_rx_channel);
    if (aspi->loop_buf.start)
        kfree(aspi->loop_buf.start);
}

static ssize_t
spivui_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t
spivui_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    if (g_aspi->block) {
	    wait_event_interruptible(vui_waitq, vui_buffer_input);
	    vui_buffer_input = false;
    }

	return loop_buf_consumer(&g_aspi->loop_buf, (void*)buf, count);
}

static long
spivui_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret;
    switch (cmd) {
        case SET_VUI_SAMPLE_RATE:
            return vui_init_regs(g_aspi->client, (u32)arg, (u32)g_aspi->adc_clk);
        case SET_VUI_CH:
            g_aspi->chn_num = (u32)arg;
            break;
        case SET_VUI_DATAWIDTH:
            if (arg != 16 && arg != 32)
                return -1;
            g_aspi->dw = (u32)arg;
            break;
	    case START_VUI_DMA:
            spi_init();
            spivui_dma_start(g_aspi);
	    	break; 
	    case STOP_VUI_DMA:
            owl_spi_terminate_dma(g_aspi);
	    	spi_writel(0x0 , SPI_CTL);
	        break; 
        default:
            return -1;
    }
	return 0;
}

#ifdef CONFIG_COMPAT
static long
spivui_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return spivui_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define spivui_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int spivui_open(struct inode *inode, struct file *filp)
{
    int ret = 0;

    g_aspi->block = true;
    if (filp->f_flags & O_NONBLOCK)
        g_aspi->block = false;

    //init spi2 and set dma regs.
    ret = vui_init_regs(g_aspi->client, DEFAULT_SAMPLE_RATE, g_aspi->adc_clk);
    if (ret < 0)
        return -1;

    g_aspi->dw = DEFAULT_DATAWIDTH;
    g_aspi->chn_num = MAX_CH;

	return 0;
}

static int spivui_release(struct inode *inode, struct file *filp)
{
	struct dma_chan *rxchan = g_aspi->dma_rx_channel;
    //release spi2 and close dma.
    spi_writel(0x0, SPI_CTL);

    if (rxchan)
	    dmaengine_terminate_all(rxchan);

	return 0;
}

static const struct file_operations spivui_fops = {
	.owner =	THIS_MODULE,
	.write =	spivui_write,
	.read =		spivui_read,
	.unlocked_ioctl = spivui_ioctl,
	.compat_ioctl = spivui_compat_ioctl,
	.open =		spivui_open,
	.release =	spivui_release,
	.llseek =	no_llseek,
};

static int spivui_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret = -1;
    struct spivui_data *data;
	char i2c_clk_name[] = "i2c0";
	char spi_clk_name[] = "spi0";

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality error\n");
		return -EIO;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	data->client = client;
    ret = of_data_get(data);
    if (ret < 0)
        return ret;
    
    major = register_chrdev(0, "spivui", &spivui_fops);
    if (major < 0) {
        pr_err("Can't regist spivui chrdev!\n");
        return major;
    }
	spivui_class = class_create(THIS_MODULE, "spivui");
	if (IS_ERR(spivui_class)) {
		unregister_chrdev(0, "spivui");
		return PTR_ERR(spivui_class);
	}
    devno = MKDEV(major, 0);
	spivui_device = device_create(spivui_class, NULL, devno, NULL, "spivui");
	if (IS_ERR(spivui_device)) {
	    class_destroy(spivui_class);
		unregister_chrdev(major, "spivui");
		return PTR_ERR(spivui_device);
	}

    ret = vui_check_chipid(client);
    if (ret < 0)
        return -1;

	spi_clk_name[3] += (char)data->spi_bus;
	data->spi_clk = devm_clk_get(&client->dev, spi_clk_name);
	if (IS_ERR(data->spi_clk)) {
		pr_err("spi2 clock not found.\n");
	}

	ret = clk_prepare_enable(data->spi_clk);
	if (ret) {
		pr_err("Unable to enable AHB clock.\n");
	}

	i2c_clk_name[3] += (char)client->adapter->nr;
	data->i2c_clk = devm_clk_get(&client->dev, i2c_clk_name);
	if (IS_ERR(data->i2c_clk)) {
		pr_err("i2c0 clock not found.\n");
	}

	ret = clk_prepare_enable(data->i2c_clk);
	if (ret) {
		pr_err("Unable to enable i2c0 clock.\n");
	}

    data->dev = &client->dev;
	i2c_set_clientdata(client, data);
	if(owl_spi_dma_probe(data) < 0) {
        pr_err("probe dma failed!=====\n");
        return -1;
    }
    g_aspi = data;

    return 0;
}

static int spivui_remove(struct i2c_client *client)
{
	struct spivui_data *data = i2c_get_clientdata(client);

	device_destroy(spivui_class, devno);
	class_destroy(spivui_class);
	unregister_chrdev(major, "spivui");

	owl_spi_dma_remove(data);

    if (!gpio_is_valid(data->vui_en_gpios))
        gpio_free(data->vui_en_gpios);
    if (!gpio_is_valid(data->vui_reset_gpios))
        gpio_free(data->vui_reset_gpios);

	return 0;
}

static int spivui_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct spivui_data *data = i2c_get_clientdata(client);

    //TODO: Suspend deal.

	return 0;
}

static int spivui_resume(struct device *dev)
{
    int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct spivui_data *data = i2c_get_clientdata(client);

    //TODO: Vui init regs.

	return 0;
}

static SIMPLE_DEV_PM_OPS(spivui_pm_ops, spivui_suspend, spivui_resume);
#define SPIVUI_PM_OPS (&spivui_pm_ops)

static const struct i2c_device_id spivui_i2c_id[] = {
    {"att3008", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, spivui_i2c_id);

static const struct of_device_id spivui_of_match[] = {
	{ .compatible = "spivui", },
	{ },
};
MODULE_DEVICE_TABLE(of, spivui_of_match);

static struct i2c_driver spivui_driver = {
    .probe =            spivui_probe,
    .remove =           spivui_remove,
    .id_table =         spivui_i2c_id,
    .driver = {
            .name = "spivui",
            .owner  = THIS_MODULE,
            .pm = SPIVUI_PM_OPS,
            .of_match_table = spivui_of_match,
    },
};

module_i2c_driver(spivui_driver);

MODULE_AUTHOR("Yiguang <liuyiguang@actions-semi.com>");
MODULE_DESCRIPTION("Actions Devices VUI board(ATT3008) I2C-SPI driver");
MODULE_LICENSE("GPL v2");
