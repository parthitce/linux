/*
 * Actions Devices VUI board(ATT3008/ATT3006) I2C-SPI driver
 *
 * Copyright 2017 Actions-semi Inc.
 * Author: Yiguang <liuyiguang@actions-semi.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/i2c.h>
#include "module_att3008.h"

static inline int vui_reg_read(struct i2c_client *client, unsigned int reg)
{
    int val_read, val;

    val_read = i2c_smbus_read_word_data(client, reg);
    if (val < 0)
        pr_err("failed to read 0x%x!\n", reg);

    val = ((val_read >> 8) & 0xff) | ((val_read << 8) & 0xff00);
    return val;
}

static inline int vui_reg_write(struct i2c_client *client, unsigned int reg, unsigned int val)
{
    int ret, val_write;

    val_write = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
    ret = i2c_smbus_write_word_data(client, reg, val_write);
    if (ret < 0)
        pr_err("failed to write 0x%x!\n", reg);

    return ret;
}

static int vui_reg_write_array(struct i2c_client *client, const struct regval_list *vals)
{
	while (vals->reg != 0xff) {
        int ret = vui_reg_write(client, vals->reg, vals->value);
        if (ret < 0) {
            pr_err("failed to write 0x%x!\n", vals->reg);
            return ret;
        }
        vals++;
	}

	return 0;
}

static int vui_check_chipid(struct i2c_client *client)
{
    int ret = 0;
    int chipid = 0;

    chipid = vui_reg_read(client, ATT3008_CHIPID_REG);
    //pr_err("get id = 0x%x =====\n", chipid);
    if (chipid != ATT3008_CHIPID_VAL) {
        pr_err("Get chipid failed! Maybe i2c transfer failed, please check!\n");
        return -1;
    }

    return 0;
}

static int vui_init_regs(struct i2c_client *client, u32 rate, u32 clk)
{
    int ret = 0;

    if (clk == 24)
        ret = vui_reg_write_array(client, module_adcclk_24m_source);
    else
        ret = vui_reg_write_array(client, module_adcclk_26m_source);

    switch (rate) {
        case 8000:
            ret = vui_reg_write_array(client, module_adcclk_8khz_init);
            break;
        case 12000:
            ret = vui_reg_write_array(client, module_adcclk_12khz_init);
            break;
        case 16000:
            ret = vui_reg_write_array(client, module_adcclk_16khz_init);
            break;
        case 24000:
            ret = vui_reg_write_array(client, module_adcclk_24khz_init);
            break;
        case 32000:
            ret = vui_reg_write_array(client, module_adcclk_32khz_init);
            break;
        case 48000:
            ret = vui_reg_write_array(client, module_adcclk_48khz_init);
            break;
        case 11025:
            ret = vui_reg_write_array(client, module_adcclk_11khz_init);
            break;
        case 22050:
            ret = vui_reg_write_array(client, module_adcclk_22khz_init);
            break;
        case 44100:
            ret = vui_reg_write_array(client, module_adcclk_44khz_init);
            break;
        default:
            return -1;
    }

    ret = vui_reg_write_array(client, module_gpio_cfg);
    ret = vui_reg_write_array(client, module_adc_cfg);
	msleep(100);
    ret = vui_reg_write_array(client, module_adc_init);
    ret = vui_reg_write_array(client, module_ch_init);

    return ret;
}

static int vui_audio_start(struct i2c_client *client)
{
    return vui_reg_write_array(client, module_audio_if_start);
}
