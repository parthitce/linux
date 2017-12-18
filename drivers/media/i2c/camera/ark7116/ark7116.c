/*
 * gc2145 Camera Driver
 *
 * Copyright (C) 2011 Actions Semiconductor Co.,LTD
 * Wang Xin <wangxin@actions-semi.com>
 *
 * Based on ov227x driver
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <media/soc_camera.h>
#include <media/v4l2-chip-ident.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>

#include "module_diff.h"

struct i2c_client *g_client;
static struct clk *g_camera_host_clk;

/*define and phrase these nodes in dts*/
#define CAMERA_COMMON           "sensor_common"
#define SI_FDT_COMPATIBLE       "actions,s700-isp"
#define I2C_ADAPTER             "front_i2c_adapter"
#define AVIN_RESET_GPIO         "avin_reset_gpio"

#define SI_MODULE_CLOCK         24000000
#define CAMERA_NAME             "front_camera"

#define CVBS00 0x00
#define CVBS01 0x01
#define CVBS02 0x02
#define CVBS_INPUT_CHANNEL 0x01
#define CVBS_INPUT_STATE   0x02
#define CVBS_INPUT_CHECK   0x03
static int camera_offset = -1;

static char camera_name[32];
static struct i2c_adapter *i2c_adap_probe = NULL;
/*
SI Interface params setting
*/
static struct host_module_setting_t module_setting = {
	.hs_pol = 1,		/*0: active low 1:active high */
	.vs_pol = 0,		/*0: active low 1:active high */
	.clk_edge = 0,		/*0: rasing edge 1:falling edge */
	/*0: BG/GR, U0Y0V0Y1, 1: GR/BG, V0Y0U0Y1,
	* 2: GB/RG, Y0U0Y1V0, 3: RG/GB, Y0V0Y1U0 */
	.color_seq = COLOR_SEQ_UYVY,
};

static struct module_info camera_module_info = {
	.flags = 0
	    | SENSOR_FLAG_8BIT
	    | SENSOR_FLAG_YUV | SENSOR_FLAG_DVP | SENSOR_FLAG_SI_TVIN | SENSOR_FLAG_CHANNEL1,
	/*.mipi_cfg = &mipi_sensor_setting, */
	.module_cfg = &module_setting,
};

static const struct initial_data configslavemode[] = {
	{
		0xbe,
		{1, 0xc6, 0x40},
	},
	{
		0xb0,
		{1, 0x0, 0x0},
	},
	#if 1
	{
		0xb0,
		{1, 0x0, 0x5a},
	},
	#endif
	{
		0xb0,
		{1, 0x0, 0x5a},
	},
	{
		0xbe,
		{1, 0xaf, 0x0},
	},
	{
		0xbe,
		{1, 0xa1, 0x55},
	},
	{
		0xbe,
		{1, 0xa2, 0xaa},
	},
	{
		0xbe,
		{1, 0xa3, 0x3},
	},
	{
		0xbe,
		{1, 0xa4, 0x50},
	},
	{
		0xbe,
		{1, 0xa5, 0x0},
	},
	{
		0xbe,
		{1, 0xa6, 0x53},
	},
	{
		0xbe,
		{1, 0xaf, 0x11},
	},
	END,
};

