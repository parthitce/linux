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

static int dai_mode_hdmi_count;
static int audio_clk_enable;
static int Speaker;			/*Speaker Placement, stereo*/

#define Audio60958	1	/*1--IEC60958; 0--IEC61937*/
#define HDMI_RAMPKT_AUDIO_SLOT	1
#define HDMI_RAMPKT_PERIOD	1

/* define IC type to use */
static int ic_type_used = IC_NOT_USED;

struct asoc_dai_resource {
    void __iomem    *base[MAX_RES_NUM];/*virtual base for every resource*/
    void __iomem    *baseptr; /*pointer to every virtual base*/

    struct clk	    *hdmia_clk;
	struct clk      *clk;
    int             irq;
    unsigned int    setting;
    struct s900_pcm_dma_params dma_params;
};

/* ic information of platform */
struct ic_data {
	int ic_type;
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
	phys_addr_t i2s_reg_base;
	phys_addr_t hdmi_reg_base;
};

/* dai resources */
static struct asoc_dai_resource dai_res;
/* data of i2s/hdmi register address */
static struct audio_reg_addr dai_regs;

void __iomem *get_hdmi_dai_reg_base(int num)
{
	return dai_res.base[num];
}
EXPORT_SYMBOL_GPL(get_hdmi_dai_reg_base);

void set_hdmi_dai_reg_base(int num)
{
	dai_res.baseptr = dai_res.base[num];
}

EXPORT_SYMBOL_GPL(set_hdmi_dai_reg_base);

u32 snd_hdmi_dai_readl(u32 reg)
{
	u32 res;
	res = readl(dai_res.baseptr + reg);
	return res;
}

EXPORT_SYMBOL_GPL(snd_hdmi_dai_readl);

void snd_hdmi_dai_writel(u32 val, u32 reg)
{
	u32 reg_val;
	writel(val, dai_res.baseptr + reg);

	reg_val = readl(dai_res.baseptr + reg);

}
EXPORT_SYMBOL_GPL(snd_hdmi_dai_writel);

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

static int hdmi_EnableWriteRamPacket(void)
{
	int i;
	set_hdmi_dai_reg_base(HDMI_NUM);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_OPCR) |
		(0x1 << 31), HDMI_OPCR);
	while ((snd_hdmi_dai_readl(HDMI_OPCR) & (0x1 << 31)) != 0) {
		for (i = 0; i < 10; i++)
			;
	}
	return 0;
}

static int hdmi_SetRamPacket(unsigned no, unsigned char *pkt)
{
	unsigned char tpkt[36];
	unsigned int *reg = (unsigned int *) tpkt;
	unsigned int addr = 126 + no * 14;

	set_hdmi_dai_reg_base(HDMI_NUM);
	if (no > 5)
		return -EINVAL;

	/**
	 * according to change by genganan 2008-09-24
	 */
	/* Packet Header */
	tpkt[0] = pkt[0];
	tpkt[1] = pkt[1];
	tpkt[2] = pkt[2];
	tpkt[3] = 0;
	/* Packet Word0 */
	tpkt[4] = pkt[3];
	tpkt[5] = pkt[4];
	tpkt[6] = pkt[5];
	tpkt[7] = pkt[6];
	/* Packet Word1 */
	tpkt[8] = pkt[7];
	tpkt[9] = pkt[8];
	tpkt[10] = pkt[9];
	tpkt[11] = 0;
	/* Packet Word2 */
	tpkt[12] = pkt[10];
	tpkt[13] = pkt[11];
	tpkt[14] = pkt[12];
	tpkt[15] = pkt[13];
	/* Packet Word3 */
	tpkt[16] = pkt[14];
	tpkt[17] = pkt[15];
	tpkt[18] = pkt[16];
	tpkt[19] = 0;
	/* Packet Word4 */
	tpkt[20] = pkt[17];
	tpkt[21] = pkt[18];
	tpkt[22] = pkt[19];
	tpkt[23] = pkt[20];
	/* Packet Word5 */
	tpkt[24] = pkt[21];
	tpkt[25] = pkt[22];
	tpkt[26] = pkt[23];
	tpkt[27] = 0;
	/* Packet Word6 */
	tpkt[28] = pkt[24];
	tpkt[29] = pkt[25];
	tpkt[30] = pkt[26];
	tpkt[31] = pkt[27];
	/* Packet Word7 */
	tpkt[32] = pkt[28];
	tpkt[33] = pkt[29];
	tpkt[34] = pkt[30];
	tpkt[35] = 0;

	/* write mode */
	set_hdmi_dai_reg_base(HDMI_NUM);
	snd_hdmi_dai_writel((1 << 8) | (((addr) & 0xFF) << 0), HDMI_OPCR);
	snd_hdmi_dai_writel(reg[0], HDMI_ORP6PH);
	snd_hdmi_dai_writel(reg[1], HDMI_ORSP6W0);
	snd_hdmi_dai_writel(reg[2], HDMI_ORSP6W1);
	snd_hdmi_dai_writel(reg[3], HDMI_ORSP6W2);
	snd_hdmi_dai_writel(reg[4], HDMI_ORSP6W3);
	snd_hdmi_dai_writel(reg[5], HDMI_ORSP6W4);
	snd_hdmi_dai_writel(reg[6], HDMI_ORSP6W5);
	snd_hdmi_dai_writel(reg[7], HDMI_ORSP6W6);
	snd_hdmi_dai_writel(reg[8], HDMI_ORSP6W7);

	hdmi_EnableWriteRamPacket();

	return 0;
}

