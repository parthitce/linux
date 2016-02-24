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
#include <linux/string.h>

/*
 * boot mode:
 *   0: normal
 *   1: upgrade
 */
static int owl_boot_mode = 0;

static int __init boot_mode_process(char *str)
{
	if (strcmp(str, "upgrade") == 0) {
		printk("owl bootmode: upgrade\n");
		owl_boot_mode = 1;
	}
	return 1;
}
__setup("bootmode=", boot_mode_process);

int owl_get_boot_mode(void)
{
	return owl_boot_mode;
}
EXPORT_SYMBOL(owl_get_boot_mode);
