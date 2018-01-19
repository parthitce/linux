#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <linux/clk.h>			/* clk_enable */
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include "../sndrv-owl.h"
#include "../common-regs-owl.h"

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <linux/mfd/atc260x/atc260x.h>

/* define IC type to use */
static int ic_type_used = IC_NOT_USED;

struct asoc_dai_resource {
    void __iomem    *base[MAX_RES_NUM];/*virtual base for every resource*/
    void __iomem    *baseptr; /*pointer to every virtual base*/
	struct clk	    *spdif_clk;
	struct clk	    *spi_clk;
	struct clk	    *clk;
    int             irq;
    unsigned int    setting;
	struct s900_pcm_dma_params dma_params;
};

/* ic information of platform */
struct ic_data {
	int ic_type;
};

/* ic information of s700 */
static const struct ic_data  ic_3605 = {
	.ic_type = IC_PLATFM_3605,
};

/* ic information of s700 */
static const struct ic_data  ic_s700 = {
	.ic_type = IC_PLATFM_S700,
};

/* ic information of s900 */
static const struct ic_data  ic_s900 = {
	.ic_type = IC_PLATFM_S900,
};

/* store base register address of i2s and hdmi, pcm */
struct audio_reg_addr {
	phys_addr_t spi_reg_base;
	phys_addr_t mfp_reg_base;
	phys_addr_t cmu_reg_base;
};

/* dai resources */
static struct asoc_dai_resource dai_res;
/* data of i2s/hdmi register address */
static struct audio_reg_addr dai_regs;

phys_addr_t get_dai_spi_reg_base(void)
{
	return dai_regs.spi_reg_base;
}
EXPORT_SYMBOL_GPL(get_dai_spi_reg_base);

void set_spi_dai_reg_base(int num)
{
	dai_res.baseptr = dai_res.base[num];
}

EXPORT_SYMBOL_GPL(set_spi_dai_reg_base);

u32 snd_spi_dai_readl(u32 reg)
{
	u32 res;
	res = readl(dai_res.baseptr + reg);
	return res;
}

EXPORT_SYMBOL_GPL(snd_spi_dai_readl);

void snd_spi_dai_writel(u32 val, u32 reg)
{
	u32 reg_val;
	writel(val, dai_res.baseptr + reg);

	reg_val = readl(dai_res.baseptr + reg);

}
EXPORT_SYMBOL_GPL(snd_spi_dai_writel);

static ssize_t error_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", error_switch);
	return cnt;
}

static ssize_t error_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		error_switch = tmp;
		break;
	default:
		printk(KERN_ERR"invalid input\n");
		break;
	}
	return count;
}

static ssize_t debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", debug_switch);
	return cnt;
}

static ssize_t debug_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		debug_switch = tmp;
		break;
	default:
		printk(KERN_INFO"invalid input\n");
		break;
	}
	return count;
}

static struct device_attribute dai_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

static int s900_spi_dai_record_clk_set(int rate)
{
	unsigned long reg_val;
	int sf_index, ret = -1;		

	ret = clk_prepare_enable(dai_res.spi_clk);
	if (ret) {
		snd_err("spi clk rate set error!!\n");
		return ret;
	}

	return 0;
}

static int s900_spi_dai_record_clk_disable(int mode)
{
	clk_disable(dai_res.spi_clk);
	return 0;
}

static int s900_spi_trigger(struct snd_pcm_substream *substream,
	int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned long flags;

	if(substream->stream != SNDRV_PCM_STREAM_CAPTURE){
		return -EINVAL;
	}
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		set_spi_dai_reg_base(SPI_NUM);
		snd_spi_dai_writel(snd_spi_dai_readl(SPI_STAT), SPI_STAT);
		snd_spi_dai_writel(0xFFF0, SPI_RXCR);
		snd_spi_dai_writel(0xA446C3, SPI_CTL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		set_spi_dai_reg_base(SPI_NUM);
		snd_spi_dai_writel(0x00, SPI_CTL);
		break;
	}

	return 0;
}

static int s900_spi_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	if (SNDRV_PCM_STREAM_CAPTURE== substream->stream) {
		snd_soc_dai_set_dma_data(dai, substream, &(dai_res.dma_params));
	}
	
	return 0;
}