static int hdmi_SetRamPacketPeriod(unsigned int no, int period)
{
	if (no > 5)
		return -EINVAL;

	if ((period > 0xf) || (period < 0))
		return -EINVAL;

	/* disable */
	set_hdmi_dai_reg_base(HDMI_NUM);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_RPCR) &
		(unsigned int) (~(1 << no)), HDMI_RPCR);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_RPCR) &
		(unsigned int) (~(0xf << (no * 4 + 8))), HDMI_RPCR);

	if (period != 0) {
		/* enable and set period */
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_RPCR) |
		(unsigned int) (period << (no * 4 + 8)), HDMI_RPCR);
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_RPCR) |
			(unsigned int) (1 << no), HDMI_RPCR);
	}
	return 0;
}

static int hdmi_gen_audio_infoframe(int audio_channel)
{
		unsigned char pkt[32];
		unsigned int  checksum = 0;
		int i;
		memset(pkt, 0, 32);
		/* header */
		pkt[0] = 0x80 | 0x04;
		pkt[1] = 1;
		pkt[2] = 0x1f & 10;
		pkt[3] = 0x00;
		pkt[4] = audio_channel & 0x7;
		pkt[5] = 0x0;
		pkt[6] = 0x0;
		pkt[7] = Speaker;
		pkt[8] = (0x0 << 7) | (0x0 << 3);

		/* count checksum */
		for (i = 0; i < 31; i++)
			checksum += pkt[i];

		pkt[3] = (unsigned char)((~checksum + 1) & 0xff);
    /* set to RAM Packet */
		hdmi_SetRamPacket(HDMI_RAMPKT_AUDIO_SLOT, pkt);
		hdmi_SetRamPacketPeriod(HDMI_RAMPKT_AUDIO_SLOT,
		HDMI_RAMPKT_PERIOD);
		return 0;
}
void set_hdmi_audio_interface(int channel, int samplerate)
{
	unsigned int tmp03;
	unsigned int tmp47;
	unsigned int CRP_N = 0;
	unsigned int ASPCR = 0;
	unsigned int ACACR = 0;

	/*改变音频相关参数时需要首先disable audio, 配置完成后enable*/
	set_hdmi_dai_reg_base(HDMI_NUM);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_ICR) &
		~(0x1 << 25), HDMI_ICR);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_ACRPCR) |
		(0x1 << 31), HDMI_ACRPCR);
	snd_hdmi_dai_readl(HDMI_ACRPCR); /*flush write buffer effect*/

	tmp03 = snd_hdmi_dai_readl(HDMI_AICHSTABYTE0TO3);
	tmp03 &= (~(0xf << 24));

	tmp47 = snd_hdmi_dai_readl(HDMI_AICHSTABYTE4TO7);
	tmp47 &= (~(0xf << 4));
	tmp47 |= 0xb;

	switch (samplerate) {
	/* 32000, 44100, 48000, 88200, 96000,
	176400, 192000, 352.8kHz, 384kHz */
	case 1:
		tmp03 |= (0x3 << 24);
		tmp47 |= (0xc << 4);
		CRP_N = 4096;
		break;

	case 2:
		tmp03 |= (0x0 << 24);
		tmp47 |= (0xf << 4);
		CRP_N = 6272;
		break;

	case 3:
		tmp03 |= (0x2 << 24);
		tmp47 |= (0xd << 4);
		CRP_N = 6144;
		break;

	case 4:
		tmp03 |= (0x8 << 24);
		tmp47 |= (0x7 << 4);
		CRP_N = 12544;
		break;

	case 5:
		tmp03 |= (0xa << 24);
		tmp47 |= (0x5 << 4);
		CRP_N = 12288;
		break;

	case 6:
		tmp03 |= (0xc << 24);
		tmp47 |= (0x3 << 4);
		CRP_N = 12288;
		break;

	case 7:
		tmp03 |= (0xe << 24);
		tmp47 |= (0x1 << 4);
		CRP_N = 24576;
		break;

	case 8:
		tmp03 |= (0x1 << 24);
		CRP_N = 12544;
		break;

	case 9:
		tmp03 |= (0x1 << 24);
		CRP_N = 12288;
		break;

	default:
		break;
	}
	snd_hdmi_dai_writel(tmp03, HDMI_AICHSTABYTE0TO3);
	snd_hdmi_dai_writel(tmp47, HDMI_AICHSTABYTE4TO7);

	snd_hdmi_dai_writel(0x0, HDMI_AICHSTABYTE8TO11);
	snd_hdmi_dai_writel(0x0, HDMI_AICHSTABYTE12TO15);
	snd_hdmi_dai_writel(0x0, HDMI_AICHSTABYTE16TO19);
	snd_hdmi_dai_writel(0x0, HDMI_AICHSTABYTE20TO23);

	switch (channel) {
	case 2:
		snd_hdmi_dai_writel(0x20001, HDMI_AICHSTASCN);
		break;

	case 3:
		snd_hdmi_dai_writel(0x121, HDMI_AICHSTASCN);
		break;

	case 4:
		snd_hdmi_dai_writel(0x2121, HDMI_AICHSTASCN);
		break;

	case 5:
		snd_hdmi_dai_writel(0x12121, HDMI_AICHSTASCN);
		break;

	case 6:
		snd_hdmi_dai_writel(0x212121, HDMI_AICHSTASCN);
		break;

	case 7:
		snd_hdmi_dai_writel(0x1212121, HDMI_AICHSTASCN);
		break;

	case 8:
		snd_hdmi_dai_writel(0x21212121, HDMI_AICHSTASCN);
		break;

	default:
		break;
	}
	/* TODO samplesize 16bit, 20bit */
	/* 24 bit */
	snd_hdmi_dai_writel((snd_hdmi_dai_readl(HDMI_AICHSTABYTE4TO7) & ~0xf),
		HDMI_AICHSTABYTE4TO7);
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_AICHSTABYTE4TO7) |
		0xb, HDMI_AICHSTABYTE4TO7);
	if (Audio60958 == 1) {
		switch (channel) {
		case 2:
			ASPCR = 0x00000011;
			ACACR = 0xfac688;
			Speaker = 0x0;
			break;

		case 3:
			ASPCR = 0x0002d713;
			ACACR = 0x4008;
			Speaker = 0x1;
			break;

		case 4:
			ASPCR = 0x0003df1b;
			ACACR = 0x4608;
			Speaker = 0x3;
			break;

		case 5:
			ASPCR = 0x0003df3b;
			ACACR = 0x2c608;
			Speaker = 0x7;
			break;

		case 6:
			ASPCR = 0x0003df3f;
			ACACR = 0x2c688;
			Speaker = 0xb;
			break;

		case 7:
			ASPCR = 0x0007ff7f;
			ACACR = 0x1ac688;
			Speaker = 0xf;
			break;

		case 8:
			ASPCR = 0x0007ffff;
			ACACR = 0xfac688;
			Speaker = 0x13;
			break;

		default:
			break;
		}
	} else {
		ASPCR = 0x7f87c003;
		ACACR = 0xfac688;

		tmp03 = snd_hdmi_dai_readl(HDMI_AICHSTABYTE0TO3);
		tmp03 |= 0x1;
		snd_hdmi_dai_writel(tmp03, HDMI_AICHSTABYTE0TO3);
		snd_hdmi_dai_writel(0x21, HDMI_AICHSTASCN);
	}

	/* enable Audio FIFO_FILL  disable wait cycle */
	snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_CR) | 0x50, HDMI_CR);

	if (samplerate > 7)
		ASPCR |= (0x1 << 31);

	snd_hdmi_dai_writel(ASPCR, HDMI_ASPCR);
	snd_hdmi_dai_writel(ACACR, HDMI_ACACR);
    /*非压缩格式23~30位写0
    * 如果针对压缩码流,
    则HDMI_AICHSTABYTE0TO3的bit[1:0]=0x2（5005新加）;
    * 如果针对线性PCM码流，则HDMI_AICHSTABYTE0TO3
    的bit[1:0]=0x0（同227A）;
    */
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_AICHSTABYTE0TO3) &
			~0x3, HDMI_AICHSTABYTE0TO3);

    /* 如果针对压缩码流，则
    HDMI_ASPCR的bit[30:23]=0xff（5005新加）;
     *  如果针对线性PCM码流,
     则HDMI_ASPCR的bit[30:23]=0x0（同227A）;
     */
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_ASPCR) &
			~(0xff << 23), HDMI_ASPCR);

		snd_hdmi_dai_writel(CRP_N | (0x1 << 31), HDMI_ACRPCR);
		hdmi_gen_audio_infoframe(channel - 1);

    /*****配置完音频相关参数后enable audio*/
    /* enable CRP */
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_ACRPCR) &
			~(0x1 << 31), HDMI_ACRPCR);

    /* enable Audio Interface */
		snd_hdmi_dai_writel(snd_hdmi_dai_readl(HDMI_ICR) |
			(0x1 << 25), HDMI_ICR);
}

