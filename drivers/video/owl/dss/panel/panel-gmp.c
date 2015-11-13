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


struct panel_gmp_data {
	struct owl_dss_gpio	reset_gpio;
	struct owl_dss_gpio	power_gpio;
	enum owl_dss_state	state;

	struct platform_device *pdev;
	/* others can be added here */
};

static void __panel_gmp_power_on(struct panel_gmp_data *gmp)
{
	pr_info("%s\n", __func__);
	/*owl_dss_gpio_active(&gmp->reset_gpio);*/
	owl_dss_gpio_active(&gmp->power_gpio);
	/*owl_dss_gpio_deactive(&gmp->reset_gpio);*/
}

static void __panel_gmp_power_off(struct panel_gmp_data *gmp)
{
	owl_dss_gpio_deactive(&gmp->power_gpio);
}

/*==============================================================
			dss device ops
 *============================================================*/
static int panel_gmp_power_on(struct owl_panel *panel)
{
	struct panel_gmp_data *gmp = panel->pdata;

	dev_info(&gmp->pdev->dev, "%s\n", __func__);
	printk("zhizheng = %d", &gmp->pdev->dev);
	if (gmp->state != OWL_DSS_STATE_ON) {
		__panel_gmp_power_on(gmp);
		gmp->state = OWL_DSS_STATE_ON;
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

static struct owl_panel_ops panel_gmp_panel_ops = {
	.power_on = panel_gmp_power_on,
	.power_off = panel_gmp_power_off,
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

static int panel_gmp_probe(struct platform_device *pdev)
{
	int ret = 0;

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
		pr_err("Failed to initialize edp platform driver\n");
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
