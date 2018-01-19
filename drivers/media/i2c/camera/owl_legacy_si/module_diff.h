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

#include "owl_camera.h"
#include "owl_device.h"

#define MODULE_SI

#define MODULE_CLK_PARENT    "display_pll"
#define MODULE_CLK           "si"

#define SENSOR_CLK_PARENT  "sensor_out"
#define CH_CLK_0             "sensor_clk"
#define CH_CLK_1             "sensor_clk"
#define FDT_COMPATIBLE              "actions,ats3605-isp"

#define CAM_HOST_NAME               "camera-host"

#define MODULE_MAX_WIDTH		     4288
#define MODULE_MAX_HEIGHT		     4096
#define MODULE_WORK_CLOCK		     60000000	/*60MHz */
#define SENSOR_WORK_CLOCK		     24000000	/*24MHz */

#define OFFSET_OF_ALIGN			     0
#define MAX_VIDEO_MEM			     100
#define PRELINE_NUM		         16
#define MIN_FRAME_RATE		         2
#define FRAME_INTVL_MSEC	         (1000 / MIN_FRAME_RATE)

#define GPIO_HIGH				     0x1
#define GPIO_LOW				     0x0
#define SENSOR_FRONT			     0x1
#define SENSOR_REAR			     0x2
#define SENSOR_DUAL				     0x4

#define V4L2_CID_MODULE_UPDATE	     0x10001
#define V4L2_CID_AF_UPDATE		     0x10002
#define V4L2_CID_MODULE_GETINFO      0x1003
#define V4L2_CID_MODULE_UPDATERAW    0x1004

#define MODULE_INT_STAT_CH2_FRAME_END_INT_EN (0x1 << 5)
#define MODULE_INT_STAT_CH_FRAME_END_INT_EN (0x1 << 4)
#define MODULE_INT_STAT_AF_INT_EN (0x1 << 3)
#define MODULE_INT_STAT_RAW_RB_PL_INT_EN (0x1 << 4)
#define MODULE_INT_STAT_CH_PL_INT_EN (0x1 << 5)
#define MODULE_INT_STAT_CH2_PL_INT_EN (0x1 << 1)
#define MODULE_INT_STAT_CH_PL_PD (0x1 << 9)
#define MODULE_INT_STAT_CH2_PL_PD (0x1 << 9)

#define MODULE_CTL_CHANNEL2_INTF_MIPI (0x1 << 14)
#define MODULE_CTL_CHANNEL2_INTF_PARAL (0x0 << 14)

#endif				/* __MODULE_DIFF_H__ */