static int get_hdmi_audio_fs(int sample_rate)
{
	int AudioFS;
	int fs = sample_rate / 1000;

	/* for O_MODE_HDMI */
	/* 32000, 44100, 48000, 88200, 96000, 176400,
	192000, 352.8kHz, 384kHz */
	switch (fs) {
	case 32:
		AudioFS = 1;
		break;
	case 44:
		AudioFS = 2;
		break;
	case 48:
		AudioFS = 3;
		break;
	case 88:
		AudioFS = 4;
		break;
	case 96:
		AudioFS = 5;
		break;
	case 176:
		AudioFS = 6;
		break;
	case 192:
		AudioFS = 7;
		break;
	case 352:
		AudioFS = 8;
		break;
	case 384:
		AudioFS = 9;
		break;
	default:
		AudioFS = 3;
		break;
	}
	return AudioFS;
}

/******************************************************************************/
/*!
 * \par  Description:
 *    将采样率转换成矊 *¬件寄存器设置所需的索引值
 * \param[in]    sample_rate  采样率
 * \param[in]    mode 输入输出模
 *式 （如果是spdif或者hdmi，index不同）
 * \return       索引值
 * \retval           -1 failed
 * \ingroup      sndrv
 *****************************************/
static int get_sf_index(int sample_rate)
{
	int i = 0;
	char fs = sample_rate / 1000;
	/* 44k 在基础上寄存器bit位基础上加16 */
	static fs_t fs_list[] = {
		{ 384, { -1, 0} },
		{ 352, { -1, 16} },
		{ 192, { 0,  1} },
		{ 176, { 16, 17} },
		{ 96,  { 1,  3} },
		{ 88,  { 17, 19} },
		{ 64,  { 2, -1} },
		{ 48,  { 3,  5} },
		{ 44,  { 19, 21} },
		{ 32,  { 4,  6} },
		{ 24,  { 5, -1} },
		{ 22,  {21, -1} },
		{ 16,  { 6, -1} },
		{ 12,  { 7, -1} },
		{ 11,  {23, -1} },
		{ 8,   { 8, -1} },
		{ -1,  {-1, -1} }
	};

	while ((fs_list[i].sample_rate > 0) && (fs_list[i].sample_rate != fs))
		i++;

	return fs_list[i].index[1];
}