static const struct initial_data initial_data_list[] = {
	{
		0xb0,
		{1, 0xe, 0x20},
	},
	{
		0xb0,
		{1, 0xa, 0x30},
	},
	{
		0xb0,
		{1, 0xb, 0x27},
	},
	{
		0xb0,
		{1, 0xe, 0x2c},
	},
	{
		0xb0,
		{1, 0xf, 0x3},
	},
	{
		0xb0,
		{1, 0x10, 0x4},
	},
	{
		0xb0,
		{1, 0x11, 0xff},
	},
	{
		0xb0,
		{1, 0x12, 0xff},
	},
	{
		0xb0,
		{1, 0x13, 0xff},
	},
	{
		0xb0,
		{1, 0x14, 0x2},
	},
	{
		0xb0,
		{1, 0x15, 0x2},
	},
	{
		0xb0,
		{1, 0x16, 0xa},
	},
	{
		0xb0,
		{1, 0x1a, 0x40},
	},
	{
		0xb2,
		{1, 0xd7, 0xf7},
	},
	{
		0xb2,
		{1, 0x83, 0xff},
	},
	{
		0xb2,
		{1, 0xc9, 0x1},
	},
	{
		0xb2,
		{1, 0xcb, 0x10},
	},
	{
		0xb2,
		{1, 0x15, 0x5},
	},
	{
		0xb2,
		{1, 0xd5, 0xb1},
	},
	{
		0xb2,
		{1, 0x26, 0xe},
	},
	{
		0xb2,
		{1, 0x27, 0x8},
	},
	{
		0xb2,
		{1, 0x28, 0x5},
	},
	{
		0xb2,
		{1, 0x2a, 0x10},
	},
	{
		0xb2,
		{1, 0x54, 0x50},
	},
	{
		0xb4,
		{1, 0xf0, 0x38},
	},
	{
		0xb4,
		{1, 0xf1, 0xf3},
	},
	{
		0xb4,
		{1, 0xf2, 0xd5},
	},
	{
		0xb4,
		{1, 0xf3, 0xc4},
	},
	{
		0xb4,
		{1, 0xf4, 0xfd},
	},
	{
		0xb4,
		{1, 0xf5, 0x40},
	},
	{
		0xb4,
		{1, 0xf6, 0xfd},
	},
	{
		0xb4,
		{1, 0xf7, 0xbe},
	},
	{
		0xb4,
		{1, 0xf8, 0xef},
	},
	{
		0xb4,
		{1, 0xf9, 0xfd},
	},
	{
		0xb4,
		{1, 0xfa, 0x54},
	},
	{
		0xb4,
		{1, 0xfb, 0x1},
	},
	{
		0xb4,
		{1, 0xb0, 0x26},
	},
	{
		0xb4,
		{1, 0xb1, 0xd},
	},
	{
		0xb4,
		{1, 0xb2, 0x10},
	},
	{
		0xb4,
		{1, 0xb3, 0x10},
	},
	{
		0xb4,
		{1, 0xb4, 0x10},
	},
	{
		0xb4,
		{1, 0xb6, 0x10},
	},
	{
		0xb4,
		{1, 0xb7, 0x90},
	},
	{
		0xb4,
		{1, 0xb8, 0x10},
	},
	{
		0xb4,
		{1, 0xb9, 0x62},
	},
	{
		0xb4,
		{1, 0xbb, 0xee},
	},
	{
		0xb4,
		{1, 0xba, 0x20},
	},
	{
		0xb4,
		{1, 0xc8, 0x6},
	},
	{
		0xb4,
		{1, 0xc7, 0x31},
	},
	{
		0xb4,
		{1, 0xc9, 0x0},
	},
	{
		0xb4,
		{1, 0xce, 0x10},
	},
	{
		0xb4,
		{1, 0xcf, 0x80},
	},
	{
		0xb4,
		{1, 0xd0, 0x80},
	},
	{
		0xb4,
		{1, 0xd7, 0x7},
	},
	{
		0xb4,
		{1, 0xd8, 0x80},
	},
	{
		0xb8,
		{1, 0x90, 0x2},
	},
	{
		0xb8,
		{1, 0x91, 0x0},
	},
	{
		0xb8,
		{1, 0x92, 0x0},
	},
	{
		0xb8,
		{1, 0x93, 0xc},
	},
	{
		0xb8,
		{1, 0x98, 0xf9},
	},
	{
		0xb8,
		{1, 0x99, 0x3},
	},
	{
		0xb8,
		{1, 0x9a, 0x55},
	},
	{
		0xb8,
		{1, 0x9b, 0x3},
	},
	{
		0xb8,
		{1, 0x9c, 0x1},
	},
	{
		0xb8,
		{1, 0x9d, 0x0},
	},
	{
		0xb8,
		{1, 0x9e, 0x6},
	},
	{
		0xb8,
		{1, 0x9f, 0x0},
	},
	{
		0xb8,
		{1, 0xa0, 0x20},
	},
	{
		0xb8,
		{1, 0xa1, 0x0},
	},
	{
		0xb8,
		{1, 0xa2, 0xf2},
	},
	{
		0xb8,
		{1, 0xa3, 0x2},
	},
	{
		0xb8,
		{1, 0xa4, 0x3},
	},
	{
		0xb8,
		{1, 0xa5, 0x0},
	},
	{
		0xb8,
		{1, 0xa6, 0x5},
	},
	{
		0xb8,
		{1, 0xa7, 0x0},
	},
	{
		0xb8,
		{1, 0xa8, 0xe},
	},
	{
		0xb8,
		{1, 0xa9, 0x0},
	},
	{
		0xb8,
		{1, 0xaa, 0xff},
	},
	{
		0xb8,
		{1, 0xab, 0x0},
	},
	{
		0xb8,
		{1, 0xb7, 0x7},
	},
	{
		0xb8,
		{1, 0xb8, 0x1},
	},
	{
		0xb8,
		{1, 0xbb, 0x37},
	},
	{
		0xb8,
		{1, 0xbc, 0x1},
	},
	{
		0xb8,
		{1, 0xbd, 0x1},
	},
	{
		0xb8,
		{1, 0xbe, 0x0},
	},
	{
		0xb8,
		{1, 0xbf, 0xc},
	},
	{
		0xb8,
		{1, 0xc4, 0x0},
	},
	{
		0xb8,
		{1, 0xc5, 0x4},
	},
	{
		0xb8,
		{1, 0xc6, 0x62},
	},
	{
		0xb8,
		{1, 0xc7, 0x3},
	},
	{
		0xb8,
		{1, 0xc8, 0x1},
	},
	{
		0xb8,
		{1, 0xc9, 0x0},
	},
	{
		0xb8,
		{1, 0xca, 0x6},
	},
	{
		0xb8,
		{1, 0xcb, 0x0},
	},
	{
		0xb8,
		{1, 0xcc, 0x20},
	},
	{
		0xb8,
		{1, 0xcd, 0x0},
	},
	{
		0xb8,
		{1, 0xce, 0xf2},
	},
	{
		0xb8,
		{1, 0xcf, 0x2},
	},
	{
		0xb8,
		{1, 0xd1, 0x0},
	},
	{
		0xb8,
		{1, 0xd2, 0x8},
	},
	{
		0xb8,
		{1, 0xd3, 0x0},
	},
	{
		0xb8,
		{1, 0xd4, 0xc},
	},
	{
		0xb8,
		{1, 0xd5, 0x0},
	},
	{
		0xb8,
		{1, 0xd6, 0x2d},
	},
	{
		0xb8,
		{1, 0xd7, 0x1},
	},
	{
		0xb8,
		{1, 0xe3, 0x1},
	},
	{
		0xb8,
		{1, 0xe4, 0x5},
	},
	{
		0xb8,
		{1, 0xd0, 0x3},
	},
	{
		0xb8,
		{1, 0xe2, 0x2},
	},
	{
		0xb8,
		{1, 0x0, 0x40},
	},
	{
		0xb4,
		{1, 0x0, 0x3},
	},
	{
		0xb4,
		{1, 0x1, 0x3},
	},
	{
		0xb4,
		{1, 0x2, 0x7},
	},
	{
		0xb4,
		{1, 0x3, 0xb},
	},
	{
		0xb4,
		{1, 0x4, 0x11},
	},
	{
		0xb4,
		{1, 0x5, 0x17},
	},
	{
		0xb4,
		{1, 0x6, 0x1e},
	},
	{
		0xb4,
		{1, 0x7, 0x25},
	},
	{
		0xb4,
		{1, 0x8, 0x2d},
	},
	{
		0xb4,
		{1, 0x9, 0x35},
	},
	{
		0xb4,
		{1, 0xa, 0x3d},
	},
	{
		0xb4,
		{1, 0xb, 0x45},
	},
	{
		0xb4,
		{1, 0xc, 0x4e},
	},
	{
		0xb4,
		{1, 0xd, 0x56},
	},
	{
		0xb4,
		{1, 0xe, 0x5f},
	},
	{
		0xb4,
		{1, 0xf, 0x68},
	},
	{
		0xb4,
		{1, 0x10, 0x70},
	},
	{
		0xb4,
		{1, 0x11, 0x79},
	},
	{
		0xb4,
		{1, 0x12, 0x81},
	},
	{
		0xb4,
		{1, 0x13, 0x8a},
	},
	{
		0xb4,
		{1, 0x14, 0x93},
	},
	{
		0xb4,
		{1, 0x15, 0x9c},
	},
	{
		0xb4,
		{1, 0x16, 0xa4},
	},
	{
		0xb4,
		{1, 0x17, 0xad},
	},
	{
		0xb4,
		{1, 0x18, 0xb6},
	},
	{
		0xb4,
		{1, 0x19, 0xbf},
	},
	{
		0xb4,
		{1, 0x1a, 0xc8},
	},
	{
		0xb4,
		{1, 0x1b, 0xd1},
	},
	{
		0xb4,
		{1, 0x1c, 0xda},
	},
	{
		0xb4,
		{1, 0x1d, 0xe3},
	},
	{
		0xb4,
		{1, 0x1e, 0xec},
	},
	{
		0xb4,
		{1, 0x1f, 0xf6},
	},
	{
		0xb4,
		{1, 0x20, 0x3},
	},
	{
		0xb4,
		{1, 0x21, 0x7},
	},
	{
		0xb4,
		{1, 0x22, 0xc},
	},
	{
		0xb4,
		{1, 0x23, 0x11},
	},
	{
		0xb4,
		{1, 0x24, 0x16},
	},
	{
		0xb4,
		{1, 0x25, 0x1c},
	},
	{
		0xb4,
		{1, 0x26, 0x22},
	},
	{
		0xb4,
		{1, 0x27, 0x29},
	},
	{
		0xb4,
		{1, 0x28, 0x31},
	},
	{
		0xb4,
		{1, 0x29, 0x39},
	},
	{
		0xb4,
		{1, 0x2a, 0x43},
	},
	{
		0xb4,
		{1, 0x2b, 0x4c},
	},
	{
		0xb4,
		{1, 0x2c, 0x56},
	},
	{
		0xb4,
		{1, 0x2d, 0x5f},
	},
	{
		0xb4,
		{1, 0x2e, 0x69},
	},
	{
		0xb4,
		{1, 0x2f, 0x72},
	},
	{
		0xb4,
		{1, 0x30, 0x7b},
	},
	{
		0xb4,
		{1, 0x31, 0x83},
	},
	{
		0xb4,
		{1, 0x32, 0x8c},
	},
	{
		0xb4,
		{1, 0x33, 0x94},
	},
	{
		0xb4,
		{1, 0x34, 0x9d},
	},
	{
		0xb4,
		{1, 0x35, 0xa5},
	},
	{
		0xb4,
		{1, 0x36, 0xae},
	},
	{
		0xb4,
		{1, 0x37, 0xb6},
	},
	{
		0xb4,
		{1, 0x38, 0xbe},
	},
	{
		0xb4,
		{1, 0x39, 0xc6},
	},
	{
		0xb4,
		{1, 0x3a, 0xce},
	},
	{
		0xb4,
		{1, 0x3b, 0xd7},
	},
	{
		0xb4,
		{1, 0x3c, 0xe0},
	},
	{
		0xb4,
		{1, 0x3d, 0xe9},
	},
	{
		0xb4,
		{1, 0x3e, 0xf4},
	},
	{
		0xb4,
		{1, 0x3f, 0x3},
	},
	{
		0xb4,
		{1, 0x40, 0x7},
	},
	{
		0xb4,
		{1, 0x41, 0xc},
	},
	{
		0xb4,
		{1, 0x42, 0x11},
	},
	{
		0xb4,
		{1, 0x43, 0x16},
	},
	{
		0xb4,
		{1, 0x44, 0x1c},
	},
	{
		0xb4,
		{1, 0x45, 0x22},
	},
	{
		0xb4,
		{1, 0x46, 0x29},
	},
	{
		0xb4,
		{1, 0x47, 0x31},
	},
	{
		0xb4,
		{1, 0x48, 0x39},
	},
	{
		0xb4,
		{1, 0x49, 0x43},
	},
	{
		0xb4,
		{1, 0x4a, 0x4c},
	},
	{
		0xb4,
		{1, 0x4b, 0x56},
	},
	{
		0xb4,
		{1, 0x4c, 0x5f},
	},
	{
		0xb4,
		{1, 0x4d, 0x69},
	},
	{
		0xb4,
		{1, 0x4e, 0x72},
	},
	{
		0xb4,
		{1, 0x4f, 0x7b},
	},
	{
		0xb4,
		{1, 0x50, 0x83},
	},
	{
		0xb4,
		{1, 0x51, 0x8c},
	},
	{
		0xb4,
		{1, 0x52, 0x94},
	},
	{
		0xb4,
		{1, 0x53, 0x9d},
	},
	{
		0xb4,
		{1, 0x54, 0xa5},
	},
	{
		0xb4,
		{1, 0x55, 0xae},
	},
	{
		0xb4,
		{1, 0x56, 0xb6},
	},
	{
		0xb4,
		{1, 0x57, 0xbe},
	},
	{
		0xb4,
		{1, 0x58, 0xc6},
	},
	{
		0xb4,
		{1, 0x59, 0xce},
	},
	{
		0xb4,
		{1, 0x5a, 0xd7},
	},
	{
		0xb4,
		{1, 0x5b, 0xe0},
	},
	{
		0xb4,
		{1, 0x5c, 0xe9},
	},
	{
		0xb4,
		{1, 0x5d, 0xf4},
	},
	{
		0xb4,
		{1, 0x5e, 0xff},
	},
	{
		0xb4,
		{1, 0x5f, 0xff},
	},
	{
		0xb4,
		{1, 0x60, 0xff},
	},
	{
		0xb8,
		{1, 0x96, 0xd0},
	},
	{
		0xb8,
		{1, 0x97, 0x3},
	},
	{
		0xb8,
		{1, 0xac, 0x20},
	},
	{
		0xb8,
		{1, 0xad, 0x0},
	},
	{
		0xb8,
		{1, 0xae, 0x2},
	},
	{
		0xb8,
		{1, 0xaf, 0x4},
	},
	{
		0xb8,
		{1, 0xb0, 0x0},
	},
	{
		0xb8,
		{1, 0xc2, 0xe0},
	},
	{
		0xb8,
		{1, 0xc3, 0x3},
	},
	{
		0xb8,
		{1, 0xd8, 0xf},
	},
	{
		0xb8,
		{1, 0xd9, 0x0},
	},
	{
		0xb8,
		{1, 0xda, 0xa},
	},
	{
		0xb8,
		{1, 0xdb, 0x5},
	},
	{
		0xb8,
		{1, 0xdc, 0x0},
	},
	{
		0xb4,
		{1, 0xd3, 0x77},
	},
	{
		0xb4,
		{1, 0xd4, 0x72},
	},
	{
		0xb4,
		{1, 0xd5, 0x0},
	},
	{
		0xb4,
		{1, 0xd6, 0x4d},
	},
	{
		0xb0,
		{1, 0x32, 0x11},
	},
	{
		0xb0,
		{1, 0x33, 0x11},
	},
	{
		0xb0,
		{1, 0x34, 0x0},
	},
	{
		0xb0,
		{1, 0x35, 0x40},
	},
	{
		0xb0,
		{1, 0x36, 0x44},
	},
	{
		0xb0,
		{1, 0x37, 0x44},
	},
	{
		0xb0,
		{1, 0x38, 0x44},
	},
	{
		0xb0,
		{1, 0x39, 0x44},
	},
	{
		0xb0,
		{1, 0x3a, 0x0},
	},
	{
		0xb0,
		{1, 0x3b, 0x0},
	},
	{
		0xb0,
		{1, 0x3c, 0x0},
	},
	{
		0xb0,
		{1, 0x3d, 0x0},
	},
	{
		0xb0,
		{1, 0x3e, 0x0},
	},
	{
		0xb0,
		{1, 0x3f, 0x0},
	},
	{
		0xb0,
		{1, 0x40, 0x0},
	},
	{
		0xb0,
		{1, 0x41, 0x0},
	},
	{
		0xb0,
		{1, 0x44, 0x1},
	},
	{
		0xb0,
		{1, 0x45, 0x0},
	},
	{
		0xb0,
		{1, 0x46, 0x0},
	},
	{
		0xb0,
		{1, 0x47, 0x0},
	},
	{
		0xb0,
		{1, 0x48, 0x0},
	},
	{
		0xb0,
		{1, 0x49, 0x0},
	},
	{
		0xb0,
		{1, 0x4a, 0x0},
	},
	{
		0xb0,
		{1, 0x4b, 0x0},
	},
	{
		0xb0,
		{1, 0x50, 0xb},
	},
	{
		0xb0,
		{1, 0xe, 0x2c},
	},
	END,
};



