/*
 * f_stub.c  --  Actions USB Stub Gadget Driver
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

/*
 * NOTE: The USB Stub Protocol is defined by Actions, Inc.
 * In my opinion, it is not simple and kind of weird, but I
 * have no choice apparently case it has been used for a
 * really long time.
 */

/*
 * NOTICE: The direction of data transfer for USB Stub is different.
 * "Read" means Stub reads the data from PC, and uses USB OUT EP.
 * "Write" means Stub write the data to PC, and uses USB IN EP.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/bitmap.h>
#include <linux/interrupt.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/ch9.h>

#include "f_stub.h"

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#include <linux/uaccess.h>
#endif

static const char stub_shortname[] = "stub_usb";

enum stub_state {
	/* This one isn't used anywhere */
	STUB_STATE_COMMAND_PHASE = -10,
	STUB_STATE_DATA_PHASE,
	STUB_STATE_STATUS_PHASE,

	STUB_STATE_IDLE = 0,
	STUB_STATE_TERMINATED
};

enum stub_buffer_state {
	BUF_STATE_EMPTY = 0,
	BUF_STATE_FULL,
	BUF_STATE_BUSY
};

struct stub_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;

	/* synchronize access to our device file */
	atomic_t open_excl;
	/* to enforce only one ioctl at a time */
	atomic_t ioctl_excl;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	wait_queue_head_t in_wq;
	wait_queue_head_t out_wq;

	struct usb_request *req_in;
	struct usb_request *req_out;

	enum stub_buffer_state in_state;
	enum stub_buffer_state out_state;
};

static struct usb_interface_descriptor stub_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol = 0xff,
};

static struct usb_endpoint_descriptor stub_fs_bulk_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by usb_ep_autoconfig() */
};

static struct usb_endpoint_descriptor stub_fs_bulk_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by usb_ep_autoconfig() */
};

static struct usb_descriptor_header *fs_stub_descs[] = {
	(struct usb_descriptor_header *) &stub_interface_desc,
	(struct usb_descriptor_header *) &stub_fs_bulk_in_desc,
	(struct usb_descriptor_header *) &stub_fs_bulk_out_desc,
	NULL,
};

static struct usb_endpoint_descriptor stub_hs_bulk_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	/* .bEndpointAddress       = USB_DIR_IN, */
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_endpoint_descriptor stub_hs_bulk_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	/* .bEndpointAddress       = USB_DIR_OUT, */
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_stub_descs[] = {
	(struct usb_descriptor_header *) &stub_interface_desc,
	(struct usb_descriptor_header *) &stub_hs_bulk_in_desc,
	(struct usb_descriptor_header *) &stub_hs_bulk_out_desc,
	NULL,
};

static struct usb_endpoint_descriptor stub_ss_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor stub_ss_bulk_in_comp_desc = {
	.bLength =		sizeof(stub_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =		0, */
};

static struct usb_endpoint_descriptor stub_ss_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor stub_ss_bulk_out_comp_desc = {
	.bLength =		sizeof(stub_ss_bulk_out_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
};

static struct usb_descriptor_header *ss_stub_descs[] = {
	(struct usb_descriptor_header *) &stub_interface_desc,
	(struct usb_descriptor_header *) &stub_ss_bulk_in_desc,
	(struct usb_descriptor_header *) &stub_ss_bulk_in_comp_desc,
	(struct usb_descriptor_header *) &stub_ss_bulk_out_desc,
	(struct usb_descriptor_header *) &stub_ss_bulk_out_comp_desc,
	NULL,
};

/* Global Variables */
static struct stub_dev *_stub_dev;

static inline int stub_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void stub_unlock(atomic_t *excl)
{
	if (atomic_read(excl) != 0)
		atomic_dec(excl);
}

static inline struct stub_dev *func_to_stub(struct usb_function *f)
{
	return container_of(f, struct stub_dev, function);
}

static struct usb_request *stub_request_new(struct usb_ep *ep, int size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	/* Allocate buffers for the requests */
	req->buf = kmalloc(size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void stub_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static void stub_bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct stub_dev *stub = req->context;

	pr_debug("%s status %d, actual %d, length: %d\n",
		__func__, req->status, req->actual, req->length);

	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	stub->in_state = BUF_STATE_EMPTY;
	wake_up(&stub->in_wq);
}

static void stub_bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct stub_dev *stub = req->context;

	pr_debug("%s status %d, actual %d, length: %d\n",
		__func__, req->status, req->actual, req->length);

	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	stub->out_state = BUF_STATE_FULL;
	wake_up(&stub->out_wq);
}

static int stub_function_bind(struct usb_configuration *c,
				struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct stub_dev *stub = func_to_stub(f);
	struct usb_ep *ep;
	struct usb_request *req;
	int i;

	/* New interface */
	i = usb_interface_id(c, f);
	if (i < 0)
		return i;
	stub_interface_desc.bInterfaceNumber = i;

	/* Allocate our endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &stub_fs_bulk_in_desc);
	if (!ep)
		goto fail;
	ep->driver_data = stub;	/* claim the endpoint */
	stub->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &stub_fs_bulk_out_desc);
	if (!ep)
		goto fail;
	ep->driver_data = stub;	/* claim the endpoint */
	stub->ep_out = ep;

