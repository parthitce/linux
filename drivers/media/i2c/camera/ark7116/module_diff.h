/*
 * module different macro
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MODULE_DIFF_H__
#define __MODULE_DIFF_H__

#define SI_TVIN

#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "./../host_comm/owl_device.h"
#include "./../flashlight/flashlight.h"

#define END      	{0xff, { 0xff, 0xff, 0xff }, }
#define ENDMARKER       { 0xff, 0xff, 0xff }

#define MODULE_FLAG_8BIT	(1 << 2)	/* default 8 bit */
#define MODULE_FLAG_10BIT	(1 << 3)	/*  10 bit interface */
#define MODULE_FLAG_12BIT	(1 << 4)	/* 12 bit interface */
#define MODULE_FLAG_FRONT	(1 << 5)	/* posization front */
#define MODULE_FLAG_BACK	(1 << 6)	/* posization back */
#define MODULE_FLAG_PALL	(1 << 7)	/* parellal interface */
#define MODULE_FLAG_MIPI	(1 << 8)	/* mipi interface */
#define MODULE_FLAG_CHANNEL1	(1 << 9)	/* use isp channel1 */
#define MODULE_FLAG_CHANNEL2	(1 << 10)	/* use isp channel2 */
#define MODULE_FLAG_3D	        (1 << 11)	/* 3d mode */
#define MODULE_FLAG_HOST1	(1 << 12)	/* on host1 */
#define MODULE_FLAG_HOST2	(1 << 13)	/* on host2 */
#define MODULE_FLAG_NODVDD	(1 << 14)	/* no use dvdd */
#define MODULE_FLAG_AF	        (1 << 15)	/* AUTO FOCUS */
#define MODULE_FLAG_ALWAYS_POWER	(1 << 16)	/* always power on */
#define MODULE_FLAG_NO_AVDD	(1 << 17)	/* no need to operate avdd */

#define WIDTH_QQVGA	160
#define HEIGHT_QQVGA	120
#define WIDTH_QVGA	320
#define HEIGHT_QVGA     240
#define WIDTH_VGA       640
#define HEIGHT_VGA      480
#define WIDTH_SVGA      800
#define HEIGHT_SVGA     600
#define WIDTH_720P      1280
#define HEIGHT_720P     720
#define WIDTH_1080P     1920
#define HEIGHT_1080P	1080
#define WIDTH_UXGA	1600
#define HEIGHT_UXGA	1200
#define WIDTH_QSXGA	2592
#define HEIGHT_QSXGA	1944
#define WIDTH_QXGA      2048
#define HEIGHT_QXGA     1536
#define WIDTH_QUXGA     3264
#define HEIGHT_QUXGA    2448

#define GPIO_HIGH	1
#define GPIO_LOW	0

#define MIPI_YUV422     0x1e
#define MIPI_RAW8       0x2a
#define MIPI_RAW10      0x2b
#define MIPI_RAW12      0x2c

/*color_seq*/
#define COLOR_SEQ_UYVY  (0x0)
#define COLOR_SEQ_VYUY  (0x1)
#define COLOR_SEQ_YUYV  (0x2)
#define COLOR_SEQ_YVYU  (0x3)
#define COLOR_SEQ_SBGGR (0x0)
#define COLOR_SEQ_SGRBG (0x1)
#define COLOR_SEQ_SGBRG (0x2)
#define COLOR_SEQ_SRGGB (0x3)

#define MODULE_DBG_INFO
#define CAMERA_INFO "ARK7116"
#ifdef MODULE_DBG_INFO
#define DBG_INFO(fmt, args...) \
	pr_info("%s %d--%s() "fmt"\n", \
	CAMERA_INFO, __LINE__, __func__, ##args)
#else
#define DBG_INFO(fmt, args...) do {} while (0)
#endif

#ifdef MODULE_DBG_ERR
#define DBG_ERR(fmt, args...) \
	pr_err("%s %d--%s() "fmt"\n", \
	CAMERA_ERR, __LINE__, __func__, ##args)
#else
#define DBG_ERR(fmt, args...) do {} while (0)
#endif

struct regval_list {
	unsigned short data_width;
	unsigned int reg_num;
	unsigned int value;
};

struct initial_data {
	unsigned int slave_addr;
	struct regval_list regval_lists;
};

struct camera_module_win_size {
	char *name;
	__u32 width;
	__u32 height;
	const struct regval_list *win_regs;
	unsigned int *frame_rate_array;
	unsigned int capture_only;
};

struct exposure_param {
	unsigned int max_shutter;
	unsigned int shutter;
	unsigned int gain;
	unsigned int dummy_line;
	unsigned int dummy_pixel;
	unsigned int extra_line;
};

struct module_color_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

struct camera_module_priv {
	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	
	struct module_info *info;
	const struct module_color_format *cfmt;
	const struct camera_module_win_size *win;
	int model;
	int pcv_mode;
	int flip_flag;

	unsigned short auto_white_balance;
	unsigned short exposure;
	unsigned short power_line_frequency;
	unsigned short white_balance_temperature;
	unsigned short colorfx;
	unsigned short exposure_auto;
	unsigned short scene_exposure;

	struct exposure_param preview_exposure_param;
	struct exposure_param capture_exposure_param;
	struct v4l2_afregion af_region;
	enum v4l2_flash_led_mode flash_led_mode;
	enum af_status af_status;
	enum af_mode af_mode;
};

int camera_i2c_read(struct i2c_adapter *i2c_adap, unsigned int slave_addr,
			   unsigned int data_width, unsigned int reg,
			   unsigned int *dest);
static int amera_i2c_write(struct i2c_adapter *i2c_adap,
			    unsigned int data_width, unsigned int reg,
			    unsigned int src);

static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct initial_data *data_lists);

static int module_soft_reset(struct i2c_client *client);

int module_register_v4l2_subdev(struct camera_module_priv *priv,
				struct i2c_client *client);

#define to_camera_priv(client) \
	container_of(i2c_get_clientdata(client), struct camera_module_priv, subdev)

#define CAMERA_MODULE_NAME		"ark7116"

#define MODULE_I2C_REG_ADDRESS		(0xBE>>1)
#define I2C_REGS_WIDTH			1
#define I2C_DATA_WIDTH			1

#define OUTTO_SENSO_CLOCK		24000000

#define DEFAULT_VSYNC_ACTIVE_LEVEL		V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL		V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE		V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY	V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#define MODULE_DEFAULT_WIDTH	WIDTH_SVGA
#define MODULE_DEFAULT_HEIGHT	HEIGHT_SVGA
#define MODULE_MAX_WIDTH		WIDTH_UXGA
#define MODULE_MAX_HEIGHT		HEIGHT_UXGA

#define AHEAD_LINE_NUM			15
#define DROP_NUM_CAPTURE		0
#define DROP_NUM_PREVIEW		0

/*Every sensor must set this value*/
#define USE_AS_FRONT 0
#define USE_AS_REAR 1

static unsigned int frame_rate_bt656[] = { 30, };
#endif				/* __MODULE_DIFF_H__ */
