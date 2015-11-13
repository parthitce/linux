/*
 * HDMI Display Data Channel (I2C bus protocol)
 *
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
 *	2015/10/15: Created by Lipeng.
 */
#define DEBUGX
#define pr_fmt(fmt) "ip_sx00_hdcp: %s, " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/switch.h>
#include <linux/sysfs.h>

#include "hdmi.h"
#include "ip-sx00.h"

/*
 * READ_HDCP_KEY_FROM_NATIVE,
 * A test definition, if is defined, we will not read real hdcp
 * key from disk, instead of from a static table.
 */
#define READ_HDCP_KEY_FROM_NATIVEX

/* HDCP authen */
#define HDCP_XMT_LINK_H0	0
#define HDCP_XMT_LINK_H1	1
#define HDCP_XMT_AUTH_A0	2
#define HDCP_XMT_AUTH_A1	3
#define HDCP_XMT_AUTH_A2	4
#define HDCP_XMT_AUTH_A3	5
#define HDCP_XMT_AUTH_A4	6
#define HDCP_XMT_AUTH_A5	7
#define HDCP_XMT_AUTH_A6	8
#define HDCP_XMT_AUTH_A7	9
#define HDCP_XMT_AUTH_A8	10
#define HDCP_XMT_AUTH_A9	11
#define HDCP_XMT_AUTH_A9_1	12

#define HDCP_FAIL_TIMES		50

static struct hdmi_hdcp		g_hdcp;

static struct hdmi_ip		*g_ip;
#define hdcp_readl(reg)		hdmi_ip_readl(g_ip, (reg))
#define hdcp_writel(val, reg)	hdmi_ip_writel(g_ip, (reg), (val))

static unsigned char aksv[6] = {
	0x10,
	0x1e,
	0xc8,
	0x42,
	0xdd,
	0xd9
};

#ifdef READ_HDCP_KEY_FROM_NATIVE
static char test_keyr0[40][15] = {
	"aa6197eb701e4e",
	"780717d2d425d3",
	"7c4f7efe39f44d",
	"1e05f28f0253bd",
	"d614c6ccb090ee",
	"82f7f2803ffefc",
	"4ae2ebe12741d7",
	"e203695a82311c",
	"4537b3e7f9f557",
	"b02d715e2961ed",
	"cd6e0ab9834016",
	"d64fd9edfdd35b",
	"3a13f170726840",
	"4f52fb759d6f92",
	"2afc1d08b4221f",
	"b99db2a29aea40",
	"2020da8eb3282a",
	"0b4c6aee4aa771",
	"edeef811ec6ac0",
	"8f451ad92112f3",
	"705e2fddc47e6e",
	"6efc72aaa3348f",
	"7debb76130fb0e",
	"5fd1a853c40f52",
	"70168fbeaeba3c",
	"a0cb8f3162d198",
	"5ed0a9eef0c1ed",
	"fa18541a02d283",
	"0661135d19feec",
	"da6b02ca363cf3",
	"dd7142f81e832f",
	"cd3490a48be99a",
	"0e08bceaeec81a",
	"9b0d183dbbbbf3",
	"7788f601c07e57",
	"4e7271a716e4aa",
	"f510b293bec62a",
	"16158c6a1f3c84",
	"33656236296f06",
	"a5438e67d72d80",
};
#else
unsigned char test_keyr0[40][15];
#endif

#define M0_LENGTH		8
#define BSTATUS_LENGTH		2
#define KSV_LENGTH		5
#define KSV_RESERVED		3
#define KEY_COL_LENGTH		7
#define KEY_ARRAY_LENGTH	40
#define KEY_LENGTH		280
#define VERIFY_LENGTH		20
#define MAX_SHA_1_INPUT_LENGTH	704

#define MISC_INFO_TYPE_HDCP	STORAGE_DATA_TYPE_HDCP

#define SWAPL(x)		((((x) & 0x000000ff) << 24) \
				 + (((x) & 0x0000ff00) << 8) \
				 + (((x) & 0x00ff0000) >> 8) \
				 + (((x) & 0xff000000) >> 24))
/* x leftrotate n */
#define SXOLN(n, x)		((x << n) | (x >> (32 - n)))

/* x rightrotate n */
#define SXORN(n, x)		((x >> n) | (x << (32 - n)))

/* sha_1 160bit */
#define HLEN			20

static int			hdcp_timer_interval = 50;

#ifndef READ_HDCP_KEY_FROM_NATIVE

static int check_one_number(unsigned char data)
{
	int num = 0, i;

	for (i = 0; i < 8; i++) {
		if ((data >> i) & 0x1)
			num++;
	}

	return num;
}

