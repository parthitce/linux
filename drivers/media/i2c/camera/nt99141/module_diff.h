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

#include "./../module_comm/module_comm.h"
#include "./../host_comm/owl_device.h"

#define CAMERA_MODULE_NAME 		"NT99141"
#define CAMERA_MODULE_PID		0x1410

#define MODULE_I2C_REAL_ADDRESS		(0x54>>1)
#define MODULE_I2C_REG_ADDRESS		(0x54>>1)

#define I2C_REGS_WIDTH			2
#define I2C_DATA_WIDTH			1

#define DEFAULT_VSYNC_ACTIVE_LEVEL		V4L2_MBUS_VSYNC_ACTIVE_HIGH
#define DEFAULT_HSYNC_ACTIVE_LEVEL		V4L2_MBUS_HSYNC_ACTIVE_HIGH
#define DEFAULT_PCLK_SAMPLE_EDGE		V4L2_MBUS_PCLK_SAMPLE_RISING
#define DEFAULT_POWER_LINE_FREQUENCY	V4L2_CID_POWER_LINE_FREQUENCY_50HZ

#if 0
#define PID						0x00 /* Product ID Number */
#else
#define PIDH					0x3000 /* Product ID Number H byte */
#define PIDL					0x3001 /* Product ID Number L byte */
#endif

#define OUTTO_SENSO_CLOCK 		24000000


#define MODULE_DEFAULT_WIDTH	WIDTH_720P
#define MODULE_DEFAULT_HEIGHT	HEIGHT_720P
#define MODULE_MAX_WIDTH		WIDTH_720P
#define MODULE_MAX_HEIGHT		HEIGHT_720P

#define AHEAD_LINE_NUM			15
#define DROP_NUM_CAPTURE		3
#define DROP_NUM_PREVIEW		3

/*Every sensor must set this value*/
#define USE_AS_FRONT 0
#define USE_AS_REAR 1

static unsigned int frame_rate_720p[]  = {25,};
static unsigned int frame_rate_svga[]  = {30,};
static unsigned int frame_rate_vga[]  = {60,};

/*
ISP Interface params setting
*/
static struct host_module_setting_t module_setting = {
	.hs_pol = 1,		/*0: active low 1:active high */
	.vs_pol = 1,		/*0: active low 1:active high */
	.clk_edge = 0,		/*0: rasing edge 1:falling edge */
	/*0: BG/GR, U0Y0V0Y1, 1: GR/BG, V0Y0U0Y1,
	* 2: GB/RG, Y0U0Y1V0, 3: RG/GB, Y0V0Y1U0 */
	.color_seq = COLOR_SEQ_UYVY,
};

struct module_info camera_module_info = {
	.flags = 0
	    | SENSOR_FLAG_10BIT
	    | SENSOR_FLAG_YUV | SENSOR_FLAG_DVP | SENSOR_FLAG_CHANNEL0,
	/*.mipi_cfg = &mipi_sensor_setting, */
	.module_cfg = &module_setting,
};

/*
 * supported color format list.
 * see definition in
 *     http://thread.gmane.org/gmane.linux.drivers.video-input-infrastructure/12830/focus=13394
 * YUYV8_2X8_LE == YUYV with LE packing
 * YUYV8_2X8_BE == UYVY with LE packing
 * YVYU8_2X8_LE == YVYU with LE packing
 * YVYU8_2X8_BE == VYUY with LE packing
 */
static const struct module_color_format module_cfmts[] = {
	{
		.code		= V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
    },
	//{
	//	.code		= V4L2_MBUS_FMT_YUYV8_2X8,
	//	.colorspace	= V4L2_COLORSPACE_JPEG,},
	//{
	//	.code		= V4L2_MBUS_FMT_YVYU8_2X8,
	//	.colorspace	= V4L2_COLORSPACE_JPEG,},
	//{
	//	.code		= V4L2_MBUS_FMT_VYUY8_2X8,
	//	.colorspace	= V4L2_COLORSPACE_JPEG,},
};