static ssize_t rear_name_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return strlcpy(buf, camera_name, sizeof(camera_name));
}

static ssize_t rear_offset_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", camera_offset);
}

static DEVICE_ATTR(rear_name, 0444, rear_name_show, NULL);
static DEVICE_ATTR(rear_offset, 0444, rear_offset_show, NULL);

static int creat_carmera_sysfs(void)
{
	int ret = 0;

	struct kobject *rear_kobj;
	rear_kobj = kobject_create_and_add(CAMERA_NAME,
			NULL);
	if (!rear_kobj) {
		DBG_ERR("kobject_create_and_add failed.");
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(rear_kobj,
			&dev_attr_rear_offset.attr);
	sprintf(camera_name, "%s.ko", CAMERA_MODULE_NAME);
	ret = sysfs_create_file(rear_kobj,
			&dev_attr_rear_name.attr);

	camera_offset = 0;

	return 0;
}

/*******************************************************************************************/
int camera_i2c_read(struct i2c_adapter *i2c_adap, unsigned int slave_addr,
			   unsigned int data_width, unsigned int reg,
			   unsigned int *dest)
{
	
	int ret, i;
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	struct i2c_client *client = g_client;
	struct i2c_adapter *adap = i2c_adap;
	struct i2c_msg msg[2];
	
	char addr_buffer[2];
	//printk("camera_i2c_read: slave_addr: 0x%x, data_width: %d, reg: 0x%x, \n",slave_addr, data_width, reg);
	
	if (client == NULL) {
		printk("no I2C adater\n");
		return -ENODEV;
	}

	msg[0].addr = slave_addr >> 1;
	msg[0].flags = client->flags | I2C_M_IGNORE_NAK;
	msg[0].buf = (unsigned char *)&reg;
	msg[0].len = I2C_REGS_WIDTH; /* chip addr is 2 byte */

	msg[1].addr = slave_addr >> 1;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = data_array;
	msg[1].len = data_width;

	ret = i2c_transfer(adap, msg, 2);
	for (i = 0; i < data_width; i++){
		//printk("i2c read :buf[%d] 0x%x\n", i, msg[1].buf[i]);
	}

	if (1 == data_width){
		*dest = (unsigned int)msg[1].buf[0];
	}

	if (ret < 0) {
		printk("%s, fail to read edp i2c data(%d)\n", __func__, ret);
	}

	return ret;
}

static int camera_i2c_write(struct i2c_adapter *i2c_adap, unsigned int slave_addr,
			    unsigned int data_width, unsigned int reg,
			    unsigned int data)
{
	unsigned char regs_array[4] = { 0, 0, 0, 0 };
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	unsigned char tran_array[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	struct i2c_msg msg;
	int ret, i;

	//printk("slave_addr: 0x%x, data_width: %d, reg: 0x%x, data: 0x%x\n",
	//	slave_addr, data_width, reg, data);

	if (I2C_REGS_WIDTH == 1)
		regs_array[0] = reg & 0xff;
	if (I2C_REGS_WIDTH == 2) {
		regs_array[0] = (reg >> 8) & 0xff;
		regs_array[1] = reg & 0xff;
	}
	if (data_width == 1)
		data_array[0] = data & 0xff;
	if (data_width == 2) {
		data_array[0] = (data >> 8) & 0xff;
		data_array[1] = data & 0xff;
	}
	for (i = 0; i < I2C_REGS_WIDTH; i++) {
		tran_array[i] = regs_array[i];
		//printk("tran_array[%d] 0x%x\n", i, tran_array[i]);
	}

	for (i = I2C_REGS_WIDTH; i < (I2C_REGS_WIDTH + data_width); i++) {
		tran_array[i] = data_array[i - I2C_REGS_WIDTH];
		//printk("tran_array[%d] 0x%x\n", i, tran_array[i]);
	}

	msg.addr = slave_addr >> 1;
	msg.flags = 0;
	msg.len = I2C_REGS_WIDTH + data_width;
	msg.buf = tran_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret > 0)
		ret = 0;
	else if (ret < 0)
		DBG_ERR("write register %s error %d,reg :0x%x,value : 0x%x\n",
		       CAMERA_MODULE_NAME, ret, reg, data);

	return ret;
}

static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct initial_data *data_lists)
{
	struct regval_list *vals = &data_lists->regval_lists;

	while (vals->reg_num != 0xff) {
		
		if (vals->reg_num == 0xfffe) {
			DBG_INFO("delay %d", vals->value);
			mdelay(vals->value);
			data_lists++;
			vals = &data_lists->regval_lists;
		} else {
			int ret = camera_i2c_write(i2c_adap, data_lists->slave_addr,
						   vals->data_width,
						   vals->reg_num,
						   vals->value);
			if (ret < 0) {
				DBG_ERR
				    ("i2c write error!,i2c address is %x\n",
				     data_lists->slave_addr);
			}
			
			unsigned int reg_val;
			ret = camera_i2c_read(i2c_adap, data_lists->slave_addr, 1, vals->reg_num, &reg_val);
			
			data_lists++;
			vals = &data_lists->regval_lists;
		}
	
	}
	return 0;
}