unsigned char sha_1(unsigned char *sha_output, unsigned char *m_input, int len)
{
	unsigned int Kt[4] = {
		0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6
	};

	unsigned int h[5] = {
		0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0
	};

	int t, n, n_block;
	unsigned int ft, x, temp;
	unsigned int A, B, C, D, E;
	int i, j;
	unsigned char *sha_input;
	unsigned char *out_temp;
	unsigned int W[80];

	sha_input = kmalloc(700, GFP_KERNEL);
	if (NULL == sha_input)
		return -ENOMEM;
	memset(sha_input, 0, 700);

	for (i = 0; i < len; i++)
		sha_input[i] = m_input[i];

	/* do message padding */
	n_block = (len * 8 + 1 + 64) / 512 + 1;
	sha_input[len] = 0x80;
	pr_debug("n_block=%d\n", n_block);

	/* set len */
	sha_input[(n_block - 1) * 64 + 60]
		= (unsigned char)(((len * 8) & 0xff000000) >> 24);
	sha_input[(n_block - 1) * 64 + 61]
		= (unsigned char)(((len * 8) & 0x00ff0000) >> 16);
	sha_input[(n_block - 1) * 64 + 62]
		= (unsigned char)(((len * 8) & 0x0000ff00) >> 8);
	sha_input[(n_block - 1) * 64 + 63]
		= (unsigned char)((len * 8) & 0x000000ff);

	for (j = 0; j < n_block; j++) {
		pr_debug("\nBlock %d\n", j);
		for (i = 0; i < 64; i++) {
			if ((i % 16 == 0) && (i != 0))
				pr_debug("\n0x%2x,", sha_input[j * 64 + i]);
			else
				pr_debug("0x%2x,", sha_input[j * 64 + i]);
		}
	}
	pr_debug("SHA sha_input end\n");

	for (n = 0; n < n_block; n++) {
		for (t = 0; t < 16; t++) {
			x = *((unsigned int *)&sha_input[n * 64 + t * 4]);
			W[t] = SWAPL(x);
		}
		for (t = 16; t < 80; t++) {
			x = W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16];
			W[t] = SXOLN(1, x);
		}

		A = h[0];
		B = h[1];
		C = h[2];
		D = h[3];
		E = h[4];

		for (t = 0; t < 80; t++) {
			if (t >= 0 && t <= 19)
				ft = (B & C) | ((~B) & D);
			else if (t >= 20 && t <= 39)
				ft = B ^ C ^ D;
			else if (t >= 40 && t <= 59)
				ft = (B & C) | (B & D) | (C & D);
			else
				ft = (B ^ C ^ D);

			/* temp = S^5(A) + f(t;B,C,D) + E + W(t) + K(t) */
			temp = SXOLN(5, A) + ft + E + W[t] + Kt[t / 20];

			E = D;
			D = C;
			C = SXOLN(30, B);	/* C = S^30(B) */
			B = A;
			A = temp;
		}

		h[0] += A;			/* H0 = H0 + A */
		h[1] += B;			/* H1 = H1 + B */
		h[2] += C;			/* H2 = H2 + C */
		h[3] += D;			/* H3 = H3 + D */
		h[4] += E;			/* H4 = H4 + E */
	}

	pr_debug("\noutput original sha_input:\n");
	for (i = 0; i < HLEN / 4; i++)
		pr_debug("0x%x\t", h[i]);

	pr_debug("\nconvert to little endien\n");
	for (i = 0; i < HLEN / 4; i++) {
		h[i] = SWAPL(h[i]);
		pr_debug("0x%x\t", h[i]);
	}
	pr_debug("\n");

	/* copy to output pointer */
	out_temp = (unsigned char *) h;
	pr_debug("out_temp:\n");
	for (i = 0; i < HLEN; i++) {
		sha_output[i] = out_temp[i];
		pr_debug("0x%x\t", out_temp[i]);
	}
	pr_debug("\n");
	kfree(sha_input);
	return 0;
}

extern int read_mi_item(char *name, void *buf, unsigned int count);

static int hdcp_read_key(void)
{
	int array, col, index;
	int num = 0;
	int i = 0;
	int ret;
	unsigned char key[308];
	unsigned char sha1_verify[20];
	unsigned char sha1_result[20];

	ret = read_mi_item("HDCP", key, sizeof(key));
	if (ret < 0) {
		pr_info("failed to read hdcp key from secure storage(%d)\n",
			ret);
		return ret;
	}
	pr_debug("hdcp key cnt %d\n", ret);

	for (i = 1; i < sizeof(aksv); i++)
		aksv[i] = key[i - 1];

	for (i = 0; i < KSV_LENGTH; i++)
		num += check_one_number(key[i]);

	/* key */
	if (num == 20) {
		pr_debug("aksv is valid\n");
		pr_debug("hdcp key is as follows:\n");
		for (array = 0; array < KEY_ARRAY_LENGTH; array++) {
			for (col = 0; col < KEY_COL_LENGTH; col++) {
				index = (KSV_LENGTH + KSV_RESERVED
					+ (array + 1) * KEY_COL_LENGTH)
					- col - 1;
				sprintf(&test_keyr0[array][2 * col], "%x",
					((key[index] & 0xf0) >> 4) & 0x0f);
				sprintf(&test_keyr0[array][2 * col + 1], "%x",
					key[index] & 0x0f);

				pr_debug("key_tmp[%d][%d]:%c\n", array,
					 2 * col, test_keyr0[array][2 * col]);
				pr_debug("key_tmp[%d][%d]:%c\n", array,
					 2 * col + 1,
					 test_keyr0[array][2 * col + 1]);
			}
			pr_debug("%s\n", test_keyr0[array]);
		}
		pr_debug("\n\nhdcp key parse finished\n\n");

		pr_debug("verify code is as follows:\n");
		for (i = 0; i < sizeof(sha1_verify); i++) {
			index = i + KSV_LENGTH + KSV_RESERVED + KEY_LENGTH;
			sha1_verify[i] = key[index];
			pr_debug("sha1_verify[%d]:%x\n", i, sha1_verify[i]);
		}
		pr_debug("verify code parse finished\n\n");

		if (sha_1(sha1_result, key,
			  KSV_LENGTH + KSV_RESERVED + KEY_LENGTH)) {
			pr_debug("aksv kmalloc mem is failed\n");
			return 1;
		}

	} else {
		pr_debug("aksv is invalid\n");
		return 1;
	}

	/* verify */
	for (i = 0; i < VERIFY_LENGTH; i++) {
		if (sha1_verify[i] != sha1_result[i]) {
			pr_err("sha1 verify error!\n");
			return 1;
		}
	}
	pr_debug("[%s]sha1 verify success !\n", __func__);

	return 0;
}
#else
static int hdcp_read_key(void)
{
	return 0;
}
#endif