static const struct regval_list module_init_regs[] = { 
    {1, 0x3109, 0x04},
    {1, 0x3040, 0x04},
    {1, 0x3041, 0x02},
    {1, 0x3042, 0xFF},
    {1, 0x3043, 0x08},
    {1, 0x3052, 0xE0},
    {1, 0x305F, 0x33},
    {1, 0x3100, 0x07},
    {1, 0x3106, 0x03},
    {1, 0x3105, 0x01},
    {1, 0x3108, 0x05},
    {1, 0x3110, 0x22},
    {1, 0x3111, 0x57},
    {1, 0x3112, 0x22},
    {1, 0x3113, 0x55},
    {1, 0x3114, 0x05},
    {1, 0x3135, 0x00},
    {1, 0x3210, 0x11},  //Gain0 of R
    {1, 0x3211, 0x14},  //Gain1 of R
    {1, 0x3212, 0x11},  //Gain2 of R
    {1, 0x3213, 0x10},  //Gain3 of R
    {1, 0x3214, 0x0F},  //Gain0 of Gr
    {1, 0x3215, 0x12},  //Gain1 of Gr
    {1, 0x3216, 0x10},  //Gain2 of Gr
    {1, 0x3217, 0x0F},  //Gain3 of Gr
    {1, 0x3218, 0x0F},  //Gain0 of Gb
    {1, 0x3219, 0x13},  //Gain1 of Gb
    {1, 0x321A, 0x10},  //Gain2 of Gb
    {1, 0x321B, 0x0F},  //Gain3 of Gb
    {1, 0x321C, 0x0F},  //Gain0 of B
    {1, 0x321D, 0x12},  //Gain1 of B
    {1, 0x321E, 0x0F},  //Gain2 of B
    {1, 0x321F, 0x0D},  //Gain3 of B
    {1, 0x3231, 0x74},  //LSC bottom EV boundary
    {1, 0x3232, 0xC4},  // 
    {1, 0x32F0, 0x00},
    {1, 0x3290, 0x01},
    {1, 0x3291, 0x80},
    {1, 0x3296, 0x01},
    {1, 0x3297, 0x73},
    {1, 0x3250, 0x80},
    {1, 0x3251, 0x03},
    {1, 0x3252, 0xFF},
    {1, 0x3253, 0x00},
    {1, 0x3254, 0x00},
    {1, 0x3255, 0xA4},
    {1, 0x3256, 0x94},
    {1, 0x3257, 0x50},
    {1, 0x3270, 0x00},
    {1, 0x3271, 0x0C},
    {1, 0x3272, 0x18},
    {1, 0x3273, 0x32},
    {1, 0x3274, 0x44},
    {1, 0x3275, 0x54},
    {1, 0x3276, 0x70},
    {1, 0x3277, 0x88},
    {1, 0x3278, 0x9D},
    {1, 0x3279, 0xB0},
    {1, 0x327A, 0xCF},
    {1, 0x327B, 0xE2},
    {1, 0x327C, 0xEF},
    {1, 0x327D, 0xF7},
    {1, 0x327E, 0xFF},
    {1, 0x3302, 0x00},
    {1, 0x3303, 0x40},
    {1, 0x3304, 0x00},
    {1, 0x3305, 0x96},
    {1, 0x3306, 0x00},
    {1, 0x3307, 0x29},
    {1, 0x3308, 0x07},
    {1, 0x3309, 0xBA},
    {1, 0x330A, 0x06},
    {1, 0x330B, 0xF5},
    {1, 0x330C, 0x01},
    {1, 0x330D, 0x51},
    {1, 0x330E, 0x01},
    {1, 0x330F, 0x30},
    {1, 0x3310, 0x07},
    {1, 0x3311, 0x16},
    {1, 0x3312, 0x07},
    {1, 0x3313, 0xBA},
    {1, 0x3326, 0x02},
    {1, 0x32F6, 0x0F},
    {1, 0x32F9, 0x42},
    {1, 0x32FA, 0x24},
    {1, 0x3325, 0x4A},
    {1, 0x3330, 0x00},
    {1, 0x3331, 0x0A},
    {1, 0x3332, 0xFF},
    {1, 0x3338, 0x30},
    {1, 0x3339, 0x84},
    {1, 0x333A, 0x48},
    {1, 0x333F, 0x07},
    {1, 0x3360, 0x10},
    {1, 0x3361, 0x18},
    {1, 0x3362, 0x1f},
    {1, 0x3363, 0x37},
    {1, 0x3364, 0x80},
    {1, 0x3365, 0x80},
    {1, 0x3366, 0x68},
    {1, 0x3367, 0x60},
    {1, 0x3368, 0x30},
    {1, 0x3369, 0x28},
    {1, 0x336A, 0x20},
    {1, 0x336B, 0x10},
    {1, 0x336C, 0x00},
    {1, 0x336D, 0x20},
    {1, 0x336E, 0x1C},
    {1, 0x336F, 0x18},
    {1, 0x3370, 0x10},
    {1, 0x3371, 0x38},
    {1, 0x3372, 0x3C},
    {1, 0x3373, 0x3F},
    {1, 0x3374, 0x3F},
    {1, 0x338A, 0x34},
    {1, 0x338B, 0x7F},
    {1, 0x338C, 0x10},
    {1, 0x338D, 0x23},
    {1, 0x338E, 0x7F},
    {1, 0x338F, 0x14},
    {1, 0x3375, 0x0A},
    {1, 0x3376, 0x0C},
    {1, 0x3377, 0x10},
    {1, 0x3378, 0x14},
    {1, 0x3012, 0x02},
    {1, 0x3013, 0xD0},
    {1, 0x3069, 0x01},
    {1, 0x306a, 0x03},
	ENDMARKER,
};

