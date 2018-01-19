/*
 * Actions OWL SoC Power domain controller driver
 *
 * Copyright (C) 2014 Actions Semi Inc.
 *  <liaotianyang@actions-semi.com>
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

#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_clock.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <mach/hardware.h>
#include "pd.h"
#include <dt-bindings/pm-domains/pm-domains-ats3605.h>

/* SPS Registers Offset */
#define SPS_PG_CTL_OFF			0x0000
#define SPS_PG_ACK_OFF			0x0004
extern spinlock_t  sps_lock;


static struct owl_pm_domain owl_pm_domain_gpu =
	OWL_DOMAIN(POWER_DOMAIN_GPU, "gpu", SPS_PG_CTL_OFF, 2, SPS_PG_ACK_OFF, 2, NULL);
static struct owl_pm_domain owl_pm_domain_vce =
	OWL_DOMAIN(POWER_DOMAIN_VCE, "vce", SPS_PG_CTL_OFF, 4, SPS_PG_ACK_OFF, 4, NULL);
static struct owl_pm_domain owl_pm_domain_vde =
	OWL_DOMAIN(POWER_DOMAIN_VDE, "vde", SPS_PG_CTL_OFF, 5, SPS_PG_ACK_OFF, 5, NULL);
static struct owl_pm_domain owl_pm_domain_cpu1 =
	OWL_DOMAIN(POWER_DOMAIN_CPU1, "cpu1", SPS_PG_CTL_OFF, 0, SPS_PG_ACK_OFF, 0, NULL);
static struct owl_pm_domain owl_pm_domain_cpu2 =
	OWL_DOMAIN(POWER_DOMAIN_CPU2, "cpu2", SPS_PG_CTL_OFF, 1, SPS_PG_ACK_OFF, 1, NULL);
static struct owl_pm_domain owl_pm_domain_cpu3 =
	OWL_DOMAIN(POWER_DOMAIN_CPU3, "cpu3", SPS_PG_CTL_OFF, 8, SPS_PG_ACK_OFF, 8, NULL);



static struct owl_pm_domain *owl_pm_domains[] = {
	[POWER_DOMAIN_GPU] = &owl_pm_domain_gpu,
	[POWER_DOMAIN_CPU1] = &owl_pm_domain_cpu1,
	[POWER_DOMAIN_CPU2] = &owl_pm_domain_cpu2,
	[POWER_DOMAIN_CPU3] = &owl_pm_domain_cpu3,
	[POWER_DOMAIN_VCE] = &owl_pm_domain_vce,
	[POWER_DOMAIN_VDE] = &owl_pm_domain_vde,
};


static unsigned int owl_cpu_domains[] = {
	0xffffffff,
	POWER_DOMAIN_CPU1,
	POWER_DOMAIN_CPU2,
	POWER_DOMAIN_CPU3,
};
/*smp cpu power on/off before powergate init,so export function owl_cpu_powergate_power to smp*/
int owl_cpu_powergate_power(int cpu, bool cpu_on)
{
	unsigned long val, flags;
    	int timeout, id, cpu_id;
	bool ack_is_on; 	

	if ( cpu < 1 || cpu > 3) {
		pr_err("[CPU power] cpu %d invalid\n", cpu);
		return -1;
	}
	cpu_id = owl_cpu_domains[cpu];
	
	id = owl_pm_domains[cpu_id]->ack.bit_idx;
	spin_lock_irqsave(&sps_lock, flags);
	ack_is_on = !!(act_readl(SPS_PG_ACK) & (1 << id));
	if (ack_is_on == cpu_on) {
		spin_unlock_irqrestore(&sps_lock, flags);
		printk("cpu=%d is already state=%d\n", cpu, ack_is_on);
		return 0;
	}
	val = act_readl(SPS_PG_CTL);
	if (cpu_on)
	    val |= (1 << id);
	else
	    val &= ~(1 << id);
	act_writel(val, SPS_PG_CTL);

	timeout = 5000;  /* 5ms */
	while (timeout > 0) {
		ack_is_on = (act_readl(SPS_PG_ACK) & (1 << id));
		if (ack_is_on == cpu_on)
			break;
		udelay(50);
		timeout -= 50;
	}
	if (timeout <= 0) {
		pr_err("[CPU power] power(%d) for cpuid(%d) timeout\n", cpu_on, cpu_id);
	}
	spin_unlock_irqrestore(&sps_lock, flags);
	return 0;
}
EXPORT_SYMBOL(owl_cpu_powergate_power);

static struct owl_pm_domain_info owl_pm_domain_info = {
	.num_domains = ARRAY_SIZE(owl_pm_domains),
	.domains = owl_pm_domains,
};

static int owl_pm_domain_probe(struct platform_device *pdev)
{
	pr_info("ats3605 owl_pm_domain init\n");
	owl_pm_domain_info.sps_base = of_iomap(pdev->dev.of_node, 0);
	owl_pm_domain_info.of_node = pdev->dev.of_node;
	return owl_pm_domain_register(&owl_pm_domain_info);
}

static const struct of_device_id owl_of_match[] = {
	{ .compatible = "actions,ats3605-pm-domains" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, owl_of_match);

static struct platform_driver owl_pd_platform_driver = {
	.driver = {
		.name = "ats3605-pm-domains",
		.owner = THIS_MODULE,
		.of_match_table = owl_of_match,
	},
	.probe = owl_pm_domain_probe,
};

static __init int owl_pm_domain_init(void)
{
	return platform_driver_register(&owl_pd_platform_driver);
}
arch_initcall(owl_pm_domain_init);