static int module_soft_reset(struct i2c_client *client)
{
	struct i2c_adapter *i2c_adap = client->adapter;
	unsigned int reg_0xfe;
	int ret;

	camera_write_array(i2c_adap, configslavemode);
	mdelay(200);

	return ret;
}

static void camera_module_priv_init(struct camera_module_priv *priv)
{
	priv->pcv_mode = ACTS_PREVIEW_MODE;
	priv->exposure_auto = 1;
	priv->auto_white_balance = 1;
	priv->power_line_frequency = DEFAULT_POWER_LINE_FREQUENCY;
	priv->power_line_frequency = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
	priv->win = NULL;
	priv->af_status = AF_STATUS_DISABLE;
	priv->af_mode = CONTINUE_AF;

	return;
}

static int camera_module_sensor_init(struct i2c_client *client)
{
	int ret = 0;

	ret = module_soft_reset(client);
	if (0 > ret)
		return ret;

	ret = camera_write_array(client->adapter, initial_data_list);
	if (0 > ret)
		return ret;
	i2c_adap_probe = client->adapter ;

	return ret;
}

static int camera_module_probe(struct i2c_client *client,
			       const struct i2c_device_id *did)
{
	int ret = 0;
	struct camera_module_priv *priv;
	struct soc_camera_subdev_desc *desc = soc_camera_i2c_to_desc(client);
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	pr_info("%s probe start...", CAMERA_MODULE_NAME);

	if (!desc) {
		DBG_ERR("error: camera module missing soc camera link");
		return -EINVAL;
	}
	if (!desc->drv_priv) {
		DBG_ERR("error: no init module_info of camera module");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		DBG_ERR
		    ("I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pr_info("%s, priv 0x%x\n", __func__, priv);

	camera_module_priv_init(priv);
	priv->info = desc->drv_priv;
	g_client = client;

	/* v4l2_subdev register */
	module_register_v4l2_subdev(priv, client);

	/* config sensor chip */
	pr_info(" do %s.\n", __func__);
	ret = camera_module_sensor_init(client);

	return ret;
}

static int camera_module_remove(struct i2c_client *client)
{
	struct camera_module_priv *priv = to_camera_priv(client);

	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	kfree(priv);

	return 0;
}

/* soc_camer_link's hooks */
static int camera_module_power(struct device *dev, int mode)
{
	return 0;
}

static int camera_module_reset(struct device *dev)
{
	return 0;
}

static struct i2c_board_info asoc_i2c_camera = {
	I2C_BOARD_INFO(CAMERA_MODULE_NAME, MODULE_I2C_REG_ADDRESS),
};

static const unsigned short camera_module_addrs[] = {
	MODULE_I2C_REG_ADDRESS,
	I2C_CLIENT_END,
};

static struct soc_camera_link camera_module_link = {
	/* Subdevice part */
	.priv = &camera_module_info,
	.power = camera_module_power,
	.reset = camera_module_reset,

	/* Host part */
	
	/* Camera bus id, used to match a camera and a bus */
	.bus_id = 0,
	.i2c_adapter_id = 0, /* i2c id default value */
	.board_info = &asoc_i2c_camera,
	.module_name = CAMERA_MODULE_NAME,
};

static struct platform_device camera_module_device = {
	.name = "soc-camera-pdrv",
	.id = 0,
	.dev = {
		.platform_data = &camera_module_link,
	},
};

static const struct i2c_device_id camera_module_id[] = {
	{CAMERA_MODULE_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, camera_module_id);

static int camera_module_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}

static int camera_module_resume(struct i2c_client *client)
{
	return camera_module_sensor_init(client);
}

static struct i2c_driver camera_i2c_driver = {
	.driver = {
		.name = CAMERA_MODULE_NAME,
	},
	.probe = camera_module_probe,
	.suspend = camera_module_suspend,
	.resume = camera_module_resume,
	.remove = camera_module_remove,
	.id_table = camera_module_id,
};


static int camera_host_clk_init(void)
{
	struct clk *tmp = NULL;
	int ret = 0;

	/*get isp clock first, if not exist, get si clock. */
	tmp = clk_get(NULL, "bisp");
	if (IS_ERR(tmp)) {
		tmp = clk_get(NULL, "si");
		if (IS_ERR(tmp)) {
			ret = PTR_ERR(tmp);
			g_camera_host_clk = NULL;
			DBG_ERR("get clock error (%d)", ret);
			return ret;
		}
	}
	g_camera_host_clk = tmp;
	mdelay(1);

	return ret;
}


static int camera_host_clk_enable(void)
{
	int ret = 0;

	if (g_camera_host_clk != NULL) {
		clk_prepare(g_camera_host_clk);
		ret = clk_enable(g_camera_host_clk);	/*enable clk */
		if (ret)
			DBG_ERR("si clock enable error (%d)", ret);
		/*set isp work freq */
		ret = clk_set_rate(g_camera_host_clk, SI_MODULE_CLOCK);
	}
	return ret;
}

static void isp_clk_disable(void)
{
	if (g_camera_host_clk != NULL) {
		clk_disable(g_camera_host_clk);
		clk_unprepare(g_camera_host_clk);
		clk_put(g_camera_host_clk);
		g_camera_host_clk = NULL;
	}
}

int camera_host_init(void)
{
	int ret = 0;
	
	/*init host clock */
	ret = camera_host_clk_init();
	if (ret) {
		pr_err("init host clock error");
		goto exit;
	}

	ret = camera_host_clk_enable();
	if (ret) {
		pr_err("enable host clock error");
		goto exit;
	}

	return ret;
 exit:

	return ret;
}

static int sensor_get_i2c_bus_id(struct device_node *node,
			      const char *property, const char *stem)
{
	struct device_node *pnode;

	pnode = of_parse_phandle(node, property, 0);
	if (NULL == pnode) {
		pr_err("fail to get node[%s]", property);
		return -ENODEV;
	}

	return of_alias_get_id(pnode, stem);
}

static int get_i2c_adapter_id(struct soc_camera_link *link)
{
	struct device_node *fdt_node;
	int ret = 0, id;

	fdt_node = of_find_compatible_node(NULL, NULL, CAMERA_COMMON);
	if (!fdt_node) {
		pr_err("no sensor common");
		return -EINVAL;
	}
	
	id = sensor_get_i2c_bus_id(fdt_node, I2C_ADAPTER, "i2c");
	if (id < 0) {
		pr_err("fail to get i2c adapter id");
		return -EINVAL;
	}
	
	link->i2c_adapter_id = id; 

	/*FIXME*/
	ret = camera_host_init();
	if (ret)
		pr_err("camera_host_init error.");

	creat_carmera_sysfs();

	return ret;
}

static int reset_gpio_pin = 0 ;
int reset_gpio_init(void){
	struct device_node *of_node;
	int ret = 0;
	of_node = of_find_compatible_node(NULL, NULL, CAMERA_COMMON);
	if (!of_node) {
		pr_err("no sensor config");
		return -EINVAL;
	}
	reset_gpio_pin = of_get_named_gpio(of_node, AVIN_RESET_GPIO, 0);
	if(gpio_is_valid(reset_gpio_pin)){
		ret = gpio_request(reset_gpio_pin, AVIN_RESET_GPIO);
                if(ret < 0){ 
                        pr_err("gpio_request fail.\n");
                        return -1; 
                }   
                gpio_direction_output(reset_gpio_pin, 0); // default output low;
        }else{
                pr_err("gpio for avin_reset_gpio invalid.\n");
                return -1; 
        }
	msleep(100);
	return 0;
}

int set_cvbs_num_input(int cvbs_id){
	int ret = 0;
	if(NULL == i2c_adap_probe){
		pr_err("can't get ark7116 devices.\n");
		return -1;
	}
	switch(cvbs_id){
		case CVBS01 :
			ret = camera_i2c_write(i2c_adap_probe,0xb2,1,0xdc,0x10);
			break;
		case CVBS02 :
			ret = camera_i2c_write(i2c_adap_probe,0xb2,1,0xdc,0x30);
			break;
		case CVBS00 :
		default :
			ret = camera_i2c_write(i2c_adap_probe,0xb2,1,0xdc,0x00);
			break;
	}
	return ret;
}

int get_cvbs_input_state(void){
	unsigned int reg_0xfe26;
	if(NULL == i2c_adap_probe){
		pr_err("can't get ark7116 devices.\n");
		return -1;
	}
	camera_i2c_read(i2c_adap_probe, 0xb2, 1, 0x26, &reg_0xfe26);
	return (reg_0xfe26 & 0x6);
}

/*
 * return value == 1 PAL ,or 0 : NTSC
 */
int check_cvbs_input_is_PAL(void){
	unsigned int reg_0xfe28 = 0;
	camera_i2c_read(i2c_adap_probe, 0xb2, 1, 0x28, &reg_0xfe28);
	return (reg_0xfe28 & 0x04);
}

static int ark7116_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	int ret, channel = 0;
	printk("do ark7116_ioctl cmd=0x%2x.\n",cmd);
	switch(cmd){
		case CVBS_INPUT_CHANNEL :
			copy_from_user(&channel, (int *)arg, 4);
			printk("ark7116 driver: channel=%d.\n",channel);
			if(ret = set_cvbs_num_input(channel))
				pr_err("Set AuxIn channel fail!\n");
			break;
		case CVBS_INPUT_STATE :
			ret = get_cvbs_input_state();
			printk("ark7116 driver: get cvbs state =%d.\n",ret);
			break;
		case CVBS_INPUT_CHECK :
			ret = check_cvbs_input_is_PAL();
			printk("ark7116 driver:check input is %d.\n",ret);
		default:
			break;
	}
	return ret;
}

static const struct file_operations ark7116_fops = {
        .owner = THIS_MODULE,
        .unlocked_ioctl = ark7116_ioctl,
        .compat_ioctl = ark7116_ioctl,
};

static struct miscdevice ark7116_miscdevice = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "AuxIn_ark7116",
        .fops = &ark7116_fops,
};