/* 1280*720: 720P*/
static const struct regval_list module_720p_regs[] = {
    {1, 0x32BF, 0x60},
    {1, 0x32C0, 0x60},
    {1, 0x32C1, 0x60},
    {1, 0x32C2, 0x60},
    {1, 0x32C3, 0x00},
    {1, 0x32C4, 0x28},
    {1, 0x32C5, 0x20},
    {1, 0x32C6, 0x20},
    {1, 0x32C7, 0x00},
    {1, 0x32C8, 0xDF},
    {1, 0x32C9, 0x60},
    {1, 0x32CA, 0x80},
    {1, 0x32CB, 0x80},
    {1, 0x32CC, 0x80},
    {1, 0x32CD, 0x80},
    {1, 0x32DB, 0x7B},
    {1, 0x3200, 0x3E},
    {1, 0x3201, 0x0F},
    {1, 0x3028, 0x24},
    {1, 0x3029, 0x20},
    {1, 0x302A, 0x04},
    {1, 0x3022, 0x24},
    {1, 0x3023, 0x24},
    {1, 0x3002, 0x00},
    {1, 0x3003, 0x04},
    {1, 0x3004, 0x00},
    {1, 0x3005, 0x04},
    {1, 0x3006, 0x05},
    {1, 0x3007, 0x03},
    {1, 0x3008, 0x02},
    {1, 0x3009, 0xD3},
    {1, 0x300A, 0x06},
    {1, 0x300B, 0x7C},
    {1, 0x300C, 0x02},
    {1, 0x300D, 0xE6},
    {1, 0x300E, 0x05},
    {1, 0x300F, 0x00},
    {1, 0x3010, 0x02},
    {1, 0x3011, 0xD0},
    {1, 0x32B8, 0x3B},
    {1, 0x32B9, 0x2D},
    {1, 0x32BB, 0x87},
    {1, 0x32BC, 0x34},
    {1, 0x32BD, 0x38},
    {1, 0x32BE, 0x30},
    {1, 0x3201, 0x3F},
    {1, 0x3021, 0x06},
    {1, 0x3060, 0x01},
	ENDMARKER,
};

