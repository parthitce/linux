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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf-dma-contig.h>
#include <linux/slab.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <linux/v4l2-mediabus.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/io.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <linux/pinctrl/consumer.h>
#include "owl_host_reg.h"
#include "owl_camera.h"
#include "isp_pwd-owl.h"

#define ISP_FDT_COMPATIBLE "actions,s900-isp"
#define S900_CAM_HOST_NAME "s900-camera-host"

#define ISP_MAX_WIDTH			4288
#define ISP_MAX_HEIGHT			4096
#define ISP_WORK_CLOCK			334000000		/*330MHz*/
#define SENSOR_WORK_CLOCK		24000000		/*24MHZ*/

#define OFFSET_OF_ALIGN			0
#define MAX_VIDEO_MEM			100
#define ISP_PRELINE_NUM			10
#define ISP_MIN_FRAME_RATE		2
#define ISP_FRAME_INTVL_MSEC	(1000 / ISP_MIN_FRAME_RATE)
#define ISP_RAWSTORE_FRAMES_NUM	1

#define GPIO_HIGH				0x1
#define GPIO_LOW				0x0
#define SENSOR_FRONT			0x1
#define SENSOR_REAR				0x2
#define SENSOR_DUAL				0x4

#define V4L2_CID_BISP_UPDATE	0x10001
#define V4L2_CID_AF_UPDATE		0x10002
#define V4L2_CID_BISP_GETINFO	0x1003
#define V4L2_CID_BISP_UPDATERAW	0x1004

static int isp_gpio_init(struct device_node *fdt_node, struct sensor_pwd_info *spinfo);

static void *GISPMAPADDR;
static void *GCSI1MAPADDR;
static void *GCSI2MAPADDR;
static void *GCMUMAPADDR;

static unsigned int  isp_reg_read(unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GISPMAPADDR + reg - ISP_BASE;
	unsigned int value = 0;
	value = readl_relaxed(pregs);
	return value;
}

static void isp_reg_write(unsigned int  value, unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GISPMAPADDR + reg - ISP_BASE;
	writel_relaxed(value, pregs);
}

static unsigned int  csi0_reg_read(unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCSI1MAPADDR + reg - CSI0_BASE;
	unsigned int value = 0;
	value = readl_relaxed(pregs);
	return value;
}

static void  csi0_reg_write(unsigned int value, unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCSI1MAPADDR + reg - CSI0_BASE;
	writel_relaxed(value, pregs);
}

static unsigned int  csi1_reg_read(unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCSI2MAPADDR + reg - CSI1_BASE;
	unsigned int value = 0;
	value = readl_relaxed(pregs);
	return value;
}

static void  csi1_reg_write(unsigned int value, unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCSI2MAPADDR + reg - CSI1_BASE;
	writel_relaxed(value, pregs);
}

static unsigned int  cmu_reg_read(unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCMUMAPADDR + reg - CMU_BASE;
	unsigned int value = 0;
	value = readl_relaxed(pregs);
	return value;
}

static void  cmu_reg_write(unsigned int value, unsigned int  reg)
{
	volatile unsigned char *pregs = (unsigned char *)GCMUMAPADDR + reg - CMU_BASE;
	writel_relaxed(value, pregs);
}

static int camera_clock_init(struct device *dev,
	struct s900_camera_dev *cdev)
{
	struct sensor_pwd_info *spinfo = &cdev->spinfo;

	isp_info("%s\n", __func__);
	cdev->isp_clk_parent = devm_clk_get(dev, "assist_pll");
	cdev->isp_clk = devm_clk_get(dev, "bisp");

	cdev->sensor_clk_parent = devm_clk_get(dev, "hosc");
	cdev->sensor_clk = devm_clk_get(dev, "sensor");

	cdev->csi_clk_parent = devm_clk_get(dev, "dev_clk");
	cdev->ch_clk[ISP_CHANNEL_0] = devm_clk_get(dev, "csi0");
	cdev->ch_clk[ISP_CHANNEL_1] = devm_clk_get(dev, "csi1");

	if (IS_ERR(cdev->isp_clk) || IS_ERR(cdev->sensor_clk)
		|| IS_ERR(cdev->ch_clk[ISP_CHANNEL_0])
		|| IS_ERR(cdev->ch_clk[ISP_CHANNEL_1])
		|| IS_ERR(cdev->isp_clk_parent)
		|| IS_ERR(cdev->sensor_clk_parent)
		|| IS_ERR(cdev->csi_clk_parent)) {
		isp_err("error:can't get clocks!!!\n");
		return -EINVAL;
	}

	/*setting bisp clock parent -> dev_clk*/
	clk_set_parent(cdev->isp_clk, cdev->isp_clk_parent);

	/*setting sensor clock parent -> hosc*/
	clk_set_parent(cdev->sensor_clk, cdev->sensor_clk_parent);

	/*setting csi0/1 clock parent -> display_pll*/
	clk_set_parent(cdev->ch_clk[ISP_CHANNEL_0], cdev->csi_clk_parent);
	clk_set_parent(cdev->ch_clk[ISP_CHANNEL_1], cdev->csi_clk_parent);


	spinfo->ch_clk[ISP_CHANNEL_0] = cdev->ch_clk[ISP_CHANNEL_0];
	spinfo->ch_clk[ISP_CHANNEL_1] = cdev->ch_clk[ISP_CHANNEL_1];

	isp_info("camera_clock_init success!!!\n");
	return 0;
}

static void camera_clock_enable(struct s900_camera_dev *cdev,
	struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	mipi_setting *mipi_cfg = module_info->mipi_cfg;

	isp_info("%s\n", __func__);

	/*setting bisp clock rate 330M*/
	clk_set_rate(cdev->isp_clk, ISP_WORK_CLOCK);

	/*setting sensor clock rate 24M*/
	clk_set_rate(cdev->sensor_clk, SENSOR_WORK_CLOCK);

	/* Enable SENSOR CLOCK */
	clk_prepare(cdev->sensor_clk);
	clk_enable(cdev->sensor_clk);

	/* Enable CSI CLOCK */
	if (ISP_CHANNEL_0 == cam_param->channel) {
		clk_set_rate(cdev->ch_clk[ISP_CHANNEL_0], mipi_cfg->csi_clk);
		clk_prepare(cdev->ch_clk[ISP_CHANNEL_0]);
		clk_enable(cdev->ch_clk[ISP_CHANNEL_0]);

		isp_info("bisp: %ld, csi_parent:%ld, csi0:%ld,sensor:%ld\n",
			clk_get_rate(cdev->isp_clk),
			clk_get_rate(cdev->csi_clk_parent),
			clk_get_rate(cdev->ch_clk[ISP_CHANNEL_0]),
			clk_get_rate(cdev->sensor_clk));
	} else {
		clk_set_rate(cdev->ch_clk[ISP_CHANNEL_1], mipi_cfg->csi_clk);
		clk_prepare(cdev->ch_clk[ISP_CHANNEL_1]);
		clk_enable(cdev->ch_clk[ISP_CHANNEL_1]);

		isp_info("bisp: %ld, csi_parent:%ld, csi1:%ld,sensor:%ld\n",
			clk_get_rate(cdev->isp_clk),
			clk_get_rate(cdev->csi_clk_parent),
			clk_get_rate(cdev->ch_clk[ISP_CHANNEL_1]),
			clk_get_rate(cdev->sensor_clk));
	}

	/* Enable ISP CLOCK */
	clk_prepare(cdev->isp_clk);
	clk_enable(cdev->isp_clk);


}

static void camera_clock_disable(struct s900_camera_dev *cdev,
	struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;

	isp_info("%s\n", __func__);
	/* Disable SENSOR CLOCK */
	clk_put(cdev->sensor_clk);

	/* Disable CSI CLOCK */
	if (ISP_CHANNEL_0 == cam_param->channel)
		clk_put(cdev->ch_clk[ISP_CHANNEL_0]);
	else
		clk_put(cdev->ch_clk[ISP_CHANNEL_1]);

	/* Disable ISP CLOCK */
	clk_put(cdev->isp_clk);

}

static int updata_bisp_info(struct v4l2_subdev *sd,
	struct s900_camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0)*/
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd)
			cam_param->ext_cmd(sd, V4L2_CID_BISP_UPDATE, cam_param);
	}
	return ret;
}

static int updata_bisp_rawinfo(struct v4l2_subdev *sd,
	struct s900_camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0)*/
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd)
			cam_param->ext_cmd(sd, V4L2_CID_BISP_UPDATERAW, cam_param);
	}
	return ret;
}

static int updata_get_bispinfo(struct v4l2_subdev *sd,
	struct s900_camera_param *cam_param)
{
	int ret = 0;
	/*if(cam_param->bFlagOutBayer == 0)*/
	if (SENSOR_DATA_TYPE_RAW == cam_param->data_type) {
		if (cam_param->ext_cmd)
			cam_param->ext_cmd(sd, V4L2_CID_BISP_GETINFO, (void *)cam_param);
	}
	return ret;
}

static int mipi_csi_init(struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	mipi_setting *mipi_cfg = module_info->mipi_cfg;

	unsigned int reg_data = 0;
	int time = 50;

	if (ISP_CHANNEL_0 == cam_param->channel) {
		csi0_reg_write(MIPI_PHY_4LANE | (mipi_cfg->clk_lane_direction << 5), CSI0_ANALOG_PHY);

		reg_data = 0x0c | ((mipi_cfg->lan_num & 0x3) << 4)
			| (mipi_cfg->ecc_en << 6)
			| (mipi_cfg->crc_en << 7)
			| (mipi_cfg->hclk_om_ent_en << 8)
			| (mipi_cfg->lp11_not_chek << 9)
			| (mipi_cfg->hsclk_edge << 10);

		csi0_reg_write(reg_data, CSI0_CTRL);

		reg_data = csi0_reg_read(CSI0_CTRL);
		while ((reg_data & CSI_CTRL_PHY_INIT) && (--time > 0)) {
			reg_data = csi0_reg_read(CSI0_CTRL);
			udelay(1);
		}
		if (time <= 0)
			isp_err("csi0 init time out\n");

		csi0_reg_write((mipi_cfg->contex0_virtual_num << 7)
			| (mipi_cfg->contex0_data_type << 1)
			| (mipi_cfg->contex0_en), CSI0_CONTEXT_CFG);

		csi0_reg_write(0xF4, CSI0_PHY_T0);
		csi0_reg_write(mipi_cfg->clk_term_time +
			(mipi_cfg->clk_settle_time << 4), CSI0_PHY_T1);
		csi0_reg_write(mipi_cfg->data_term_time +
			(mipi_cfg->data_settle_time << 4), CSI0_PHY_T2);

		csi0_reg_write(0xffffffff, CSI0_ERROR_PENDING);
		csi0_reg_write(0xffff0000, CSI0_STATUS_PENDING);

		csi0_reg_write(csi0_reg_read(CSI0_CTRL)
			| CSI_CTRL_EN, CSI0_CTRL);
		isp_info("enable csi0\n");
	} else {
		csi1_reg_write(MIPI_PHY_4LANE | (mipi_cfg->clk_lane_direction << 5), CSI1_ANALOG_PHY);

		reg_data = 0x0c | ((mipi_cfg->lan_num & 0x3) << 4)
			| (mipi_cfg->ecc_en << 6)
			| (mipi_cfg->crc_en << 7)
			| (mipi_cfg->hclk_om_ent_en << 8)
			| (mipi_cfg->lp11_not_chek << 9)
			| (mipi_cfg->hsclk_edge << 10);

		csi1_reg_write(reg_data, CSI1_CTRL);

		reg_data = csi1_reg_read(CSI1_CTRL);
		while ((reg_data & CSI_CTRL_PHY_INIT) && (--time > 0)) {
			reg_data = csi1_reg_read(CSI1_CTRL);
			udelay(1);
		}
		if (time <= 0)
			isp_err("csi1 init time out\n");

		csi1_reg_write((mipi_cfg->contex0_virtual_num << 7)
			| (mipi_cfg->contex0_data_type << 1)
			| (mipi_cfg->contex0_en), CSI1_CONTEXT_CFG);

		csi1_reg_write(0xF4, CSI1_PHY_T0);
		csi1_reg_write(mipi_cfg->clk_term_time +
			(mipi_cfg->clk_settle_time << 4), CSI1_PHY_T1);
		csi1_reg_write(mipi_cfg->data_term_time +
			(mipi_cfg->data_settle_time << 4), CSI1_PHY_T2);

		csi1_reg_write(0xffffffff, CSI1_ERROR_PENDING);
		csi1_reg_write(0xffff0000, CSI1_STATUS_PENDING);

		csi1_reg_write(csi1_reg_read(CSI1_CTRL)
			| CSI_CTRL_EN, CSI1_CTRL);

		isp_info("enable csi1\n");
	}
	return 0;

}

static inline void mipi_csi_disable(struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cdev = ici->priv;
	struct clk *csi_clk = NULL;

	if (ISP_CHANNEL_0 == cam_param->channel) {
		csi_clk = cdev->ch_clk[ISP_CHANNEL_0];
		csi0_reg_write(0x0, CSI0_CONTEXT_CFG);
		csi0_reg_write(0x0, CSI0_CTRL);
		csi0_reg_write(0x0, CSI0_ANALOG_PHY);
		csi0_reg_write(0x0, CSI0_PHY_T0);
		csi0_reg_write(0x0, CSI0_PHY_T1);
		csi0_reg_write(0x0, CSI0_PHY_T2);
	} else {
		csi_clk = cdev->ch_clk[ISP_CHANNEL_1];
		csi1_reg_write(0x0, CSI1_CONTEXT_CFG);
		csi1_reg_write(0x0, CSI1_CTRL);
		csi1_reg_write(0x0, CSI1_ANALOG_PHY);
		csi1_reg_write(0x0, CSI1_PHY_T0);
		csi1_reg_write(0x0, CSI1_PHY_T1);
		csi1_reg_write(0x0, CSI1_PHY_T2);
	}

	isp_info("disable csi\n");
	return;
}