static int s900_hdmi_dai_clk_set(int rate)
{
	unsigned long reg_val;
	int sf_index, ret = -1;

	if (audio_clk_enable == 0) {
		sf_index = get_sf_index(rate);
		if (sf_index & 0x10)
			reg_val = 45158400;
		else
			reg_val = 49152000;

		ret = clk_set_rate(dai_res.clk, reg_val);
		clk_prepare_enable(dai_res.clk);
		if (ret < 0) {
			snd_err("audiopll set error!\n");
			return ret;
		}

		ret = clk_set_rate(dai_res.hdmia_clk, rate << 7);
		clk_prepare_enable(dai_res.hdmia_clk);
		if (ret) {
			snd_dbg("i2stx clk rate set error!!\n");
			return ret;
		}
		audio_clk_enable = 1;
	}

	return 0;
}

static int s900_hdmi_dai_clk_disable()
{
	if (audio_clk_enable == 1) {
		audio_clk_enable = 0;
		if (dai_res.hdmia_clk) {
			clk_disable(dai_res.hdmia_clk);
		} else {
			return -1;
		}		
	}
	return 0;
}


static int s900_hdmi_dai_mode_set(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	int ret;

	/* HDMI&SPDIF fifo reset */
	set_hdmi_dai_reg_base(I2S_SPDIF_NUM);
	if (dai_mode_hdmi_count == 1) {
		snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x3,
				SPDIF_HDMI_CTL);
		/* HDMI fifo enable,DRQ enable */
		snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) |
				0x102, SPDIF_HDMI_CTL);
	}
	//dai_mode_hdmi_count++;

	return ret;
}

