/*
 * Copyright (c) 2015 Actions Semi Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * Change log:
 *	2015/9/9: Created by Lipeng.
 */
#ifndef __OWL_HDMI_H
#define __OWL_HDMI_H

#include <video/owl_dss.h>

enum hdmi_core_hdmi_dvi {
	HDMI_DVI = 0,
	HDMI_HDMI = 1,
	MHL_24BIT = 2,
	MHL_PK = 3
};

enum SRC_SEL {
	VITD = 0,
	DE,
	SRC_MAX
};

enum hdmi_core_packet_mode {
	HDMI_PACKETMODERESERVEDVALUE = 0,
	HDMI_PACKETMODE24BITPERPIXEL = 4,
	HDMI_PACKETMODE30BITPERPIXEL = 5,
	HDMI_PACKETMODE36BITPERPIXEL = 6,
	HDMI_PACKETMODE48BITPERPIXEL = 7
};

enum hdmi_packing_mode {
	HDMI_PACK_10b_RGB_YUV444 = 0,
	HDMI_PACK_24b_RGB_YUV444_YUV422 = 1,
	HDMI_PACK_20b_YUV422 = 2,
	HDMI_PACK_ALREADYPACKED = 7
};

enum hdmi_pixel_format {
	RGB444 = 0,
	YUV444 = 2
};

enum hdmi_deep_color {
	color_mode_24bit = 0,
	color_mode_30bit = 1,
	color_mode_36bit = 2,
	color_mode_48bit = 3
};

/*
 * a configuration structure to convet HDMI resolutoins
 * between vid and owl_video_timings.
 * vid is some fix number defined by HDMI spec,
 * are used in EDID etc.
 */

enum hdmi_vid_table {
	VID640x480P_60_4VS3 = 1,
	VID720x480P_60_4VS3,
	VID1280x720P_60_16VS9 = 4,
	VID1920x1080I_60_16VS9,
	VID720x480I_60_4VS3,
	VID1920x1080P_60_16VS9 = 16,
	VID720x576P_50_4VS3,
	VID1280x720P_50_16VS9 = 19,
	VID1920x1080I_50_16VS9,
	VID720x576I_50_4VS3,
	VID1440x576P_50_4VS3 = 29,
	VID1920x1080P_50_16VS9 = 31,
	VID1920x1080P_24_16VS9,
	VID3840x2160p_24 = 93,
	VID3840x2160p_25,
	VID3840x2160p_30,
	VID4096x2160p_24 = 98,
	VID4096x2160p_25,
	VID4096x2160p_30,

	/* some specail VID */
	VID1280x1024p_60 = 124,
	VID2560x1024p_60 = 125,
	VID2560x1024p_75 = 126,
	VID3840x1080p_60 = 127,
};

enum hdmi_packet_type {
	PACKET_AVI_SLOT		= 0,
	PACKET_AUDIO_SLOT	= 1,
	PACKET_SPD_SLOT		= 2,
	PACKET_GBD_SLOT		= 3,
	PACKET_VS_SLOT		= 4,
	PACKET_HFVS_SLOT	= 5,
	PACKET_MAX,
};


struct hdmi_config {
	int			vid;

	struct owl_videomode	mode;

	bool			interlace;
	int			vstart;	/* Vsync start line */
	bool			repeat;	/* video data repetetion */
};

#define HDMI_EDID_LEN		1024

struct hdmi_edid {
	u8			edid_buf[HDMI_EDID_LEN];
	u8			device_support_vic[512];
	u8			hdmi_mode;
	u8			ycbcr444_support;
	u32			video_formats[4];
	bool			read_ok;
};

struct hdmi_hdcp {
	int hdcp_oper_state;
	int hdcp_a2;
	int hdcp_oper_retry;
	int repeater;
	int ri;
	int ri_read;
	int retry_times_for_set_up_5_second;

	int hdcp_check_status;
	int need_to_delay;
	int check_state;
	int hot_plug_pin_status;
	int hdcp_have_goin_authentication;
	int hdcp_have_open_authentication;
	int hdcp_have_authentication_finished;
	int hdcp_error_timer_open;

	unsigned char bksv[5];
	unsigned char b_status[2];
	unsigned char ksv_list[128*5];
	unsigned char vp[20];
	unsigned char hdcp_oper_m0[8];

	int i2c_error;
	int hdcp_fail_times;

	struct workqueue_struct *wq;
	struct delayed_work hdcp_check_work;
	struct delayed_work hdcp_ri_update_work;
	struct delayed_work hdcp_read_key_work;
	bool read_hdcp_success;
	bool hdcp_authentication_success;
};

/*===========================================================================
 *				HDMI IP
 *=========================================================================*/
struct hdmi_ip;

struct hdmi_ip_ops {
	int (*init)(struct hdmi_ip *ip);
	void (*exit)(struct hdmi_ip *ip);