static inline void isp_set_preline(struct soc_camera_device *icd)
{
	struct s900_camera_param *param = icd->host_priv;
	unsigned int state;


	if (ISP_CHANNEL_0 == param->channel) {
		state = isp_reg_read(CH_PRELINE_SET);
		state &= ~ISP_CH1_PRELINE_NUM_MASK;

		state |= ISP_CH1_PRELINE_NUM(ISP_PRELINE_NUM);
		isp_reg_write(state, CH_PRELINE_SET);
		isp_reg_write(state, RAW_RB_PRELINE_SET);
	} else {
		state = isp_reg_read(CH_PRELINE_SET);
		state &= ~ISP_CH2_PRELINE_NUM_MASK;

		state |= ISP_CH2_PRELINE_NUM(ISP_PRELINE_NUM);
		isp_reg_write(state, CH_PRELINE_SET);
		isp_reg_write(state, RAW_RB_PRELINE_SET);
	}
}

static inline int isp_set_rb_size(struct s900_camera_param *cam_param,
	int bleft)
{
	if (bleft == 0) {
		isp_reg_write(cam_param->rb_lcol_stride
			| (cam_param->height << 16), RAW_RB_SIZE);
		isp_reg_write(cam_param->rb_lsub_col_size
			| (cam_param->rb_lleft_cut << 16)
			| (cam_param->rb_lright_cut << 24), RAW_RB_COL_RANGE);
	} else {
		isp_reg_write(cam_param->rb_rcol_stride
			| (cam_param->height << 16), RAW_RB_SIZE);
		isp_reg_write(cam_param->rb_rsub_col_size
			| (cam_param->rb_rleft_cut << 16)
			| (cam_param->rb_rright_cut << 24), RAW_RB_COL_RANGE);
	}
	return 0;
}

static inline int isp_set_col_range(struct soc_camera_device *icd,
	unsigned int start, unsigned int end, unsigned int rstart,
	unsigned int rend)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	int out_fmt;
	int width = end - start + 1;
	if ((width > ISP_MAX_WIDTH) || (width < 32)
		|| (start >= 8192) || (end >= 8192)) {
		isp_err("width is over range, width:%d, start:%d, end:%d\n",
			width, start, end);
		return -EINVAL;
	}

	if ((cam_param->width > 2592) && (cam_param->data_type == SENSOR_DATA_TYPE_RAW)) {
		int w2 = (cam_param->width / 2) & 0xffc0;

		cam_param->rb_cnt = 0;
		cam_param->rb_w = w2;
		cam_param->b_splited_capture = 1;
		cam_param->rb_lsub_col_size = w2 + 64;
		cam_param->rb_lright_cut = 64;
		cam_param->rb_lcol_stride = cam_param->width
			- cam_param->rb_lsub_col_size;
		cam_param->rb_lleft_cut = 0;

		cam_param->rb_rsub_col_size =  cam_param->width - w2 + 64;
		cam_param->rb_rright_cut = 0;
		cam_param->rb_rcol_stride = cam_param->width
			- cam_param->rb_rsub_col_size;
		cam_param->rb_rleft_cut = 64;
		cam_param->rb_rows = cam_param->height;
		out_fmt = isp_reg_read(ISP_OUT_FMT);
		out_fmt = out_fmt & (~(0x1FFF << 16));
		out_fmt = out_fmt | ((cam_param->rb_lcol_stride
			+ cam_param->rb_lright_cut) << 16);
		isp_reg_write(out_fmt, ISP_OUT_FMT);

		isp_reg_write(400, RAW_RB_PRELINE_SET);
		isp_reg_write(cam_param->rb_lcol_stride
			| (cam_param->height << 16), RAW_RB_SIZE);
		isp_reg_write(cam_param->rb_lsub_col_size
			| (cam_param->rb_lleft_cut << 16)
			| (cam_param->rb_lright_cut << 24), RAW_RB_COL_RANGE);

	} else {
		cam_param->b_splited_capture = 0;
		cam_param->rb_w = 0;
	}
	/* support stride */
	/*if ((cam_param->width % 32) && (cam_param->b_splited_capture == 0)) {
		int alinged = 32;
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			out_fmt = isp_reg_read(ISP_OUT_FMT);
		else
			out_fmt = isp_reg_read(YUV_OUT_FMT);

		out_fmt = out_fmt & (~(0x1FFF << 16));
		out_fmt = out_fmt | (alinged << 16);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			isp_reg_write(out_fmt, ISP_OUT_FMT);
		else
			isp_reg_write(out_fmt, YUV_OUT_FMT);

		end = (width & (~0x1f)) - 1;
	} else if (((cam_param->width % 32) == 0)
			&& (cam_param->b_splited_capture == 0)) {*/
	{

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			out_fmt = isp_reg_read(ISP_OUT_FMT);
		else
			out_fmt = isp_reg_read(YUV_OUT_FMT);

		out_fmt = out_fmt & (~(0x1FFF << 16));

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			isp_reg_write(out_fmt, ISP_OUT_FMT);
		else
			isp_reg_write(out_fmt, YUV_OUT_FMT);
	}

	/*if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_reg_write(ISP_CH1_COL_START(start)
		| ISP_CH1_COL_END(end), CH1_COL_RANGE);
	} else {
		isp_reg_write(ISP_CH2_COL_START(start)
		| ISP_CH2_COL_END(end), CH2_COL_RANGE);
	}*/

	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_reg_write(ISP_CH1_COL_START(0)
			| ISP_CH1_COL_END(end - start), CH1_COL_RANGE);
	} else {
		isp_reg_write(ISP_CH2_COL_START(0)
			| ISP_CH2_COL_END(end - start), CH2_COL_RANGE);
	}

	return 0;
	}

static inline int isp_set_row_range(struct soc_camera_device *icd,
	unsigned int start, unsigned int end)
	{
	struct s900_camera_param *cam_param = icd->host_priv;
	int height = end - start + 1;

	if ((height > ISP_MAX_HEIGHT) || (height < 1)
		|| (start >= 4096) || (end >= 4096)) {
		isp_err("height is over range, height:%d, start:%d, end:%d\n",
			height, start, end);
		return -EINVAL;
	}

	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_reg_write(ISP_CH1_ROW_START(0)
			| ISP_CH1_ROW_END(end - start), CH1_ROW_RANGE);
	} else {
		isp_reg_write(ISP_CH2_ROW_START(0)
			| ISP_CH2_ROW_END(end - start), CH2_ROW_RANGE);
	}
	return 0;
}

/* rect is guaranteed to not exceed the camera rectangle */
static inline void isp_set_rect(struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	isp_set_col_range(icd, cam_param->isp_left,
		cam_param->isp_left + cam_param->real_w - 1,
		cam_param->isp_top, cam_param->isp_top
		+ cam_param->real_h  - 1);
	isp_set_row_range(icd, cam_param->isp_top,
		cam_param->isp_top + cam_param->real_h - 1);
}

static int isp_set_input_fmt(struct soc_camera_device *icd, enum v4l2_mbus_pixelcode code)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	unsigned int isp_ctl = isp_reg_read(ISP_ENABLE);

	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_ctl &= ~(ISP_CTL_MODE_MASK
			| ISP_CTL_COLOR_SEQ_MASK | ISP_CTL_RAW_CLOLR_SEQ_MASK);
		switch (code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= (ISP_CTL_MODE_YUYV | ISP_CTL_COLOR_SEQ_UYVY);
			break;
		case V4L2_MBUS_FMT_VYUY8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= (ISP_CTL_MODE_YUYV | ISP_CTL_COLOR_SEQ_VYUY);
			break;
		case V4L2_MBUS_FMT_YUYV8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= (ISP_CTL_MODE_YUYV | ISP_CTL_COLOR_SEQ_YUYV);
			break;
		case V4L2_MBUS_FMT_YVYU8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= (ISP_CTL_MODE_YUYV | ISP_CTL_COLOR_SEQ_YVYU);
			break;
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			isp_ctl |= ISP_CTL_MODE_RGB8
				| ISP_CTL_COLOR_SEQ_SGRBG
				| ISP_CTL_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB8_1X8:
			isp_ctl |= ISP_CTL_MODE_RGB8
				| ISP_CTL_COLOR_SEQ_SRGGB
				| ISP_CTL_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG8_1X8:
			isp_ctl |= ISP_CTL_MODE_RGB8
				| ISP_CTL_COLOR_SEQ_SGBRG
				| ISP_CTL_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR8_1X8:
			isp_ctl |= ISP_CTL_MODE_RGB8
				| ISP_CTL_COLOR_SEQ_SBGGR
				| ISP_CTL_RAW_COLOR_SEQ_SBGGR;
			break;

		case V4L2_MBUS_FMT_SGRBG10_1X10:
			isp_ctl |= ISP_CTL_MODE_RGB10
				| ISP_CTL_COLOR_SEQ_SGRBG
				| ISP_CTL_RAW_COLOR_SEQ_SGRBG;
			break;

		case V4L2_MBUS_FMT_SRGGB10_1X10:
			isp_ctl |= ISP_CTL_MODE_RGB10
				| ISP_CTL_COLOR_SEQ_SRGGB
				| ISP_CTL_RAW_COLOR_SEQ_SRGGB;
			break;

		case V4L2_MBUS_FMT_SGBRG10_1X10:
			isp_ctl |= ISP_CTL_MODE_RGB10
				| ISP_CTL_COLOR_SEQ_SGBRG
				| ISP_CTL_RAW_COLOR_SEQ_SGBRG;
			break;

		case V4L2_MBUS_FMT_SBGGR10_1X10:
			isp_ctl |= ISP_CTL_MODE_RGB10
				| ISP_CTL_COLOR_SEQ_SBGGR
				| ISP_CTL_RAW_COLOR_SEQ_SBGGR;
			break;
		default:
			isp_err("input data error (pixel code:0x%x)\n", code);
			break;
	    }
	    isp_reg_write(isp_ctl, ISP_ENABLE);
	} else if (ISP_CHANNEL_1 == cam_param->channel) {
		isp_ctl &= ~(ISP2_CTL_MODE_MASK
			| ISP2_CTL_COLOR_SEQ_MASK | ISP_CTL_RAW_CLOLR_SEQ_MASK);
		switch (code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= ISP2_CTL_MODE_YUYV | ISP2_CTL_COLOR_SEQ_UYVY;
			break;
		case V4L2_MBUS_FMT_VYUY8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= ISP2_CTL_MODE_YUYV | ISP2_CTL_COLOR_SEQ_VYUY;
			break;
		case V4L2_MBUS_FMT_YUYV8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= ISP2_CTL_MODE_YUYV | ISP2_CTL_COLOR_SEQ_YUYV;
			break;
		case V4L2_MBUS_FMT_YVYU8_2X8:   /* UYVY */
			/* should be the same as senor's output format*/
			isp_ctl |= ISP2_CTL_MODE_YUYV | ISP2_CTL_COLOR_SEQ_YVYU;
			break;
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			isp_ctl |= ISP2_CTL_MODE_RGB8
				| ISP2_CTL_COLOR_SEQ_SGRBG
				| ISP2_CTL_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB8_1X8:
			isp_ctl |= ISP2_CTL_MODE_RGB8
				| ISP2_CTL_COLOR_SEQ_SRGGB
				| ISP2_CTL_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG8_1X8:
			isp_ctl |= ISP2_CTL_MODE_RGB8
				| ISP2_CTL_COLOR_SEQ_SGBRG
				| ISP2_CTL_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR8_1X8:
			isp_ctl |= ISP2_CTL_MODE_RGB8
				| ISP2_CTL_COLOR_SEQ_SBGGR
				| ISP2_CTL_RAW_COLOR_SEQ_SBGGR;
			break;
		case V4L2_MBUS_FMT_SGRBG10_1X10:
			isp_ctl |= ISP2_CTL_MODE_RGB10
				| ISP2_CTL_COLOR_SEQ_SGRBG
				| ISP2_CTL_RAW_COLOR_SEQ_SGRBG;
			break;
		case V4L2_MBUS_FMT_SRGGB10_1X10:
			isp_ctl |= ISP2_CTL_MODE_RGB10
				| ISP2_CTL_COLOR_SEQ_SRGGB
				| ISP2_CTL_RAW_COLOR_SEQ_SRGGB;
			break;
		case V4L2_MBUS_FMT_SGBRG10_1X10:
			isp_ctl |= ISP2_CTL_MODE_RGB10
				| ISP2_CTL_COLOR_SEQ_SGBRG
				| ISP2_CTL_RAW_COLOR_SEQ_SGBRG;
			break;
		case V4L2_MBUS_FMT_SBGGR10_1X10:
			isp_ctl |= ISP2_CTL_MODE_RGB10
				| ISP2_CTL_COLOR_SEQ_SBGGR
				| ISP2_CTL_RAW_COLOR_SEQ_SBGGR;
			break;
		default:
			isp_err("input data error (pixel code:0x%x)\n", code);
			break;

		}
	    isp_reg_write(isp_ctl, ISP_ENABLE);

	}
	return 0;
}