/* module function */
static int __init camera_module_init(void)
{
	unsigned int ret = 0;

	ret = reset_gpio_init();
	if(ret <0){
		pr_err("avin_reset_gpio init fail.\n");
		goto err0;
	}

	ret = misc_register(&ark7116_miscdevice);
	if (ret) {
		pr_err("register bisp misc device failed!...\n");
		goto err1;
	}
	/* get i2c adapter id to soc_camera_link info */
	ret = get_i2c_adapter_id(&camera_module_link);

	/* register a camera module device, with module device soc_camera_link info */
	ret = platform_device_register(&camera_module_device);
	if (ret){
		pr_err("fail to register platform device.");
		goto err2;
	}
	
	/* register a camera i2c driver */
	ret = i2c_add_driver(&camera_i2c_driver);
	if (ret){
		pr_err("fail to add i2c driver.");
		goto err3;
	}
	return 0;

err3:
	i2c_del_driver(&camera_i2c_driver);
err2:
	platform_device_unregister(&camera_module_device);
err1:
	misc_deregister(&ark7116_miscdevice);
err0:
	return ret;
}
module_init(camera_module_init);

static void __exit camera_module_exit(void)
{

	i2c_del_driver(&camera_i2c_driver);
	platform_device_unregister(&camera_module_device);
	misc_deregister(&ark7116_miscdevice);
	gpio_free(reset_gpio_pin);
}
module_exit(camera_module_exit);

MODULE_DESCRIPTION("Camera module driver");
MODULE_AUTHOR("Actions-semi");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