static void set_hdcp_ri_pj(void)
{
	int tmp;

	pr_debug("set_hdcp_ri_pj   ~~\n");

	hdcp_writel(0x7f0f, HDCP_ICR);

	tmp = hdcp_readl(HDCP_CR);
	tmp &= (~HDCP_CR_ENRIUPDINT);
	tmp &= (~HDCP_CR_ENPJUPDINT);
	tmp &= (~HDCP_CR_HDCP_ENCRYPTENABLE);
	tmp |= HDCP_CR_FORCETOUNAUTHENTICATED;
	hdcp_writel(tmp, HDCP_CR);

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_EN1DOT1_FEATURE;
	hdcp_writel(tmp, HDCP_CR);

	pr_debug("end set_hdcp_ri_pj  temp %x ~~\n", tmp);
}

static void hdcp_set_out_opportunity_window(void)
{
	/*42,end 651,star 505 */
	hdcp_writel(HDCP_KOWR_HDCPREKEYKEEPOUTWIN(0x2a) |
		    HDCP_KOWR_HDCPVERKEEPOUTWINEND(0x28b) |
		    HDCP_KOWR_HDCPVERTKEEPOUTWINSTART(0x1f9), HDCP_KOWR);

	/*HDCP1.1 Mode: start 510,end 526 */
	hdcp_writel(HDCP_OWR_HDCPOPPWINEND(0x20e) |
		    HDCP_OWR_HDCPOPPWINSTART(0x1fe), HDCP_OWR);

}

static void hdcp_launch_authen_seq(void)
{
	int i = 6;

	hdcp_set_out_opportunity_window();

	while ((hdcp_readl(HDMI_LPCR) & HDMI_LPCR_CURLINECNTR) == 0 && i < 65) {
		udelay(1);
		i++;
	}

	if (i > 64)
		pr_err("hdcp:HDMI_LPCR_CURLINECNTR timeout!\n");

	i = 6;
	while ((hdcp_readl(HDMI_LPCR) & HDMI_LPCR_CURLINECNTR) != 0 &&
	       i < 65) {
		udelay(1);
		i++;
	}

	/*set Ri/Pj udpdate:128,16 */
	hdcp_writel(HDCP_ICR_RIRATE(0x7f) | HDCP_ICR_PJRATE(0x0f), HDCP_ICR);
}

/* disable link integry check */
/* force Encryption disable */
static void hdcp_force_unauthentication(void)
{
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_CR);

	tmp = (tmp & (~HDCP_CR_ENRIUPDINT) & (~HDCP_CR_ENPJUPDINT)
	       & (~HDCP_CR_HDCP_ENCRYPTENABLE))
	      | HDCP_CR_FORCETOUNAUTHENTICATED;

	hdcp_writel(tmp, HDCP_CR);
	hdcp_readl(HDCP_CR);

	/* force HDCP module to unauthenticated state */
	/* P_HDCP_CR |= HDCP_CR_FORCE_UNAUTH; */
}

/*===========================================================================
 *		hdcp_check_handle, handle HDCP checking
 *=========================================================================*/

static int hdcp_freerun_get_an(unsigned char *an)
{
	/* Get An */
	/* get An influence from CRC64 */
	unsigned int tmp;
	int i;

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_ANINFREQ;
	hdcp_writel(tmp, HDCP_CR);

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_ANINFLUENCEMODE;
	hdcp_writel(tmp, HDCP_CR);

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_AUTHREQUEST;
	hdcp_writel(tmp, HDCP_CR);

	/* P_HDCP_CR |= HDCP_CR_AN_INF_REQ; //25 bit */

	/* set An Influence Mode, influence will be load from AnIR0, AnIR1 */
	/* P_HDCP_CR |= HDCP_CR_LOAD_AN;  //7 bit */

	/* trigger to get An */
	/* P_HDCP_CR |= HDCP_CR_AUTH_REQ;  //0 bit  --写1，生成an */
	pr_debug("[hdcp_freerun_get_an]:wait An ready\n");
	i = 100;
	while (!(hdcp_readl(HDCP_SR) & (HDCP_SR_ANREADY)) && (i--))
		udelay(1);
	pr_debug("[hdcp_freerun_get_an]:wait An ok\n");

	/* leave An influence mode */
	tmp = hdcp_readl(HDCP_CR);
	tmp &= (~HDCP_CR_ANINFLUENCEMODE);
	hdcp_writel(tmp, HDCP_CR);

	/*
	 * Convert HDCP An from bit endien to little endien
	 * HDCP An should stored in little endien,
	 * but HDCP HW store in bit endien.
	 */
	an[0] = 0x18;
	tmp = hdcp_readl(HDCP_ANLR);
	an[1] = tmp & 0xff;
	an[2] = (tmp >> 8) & 0xff;
	an[3] = (tmp >> 16) & 0xff;
	an[4] = (tmp >> 24) & 0xff;

	tmp = hdcp_readl(HDCP_ANMR);

	an[5] = tmp & 0xff;
	an[6] = (tmp >> 8) & 0xff;
	an[7] = (tmp >> 16) & 0xff;
	an[8] = (tmp >> 24) & 0xff;

	for (i = 0; i < 9; i++)
		pr_debug("an[%d]:0x%x\n", i, an[i]);

	return 0;
}

/*
 * check if Bksv is valid, need have 20 "1"  and 20 "0"
 */
