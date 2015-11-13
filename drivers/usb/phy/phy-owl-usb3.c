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



static inline u8 owl_phy_readb(void __iomem *base, u32 offset)
{
	return readb(base + offset);
}

static inline void owl_phy_writeb(void __iomem *base, u32 offset, u8 value)
{
	writeb(value, base + offset);
}



#define     USB3_TX_DATA_PATH_CTRL                                            (0X5D)
#define     USB3_RX_DATA_PATH_CTRL1                                          (0X87)

static int s900_usb3phy_init(struct usb_phy *phy)
{
	struct owl_usbphy *sphy = phy_to_sphy(phy);
	void __iomem *base = sphy->regs;
	u8		reg;
	u32		offset;


	/* IO_OR_U8(USB3_TX_DATA_PATH_CTRL, 0x02); */
	offset = USB3_TX_DATA_PATH_CTRL;
	reg = owl_phy_readb(base, offset);
	reg |= 0x02;
	owl_phy_writeb(base, offset, reg);
	printk("%s 0x%x:0x%x\n",__FUNCTION__, offset, owl_phy_readb(base, offset));

	/* IO_OR_U8(USB3_RX_DATA_PATH_CTRL1, 0x20); */
	offset = USB3_RX_DATA_PATH_CTRL1;
	reg = owl_phy_readb(base, offset);
	reg |= 0x20;
	owl_phy_writeb(base, offset, reg);
	printk("%s 0x%x:0x%x\n",__FUNCTION__, offset, owl_phy_readb(base, offset));

	return 0;
}

static int owl_usb3phy_init(struct usb_phy *phy)
{
	struct owl_usbphy *sphy = phy_to_sphy(phy);

	if (sphy->data->ic_type == PHY_S900)
		s900_usb3phy_init(phy);

	return 0;
}

static void owl_usb3phy_shutdown(struct usb_phy *phy)
{
}

static const struct of_device_id owl_usbphy_dt_match[];
static int owl_usb3phy_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(owl_usbphy_dt_match, &pdev->dev);
	struct owl_usbphy *sphy;
	struct device *dev = &pdev->dev;
	struct resource *phy_mem;
	void __iomem	*phy_base;

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
	sphy->phy.label		= "owl-usb3phy";
	sphy->phy.init		= owl_usb3phy_init;
	sphy->phy.shutdown	= owl_usb3phy_shutdown;


	platform_set_drvdata(pdev, sphy);

	return usb_add_phy(&sphy->phy, USB_PHY_TYPE_USB3);
}

static int owl_usb3phy_remove(struct platform_device *pdev)
{
	struct owl_usbphy *sphy = platform_get_drvdata(pdev);

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
		.compatible = "actions,s700-usb3phy",
		.data = &phy_owl_s700_data,
	},
	{
		.compatible = "actions,s900-usb3phy",
		.data = &phy_owl_s900_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, owl_usbphy_dt_match);
#endif


static struct platform_driver owl_usb3phy_driver = {
	.probe		= owl_usb3phy_probe,
	.remove		= owl_usb3phy_remove,
	.driver		= {
		.name	= "owl-usb3phy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(owl_usbphy_dt_match),
	},
};

module_platform_driver(owl_usb3phy_driver);

MODULE_DESCRIPTION("Actions owl USB 3.0 phy controller");
MODULE_AUTHOR("tangshaoqing <tangshaoqing@actions-semi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:owl-usb3phy");