static int isp_set_output_fmt(struct soc_camera_device *icd, u32 fourcc)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	unsigned int isp_out_fmt = isp_reg_read(YUV_OUT_FMT);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
		isp_out_fmt = isp_reg_read(ISP_OUT_FMT);

	isp_out_fmt &= ~(ISP_OUT_FMT_MASK | ISP_OUT_FMT_SEMI_UV_INV);

	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:    /*420 planar*/
		isp_out_fmt |= ISP_OUT_FMT_YUV420;
		break;
	case V4L2_PIX_FMT_YVU420:    /*420 planar YV12*/
		isp_out_fmt |= ISP_OUT_FMT_YUV420;
		break;
	case V4L2_PIX_FMT_YUV422P:   /*422 semi planar*/
		isp_out_fmt |= ISP_OUT_FMT_YUV422P;
		break;
	case V4L2_PIX_FMT_NV12:      /*420 semi-planar*/
		isp_out_fmt |= ISP_OUT_FMT_NV12;
		break;
	case V4L2_PIX_FMT_NV21:      /*420 semi-planar*/
		isp_out_fmt |= ISP_OUT_FMT_NV12 | ISP_OUT_FMT_SEMI_UV_INV;
		break;
	case V4L2_PIX_FMT_YUYV:     /*interleaved*/
		isp_out_fmt |= ISP_OUT_FMT_YUYV;
		break;
	default:   /* Raw RGB */
		isp_err("set isp output format failed, fourcc = 0x%x\n",
			fourcc);
		return -EINVAL;
	}

	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
		isp_reg_write(isp_out_fmt, ISP_OUT_FMT);
	else
		isp_reg_write(isp_out_fmt, YUV_OUT_FMT);

	return 0;
}

static void isp_set_frame2(struct soc_camera_device *icd/*struct s900_camera_dev *cam_dev*/,
	struct videobuf_buffer *vb)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	u32 fourcc = icd->current_fmt->host_fmt->fourcc;
	phys_addr_t isp_addr;
	phys_addr_t isp_addr_y;
	phys_addr_t isp_addr_u;
	phys_addr_t isp_addr_v;
	phys_addr_t isp_addr_uv;

	if (NULL == vb) {
		isp_err("cannot get video buffer.\n");
	    return;
	}

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		isp_addr = videobuf_to_dma_contig(vb);
		break;
	case V4L2_MEMORY_USERPTR:
		isp_addr = vb->baddr;
		break;
	default:
		isp_err("isp_set_frame2 wrong memory type. %d,%p\n",
			vb->memory, (void *)vb->baddr);
		return;
	}

	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:       /*420 planar*/

		isp_addr_y = ALIGN(isp_addr, 2);
		isp_addr_u = ALIGN(isp_addr_y
			+ icd->user_width * icd->user_height, 2);
		isp_addr_v = ALIGN(isp_addr_u
			+ icd->user_width * icd->user_height / 4, 2);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			if (cam_param->b_splited_capture == 1) {
				unsigned int out_fmt = isp_reg_read(ISP_OUT_FMT);
				if ((cam_param->rb_cnt & 0x1) == 1) {
					isp_addr_y += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut));
					isp_addr_u += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
					isp_addr_v += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt
						| ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				} else {
					out_fmt = isp_reg_read(ISP_OUT_FMT);
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt | ((cam_param->rb_lcol_stride + cam_param->rb_lright_cut) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				}
				isp_set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

				cam_param->rb_cnt++;
				if (cam_param->rb_cnt >= 2)
					cam_param->rb_cnt = 0;
			}

			isp_reg_write(isp_addr_y, ISP_OUT_ADDRY);
			isp_reg_write(isp_addr_u, ISP_OUT_ADDRU);
			isp_reg_write(isp_addr_v, ISP_OUT_ADDRV);
		} else {
			isp_reg_write(isp_addr_y, YUV_OUT_ADDRY);
			isp_reg_write(isp_addr_u, YUV_OUT_ADDRU);
			isp_reg_write(isp_addr_v, YUV_OUT_ADDRV);
		}
		break;

	case V4L2_PIX_FMT_YVU420:       /*420 planar YV12*/

		isp_addr_y = ALIGN(isp_addr, 2);
		isp_addr_u = ALIGN(isp_addr_y + icd->user_width * icd->user_height, 2);
		isp_addr_v = ALIGN(isp_addr_u + icd->user_width * icd->user_height / 4, 2);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			if (cam_param->b_splited_capture == 1) {
				unsigned int out_fmt = isp_reg_read(ISP_OUT_FMT);
				if ((cam_param->rb_cnt & 0x1) == 1) {
					isp_addr_y += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut));
					isp_addr_u += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
					isp_addr_v += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt | ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				} else {
					out_fmt = isp_reg_read(ISP_OUT_FMT);
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt | ((cam_param->rb_lcol_stride + cam_param->rb_lright_cut) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				}
				isp_set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

				cam_param->rb_cnt++;
				if (cam_param->rb_cnt >= 2)
					cam_param->rb_cnt = 0;
			}

			isp_reg_write(isp_addr_y, ISP_OUT_ADDRY);
			isp_reg_write(isp_addr_v, ISP_OUT_ADDRU);
			isp_reg_write(isp_addr_u, ISP_OUT_ADDRV);
		} else {
			isp_reg_write(isp_addr_y, YUV_OUT_ADDRY);
			isp_reg_write(isp_addr_v, YUV_OUT_ADDRU);
			isp_reg_write(isp_addr_u, YUV_OUT_ADDRV);
		}
		break;


	case V4L2_PIX_FMT_YUV422P:      /*422 semi planar*/

		isp_addr_y = ALIGN(isp_addr, 2);
		isp_addr_uv = ALIGN(isp_addr_y + icd->user_width * icd->user_height, 2);

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			if (cam_param->b_splited_capture == 1) {
				if (cam_param->b_splited_capture == 1) {
					unsigned int out_fmt = isp_reg_read(ISP_OUT_FMT);
					if ((cam_param->rb_cnt & 0x1) == 1) {
						isp_addr_y += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut));
						isp_addr_u += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
						isp_addr_v += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut)) / 2;
						out_fmt = out_fmt & (~(0x1FFF << 16));
						out_fmt = out_fmt | ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut) << 16);
						isp_reg_write(out_fmt, ISP_OUT_FMT);
					} else {
						out_fmt = isp_reg_read(ISP_OUT_FMT);
						out_fmt = out_fmt & (~(0x1FFF << 16));
						out_fmt = out_fmt | ((cam_param->rb_lcol_stride + cam_param->rb_lright_cut) << 16);
						isp_reg_write(out_fmt, ISP_OUT_FMT);
					}
					isp_set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

					cam_param->rb_cnt++;
					if (cam_param->rb_cnt >= 2)
						cam_param->rb_cnt = 0;
				}
				cam_param->rb_cnt++;
				if (cam_param->rb_cnt >= 2)
					cam_param->rb_cnt = 0;
				}

			isp_reg_write(isp_addr_y, ISP_OUT_ADDRY);
			isp_reg_write(isp_addr_uv, ISP_OUT_ADDRU);
		} else {
			isp_reg_write(isp_addr_y, YUV_OUT_ADDRY);
			isp_reg_write(isp_addr_uv, YUV_OUT_ADDRU);
		}
		break;


	case V4L2_PIX_FMT_NV12:         /*420 semi-planar*/
	case V4L2_PIX_FMT_NV21:         /*420 semi-planar*/

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			phys_addr_t nIsp_Addr_Y = ALIGN(isp_addr, 2);
			unsigned int out_fmt = isp_reg_read(ISP_OUT_FMT);
			/*isp_addr_y = ALIGN(isp_addr, 2);
			isp_addr_uv = ALIGN(isp_addr_y + icd->user_width * icd->user_height, 2);*/

			isp_addr_y = nIsp_Addr_Y + OFFSET_OF_ALIGN * (icd->user_width + OFFSET_OF_ALIGN) + OFFSET_OF_ALIGN;
			isp_addr_uv = nIsp_Addr_Y + (OFFSET_OF_ALIGN + icd->user_height) * (icd->user_width + OFFSET_OF_ALIGN) + \
				OFFSET_OF_ALIGN / 2 * (icd->user_width + OFFSET_OF_ALIGN) + OFFSET_OF_ALIGN;

			if (cam_param->b_splited_capture == 1) {
				if ((cam_param->rb_cnt & 0x1) == 1) {
					isp_addr_y += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut));
					isp_addr_uv += ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut));
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt | ((cam_param->rb_rcol_stride + cam_param->rb_rleft_cut + OFFSET_OF_ALIGN) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				} else {
					out_fmt = isp_reg_read(ISP_OUT_FMT);
					out_fmt = out_fmt & (~(0x1FFF << 16));
					out_fmt = out_fmt | ((cam_param->rb_lcol_stride + cam_param->rb_lright_cut + OFFSET_OF_ALIGN) << 16);
					isp_reg_write(out_fmt, ISP_OUT_FMT);
				}
				isp_set_rb_size(cam_param, (cam_param->rb_cnt & 0x1));

				cam_param->rb_cnt++;
				if (cam_param->rb_cnt >= 2)
					cam_param->rb_cnt = 0;
			} else {
				out_fmt = out_fmt & (~(0x1FFF << 16));
				out_fmt = out_fmt | (OFFSET_OF_ALIGN << 16);
				isp_reg_write(out_fmt, ISP_OUT_FMT);
			}

			isp_reg_write(isp_addr_y, ISP_OUT_ADDRY);
			isp_reg_write(isp_addr_uv, ISP_OUT_ADDRU);
		} else {
			isp_addr_y = ALIGN(isp_addr, 2);
			isp_addr_uv = ALIGN(isp_addr_y + icd->user_width * icd->user_height, 2);
			isp_reg_write(isp_addr_y, YUV_OUT_ADDRY);
			isp_reg_write(isp_addr_uv, YUV_OUT_ADDRU);
		}
		break;


	case V4L2_PIX_FMT_YUYV:  /*interleaved*/

		isp_addr_y = ALIGN(isp_addr, 2);

		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			isp_reg_write(isp_addr_y, ISP_OUT_ADDRY);
		else
			isp_reg_write(isp_addr_y, YUV_OUT_ADDRY);
		break;

	default:       /* Raw RGB */
		isp_err("Set isp output format failed, fourcc = 0x%x\n",
			fourcc);
		return;
	}
}

static int isp_set_signal_polarity(struct soc_camera_device *icd,
	unsigned int common_flags)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	u32 isp_ctl, isp2_ctl;

	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_ctl = isp_reg_read(ISP_ENABLE);
		isp_ctl &= (~(1 << 9)); /*edge clk trigger*/

		if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			isp_ctl &= ~ISP_CTL_HSYNC_ACTIVE_HIGH;
		else
			isp_ctl |= ISP_CTL_HSYNC_ACTIVE_HIGH;

		if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			isp_ctl &= ~ISP_CTL_VSYNC_ACTIVE_HIGH;
		else
			isp_ctl |= ISP_CTL_VSYNC_ACTIVE_HIGH;

		isp_info("isp0 polarity enable %x\n", isp_ctl);
		isp_reg_write(isp_ctl, ISP_ENABLE);
	} else {
		isp2_ctl = isp_reg_read(ISP_ENABLE);
		isp_ctl &= (~(1 << 16));

		if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			isp2_ctl &= ~ISP2_CTL_HSYNC_ACTIVE_HIGH;
		else
			isp2_ctl |= ISP2_CTL_HSYNC_ACTIVE_HIGH;

		if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			isp2_ctl &= ~ISP2_CTL_VSYNC_ACTIVE_HIGH;
		else
			isp2_ctl |= ISP2_CTL_VSYNC_ACTIVE_HIGH;

		isp_info("isp1 polarity enable %x\n", isp2_ctl);
		isp_reg_write(isp2_ctl, ISP_ENABLE);
	}

	return 0;
}

