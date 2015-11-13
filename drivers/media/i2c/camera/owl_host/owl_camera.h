/*
 * Actions OWL S900 SoC Camera Host driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * LiChi <lichi@actions-semi.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _S900_CAMERA_H_
#define _S900_CAMERA_H_

#include <linux/clk-private.h>
#include <linux/device.h>
#include <linux/videodev2.h>
#include <media/soc_mediabus.h>
#include "owl_isp.h"

#define S900_CAMERA_DATA_HIGH		1
#define S900_CAMERA_PCLK_RISING	2
#define S900_CAMERA_HSYNC_HIGH	4
#define S900_CAMERA_VSYNC_HIGH	8
#define S900_CAMERA_DATAWIDTH_4	0x10
#define S900_CAMERA_DATAWIDTH_8	0x20
#define S900_CAMERA_DATAWIDTH_10	0x40
#define S900_CAMERA_DATAWIDTH_16	0x80

#define S900_CAMERA_DATAWIDTH_MASK (S900_CAMERA_DATAWIDTH_4 |\
				S900_CAMERA_DATAWIDTH_8 |\
				S900_CAMERA_DATAWIDTH_10 |\
				S900_CAMERA_DATAWIDTH_16)

enum s900_dev_state {
	DEV_STOP = 0,
	DEV_START,
	DEV_SUSPEND,
	DEV_RESUME,
	DEV_OPEN,
	DEV_CLOSE,
};

/*
 * GL5023 have tow channel, if support all, should register tow soc camera host
 */
struct s900_camera_dev {
	struct soc_camera_device *icds[2];
	unsigned int dvp_mbus_flags;
	struct soc_camera_host soc_host;
	int skip_frames;

	spinlock_t lock;            /* Protects video buffer lists */
	int irq;
	struct pinctrl *mfp;
	struct sensor_pwd_info spinfo;
	struct isp_regulators ir;
	struct clk *isp_clk_parent;
	struct clk *isp_clk;
	struct clk *sensor_clk_parent;
	struct clk *sensor_clk;

	/*csi clock src parent correspond to
	ISP_CHANNEL_0/1 (should be 0 or 1) */
	struct clk *csi_clk_parent;
	/*csi clock correspond to
	ISP_CHANNEL_0/1 (should be 0 or 1) */
	struct clk *ch_clk[2];
	phys_addr_t pBase;
};

struct s900_camera_param {
	struct soc_camera_device *icd;
	struct list_head capture;
#if 0
	struct vb2_buffer *cur_frm;
	struct vb2_buffer *prev_frm;
#else

	struct videobuf_buffer *cur_frm;
	struct videobuf_buffer *prev_frm;
#endif
	struct completion wait_stop;

	int started;
	/* ISP data offsets within croped by the S900 camera output */
	unsigned int isp_left;
	unsigned int isp_top;
	/* Client output, as seen by the S900 */
	unsigned int width;
	unsigned int height;
	/*
	 * User window from S_CROP / G_CROP, produced by client cropping,
	 * S900 cropping, mapped back onto the client
	 * input window
	 */
	struct v4l2_rect subrect;
	/* Camera cropping rectangle */
	struct v4l2_rect rect;
	const struct soc_mbus_pixelfmt *extra_fmt;
	enum v4l2_mbus_pixelcode code;
	unsigned long flags;
	unsigned int skip_frames;
	int channel;
	enum v4l2_mbus_type bus_type;
	int lane_num;
	int raw_width;
	int data_type;/*0 for yuv sensor,1 for raw-bayer sensor*/
	int (*ext_cmd)(struct v4l2_subdev *sd, int cmd, void *args);
	int b_splited_capture;

	int rb_w;
	int rb_lsub_col_size;
	int rb_lright_cut;
	int rb_lcol_stride;
	int rb_lleft_cut;
	int rb_rsub_col_size;
	int rb_rright_cut;
	int rb_rcol_stride;
	int rb_rleft_cut;
	int rb_rows;
	int rb_cnt;

	phys_addr_t p_raw_addr;

	int n_crop_x;
	int n_crop_y;
	int n_r_skip_num;
	int n_r_skip_size;

	int real_w;
	int real_h;

	/*
	 * 0 disable raw store
	 * 1 enable raw store in preview mode
	 * 2 enable raw store in capture mode
	 * 3 raw store finished oneFrame
	 * 4 raw store finished twoFrame
	 * 5 ....
	 */
	unsigned long b_raw_store_status;
	int bRawStoreUpdata;
};

/*#define DEBUG_ISP_INFO*/
#define DEBUG_ISP_ERR

