/*
 * Actions OWL SoC Power domain controller driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bootafinfo.h>
#include <linux/module.h> 
#include <asm/setup.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
/*
 * boot mode:
 *   0: normal
 *   1: upgrade
 *   2: minicharge	
 *   3: recovery
 */
static int owl_boot_mode = BOOT_MODE_NORMAL;

static int __init boot_mode_process(char *str)
{
	if (strcmp(str, "upgrade") == 0) {
		printk("owl bootmode: upgrade\n");
		owl_boot_mode = BOOT_MODE_UPGRADE;
	}else if (strcmp(str, "charger") == 0) {
		printk("owl bootmode: charger\n");
		owl_boot_mode = BOOT_MODE_CHARGER;
	}else if (strcmp(str, "recovery") == 0) {
		printk("owl bootmode: recovery\n");
		owl_boot_mode = BOOT_MODE_RECOVERY;
	}
	return 1;
}

__setup("bootmode=", boot_mode_process);

static int __init boot1_mode_process(char *str)
{
	return boot_mode_process(str);
}

__setup("androidboot.mode=", boot1_mode_process);

int owl_get_boot_mode(void)
{
	return owl_boot_mode;
}
EXPORT_SYMBOL(owl_get_boot_mode);




static int bootmode_proc_show(struct seq_file *m, void *v)
{
	
	if (owl_boot_mode == BOOT_MODE_UPGRADE) {
		seq_printf(m, "%s\n", "upgrade"); 
	}else if (owl_boot_mode == BOOT_MODE_CHARGER) {
		seq_printf(m, "%s\n", "charger"); 
	}else if (owl_boot_mode == BOOT_MODE_RECOVERY) {
		seq_printf(m, "%s\n", "recovery"); 
	}else {
		seq_printf(m, "%s\n", "normal"); 
	}
	return 0;
}

static int bootmode_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootmode_proc_show, NULL);
}

static const struct file_operations bootmode_proc_fops = {
	.open		= bootmode_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_bootmode_init(void)
{
	proc_create("bootmode", 0, NULL, &bootmode_proc_fops);
	return 0;
}

module_init(proc_bootmode_init);
/*
 *
 *   vendor id (boardinfo)
 *
 */

static int owl_vendor_bool;
static char owl_vendor_str[17];
static int __init boot_vendor_process(char *str)
{
	int len = strlen(str);

	len = (len < 16 ? len : 16);
	strncpy(owl_vendor_str, str, len);
	owl_vendor_bool = 1;
	printk("boardinfo,%d=%s\n", len, str);

	return 1;
}
__setup("androidboot.boardinfo=", boot_vendor_process);

int owl_get_boardinfo(char *buf)
{
	if (owl_vendor_bool) {
		memcpy(buf, owl_vendor_str, 16);
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(owl_get_boardinfo);


#define WIFI_POS					14
#define DEFAULT_WIFI_VENDOR			0x00
#define WIFI_VENDOR_ID_MASK			0x1F

static unsigned char decode_wifi_vendor_id(char *buf)
{
	unsigned char wifi_id = DEFAULT_WIFI_VENDOR;

	if (((('0' <= buf[0]) && ('9' >= buf[0]))
		|| (('a' <= buf[0]) && ('f' >= buf[0]))
		|| (('A' <= buf[0]) && ('F' >= buf[0])))
		&& ((('0' <= buf[1]) && ('9' >= buf[1]))
		|| (('a' <= buf[1]) && ('f' >= buf[1]))
		|| (('A' <= buf[1]) && ('F' >= buf[1])))) {

		if (buf[0] >= 'a')
			wifi_id = buf[0] - 'a' + 10;
		else if (buf[0] >= 'A')
			wifi_id = buf[0] - 'A' + 10;
		else
			wifi_id = buf[0] - '0';

		wifi_id <<= 4;

		if (buf[1] >= 'a')
			wifi_id |= buf[1] - 'a' + 10;
		else if (buf[1] >= 'A')
			wifi_id |= buf[1] - 'A' + 10;
		else
			wifi_id |= buf[1] - '0';

		wifi_id &= WIFI_VENDOR_ID_MASK;
		printk("%s:0x%x\n", __func__, wifi_id);
	} else {
		printk("%s error:0x%x,0x%x\n", __func__, buf[0], buf[1]);
	}

	return wifi_id;
}


unsigned char owl_get_wifi_vendor_id(void)
{
	unsigned char vendor_id = DEFAULT_WIFI_VENDOR;

	if (owl_vendor_bool)
		return decode_wifi_vendor_id(&owl_vendor_str[WIFI_POS]);
	else
		return vendor_id;
}
EXPORT_SYMBOL(owl_get_wifi_vendor_id);


#include <linux/mfd/atc260x/atc260x.h>
static unsigned int g_chr_flag = 0;
static int chrmode_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_chr_flag); 
	return 0;
}

static int chrmode_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, chrmode_proc_show, NULL);
}

static const struct file_operations chrmode_proc_fops = {
	.open		= chrmode_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_serialno_init(void)
{

	atc260x_ex_pstore_get(ATC260X_PSTORE_TAG_ENTER_CHARGER, &g_chr_flag);
	printk("g_chr_flag=%d\n", g_chr_flag);
	atc260x_ex_pstore_set(ATC260X_PSTORE_TAG_ENTER_CHARGER, 0);
	proc_create("charger", 0, NULL, &chrmode_proc_fops);
	return 0;
}

module_init(proc_serialno_init);
