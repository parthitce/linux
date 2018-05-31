/*
 * Actions USB Stub Gadget Driver
 *
 * Copyright (C) 2018 Actions, Inc.
 * Author: Jinang Lv <lvjinang@actions-semi.com>
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/platform_device.h>

#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"

#include "f_stub.c"

/* Default vendor and product IDs */
#define STUB_VENDOR_ID	0x10d6
#define STUB_PRODUCT_ID	0xff00

/* string IDs */
#define STUB_MANUFACTURER_IDX	0
#define STUB_PRODUCT_IDX	1
#define STUB_SERIAL_IDX	2

static char stub_manufacturer[32] = "Actions, Inc.";
static char stub_product[32] = "USB Gadget Stub";
static char stub_serial[32] = "0123456789ABCDEF";

/* String Table */
static struct usb_string stub_strings_dev[] = {
	[STUB_MANUFACTURER_IDX].s = stub_manufacturer,
	[STUB_PRODUCT_IDX].s = stub_product,
	[STUB_SERIAL_IDX].s = stub_serial,
	{  }			/* end of list */
};

static struct usb_gadget_strings stub_stringtab_dev = {
	.language = 0x0409,	/* en-us */
	.strings = stub_strings_dev,
};

static struct usb_gadget_strings *stub_dev_strings[] = {
	&stub_stringtab_dev,
	NULL,
};

static struct usb_device_descriptor stub_device_desc = {
	.bLength = sizeof(stub_device_desc),
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = cpu_to_le16(0x0200),
	.bDeviceClass = USB_CLASS_PER_INTERFACE,
	.idVendor = cpu_to_le16(STUB_VENDOR_ID),
	.idProduct = cpu_to_le16(STUB_PRODUCT_ID),
	.bcdDevice = cpu_to_le16(0x0100),
	.bNumConfigurations = 1,
};

/* NOTE: USB_CONFIG_ATT_ONE will be set in "Set Configuration" */
static struct usb_configuration stub_config_driver = {
	.label = "usb_stub",
	.bConfigurationValue = 1,
	.bmAttributes = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.MaxPower = 500, /* 500ma */
};



static int stub_do_config(struct usb_configuration *c)
{
	return stub_bind_config(c);
}

static int stub_bind(struct usb_composite_dev *cdev)
{
	int status;

	status = stub_setup();
	if (status)
		return status;

	status = usb_string_ids_tab(cdev, stub_strings_dev);
	if (status < 0)
		return status;
	stub_device_desc.iManufacturer = stub_strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	stub_device_desc.iProduct = stub_strings_dev[USB_GADGET_PRODUCT_IDX].id;
	stub_device_desc.iSerialNumber = stub_strings_dev[USB_GADGET_SERIAL_IDX].id;

	status = usb_add_config(cdev, &stub_config_driver, stub_do_config);
	if (status < 0)
		return status;

	usb_gadget_connect(cdev->gadget);

	return 0;
}

static int stub_unbind(struct usb_composite_dev *cdev)
{
	stub_cleanup();

	return 0;
}

static struct usb_composite_driver stub_driver = {
	.name = "usb_stub",
	.dev = &stub_device_desc,
	.strings = stub_dev_strings,
	.bind = stub_bind,
	.unbind = stub_unbind,
	.max_speed = USB_SPEED_SUPER,
};

static int __init init(void)
{
	return usb_composite_probe(&stub_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&stub_driver);
}
module_exit(cleanup);

MODULE_AUTHOR("Jinang Lv");
MODULE_DESCRIPTION("Actions USB Stub Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