	/* High-speed */
	stub_hs_bulk_in_desc.bEndpointAddress =
		stub_fs_bulk_in_desc.bEndpointAddress;
	stub_hs_bulk_out_desc.bEndpointAddress =
		stub_fs_bulk_out_desc.bEndpointAddress;

	/* Super-speed */
	stub_ss_bulk_in_desc.bEndpointAddress =
		stub_fs_bulk_in_desc.bEndpointAddress;
	stub_ss_bulk_out_desc.bEndpointAddress =
		stub_fs_bulk_out_desc.bEndpointAddress;

	/* Allocate ep->maxpacket to hold transfer buffer */
	req = stub_request_new(stub->ep_in, stub->ep_in->maxpacket);
	if (req) {
		req->context = stub;
		req->complete = stub_bulk_in_complete;
	} else
		goto fail;
	stub->req_in = req;

	/*
	 * NOTE: If failed to request here, just return and it will be fine
	 * because stub_function_unbind() will be called if bind failed.
	 */
	req = stub_request_new(stub->ep_out, stub->ep_out->maxpacket);
	if (req) {
		req->context = stub;
		req->complete = stub_bulk_out_complete;
	} else
		goto fail;
	stub->req_out = req;

	return 0;

fail:
	return -ENOTSUPP;
}

static void stub_function_unbind(struct usb_configuration *c,
				struct usb_function *f)
{
	struct stub_dev *stub = func_to_stub(f);

	stub_request_free(stub->req_in, stub->ep_in);
	stub_request_free(stub->req_out, stub->ep_out);