static int s900_spi_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	unsigned long reg_val;
	int ret;
	reg_val = 49152000;
	
	clk_prepare(dai_res.clk);
	ret = clk_set_rate(dai_res.clk, reg_val);
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}
	ret = clk_set_rate(dai_res.spdif_clk, ((freq * 3) << 8));//supply 12.288M  as MCLK 
	if (ret) {
		snd_dbg("spdif clk rate set error!!\n");
		return ret;
	}
	set_spi_dai_reg_base(MFP_NUM);
	snd_spi_dai_writel(snd_spi_dai_readl(DEBUG_SEL) & ~(0x1F), DEBUG_SEL);
	snd_spi_dai_writel(snd_spi_dai_readl(DEBUG_OEN0) | (1<<28), DEBUG_OEN0);
	set_spi_dai_reg_base(CMU_NUM);
	snd_spi_dai_writel((1<<31)|25, CMU_DIGITALDEBUG);
	snd_spi_dai_writel(snd_spi_dai_readl(CMU_DEVCLKEN0) | (1<<23), CMU_DEVCLKEN0);

	return 0;
}

static int s900_spi_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	
	if(substream->stream != SNDRV_PCM_STREAM_CAPTURE){
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		return -EINVAL;
	}
	s900_spi_dai_record_clk_set(params_rate(params));

	set_spi_dai_reg_base(SPI_NUM);
	snd_spi_dai_writel(0x10, SPI_CTL);
	snd_spi_dai_writel(20, SPI_CLKDIV);
	snd_spi_dai_writel(0xFF, SPI_STAT);
	return 0;
}

struct snd_soc_dai_ops s900_spi_dai_ops = {
	.startup	= s900_spi_dai_startup,
	.trigger = s900_spi_trigger,
	.set_sysclk = s900_spi_dai_set_sysclk,
	.hw_params = s900_spi_dai_hw_params,
};

#define S900_RATES SNDRV_PCM_RATE_16000
#define S900_FORMATS SNDRV_PCM_FMTBIT_S32_LE

struct snd_soc_dai_driver s900_spi_dai = {
	.name = "owl-audio-spi",
	.id = 0,
	.capture = {
		.stream_name = "s900 spi dai Capture",
		.channels_min = 4,
		.channels_max = 4,
		.rates = S900_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &s900_spi_dai_ops,
};

static const struct snd_soc_component_driver s900_spi_component = {
	.name		= "s900_spi",
};

/* modify compatible to match diff IC */
static const struct of_device_id owl_spi_of_match[] = {
	{.compatible = "actions,ats3605-audio-spi", .data = &ic_3605,},
	{.compatible = "actions,s700-audio-spi", .data = &ic_s700,},
	{.compatible = "actions,s900-audio-spi", .data = &ic_s900,},
	{},
};

MODULE_DEVICE_TABLE(of, owl_spi_of_match);

static int s900_spi_dai_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct resource *res;
	int i;
	int ret = 0;

	struct ic_data *ic;

	/* dn = of_find_compatible_node(NULL, NULL, "actions,owl-audio-i2s"); */
	/* get IC type by match device */
	id = of_match_device(of_match_ptr(owl_spi_of_match), &pdev->dev);
	if (!id) {
		printk(KERN_ERR"Fail to match device_node actions,owl-audio-spi!\n");
		return -ENODEV;
	}
	ic = id->data;
	ic_type_used = ic->ic_type;