static int check_bksv_invalid(void)
{
	int i, j;
	unsigned char counter = 0;
	unsigned char invalid_bksv[4][5] = {
		{0x0b, 0x37, 0x21, 0xb4, 0x7d},
		{0xf4, 0xc8, 0xde, 0x4b, 0x82},
		{0x23, 0xde, 0x5c, 0x43, 0x93},
		{0x4e, 0x4d, 0xc7, 0x12, 0x7c},
	};

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if (((g_hdcp.bksv[i] >> j) & 0x1) != 0)
				counter++;
		}
	}

	if (counter != 20) {
		pr_err("[%s]fail  0x%x 0x%x 0x%x 0x%x 0x%x\n", __func__,
		       g_hdcp.bksv[0], g_hdcp.bksv[1], g_hdcp.bksv[2],
		       g_hdcp.bksv[3], g_hdcp.bksv[4]);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 5; j++) {
			if (invalid_bksv[i][j] != g_hdcp.bksv[j])
				break;

			if (j == 4)
				return 0;
		}
	}

	pr_debug("[%s]successful!\n", __func__);
	return 1;
}

static void enable_hdcp_repeater(void)
{
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_DOWNSTRISREPEATER;
	hdcp_writel(tmp, HDCP_CR);
	tmp = hdcp_readl(HDCP_CR);
}

static void disable_hdcp_repeater(void)
{
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_CR);
	tmp &= (~HDCP_CR_DOWNSTRISREPEATER);
	hdcp_writel(tmp, HDCP_CR);
	tmp = hdcp_readl(HDCP_CR);
}

/* convert INT8 number to little endien number */
static int c2ln14(unsigned char *num, char *a)
{
	int i;
	int n = 14;
	for (i = 0; i < 11; i++)
		num[i] = 0;

	for (i = 0; i < n; i++) {
		if (i % 2) {
			if (a[n - i - 1] >= '0' && a[n - i - 1] <= '9')
				num[i / 2] |= (a[n - i - 1] - '0') << 4;
			else if (a[n - i - 1] >= 'a' && a[n - i - 1] <= 'f')
				num[i / 2] |= (a[n - i - 1] - 'a' + 10) << 4;
			else if (a[n - i - 1] >= 'A' && a[n - i - 1] <= 'F')
				num[i / 2] |= (a[n - i - 1] - 'A' + 10) << 4;
		} else {
			if (a[n - i - 1] >= '0' && a[n - i - 1] <= '9')
				num[i / 2] |= (a[n - i - 1] - '0');
			else if (a[n - i - 1] >= 'a' && a[n - i - 1] <= 'f')
				num[i / 2] |= (a[n - i - 1] - 'a' + 10);
			else if (a[n - i - 1] >= 'A' && a[n - i - 1] <= 'F')
				num[i / 2] |= (a[n - i - 1] - 'A' + 10);
		}
	}

	return 0;
}



int hdcp_set_km(unsigned char *key, int pnt)
{
	unsigned int tmp;
	unsigned char dKey[11];

	dKey[0] = key[0] ^ pnt;
	dKey[1] = ~key[1] ^ dKey[0];
	dKey[2] = key[2] ^ dKey[1];
	dKey[3] = key[3] ^ dKey[2];
	dKey[4] = key[4] ^ dKey[3];
	dKey[5] = ~key[5] ^ dKey[4];
	dKey[6] = ~key[6] ^ dKey[5];

	/* write to HW */
	/*P_HDCP_DPKLR*/
	tmp = pnt | (dKey[0] << 8) | (dKey[1] << 16) | (dKey[2] << 24);
	hdcp_writel(tmp, HDCP_DPKLR);

	/*P_HDCP_DPKMR*/
	tmp = dKey[3] | (dKey[4] << 8) | (dKey[5] << 16) | (dKey[6] << 24);
	hdcp_writel(tmp, HDCP_DPKMR);

	/* trigger accumulation */
	while (!(hdcp_readl(HDCP_SR) & (1 << 3)))
		;

	return 0;
}

static int hdcp_gen_km(void)
{
	unsigned char key[11];
	int i, j;

	for (i = 0; i < 30; i++)
		pr_debug("test_keyr0  %d %d\n", i, test_keyr0[0][i]);

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 8; j++) {
			if (g_hdcp.bksv[i] & (1 << j)) {
				c2ln14(key, test_keyr0[i * 8 + j]);
				hdcp_set_km(&key[0], 0x55);
			}
		}
	}

	return 0;
}

/* force Encryption disable */
/* reset Km accumulation */
static int hdcp_authentication_sequence(void)
{
	int i;
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_CR);
	tmp = (tmp & (~HDCP_CR_HDCP_ENCRYPTENABLE)) | HDCP_CR_RESETKMACC;
	hdcp_writel(tmp, HDCP_CR);

	/* set Bksv to accumulate Km */
	hdcp_gen_km();

	/* disable Ri update interrupt */
	tmp = hdcp_readl(HDCP_CR);
	tmp &= (~HDCP_CR_ENRIUPDINT);
	hdcp_writel(tmp, HDCP_CR);

	/* clear Ri updated pending bit */
	tmp = hdcp_readl(HDCP_SR);
	tmp |= HDCP_SR_RIUPDATED;
	hdcp_writel(tmp, HDCP_SR);

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_AUTHCOMPUTE;
	hdcp_writel(tmp, HDCP_CR);

	/* trigger hdcpBlockCipher at authentication */
	/* wait 48+56 pixel clock to get R0 */
	pr_debug("while (!(hdcp_readl(HDCP_SR) & HDCP_SR_RIUPDATED)\n");
	i = 100;
	while ((!(hdcp_readl(HDCP_SR) & HDCP_SR_RIUPDATED)) && (i--))
		mdelay(1);
	pr_debug("end %d\n", i);

	/* get Ri */
	g_hdcp.ri = (hdcp_readl(HDCP_LIR) >> 16) & 0xffff;
	pr_debug("Ri:0x%x\n", g_hdcp.ri);

	return 0;
}

