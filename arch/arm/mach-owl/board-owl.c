/*
 * ACTIONS OWL SoC device tree board support
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 */

#include <linux/clocksource.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/irqchip.h>
#include <linux/clk-provider.h>

#include <asm/localtimer.h>
#include <asm/smp_twd.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include "common.h"
#include "board-owl.h"


void __init owl_init_early(void)
{
	pr_info("%s ()\n", __func__);
	//owl_init_cache();

}

void __init owl_time_init(void)
{
	pr_info("%s()\n", __func__);
	of_clk_init(NULL);
	clocksource_of_init();
}

static struct of_device_id owl_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};
static void __init owl_machine_init(void)
{
	int ret;
	
	pr_info("%s()\n", __func__);
	ret = of_platform_populate(NULL, owl_dt_match_table, NULL, NULL);
	if (ret)
		pr_warn("of_platform_populate() fail\n");

}
static void __init owl_init_late(void)
{
	pr_info("%s()\n", __func__);
}

static const char * const owl_dt_board_compat[] = {
	"actions,owl",
	"actions,ats3605",
	NULL
};

extern bool __init owl_smp_init(void);

DT_MACHINE_START(OWL_DT, "ACTIONS OWL SoC (Flattened Device Tree)")
	.map_io		= owl_map_io,
	.smp_init       = owl_smp_init,
	.init_early	= owl_init_early,
	.init_irq	= irqchip_init,
	.init_time	= owl_time_init,
	.init_machine	= owl_machine_init,
	.init_late	= owl_init_late,
	.restart	= owl_assert_system_reset,
	.dt_compat	= owl_dt_board_compat,
MACHINE_END