static void isp_capture_start(struct s900_camera_dev *cam_dev,
	struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	isp_setting_t *isp_setting = module_info->isp_cfg;

	unsigned int isp_enable, isp_int_stat, isp_ctl;
	unsigned int ch1_intf = 0;
	unsigned int isp_mode = 0;

	if (cam_param->started == DEV_START) {
		isp_info("already start\n");
		return;
	}
	if (V4L2_MBUS_CSI2 == cam_param->bus_type)
		mipi_csi_init(icd);

	isp_set_preline(icd);

	isp_ctl = isp_reg_read(ISP_ENABLE);
	if (cam_param->b_splited_capture == 1)
		isp_ctl = isp_ctl | (isp_mode);

	isp_ctl &= (~(3 << 1));
	if (V4L2_MBUS_PARALLEL == cam_param->bus_type) {
		if (cam_param->channel == ISP_CHANNEL_0)
			ch1_intf = ISP_CTL_CHANNEL1_INTF_PARAL;
		else
			ch1_intf = ISP2_CTL_CHANNEL_INTF_PARAL;

		isp_ctl &= ~ch1_intf;
	} else {
		if (cam_param->channel == ISP_CHANNEL_0) {
			ch1_intf = ISP_CTL_CHANNEL1_INTF_MIPI;
			isp_ctl &= ~ISP_CTL_CHANNEL1_INTF_MIPI;
		} else {
			ch1_intf = ISP2_CTL_CHANNEL_INTF_MIPI;
			isp_ctl &= ~ISP2_CTL_CHANNEL_INTF_MIPI;
		}
	}
	isp_ctl = isp_ctl | (ch1_intf);
	isp_reg_write(isp_ctl, ISP_ENABLE);

	isp_enable = isp_reg_read(ISP_ENABLE);
	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_int_stat = isp_reg_read(ISP_INT_STAT);
		isp_int_stat |= ISP_INT_STAT_CH1_PL_INT_EN;
		if (cam_param->b_splited_capture == 1) {
			isp_int_stat |= ISP_INT_STAT_RAW_RB_PL_INT_EN;
			isp_int_stat |= ISP_INT_STAT_RAWSTORE_FRAME_END_EN;
		}

		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW) {
			isp_int_stat |= ISP_INT_STAT_REFORM_FRAME_END_EN;
			isp_enable |= (1 << 5);
		} else {
			if (cam_param->b_splited_capture == 0)
				isp_enable |= 1;
		}

		if ((cam_param->b_raw_store_status == 0x1)
			|| (cam_param->b_raw_store_status == 0x2))
			isp_enable |= (1 << 4);

		isp_int_stat |= ISP_INT_STAT_FRAME_END_INT_EN;
		isp_reg_write(isp_int_stat, ISP_INT_STAT);

		/* clk_edge: */
		isp_enable = (isp_enable & (~(1 << 9)))
		| (isp_setting->clk_edge << 9);
		/* vsync: */
		isp_enable = (isp_enable & (~(1 << 10)))
		| (isp_setting->vs_pol << 10);
		/* hsync: */
		isp_enable = (isp_enable & (~(1 << 11)))
		| (isp_setting->hs_pol << 11);
		/*color seq*/


		/*
			channel mux setting:
			0: ch1 isp, ch2 yuv reform
			1:ch1 yuv reform, ch2 isp
		*/
		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW)
			isp_enable = isp_enable | (1<<6);
		else
			isp_enable = isp_enable & ~(1<<6);

		isp_reg_write(isp_enable, ISP_ENABLE);

	} else {
		isp_int_stat = isp_reg_read(ISP_INT_STAT);
		isp_int_stat |= ISP_INT_STAT_CH2_PL_INT_EN;
		if (cam_param->b_splited_capture == 1) {
			isp_int_stat &= (~1);
			isp_int_stat |= ISP_INT_STAT_RAW_RB_PL_INT_EN;
			isp_int_stat |= ISP_INT_STAT_RAWSTORE_FRAME_END_EN;
		}

		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW) {
			isp_int_stat |= ISP_INT_STAT_REFORM_FRAME_END_EN;
			isp_enable |= (1 << 5);
		} else {
			isp_enable |= 1;
		}

		if ((cam_param->b_raw_store_status == 0x1)
			|| (cam_param->b_raw_store_status == 0x2))
			isp_enable |= (1 << 4);

		isp_int_stat |= ISP_INT_STAT_FRAME_END_INT_EN;
		isp_reg_write(isp_int_stat, ISP_INT_STAT);


		/* clk_edge: */
		isp_enable = (isp_enable & (~(1 << 16)))
		| (isp_setting->clk_edge << 16);
		/* vsync: */
		isp_enable = (isp_enable & (~(1 << 17)))
		| (isp_setting->vs_pol << 17);
		/* hsync: */
		isp_enable = (isp_enable & (~(1 << 18)))
		| (isp_setting->hs_pol << 18);
		/*color seq*/

		/*
			channel mux setting:
			0: ch1 isp, ch2 yuv reform
			1:ch1 yuv reform, ch2 isp
		*/
		if (cam_param->data_type != SENSOR_DATA_TYPE_RAW)
			isp_enable = isp_enable & ~(1<<6);
		else
			isp_enable = isp_enable | (1<<6);

		isp_reg_write(isp_enable, ISP_ENABLE);
	}
	isp_reg_write(isp_reg_read(ISP_INT_STAT), ISP_INT_STAT);
	cam_param->started = DEV_START;

	isp_info("%s: chnnel-%d, %s\n", __func__, cam_param->channel,
		V4L2_MBUS_PARALLEL == cam_param->bus_type ? "dvp" : "mipi");

	if (ISP_CHANNEL_0 == cam_param->channel) {
		isp_reg_write(0, VTD_CTL);
		isp_reg_write(1, VTD_CTL);
	} else
		isp_reg_write(3, VTD_CTL);

	return;
}

static void clear_isr(struct s900_camera_param *cam_param)
{
	unsigned int int_stat;
	unsigned int m_isr_init = 0;
	int_stat = isp_reg_read(ISP_INT_STAT);

	if (ISP_CHANNEL_0 == cam_param->channel) {
		m_isr_init |= (1 << 30) | (3 << 26) | (1 << 24) | (1 << 21);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= (1 << 29)
				| (1 << 23)
				| (7 << 18)
				| (1 << 16)
				| (1 << 12);
		} else {
			m_isr_init |= (1 << 17) | (1 << 13);
		}
		isp_info("isp_capture_stop %x,%x\n", int_stat, m_isr_init);
		int_stat |= (m_isr_init);

		isp_reg_write(int_stat, ISP_INT_STAT);
	} else {
		m_isr_init |= (1 << 31) | (3 << 27) | (1 << 25) | (1 << 22);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= (1 << 29)
				| (1 << 23)
				| (7 << 18)
				| (1 << 16)
				| (1 << 12);
		} else {
			m_isr_init |= (1 << 17) | (1 << 13);
		}
		isp_info("isp_capture_stop %x,%x\n", int_stat, m_isr_init);
		int_stat |= (m_isr_init);
		isp_reg_write(int_stat, ISP_INT_STAT);
	}
}

static void isp_capture_stop(struct s900_camera_dev *cam_dev, struct soc_camera_device *icd)
{
	unsigned int isp_enable;
	struct s900_camera_param *cam_param = icd->host_priv;
	unsigned long flags = 0;
	unsigned int int_stat = 0;
	unsigned int m_isr_init = 0;
	unsigned int count = 0;

	if (cam_param->started == DEV_STOP) {
		isp_info("already stop\n");
		clear_isr(cam_param);
		return;
	}

	while (cam_param->rb_cnt & 0x1) {
		mdelay(5);
		count++;
		if (count > 10)
			break;
		isp_info("stop waiting %d\n", cam_param->rb_cnt);
	}
	spin_lock_irqsave(&cam_dev->lock, flags);

	isp_enable = isp_reg_read(ISP_ENABLE);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
		isp_reg_write(isp_enable & (~(ISP_ENABLE_EN | (1 << 4))), ISP_ENABLE);
	else
		isp_reg_write(isp_enable & (~(1 << 5)), ISP_ENABLE);

	/* enable frame end interrupt to check that frame tx already finish*/
	int_stat = isp_reg_read(ISP_INT_STAT);

	if (ISP_CHANNEL_0 == cam_param->channel) {
		m_isr_init |= ISP_INT_STAT_CH1_PL_INT_EN;
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= ISP_INT_STAT_FRAME_END_INT_EN
				| ISP_INT_STAT_RAWSTORE_FRAME_END_EN;
		} else {
			m_isr_init |= ISP_INT_STAT_REFORM_FRAME_END_EN;
		}

		int_stat &= (~m_isr_init);

		isp_reg_write(int_stat, ISP_INT_STAT);
	} else {
		m_isr_init |= (ISP_INT_STAT_CH2_PL_INT_EN);
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
			m_isr_init |= ISP_INT_STAT_FRAME_END_INT_EN
				| ISP_INT_STAT_RAWSTORE_FRAME_END_EN;
		} else {
			m_isr_init |= ISP_INT_STAT_REFORM_FRAME_END_EN;
		}
		int_stat &= (~m_isr_init);
		isp_reg_write(int_stat, ISP_INT_STAT);
	}

	cam_param->started = DEV_STOP;
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	/*
	ret = wait_for_completion_timeout(&cam_param->wait_stop, msecs_to_jiffies(ISP_FRAME_INTVL_MSEC));
	if (ret <= 0) {
	    isp_info("%s wake up before frame complete\n", __func__);
	} else {
	    isp_info("%s remain %dms to wait for stop,%x\n", __func__, jiffies_to_msecs(ret), isp_reg_read(ISP_INT_STAT));
	}
	*/
		mdelay(50);
	if (V4L2_MBUS_CSI2 == cam_param->bus_type)
		mipi_csi_disable(icd);

	cam_param->cur_frm = NULL;
	cam_param->prev_frm = NULL;
	return;
}

static inline int get_bytes_per_line(struct soc_camera_device *icd)
{
	return soc_mbus_bytes_per_line(icd->user_width, icd->current_fmt->host_fmt);
}

static inline int get_frame_size(struct soc_camera_device *icd)
{
	int bytes_per_line = get_bytes_per_line(icd);
	return icd->user_height * bytes_per_line;
}

static irqreturn_t s900_camera_host_isp_isr(int irq, void *data)
{
	struct s900_camera_dev *cam_dev = data;
	struct soc_camera_device *icd = NULL;
	struct s900_camera_param *cam_param = NULL;
	struct v4l2_subdev *sd = NULL;
	struct videobuf_buffer *vb;
	unsigned int isp_int_stat = 0;
	unsigned long flags;
	unsigned int preline_int_pd, prelien_rs_pd;
	unsigned int isp_bk_int_stat = 0;
	unsigned int temp_data = 0;
	unsigned int nw = 0;
	unsigned int nh = 0;
	int i;

	spin_lock_irqsave(&cam_dev->lock, flags);

	for (i = 0; i < 2; i++) {
		if (cam_dev->icds[i]) {
			icd = cam_dev->icds[i];
			cam_param = icd->host_priv;
			sd = soc_camera_to_subdev(icd);
			isp_int_stat = isp_reg_read(ISP_INT_STAT);
			isp_bk_int_stat = isp_int_stat;
			temp_data = isp_reg_read(VTD_CTL);
			nw = (temp_data >> 3) & 0x1fff;
			nh = (temp_data >> 16) & 0xffff;

			if (ISP_CHANNEL_0 == cam_param->channel)
				preline_int_pd = ISP_INT_STAT_CH1_PL_PD;
			else
				preline_int_pd = ISP_INT_STAT_CH2_PL_PD;

			if ((cam_param->skip_frames) && (isp_int_stat & preline_int_pd)) {
				cam_param->skip_frames--;
				isp_reg_write(isp_int_stat, ISP_INT_STAT);
				goto out;
			 }
			if ((isp_int_stat & 0xff000000)) {
				isp_err("isr warning!overflow(%x)!\n", isp_int_stat);
				isp_reg_write(isp_int_stat, ISP_INT_STAT);
				goto out;
			}

			if (nw != icd->user_width || nh != icd->user_height) {
				printk("isr warning!vtd(%d,%d)!= (%d,%d),skip....\n", nw, nh, icd->user_width, icd->user_height);
				isp_reg_write(isp_int_stat, ISP_INT_STAT);
				goto out;
			}
			if (cam_param->b_raw_store_status >= 2 && cam_param->b_raw_store_status < (2 + ISP_RAWSTORE_FRAMES_NUM)) {
				if (ISP_CHANNEL_0 == cam_param->channel)
					preline_int_pd =  ISP_INT_STAT_CH1_PL_PD;
				else
					preline_int_pd =  ISP_INT_STAT_CH2_PL_PD;

				isp_reg_write(isp_bk_int_stat, ISP_INT_STAT);

				if (isp_int_stat & preline_int_pd) {
					cam_param->b_raw_store_status++;
					updata_bisp_rawinfo(sd, cam_param);
				}
				if (cam_param->b_raw_store_status >= (2 + ISP_RAWSTORE_FRAMES_NUM)) {
					int nisp_enable = isp_reg_read(ISP_ENABLE);
					nisp_enable &= (~(1 << 4));
					if (cam_param->b_splited_capture == 1) {
						nisp_enable |= 1;
						nisp_enable |= (1 << 1);
					}
					cam_param->rb_cnt = 0;
					updata_bisp_info(sd, cam_param);
					cam_param->rb_cnt = 1;
					isp_reg_write(nisp_enable, ISP_ENABLE);
				}
		    } else {
				if (cam_param->b_splited_capture == 1) {
					if (ISP_CHANNEL_0 == cam_param->channel)
						prelien_rs_pd = ISP_INT_STAT_CH1_PL_PD;
					else
						prelien_rs_pd = ISP_INT_STAT_CH2_PL_PD;

					isp_reg_write(isp_bk_int_stat, ISP_INT_STAT);

					if (ISP_CHANNEL_0 == cam_param->channel)
						preline_int_pd = ISP_INT_STAT_RB_PL_PD;
					else
						preline_int_pd = ISP_INT_STAT_RB_PL_PD;

					if (cam_param->started == DEV_STOP) {
						isp_info("found stop in isp_isr %x,%x\n",
							isp_reg_read(ISP_INT_STAT),
							isp_reg_read(ISP_ENABLE));
						complete(&cam_param->wait_stop);
						if (i == 0)
							continue;
						else
							goto out;
					}

					if (isp_int_stat & preline_int_pd) {
						/* send out a packet already recevied all data*/
						if ((cam_param->rb_cnt & 0x1) == 0) {
							if (cam_param->prev_frm != NULL) {
							    vb = cam_param->prev_frm;
							    vb->state = VIDEOBUF_DONE;
							    do_gettimeofday(&vb->ts);
							    vb->field_count++;
							    wake_up(&vb->done);
						    }

							if (!list_empty(&cam_param->capture)) {
								cam_param->prev_frm = cam_param->cur_frm;
								cam_param->cur_frm = list_entry(cam_param->capture.next,
									struct videobuf_buffer, queue);
								list_del_init(&cam_param->cur_frm->queue);
								updata_bisp_info(sd, cam_param);
								isp_set_frame2(icd, cam_param->cur_frm);
								cam_param->cur_frm->state = VIDEOBUF_ACTIVE;
							} else {
								cam_param->prev_frm = NULL;
								updata_bisp_info(sd, cam_param);
								isp_set_frame2(icd, cam_param->cur_frm);
							}
					    } else {
							updata_bisp_info(sd, cam_param);
							isp_set_frame2(icd, cam_param->cur_frm);
					    }
					}
				} else {
					/* isp_capture_stop function set it, clear it here anyway.*/
					isp_reg_write(isp_bk_int_stat, ISP_INT_STAT);

					if (ISP_CHANNEL_0 == cam_param->channel)
						preline_int_pd = ISP_INT_STAT_CH1_PL_PD;
					else
						preline_int_pd = ISP_INT_STAT_CH2_PL_PD;

					if (cam_param->started == DEV_STOP) {
						isp_info("found stop in isp_isr %x,%x\n",
							isp_reg_read(ISP_INT_STAT),
							isp_reg_read(ISP_ENABLE));
						complete(&cam_param->wait_stop);
						if (i == 0)
							continue;
						else
							goto out;
					}

					if (isp_int_stat & preline_int_pd) {
						/* send out a packet already recevied all data*/
						if (cam_param->prev_frm != NULL) {
						    vb = cam_param->prev_frm;
						    vb->state = VIDEOBUF_DONE;
						    do_gettimeofday(&vb->ts);
						    vb->field_count++;
						    wake_up(&vb->done);
						}

						if (!list_empty(&cam_param->capture)) {
							cam_param->prev_frm = cam_param->cur_frm;
							cam_param->cur_frm = list_entry(cam_param->capture.next,
								struct videobuf_buffer, queue);
							list_del_init(&cam_param->cur_frm->queue);

							updata_bisp_info(sd, cam_param);
							isp_set_frame2(icd, cam_param->cur_frm);
							cam_param->cur_frm->state = VIDEOBUF_ACTIVE;
							BUG_ON(cam_param->prev_frm == cam_param->cur_frm);

						} else {
							cam_param->prev_frm = NULL;
							updata_bisp_info(sd, cam_param);
							isp_set_frame2(icd, cam_param->cur_frm);
						}
					}
				}
			}

		}
	}

out:
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	return IRQ_HANDLED;
}

