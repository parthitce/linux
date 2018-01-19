
/*
 * Actions OWL SoCs BISP driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Shiheng Tan <tanshiheng@actions-semi.com>
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

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/errno.h>	/* error codes */
#include <linux/vmalloc.h>
#include <linux/init.h>		/* module_init/module_exit */
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/prom.h>

#include "legacy_si.h"
#include "module_diff.h"
#include "owl_camera.c"

static inline int
module_camera_clock_init(struct device *dev, struct camera_dev *cdev)
{
	cdev->sensor_clk_parent = devm_clk_get(dev, SENSOR_CLK_PARENT);
	if (IS_ERR(cdev->sensor_clk_parent)) {
		module_err("error: sensor_clk_parent can't get clocks!!!\n");
		return -EINVAL;
	}

	return 0;
}

static inline int
module_camera_clock_enable(struct camera_dev *cdev,
			   struct camera_param *cam_param)
{
	return 0;
}

static inline int set_col_range(struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	unsigned int start = cam_param->left;
	unsigned int end = cam_param->left + icd->user_width - 1;
	int width = end - start + 1;

	if ((width > MODULE_MAX_WIDTH) || (width < 32) || (start >= 8192) ||
	    (end >= 8192)) {
		return -EINVAL;
	}

    reg_write(CH_COL_START(0) | CH_COL_END(end - start),
    	  GMODULEMAPADDR, SI_COL_RANGE, MODULE_BASE);

	return 0;
}

static inline void
module_set_frame_yuv420(phys_addr_t module_addr, struct soc_camera_device *icd)
{
    /*Notice: This format maybe not support in legacy si!*/
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_u;
	phys_addr_t module_addr_v;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_u = ALIGN(module_addr_y + icd->user_width *
			      icd->user_height, 2);
	module_addr_v = ALIGN(module_addr_u + icd->user_width *
			      icd->user_height / 4, 2);

	reg_write(module_addr_y, GMODULEMAPADDR, SI_ADDR0, MODULE_BASE);
	reg_write(module_addr_u, GMODULEMAPADDR, SI_ADDR1, MODULE_BASE);
	reg_write(module_addr_v, GMODULEMAPADDR, SI_ADDR1, MODULE_BASE);
}

static inline void
module_set_frame_yvu420(phys_addr_t module_addr, struct soc_camera_device *icd)
{

}

static inline void
module_set_frame_yvu422(phys_addr_t module_addr, struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_uv;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_uv = ALIGN(module_addr_y + icd->user_width *
			       icd->user_height, 2);
	reg_write(module_addr_y, GMODULEMAPADDR, SI_ADDR0, MODULE_BASE);
	reg_write(module_addr_uv, GMODULEMAPADDR, SI_ADDR1, MODULE_BASE);
}

static inline void
module_set_frame_nv12_nv21(phys_addr_t module_addr,
			   struct soc_camera_device *icd)
{
	struct camera_param *cam_param = icd->host_priv;
	phys_addr_t module_addr_y;
	phys_addr_t module_addr_uv;

	module_addr_y = ALIGN(module_addr, 2);
	module_addr_uv = ALIGN(module_addr_y + icd->user_width *
			       icd->user_height, 2);
	reg_write(module_addr_y, GMODULEMAPADDR, SI_ADDR0, MODULE_BASE);
	reg_write(module_addr_uv, GMODULEMAPADDR, SI_ADDR1, MODULE_BASE);
}

static inline int module_isr(struct soc_camera_device *icd,
		struct camera_param *cam_param, struct v4l2_subdev *sd,
		unsigned int module_int_stat, int i)
{
	unsigned int preline_int_pd;
	struct videobuf_buffer *vb;

	if (cam_param->started == DEV_STOP) {
		reg_write(reg_read(GMODULEMAPADDR, SI_CON_STAT,
			MODULE_BASE) &
			(~MODULE_INT_STAT_CH_FRAME_END_INT_EN),
			GMODULEMAPADDR, SI_CON_STAT, MODULE_BASE);
		complete(&cam_param->wait_stop);

		if (i == 0)
			return 0;
		else
			return -1;
	}

	//if ((module_int_stat & 0x1800)) {
	//	reg_write(module_int_stat, GMODULEMAPADDR, SI_CON_STAT,
	//		MODULE_BASE);
	//	return -1;
	//}
	/*capture_stop function set it, clear it here anyway. */
	reg_write(module_int_stat, GMODULEMAPADDR, SI_CON_STAT, MODULE_BASE);
	preline_int_pd = MODULE_INT_STAT_CH_PL_PD;

	if (module_int_stat & preline_int_pd) {
		/*send out a packet already recevied all data */
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
			/*set_rect(icd); */
			set_frame(icd, cam_param->cur_frm);
			cam_param->cur_frm->state = VIDEOBUF_ACTIVE;
			BUG_ON(cam_param->prev_frm == cam_param->cur_frm);
		} else {
			cam_param->prev_frm = NULL;
			/*set_rect(icd); */
			set_frame(icd, cam_param->cur_frm);
		}
	}

	return 0;
}

static inline void module_camera_suspend(struct device *dev)
{
	/* usually when system suspend, it will close camera, so no need
	 * process more */

	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct camera_dev *cam_dev = container_of(ici, struct camera_dev,
						  soc_host);

	disable_irq(cam_dev->irq);

	/* si_reg_write(si_reg_read(MODULE_ENABLE) | (~1), MODULE_ENABLE); */

	pm_runtime_put_sync(dev);
}

static inline void
module_camera_resume(struct device *dev, struct camera_dev *cam_dev)
{
	pm_runtime_enable(dev);
	msleep(100);
	pm_runtime_get_sync(dev);
	msleep(150);

	//clk_set_parent(cam_dev->ch_clk, cam_dev->sensor_clk_parent);

	enable_irq(cam_dev->irq);
}

static inline void
module_set_output_fmt(struct soc_camera_device *icd, u32 fourcc)
{
	struct camera_param *cam_param = icd->host_priv;
	unsigned int module_out_fmt;

	module_out_fmt = reg_read(GMODULEMAPADDR, SI_FORMAT, MODULE_BASE);
	module_out_fmt &= ~MODULE_CTL_CHANNEL_OUTPUT_SEQ_MASK;
	get_fmt(fourcc, &module_out_fmt);
	reg_write(module_out_fmt, GMODULEMAPADDR, SI_FORMAT, MODULE_BASE);
}

static inline void module_camera_clock_disable(struct camera_dev *cdev)
{
}

static int
updata_module_info(struct v4l2_subdev *sd, struct camera_param *cam_param)
{
	return 0;
}

static int raw_store_set(struct soc_camera_device *icd)
{
	return 0;
}

static void module_regs_init(void)
{

}

static int ext_cmd(struct v4l2_subdev *sd, int cmd, void *args)
{
	return 0;
}

static int host_module_init(void)
{
	return 0;
}

static void host_module_exit(void)
{
}