static int s900_hdmi_dai_mode_unset(struct s900_pcm_priv *pcm_priv)
{
	/* HDMI fifo disable */
	if(dai_mode_hdmi_count > 0)
		dai_mode_hdmi_count--;

	if (dai_mode_hdmi_count == 0) {
		set_hdmi_dai_reg_base(I2S_SPDIF_NUM);
		snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x2, SPDIF_HDMI_CTL);
	}
}

static int s900_hdmi_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	if (SNDRV_PCM_STREAM_PLAYBACK== substream->stream) {
		snd_soc_dai_set_dma_data(dai, substream, &(dai_res.dma_params));
	}
	dai_mode_hdmi_count++;
	
	return 0;
}

static int s900_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv
		= snd_soc_platform_get_drvdata(platform);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		return -EINVAL;
	}

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		int rate = params_rate(params);
		int audio_fs = get_hdmi_audio_fs(rate);
		/* we set the hdmi audio channels stereo now*/
		set_hdmi_audio_interface(2, audio_fs);
		s900_hdmi_dai_clk_set(params_rate(params));
		s900_hdmi_dai_mode_set(pcm_priv, dai);
	}
	return 0;
}

static int s900_hdmi_dai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		s900_hdmi_dai_clk_disable();
		s900_hdmi_dai_mode_unset(pcm_priv);
	}
	return 0;
}

struct snd_soc_dai_ops s900_hdmi_dai_ops = {
	.startup	= s900_hdmi_dai_startup,
	.hw_params = s900_hdmi_dai_hw_params,
	.hw_free = s900_hdmi_dai_hw_free,
};

#define S900_STEREO_CAPTURE_RATES SNDRV_PCM_RATE_8000_96000
#define S900_STEREO_PLAYBACK_RATES SNDRV_PCM_RATE_8000_192000
#define S900_FORMATS SNDRV_PCM_FMTBIT_S16_LE

struct snd_soc_dai_driver s900_hdmi_dai = {
	.name = "owl-audio-hdmi",
	.id = S900_AIF_HDMI,
	.playback = {
		.stream_name = "s900 hdmi dai Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = S900_STEREO_PLAYBACK_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &s900_hdmi_dai_ops,
};

static const struct snd_soc_component_driver s900_hdmi_component = {
	.name		= "s900_hdmi",
};

/* modify compatible to match diff IC */
static const struct of_device_id owl_hdmi_of_match[] = {
	{.compatible = "actions,s700-audio-hdmi", .data = &ic_s700,},
	{.compatible = "actions,s900-audio-hdmi", .data = &ic_s900,},
	{},
};

MODULE_DEVICE_TABLE(of, owl_hdmi_of_match);

static int s900_hdmi_dai_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct resource *res;
	int i;
	int ret = 0;

	struct ic_data *ic;

	/* dn = of_find_compatible_node(NULL, NULL, "actions,owl-audio-i2s"); */
	/* get IC type by match device */
	id = of_match_device(of_match_ptr(owl_hdmi_of_match), &pdev->dev);
	if (!id) {
		printk(KERN_ERR"Fail to match device_node actions,owl-audio-hdmi!\n");
		return -ENODEV;
	}
	ic = id->data;
	ic_type_used = ic->ic_type;

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

