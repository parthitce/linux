/*
 * drivers/video/owl/display/dsi/panel_gmp.c
 *
 * Generic MIPI Panel(gmp).
 *
 * Author: Lipeng<lipeng@actions-semi.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/bootafinfo.h>
#include <video/owl_dss.h>

#define MIPI_MAX_PARS   100
struct dsi_cmd {
	uint8_t		data_type;
	uint8_t		address;
	uint8_t		parameters[MIPI_MAX_PARS];
	uint8_t		n_parameters;
	uint8_t		delay;
};

struct dsi_init_cmd {
	uint32_t cmd_nums;
	uint32_t *mipi_cmd;
};
struct panel_gmp_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	power_gpio;
	struct owl_dss_gpio	power1_gpio;
	enum owl_dss_state	state;

	struct platform_device *pdev;

	/* Specific data can be added here */
	struct dsi_cmd			*init_cmd_list;
	uint32_t			n_init_cmd_list;

	struct dsi_cmd			*disable_cmd_list;
	uint32_t			n_disable_cmd_list;
};

static void __panel_gmp_power_on(struct panel_gmp_data *gmp)
{
	pr_info("%s ... ...\n", __func__);

	owl_dss_gpio_active(&gmp->reset_gpio);
	mdelay(10);

	owl_dss_gpio_active(&gmp->power_gpio);
	owl_dss_gpio_active(&gmp->power1_gpio);
	mdelay(10);

	owl_dss_gpio_deactive(&gmp->reset_gpio);
	mdelay(10);
}

static void __panel_gmp_power_off(struct panel_gmp_data *gmp)
{
	pr_info("%s ... ...\n", __func__);
	owl_dss_gpio_deactive(&gmp->reset_gpio);
	mdelay(10);

	owl_dss_gpio_deactive(&gmp->power_gpio);
	owl_dss_gpio_deactive(&gmp->power1_gpio);
	mdelay(10);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_gmp_power_on(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);

	dev_info(&gmp->pdev->dev, "%s, ctrl_is_enabled? %d\n",
				__func__, ctrl_is_enabled);

	if (gmp->state != OWL_DSS_STATE_ON) {
		gmp->state = OWL_DSS_STATE_ON;
		if (!ctrl_is_enabled)
			__panel_gmp_power_on(gmp);
	}
	pr_info("%s end\n", __func__);
	return 0;
}

static int panel_gmp_power_off(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;

	dev_info(&gmp->pdev->dev, "%s\n", __func__);

	if (gmp->state != OWL_DSS_STATE_OFF) {
		gmp->state = OWL_DSS_STATE_OFF;
		__panel_gmp_power_off(gmp);
	}

	return 0;
}

static int panel_gmp_enable(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct device *dev = &gmp->pdev->dev;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	bool ctrl_is_enabled = false;

	uint8_t *buffer, buffer_size;
	struct dsi_cmd *tmp_cmd = NULL;
	int i;

	dev_info(dev, "%s\n", __func__);

	/* get controller status */
	ctrl_is_enabled = ctrl->ops->ctrl_is_enabled(ctrl);
	if (ctrl_is_enabled)
		return 0;

	tmp_cmd = gmp->init_cmd_list;

	for (i = 0; i < gmp->n_init_cmd_list; i++) {

		buffer_size = 4 + tmp_cmd[i].n_parameters;
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if(!buffer) {
			dev_err(dev, "malloc buffer failed!\n");
			return -1;
		}

		buffer[0] = tmp_cmd[i].delay;
		buffer[1] = tmp_cmd[i].data_type;
		buffer[2] = tmp_cmd[i].n_parameters;
		buffer[3] = tmp_cmd[i].address;

		dev_dbg(dev, "cmd buffer size %d\n", buffer_size);
		dev_dbg(dev, "data type 0x%x, n_parameters %d, address 0x%x\n",
			buffer[1], buffer[2], buffer[3]);

		memcpy(&buffer[4], &tmp_cmd[i].parameters,
					tmp_cmd[i].n_parameters);

		/* send mipi initail command */
		if (ctrl->ops && ctrl->ops->aux_write)
			ctrl->ops->aux_write(ctrl, (char *)buffer, buffer_size);

		kfree(buffer);
	}

	return 0;
}
static int panel_gmp_disable(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;
	struct device *dev = &gmp->pdev->dev;
	struct owl_display_ctrl *ctrl = panel->ctrl;
	uint8_t *buffer, buffer_size;
	struct dsi_cmd *tmp_cmd = NULL;
	int i;

	dev_info(dev, "%s\n", __func__);

	tmp_cmd = gmp->disable_cmd_list;

	for (i = 0; i < gmp->n_disable_cmd_list; i++) {

		buffer_size = 4 + tmp_cmd[i].n_parameters;
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if(!buffer) {
			dev_err(dev, "malloc buffer failed!\n");
			return -1;
		}

		buffer[0] = tmp_cmd[i].delay;
		buffer[1] = tmp_cmd[i].data_type;
		buffer[2] = tmp_cmd[i].n_parameters;
		buffer[3] = tmp_cmd[i].address;

		dev_dbg(dev, "cmd buffer size %d\n", buffer_size);
		dev_dbg(dev, "data type 0x%x, n_parameters %d, address 0x%x\n",
			buffer[1], buffer[2], buffer[3]);

		memcpy(&buffer[4], &tmp_cmd[i].parameters,
					tmp_cmd[i].n_parameters);

		/* send mipi initail command */
		if (ctrl->ops && ctrl->ops->aux_write)
			ctrl->ops->aux_write(ctrl, (char *)buffer, buffer_size);

		kfree(buffer);
	}

	return 0;
}

