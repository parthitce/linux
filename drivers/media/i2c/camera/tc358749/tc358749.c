/*
 * tc358749 Hdmi-in Driver
 *
 * Copyright (C) 2014 Actions Semiconductor Co.,LTD
 * Yiguang <liuyiguang@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <media/v4l2-chip-ident.h>
#include <linux/platform_device.h>
#include "module_diff.h"
#include "../host_comm/owl_device.h"
#include "../module_comm/module_comm.c"
#include "../module_comm/module_detect.c"

#include <linux/init.h>
#include <linux/kthread.h>

#include <linux/switch.h>

bool verified;
bool pull_out = true;
static struct task_struct *tsk;
static unsigned int in_irq_gpio = -1;
static struct sensor_pwd_info *spif;

//irq flag
#define IRQ_FLAG (IRQF_TRIGGER_LOW | IRQF_ONESHOT) 

//For hdmi input audio state.0-disconnect audio;1-input audio.
static struct switch_dev sdev_hdmi_audio = {
	.name = "hdmi_in_audio",
};

//For tc358749 state.0-disconnect hdmi input;1-connect hdmi input.
static struct switch_dev sdev_tc358749 = {
	.name = "tc358749",
};

void sensor_reset(struct i2c_adapter *i2c_adap, struct sensor_pwd_info *spinfo);
void check_tc_reg(struct i2c_adapter *i2c_adap)
{
    int state;
	unsigned int hdmi_check_value[6] = {0};
    int de_width, de_height, m_width, m_height;

    camera_i2c_read(i2c_adap, 1, 0x8520, &hdmi_check_value[0]); // SYS_STATUS
    if (hdmi_check_value[0] != 0x1f && hdmi_check_value[0] != 0x9f) {
        pr_info("Read SYS_STATUS = 0x%x \n", hdmi_check_value[0]);
        pull_out = true;
        sensor_power_off(true, spif, true);
        //No Hdmi input, exit.
        if (hdmi_check_value[0] == 0x01) {
            pr_info("Maybe something error, now reset tc358749");
            sensor_reset(i2c_adap, spif);
        }
        return;
    }

    camera_i2c_read(i2c_adap, 1, 0x8523, &hdmi_check_value[1]); // audio status0
    camera_i2c_read(i2c_adap, 1, 0x8582, &hdmi_check_value[2]); // DE_WIDTH_H0
    camera_i2c_read(i2c_adap, 1, 0x8583, &hdmi_check_value[3]); // DE_WIDTH_H1
    camera_i2c_read(i2c_adap, 1, 0x8588, &hdmi_check_value[4]); // DE_WIDTH_V0
    camera_i2c_read(i2c_adap, 1, 0x8589, &hdmi_check_value[5]); // DE_WIDTH_V1

    //reg 0x8523,check bit0:0-no audio;1-audio input.
    if (hdmi_check_value[1] & 0x1 == 0) {
        pr_err("No Hdmi input audio, close audio switch \n");
        if (switch_get_state(&sdev_hdmi_audio) == 1) {
            switch_set_state(&sdev_hdmi_audio, 0);
        }
    }

    de_width = (hdmi_check_value[3] << 8) | hdmi_check_value[2];
    de_height = (hdmi_check_value[5] << 8) | hdmi_check_value[4];
    m_width = module_win_list[0]->width;
    m_height = module_win_list[0]->height;
    pr_info("Get hdmi input w = %d ; h = %d ; m_w = %d ; m_h = %d \n", \
            de_width, de_height, m_width, m_height);
    state = switch_get_state(&sdev_tc358749);

    //1080P
    if (1920 == de_width && 1080 == de_height) {
        if (pull_out || m_width != de_width || m_height != de_height) {
            pull_out = false;
            camera_i2c_write(i2c_adap, 2, 0x0022, 0x0203);
            msleep(100);
            camera_i2c_write(i2c_adap, 2, 0x0022, 0x0213);
            msleep(100);
            module_win_list[0] = &module_win_1080p;
            pr_info("Change regs to 1080P========\n");
            camera_write_array(i2c_adap, module_1080p_regs);
        }
        if (state == 0 || state != 1) {
            switch_set_state(&sdev_tc358749, 1);
        }
    }
    //720P
    if (1280 == de_width && 720 == de_height) {
        if (pull_out || m_width != de_width || m_height != de_height) {
            pull_out = false;
            camera_i2c_write(i2c_adap, 2, 0x0022, 0x0603);
            msleep(100);
            camera_i2c_write(i2c_adap, 2, 0x0022, 0x0613);
            msleep(100);
            module_win_list[0] = &module_win_720p;
            pr_info("Change regs to 720P========\n");
            camera_write_array(i2c_adap, module_720p_regs);
        }
        if (state == 0 || state != 2) {
            switch_set_state(&sdev_tc358749, 2);
        }
    }
}

irqreturn_t checkhdmi_input(int irq, void *dev_id)
{
    int state;
    struct i2c_adapter *i2c_adap = dev_id;
    //pr_err("checkhdmiinput spif gpio rear state: %d-%d ; rear_reset state: %d-%d ======\n", spif->gpio_rear.num, gpio_get_value(spif->gpio_rear.num), spif->gpio_rear_reset.num, gpio_get_value(spif->gpio_rear_reset.num));
    if (gpio_get_value(spif->gpio_rear.num) == 0 || gpio_get_value(spif->gpio_rear_reset.num) == 0) {
        sensor_power_on(true, spif, true);
        msleep(200);
	    camera_write_array(i2c_adap, module_init_regs);
        state = switch_get_state(&sdev_tc358749);
        if (gpio_get_value(spif->gpio_rear.num) == 1 && gpio_get_value(spif->gpio_rear_reset.num) == 1) {
            if (module_win_list[0] == &module_win_1080p) {
                pr_info("recover 1080P============\n");
                camera_i2c_write(i2c_adap, 2, 0x0022, 0x0203);
                msleep(100);
                camera_i2c_write(i2c_adap, 2, 0x0022, 0x0213);
                msleep(100);
	            camera_write_array(i2c_adap, module_1080p_regs);
                if (state == 0 || state != 1) {
                    switch_set_state(&sdev_tc358749, 1);
                }
            }
            if (module_win_list[0] == &module_win_720p) {
                pr_info("recover 720P============\n");
                camera_i2c_write(i2c_adap, 2, 0x0022, 0x0603);
                msleep(100);
                camera_i2c_write(i2c_adap, 2, 0x0022, 0x0613);
                msleep(100);
	            camera_write_array(i2c_adap, module_720p_regs);
                if (state == 0 || state != 2) {
                    switch_set_state(&sdev_tc358749, 2);
                }
            }
        }
        //if (switch_get_state(&sdev_tc358749) == 0) {
        //    switch_set_state(&sdev_tc358749, 1);
        //}

        return IRQ_HANDLED;
    } 
    check_tc_reg(i2c_adap);
    msleep(500);
    return IRQ_HANDLED;
}

static int checkhdmi_plug(void *data)
{
    int rst;
    struct i2c_adapter *i2c_adap = data;
    do
    {
        if (in_irq_gpio != -1) {
            rst = gpio_get_value(in_irq_gpio);
            //pr_err("gpio stat: %d; gpio: %d in line %d ============\n", rst, in_irq_gpio, __LINE__);
            if(rst == 1){
                pr_err("hdmi pull out.\n");
                if (switch_get_state(&sdev_hdmi_audio)) {
                    switch_set_state(&sdev_hdmi_audio, 0);
                }

                if (switch_get_state(&sdev_tc358749)) {
                    switch_set_state(&sdev_tc358749, 0);
                }

                if (!pull_out) {
                    pull_out = true;
                    sensor_power_off(true, spif, true);
                }
            }
        }
        else
        {
            check_tc_reg(i2c_adap);
        }
        msleep(500);
    } while (!kthread_should_stop());

    return 0;    
}

static inline void tc358749_init(struct i2c_client *client)
{
    struct device_node *of_node = NULL;
    unsigned int in_irq;
    int ret; 
    int rst;

    if (verified && tsk == NULL) {
        of_node = of_find_compatible_node(NULL, NULL, "hdmi_in_hotplug");
        
        if (of_node) {
            in_irq = irq_of_parse_and_map(of_node, 0);
            pr_info("irq number=%d\n", in_irq);
            in_irq_gpio = of_get_gpio(of_node, 0);
            pr_info("tc358749 request_threaded_irq:%d\n", in_irq_gpio);
            
            if (gpio_request(in_irq_gpio, "hdin_irq") == 0) {
                gpio_direction_input(in_irq_gpio);
                rst = gpio_get_value(in_irq_gpio);
                pr_info("gpio stat: %d; gpio: %d \n", rst, in_irq_gpio);
                
                ret = devm_request_threaded_irq(&client->dev, in_irq, NULL, checkhdmi_input, IRQ_FLAG, "hdin_irq", client->adapter);
                if (ret < 0) {
                    pr_err("request_thread_irq faild , get ret = %d ===========\n", ret);
                }
            }
        }

        tsk = kthread_run(checkhdmi_plug, client->adapter, "checkhdmi_plug_thread");
        if (IS_ERR(tsk)) {
            pr_err("create checkhdmi_plug_thread failed!\n");
        } else {
            pr_info("create checkhdmi_plug_thread succeed!\n");
            ret = switch_dev_register(&sdev_hdmi_audio);
            if (ret < 0) {
                pr_err("%s: register switch %s failed(%d)\n", __func__, &sdev_hdmi_audio.name, ret);
            } else
                switch_set_state(&sdev_hdmi_audio, 0);
            ret = switch_dev_register(&sdev_tc358749);
            if (ret < 0) {
                pr_err("%s: register switch %s failed(%d)\n", __func__, &sdev_tc358749.name, ret);
            } else
                switch_set_state(&sdev_tc358749, 0);
        }
    }
}

static inline void free_thread()
{
    if (verified && tsk != NULL)
    {
        if (!IS_ERR(tsk)) {
            int ret = kthread_stop(tsk);
            switch_dev_unregister(&sdev_hdmi_audio);
            switch_dev_unregister(&sdev_tc358749);
            pr_info("unregist switch %s ==============\n", &sdev_hdmi_audio.name);
            pr_info("unregist switch %s ==============\n", &sdev_tc358749.name);
        }
    }   
}

void sensor_reset(struct i2c_adapter *i2c_adap, struct sensor_pwd_info *spinfo)
{
    if (spinfo->gpio_hdmiin_pwdn.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_pwdn, GPIO_LOW);
    } else {
        set_gpio_level(&spinfo->gpio_rear, GPIO_LOW);
    }
    if (spinfo->gpio_hdmiin_reset.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_reset, GPIO_LOW);
    } else {
        set_gpio_level(&spinfo->gpio_rear_reset, GPIO_LOW);
    }
    if (spinfo->gpio_hdmiin_pwdn.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_pwdn, GPIO_HIGH);
    } else {
        set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
    }
    if (spinfo->gpio_hdmiin_reset.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_reset, GPIO_HIGH);
    } else {
        set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
    }
	camera_write_array(i2c_adap, module_init_regs);
}

void sensor_power_on(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
    spif = spinfo;
    if (spinfo->gpio_hdmiin_pwdn.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_pwdn, GPIO_HIGH);
    } else {
        set_gpio_level(&spinfo->gpio_rear, GPIO_HIGH);
    }
    if (spinfo->gpio_hdmiin_reset.num != -1) {
        set_gpio_level(&spinfo->gpio_hdmiin_reset, GPIO_HIGH);
    } else {
        set_gpio_level(&spinfo->gpio_rear_reset, GPIO_HIGH);
    }
}

void sensor_power_off(bool rear, struct sensor_pwd_info *spinfo, bool hardware)
{
    if (switch_get_state(&sdev_hdmi_audio)) {
        switch_set_state(&sdev_hdmi_audio, 0);
    }       
    if (switch_get_state(&sdev_tc358749)) {
        switch_set_state(&sdev_tc358749, 0);
    }
}

static int camera_i2c_read(struct i2c_adapter *i2c_adap,
			   unsigned int data_width, unsigned int reg,
			   unsigned int *dest)
{
	unsigned char regs_array[4] = { 0, 0, 0, 0 };
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	struct i2c_msg msg;
	int ret = 0;

	if (I2C_REGS_WIDTH == 1)
		regs_array[0] = reg & 0xff;
	if (I2C_REGS_WIDTH == 2) {
		regs_array[0] = (reg >> 8) & 0xff;
		regs_array[1] = reg & 0xff;
	}
	msg.addr = MODULE_I2C_REAL_ADDRESS;
	msg.flags = 0;
	msg.len = I2C_REGS_WIDTH;
	msg.buf = regs_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret < 0) {
		pr_err("write register %s error %d, in line %d reg 0x%x\n", CAMERA_MODULE_NAME, ret, __LINE__, reg);
		return ret;
	}

	msg.flags = I2C_M_RD;
	msg.len = data_width;
	msg.buf = data_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret >= 0) {
		ret = 0;
		if (data_width == 1)
			*dest = data_array[0];
		if (data_width == 2)
			*dest = data_array[1] << 8 | data_array[0];
		if (data_width == 4)
			*dest = data_array[3] << 24 | data_array[2] << 16 | data_array[1] << 8 | data_array[0];
	} else
		pr_err("read register %s error %d", CAMERA_MODULE_NAME, ret);

	return ret;
}

static int camera_i2c_write(struct i2c_adapter *i2c_adap,
			    unsigned int data_width, unsigned int reg,
			    unsigned int data)
{
	unsigned char regs_array[4] = { 0, 0, 0, 0 };
	unsigned char data_array[4] = { 0, 0, 0, 0 };
	unsigned char tran_array[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	struct i2c_msg msg;
	int ret, i;

	if (I2C_REGS_WIDTH == 1)
		regs_array[0] = reg & 0xff;
	if (I2C_REGS_WIDTH == 2) {
		regs_array[0] = (reg >> 8) & 0xff;
		regs_array[1] = reg & 0xff;
	}
	if (data_width == 1)
		data_array[0] = data & 0xff;
	if (data_width == 2) {
		data_array[0] = data & 0xff;
		data_array[1] = (data >> 8) & 0xff;
	}
	if (data_width == 4) {
		data_array[0] = data & 0xff;
		data_array[1] = (data >> 8) & 0xff;
		data_array[2] = (data >> 16) & 0xff;
		data_array[3] = (data >> 24) & 0xff;
	}
	for (i = 0; i < I2C_REGS_WIDTH; i++)
		tran_array[i] = regs_array[i];

	for (i = I2C_REGS_WIDTH; i < (I2C_REGS_WIDTH + data_width); i++)
		tran_array[i] = data_array[i - I2C_REGS_WIDTH];

	msg.addr = MODULE_I2C_REAL_ADDRESS;
	msg.flags = 0;
	msg.len = I2C_REGS_WIDTH + data_width;
	msg.buf = tran_array;
	ret = i2c_transfer(i2c_adap, &msg, 1);
	if (ret > 0)
		ret = 0;
	else if (ret < 0)
		pr_err("write register %s error %d in line %d \n", CAMERA_MODULE_NAME, ret, __LINE__);

	return ret;
}

static int camera_write_array(struct i2c_adapter *i2c_adap,
			      const struct regval_list *vals)
{
	while (vals->reg_num != 0xff) {
		int ret = camera_i2c_write(i2c_adap,
					   vals->data_width,
					   vals->reg_num,
					   vals->value);
		if (ret < 0) {
			pr_err("[camera] i2c write error!,i2c address is %x\n",
				MODULE_I2C_REAL_ADDRESS);
			return ret;
		}
		vals++;
	}
	return 0;
}

static int module_verify_pid(struct i2c_adapter *i2c_adap,
			     struct camera_module_priv *priv)
{
	unsigned int pidh = 0;
	int ret;

	ret = camera_i2c_read(i2c_adap, 2, PIDH, &pidh);

	switch (pidh) {
	case CAMERA_MODULE_PID:
		pr_info("[%s] Product ID verified %x\n",
			CAMERA_MODULE_NAME, pidh);
        verified = true;
		break;

	default:
		pr_err("[%s] Product ID error %x\n",
			CAMERA_MODULE_NAME, pidh);
        verified = false;
		return -ENODEV;
	}
	return ret;
}

static void update_after_init(struct i2c_adapter *i2c_adap)
{
	int ret = 0;
}


static void enter_preview_mode(struct i2c_adapter *i2c_adap)
{

}

static void enter_capture_mode(struct i2c_adapter *i2c_adap)
{

}

static int module_set_mbusformat(struct i2c_client *client,
				 const struct module_color_format *cfmt)
{
	return 0;
}

static int module_s_mirror_flip(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_mirror_flip(struct i2c_client *client, int mf)
{
	return 0;
}

static int module_set_color_format(int mf)
{
	return 0;
}

static int module_set_af_region(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_soft_reset(struct i2c_client *client)
{
	return 0;
}

static int module_set_stream(struct i2c_client *client, int enable)
{
    unsigned int value;
    //if (switch_get_state(&sdev_tc358749) == 0) {
    //    switch_set_state(&sdev_tc358749, 1);
    //}
    if (switch_get_state(&sdev_hdmi_audio) == 0) {
        switch_set_state(&sdev_hdmi_audio, 1);
    }

	return 0;
}

static int module_start_aec(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_freeze_aec(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_save_exposure_param(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_set_exposure_param(struct v4l2_subdev *sd)
{
	return 0;
}

static int module_set_auto_white_balance(struct v4l2_subdev *sd,
					 struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_white_balance_temperature(struct v4l2_subdev *sd,
						struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_exposure_auto(struct v4l2_subdev *sd,
				    struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_scene_exposure(struct v4l2_subdev *sd,
				     struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_ev(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_exposure(struct i2c_client *client, int *val)
{
	return 0;
}

static int module_set_exposure(struct i2c_client *client, int val)
{
	return 0;
}

static int module_get_gain(struct i2c_client *client, int *val)
{
	return 0;
}

static int module_set_gain(struct i2c_client *client, int val)
{
	return 0;
}

static int module_set_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_mode(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_af_status(struct camera_module_priv *priv,
				struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_set_colorfx(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int get_sensor_id(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	ctrl->val = CAMERA_MODULE_PID;
	return ret;
}

static int module_set_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	return 0;
}

static int module_get_power_line(struct v4l2_subdev *sd, struct v4l2_ctrl *ctrl)
{
	ctrl->val = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
	return 0;
}