	stub->req_in = NULL;
	stub->req_out = NULL;
	stub->ep_in = NULL;
	stub->ep_out = NULL;
}

static int stub_function_setup(struct usb_function *f,
				const struct usb_ctrlrequest *ctrl)
{
	pr_info("stub_function_setup: not implemented\n");
	return -EINVAL;
}

static int stub_function_set_alt(struct usb_function *f,
				unsigned intf, unsigned alt)
{
	struct stub_dev *stub = func_to_stub(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	pr_info("%s intf %d, alt %d\n", __func__, intf, alt);

	/*
	 * NOTE: Don't need to take care of errors, because USB Stub
	 * don't support "Set Interface" and "Set Configuraion" will
	 * take care of errors by gadget core.
	 */
	ret = config_ep_by_speed(cdev->gadget, f, stub->ep_in);
	if (ret)
		return ret;
	usb_ep_enable(stub->ep_in);

	ret = config_ep_by_speed(cdev->gadget, f, stub->ep_out);
	if (ret)
		return ret;
	usb_ep_enable(stub->ep_out);

	return 0;
}

static void stub_function_disable(struct usb_function *f)
{
	struct stub_dev *stub = func_to_stub(f);

	usb_ep_disable(stub->ep_in);
	stub->ep_in->driver_data = NULL;

	usb_ep_disable(stub->ep_out);
	stub->ep_out->driver_data = NULL;
}

static int stub_bind_config(struct usb_configuration *c)
{
	struct stub_dev *dev = _stub_dev;

	dev->cdev = c->cdev;
	dev->function.name = "f_stub";
	dev->function.fs_descriptors = fs_stub_descs;
	dev->function.hs_descriptors = hs_stub_descs;
	dev->function.ss_descriptors = ss_stub_descs;
	dev->function.bind = stub_function_bind;
	dev->function.unbind = stub_function_unbind;
	dev->function.setup = stub_function_setup;
	dev->function.set_alt = stub_function_set_alt;
	dev->function.disable = stub_function_disable;

	return usb_add_function(c, &dev->function);
}

static short int stub_checksum(void *buf, int size)
{
	short int *tmp = buf;
	short int sum = 0;
	int i;

	for (i = 0; i < size / 2; i++)
		sum += *tmp++;

	if (size % 2)
		sum += *(char *)tmp;

	return sum;
}

static int stub_start_transfer(struct stub_dev *stub, struct usb_request *req)
{
	int ret = 0;
	struct usb_ep *ep;

	if (req == stub->req_in) {
		stub->in_state = BUF_STATE_FULL;
		ep = stub->ep_in;
		ret = usb_ep_queue(ep, req, GFP_KERNEL);
		if (ret)
			return ret;

		ret = wait_event_interruptible_timeout(stub->in_wq,
				stub->in_state == BUF_STATE_EMPTY,
				msecs_to_jiffies(1000));
		/* Timeout */
		if (ret == 0)
			ret = -ETIME;
		/* Normal */
		else if (ret > 0)
			ret = 0;
	} else if (req == stub->req_out) {
		stub->out_state = BUF_STATE_EMPTY;
		ep = stub->ep_out;
		ret = usb_ep_queue(ep, req, GFP_KERNEL);
		if (ret)
			return ret;

		ret = wait_event_interruptible_timeout(stub->out_wq,
				stub->out_state == BUF_STATE_FULL,
				msecs_to_jiffies(1000));
		/* Timeout */
		if (ret == 0)
			ret = -ETIME;
		/* Normal */
		else if (ret > 0)
			ret = 0;
	}

	if (ret)
		usb_ep_dequeue(ep, req);

	return ret;
}

/*
 * Initialize normal command packet header
 */
static inline void stub_init_packet_header(struct stub_packet_header *header,
				s8 code, s16 len)
{
	header->protocol = STUB_PROTOCOL;
	header->type = STUB_TYPE_ASET;
	header->code = code;
	header->attribute = 0;
	header->length = len;
}

/*
 * Initialize normal Stub In command packet
 */
static inline void stub_init_cmd_packet(struct stub_cmd_packet *packet,
				s8 code)
{
	stub_init_packet_header(&packet->header, code, 0);
	packet->checksum = stub_checksum((void *)&packet->header,
			sizeof(struct stub_packet_header));
}

/*
 * Sanity Check for PC Write: return true if valid, else return false
 *
 * NOTICE: We check the length only and don't try to check protocol,
 * type, opcode and cheksum, because we think is redundant for the
 * USB Stub Protocol. Actually, we don't like it (not simple enough).
 */
static inline bool stub_validity_check(struct usb_request *req, int len)
{
	/* Sanity Check */
	if (*(short int *)((char *)req->buf + STUB_PACKET_LEN_INDEX) != len ||
		req->actual != len + sizeof(struct stub_cmd_packet))
		return false;

	return true;
}

static int stub_recv_ack(struct stub_dev *stub)
{
	struct usb_request *req;
	char *buf;

	req = stub->req_out;
	req->length = stub->ep_out->maxpacket;

	if (stub_start_transfer(stub, req))
		return -EIO;

	buf = req->buf;
	if (buf[0] == STUB_PROTOCOL &&
		buf[1] == STUB_TYPE_ASET &&
		buf[2] == STUB_OP_ACK &&
		buf[3] == 0 &&
		buf[4] == 0 &&
		buf[5] == 0 &&
		buf[6] == (char)STUB_ACK_CHECKSUM &&
		buf[7] == STUB_ACK_CHECKSUM >> 8)
		return 0;

	return -EIO;
}

/*
 * NOTE: For now, NAK is not used!
 */
static int stub_send_ack(struct stub_dev *stub)
{
	struct usb_request *req;
	char *buf;

	req = stub->req_in;
	req->length = sizeof(struct stub_cmd_packet);
	req->zero = 0;

	buf = req->buf;
	buf[0] = STUB_PROTOCOL;
	buf[1] = STUB_TYPE_ASET;
	buf[2] = STUB_OP_ACK;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = (char)STUB_ACK_CHECKSUM;
	buf[7] = STUB_ACK_CHECKSUM >> 8;

	return stub_start_transfer(stub, req);
}

/*
 * Transfer (3 transactions): Command -> Payload -> ACK
 */
static int stub_do_read_common(struct stub_dev *stub, void __user *buf,
				int len, s8 code)
{
	struct usb_request *req;
	char *tmp;

	req = stub->req_in;
	stub_init_cmd_packet((struct stub_cmd_packet *)req->buf, code);
	req->length = sizeof(struct stub_cmd_packet);
	req->zero = 0;

	/* Command: Stub IN (Bulk IN) */
	if (stub_start_transfer(stub, req))
		return -EIO;

	req = stub->req_out;
	req->length = stub->ep_out->maxpacket;

	/* Payload: PC Write (Bulk OUT) */
	if (stub_start_transfer(stub, req))
		return -EIO;

	if (!stub_validity_check(req, len))
		return -EIO;

	/* ACK: Stub IN (Bulk IN) */
	if (stub_send_ack(stub))
		return -EIO;

	/* Check OK: load payload (skip packet header) */
	tmp = (char *)req->buf + sizeof(struct stub_packet_header);
	if (copy_to_user(buf, tmp, len))
		return -EFAULT;

	return 0;
}

/*
 * Transfer (2 transactions): Payload -> ACK
 */
static int stub_do_write_common(struct stub_dev *stub, void __user *buf,
				int len, s8 code)
{
	struct usb_request *req;
	char *tmp;

	req = stub->req_in;
	stub_init_packet_header((struct stub_packet_header *)req->buf, code, len);

	tmp = (char *)req->buf + sizeof(struct stub_packet_header);
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;

	tmp += len;
	*(short int *)tmp = stub_checksum(req->buf,
		len + sizeof(struct stub_packet_header));

	req->length = len + sizeof(struct stub_cmd_packet);
	req->zero = 0;

	/* Payload: Stub IN (Bulk IN) */
	if (stub_start_transfer(stub, req))
		return -EIO;

	/* ACK: PC Write (Bulk OUT) */
	if (stub_recv_ack(stub))
		return -EIO;

	return 0;
}

static int stub_do_read_status(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_status),
			STUB_OP_READ_STATUS);
}