	if (ic_type_used == IC_PLATFM_3605) {
		printk(KERN_INFO"audio: IC type is 3605!\n");
	}
	if (ic_type_used == IC_PLATFM_S700) {
		printk(KERN_INFO"audio: IC type is S700!\n");
	}
	if (ic_type_used == IC_PLATFM_S900) {
		printk(KERN_INFO"audio: IC type is S900!\n");
	}
	if (ic_type_used == IC_NOT_USED) {
		printk(KERN_INFO"audio: IC type is not used!\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mfp_base");
	if (!res) {
		snd_err("no memory resource of mfp!\n");
		return -ENODEV;
	}else{

		/* get virtual base for mfp */
		//dai_res.base[MFP_NUM] =devm_ioremap_resource(&pdev->dev, res);
		dai_res.base[MFP_NUM] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (dai_res.base[MFP_NUM] == NULL) {
			printk(KERN_INFO"Unable to ioremap register region of mfp\n");
			return -ENXIO;
		}
		/* get phys_addr of hdmi register from dts file */
		dai_regs.mfp_reg_base = res->start;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmu_base");
	if (!res) {
		snd_err("no memory resource of cmu!\n");
		return -ENODEV;
	}else{

		/* get virtual base for mfp */
		//dai_res.base[CMU_NUM] =devm_ioremap_resource(&pdev->dev, res);
		dai_res.base[CMU_NUM] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (dai_res.base[CMU_NUM] == NULL) {
			printk(KERN_INFO"Unable to ioremap register region of cmu\n");
			return -ENXIO;
		}
		/* get phys_addr of hdmi register from dts file */
		dai_regs.cmu_reg_base = res->start;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spi2_base");
	if (!res) {
		snd_err("no memory resource of spi!\n");
		return -ENODEV;
	}else{

		/* get virtual base for mfp */
		//dai_res.base[CMU_NUM] =devm_ioremap_resource(&pdev->dev, res);
		dai_res.base[SPI_NUM] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (dai_res.base[SPI_NUM] == NULL) {
			printk(KERN_INFO"Unable to ioremap register region of spi\n");
			return -ENXIO;
		}
		/* get phys_addr of hdmi register from dts file */
		dai_regs.spi_reg_base = res->start;
	}
	
	dai_res.dma_params.dma_addr = dai_regs.spi_reg_base + SPI_RXDAT;
	
	printk(KERN_INFO"it's ok to get resource of owl-audio-spi !\n");

	if (1) {
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) {
			ret = device_create_file(&pdev->dev, &dai_attr[i]);
			if (ret) {
				snd_err("Add device file failed");
				/*goto device_create_file_failed;*/
			}
		}
	} else {
		snd_err("Find device failed");
		/*goto err_bus_find_device;	*/
	}

	dai_res.spdif_clk= devm_clk_get(&pdev->dev, "spdif");
	if (IS_ERR(dai_res.spdif_clk)) {
		snd_err("no spi clock defined\n");
		ret = PTR_ERR(dai_res.spdif_clk);
		return ret;
	}

	dai_res.spi_clk= devm_clk_get(&pdev->dev, "spi2");
	if (IS_ERR(dai_res.spi_clk)) {
		snd_err("no spi clock defined\n");
		ret = PTR_ERR(dai_res.spi_clk);
		return ret;
	}

	dai_res.clk= devm_clk_get(&pdev->dev, "audio_pll");
	if (IS_ERR(dai_res.clk)) {
		snd_err("no audio_pll clock defined\n");
		ret = PTR_ERR(dai_res.clk);
		return ret;
	}

	dai_res.dma_params.dma_chan= dma_request_slave_channel(&pdev->dev, "spirx");
	if(!dai_res.dma_params.dma_chan)
	{
		dev_warn(&pdev->dev, "request spirx chan failed\n");
		return ret;	
	}

	dev_warn(&pdev->dev, "s900_spi_dai_probe\n");

	pdev->dev.init_name = "owl-audio-spi";
	
	return snd_soc_register_component(&pdev->dev, &s900_spi_component,
				&s900_spi_dai, 1);

}

static int s900_spi_dai_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	
	if(dai_res.dma_params.dma_chan){
		dma_release_channel(dai_res.dma_params.dma_chan);
	}

	if(dai_res.spi_clk) {
		devm_clk_put(&pdev->dev, dai_res.spi_clk);
		dai_res.spi_clk = NULL;
	}

	if(dai_res.spdif_clk) {
		devm_clk_put(&pdev->dev, dai_res.spdif_clk);
		dai_res.spdif_clk = NULL;
	}

	if(dai_res.clk) {
		devm_clk_put(&pdev->dev, dai_res.clk);
		dai_res.clk = NULL;
	}
	return 0;
}



static struct platform_driver s900_spi_dai_driver = {
	.driver = {
		.name = "owl-audio-spi",
		.owner = THIS_MODULE,
		.of_match_table = owl_spi_of_match,
	},

	.probe = s900_spi_dai_probe,
	.remove = s900_spi_dai_remove,
};

/*static struct platform_device *s900_dai_device;*/

static int __init s900_spi_dai_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&s900_spi_dai_driver);
	if (ret) {
		snd_err("ASoC: Platform driver s900-dai spi register failed\n");
	}

	return ret;
}

static void __exit s900_spi_dai_exit(void)
{
	int i = 0;
	struct device *dev = NULL;

    dev = bus_find_device_by_name(&platform_bus_type, NULL, "owl-audio-spi");

	if (dev) {
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) {
			device_remove_file(dev, &dai_attr[i]);
		}
	}

	platform_driver_unregister(&s900_spi_dai_driver);
}

module_init(s900_spi_dai_init);
module_exit(s900_spi_dai_exit);


/* Module information */
MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 I2S Interface");
MODULE_LICENSE("GPL");