static int get_supported_mbus_param(struct s900_camera_param *cam_param)
{
	int flags = 0;
	/*
	 * S900  camera interface only works in master mode
	 * and only support 8bits currently
	 */
	if (V4L2_MBUS_PARALLEL == cam_param->bus_type) {
		flags = V4L2_MBUS_MASTER
			| V4L2_MBUS_HSYNC_ACTIVE_HIGH
			| V4L2_MBUS_HSYNC_ACTIVE_LOW
			| V4L2_MBUS_VSYNC_ACTIVE_HIGH
			| V4L2_MBUS_VSYNC_ACTIVE_LOW
			| V4L2_MBUS_PCLK_SAMPLE_RISING
			| V4L2_MBUS_PCLK_SAMPLE_FALLING
			| V4L2_MBUS_DATA_ACTIVE_HIGH
			| SOCAM_DATAWIDTH_8;

	} else if (V4L2_MBUS_CSI2 == cam_param->bus_type) {
		flags = V4L2_MBUS_CSI2_CHANNEL_0
			| V4L2_MBUS_CSI2_CHANNEL_1
			| V4L2_MBUS_CSI2_CONTINUOUS_CLOCK
			| V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK
			| V4L2_MBUS_CSI2_LANES;
	} else {
	    isp_err("bus_type is not supported\n");
	}

	return flags;
}

static int select_dvp_mbus_param(struct s900_camera_dev *cdev,
	unsigned int common_flags)
{
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		&& (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & S900_CAMERA_HSYNC_HIGH)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		&& (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & S900_CAMERA_VSYNC_HIGH)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_DATA_ACTIVE_HIGH)
		&& (common_flags & V4L2_MBUS_DATA_ACTIVE_LOW)) {
		if (cdev->dvp_mbus_flags & S900_CAMERA_DATA_HIGH)
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		&& (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (cdev->dvp_mbus_flags & S900_CAMERA_PCLK_RISING)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	return common_flags;
}

static int select_mipi_mbus_param(struct s900_camera_param *param,
	unsigned int common_flags)
{

	/* correspond to cis  CSI_CTRL_LANE_NUM's defined */
	if (common_flags & V4L2_MBUS_CSI2_1_LANE)
		param->lane_num = 0;
	else if (common_flags & V4L2_MBUS_CSI2_2_LANE)
		param->lane_num = 1;
	else if (common_flags & V4L2_MBUS_CSI2_3_LANE)
		param->lane_num = 2;
	else
		param->lane_num = 3;
	/* determined by CSI, ignore pclk polarity*/
	return V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_HIGH;
}

static int s900_camera_set_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_mbus_config cfg;

	unsigned int bus_flags;
	unsigned int common_flags;
	int ret;

	bus_flags = get_supported_mbus_param(cam_param);

	v4l2_subdev_call(sd, video, g_mbus_config, &cfg);

	common_flags = soc_mbus_config_compatible(&cfg, bus_flags);
	if (!common_flags) {
		isp_err("flags incompatible: host 0x%x, sensor 0x%x\n",
			bus_flags, cfg.flags);
		return -EINVAL;
	}

	if (V4L2_MBUS_PARALLEL == cam_param->bus_type)
		common_flags = select_dvp_mbus_param(cam_dev, common_flags);
	else
		common_flags = select_mipi_mbus_param(cam_param, common_flags);

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		isp_err("camera s_mbus_config(0x%x) returned %d\n",
			common_flags, ret);
		return ret;
	}

	isp_set_signal_polarity(icd, common_flags);
	isp_set_input_fmt(icd, cam_param->code);

	return 0;
}

static int check_mbus_param(struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;

	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	unsigned long bus_flags, common_flags;
	int ret;

	bus_flags = get_supported_mbus_param(cam_param);

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);

	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, bus_flags);
		if (!common_flags) {
			isp_err("flags incompatible: sensor[0x%x], host[0x%lx]\n",
				cfg.flags, bus_flags);
			return -EINVAL;
		}
	} else if (ret == -ENOIOCTLCMD) {
		ret = 0;
	}

	return ret;
}

static const struct soc_mbus_pixelfmt s900_camera_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.name = "YUV 4:2:0 planar 12 bit",
		.bits_per_sample = 12,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVU420,
		.name = "YVU 4:2:0 planar 12 bit",
		.bits_per_sample = 12,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.name = "YUV 4:2:2 planar 16 bit",
		.bits_per_sample = 16,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.name = "YUYV 4:2:2 interleaved 16bit",
		.bits_per_sample = 16,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.name = "YUV 4:2:0 semi-planar 12 bit",
		.bits_per_sample = 12,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.name = "YUV 4:2:0 semi-planar 12 bit",
		.bits_per_sample = 12,
		.packing = SOC_MBUS_PACKING_NONE,
		.order = SOC_MBUS_ORDER_LE,
	},
};

static int client_g_rect(struct v4l2_subdev *sd, struct v4l2_rect *rect)
{
	struct v4l2_crop crop;
	struct v4l2_cropcap cap;
	int ret;

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, g_crop, &crop);
	if (!ret) {
		*rect = crop.c;
		return ret;
	}

	/* Camera driver doesn't support .g_crop(), assume default rectangle */
	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (!ret)
		*rect = cap.defrect;

	return ret;
}

/*
 * The common for both scaling and cropping iterative approach is:
 * 1. try if the client can produce exactly what requested by the user
 * 2. if (1) failed, try to double the client image until we get one big enough
 * 3. if (2) failed, try to request the maximum image
 */
static int client_s_crop(struct soc_camera_device *icd, const struct v4l2_crop *crop,
struct v4l2_crop *cam_crop)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_rect *rect = (struct v4l2_rect *)&crop->c;
	struct v4l2_rect *cam_rect = &cam_crop->c;
	struct v4l2_cropcap cap;
	int ret;
	unsigned int width, height;

	ret = client_g_rect(sd, cam_rect);
	if (ret < 0) {
		isp_err("get sensor rect failed %d\n", ret);
		return ret;
	}

	/* Try to fix cropping, that camera hasn't managed to set */

	/* We need sensor maximum rectangle */
	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0) {
		isp_err("get sensor cropcap failed %d\n", ret);
		return ret;
	}

	/* Put user requested rectangle within sensor and isp bounds */
	soc_camera_limit_side(&rect->left, &rect->width, cap.bounds.left, 32,
	min(ISP_MAX_WIDTH, cap.bounds.width));
	/* TO FIXUP: must be 32B-aligned if not support stride */
	/*rect->width = rect->width - (rect->width % 32);*/

	soc_camera_limit_side(&rect->top, &rect->height, cap.bounds.top, 1,
	min(ISP_MAX_HEIGHT, cap.bounds.height));

	/*
	 * Popular special case - some cameras can only handle fixed sizes like
	 * QVGA, VGA,... Take care to avoid infinite loop.
	 */
	width = max(cam_rect->width, 2);
	height = max(cam_rect->height, 2);

	if (!ret && (cap.bounds.width >= width || cap.bounds.height >= height)
		&& ((rect->left >= cam_rect->left) && (rect->top >= cam_rect->top)
		&& (rect->left + rect->width <= cam_rect->left + cam_rect->width)
		&& (rect->top + rect->height <= cam_rect->top + cam_rect->height)))
		return 0;
	else {
		isp_err("crop rect must be within sensor rect.\n");
		return -ERANGE;
	}
}

extern void bisp_get_info(int chid, int cmd, void *args);
extern void bisp_updata_stat(int isp_ch, int bstat);
extern void bisp_updata_rawstore(int isp_ch, int brs_en);
extern void bisp_updata_readback(int isp_ch, int brb_mode,
			 int bsplit_next, int rb_w);
static int ext_cmd(struct v4l2_subdev *sd, int cmd, void *args)
{
	int ret = 0;
	struct s900_camera_param *cam_param = (struct s900_camera_param *)args;

	switch (cmd) {
	case V4L2_CID_BISP_UPDATE:
		bisp_updata_stat(cam_param->channel, 1);
		if (cam_param->b_splited_capture) {
			bisp_updata_readback(cam_param->channel, cam_param->b_splited_capture,
			(cam_param->rb_cnt & 0x1), cam_param->rb_rcol_stride);
		}
		break;

	case V4L2_CID_BISP_UPDATERAW:
		bisp_updata_rawstore(cam_param->channel, 1);
		break;

	case V4L2_CID_BISP_GETINFO:
		bisp_get_info(cam_param->channel, 0x100,
			&cam_param->b_raw_store_status);
		break;

	default:
		isp_err("getctrl invalid control id %d\n", cmd);
		return -EINVAL;
	}
	return ret;
}

static int parse_sensor_flags(struct s900_camera_param *param,
	unsigned long flags)
{
	if (flags & SENSOR_FLAG_CH_MASK)
		param->channel = ISP_CHANNEL_0;
	else
		param->channel = ISP_CHANNEL_1;

	if (flags & SENSOR_FLAG_INTF_MASK) {
		param->ext_cmd = NULL;
		param->bus_type = V4L2_MBUS_PARALLEL;
	} else {
		param->ext_cmd = ext_cmd;
		param->bus_type = V4L2_MBUS_CSI2;
	}

	if (flags & SENSOR_FLAG_DATA_MASK)
		param->data_type = SENSOR_DATA_TYPE_YUV;
	else
		param->data_type = SENSOR_DATA_TYPE_RAW;

	return 0;
}

static void isp_regs_init(void)
{
	/*

	channel 0 dpc and ee
	isp_reg_write(0xff00e4, BUS_PRIORITY_CTL);*/
	isp_reg_write(0, ISP_CTL);

	isp_reg_write(0xFF01FF01, BA_OFFSET0);
	isp_reg_write(0xFF01FF01, BA_OFFSET1);

	isp_reg_write(0x00000100, ISP_CG_B_GAIN);
	isp_reg_write(0x00000100, ISP_CG_G_GAIN);
	isp_reg_write(0x00000100, ISP_CG_R_GAIN);
	isp_reg_write(0x00000100, ISP_SECOND_CG_B_GAIN);
	isp_reg_write(0x00000100, ISP_SECOND_CG_G_GAIN);
	isp_reg_write(0x00000100, ISP_SECOND_CG_R_GAIN);

	isp_reg_write(0x00000400, ISP_CC_CMA1);
	isp_reg_write(0x00000000, ISP_CC_CMA2);
	isp_reg_write(0x00000400, ISP_CC_CMA3);
	isp_reg_write(0x00000000, ISP_CC_CMA4);
	isp_reg_write(0x00000400, ISP_CC_CMA5);


	/*channel 0 csc setting*/
	isp_reg_write(0x00000010, ISP_CSC_Y_OFFSET);
	isp_reg_write(0x00000080, ISP_CSC_CB_OFFSET);
	isp_reg_write(0x00000080, ISP_CSC_CR_OFFSET);
	isp_reg_write(0x00000042, ISP_CSC_Y_R);
	isp_reg_write(0x00000081, ISP_CSC_Y_G);
	isp_reg_write(0x00000019, ISP_CSC_Y_B);
	isp_reg_write(0x000007DA, ISP_CSC_CB_R);
	isp_reg_write(0x000007B6, ISP_CSC_CB_G);
	isp_reg_write(0x00000070, ISP_CSC_CB_B);
	isp_reg_write(0x00000070, ISP_CSC_CR_R);
	isp_reg_write(0x000007A2, ISP_CSC_CR_G);
	isp_reg_write(0x000007EE, ISP_CSC_CR_B);
	isp_reg_write(0x00000000, ISP_CSC_CONTROL);
}

/* Called with .video_lock held */
static int s900_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;
	int channel = -1;
	int b_raw = -1;
	void __iomem *dmm_axi_bisp_priority = NULL;
	void __iomem *dmm_axi_normal_priority = NULL;
	int val;

	isp_info("add devices\n");

	if (module_info->flags & SENSOR_FLAG_CH_MASK)
		channel = ISP_CHANNEL_0;
	else
		channel = ISP_CHANNEL_1;

	if (module_info->flags & SENSOR_FLAG_DATA_MASK)
		b_raw = SENSOR_DATA_TYPE_YUV;
	else
		b_raw = SENSOR_DATA_TYPE_RAW;

	if (cam_dev->icds[channel]) {
		isp_err("devices has already exists.\n");
		return -EBUSY;
	}

	/* powergate on */
	pm_runtime_get_sync(ici->v4l2_dev.dev);

	/* Cache current client geometry */
	cam_param = kzalloc(sizeof(*cam_param), GFP_KERNEL);
	if (NULL == cam_param) {
		isp_err("alloc camera_param struct failed\n");
		return -ENOMEM;
	}
	cam_param->width = 0;
	cam_param->height = 0;
	cam_param->isp_left = 0;
	cam_param->isp_top = 0;
	cam_param->flags = module_info->flags;
	cam_param->skip_frames = 0;
	cam_param->data_type = b_raw;
	cam_param->channel = channel;

	icd->host_priv = cam_param;
	parse_sensor_flags(cam_param, module_info->flags);

	INIT_LIST_HEAD(&cam_param->capture);
	init_completion(&cam_param->wait_stop);
	cam_param->cur_frm = NULL;
	cam_param->prev_frm = NULL;
	cam_param->icd = icd;
	cam_dev->icds[channel] = icd;
	cam_param->started = DEV_OPEN;

	camera_clock_enable(cam_dev, icd);

	if (cam_param && (cam_param->data_type == SENSOR_DATA_TYPE_RAW))
		isp_regs_init();


	dmm_axi_bisp_priority = ioremap(DMM_AXI_BISP_PRIORITY, 4);
	dmm_axi_normal_priority = ioremap(DMM_AXI_NORMAL_PRIORITY, 4);
	writel(0x0, dmm_axi_bisp_priority);
	val = readl(dmm_axi_normal_priority);
	val &= ~(1 << 4);
	writel(val, dmm_axi_normal_priority);
	val |= (1 << 4);
	writel(val, dmm_axi_normal_priority);

	return 0;
}