static int stub_do_write_status(struct stub_dev *stub, void __user *buf)
{
	return stub_do_write_common(stub, buf, sizeof(struct stub_case_info),
			STUB_OP_WRITE_STATUS);
}

static int stub_do_read_volume(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_volume_data),
			STUB_OP_READ_VOLUME);
}

static int stub_do_read_eq(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_eq_data),
			STUB_OP_READ_EQ);
}

static int stub_do_read_cpdrc(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_cpdrc_data),
			STUB_OP_READ_CPDRC);
}

static int stub_do_read_cpdrc2(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_cpdrc2_data),
			STUB_OP_READ_CPDRC2);
}

static int stub_do_main_switch(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_main_switch),
			STUB_OP_MAIN_SWITCH);
}

static int stub_do_write_aux(struct stub_dev *stub, void __user *buf)
{
	return stub_do_write_common(stub, buf, sizeof(struct stub_aux_data),
			STUB_OP_WRITE_AUX);
}

static int stub_do_read_mode(struct stub_dev *stub, void __user *buf)
{
	return stub_do_read_common(stub, buf, sizeof(struct stub_alg_mode_data),
			STUB_OP_READ_MODE);
}

static int stub_do_open(struct stub_dev *stub)
{
	struct usb_request *req;
	u8 *buf;
	int ret;
	int i = 3;

	do {
		req = stub->req_in;
		req->length = 8;
		req->zero = 0;

		buf = req->buf;
		buf[0] = STUB_PROTOCOL;
		buf[1] = STUB_TYPE_OPEN;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0x04;
		buf[5] = 0;
		buf[6] = 0;
		buf[7] = 0;

		ret = stub_start_transfer(stub, req);
		if (ret == -ETIME)
			continue;
		else if (ret)
			return ret;

		req = stub->req_out;
		/* FIXME: Make bulk-out requests be divisible by the maxpacket size */
		req->length = stub->ep_out->maxpacket;

		ret = stub_start_transfer(stub, req);
		if (ret == -ETIME)
			continue;
		else if (ret)
			return ret;

		/* Arbitrary check */
		buf = (char *)req->buf;
		if (*buf != 0x03 && req->actual != 1)
			return -EIO;

		ret = stub_send_ack(stub);
		if (ret == -ETIME)
			continue;
		else if (ret)
			return ret;

		return 0;
	} while (--i);

	return -EIO;
}

static ssize_t stub_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{
	pr_info("stub_read: not implemented\n");
	return -EINVAL;
}

