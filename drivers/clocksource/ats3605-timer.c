/*
 * 
 *
 * time0 use as clocksource timer1 use as tick
 * timer1 for time tick at boot stage
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/cpu.h>
#include <asm/sched_clock.h>

/* timer register offset */
#define T0_CTL		0x08
#define T0_CMP		0x0c
#define T0_VAL		0x10
#define T1_CTL		0x14
#define T1_CMP		0x18
#define T1_VAL		0x1c


static void __iomem *owl_timer_base;

/*
 * clocksource
 */
static cycle_t ats3605_read_timer(struct clocksource *cs)
{
	return (cycle_t)readl(owl_timer_base + T0_VAL);
}

static struct clocksource ats3605_clksrc = {
	.name		= "timer0",
	.rating		= 200,
	.read		= ats3605_read_timer,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Using this local implementation sched_clock which uses timer0
 * to get some better resolution when scheduling the kernel.
 */
static u64 notrace ats3605_read_sched_clock(void)
{
	return readl(owl_timer_base + T0_VAL);
}

/* Clockevent device: use one-shot mode */
static void ats3605_clkevt_mode(enum clock_event_mode mode,
				 struct clock_event_device *dev)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_err("%s: periodic mode not supported\n", __func__);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		writel(0, owl_timer_base + T1_CTL);
		writel(0, owl_timer_base + T1_VAL);
		writel(0, owl_timer_base + T1_CMP);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq */
		writel(0, owl_timer_base + T1_CTL);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int ats3605_clkevt_next(unsigned long evt, struct clock_event_device *ev)
{
	/* disable timer */
	writel(0x0, owl_timer_base + T1_CTL);

	/* writing the value has immediate effect */
	writel(0, owl_timer_base + T1_VAL);
	writel(evt, owl_timer_base + T1_CMP);

	/* enable timer & IRQ */
	writel(0x6, owl_timer_base + T1_CTL);

	return 0;
}

static struct clock_event_device ats3605_clkevt = {
	.name		= "timer1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating	 = 200,
	.set_mode	= ats3605_clkevt_mode,
	.set_next_event	= ats3605_clkevt_next,
};

/*
 * IRQ Handler for timer 1 of the MTU block.
 */
static irqreturn_t ats3605_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = dev_id;

	writel(1 << 0, owl_timer_base + T1_CTL); /* Interrupt clear reg */
	evdev->event_handler(evdev);
	
	return IRQ_HANDLED;
}

static struct irqaction ats3605_timer_irq = {
	.name		= "timer1_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= ats3605_timer_interrupt,
	.dev_id		= &ats3605_clkevt,
};

static void ats3605_check_and_reset_time0_in_cpu0_idle(unsigned long flag)
{
	static unsigned int val = 0;
	
	if(smp_processor_id() != 0 || (readl(owl_timer_base + T0_CTL) & 0x4) == 0)
		return;
	
	if(flag == IDLE_START)
		val = readl(owl_timer_base + T0_VAL);
	else if(flag == IDLE_END && readl(owl_timer_base + T0_VAL) == val)
	{
		pr_err("time0 crashed, now reset it.\n");
		writel(0, owl_timer_base + T0_CTL);
		while(readl(owl_timer_base + T0_CTL) & 0x4) 
		{
			//do nothing here, just wait
		}
		writel(val, owl_timer_base + T0_VAL);
		writel(0x4, owl_timer_base + T0_CTL);
		pr_err("time0 reset successed.\n");
	}
}

static int ats3605_idle_notifier(struct notifier_block *nb,
					     unsigned long val,
					     void *data)
{
	ats3605_check_and_reset_time0_in_cpu0_idle(val);
    return 0;
}

static struct notifier_block ats3605_idle_nb = {
    .notifier_call = ats3605_idle_notifier,
};

static void ats3605_register_idle_notifier(void)
{
	idle_notifier_register(&ats3605_idle_nb);
}

void __init ats3605_timer_init(struct device_node *np)
{
	unsigned long rate;
	int ret, irq;
	struct clk *timer_clk;

	pr_info("[ats3605] timer initialization\n");

	owl_timer_base = of_iomap(np, 0);
	if (!owl_timer_base) {
		pr_err("%s: failed to map registers\n", __func__);
		return;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("%s: failed to parse IRQ\n", __func__);
		return;
	}
	/* enable the clock of timer */
	//act_setl(1 << 27, CMU_DEVCLKEN1);
	timer_clk = of_clk_get(np, 0);
	if (IS_ERR(timer_clk)) {
		pr_err("%s: failed to get clk\n", __func__);
		return;
	}

	clk_prepare_enable(timer_clk);
	rate = clk_get_rate(timer_clk);
	//rate = 24000000;
	/* Timer 0 is the free running clocksource */
	writel(0, owl_timer_base + T0_CTL);
	writel(0, owl_timer_base + T0_VAL);
	writel(0, owl_timer_base + T0_CMP);
	writel(4, owl_timer_base + T0_CTL);
	
	sched_clock_register(ats3605_read_sched_clock, 32, rate);
	clocksource_register_hz(&ats3605_clksrc, rate);
	ats3605_register_idle_notifier();
	
	/* Timer 1 is used for events, fix according to rate */
	writel(0, owl_timer_base + T1_CTL);
	writel(0, owl_timer_base + T1_VAL);
	writel(0, owl_timer_base + T1_CMP);

	setup_irq(irq, &ats3605_timer_irq);
	ats3605_clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&ats3605_clkevt, rate,
					0xf, 0xffffffff);
}

CLOCKSOURCE_OF_DECLARE(ats3605, "actions,ats3605-timer",
		ats3605_timer_init);