/* Called with .video_lock held */
static void s900_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	unsigned long flags;
	unsigned int  value = 0;
	isp_info("s900_camera_remove_device %d\n",
		cam_param->channel);
	/* make sure active buffer is canceled */

	/*disable clock*/
	camera_clock_disable(cam_dev, icd);
	isp_capture_stop(cam_dev, icd);

	spin_lock_irqsave(&cam_dev->lock, flags);
	cam_param->started = DEV_CLOSE;
	spin_unlock_irqrestore(&cam_dev->lock, flags);
	icd->host_priv = NULL;

	/* powergate off */
	pm_runtime_put_sync(ici->v4l2_dev.dev);

	isp_reg_write(isp_reg_read(ISP_INT_STAT), ISP_INT_STAT);

	cam_dev->icds[cam_param->channel] = NULL;
	kfree(cam_param);

	value = cmu_reg_read(CMU_DEVRST0);
	value &= ~(0x2 << 11);
	cmu_reg_write(value, CMU_DEVRST0);
	msleep(20);
	value |= 0x2 << 11;
	cmu_reg_write(value, CMU_DEVRST0);
	msleep(20);
}

static int s900_camera_get_formats(struct soc_camera_device *icd,
	unsigned int idx, struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct s900_camera_param *cam_param = icd->host_priv;
	int formats = 0, ret, k, n;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;
	struct device *pdev = icd->pdev;
	struct soc_camera_link *module_link = pdev->platform_data;
	struct module_info *module_info = module_link->priv;


	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0) {
		/* No more formats */
		isp_err("No more formats, %s, %d\n", __func__, __LINE__);
		return ret;
	}

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		isp_err("invalid format code #%u: %d\n", idx, code);
		return -EINVAL;
	}

	if (!icd->host_priv) {
		struct v4l2_mbus_framefmt mf;
		struct v4l2_rect rect;

		/* Cache current client geometry */
		ret = client_g_rect(sd, &rect);
		if (ret < 0)
			return ret;

		/* First time */
		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
		if (ret < 0) {
			isp_err("failed to g_mbus_fmt.\n");
			return ret;
		}

		isp_info("isp get sensor's default fmt %ux%u,%x\n",
			mf.width, mf.height, (unsigned int)module_info->flags);

		cam_param = kzalloc(sizeof(*cam_param), GFP_KERNEL);
		if (NULL == cam_param) {
			isp_err("alloc camera_param struct failed\n");
			return -ENOMEM;
		}

		/* ***We are called with current camera crop,
		*****initialise subrect with it *************/
		cam_param->rect = rect;
		cam_param->subrect = rect;
		cam_param->width = mf.width;
		cam_param->height = mf.height;
		cam_param->isp_left = 0;
		cam_param->isp_top = 0;
		cam_param->flags = module_info->flags;
		cam_param->skip_frames = 0;
		icd->host_priv = cam_param;

		parse_sensor_flags(cam_param, module_info->flags);

		/* This also checks support for the requested bits-per-sample */
		ret = check_mbus_param(icd);
		if (ret < 0) {
			kfree(cam_param);
			icd->host_priv = NULL;
			isp_err("no right formats, %s, %d\n",
				__func__, __LINE__);
			return ret;
		}
	} else {
		cam_param = icd->host_priv;
		if ((cam_param->width == 0) && (cam_param->height == 0)) {
			struct v4l2_mbus_framefmt mf;
			struct v4l2_rect rect;

			/* Cache current client geometry */
			ret = client_g_rect(sd, &rect);
			if (ret < 0) {
				isp_err("failed to client_g_rect.\n");
				return ret;
			}

			/* First time */
			ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
			if (ret < 0) {
				isp_err("failed to g_mbus_fmt.\n");
				return ret;
			}

			isp_info("isp get sensor's default fmt %ux%u,%x\n",
				mf.width, mf.height, (unsigned int)module_info->flags);

			/* We are called with current camera crop,**
			*******initialise subrect with it **********/
			cam_param->rect = rect;
			cam_param->subrect = rect;
			cam_param->width = mf.width;
			cam_param->height = mf.height;
			cam_param->isp_left = 0;
			cam_param->isp_top = 0;
			cam_param->flags = module_info->flags;
			cam_param->skip_frames = 0;
			parse_sensor_flags(cam_param, module_info->flags);
			/* This also checks support for the requested bits-per-sample */
			ret = check_mbus_param(icd);
			if (ret < 0) {
				isp_err("no right formats, %s, %d\n", __func__, __LINE__);
				return ret;
			}
		}
	}

	/* Beginning of a pass */
	if (!idx)
		cam_param->extra_fmt = NULL;

		switch (code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:
		case V4L2_MBUS_FMT_VYUY8_2X8:
		case V4L2_MBUS_FMT_YVYU8_2X8:
		case V4L2_MBUS_FMT_YUYV8_2X8:
		case V4L2_MBUS_FMT_SGBRG8_1X8:
		case V4L2_MBUS_FMT_SBGGR8_1X8:
		case V4L2_MBUS_FMT_SRGGB8_1X8:
		case V4L2_MBUS_FMT_SGRBG8_1X8:
		case V4L2_MBUS_FMT_SGBRG10_1X10:
		case V4L2_MBUS_FMT_SBGGR10_1X10:
		case V4L2_MBUS_FMT_SRGGB10_1X10:
		case V4L2_MBUS_FMT_SGRBG10_1X10:
			if (cam_param->extra_fmt)
				break;

			/*
			* Our case is simple so far: for any of the above four camera
			* formats we add all our four synthesized NV* formats, so,
			* just marking the device with a single flag suffices. If
			* the format generation rules are more complex, you would have
			* to actually hang your already added / counted formats onto
			* the host_priv pointer and check whether the format you're
			* going to add now is already there.
			*/
			cam_param->extra_fmt = s900_camera_formats;

			n = ARRAY_SIZE(s900_camera_formats);
			formats += n;
			isp_info("isp provide output format:\n");
			for (k = 0; xlate && k < n; k++) {
				xlate->host_fmt = &s900_camera_formats[k];
				xlate->code = code;
				xlate++;
				isp_info("[%d].code-%#x, %s\n", k, code, s900_camera_formats[k].name);
			}
			break;

		default:
			return 0;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code = code;
		isp_info("xlate->code = %#x\n", xlate->code);
		xlate++;
	}

	return formats;
}

static void s900_camera_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);

	icd->host_priv = NULL;
}

static int check_frame_range(u32 width, u32 height)
{
	/* limit to s900 hardware capabilities */
	return height < 1 || height > ISP_MAX_HEIGHT || width < 32
	       || width > ISP_MAX_WIDTH /* || (width % 32) */;
}

static int s900_camera_cropcap(struct soc_camera_device *icd,
	struct v4l2_cropcap *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_rect *rect = &a->defrect;
	int ret;

	ret = v4l2_subdev_call(sd, video, cropcap, a);
	if (ret < 0) {
		isp_err("failed to get camera cropcap\n");
		return ret;
	}

	/* Put user requested rectangle within sensor and isp bounds */
	if (rect->width > ISP_MAX_WIDTH)
		rect->width = ISP_MAX_WIDTH;

	rect->width = rect->width - (rect->width % 32);

	if (rect->height > ISP_MAX_HEIGHT)
		rect->height = ISP_MAX_HEIGHT;

	return 0;
}

static int raw_store_set(struct soc_camera_device *icd)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	int ret = 0;
	int start = 0;
	int end = 0;

	int nisp_enable = 0;
	int nch_preline;
	int isp_bk_int_stat;

	updata_get_bispinfo(sd, cam_param);
	isp_info("raw store set? %d\n", (int)cam_param->b_raw_store_status);
	if ((cam_param->b_raw_store_status == 0x1) || cam_param->b_raw_store_status == 0x2) {
		if (ISP_CHANNEL_0 == cam_param->channel) {
			start = 0;
			end = cam_param->real_h - 1;
			isp_reg_write(ISP_CH1_ROW_START(0)
				| ISP_CH1_ROW_END(end - start), CH1_ROW_RANGE);
			start = 0;
			end = cam_param->real_w - 1;
			isp_reg_write(ISP_CH1_COL_START(0)
				| ISP_CH1_COL_END(end - start), CH1_COL_RANGE);

			/* waiting for raw-store finished */
			nisp_enable = isp_reg_read(ISP_ENABLE);
			nisp_enable &= 0xfffffffe;    /*disable now*/

			nch_preline = isp_reg_read(CH_PRELINE_SET);
			nch_preline &= 0xffff0000;
			nch_preline |= (cam_param->real_h - 200);
			isp_reg_write(nch_preline, CH_PRELINE_SET);
			isp_bk_int_stat = isp_reg_read(ISP_INT_STAT);
			isp_bk_int_stat |= 1;
			isp_reg_write(isp_bk_int_stat, ISP_INT_STAT);

			updata_bisp_rawinfo(sd, cam_param);
			nisp_enable &= (~(1 << 4));
			isp_reg_write(nisp_enable, ISP_ENABLE);
	    } else {

			start = 0;
			end = cam_param->real_h - 1;
			isp_reg_write(ISP_CH2_ROW_START(0)
				| ISP_CH2_ROW_END(end - start), CH2_ROW_RANGE);
			start = 0;
			end = cam_param->real_w - 1;
			isp_reg_write(ISP_CH2_COL_START(0)
				| ISP_CH2_COL_END(end - start), CH2_COL_RANGE);

			/* waiting for raw-store finished */
			nisp_enable = isp_reg_read(ISP_ENABLE);
			nisp_enable &= 0xfffffffe;    /*disable now*/

			nch_preline = isp_reg_read(CH_PRELINE_SET);
			nch_preline &= 0xffff;
			nch_preline |= ((cam_param->real_h - 200) << 16);
			isp_reg_write(nch_preline, CH_PRELINE_SET);
			isp_bk_int_stat = isp_reg_read(ISP_INT_STAT);
			isp_bk_int_stat |= 1;
			isp_reg_write(isp_bk_int_stat, ISP_INT_STAT);

			updata_bisp_rawinfo(sd, cam_param);
			nisp_enable &= (~(1 << 4));
			isp_reg_write(nisp_enable, ISP_ENABLE);
	    }
	}

	return ret;
}

/*
 * S900 can not crop or scale for YUV,but can crop for RawData.
 * And we don't want to waste bandwidth and kill the
 * framerate by always requesting the maximum image from the client.
 */
static int s900_camera_set_crop(struct soc_camera_device *icd,
const struct v4l2_crop *a)
{
	const struct v4l2_rect *rect = &a->c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_crop cam_crop;
	struct v4l2_rect *cam_rect = &cam_crop.c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	unsigned long flags;
	int ret;

	/* During camera cropping its output window can change too, stop S900 */

	/* For UYVY/Bayer Raw input data */
	/* 1. - 2. Apply iterative camera S_CROP for new input window. */
	ret = client_s_crop(icd, a, &cam_crop);
	if (ret < 0)
	    return ret;

	/* On success cam_crop contains current camera crop */

	/* 3. Retrieve camera output window */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0) {
		isp_err("failed to g_mbus_fmt.\n");
		return ret;
	}

	if (check_frame_range(mf.width, mf.height)) {
		isp_err("inconsistent state. Use S_FMT to repair\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&cam_dev->lock, flags);

	/* Cache camera output window */
	cam_param->width = mf.width;
	cam_param->height = mf.height;

	icd->user_width = rect->width;
	icd->user_height = rect->height;
	cam_param->isp_left = rect->left - cam_rect->left + cam_param->n_crop_x;
	cam_param->isp_top = rect->top - cam_rect->top + cam_param->n_crop_y;

	if (cam_param->n_r_skip_num && cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		cam_param->width = rect->width;
		cam_param->height = rect->height;

		if (cam_param->n_r_skip_num == 2) {
			cam_param->n_crop_x = (cam_param->n_crop_x * 2) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y * 2) & (~7);
		} else if (cam_param->n_r_skip_num == 3) {
			cam_param->n_crop_x = (cam_param->n_crop_x * 3) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y * 3) & (~7);
		}
	}
	/* 4. Use S900 cropping to crop to the new window. */

	cam_param->rect = *cam_rect;
	cam_param->subrect = *rect;

	spin_unlock_irqrestore(&cam_dev->lock, flags);

	raw_store_set(icd);

	isp_info("4: isp cropped to %ux%u@%u:%u,%d,%d\n", icd->user_width,
		icd->user_height, cam_param->isp_left, cam_param->isp_top, cam_param->real_w, cam_param->real_h);

	return ret;
}

static int s900_camera_get_crop(struct soc_camera_device *icd,
	struct v4l2_crop *a)
{
	struct s900_camera_param *cam_param = icd->host_priv;

	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c = cam_param->subrect;
	return 0;
}