static struct owl_panel_ops panel_gmp_panel_ops = {
	.power_on = panel_gmp_power_on,
	.power_off = panel_gmp_power_off,

	.enable = panel_gmp_enable,
	.disable = panel_gmp_disable,
};

/*==============================================================
			platform device ops
 *============================================================*/
static struct of_device_id panel_gmp_of_match[] = {
	{
		.compatible	= "actions,panel-gmp",
	},
	{},
};
static int panel_parse_info(struct device_node *of_node, struct device *dev,
		struct owl_panel *panel, struct panel_gmp_data *gmp)
{
	struct property *prop;

	struct dsi_cmd *cmd = NULL, *temp = NULL;
	char entryname[64];
	int index, ret, len, i;
	uint32_t byte_lens;
	struct device_node *entry;
	pr_info("%s\n", __func__);

	/* parse mipi initial cmd ... */
	dev_dbg(dev, "parse mipi initial cmd\n");
	index = 0;
	do {
		snprintf(entryname, sizeof(entryname), "mipi_init_cmd-%u", index);
		entry = of_parse_phandle(of_node, entryname, 0);
		if (!entry) {
			pr_info("no entry for %s\n", entryname);
			break;
		} else {
			temp = krealloc(cmd, (index + 1) * sizeof(*cmd), GFP_KERNEL);
                        if (!temp) {
				dev_err(dev, "krealloc(cmd) failed\n");
				return -ENOMEM;
			}

			of_property_read_u32(entry, "data_type", &temp[index].data_type);
			of_property_read_u32(entry, "address", &temp[index].address);

			dev_dbg(dev, "data_type 0x%x\n", temp[index].data_type);
			dev_dbg(dev, "address 0x%x\n", temp[index].address);

			prop = of_find_property(entry, "parameters", &len);
			if (!prop) {
				dev_dbg(dev, "Can not read property: parameters\n");
				return -EINVAL;
			}
			/*
			ret = of_property_read_u8_array(of_node,
					"parameters", &temp[index].parameters[0],
					len);
			*/
			uint8_t *ret;
			ret = of_get_property(entry, "parameters", &len);
			for (i = 0; i < len; i++)
				dev_dbg(dev, "parameters 0x%x\n", ret[i]);
			memcpy(&temp[index].parameters[0], ret, len);

			temp[index].n_parameters = len;
			dev_dbg(dev, "parameters lens %d\n", len);

			of_property_read_u32(entry, "delay", &temp[index].delay);
			dev_dbg(dev, "delay %d\n", temp[index].delay);

			cmd = temp;
			index++;
		}
	} while (1);

	gmp->init_cmd_list = cmd;
	gmp->n_init_cmd_list = index;
	dev_dbg(dev, "p init_cmd_list 0x%x\n", gmp->init_cmd_list);
	dev_dbg(dev, "n_init_cmd_list %d\n", gmp->n_init_cmd_list);

	/* parse mipi disable cmd ... */
	index = 0;
	cmd = NULL;
	temp = NULL;