/*读取M0的前4字节*/
static void hdcp_read_hdcp_milr(unsigned char *m0)
{
	unsigned int tmp = 0;

	tmp = hdcp_readl(HDCP_MILR);
	m0[0] = (unsigned char) (tmp & 0xff);
	m0[1] = (unsigned char) ((tmp >> 8) & 0xff);
	m0[2] = (unsigned char) ((tmp >> 16) & 0xff);
	m0[3] = (unsigned char) ((tmp >> 24) & 0xff);
}

/*读取M0的后4字节*/
static void hdcp_read_hdcp_mimr(unsigned char *m0)
{
	unsigned int tmp = 0;

	tmp = hdcp_readl(HDCP_MIMR);
	m0[0] = (unsigned char) (tmp & 0xff);
	m0[1] = (unsigned char) ((tmp >> 8) & 0xff);
	m0[2] = (unsigned char) ((tmp >> 16) & 0xff);
	m0[3] = (unsigned char) ((tmp >> 24) & 0xff);
}

static void set_hdcp_to_authenticated_state(void)
{
	unsigned int tmp;

	/* set HDCP module to authenticated state */
	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_DEVICEAUTHENTICATED;
	hdcp_writel(tmp, HDCP_CR);

	/* start encryption */
	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_HDCP_ENCRYPTENABLE;
	hdcp_writel(tmp, HDCP_CR);

	tmp = hdcp_readl(HDCP_CR);
}

static void enable_ri_update_check(void)
{
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_SR);
	tmp |= HDCP_SR_RIUPDATED;
	hdcp_writel(tmp, HDCP_SR);

	tmp = hdcp_readl(HDCP_CR);
	tmp |= HDCP_CR_ENRIUPDINT;
	hdcp_writel(tmp, HDCP_CR);
	hdcp_readl(HDCP_CR);
}

static int hdcp_read_ksv_list(unsigned char *b_status, unsigned char *ksvlist)
{
	int cnt;

	/* get device count in Bstatus [6:0] */
	if (hdmi_ddc_hdcp_read(b_status, 0x41, 2) < 0)
		if (hdmi_ddc_hdcp_read(b_status, 0x41, 2) < 0)
			return 0;

	/* if Max_devs_exceeded then quit */
	if (b_status[0] & 0x80)
		return 0;

	/* if Max_cascade_exceeded then quit */
	if (b_status[1] & 0x8)
		return 0;

	cnt = b_status[0] & 0x7f;

	if (!cnt)
		return 1;

	if (hdmi_ddc_hdcp_read(ksvlist, 0x43, 5 * cnt) < 0)
		if (hdmi_ddc_hdcp_read(ksvlist, 0x43, 5 * cnt) < 0)
			return 0;
	return 1;
}

static int hdcp_read_vprime(unsigned char *vp)
{
	/* read Vp */
	if (hdmi_ddc_hdcp_read(vp, 0x20, 20) != 0) {
		if (hdmi_ddc_hdcp_read(vp, 0x20, 20) != 0)
			return 0;
	}
	return 1;
}

static int hdcp_do_vmatch(unsigned char *v, unsigned char *ksvlist,
			  unsigned char *bstatus, unsigned char *m0)
{
	unsigned int tmp;
	int data_counter;
	unsigned char sha_1_input_data[MAX_SHA_1_INPUT_LENGTH];
	int nblock, llen;
	int cnt2 = g_hdcp.b_status[0] & 0x7f;
	int i, j;
	int hdcp_shacr = 0;

	llen = 8 * M0_LENGTH + 8 * BSTATUS_LENGTH + cnt2 * 8 * KSV_LENGTH;

	for (i = 0; i < MAX_SHA_1_INPUT_LENGTH; i++)
		sha_1_input_data[i] = 0;

	for (data_counter = 0;
	     data_counter < cnt2 * KSV_LENGTH + BSTATUS_LENGTH + M0_LENGTH;
	     data_counter++) {
		if (data_counter < cnt2 * KSV_LENGTH)
			sha_1_input_data[data_counter] = ksvlist[data_counter];
		else if ((data_counter >= cnt2 * KSV_LENGTH) &&
			 (data_counter < cnt2 * KSV_LENGTH + BSTATUS_LENGTH))
			sha_1_input_data[data_counter] = bstatus[data_counter
					- (cnt2 * KSV_LENGTH)];
		else
			sha_1_input_data[data_counter] = m0[data_counter
				- (cnt2 * KSV_LENGTH + BSTATUS_LENGTH)];
	}

	sha_1_input_data[data_counter] = 0x80;	/* block ending signal */

	nblock = (int)(data_counter / 64);

	/* total SHA counter high */
	sha_1_input_data[nblock * 64 + 62]
		= (unsigned char)(((data_counter * 8) >> 8) & 0xff);

	/* total SHA counter low */
	sha_1_input_data[nblock * 64 + 63]
		= (unsigned char)((data_counter * 8) & 0xff);

	/* reset SHA write pointer */
	tmp = hdcp_readl(HDCP_SHACR);
	hdcp_writel(tmp | 0x1, HDCP_SHACR);

	/* wait completing reset operation */
	while (hdcp_readl(HDCP_SHACR) & 0x1)
		;

	/* set new SHA-1 operation */
	tmp = hdcp_readl(HDCP_SHACR);
	hdcp_writel(tmp | 0x2, HDCP_SHACR);

	for (i = 0; i < nblock; i++) {
		for (j = 0; j < 16; j++) {
			/* P_HDCP_SHADR */
			tmp = (sha_1_input_data[i * 64 + (j * 4 + 0)] << 24) |
			       (sha_1_input_data[i * 64 + (j * 4 + 1)] << 16) |
			       (sha_1_input_data[i * 64 + (j * 4 + 2)] << 8) |
			       (sha_1_input_data[i * 64 + (j * 4 + 3)]);
			hdcp_writel(tmp, HDCP_SHADR);
			hdcp_readl(HDCP_SHADR);
		}

		 /* Start 512bit SHA operation */
		tmp = hdcp_readl(HDCP_SHACR);
		hdcp_writel(tmp | 0x4, HDCP_SHACR);

		/* after 512bit SHA operation, this bit will be set to 1 */
		while (!(hdcp_readl(HDCP_SHACR) & 0x8))
			;

		/* clear SHAfirst bit */
		tmp = hdcp_readl(HDCP_SHACR);
		hdcp_writel(tmp & 0xfd, HDCP_SHACR);
		hdcp_readl(HDCP_SHACR);
	}

	for (j = 0; j < 16; j++) {
		/* P_HDCP_SHADR */
		tmp = (sha_1_input_data[nblock * 64 + (j * 4 + 0)] << 24) |
		       (sha_1_input_data[nblock * 64 + (j * 4 + 1)] << 16) |
		       (sha_1_input_data[nblock * 64 + (j * 4 + 2)] << 8) |
		       (sha_1_input_data[nblock * 64 + (j * 4 + 3)]);
		hdcp_writel(tmp, HDCP_SHADR);
		hdcp_readl(HDCP_SHADR);
	}

	/* Start 512bit SHA operation */
	tmp = hdcp_readl(HDCP_SHACR);
	hdcp_writel(tmp | 0x4, HDCP_SHACR);

	/* after 512bit SHA operation, this bit will be set to 1 */
	while (!(hdcp_readl(HDCP_SHACR) & 0x8))
		;

	/* write V */
	tmp = (v[3] << 24) | (v[2] << 16) | (v[1] << 8) | (v[0] << 0);
	hdcp_writel(tmp, HDCP_SHADR);
	hdcp_readl(HDCP_SHADR);

	tmp = (v[7] << 24) | (v[6] << 16) | (v[5] << 8) | (v[4] << 0);
	hdcp_writel(tmp, HDCP_SHADR);
	hdcp_readl(HDCP_SHADR);

	tmp = (v[11] << 24) | (v[10] << 16) | (v[9] << 8) | (v[8] << 0);
	hdcp_writel(tmp, HDCP_SHADR);
	hdcp_readl(HDCP_SHADR);

	tmp = (v[15] << 24) | (v[14] << 16) | (v[13] << 8) | (v[12] << 0);
	hdcp_writel(tmp, HDCP_SHADR);
	hdcp_readl(HDCP_SHADR);

	tmp = (v[19] << 24) | (v[18] << 16) | (v[17] << 8) | (v[16] << 0);
	hdcp_writel(tmp, HDCP_SHADR);
	hdcp_readl(HDCP_SHADR);

	/* wait Vmatch */
	for (i = 0; i < 3; i++) {
		j = 0;
		while ((j++) < 100)
			;
		hdcp_shacr = hdcp_readl(HDCP_SHACR);
		if (hdcp_shacr & 0x10)
			return 1;	/* Vmatch */
	}

	return 0;
}