/* 800*600: SVGA*/
static const struct regval_list module_svga_regs[] = {
	{1, 0x32BF, 0x60}, 
	{1, 0x32C0, 0x5A}, 
	{1, 0x32C1, 0x5A}, 
	{1, 0x32C2, 0x5A}, 
	{1, 0x32C3, 0x00}, 
	{1, 0x32C4, 0x28}, 
	{1, 0x32C5, 0x20}, 
	{1, 0x32C6, 0x20}, 
	{1, 0x32C7, 0x00}, 
	{1, 0x32C8, 0xDD}, 
	{1, 0x32C9, 0x5A}, 
	{1, 0x32CA, 0x7A}, 
	{1, 0x32CB, 0x7A}, 
	{1, 0x32CC, 0x7A}, 
	{1, 0x32CD, 0x7A}, 
	{1, 0x32DB, 0x7B}, 
	{1, 0x32E0, 0x03}, 
	{1, 0x32E1, 0x20}, 
	{1, 0x32E2, 0x02}, 
	{1, 0x32E3, 0x58}, 
	{1, 0x32E4, 0x00}, 
	{1, 0x32E5, 0x33}, 
	{1, 0x32E6, 0x00}, 
	{1, 0x32E7, 0x33}, 
	{1, 0x3200, 0x3E}, 
	{1, 0x3201, 0x0F}, 
	{1, 0x3028, 0x1F}, 
	{1, 0x3029, 0x20}, 
	{1, 0x302A, 0x04}, 
	{1, 0x3022, 0x24}, 
	{1, 0x3023, 0x24}, 
	{1, 0x3002, 0x00}, 
	{1, 0x3003, 0xA4}, 
	{1, 0x3004, 0x00}, 
	{1, 0x3005, 0x04}, 
	{1, 0x3006, 0x04}, 
	{1, 0x3007, 0x63}, 
	{1, 0x3008, 0x02}, 
	{1, 0x3009, 0xD3}, 
	{1, 0x300A, 0x05}, 
	{1, 0x300B, 0xA9}, 
	{1, 0x300C, 0x02}, 
	{1, 0x300D, 0xE0}, 
	{1, 0x300E, 0x03}, 
	{1, 0x300F, 0xC0}, 
	{1, 0x3010, 0x02}, 
	{1, 0x3011, 0xD0}, 
	{1, 0x32B8, 0x3F}, 
	{1, 0x32B9, 0x31}, 
	{1, 0x32BB, 0x87}, 
	{1, 0x32BC, 0x38}, 
	{1, 0x32BD, 0x3C}, 
	{1, 0x32BE, 0x34}, 
	{1, 0x3201, 0x7F}, 
	{1, 0x3021, 0x06}, 
	{1, 0x3060, 0x01}, 
	ENDMARKER,
};

/* 640*480: VGA(crop720x540->scaler->vga)*/
static const struct regval_list module_vga_regs[] = {
    {1, 0x32BF, 0x60},
    {1, 0x32C0, 0x4A},
    {1, 0x32C1, 0x4B},
    {1, 0x32C2, 0x4B},
    {1, 0x32C3, 0x00},
    {1, 0x32C4, 0x28},
    {1, 0x32C5, 0x20},
    {1, 0x32C6, 0x20},
    {1, 0x32C7, 0x40},
    {1, 0x32C8, 0x50},
    {1, 0x32C9, 0x4B},
    {1, 0x32CA, 0x6B},
    {1, 0x32CB, 0x6B},
    {1, 0x32CC, 0x6B},
    {1, 0x32CD, 0x6A},
    {1, 0x32DB, 0x85},
    {1, 0x32E0, 0x02},
    {1, 0x32E1, 0x80},
    {1, 0x32E2, 0x01},
    {1, 0x32E3, 0xE0},
    {1, 0x32E4, 0x00},
    {1, 0x32E5, 0x20},
    {1, 0x32E6, 0x00},
    {1, 0x32E7, 0x20},
    {1, 0x3200, 0x3E},
    {1, 0x3201, 0x0F},
    {1, 0x3028, 0x24},
    {1, 0x3029, 0x20},
    {1, 0x302A, 0x04},
    {1, 0x3022, 0x24},
    {1, 0x3023, 0x24},
    {1, 0x3002, 0x01},
    {1, 0x3003, 0x1C},
    {1, 0x3004, 0x00},
    {1, 0x3005, 0x5E},
    {1, 0x3006, 0x03},
    {1, 0x3007, 0xEB},
    {1, 0x3008, 0x02},
    {1, 0x3009, 0x79},
    {1, 0x300A, 0x04},
    {1, 0x300B, 0x4C},
    {1, 0x300C, 0x02},
    {1, 0x300D, 0x30},
    {1, 0x300E, 0x02},
    {1, 0x300F, 0xD0},
    {1, 0x3010, 0x02},
    {1, 0x3011, 0x1C},
    {1, 0x32B8, 0x3B},
    {1, 0x32B9, 0x2D},
    {1, 0x32BB, 0x87},
    {1, 0x32BC, 0x34},
    {1, 0x32BD, 0x38},
    {1, 0x32BE, 0x30},
    {1, 0x3201, 0x7F},
    {1, 0x3021, 0x06},
    {1, 0x3060, 0x01},
	ENDMARKER,
};

static const struct regval_list module_init_auto_focus[] = {
	ENDMARKER,
};

/* 1280*720 */
static struct camera_module_win_size module_win_720p = {
	.name             = "720P",
	.width            = WIDTH_720P,
	.height           = HEIGHT_720P,
	.win_regs         = module_720p_regs,
	.frame_rate_array = frame_rate_720p,
	.capture_only = 0,
};