	dev_dbg(dev, "parse mipi disable cmd\n");
	do {
		snprintf(entryname, sizeof(entryname), "mipi_dis_cmd-%u", index);
		entry = of_parse_phandle(of_node, entryname, 0);
		if (!entry) {
			pr_info("no entry for %s\n", entryname);
			break;
		} else {
			temp = krealloc(cmd, (index + 1) * sizeof(*cmd), GFP_KERNEL);
	 		if (!temp) {
				dev_err(dev, "krealloc(cmd) failed\n");
				return -ENOMEM;
			}

			of_property_read_u32(entry, "data_type", &temp[index].data_type);
			of_property_read_u32(entry, "address", &temp[index].address);

			dev_dbg(dev, "data_type 0x%x\n", temp[index].data_type);
			dev_dbg(dev, "address 0x%x\n", temp[index].address);

			prop = of_find_property(entry, "parameters", &len);
			if (!prop) {
				dev_dbg(dev, "Can not read property: parameters\n");
				return -EINVAL;
			}

			/*
			ret = of_property_read_u32_array(of_node,
					"parameters", &temp[index].parameters,
					len);
			*/

			uint8_t *ret;
			ret = of_get_property(entry, "parameters", &len);
			for (i = 0; i < len; i++)
				dev_dbg(dev, "parameters 0x%x\n", ret[i]);
			memcpy(&temp[index].parameters[0], ret, len);
			temp[index].n_parameters = len;
			dev_dbg(dev, "parameters lens %d\n", len);

			of_property_read_u32(entry, "delay", &temp[index].delay);
			dev_dbg(dev, "delay %d\n", temp[index].delay);

			cmd = temp;
			index++;
		}
	} while (1);

	gmp->disable_cmd_list = cmd;
	gmp->n_disable_cmd_list = index;
	dev_dbg(dev, "p disable_cmd_list 0x%x\n", gmp->disable_cmd_list);
	dev_dbg(dev, "n_disable_cmd_list %d\n", gmp->n_disable_cmd_list);

	/*
	 * parse mipi panel power on gpio,  It is not necessary!!!
	 *
	 * */
	/* parse power gpio ... */
	ret = owl_dss_gpio_parse(of_node, "power-gpio", &gmp->power_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->power_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse reset gpio ... */
	ret = owl_dss_gpio_parse(of_node, "reset-gpio", &gmp->reset_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse reset_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->reset_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request reset_gpio failed: %d\n", ret);
		return ret;
	}
	/* parse power1 gpio ...*/
	ret = owl_dss_gpio_parse(of_node, "power1-gpio", &gmp->power1_gpio);
	if (ret < 0) {
		dev_err(dev, "%s, parse power1_gpio failed(%d)\n", __func__,
			ret);
		return ret;
	}
	ret = devm_gpio_request(dev, gmp->power1_gpio.gpio, NULL);
	if (ret < 0) {
		dev_err(dev, "request power1_gpio failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int panel_gmp_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	struct panel_gmp_data *gmp;
	struct owl_panel *panel;

	dev_info(dev, "%s\n", __func__);

	match = of_match_device(of_match_ptr(panel_gmp_of_match), dev);
	if (!match) {
		dev_err(dev, "Error: No device match found\n");
		return -ENODEV;
	}

	gmp = devm_kzalloc(dev, sizeof(*gmp), GFP_KERNEL);
	if (!gmp) {
		dev_err(dev, "alloc gmp failed\n");
		return -ENOMEM;
	}
	gmp->pdev = pdev;
	panel = owl_panel_alloc("mipi", OWL_DISPLAY_TYPE_DSI);
	if (panel) {
		dev_set_drvdata(dev, panel);

		panel_parse_info(of_node, dev, panel, gmp);

		owl_panel_parse_panel_info(of_node, panel);

		if (owl_panel_register(dev, panel) < 0) {
			dev_err(dev, "%s, fail to regitser dss device\n",
				__func__);
			owl_panel_free(panel);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "%s, fail to alloc dss device\n", __func__);
		return -ENOMEM;
	}
	panel->desc.ops = &panel_gmp_panel_ops;
	panel->pdata = gmp;

	gmp->state = OWL_DSS_STATE_OFF;
	return 0;
}

static int panel_gmp_remove(struct platform_device *pdev)
{
	struct owl_panel *panel;

	dev_info(&pdev->dev, "%s\n", __func__);

	panel = dev_get_drvdata(&pdev->dev);

	panel_gmp_power_off(panel);
	owl_panel_unregister(panel);
	owl_panel_free(panel);

	return 0;
}

static struct platform_driver panel_gmp_driver = {
	.probe          = panel_gmp_probe,
	.remove         = panel_gmp_remove,
	.driver         = {
		.name   = "panel-gmp",
		.owner  = THIS_MODULE,
		.of_match_table	= panel_gmp_of_match,
	},
};

int __init panel_gmp_init(void)
{
	int r = 0;

	pr_info("%s\n", __func__);
	r = platform_driver_register(&panel_gmp_driver);
	if (r)
		pr_err("Failed to initialize dsi platform driver\n");
	return r;
}

void __exit panel_gmp_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&panel_gmp_driver);
}

module_init(panel_gmp_init);
module_exit(panel_gmp_exit);
MODULE_LICENSE("GPL");