static void hdcp_check_handle(struct work_struct *work)
{
	unsigned char an[9] = { 0 };
	unsigned char b_caps = 0, ri_temp[8] = { 0 };

	if (!(hdcp_readl(HDMI_CR) & 0x01) ||
	    !(hdcp_readl(HDMI_CR) & (0x01 << 29))) {
		pr_debug("hdmi plug out in hdcp\n");
		set_hdcp_ri_pj();
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
		return;
	}

	if (hdcp_readl(HDMI_GCPCR) & 0x01) {
		pr_debug("hdcp_readl(HDCP_GCPCR)&0x01 is true 0x%x\n",
			 hdcp_readl(HDMI_GCPCR));
		set_hdcp_ri_pj();
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
		return;
	}

	if (aksv[0] == 0x00 && aksv[1] == 0x00 && aksv[2] == 0x00 &&
	    aksv[3] == 0x00 && aksv[4] == 0x00) {
		pr_debug("aksv == 0\n");
		set_hdcp_ri_pj();
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
		return;
	}

	pr_debug("\n**********hdcp start************\n");
	if (g_hdcp.hdcp_fail_times > HDCP_FAIL_TIMES) {
		/* stop play */

		g_hdcp.hdcp_authentication_success = false;
		pr_debug("\n**********hdcp fail many times************\n");
		goto end;
	}

	g_hdcp.need_to_delay -= 50;
	if (g_hdcp.need_to_delay > 0)
		goto restart;

	/* state machine */
	switch (g_hdcp.hdcp_oper_state) {
	case HDCP_XMT_LINK_H0:
		pr_debug("************HDCP_XMT_LINK_H0*************\n");
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H1;

	case HDCP_XMT_LINK_H1:
		pr_debug("************HDCP_XMT_LINK_H1*************\n");
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;

	/*
	 * Authentication phase 1: A0,A1,A2,A3
	 * A0.generate An, get Aksv;
	 * A1.write An and Aksv, read Bksv;
	 * A2.get R0;
	 * A3.computes Km, Ks, M0 and R0;
	 *
	 * Authentication phase 2:  A6  or  A6,A8,A9,A91
	 * A6.check if sink support repeater,
	 * if not support repeater then authentication finish;
	 * A8. check if  KSV is ready;
	 * A9:get KSV list;
	 * A91:compare V value;
	 *
	 * Authentication phase 3: A4,A5
	 * A4.set hdcp to Authenticated state;
	 * A5.authentication successful
	 */
	case HDCP_XMT_AUTH_A0:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A0*************\n");

		/* genrate An, get Aksv */
		g_hdcp.hdcp_oper_retry = 0;

		/* get An value */
		hdcp_freerun_get_an(an);
		msleep(20);

		pr_debug("************send An*************\n");
		if (hdmi_ddc_hdcp_write(&an[0], 0x18, 9) < 0)
			pr_debug("\nWrite An error\n");

		pr_debug("************send aksv*************\n");
		if (hdmi_ddc_hdcp_write(aksv, 0x10, 6) < 0)
			pr_debug("\nWrite aksv error\n");

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A1;
		g_hdcp.need_to_delay = 100;
		break;

	case HDCP_XMT_AUTH_A1:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A1*************\n");
		/*
		 * write An and Aksv, read Bksv
		 * if get Bksv successful mean receiver/repeater support HDCP
		 * check valid Bksv 20 ones and 20 zero
		 */

		pr_debug("************read Bksv*************\n");
		while (hdmi_ddc_hdcp_read(g_hdcp.bksv, 0x00, 5) < 0) {
			/* if read successful, means support HDCP */
			g_hdcp.i2c_error++;
			if (g_hdcp.i2c_error > 3) {
				g_hdcp.i2c_error = 0;
				pr_debug("[631]Do not support HDCP\n");
				break;
			}
		}
		msleep(110);

		pr_debug("************Check_Bksv*************\n");
		if (check_bksv_invalid() == 0) {
			g_hdcp.hdcp_fail_times++;
			hdcp_force_unauthentication();
			g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
			pr_debug("\ncheck_bksv_invalid\n");
			g_hdcp.need_to_delay = 100;
			break;
		}

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A2;
		g_hdcp.need_to_delay = 100;
		break;

	case HDCP_XMT_AUTH_A2:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A2*************\n");

		/* Computations */

		/* computes Km, Ks, M0 and R0 */
		pr_debug("************read Bcaps*************\n");
		if (hdmi_ddc_hdcp_read(&b_caps, 0x40, 1) < 0)
			pr_debug("Read Bcaps error\n"); /* error_handle(); */

		if ((b_caps & (1 << 6)) != 0) {
			/* set to support repeater */
			pr_debug("***********support repeater************\n");
			g_hdcp.repeater = 1;
			enable_hdcp_repeater();
		} else {
			/* set to support NO repeater */
			pr_debug("*********dont  support repeater********\n");
			g_hdcp.repeater = 0;
			disable_hdcp_repeater();
		}
		pr_debug("************generate Ri*************\n");
		hdcp_authentication_sequence();
		g_hdcp.hdcp_oper_retry = 3;

		/* if computed results are available */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A3;

		/* wait for 100 msec to read R0p. */
		mdelay(130); /* 建议不改为可调度 */

		break;

	case HDCP_XMT_AUTH_A3:	/* Authentication phase 1 */
		pr_debug("************HDCP_XMT_AUTH_A3*************\n");

		/*
		 * Validate Receiver
		 * computes Km, Ks, M0 and R0
		 * if computed results are available
		 */
		pr_debug("************read R0*************\n");

		if (hdmi_ddc_hdcp_read(ri_temp, 0x08, 2) < 0)
			memset(ri_temp, 0, sizeof(ri_temp));

		g_hdcp.ri_read = (int)((unsigned int)ri_temp[1] << 8)
				| ri_temp[0];

		pr_debug("****Ri_Read:0x%x\n****", g_hdcp.ri_read);
		if (g_hdcp.ri != g_hdcp.ri_read) {
			pr_debug("\nR0 != Ri_Read\n");
			if (g_hdcp.hdcp_oper_retry != 0) {
				g_hdcp.hdcp_oper_retry--;
				g_hdcp.need_to_delay = 100;
			} else {
				/* authentication part I failed */
				g_hdcp.hdcp_fail_times++;
				hdcp_force_unauthentication();

				/* restart */
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
				g_hdcp.need_to_delay = 200;
			}
			break;
		}

		g_hdcp.hdcp_oper_retry = 0;
		hdcp_read_hdcp_milr(&g_hdcp.hdcp_oper_m0[0]);
		hdcp_read_hdcp_mimr(&g_hdcp.hdcp_oper_m0[4]);

		/* authentication part I successful */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A6;

	case HDCP_XMT_AUTH_A6:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A6*************\n");
		/* Test for Repeater */

		/* get REPEATER */
		if (g_hdcp.repeater != 0) {
			/* change to Authentication part II */

			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A8;

			/* wait 100msec */
			g_hdcp.need_to_delay = 100;
			g_hdcp.retry_times_for_set_up_5_second = 0;
			break;
		}

		/* NO repeater */

		/* change to Authentication part III */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A4;

	case HDCP_XMT_AUTH_A4:	/* Authentication phase 3 */
		pr_debug("************HDCP_XMT_AUTH_A4*************\n");

		/*
		 *  Authenticated
		 * set HDCP module to authenticated state
		 * start encryption
		 */
		set_hdcp_to_authenticated_state();

		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A5;
		g_hdcp.hdcp_oper_retry = 0;

		break;

	case HDCP_XMT_AUTH_A5:	/* Authentication phase 3 */
		pr_debug("************HDCP_XMT_AUTH_A5*************\n");

		/* Link Integrity Check */
		/* Interrupt and BH will do this job */
		pr_info("********hdcp Authentication suceesful********\n");
		g_hdcp.hdcp_authentication_success = true;

		/* enable Ri update check */
		enable_ri_update_check();
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_ri_update_work,
				   msecs_to_jiffies(3000));
		return;

	case HDCP_XMT_AUTH_A8:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A8*************\n");

		/* 2nd part authentication */
		/* Wait for Ready */
		/* set up 5 second timer poll for KSV list ready */
		if ((g_hdcp.retry_times_for_set_up_5_second % 5) == 0)
			if (hdmi_ddc_hdcp_read(&b_caps, 0x40, 1) < 0)
				pr_debug("\nRead Bcaps err\n");

		if (!((b_caps >> 5) & 0x1)) {
			/* if KSVlist not ready! */
			if (g_hdcp.retry_times_for_set_up_5_second <= 50) {
				/* 100 msec * 50 = 5 sec */

				g_hdcp.retry_times_for_set_up_5_second++;
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A8;
				/* wait 100msec */
				g_hdcp.need_to_delay = 100;
				break;
			} else {
				/* restart */
				g_hdcp.hdcp_fail_times++;
				g_hdcp.retry_times_for_set_up_5_second = 0;
				g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
				hdcp_force_unauthentication();
				/* wait 100msec */
				g_hdcp.need_to_delay = 100;
				pr_debug("\nretry times for setup > 50\n");
				break;
			}

		}

		g_hdcp.retry_times_for_set_up_5_second = 0;
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A9;

	case HDCP_XMT_AUTH_A9:	/* Authentication phase 2 */
		 pr_debug("************HDCP_XMT_AUTH_A9*************\n");

		/* Read KSV List and Bstatus */
		if (!hdcp_read_ksv_list(g_hdcp.b_status, g_hdcp.ksv_list)) {
			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;
			hdcp_force_unauthentication();
			g_hdcp.hdcp_fail_times++;
			pr_debug("\nhdcp_read_ksv_list\n");
			g_hdcp.need_to_delay = 100;
			break;
		}

		g_hdcp.need_to_delay = 100;
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A9_1;
		break;

	case HDCP_XMT_AUTH_A9_1:	/* Authentication phase 2 */
		pr_debug("************HDCP_XMT_AUTH_A9_1*************\n");
		hdcp_read_vprime(g_hdcp.vp);
		if (!hdcp_do_vmatch(g_hdcp.vp, g_hdcp.ksv_list,
				    g_hdcp.b_status, g_hdcp.hdcp_oper_m0)) {
			/* compare with V' */
			/* authentication part II failed */
			g_hdcp.hdcp_fail_times++;
			hdcp_force_unauthentication();
			g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A0;

			g_hdcp.need_to_delay = 100;
			pr_debug("\nhdcp_do_vmatch\n");
			break;
		}

		/* KSV list correct , transit to Authentication Part III */
		g_hdcp.hdcp_oper_state = HDCP_XMT_AUTH_A4;
		g_hdcp.need_to_delay = 100;
		break;

	default:
		break;
	}