	int (*power_on)(struct hdmi_ip *ip);
	void (*power_off)(struct hdmi_ip *ip);
	bool (*is_power_on)(struct hdmi_ip *ip);

	void (*hpd_enable)(struct hdmi_ip *ip);
	void (*hpd_disable)(struct hdmi_ip *ip);
	bool (*hpd_is_pending)(struct hdmi_ip *ip);
	void (*hpd_clear_pending)(struct hdmi_ip *ip);
	bool (*cable_status)(struct hdmi_ip *ip);

	int (*video_enable)(struct hdmi_ip *ip);
	void (*video_disable)(struct hdmi_ip *ip);
	bool (*is_video_enabled)(struct hdmi_ip *ip);

	/* generate HDMI packect to 'pkt' according to packect type ('no') */
	int (*packet_generate)(struct hdmi_ip *ip, uint32_t no, uint8_t *pkt);

	/* send packect with 'period', 0 period will stop the packect */
	int (*packet_send)(struct hdmi_ip *ip, uint32_t no, int period);

	/*
	 * hdcp
	 */
	int (*hdcp_init)(struct hdmi_ip *ip);
	int (*hdcp_enable)(struct hdmi_ip *ip, bool enable);
};

struct hdmi_ip_settings {
	int hdmi_src;
	int vitd_color;
	int pixel_encoding;
	int color_xvycc;
	int deep_color;
	int hdmi_mode;
	int prelines;

	int channel_invert;
	int bit_invert;

	enum owl_3d_mode mode_3d;
};

struct hdmi_ip {
	struct platform_device		*pdev;
	const struct hdmi_ip_ops	*ops;

	void __iomem			*base;
	int				irq;
	void				*pdata;

	struct hdmi_ip_settings		settings;

	const struct hdmi_config	*cfg;
};


static inline void hdmi_ip_writel(struct hdmi_ip *ip, const uint16_t idx,
				  uint32_t val)
{
	writel(val, ip->base + idx);
}

static inline uint32_t hdmi_ip_readl(struct hdmi_ip *ip, const uint16_t idx)
{
	return readl(ip->base + idx);
}

int hdmi_ip_register(struct hdmi_ip *ip);
void hdmi_ip_unregister(struct hdmi_ip *ip);

int hdmi_ip_generic_suspend(struct hdmi_ip *ip);
int hdmi_ip_generic_resume(struct hdmi_ip *ip);

/*===========================================================================
 *				HDMI DDC
 *=========================================================================*/
int hdmi_ddc_init(void);

int hdmi_ddc_edid_read(char segment_index, char segment_offset, char *pbuf);
int hdmi_ddc_hdcp_write(const char *buf, unsigned short offset, int count);
int hdmi_ddc_hdcp_read(char *buf, unsigned short offset, int count);

/*===========================================================================
 *				HDMI edid
 *=========================================================================*/
int hdmi_edid_parse(struct hdmi_edid *edid);

/*===========================================================================
 *				HDMI packet
 *=========================================================================*/
int hdmi_packet_gen_infoframe(struct hdmi_ip *ip);

/* temp in here */
#define PATH2_SEL		2

#define CMU_REG_BASE		(0xE0160000)
#define CMU_DEVPLL		(CMU_REG_BASE + 0x0004)
#define CMU_DECLK		(CMU_REG_BASE + 0x0030)
#define CMU_ASSISTPLL		(CMU_REG_BASE + 0x0084)
#define CMU_HDMICLK		(CMU_REG_BASE + 0x0088)
#define CMU_DEVCLKEN0		(CMU_REG_BASE + 0x00A0)
#define CMU_DEVCLKEN1		(CMU_REG_BASE + 0x00A4)
#define CMU_DEVRST0		(CMU_REG_BASE + 0x00A8)
#define CMU_DEVRST1		(CMU_REG_BASE + 0x00AC)
#define CMU_TVOUTPLL		(CMU_REG_BASE + 0x0018)

#define CMU_TVOUTPLLDEBUG0	(CMU_REG_BASE + 0x00EC)
#define CMU_TVOUTPLLDEBUG1	(CMU_REG_BASE + 0x00FC)


#define SPS_PG_CTL		(0xE012E000)
#define SPS_PG_ACK		(0xE012E004)

#define SPS_LDO_CTL		(0xE012E014)

static inline void IO_WRITEU32(unsigned int addr, unsigned int val)
{
	void *io_addr;

	io_addr = ioremap(addr, 4);
	writel(val, io_addr);
	iounmap(io_addr);
}

static inline unsigned int IO_READU32(unsigned int addr)
{
	unsigned int val;
	void *io_addr;

	io_addr = ioremap(addr, 4);
	val = readl(io_addr);
	iounmap(io_addr);

	return val;
}

#endif
