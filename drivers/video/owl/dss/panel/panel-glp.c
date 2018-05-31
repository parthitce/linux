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
 *	2015/11/30: Created by Lipeng.
 */
#define DEBUGX
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include <video/owl_dss.h>

#define MIPI_MAX_PARS   100
struct glp_init_cmd {
	uint8_t		address;
	uint8_t		parameters[MIPI_MAX_PARS];
	uint8_t		n_parameters;
	uint8_t		delay;
};

struct panel_glp_data {
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	reset_gpio;
	enum owl_dss_state	state;

	struct platform_device	*pdev;
	
	/* others can be added here */
	/* enable cmd */
	struct glp_init_cmd	init_cmd;
	/* disable cmd */
};

static void __panel_glp_power_on(struct panel_glp_data *glp)
{
	/* assert */
	owl_dss_gpio_active(&glp->reset_gpio);

	/* deassert */
	owl_dss_gpio_deactive(&glp->reset_gpio);
	mdelay(100);
	/* assert */
	owl_dss_gpio_active(&glp->reset_gpio);
	udelay(20);

	owl_dss_gpio_active(&glp->power_gpio);
	
	/* deassert */
	owl_dss_gpio_deactive(&glp->reset_gpio);

	mdelay(200);
}

static void __panel_glp_power_off(struct panel_glp_data *glp)
{
	owl_dss_gpio_deactive(&glp->power_gpio);
	/* assert */
	owl_dss_gpio_active(&glp->reset_gpio);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_glp_power_on(struct owl_panel *panel)
{
	struct panel_glp_data *glp = panel->pdata;

	dev_info(&glp->pdev->dev, "%s\n", __func__);

	if (glp->state != OWL_DSS_STATE_ON) {
		__panel_glp_power_on(glp);
		glp->state = OWL_DSS_STATE_ON;
	}

	return 0;
}

static int panel_glp_power_off(struct owl_panel *panel)
{
	struct panel_glp_data *glp = panel->pdata;

	dev_info(&glp->pdev->dev, "%s\n", __func__);

	if (glp->state != OWL_DSS_STATE_OFF) {
		glp->state = OWL_DSS_STATE_OFF;
		__panel_glp_power_off(glp);
	}

	return 0;
}

/*
 * command buffer format:
 *	buffer 0---> cmd delay
 *	buffer 1---> number of parameters
 *	buffer 2---> address
 *
 */
static int panel_glp_enable(struct owl_panel *panel)
{
	struct panel_glp_data 	*glp = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	struct device 		*dev = &glp->pdev->dev;
	struct device_node 	*of_node = dev->of_node;
	struct property 	*prop;
	struct device_node 	*entry;
	struct dsi_cmd 		*tmp_cmd = NULL;
	
	uint8_t 		*buffer, buffer_size;
	int 			i, index, len = 0;
	char 			entryname[64];
	uint8_t 		*ret;

	dev_info(dev, "%s\n", __func__);

	index = 0;
	do {
		snprintf(entryname, sizeof(entryname), "glp_init_cmd-%u", index);
		entry = of_parse_phandle(of_node, entryname, 0);
		if (!entry) {
			pr_info("no entry for %s\n", entryname);
			break;
		} else {
			of_property_read_u32(entry, "address", &glp->init_cmd.address);
			dev_dbg(dev, "address 0x%x\n", glp->init_cmd.address);

			prop = of_find_property(entry, "parameters", &len);
			if (!prop) {
				glp->init_cmd.n_parameters = 0;
				dev_dbg(dev, "this cmd no parameters\n");
			} else {
				ret = of_get_property(entry, "parameters", &len);
				if (len > 0) {
					memcpy(&glp->init_cmd.parameters[0], ret, len);
					for (i = 0; i < len; i++)
						dev_dbg(dev, "parameters 0x%x\n", ret[i]);
				}
				glp->init_cmd.n_parameters = len;
			}
			dev_dbg(dev, "parameters lens %d\n", len);

			of_property_read_u32(entry, "delay", &glp->init_cmd.delay);
			dev_dbg(dev, "delay %d\n", glp->init_cmd.delay);
			
			index++;
		}
		
		buffer_size = 3 + glp->init_cmd.n_parameters;
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if(!buffer) {
			dev_err(dev, "malloc buffer failed!\n");
			return -1;
		}

		buffer[0] = glp->init_cmd.delay;
		buffer[1] = glp->init_cmd.n_parameters;
		buffer[2] = glp->init_cmd.address;

		dev_dbg(dev, "cmd buffer size %d\n", buffer_size);
		dev_dbg(dev, "delay %d, n_parameters %d, address 0x%x\n",
			buffer[0], buffer[1], buffer[2]);

		if (glp->init_cmd.n_parameters > 0)
			memcpy(&buffer[3], &glp->init_cmd.parameters[0],
				glp->init_cmd.n_parameters);

		/* send mipi initail command */
		if (ctrl->ops && ctrl->ops->aux_write)
			ctrl->ops->aux_write(ctrl, (char *)buffer, buffer_size);

		kfree(buffer);
	} while (1);
	
	return 0;
}

static int panel_glp_disable(struct owl_panel *panel)
{
	struct panel_glp_data *glp = panel->pdata;

	return 0;
}
static struct owl_panel_ops panel_glp_panel_ops = {
	.power_on = panel_glp_power_on,
	.power_off = panel_glp_power_off,
	.enable = panel_glp_enable,
	.disable = panel_glp_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_glp_of_match[] = {
	{
		.compatible	= "actions,panel-glp",
	},
	{},
};
static int panel_parse_info(struct device_node *of_node, struct device *dev,
				struct owl_panel *panel, struct panel_glp_data *glp)
{
	int  ret;
	pr_info("%s\n", __func__);

	/*
	 * parse glp panel power on gpio,  It is not necessary!!!
	 *
	 * */
	/* parse power gpio ... */
	ret = owl_dss_gpio_parse(of_node, "power-gpio", &glp->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
	} else {
		ret = devm_gpio_request(dev, glp->power_gpio.gpio, NULL);
		if (ret < 0)
			dev_err(dev, "request power_gpio failed: %d\n", ret);
	}
	/* parse reset gpio ... */
	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &glp->reset_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
	} else {
		ret = devm_gpio_request(dev, glp->reset_gpio.gpio, NULL);
		if (ret < 0)
			dev_err(dev, "request reset_gpio failed: %d\n", ret);
	}
	return 0;
}


static int panel_glp_probe(struct platform_device *pdev)
{
	int ret = 0;

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_glp_data *glp;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_glp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	glp = devm_kzalloc(dev, sizeof(*glp), GFP_KERNEL);
	if (!glp) {
		dev_err(dev, "alloc glp failed\n");
		return -ENOMEM;
	}
	glp->pdev = pdev;

	panel = owl_panel_alloc("lcd", OWL_DISPLAY_TYPE_LCD);
	if (panel) {
		dev_set_drvdata(dev, panel);

		panel_parse_info(of_node, dev, panel, glp);

		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser dss device\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc panel\n", __func__);
		return -ENOMEM;
	}
	panel->desc.ops = &panel_glp_panel_ops;
	panel->pdata = glp;

	glp->state = OWL_DSS_STATE_OFF;

	return 0;
}

static int panel_glp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_glp_power_off(panel);

	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_glp_driver = {
	.probe			= panel_glp_probe,
	.remove			= panel_glp_remove,
	.driver = {
		.name		= "panel-glp",
		.owner		= THIS_MODULE,
		.of_match_table	= panel_glp_of_match,
	},
};

int __init panel_glp_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = platform_driver_register(&panel_glp_driver);
	if (ret)
		pr_err("Failed to register platform driver\n");

	return ret;
}

void __exit panel_glp_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_glp_driver);
}

module_init(panel_glp_init);
module_exit(panel_glp_exit);
MODULE_LICENSE("GPL");