restart:
	queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
			   msecs_to_jiffies(hdcp_timer_interval));
	return;

end:
	hdcp_force_unauthentication();
	return;
}

/*===========================================================================
 *		hdcp_ri_update_handle, handle Ri updating event
 *=========================================================================*/

static int hdcp_check_ri_irq(void)
{
	unsigned int tmp;

	tmp = hdcp_readl(HDCP_SR);
	pr_debug("HDCP_SR 0x%x\n", tmp);

	if (tmp & HDCP_SR_RIUPDATED) {
		tmp |= HDCP_SR_RIUPDATED;
		tmp |= HDCP_SR_PJUPDATED;
		hdcp_writel(tmp, HDCP_SR);
		return 1;
	}

	return 0;
}

static int hdcp_check_ri(void)
{
	int ri, ri_read;
	unsigned char ri_temp[8] = {0};

	pr_debug("start\n");

	ri = (hdcp_readl(HDCP_LIR) >> 16) & 0xffff;

	if (hdmi_ddc_hdcp_read(ri_temp, 0x08, 2) == 0)
		memset(ri_temp, 0, sizeof(ri_temp));

	ri_read = (ri_temp[1] << 8) | ri_temp[0];

	pr_debug("Ri %x, Riread %x\n", ri, ri_read);
	if (ri != ri_read)
		return 1;

	return 0;
}

