/*
 * Actions OWL SoCs phy driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * tangshaoqing <tangshaoqing@actions-semi.com>
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


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "phy-owl-usb.h"

struct owl_usbphy *usb2_sphy;

static void setphy(struct owl_usbphy *sphy, unsigned char reg_add, unsigned char value)
{
	void __iomem *usb3_usb_vcon = sphy->regs;
	volatile unsigned char addr_low;
	volatile unsigned char addr_high;
	volatile unsigned int vstate;

	addr_low =  reg_add & 0x0f;
	addr_high =  (reg_add >> 4) & 0x0f;

	vstate = value;
	vstate = vstate << 8;

	addr_low |= 0x10;
	writel(vstate | addr_low, usb3_usb_vcon);
	mb();

	addr_low &= 0x0f; 
	writel(vstate | addr_low, usb3_usb_vcon);
	mb();

	addr_low |= 0x10;
	writel(vstate | addr_low, usb3_usb_vcon);
	mb();

	addr_high |= 0x10;
	writel(vstate | addr_high, usb3_usb_vcon);
	mb();

	addr_high &= 0x0f; 
	writel(vstate | addr_high, usb3_usb_vcon);
	mb();

	addr_high |= 0x10;
	writel(vstate | addr_high, usb3_usb_vcon);  
	mb();
	return;
}


#define SET_PHY_FROM_CONFIG_FILE
#undef SET_PHY_FROM_CONFIG_FILE

#ifdef SET_PHY_FROM_CONFIG_FILE
void phy_debug_setphy(unsigned char reg_add, unsigned char value)
{
	if (usb2_sphy)
		setphy(usb2_sphy, reg_add, value);
}

extern int set_phy_from_config_file(char *file_path);
static void dwc3_phy_setup_from_config_file(int is_device_mode)
{
	if (is_device_mode)
		set_phy_from_config_file("/misc/modules/phy_config_dwc3");
	else
		set_phy_from_config_file("/misc/modules/phy_config_xhci");

	udelay(100);

	return;
}
#endif


static int s900_usb2phy_param_setup(struct owl_usbphy *sphy, int is_device_mode)
{
#ifdef SET_PHY_FROM_CONFIG_FILE
	dwc3_phy_setup_from_config_file(is_device_mode);
	return 0;
#endif

	if (is_device_mode) {
		printk("%s device mode\n", __FUNCTION__);
		
		setphy(sphy, 0xe7, 0x1b);
		setphy(sphy, 0xe7,0x1f);

		udelay(10);

		setphy(sphy, 0xe2,0x48);
		/* setphy(sphy, 0xe5, 0x00); */
		setphy(sphy, 0xe0, 0xa3);
		setphy(sphy, 0x87, 0x1f);
	} else {
		printk("%s host mode\n", __FUNCTION__);
		
		setphy(sphy, 0xe7, 0x1b);
		setphy(sphy, 0xe7, 0x1f);

		udelay(10);

		setphy(sphy, 0xe2,0x46);
		/* setphy(sphy, 0xe5, 0x00); */
		setphy(sphy, 0xe0, 0xa3);
		setphy(sphy, 0x87, 0x1f);
	}
	
	return 0;
}

int owl_dwc3_usb2phy_param_setup(int is_device_mode)
{
	int ret = 0;

	if (!usb2_sphy)
		return 0;

	if (usb2_sphy->data->ic_type == PHY_S900)
		ret = s900_usb2phy_param_setup(usb2_sphy, is_device_mode);

	return ret;
}
EXPORT_SYMBOL_GPL(owl_dwc3_usb2phy_param_setup);


static int owl_usb2phy_init(struct usb_phy *phy)
{
	return 0;
}

static void owl_usb2phy_shutdown(struct usb_phy *phy)
{
}

static const struct of_device_id owl_usbphy_dt_match[];
static int owl_usb2phy_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(owl_usbphy_dt_match, &pdev->dev);
	struct owl_usbphy *sphy;
	struct device *dev = &pdev->dev;
	struct resource *phy_mem;
	void __iomem	*phy_base;
	int ret = 0;
	
	printk("%s %d:%s %s\n", __FUNCTION__, __LINE__, __DATE__, __TIME__);

	if (!of_id) {
		dev_err(dev, "no compatible OF match\n");
		return -EINVAL;
	}

	phy_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy_base = devm_ioremap_nocache(dev, phy_mem->start, resource_size(phy_mem));
	if (!phy_base) {
		dev_err(dev, "ioremap failed\n");
		return -ENOMEM;
	}

	sphy = devm_kzalloc(dev, sizeof(*sphy), GFP_KERNEL);
	if (!sphy)
		return -ENOMEM;


	sphy->dev = dev;
	sphy->data	= (struct phy_owl_data  *)of_id->data;

	sphy->regs		= phy_base;
	sphy->phy.dev		= sphy->dev;
	sphy->phy.label		= "owl-usb2phy";
	sphy->phy.init		= owl_usb2phy_init;
	sphy->phy.shutdown	= owl_usb2phy_shutdown;


	platform_set_drvdata(pdev, sphy);

	ATOMIC_INIT_NOTIFIER_HEAD(&sphy->phy.notifier);


	ret = usb_add_phy(&sphy->phy, USB_PHY_TYPE_USB2);
	if(ret) {
		return ret;
	}

	usb2_sphy = sphy;
	
	return ret;
	
}

static int owl_usb2phy_remove(struct platform_device *pdev)
{
	struct owl_usbphy *sphy = platform_get_drvdata(pdev);

	usb2_sphy = 0;
	usb_remove_phy(&sphy->phy);

	return 0;
}


#ifdef CONFIG_OF
static  struct phy_owl_data  phy_owl_s700_data = {
	.ic_type = PHY_S700,
};

static  struct phy_owl_data  phy_owl_s900_data = {
	.ic_type = PHY_S900,
};

static const struct of_device_id owl_usbphy_dt_match[] = {
	{
		.compatible = "actions,s700-usb2phy",
		.data = &phy_owl_s700_data,
	},
	{
		.compatible = "actions,s900-usb2phy",
		.data = &phy_owl_s900_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, owl_usbphy_dt_match);
#endif


static struct platform_driver owl_usb2phy_driver = {
	.probe		= owl_usb2phy_probe,
	.remove		= owl_usb2phy_remove,
	.driver		= {
		.name	= "owl-usb2phy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(owl_usbphy_dt_match),
	},
};

module_platform_driver(owl_usb2phy_driver);

MODULE_DESCRIPTION("Actions owl USB 2.0 phy controller");
MODULE_AUTHOR("tangshaoqing <tangshaoqing@actions-semi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:owl-usb2phy");