static int s900_camera_set_fmt(struct soc_camera_device *icd,
	struct v4l2_format *f)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	unsigned int skip_frames_num;
	int ret;
	int bSkipEn = 0;


	isp_info("%s, S_FMT(pix=0x%x, %ux%u)\n",
		__func__, pixfmt, pix->width, pix->height);

	/* sensor may skip different some frames */
	ret = v4l2_subdev_call(sd, sensor, g_skip_frames, &skip_frames_num);
	if (ret < 0)
		skip_frames_num = 0;

	cam_param->skip_frames = skip_frames_num;
	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
	    isp_info("format %x not found\n", pixfmt);
	    return -EINVAL;
	}

	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code = xlate->code;
	v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (cam_param->data_type == SENSOR_DATA_TYPE_RAW) {
		cam_param->real_w = mf.width;
		cam_param->real_h = mf.height;
	} else {
		cam_param->real_w = pix->width;
		cam_param->real_h = pix->height;
	}

	if ((mf.width != pix->width || mf.height != pix->height) && (cam_param->data_type == SENSOR_DATA_TYPE_RAW)) {
		unsigned int isp_ctl = isp_reg_read(ISP_CTL);
		unsigned int rh = 0;
		cam_param->n_r_skip_num = mf.width / pix->width;
		cam_param->n_r_skip_size = pix->width;
		rh = pix->height;

		cam_param->n_r_skip_size = cam_param->n_r_skip_size & (~0xf);

		if (cam_param->n_r_skip_num == 1) {
			isp_reg_write(isp_ctl & (~7), ISP_CTL);
			cam_param->n_r_skip_num = 0;
			cam_param->n_r_skip_size = 0;
			cam_param->n_crop_x = 0;
			cam_param->n_crop_y = 0;
		} else if (cam_param->n_r_skip_num == 2) {
			isp_reg_write(isp_ctl | 4, ISP_CTL);
			cam_param->n_crop_x = (cam_param->real_w - cam_param->n_r_skip_size * 2) / 2;
			cam_param->n_crop_y = (cam_param->real_h - rh * 2) / 2;
			cam_param->n_crop_x = (cam_param->n_crop_x) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y) & (~3);
			cam_param->real_w = 2 * cam_param->n_r_skip_size;
			cam_param->real_h = 2 * rh;

			isp_reg_write(cam_param->n_r_skip_size, RAW_SKIP_SIZE);
			bSkipEn = 1;

		} else if (cam_param->n_r_skip_num == 3) {
			cam_param->n_crop_x = (cam_param->real_w - cam_param->n_r_skip_size * 3) / 2;
			cam_param->n_crop_y = (cam_param->real_h - rh * 3) / 2;
			cam_param->n_crop_x = (cam_param->n_crop_x) & (~15);
			cam_param->n_crop_y = (cam_param->n_crop_y) & (~3);
			cam_param->real_w = 3 * cam_param->n_r_skip_size;
			cam_param->real_h = 3 * rh;

			isp_reg_write(isp_ctl | 6, ISP_CTL);
			isp_reg_write(cam_param->n_r_skip_size, RAW_SKIP_SIZE);
			bSkipEn = 1;
		}
		isp_info("raw skip in %d,%d,%d,%d,%d\n",
			cam_param->n_r_skip_size, cam_param->n_crop_x,
			cam_param->n_crop_y, cam_param->real_w, cam_param->real_h);
	} else {
		unsigned int isp_ctl = isp_reg_read(ISP_CTL);
		cam_param->n_r_skip_num = 0;
		cam_param->n_r_skip_size = 0;
		cam_param->n_crop_x = 0;
		cam_param->n_crop_y = 0;
		if (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
			isp_reg_write((isp_ctl & (~4)), ISP_CTL);
	}

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0) {
		isp_err("failed to configure for format %x\n",
			pix->pixelformat);
		return ret;
	}

	if (mf.code != xlate->code) {
		isp_err("wrong code: mf.code = 0x%x, xlate->code = 0x%x\n",
			mf.code, xlate->code);
		return -EINVAL;
	}


	if (check_frame_range(mf.width, mf.height)) {
		isp_err("sensor produced an unsupported frame %dx%d\n",
			mf.width, mf.height);
		return -EINVAL;
	}

	cam_param->isp_left = 0;
	cam_param->isp_top = 0;

	if (bSkipEn == 1) {
		mf.width = pix->width;
		mf.height = pix->height;
		cam_param->isp_left = cam_param->n_crop_x;
		cam_param->isp_top = cam_param->n_crop_y;
	}

	cam_param->width = mf.width;
	cam_param->height = mf.height;
	cam_param->code = xlate->code;

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
		xlate->host_fmt);
	if (pix->bytesperline < 0) {
		isp_err("bytesperline %d not correct.\n",
			pix->bytesperline);
		return pix->bytesperline;
	}
	pix->sizeimage = pix->height * pix->bytesperline;
	pix->width = mf.width;
	pix->height = mf.height;
	pix->field = mf.field;
	pix->colorspace = mf.colorspace;

	icd->current_fmt = xlate;
	icd->user_width = cam_param->width;
	icd->user_height = cam_param->height;

	isp_info("sensor set %dx%d, code = %#x\n",
		pix->width, pix->height, cam_param->code);

	isp_set_rect(icd);
	isp_set_output_fmt(icd, icd->current_fmt->host_fmt->fourcc);

	isp_info("set output data format %s\n",
		icd->current_fmt->host_fmt->name);

	return 0;
}

static int s900_camera_try_fmt(struct soc_camera_device *icd,
	struct v4l2_format *f)
{
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	isp_info("user_formats:\n");

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (pixfmt && !xlate) {
		isp_err("format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/*
	* Limit to S900 hardware capabilities.
	*YUV422P planar format requires
	*images size to be a multiple of 16 bytes.  If not, zeros will be
	* inserted between Y and U planes, and U and V planes, which violates
	* the YUV422P standard.
	*/
	v4l_bound_align_image(&pix->width, 32, ISP_MAX_WIDTH, 5,
	&pix->height, 1, ISP_MAX_HEIGHT, 0,
	pixfmt == V4L2_PIX_FMT_YUV422P ? 4 : 0);

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
		xlate->host_fmt);
	if (pix->bytesperline < 0) {
		isp_err("bytesperline %d not correct.\n",
			pix->bytesperline);
		return pix->bytesperline;
	}
	pix->sizeimage = pix->height * pix->bytesperline;

	/* limit to sensor capabilities */
	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code = xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if ((mf.width != pix->width
		|| mf.height != pix->height)
		&& (cam_param->data_type == SENSOR_DATA_TYPE_RAW)
		&& pix->width && pix->height) {
		mf.width = pix->width;
		mf.height = pix->height;
	}

	pix->width = mf.width;
	pix->height = mf.height;
	pix->colorspace = mf.colorspace;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field = V4L2_FIELD_NONE;
		break;
	default:
		isp_err("field type %d unsupported.\n", mf.field);
		ret = -EINVAL;
	}

	return ret;
}

struct s900_camera_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer			vb;
	enum v4l2_mbus_pixelcode		code;
};

static int s900_camera_reqbufs(struct soc_camera_device *icd,
	struct v4l2_requestbuffers *p)
{
	int i;

	/*
	 * This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered
	 */
	for (i = 0; i < p->count; i++) {
		struct s900_camera_buffer *buf;

		buf = container_of(icd->vb_vidq.bufs[i],
			struct s900_camera_buffer, vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int s900_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	return videobuf_poll_stream(file, &icd->vb_vidq, pt);
}

static int s900_camera_querycap(struct soc_camera_host *ici,
	struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, "S900 Camera", sizeof(cap->card));
	cap->version = KERNEL_VERSION(0, 0, 1);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int s900_camera_get_parm(struct soc_camera_device *icd,
	struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, g_parm, parm);
}

static int s900_camera_set_parm(struct soc_camera_device *icd,
	struct v4l2_streamparm *parm)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, s_parm, parm);
}

static int s900_camera_suspend(struct device *dev)
{
	/* usually when system suspend, it will*
	*close camera, so no need process more*/

	isp_reg_write(isp_reg_read(ISP_ENABLE) | (~1), ISP_ENABLE);

	isp_info("enter camera suspend...\n");
	return 0;
}

static int s900_camera_resume(struct device *dev)
{
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct s900_camera_dev *cam_dev = container_of(ici,
		struct s900_camera_dev, soc_host);


	/*clk_set_parent and clk_set_rate will check the setting value
	with last value,if no different,it will ignore and return back
	so set the value of rate and parent with a init value in order
	to avoid ignoring the setting value */
	cam_dev->isp_clk->parent = NULL;
	cam_dev->isp_clk->rate = 0;

	cam_dev->sensor_clk->parent = NULL;
	cam_dev->sensor_clk->rate = 0;

	cam_dev->ch_clk[ISP_CHANNEL_0]->parent = NULL;
	cam_dev->ch_clk[ISP_CHANNEL_0]->rate = 0;

	cam_dev->ch_clk[ISP_CHANNEL_1]->parent = NULL;
	cam_dev->ch_clk[ISP_CHANNEL_1]->rate = 0;

	/*setting bisp clock parent -> dev_clk*/
	clk_set_parent(cam_dev->isp_clk, cam_dev->isp_clk_parent);

	/*setting sensor clock parent -> hosc*/
	clk_set_parent(cam_dev->sensor_clk, cam_dev->sensor_clk_parent);

	clk_set_parent(cam_dev->ch_clk[ISP_CHANNEL_0], cam_dev->csi_clk_parent);
	clk_set_parent(cam_dev->ch_clk[ISP_CHANNEL_1], cam_dev->csi_clk_parent);

	isp_reg_write(isp_reg_read(ISP_INT_STAT), ISP_INT_STAT);

	isp_info("enter camera resume...\n");
	return 0;
}

int s900_camera_enum_fsizes(struct soc_camera_device *icd,
	struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, enum_framesizes, fsize);
}

static void free_buffer(struct videobuf_queue *vq,
	struct s900_camera_buffer *buf)
{
	if (in_interrupt())
		BUG();

	videobuf_waiton(vq, &buf->vb, 0, 0);
	if (buf->vb.memory == V4L2_MEMORY_MMAP)
		videobuf_dma_contig_free(vq, &buf->vb);

	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/*
 * Videobuf operations
 */

/*
 * Calculate the __buffer__ (not data) size and number of buffers.
 * Called with .vb_lock held
 */
static int s900_camera_videobuf_setup(struct videobuf_queue *vq,
unsigned int *count, unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
		icd->current_fmt->host_fmt);

	if (bytes_per_line < 0)
		return bytes_per_line;

	*size = bytes_per_line * icd->user_height;

	if (!*count || *count < 2)
		*count = 2;

	if (*size * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = MAX_VIDEO_MEM * 1024 * 1024 / *size;
	isp_info("count=%d, size=%d\n", *count, *size);

	return 0;
}

/* Called with .vb_lock held */
static int s900_camera_videobuf_prepare(struct videobuf_queue *vq,
	struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->parent;
	struct s900_camera_buffer *buf;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
		icd->current_fmt->host_fmt);
	int ret;

	if (bytes_per_line < 0) {
		isp_err("bytes_per_line err\n");
		return bytes_per_line;
	}

	buf = container_of(vb, struct s900_camera_buffer, vb);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));
	BUG_ON(NULL == icd->current_fmt);

	if (buf->code	!= icd->current_fmt->code
		|| vb->width	!= icd->user_width
		|| vb->height	!= icd->user_height
		|| vb->field	!= field) {
		buf->code	= icd->current_fmt->code;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
		bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);
	}

	vb->size = vb->height * bytes_per_line;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		dev_err(dev, "buffer error!\n");
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		if ((vb->memory == V4L2_MEMORY_USERPTR) && (0 != vb->baddr))
			ret = 0;
		else
			ret = videobuf_iolock(vq, vb, NULL);

		if (ret)
			goto fail;
		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
fail:
	isp_err("buffer err\n");
	free_buffer(vq, buf);
out:
	return ret;
}

/*
 * Called with .vb_lock mutex held and
 * under spinlock_irqsave(&cam_dev->lock, ...)
 */
static void s900_camera_videobuf_queue(struct videobuf_queue *vq,
	struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);


	BUG_ON(!irqs_disabled());
	vb->state = VIDEOBUF_QUEUED;

	list_add_tail(&vb->queue, &cam_param->capture);

	if (!cam_param->cur_frm) {
		cam_param->cur_frm = list_entry(cam_param->capture.next,
			struct videobuf_buffer, queue);
		cam_param->cur_frm->state = VIDEOBUF_ACTIVE;

		list_del_init(&cam_param->cur_frm->queue);
		cam_param->prev_frm = NULL;
		updata_bisp_info(sd, cam_param);

		isp_set_rect(icd);
		isp_set_frame2(icd, cam_param->cur_frm);
		isp_capture_start(cam_dev, icd);
	}
}

/* Called with .vb_lock held */
static void s900_camera_videobuf_release(struct videobuf_queue *vq,
	struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	unsigned long flags;
	int i;
	isp_capture_stop(cam_dev, icd);

	spin_lock_irqsave(&cam_dev->lock, flags);

	if (cam_param->cur_frm == vb)
		cam_param->cur_frm = NULL;

	if (cam_param->prev_frm == vb)
		cam_param->prev_frm = NULL;

	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED)) {
		vb->state = VIDEOBUF_ERROR;
		if (!list_empty(&cam_param->capture))
			list_del_init(&vb->queue);

		wake_up_all(&vb->done);
	}

	spin_unlock_irqrestore(&cam_dev->lock, flags);

	free_buffer(vq, container_of(vb, struct s900_camera_buffer, vb));

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == vq->bufs[i])
			continue;
		if (vb == vq->bufs[i]) {
			kfree(vq->bufs[i]);
			vq->bufs[i] = NULL;
		}
	}
}