static ssize_t stub_write(struct file *fp, const char __user *buf,
				size_t count, loff_t *pos)
{
	pr_info("stub_write: not implemented\n");
	return -EINVAL;
}

static long stub_do_ioctl(struct file *fp, unsigned code, void __user *p)
{
	struct stub_dev *dev = fp->private_data;
	int ret = -EINVAL;

	if (stub_lock(&dev->ioctl_excl))
		return -EBUSY;

	switch (code) {
	case STUB_READ_STATUS:
		pr_debug("%s STUB_READ_STATUS\n", __func__);
		ret = stub_do_read_status(dev, p);
		break;

	case STUB_READ_VOLUME:
		pr_debug("%s STUB_READ_VOLUME\n", __func__);
		ret = stub_do_read_volume(dev, p);
		break;

	case STUB_READ_EQ:
		pr_debug("%s STUB_READ_EQ\n", __func__);
		ret = stub_do_read_eq(dev, p);
		break;

	case STUB_READ_CPDRC:
		pr_debug("%s STUB_READ_CPDRC\n", __func__);
		ret = stub_do_read_cpdrc(dev, p);
		break;

	case STUB_READ_CPDRC2:
		pr_debug("%s STUB_READ_CPDRC2\n", __func__);
		ret = stub_do_read_cpdrc2(dev, p);
		break;

	case STUB_MAIN_SWITCH:
		pr_debug("%s STUB_MAIN_SWITCH\n", __func__);
		ret = stub_do_main_switch(dev, p);
		break;

	case STUB_WRITE_AUX:
		pr_debug("%s STUB_WRITE_AUX\n", __func__);
		ret = stub_do_write_aux(dev, p);
		break;

	case STUB_WRITE_STATUS:
		pr_debug("%s STUB_WRITE_STATUS\n", __func__);
		ret = stub_do_write_status(dev, p);
		break;

	case STUB_READ_MODE:
		pr_debug("%s STUB_READ_MODE\n", __func__);
		ret = stub_do_read_mode(dev, p);
		break;

	default:
		pr_err("%s, %d: not implemented", __func__, code);
		break;
	}

	stub_unlock(&dev->ioctl_excl);
	return ret;
}

static long stub_ioctl(struct file *fp, unsigned int code,
				unsigned long value)
{
	return stub_do_ioctl(fp, code, (void __user *)value);
}

#ifdef CONFIG_COMPAT
static long stub_compat_ioctl(struct file *fp, unsigned code,
				unsigned long value32)
{
	return stub_do_ioctl(fp, code, compat_ptr(value32));
}
#endif

static int stub_open(struct inode *ip, struct file *fp)
{
	pr_info("stub_open\n");

	if (stub_lock(&_stub_dev->open_excl))
		return -EBUSY;

	if (stub_do_open(_stub_dev)) {
		stub_unlock(&_stub_dev->open_excl);
		return -EIO;
	}

	fp->private_data = _stub_dev;
	return 0;
}

static int stub_release(struct inode *ip, struct file *fp)
{
	pr_info("stub_release\n");

	stub_unlock(&_stub_dev->open_excl);
	return 0;
}

/* file operations for /dev/stub_usb */
static const struct file_operations stub_fops = {
	.owner = THIS_MODULE,
	.read = stub_read,
	.write = stub_write,
	.unlocked_ioctl = stub_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = stub_compat_ioctl,
#endif
	.open = stub_open,
	.release = stub_release,
};

static struct miscdevice stub_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = stub_shortname,
	.fops = &stub_fops,
};

static int stub_setup(void)
{
	struct stub_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->ioctl_excl, 0);
	init_waitqueue_head(&dev->in_wq);
	init_waitqueue_head(&dev->out_wq);

	_stub_dev = dev;

	ret = misc_register(&stub_device);
	if (ret)
		goto err;

	return 0;

err:
	_stub_dev = NULL;
	kfree(dev);
	return ret;
}

static void stub_cleanup(void)
{
	struct stub_dev *dev = _stub_dev;
	int cnt = 1000;

	if (!dev)
		return;

	while ((atomic_read(&dev->open_excl) != 0) ||
		(atomic_read(&dev->ioctl_excl) != 0)) {
		msleep(1);
		cnt--;
		if (cnt < 0) {
			pr_err("wait mtp read write timeout!\n");
			break;
		}
	}

	misc_deregister(&stub_device);

	_stub_dev = NULL;
	kfree(dev);
}