/* 800*600 */
static struct camera_module_win_size module_win_svga = {
	.name             = "SVGA",
	.width            = WIDTH_SVGA,
	.height           = HEIGHT_SVGA,
	.win_regs         = module_svga_regs,
	.frame_rate_array = frame_rate_svga,
	.capture_only = 0,
};

/* 640*480 */
static struct camera_module_win_size module_win_vga = {
	.name             = "VGA",
	.width            = WIDTH_VGA,
	.height           = HEIGHT_VGA,
	.win_regs         = module_vga_regs,
	.frame_rate_array = frame_rate_vga,
	.capture_only = 0,
};

static struct camera_module_win_size *module_win_list[] = {
	&module_win_720p,
	&module_win_svga,
	//&module_win_vga,	
};

static struct regval_list module_whitebance_auto_regs[] = {
	ENDMARKER,
};

/* Cloudy Colour Temperature : 6500K - 8000K  */
static struct regval_list module_whitebance_cloudy_regs[] = {
	ENDMARKER,
};

/* ClearDay Colour Temperature : 5000K - 6500K  */
static struct regval_list module_whitebance_sunny_regs[] = {
	ENDMARKER,
};

/* Office Colour Temperature : 3500K - 5000K ,荧光灯 */
static struct regval_list module_whitebance_fluorescent_regs[] = {
	ENDMARKER,
};

/* Home Colour Temperature : 2500K - 3500K ,??? */
static struct regval_list module_whitebance_incandescent_regs[] = {
	ENDMARKER,
};

static struct regval_list module_scene_auto_regs[] = {
	ENDMARKER,
};

/*
 * The exposure target setttings
 */
static struct regval_list module_exp_comp_neg4_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_neg3_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_neg2_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_neg1_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_zero_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos1_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos2_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos3_regs[] = {
	ENDMARKER,
};

static struct regval_list module_exp_comp_pos4_regs[] = {
	ENDMARKER,
};

/*正常模式*/
static struct regval_list module_effect_normal_regs[] = {
	ENDMARKER,
};

/*单色，黑白照片*/
static struct regval_list module_effect_white_black_regs[] = {
	ENDMARKER,
};

/*负片效果*/
static struct regval_list module_effect_negative_regs[] = {
	ENDMARKER,
};

/*复古效果*/
static struct regval_list module_effect_antique_regs[] = {
	ENDMARKER,
};

static struct v4l2_ctl_cmd_info v4l2_ctl_array[] = {
	{ 
		.id = V4L2_CID_EXPOSURE, 
		.min = 0, 
		.max = 975,
		.step = 1, 
		.def = 500,
	},
	{	
		.id = V4L2_CID_GAIN, 
		.min = 10, 
		.max = 2048, 
		.step = 1, 
		.def = 30,
	},
	{
        .id = V4L2_CID_AUTO_WHITE_BALANCE,
        .min = 0,
        .max = 1,
        .step = 1,
        .def = 1,
    },
    {
        .id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
        .min = 0,
        .max = 3,
        .step = 1,
        .def = 0,
    },
    {
        .id = V4L2_CID_HFLIP,
        .min = 0,
        .max = 1,
        .step = 1,
        .def = 0,
    },
    {
        .id = V4L2_CID_VFLIP,
        .min = 0,
        .max = 1,
        .step = 1,
        .def = 0,
    },
    //{	.id = V4L2_CID_PRIVATE_PREV_CAPT, 
    //    .min = 0, 
    //    .max = 1, 
    //    .step = 1, 
    //    .def = PREVIEW_MODE,
    //},//3.4内核没有定义此命令字
	//{	.id = V4L2_CID_MIRRORFLIP, //3.10??????????,????vflip?hflip
	//	.min = NONE, 
	//	.max = HFLIP|VFLIP, 
	//	.step = 1, 
	//	.def = NONE,
	//},
};

static struct v4l2_ctl_cmd_info_menu v4l2_ctl_array_menu[] = {
	{
        .id = V4L2_CID_COLORFX,
        .max = 3,
        .mask = 0x0,
        .def = 0,
    },
    {
        .id = V4L2_CID_EXPOSURE_AUTO,
        .max = 1,
        .mask = 0x0,
        .def = 1,
    },
};

#endif /* __MODULE_DIFF_H__ */