static struct videobuf_queue_ops s900_camera_videobuf_ops = {
	.buf_setup      = s900_camera_videobuf_setup,
	.buf_prepare    = s900_camera_videobuf_prepare,
	.buf_queue      = s900_camera_videobuf_queue,
	.buf_release    = s900_camera_videobuf_release,
};

static void s900_camera_init_videobuf(struct videobuf_queue *q,
	struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct s900_camera_dev *cam_dev = ici->priv;
	struct s900_camera_param *cam_param = icd->host_priv;
	struct device *dev = icd->parent;

	videobuf_queue_dma_contig_init(q, &s900_camera_videobuf_ops, dev,
	&cam_dev->lock,
	V4L2_BUF_TYPE_VIDEO_CAPTURE,
	V4L2_FIELD_NONE,
	sizeof(struct s900_camera_buffer), icd, NULL);
	/*加入此语句后，需在soc_camera_open中
	***保证s900_camera_init_videobuf
	***在soc_camera_set_fmt之后调用。
	**如果内核版本有变，需检查此项*/

	cam_param->started = DEV_OPEN;
}

static struct soc_camera_host_ops s900_soc_camera_host_ops = {
	.owner = THIS_MODULE,
	.add = s900_camera_add_device,
	.remove = s900_camera_remove_device,
	.get_formats = s900_camera_get_formats,
	.put_formats = s900_camera_put_formats,
	.cropcap = s900_camera_cropcap,
	.get_crop = s900_camera_get_crop,
	.set_crop = s900_camera_set_crop,
	.set_livecrop = s900_camera_set_crop,
	.set_fmt = s900_camera_set_fmt,
	.try_fmt = s900_camera_try_fmt,
	.set_parm = s900_camera_set_parm,
	.get_parm = s900_camera_get_parm,
	.reqbufs = s900_camera_reqbufs,
	.poll = s900_camera_poll,
	.querycap = s900_camera_querycap,
	.set_bus_param = s900_camera_set_bus_param,
	.init_videobuf = s900_camera_init_videobuf,
	.enum_framesizes = s900_camera_enum_fsizes,
};

static inline void sensor_pwd_info_init(struct sensor_pwd_info *spinfo)
{
	spinfo->flag = 0;
	spinfo->gpio_rear.num = -1;
	spinfo->gpio_front.num = -1;
	spinfo->gpio_front_reset.num = -1;
	spinfo->gpio_rear_reset.num = -1;
	spinfo->ch_clk[ISP_CHANNEL_0] = NULL;
	spinfo->ch_clk[ISP_CHANNEL_1] = NULL;
}

static inline void isp_regulators_init(struct isp_regulators *ir)
{
	struct dts_regulator *dr = &ir->avdd.regul;

	dr->regul = NULL;
	ir->avdd_use_gpio = 0;
	ir->dvdd_use_gpio = 0;
	ir->dvdd.regul = NULL;
}

static int gpio_init(struct device_node *fdt_node,
	const char *gpio_name, struct dts_gpio *gpio, bool active)
{
	enum of_gpio_flags flags;

	if (!of_find_property(fdt_node, gpio_name, NULL)) {
		isp_err("<isp> %s no config gpios\n", gpio_name);
		goto fail;
	}

	gpio->num = of_get_named_gpio_flags(fdt_node, gpio_name, 0, &flags);
	gpio->active_level = !!(flags & OF_GPIO_ACTIVE_LOW);

	isp_info("%s: num-%d, active-%s\n",
		gpio_name, gpio->num, gpio->active_level ? "high" : "low");

	if (gpio_request(gpio->num, gpio_name)) {
		isp_err("<isp>fail to request gpio [%d]\n", gpio->num);
		gpio->num = -1;
		goto fail;
	}

	if (active)
		gpio_direction_output(gpio->num, gpio->active_level);
	else
		gpio_direction_output(gpio->num, !gpio->active_level);

	isp_info("gpio value: 0x%x\n", gpio_get_value(gpio->num));

	return 0;
fail:
	return -1;
}

static void gpio_exit(struct dts_gpio *gpio, bool active)
{
	if (gpio->num >= 0) {
		if (active)
			gpio_set_value(gpio->num, gpio->active_level);
		else
			gpio_set_value(gpio->num, !gpio->active_level);

	    gpio_free(gpio->num);
	}
}

static int isp_gpio_init(struct device_node *fdt_node,
	struct sensor_pwd_info *spinfo)
{
	const char *sensors = NULL;
	const char *board_type = NULL;

	if (of_property_read_string(fdt_node, "board_type", &board_type)) {
		isp_info("get board_type faild");
		goto free_reset;
	}
	if (!strcmp(board_type, "ces")) {
		if (gpio_init(fdt_node, "front-reset-gpios",
			&spinfo->gpio_front_reset, GPIO_HIGH))
			goto fail;

		if (gpio_init(fdt_node, "rear-reset-gpios",
			&spinfo->gpio_rear_reset, GPIO_HIGH))
			goto fail;
	} else if (!strcmp(board_type, "evb")) {
		if (gpio_init(fdt_node, "reset-gpios",
			&spinfo->gpio_front_reset, GPIO_HIGH))
			goto fail;

		spinfo->gpio_rear_reset.num =
			spinfo->gpio_front_reset.num;
		spinfo->gpio_rear_reset.active_level =
			spinfo->gpio_front_reset.active_level;
	} else {
		isp_err("get board type faild");
		return -1;
	}

	if (of_property_read_string(fdt_node, "sensors", &sensors)) {
		isp_err("<isp> get sensors faild\n");
		goto free_reset;
	}

	if (!strcmp(sensors, "front")) {
		/* default is power-down*/
		if (gpio_init(fdt_node, "pwdn-front-gpios",
			&spinfo->gpio_front, GPIO_HIGH))
			goto free_reset;
		spinfo->flag = SENSOR_FRONT;
	} else if (!strcmp(sensors, "rear")) {
		if (gpio_init(fdt_node, "pwdn-rear-gpios",
			&spinfo->gpio_rear, GPIO_HIGH))
			goto free_reset;
		spinfo->flag = SENSOR_REAR;
	} else if (!strcmp(sensors, "dual")) {
		if (gpio_init(fdt_node, "pwdn-front-gpios",
			&spinfo->gpio_front, GPIO_HIGH))
			goto free_reset;

		if (gpio_init(fdt_node, "pwdn-rear-gpios",
			&spinfo->gpio_rear, GPIO_HIGH)) {
			gpio_exit(&spinfo->gpio_front, GPIO_LOW);
			goto free_reset;
		}
		spinfo->flag = SENSOR_DUAL;
	} else {
		isp_err("sensors of dts is wrong\n");
		goto free_reset;
	}

	if (gpio_init(fdt_node, "avdd-gpios", &spinfo->gpio_power, GPIO_HIGH)) {
		gpio_exit(&spinfo->gpio_power, GPIO_LOW);
		goto free_reset;
	}
	return 0;

free_reset:
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
fail:
	return -1;
}

static void isp_gpio_exit(struct sensor_pwd_info *spinfo)
{
	/* only free valid gpio, so no need to check its existence.*/
	gpio_exit(&spinfo->gpio_front, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear, GPIO_LOW);
	gpio_exit(&spinfo->gpio_rear_reset, GPIO_LOW);
	gpio_exit(&spinfo->gpio_front_reset, GPIO_LOW);
}

static struct s900_camera_dev *cam_dev_alloc(struct device *dev,
	struct device_node *dn)
{
	struct s900_camera_dev *cdev;


	cdev = kzalloc(sizeof(struct s900_camera_dev), GFP_ATOMIC);
	if (NULL == cdev) {
		isp_err("alloc s900 camera device failed\n");
		goto ealloc;
	}

	spin_lock_init(&cdev->lock);

	cdev->mfp = NULL;
	cdev->isp_clk = NULL;
	cdev->sensor_clk = NULL;
	cdev->ch_clk[ISP_CHANNEL_0] = NULL;
	cdev->ch_clk[ISP_CHANNEL_1] = NULL;

	sensor_pwd_info_init(&cdev->spinfo);
	if (camera_clock_init(dev, cdev)) {
		isp_err("camera clocks init error\n");
		goto clk_err;
	}

	if (isp_gpio_init(dn, &(cdev->spinfo))) {
		isp_err("gpio init error\n");
		goto egpio;
	}

	cdev->mfp = pinctrl_get_select_default(dev);
	if (IS_ERR(cdev->mfp))
		return NULL;

	return cdev;

clk_err:
	kfree(cdev);
ealloc:
egpio:
	return NULL;
}

static void cam_dev_free(struct s900_camera_dev *cdev)
{
	isp_info("cam_dev_free\n");

	isp_gpio_exit(&cdev->spinfo);

	pinctrl_put(cdev->mfp);
	kfree(cdev);
}

static int s900_camera_host_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct s900_camera_dev *cam_dev;
	struct soc_camera_host *soc_host;
	unsigned int irq;
	int err = 0;
	struct resource *isp_mem;
	struct resource *csi0_mem;
	struct resource *csi1_mem;

	isp_info("%s\n", __func__);
	pdev->id = of_alias_get_id(dn, "isp");
	if (pdev->id < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n",
			pdev->id);
		goto eid;
	}

	cam_dev = cam_dev_alloc(&pdev->dev, dn);
	if (NULL == cam_dev) {
		dev_err(&pdev->dev, "s900_camera_dev alloc failed\n");
		goto eid;
	}

	attach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);

	isp_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!isp_mem) {
		dev_err(dev, "can't get isp_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GISPMAPADDR = devm_ioremap(dev, isp_mem->start,
		resource_size(isp_mem));
	if (!GISPMAPADDR) {
		dev_err(dev, "can't ioremap isp_mem\n");
		return -ENOMEM;
	}

	csi0_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!csi0_mem) {
		dev_err(dev, "can't get csi0_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GCSI1MAPADDR = devm_ioremap(dev, csi0_mem->start,
		resource_size(csi0_mem));
	if (!GCSI1MAPADDR) {
		dev_err(dev, "can't ioremap csi0_mem\n");
		return -ENOMEM;
	}

	csi1_mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!csi1_mem) {
		dev_err(dev, "can't get csi1_mem IORESOURCE_MEM\n");
		return -EINVAL;
	}

	GCSI2MAPADDR = devm_ioremap(dev, csi1_mem->start,
		resource_size(csi1_mem));
	if (!GCSI2MAPADDR) {
		dev_err(dev, "can't ioremap csi1_mem\n");
		return -ENOMEM;
	}

	if (GCMUMAPADDR == 0)
		GCMUMAPADDR =  ioremap(CMU_BASE, 0xfc);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no isp irq resource?\n");
		err = -ENODEV;
		goto egetirq;
	}

	cam_dev->irq = irq;
	err = devm_request_irq(&pdev->dev, cam_dev->irq,
		s900_camera_host_isp_isr,
		IRQF_DISABLED, S900_CAM_HOST_NAME, cam_dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register isp %d interrupt.\n",
			cam_dev->irq);
		err = -EBUSY;
		goto egetirq;
	}

	/* powergate */
	pm_runtime_enable(&pdev->dev);

	soc_host = &cam_dev->soc_host;
	soc_host->ops = &s900_soc_camera_host_ops;
	soc_host->priv = cam_dev;
	soc_host->v4l2_dev.dev = &pdev->dev;
	soc_host->nr = pdev->id;
	isp_info("host id %d\n", soc_host->nr);
	switch (soc_host->nr) {
	case 0:
		soc_host->drv_name = S900_CAM_HOST_NAME;
		break;
	case 1:
		soc_host->drv_name = S900_CAM_HOST_NAME;
		break;
	default:
		isp_err("host num error\n");
	}

	err = soc_camera_host_register(soc_host);
	if (err) {
		dev_err(&pdev->dev, "Unable to register s900 soc camera host.\n");
		goto echreg;
	}

	isp_info("isp driver probe success...\n");
	return 0;

echreg:
	pm_runtime_disable(&pdev->dev);
egetirq:
	detach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);
	cam_dev_free(cam_dev);
eid:
	isp_err("isp driver probe fail...\n");
	return err;
}

static int s900_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct s900_camera_dev *cam_dev = soc_host->priv;
	isp_info("isp driver remove...in\n");
	soc_camera_host_unregister(soc_host);
	detach_sensor_pwd_info(&pdev->dev, &cam_dev->spinfo, pdev->id);
	cam_dev_free(cam_dev);
	if (GCMUMAPADDR)
		iounmap(GCMUMAPADDR);
	isp_info("isp driver remove...\n");

	return 0;
}

static const struct dev_pm_ops s900_camera_dev_pm_ops = {
	.runtime_suspend = s900_camera_suspend,
	.runtime_resume = s900_camera_resume,
};

static const struct of_device_id s900_camera_of_match[]  = {
	{.compatible = ISP_FDT_COMPATIBLE,},
	{},
};
MODULE_DEVICE_TABLE(of, s900_camera_of_match);

static struct platform_driver s900_camera_host_driver = {
	.driver = {
		.name = S900_CAM_HOST_NAME,
		.owner = THIS_MODULE,
		.pm = &s900_camera_dev_pm_ops,
		.of_match_table = s900_camera_of_match,
	},
	.probe = s900_camera_host_probe,
	.remove = s900_camera_remove,
};

/* platform device register by dts*/
static int __init s900_camera_init(void)
{
	int ret;

	ret = platform_driver_register(&s900_camera_host_driver);
	if (ret)
		isp_err(":Could not register isp driver\n");

	isp_info("s900_camera_host_driver ok\n");
	return ret;
}

static void __exit s900_camera_exit(void)
{
	platform_driver_unregister(&s900_camera_host_driver);
}

late_initcall(s900_camera_init);
module_exit(s900_camera_exit);

MODULE_DESCRIPTION("S900 SoC Camera Host driver");
MODULE_AUTHOR("lichi <lichi@actions-semi.com>");
MODULE_LICENSE("GPL v2");