static void hdcp_ri_update_handle(struct work_struct *work)
{
	if (hdcp_check_ri_irq() && hdcp_check_ri()) {
		set_hdcp_ri_pj();
		g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
				   msecs_to_jiffies(50));
	} else {
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_ri_update_work,
				   msecs_to_jiffies(3000));
	}
}

static void hdcp_read_key_handle(struct work_struct *work)
{
	if (hdcp_read_key() == -EAGAIN)
		queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_read_key_work,
				   msecs_to_jiffies(2000));
}

/*===========================================================================
 *				APIs
 *=========================================================================*/

int ip_sx00_hdcp_init(struct hdmi_ip *ip)
{
	g_ip = ip;

	g_hdcp.hdcp_oper_state = HDCP_XMT_LINK_H0;
	g_hdcp.hdcp_fail_times = 0;

	g_hdcp.wq = create_workqueue("hdmi-hdcp");

	INIT_DELAYED_WORK(&g_hdcp.hdcp_check_work, hdcp_check_handle);
	INIT_DELAYED_WORK(&g_hdcp.hdcp_ri_update_work, hdcp_ri_update_handle);
	INIT_DELAYED_WORK(&g_hdcp.hdcp_read_key_work, hdcp_read_key_handle);

	/* try to read HDCP key */
	queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_read_key_work,
			   msecs_to_jiffies(50));

	return 0;
}

int ip_sx00_hdcp_enable(struct hdmi_ip *ip, bool enable)
{
	/* make sure Ri check work is canceled! */
	cancel_delayed_work_sync(&g_hdcp.hdcp_ri_update_work);
	flush_delayed_work(&g_hdcp.hdcp_ri_update_work);

	/*
	 * disable
	 */
	if (!enable) {
		cancel_delayed_work_sync(&g_hdcp.hdcp_check_work);
		flush_delayed_work(&g_hdcp.hdcp_check_work);

		return 0;
	}

	/*
	 * enable
	 */
	set_hdcp_ri_pj();
	hdcp_launch_authen_seq();
	hdcp_force_unauthentication();

	queue_delayed_work(g_hdcp.wq, &g_hdcp.hdcp_check_work,
			   msecs_to_jiffies(50));

	return 0;
}