	/* get resource of i2s and hdmi from dts file */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		snd_err("no memory resource of i2s!\n");
		return -ENODEV;
	}
	/* get virtual base for i2s */
	dai_res.base[I2S_SPDIF_NUM] = ioremap(res->start, resource_size(res));
	if (dai_res.base[I2S_SPDIF_NUM] == NULL) {
		snd_err("Unable to ioremap register region of i2s\n");
		return -ENXIO;
	}
	/* get phys_addr of i2s register from dts file */
	dai_regs.i2s_reg_base = res->start;
	dai_res.dma_params.dma_addr = dai_regs.i2s_reg_base + HDMI_DAT;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmi_base");
	if (!res) {
		snd_err("no memory resource of hdmi!\n");
		return -ENODEV;
	}
	/* get virtual base for hdmi */
	dai_res.base[HDMI_NUM] = ioremap(res->start, resource_size(res));
	if (dai_res.base[HDMI_NUM] == NULL) {
		snd_err("Unable to ioremap register region of hdmi\n");
		return -ENXIO;
	}
	/* get phys_addr of hdmi register from dts file */
	dai_regs.hdmi_reg_base = res->start;


	printk(KERN_INFO"it's ok to get resource of owl-audio-hdmi !\n");

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
	
	dai_res.clk = devm_clk_get(&pdev->dev, "audio_pll");
	if (IS_ERR(dai_res.clk)) {
		snd_err("no audio clock defined\n");
		ret = PTR_ERR(dai_res.clk);
		dai_res.clk = NULL;
		return ret;
	}



	dai_res.hdmia_clk = devm_clk_get(&pdev->dev, "hdmia");
	if (IS_ERR(dai_res.hdmia_clk)) {
		snd_err("no hdmia clock defined\n");
		ret = PTR_ERR(dai_res.hdmia_clk);
		dai_res.hdmia_clk = NULL;
		return ret;
	}

	dai_res.dma_params.dma_chan= dma_request_slave_channel(&pdev->dev, "hdmia");
	if (!dai_res.dma_params.dma_chan) {
		dev_warn(&pdev->dev, "request hdmi chan failed\n");
		return ret;
	}

	dev_warn(&pdev->dev, "s900_hdmi_dai_probe\n");

	pdev->dev.init_name = "owl-audio-hdmi";

	return snd_soc_register_component(&pdev->dev, &s900_hdmi_component,
					 &s900_hdmi_dai, 1);

}

static int s900_hdmi_dai_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	/* release virtual dma channels */
	if (dai_res.dma_params.dma_chan) {
		dma_release_channel(dai_res.dma_params.dma_chan);
	}


	if (dai_res.hdmia_clk) {
		devm_clk_put(&pdev->dev, dai_res.hdmia_clk);
		dai_res.hdmia_clk = NULL;
	}


	if (dai_res.clk) {
		devm_clk_put(&pdev->dev, dai_res.clk);
		dai_res.clk = NULL;
	}
	return 0;
}



static struct platform_driver s900_hdmi_dai_driver = {
	.driver = {
		.name = "owl-audio-hdmi",
		.owner = THIS_MODULE,
		.of_match_table = owl_hdmi_of_match,
	},

	.probe = s900_hdmi_dai_probe,
	.remove = s900_hdmi_dai_remove,
};

/*static struct platform_device *s900_dai_device;*/

static int __init s900_hdmi_dai_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&s900_hdmi_dai_driver);
	if (ret) {
		snd_err("ASoC: Platform driver s900-hdmi-dai register failed\n");
	}

	return ret;
}

static void __exit s900_hdmi_dai_exit(void)
{
	int i = 0;
	struct device *dev = NULL;

    dev = bus_find_device_by_name(&platform_bus_type, NULL, "owl-audio-hdmi");

	if (dev) {
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) {
			device_remove_file(dev, &dai_attr[i]);
		}
	}

	platform_driver_unregister(&s900_hdmi_dai_driver);
}

module_init(s900_hdmi_dai_init);
module_exit(s900_hdmi_dai_exit);


/* Module information */
MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 HDMI Interface");
MODULE_LICENSE("GPL");
