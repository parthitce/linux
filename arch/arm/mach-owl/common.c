/*
 * arch/arm/mach-owl/common.c
 *
 * ACTIONS OWL SoC device tree board support
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 */


#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/irqchip.h>
#include <asm/hardware/cache-l2x0.h>


void owl_assert_system_reset(char mode, const char *cmd)
{

}


void __init owl_init_cache(void)
{
#ifdef CONFIG_CACHE_L2X0
	printk(KERN_INFO "%s()\n", __func__);
	l2x0_of_init(0x3e000000, 0xc00f0fff);
#endif
}

early_initcall(owl_init_cache);