#ifdef DEBUG_ISP_INFO
#define isp_info(fmt, ...) \
    printk(KERN_INFO fmt, ## __VA_ARGS__)
#else
#define	isp_info(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_ISP_ERR
#define isp_err(fmt, ...) \
    printk(KERN_ERR "%s(L%d) error: " fmt, __func__, __LINE__, ## __VA_ARGS__)
#else
#define	isp_err(fmt, ...) do {} while (0)
#endif

#define CMU_SENSORCLK_INVT0 (0x1 << 12)
#define CMU_SENSORCLK_INVT1 (0x1 << 13)

#define ISP_CH1_PRELINE_NUM_MASK (0xFFF << 0)
#define ISP_CH1_PRELINE_NUM(x) ((0xFFF & (x)) << 0)

#define ISP_CH1_COL_START(x)  (0x1FFF & (x))
#define ISP_CH1_COL_END(x)  ((0x1FFF & (x)) << 16)

#define ISP_CH1_ROW_START(x)  (0xFFF & (x))
#define ISP_CH1_ROW_END(x)  ((0xFFF & (x)) << 16)

#define ISP_CH2_PRELINE_NUM_MASK (0xFFF << 16)
#define ISP_CH2_PRELINE_NUM(x) ((0xFFF & (x)) << 16)

#define ISP_CH2_COL_START(x)  (0x1FFF & (x))
#define ISP_CH2_COL_END(x)  ((0x1FFF & (x)) << 16)

#define ISP_CH2_ROW_START(x)  (0xFFF & (x))
#define ISP_CH2_ROW_END(x)  ((0xFFF & (x)) << 16)

#define ISP_ENABLE_EN (0x1)
#define ISP_ENABLE_MODE_MASK (0x3 << 1)
#define ISP_ENABLE_MODE(x)   ((0x3 & (x)) << 1)
#define ISP_ENABLE_CH1_EN    (0x1 << 6)
#define ISP_ENABLE_CH2_EN    (0x1 << 6)
#define ISP_ENABLE_CH1_MODE  (0x1 << 6)
#define ISP_ENABLE_CH2_MODE  (0x1 << 6)
#define ISP_ENABLE_MASK      (0x3F)

#define ISP_INT_STAT_RAWSTORE_FRAME_END_EN (0x1 << 0)
#define ISP_INT_STAT_REFORM_FRAME_END_EN (0x1 << 1)
#define ISP_INT_STAT_FRAME_END_INT_EN (0x1 << 2)
#define ISP_INT_STAT_AF_INT_EN (0x1 << 3)
#define ISP_INT_STAT_RAW_RB_PL_INT_EN (0x1 << 4)
#define ISP_INT_STAT_CH1_PL_INT_EN (0x1 << 5)
#define ISP_INT_STAT_CH2_PL_INT_EN (0x1 << 6)

#define ISP_INT_STAT_CH1_PL_PD (0x1 << 21)
#define ISP_INT_STAT_CH2_PL_PD (0x1 << 22)
#define ISP_INT_STAT_RS_FRAME_END_PD (0x1 << 16)
#define ISP_INT_STAT_YUV_FRAME_END_PD (0x1 << 17)
#define ISP_INT_STAT_FRAME_END_PD (0x1 << 18)
#define ISP_INT_STAT_AF_PD (0x1 << 19)
#define ISP_INT_STAT_RB_PL_PD (0x1 << 20)
#define ISP_INT_STAT_OVERFLOWL_PD (0x3 << 29)

/* isp_out_fmt reg*/
#define ISP_OUT_FMT_STRIDE1_MASK  (0x1fff << 16)
#define ISP_OUT_FMT_STRIDE1(x)    ((0x1fff & (x)) << 16)
#define ISP_OUT_FMT_SEMI_UV_INV   (0x1 << 15)
#define ISP_OUT_FMT_MASK    0x3
#define ISP_OUT_FMT_YUV420  0x0
#define ISP_OUT_FMT_YUV422P 0x1
#define ISP_OUT_FMT_NV12    0x2
#define ISP_OUT_FMT_YUYV    0x3

#define ISP_CTL_CHANNEL1_INTF_MIPI (0x1 << 8)
#define ISP_CTL_CHANNEL1_INTF_PARAL (0x0 << 8)
#define ISP_CTL_MODE_MASK (0x3 << 12)
#define ISP_CTL_MODE_RGB8 (0x2 << 12)
#define ISP_CTL_MODE_RGB10 (0x0 << 12)
#define ISP_CTL_MODE_RGB12 (0x0 << 12)
#define ISP_CTL_MODE_YUYV  (0x1 << 12)
#define ISP_CTL_HSYNC_ACTIVE_HIGH (0x1 << 11)
#define ISP_CTL_VSYNC_ACTIVE_HIGH (0x1 << 10)
#define ISP_CTL_COLOR_SEQ_MASK (0x3<<24)
#define ISP_CTL_COLOR_SEQ_UYVY (0x0<<24)
#define ISP_CTL_COLOR_SEQ_VYUY (0x1<<24)
#define ISP_CTL_COLOR_SEQ_YUYV (0x2<<24)
#define ISP_CTL_COLOR_SEQ_YVYU (0x3<<24)
#define ISP_CTL_COLOR_SEQ_SBGGR (0x0<<24)
#define ISP_CTL_COLOR_SEQ_SGRBG (0x1<<24)
#define ISP_CTL_COLOR_SEQ_SGBRG (0x2<<24)
#define ISP_CTL_COLOR_SEQ_SRGGB (0x3<<24)

#define ISP_CTL_RAW_CLOLR_SEQ_MASK  (0x3<<28)
#define ISP_CTL_RAW_COLOR_SEQ_SBGGR (0x0<<28)
#define ISP_CTL_RAW_COLOR_SEQ_SGRBG (0x1<<28)
#define ISP_CTL_RAW_COLOR_SEQ_SGBRG (0x2<<28)
#define ISP_CTL_RAW_COLOR_SEQ_SRGGB (0x3<<28)

#define ISP2_CTL_RAW_COLOR_SEQ_SBGGR (0x0<<28)
#define ISP2_CTL_RAW_COLOR_SEQ_SGRBG (0x1<<28)
#define ISP2_CTL_RAW_COLOR_SEQ_SGBRG (0x2<<28)
#define ISP2_CTL_RAW_COLOR_SEQ_SRGGB (0x3<<28)

#define ISP2_CTL_CHANNEL_INTF_MIPI (0x1 << 15)
#define ISP2_CTL_CHANNEL_INTF_PARAL (0x0 << 15)
#define ISP2_CTL_MODE_MASK (0x3 << 19)
#define ISP2_CTL_MODE_RGB8 (0x2 << 19)
#define ISP2_CTL_MODE_YUYV  (0x1 << 19)
#define ISP2_CTL_MODE_RGB10  (0x0 << 19)
#define ISP2_CTL_MODE_RGB12  (0x0 << 19)

#define ISP2_CTL_HSYNC_ACTIVE_HIGH (0x1 << 18)
#define ISP2_CTL_VSYNC_ACTIVE_HIGH (0x1 << 17)
#define ISP2_CTL_COLOR_SEQ_MASK (0x3<<26)
#define ISP2_CTL_COLOR_SEQ_UYVY (0x0<<26)
#define ISP2_CTL_COLOR_SEQ_VYUY (0x1<<26)
#define ISP2_CTL_COLOR_SEQ_YUYV (0x2<<26)
#define ISP2_CTL_COLOR_SEQ_YVYU (0x3<<26)
#define ISP2_CTL_COLOR_SEQ_SBGGR (0x0<<26)
#define ISP2_CTL_COLOR_SEQ_SGRBG (0x1<<26)
#define ISP2_CTL_COLOR_SEQ_SGBRG (0x2<<26)
#define ISP2_CTL_COLOR_SEQ_SRGGB (0x3<<26)



#define CSI_CTRL_EN  (0x1 << 0)
#define CSI_CTRL_D_PHY_EN    (0x1 << 2)
#define CSI_CTRL_PHY_INIT    (0x1 << 3)
#define CSI_CTRL_LANE_NUM(x) (((x) & 0x3) << 4)
#define CSI_CTRL_ECE (0x1 << 6)
#define CSI_CTRL_CCE (0x1 << 7)
#define CSI_CTRL_CLK_LANE_HS  (0x1 << 8)
#define CSI_CTRL_PHY_INIT_SEL (0x1 << 9)

#define MIPI_PHY_1LANE 0x3
#define MIPI_PHY_2LANE 0x7
#define MIPI_PHY_3LANE 0xf
#define MIPI_PHY_4LANE 0x1f

#define CSI_CONTEXT_EN (0x1 << 0)
#define CSI_CONTEXT_DT(x) (((x) & 0x3f) << 1)

#define CONTEXT_DT_RAW8  (0x2A)
#define CONTEXT_DT_RAW10 (0x2B)
#define CONTEXT_DT_RAW12 (0x2C)

#endif
