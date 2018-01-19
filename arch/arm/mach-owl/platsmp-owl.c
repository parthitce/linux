/*
 * arch/arm/mach-owl/platsmp-owl.c
 *
 * Platform file needed for Leopard. This file is based on arm
 * realview smp platform.
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/cpu.h>

#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <asm/localtimer.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include <mach/hardware.h>
#include <dt-bindings/pm-domains/pm-domains-ats3605.h>



#include <mach/hardware.h>
#define BOOT_FLAG					(0x55aa)


static DEFINE_SPINLOCK(boot_lock);
#define OWL_PA_SCU             (0xB0020000)
static void __iomem *scu_base_addr(void)
{
	return (void *)IO_ADDRESS(OWL_PA_SCU);
}

/*
 * control for which core is the next to come out of the secondary
 * boot "holding pen"
 */
//volatile int pen_release = -1;

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}



void __cpuinit owl_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();

	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

extern int owl_cpu_powergate_power(int cpu, bool cpu_on);
extern void owl_secondary_startup(void);


static unsigned int cpux_flag_regs[CONFIG_NR_CPUS] = {
    CPU1_ADDR,      /* CPU1 ADDR*/
    UNDEFINED_HANDLER_ADDR,      /* CPU2 ADDR*/
    PREFETCH_HANDLER_ADDR,      /* CPU3ADDR */
};
#define CPUX_WFE_FLAG 0x2222
static int wait_cpux_wfe(unsigned int cpu)
{
	unsigned int flag_offs;
	unsigned int flag_val , i;

	flag_offs = cpux_flag_regs[cpu - 1] + 4;

	for (i = 0; i < 1000; i++) {
		udelay(200);
		flag_val = act_readl(flag_offs);
		if (flag_val == CPUX_WFE_FLAG)
			break;
	}
	if ( i == 1000) {
		printk(KERN_INFO "%s(): wake up cpu %d fail\n",
			__func__, cpu);
	}

	return 0;
}

static void wakeup_secondary(unsigned int cpu)
{
	//enum owl_powergate_id cpuid;
	if (cpu < 1 || cpu >= CONFIG_NR_CPUS){
		printk(KERN_INFO "%s(): invalid cpu number %d\n",
			__func__, cpu);
		return;
	}


	pr_info("po %d\n", cpu);

	owl_cpu_powergate_power(cpu  ,true);

	/* wait CPUx run to WFE instruct */
	wait_cpux_wfe(cpu);

	pr_info("wakeup  %d\n", cpu);

	/*
	 * write the address of secondary startup into the boot ram register
	 * at offset 0x204/0x304, then write the flag to the boot ram register
	 * at offset 0x200/0x300, which is what boot rom code is waiting for.
	 * This would wake up the secondary core from WFE
	 */

	act_writel(virt_to_phys(owl_secondary_startup), cpux_flag_regs[cpu-1]);
	act_writel(BOOT_FLAG, cpux_flag_regs[cpu-1]+4);

	/*
	 * Send a 'sev' to wake the secondary core from WFE.
	 * Drain the outstanding writes to memory
	 */
	dsb_sev();
	mb();
}

int __cpuinit owl_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	printk(KERN_INFO "owl_boot_secondary cpu:%d\n",  cpu);

	wakeup_secondary(cpu);

	/* wait for CPUx wakeup */
	udelay(10);

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 */
	write_pen_release(cpu_logical_map(cpu));
	smp_send_reschedule(cpu);

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		if (pen_release == -1)
			break;
	}

	switch (cpu) {
	case 1:
		act_writel(0, CPU1_ADDR);
		act_writel(0, CPU1_FLAG);
		break;
	case 2:
		act_writel(0, CPU2_ADDR);
		act_writel(0, CPU2_FLAG);
		break;
	case 3:
		act_writel(0, CPU3_ADDR);
		act_writel(0, CPU3_FLAG);
		break;
	default:
		printk(KERN_INFO "%s(): invalid cpu number %d\n",
			__func__, cpu);
		break;
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);
	printk(KERN_INFO "pr:%d\n", pen_release);

	return pen_release != -1 ? -ENOSYS : 0;
}

static bool powersave = false;

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */

void __init owl_smp_init_cpus(void)
{
	void __iomem *scu_base = scu_base_addr();
	unsigned int i, ncores;

	ncores = scu_base ? scu_get_core_count(scu_base) : 1;

	printk(KERN_INFO "%s(): ncores %d\n", __func__, ncores);

	/* sanity check */
	if (ncores > nr_cpu_ids) {
		printk(KERN_WARNING
			"[PLATSMP] no. of cores (%d) greater than configured "
			"maximum of %d - clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	if(powersave)
		ncores = 2;

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

}

void __init owl_smp_prepare_cpus(unsigned int max_cpus)
{
	printk(KERN_INFO "%s(max_cpus:%d)\n", __func__, max_cpus);

	scu_enable(scu_base_addr());
}

static int __init powersave_set(char *__unused)
{
	powersave = true;
	pr_alert("powersave now\n");
	return 0;
}
early_param("powersave", powersave_set);



 #ifdef CONFIG_HOTPLUG_CPU

static inline void cpu_enter_lowpower(void)
{
	unsigned int v;

	/*flush_cache_all();*/
	/*
	Invalidate all instruction caches to PoU. Also flushes branch target cache.
	Clean data cache line to PoC by VA.
	Disable data coherency with other cores in the Cortex-A5 MPCore processor.(ACTLR)
	Data caching disabled at all levels.(SCTLR)
	*/
	asm volatile(
	"	mcr	p15, 0, %1, c7, c5, 0\n"
	"	mcr	p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %3\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static inline void cpu_leave_lowpower(void)
{
	unsigned int v;
	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v)
	  : "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

extern void cpu_reset_to_broom( void );
static inline void platform_do_lowpower(unsigned int cpu)
{
	void __iomem *scu_base = scu_base_addr();
	/*cache clean*/
	flush_cache_all();
	
	/*exit smp mode*/
	cpu_enter_lowpower();
	
	/* we put the platform to just WFI */
	for (;;) {
		/*require power off(0x3), and this cpu will shutdown at next wfi*/
		if ((cpu >= 1) && (cpu < NR_CPUS)) {	
				scu_power_mode(scu_base, 0x3);
		}
		
		__asm__ __volatile__("dsb\n\t" "wfi\n\t"
				: : : "memory");
					
		if (pen_release == cpu_logical_map(cpu)) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}
	}
	cpu_leave_lowpower();
	printk("cpu[%d] power off failed\n", cpu);
}


int owl_cpu_kill(unsigned int cpu)
{
	return 1;
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void owl_cpu_die(unsigned int cpu)
{
	/* directly enter low power state, skipping secure registers */
	platform_do_lowpower(cpu);
}

int owl_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
#endif

static struct smp_operations owl_smp_ops =
{
#ifdef CONFIG_SMP
    .smp_init_cpus = owl_smp_init_cpus,
    .smp_prepare_cpus = owl_smp_prepare_cpus,
    .smp_secondary_init = owl_secondary_init,
    .smp_boot_secondary = owl_boot_secondary,
 #ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill = owl_cpu_kill,
	.cpu_die = owl_cpu_die,
	.cpu_disable = owl_cpu_disable,
#endif
#endif
};


bool __init owl_smp_init(void)
{
	pr_info("%s()\n", __func__);
 	smp_set_ops(&owl_smp_ops);
	return true;
}


