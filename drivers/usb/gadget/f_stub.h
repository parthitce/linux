/*
 * f_stub.h  --  Actions USB Stub Gadget Driver
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

#ifndef _F_STUB_H_
#define _F_STUB_H_

#include <uapi/linux/usb/f_stub.h>

/* Protocol and Type */
#define STUB_PROTOCOL	0xae
#define STUB_TYPE_ASET	0x03
#define STUB_TYPE_OPEN	0xff

/*
 * Default Checksum: 0xae 0x03 0xfe 0x00 0x00 0x00
 * Algorithm: 0xae + 0x03 << 8 + 0xfe = 0x4ac
 */
#define STUB_ACK_CHECKSUM	0x4ac

/* Op Code */
#define STUB_OP_READ_STATUS	0x00
#define STUB_OP_WRITE_STATUS	0x80
#define STUB_OP_READ_VOLUME	0x01
#define STUB_OP_WRITE_VOLUME	0x81
#define STUB_OP_READ_EQ	0x02
#define STUB_OP_WRITE_EQ	0x82
#define STUB_OP_READ_VBASS	0x03
#define STUB_OP_WRITE_VBASS	0x83
#define STUB_OP_READ_TE	0x04
#define STUB_OP_WRITE_TE	0x84
#define STUB_OP_READ_SURR	0x05
#define STUB_OP_WRITE_SURR	0x85
#define STUB_OP_READ_LIMITER	0x06
#define STUB_OP_WRITE_LIMITER	0x86
#define STUB_OP_READ_SPKCMP	0x07
#define STUB_OP_WRITE_SPKCMP	0x87
#define STUB_OP_READ_DRC	0x08
#define STUB_OP_READ_DRC2	0x09
#define STUB_OP_READ_CPDRC	0x2a
#define STUB_OP_READ_CPDRC2	0x29
#define STUB_OP_READ_LFRC	0x0a
#define STUB_OP_READ_RFRC	0x0b
#define STUB_OP_WRITE_CONFIG	0x8f
#define STUB_OP_READ_SEE	0x0c
#define STUB_OP_READ_SEW	0x0d
#define STUB_OP_READ_SD	0x0e
#define STUB_OP_MAIN_SWITCH	0x19
#define STUB_OP_TOOL_SUSPEND	0x20
#define STUB_OP_READ_EQ2	0x21
#define STUB_OP_READ_EQ3	0x22
#define STUB_OP_NAK	0xfd
#define STUB_OP_ACK	0xfe
#define STUB_OP_POLL	0xff
#define STUB_OP_WRITE_AUX	0x90
#define STUB_OP_READ_COMPRESSOR	0x2b
#define STUB_OP_READ_MODE	0x0f


#define STUB_PACKET_LEN_INDEX	4

struct stub_packet_header {
	/* Protocol byte: should be 0xae */
	s8 protocol;
	/* PC Tool type: should be 0x03 for ASET */
	s8 type;
	/* Op code */
	s8 code;
	/* Reserved */
	s8 attribute;
	/* The length of payload except for checksum, could be 0 */
	s16 length;
};

struct stub_cmd_packet {
	struct stub_packet_header header;
	s16 checksum;
};

#endif	/* _F_STUB_H_ */
