/*
 * Actions OWL SoC Power Manager driver
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
#define DEBUG
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/pm.h>
#include <linux/seq_file.h>
#include <linux/kobject.h>
#include <linux/power_supply.h>
#include <linux/owl_pm.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#include <asm/system_misc.h>
#include <asm/smp_scu.h>
#include <linux/module.h>
#include <mach/hardware.h>
#include <asm/sections.h>

static struct owl_pmic_pm_ops *s_pmic_pm_ops = NULL;


int owl_pmic_setup_aux_wakeup_src(uint wakeup_src, uint on)
{

	return 0;
}

EXPORT_SYMBOL_GPL(owl_pmic_setup_aux_wakeup_src);


enum owl_suspend_mode {
	OWL_SUSPEND_NONE = 0,
	OWL_SUSPEND_LP2,	/* CPU voltage off */
	OWL_SUSPEND_LP1,	/* CPU voltage off, DRAM self-refresh */
	OWL_SUSPEND_LP0,	/* CPU + core voltage off, DRAM self-refresh */
	OWL_MAX_SUSPEND_MODE,
};

static const char *owl_suspend_name[OWL_MAX_SUSPEND_MODE] = {
	[OWL_SUSPEND_NONE] = "none",
	[OWL_SUSPEND_LP2] = "lp2",
	[OWL_SUSPEND_LP1] = "lp1",
	[OWL_SUSPEND_LP0] = "lp0",
};

/* default suspend mode is enter S2(OWL_SUSPEND_LP0) */
static enum owl_suspend_mode current_suspend_mode = OWL_SUSPEND_LP0;

static int owl_suspend_valid(suspend_state_t state)
{
	int valid = 1;

	pr_info("%s %d:\n", __func__, __LINE__);

	return valid;
}

static int owl_suspend_prepare_late(void)
{
	pr_info("[PM] %s %d:\n", __func__, __LINE__);
	return s_pmic_pm_ops->suspend_prepare();
}


static void set_suspend_wakup_src(void)
{
	union power_supply_propval ret = { 0, };
	struct power_supply *psy;
	unsigned int wakesrc;
	int wall_in = 0, usb_in = 0;

	wakesrc = OWL_PMIC_WAKEUP_SRC_RESET |
		OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT |
		OWL_PMIC_WAKEUP_SRC_ONOFF_LONG |
		OWL_PMIC_WAKEUP_SRC_ALARM |
		OWL_PMIC_WAKEUP_SRC_IR;

	psy = power_supply_get_by_name("atc260x-wall");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		wall_in = ret.intval;

	if (!wall_in)
		wakesrc |= OWL_PMIC_WAKEUP_SRC_WALL_IN;

	psy = power_supply_get_by_name("atc260x-usb");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		usb_in = ret.intval;

	if (!usb_in)
		wakesrc |= OWL_PMIC_WAKEUP_SRC_VBUS_IN;
	else
		wakesrc |= OWL_PMIC_WAKEUP_SRC_VBUS_OUT;

	pr_info("[PM] %s: wall_in(%d) usb_in(%d), suspend wakeup src 0x%x\n",
		__func__, wall_in, usb_in, wakesrc);

	if (s_pmic_pm_ops != NULL ) {
		s_pmic_pm_ops->set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ALL, wakesrc);

	}

}


/*
 * Here we need save/restore the core registers that are
 * either volatile or reset to some state across a processor sleep.
 * If reading a register doesn't provide a proper result for a
 * later restore, we have to provide a function for loading that
 * register and save a copy.
 *
 * We only have to save/restore registers that aren't otherwise
 * done as part of a driver pm_* function.
 */

struct CORE_REG {
	u32 reg_addr;
	u32 reg_val;
};

static struct CORE_REG core_regs[] = {
	/* <1> CMU register */
	{CMU_COREPLL, 0x0},
	{CMU_BUSCLK, 0x0},
    {CMU_DEVPLL, 0x0},
//    {CMU_NANDPLL, 0x0},
    {CMU_DISPLAYPLL, 0x0},
    {CMU_AUDIOPLL, 0x0},
    {CMU_TVOUTPLL, 0x0},
    {CMU_SENSORCLK, 0x0},
    {CMU_LCDCLK, 0x0},
    {CMU_DECLK, 0x0},
    {CMU_SICLK, 0x0},
    {CMU_VDECLK, 0x0},
    {CMU_VCECLK, 0x0},
    {CMU_GPUCLK, 0x0},
//    {CMU_NANDCCLK, 0x0},
    {CMU_SD0CLK, 0x0},
    {CMU_SD1CLK, 0x0},
    {CMU_SD2CLK, 0x0},
    {CMU_UART0CLK, 0x0},
    {CMU_UART1CLK, 0x0},
    {CMU_UART2CLK, 0x0},
    {CMU_DMACLK, 0x0},
    {CMU_PWM0CLK, 0x0},   
    {CMU_PWM1CLK, 0x0},
    {CMU_PWM2CLK, 0x0},
    {CMU_PWM3CLK, 0x0},
    {CMU_USBCLK, 0x0},
    {CMU_120MPLL, 0x0},
    {CMU_DEVCLKEN0, 0x0},
    {CMU_DEVCLKEN1, 0x0},
    {CMU_DEVRST0, 0x0},
    {CMU_DEVRST1, 0x0},
    {CMU_UART3CLK, 0x0},
    {CMU_UART4CLK, 0x0},
    {CMU_UART5CLK, 0x0},
 
    /*Timer*/
	{T0_VAL, 0x0},    
    {T0_CTL, 0x0},
	{T0_CMP, 0x0},
	{TWOHZ0_CTL, 0x0},

    
	/* <3> DMA configuration register */
	{DMA_CTL, 0x0},
	{DMA_IRQEN, 0x0},
//	{DMA_IRQPD, 0x0},
	{DMA_PAUSE_REG, 0x0},
//	{CMU_DMACLK, 0x0},

	/* <4> Multiplex pin control register */
	{GPIO_ADAT, 0x0},
	{GPIO_AOUTEN, 0x0},
	{GPIO_AINEN, 0x0},
	{GPIO_BDAT, 0x0},
	{GPIO_BOUTEN, 0x0},
	{GPIO_BINEN, 0x0},
	{GPIO_CDAT, 0x0},
	{GPIO_COUTEN, 0x0},
	{GPIO_CINEN, 0x0},
	{GPIO_DDAT, 0x0},
	{GPIO_DOUTEN, 0x0},
	{GPIO_DINEN, 0x0},

	/*PWM duty */
	{PWM_CTL0, 0x0},
	{PWM_CTL1, 0x0},
	{PWM_CTL2, 0x0},
	{PWM_CTL3, 0x0},
	
	{MFP_CTL0, 0x0},
	{MFP_CTL1, 0x0},
    {MFP_CTL2, 0x0},
    {MFP_CTL3, 0x0},
    {PAD_PULLCTL0, 0x0},
    {PAD_PULLCTL1, 0x0},
    {PAD_PULLCTL2, 0x0},


    /*INTC*/
    {INTC_EXTCTL, 0x0},
    {INTC_GPIOCTL, 0x0},
    {INTC_GPIOA_MSK, 0x0},
    {INTC_GPIOB_MSK, 0x0},
    {INTC_GPIOC_MSK, 0x0},
    {INTC_GPIOD_MSK, 0x0},

	/* <5> UART control register */
	{UART0_CTL, 0x0},
	{UART1_CTL, 0x0},
    {UART2_CTL, 0x0},
    {UART3_CTL, 0x0},
    {UART4_CTL, 0x0},
    {UART5_CTL, 0x0},
    
    /*Shareram CTL*/
    {SHARESRAM_CTL, 0x0},
    
    /* Pad_drv*/
    {PAD_DRV0, 0x0},
    {PAD_DRV1, 0x0},
    {PAD_DRV2, 0x0},

	/* Terminate tag */
	{0x0, 0x0}
};

void save_core_regs(void)
{
	/* save core register */
	s32 index = 0, num;
	num = sizeof(core_regs) / sizeof(core_regs[0]);
	pr_info("enter save_core_regs\n");

	while (index < num) {
		if (core_regs[index].reg_addr != 0) {
			core_regs[index].reg_val =
			act_readl(core_regs[index].reg_addr);
			pr_debug("register<%2d>[%#x] = 0%#x\n", index,
			core_regs[index].reg_addr,
			core_regs[index].reg_val);
		}
		index++;
	}
}
void owl_switch_jtag(void)
{
    act_writel((act_readl(MFP_CTL2) & (~((0x3<<5) | (0x3<<7) | (0x7<<11) | (0x7<<17))))
        | ((0x2<<5) | (0x3<<7) | (0x3<<11) | (0x3<<17)), MFP_CTL2);
}


static void restore_corepll_busclk(unsigned int corepll_val, unsigned int busclk_val, unsigned int devpll_value)
{
	unsigned int val, tmp;

	pr_debug("\n %s corepll:0x%x, busclk:0x%x, devpll:0x%x", __FUNCTION__, 
		act_readl(CMU_COREPLL), act_readl(CMU_BUSCLK),  act_readl(CMU_DEVPLL));    	
	pr_debug("\n [zjl] enter %s corepll_val:0x%x, busclk_val:0x%x, devpll_value:0x%x", 
		__FUNCTION__, corepll_val, busclk_val, devpll_value);


	/*restore devpll first*/
	/*先enable devpll，然后delay 50us*/
	tmp = act_readl(CMU_DEVPLL);
	tmp &= ~(0x7f);
	tmp |= (devpll_value&0x7f);
	tmp |= (0x1<<8);
	act_writel(tmp, CMU_DEVPLL);
	udelay(100);


	/*恢复busclk中，除了bit[1:0]的部分*/
	tmp = act_readl(CMU_BUSCLK);
	tmp &= (0x3);
	tmp |= (busclk_val & (~0x3));
	act_writel(tmp, CMU_BUSCLK);

	act_writel(act_readl(CMU_DEVPLL)| (0x1<<12), CMU_DEVPLL);
	udelay(50);
	/*restore vce_clk_before_gating*/
	act_writel(0x0, CMU_VCECLK);
	    
	/* cpuclk switch to vce_clk_before_gating */
	val = act_readl(CMU_BUSCLK);
	val &= ~(0x3<<0);
	val |= (0x3<<0);
	act_writel(val, CMU_BUSCLK);

	udelay(1);

	/* restore corepll register */
	act_writel(corepll_val, CMU_COREPLL);

	udelay(100);

	/* restore busclk register bit[1:0] */
	tmp = act_readl(CMU_BUSCLK);
	tmp &= (~0x3);
	tmp |= (busclk_val & 0x3);
	act_writel(tmp, CMU_BUSCLK);
	act_readl(CMU_BUSCLK);
	udelay(10);

	pr_debug("\n %s %d, corepll:0x%x, busclk:0x%x, devpll:0x%x", __FUNCTION__,__LINE__, 
		act_readl(CMU_COREPLL), act_readl(CMU_BUSCLK),  act_readl(CMU_DEVPLL));    	
}

void restore_core_regs(void)
{
	/* restore core register */
	s32 index = 0, num;
	num = sizeof(core_regs) / sizeof(core_regs[0]);

	pr_debug("enter restore_core_regs\n");

	BUG_ON(num < 3);
	BUG_ON(core_regs[0].reg_addr != CMU_COREPLL);
	BUG_ON(core_regs[1].reg_addr != CMU_BUSCLK);

	restore_corepll_busclk(core_regs[0].reg_val, core_regs[1].reg_val, core_regs[2].reg_val);

	index = 3;
	while (index < num) {
		pr_debug("\n reg[%d], reg_addr:0x%x, reg_val:0x%x", index, core_regs[index].reg_addr, core_regs[index].reg_val);
		if (core_regs[index].reg_addr != 0) {
			act_writel(core_regs[index].reg_val,
			core_regs[index].reg_addr);
			pr_debug("register<%2d>[%#x] = %#x\n", index,
			core_regs[index].reg_addr,
			act_readl(core_regs[index].reg_addr));
		}
		index++;
	}
}

#define PM_SYMBOL(symbol)  \
	EXPORT_SYMBOL(symbol)
typedef void (*finish_suspend_t)(unsigned long pnu_type, unsigned long i2c_base);
/*
	called by cpu_suspend。
	first should copy the real code to address 0xffff8000.
	the mem is configured to PT_MEM, and can run without DDR
  */
  PM_SYMBOL(owl_cpu_resume);


int owl_finish_suspend_enter(unsigned long pmu_type)
{    
	unsigned long i2c_base;
	/*测试使用shareram来实现*/
	finish_suspend_t func_run = (finish_suspend_t)0xffff8000;

	/*
	将shareram切换到AHB通路访问
	注意shareram 可用条件：
	1. shareram power enable
	2. shareram clk enable
	3. shareram ctl switch ok
	*/

	/*2. check shareram clk is enabled */ 
	if((act_readl(CMU_DEVCLKEN0) & (0x1<<28)) == 0)
	{
		act_writel((act_readl(CMU_DEVCLKEN0) | (0x1<<28)), CMU_DEVCLKEN0);
	}

	/*3. switch module*/
	act_writel(0x0, SHARESRAM_CTL);

	pr_info("\n %s SPS_PG_CTL:0x%x, CMU_DEVCLKEN0:0x%x, SHARESRAM_CTL:0x%x", __FUNCTION__, act_readl(SPS_PG_CTL), act_readl(CMU_DEVCLKEN0), act_readl(SHARESRAM_CTL));


	flush_cache_all();	
	memcpy((void*)func_run, (void*)owl_finish_suspend, 0x1000);
	flush_cache_all();
	/*跳转到leopard_real_finish_suspend执行*/


	i2c_base = IO_ADDRESS(I2C2_BASE);
	printk("\n pmu_type=%ld ,pmu_i2c_base=0x%lx  %s %d\n",pmu_type,i2c_base,__FUNCTION__,__LINE__);
	func_run(0, i2c_base);

	return 0;
}

static unsigned int g_ddr_checksum;
#define DDR_CHCEK_MAXLEN 0x100000

int c_calc_ddr_checksum(void)
{
	unsigned int  addr, start_addr, end_addr, checksum=0;

	start_addr = (unsigned int)_text;
	end_addr = start_addr+DDR_CHCEK_MAXLEN;
	for(addr = start_addr; addr < end_addr ; addr+=4) {
		checksum += *(volatile unsigned int *)addr;
	}
	pr_alert("cal:0x%x-0x%x, checksum:0x%x\n",start_addr, end_addr,  checksum);
	g_ddr_checksum = checksum;
	return 0;
}
int c_check_ddr_checksum(void)
{
	unsigned int  addr, start_addr, end_addr, checksum=0;
	start_addr = (unsigned int)_text;
	end_addr = start_addr+DDR_CHCEK_MAXLEN;
	for(addr = start_addr; addr < end_addr ; addr+=4) {
		checksum += *(volatile unsigned int *)addr;
	}
	pr_alert("checksum:0x%x vs 0x%x\n", g_ddr_checksum, checksum);

	if(g_ddr_checksum != checksum)  {
		pr_alert("ddr checksum err\n");
		while(1);
	}else{
		pr_alert("ddr checksum ok\n");
	}
	return 0;
}

static int owl_do_suspend(suspend_state_t state)
{
	int ret;
	unsigned int wakeup_src;

	set_suspend_wakup_src();
	ret = s_pmic_pm_ops->suspend_enter();
	if(ret) {
		pr_err("[PM] %s() PMIC enter suspend failed, ret=%d", __func__, ret);
		return ret;
	}

	//ddr_prio = asoc_get_ddr_prio();
	save_core_regs();
	/* save and restore the cpu core register */
	c_calc_ddr_checksum();
	owl_switch_jtag();
	cpu_suspend(0, owl_finish_suspend_enter);	
	c_check_ddr_checksum();
	restore_core_regs();

#ifdef CONFIG_SMP	
    scu_enable((void *)IO_ADDRESS(ASOC_PA_SCU));
#endif

	//asoc_set_ddr_prio(ddr_prio);
	if (s_pmic_pm_ops && s_pmic_pm_ops->set_wakeup_src) {
		wakeup_src = 0;
		pr_info("\nwakesrc: 0x%x\n", wakeup_src);
		/*xyl :alarm wakesource has some problem, so forbidden it until it's OK*/
		s_pmic_pm_ops->set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT, wakeup_src);
	}

	/* DO NOT call s_pmic_pm_ops here, it's not ready at this moment! */

	return 0;

}

static int owl_suspend_enter(suspend_state_t state)
{
	pr_info("%s %d: state %d\n", __func__, __LINE__, state);
	switch (state) {
	case PM_SUSPEND_MEM:
		owl_do_suspend(state);
		break;

	default:
		return -EINVAL;
	}

	pr_info("%s %d: state %d\n", __func__, __LINE__, state);

	return 0;
}
static void owl_suspend_wake(void)
{
	pr_info("[PM] %s %d:\n", __func__, __LINE__);
	s_pmic_pm_ops->suspend_wake();
	s_pmic_pm_ops->set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ONOFF_SHORT, 0);
}
static void owl_suspend_finish(void)
{
	pr_info("[PM] %s %d:\n", __func__, __LINE__);
	s_pmic_pm_ops->suspend_finish();
}

static const struct platform_suspend_ops owl_suspend_ops = {
	.prepare_late = owl_suspend_prepare_late,
	.valid = owl_suspend_valid,
	.wake = owl_suspend_wake,
	.finish = owl_suspend_finish,
	.enter = owl_suspend_enter,
};

static ssize_t suspend_mode_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	char *start = buf;
	char *end = buf + PAGE_SIZE;

	start += scnprintf(start, end - start, "%s ",
			   owl_suspend_name[current_suspend_mode]);
	start += scnprintf(start, end - start, "\n");

	return start - buf;
}

static ssize_t suspend_mode_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t n)
{
	int len;
	const char *name_ptr;
	enum owl_suspend_mode new_mode;

	name_ptr = buf;
	while (*name_ptr && !isspace(*name_ptr))
		name_ptr++;
	len = name_ptr - buf;
	if (!len)
		goto bad_name;
	/* OWL_SUSPEND_NONE not allowed as suspend state */
	if (!(strncmp(buf, owl_suspend_name[OWL_SUSPEND_NONE], len))
	    || !(strncmp(buf, owl_suspend_name[OWL_SUSPEND_LP2], len))) {
		pr_info("Illegal owl suspend state: %s\n", buf);
		goto bad_name;
	}

	for (new_mode = OWL_SUSPEND_NONE;
	     new_mode < OWL_MAX_SUSPEND_MODE; ++new_mode) {
		if (!strncmp(buf, owl_suspend_name[new_mode], len)) {
			current_suspend_mode = new_mode;
			break;
		}
	}

bad_name:
	return n;
}

static struct kobj_attribute suspend_mode_attribute =
__ATTR(mode, 0644, suspend_mode_show, suspend_mode_store);

static struct kobject *suspend_kobj;




/**
 *      pmic_suspend_set_ops - Set the global suspend method table.
 *      @ops:   Pointer to ops structure.
 */
void owl_pmic_set_pm_ops(struct owl_pmic_pm_ops *ops)
{
	pr_info("[PM] set pmic suspend ops 0x%lx\n", (ulong) ops);
	if (ops != NULL )
		s_pmic_pm_ops = ops;
}

EXPORT_SYMBOL_GPL(owl_pmic_set_pm_ops);

int owl_pm_wakeup_flag(void)
{
	if (s_pmic_pm_ops != NULL )
		return s_pmic_pm_ops->get_wakeup_flag();
	else
		return 0;
}

EXPORT_SYMBOL_GPL(owl_pm_wakeup_flag);

#if 0
/*获取电池充电容量，返回0表示没有电池或者充电满了*/
static int owl_battery_get_cap(void)
{
	union power_supply_propval ret = {0,};
	 struct power_supply *psy;
 	 psy = power_supply_get_by_name("atc260x-battery");
	 if (psy == NULL ) {
	 	pr_info("get atc260x-battery fail\n");
	 	return 0;
	 }
	 if (psy->get_property(psy, POWER_SUPPLY_PROP_PRESENT, &ret)) {
		pr_info("get battery present fail\n");
		return 0;
	 }
	 pr_info("get battery present = %d\n", ret.intval);
	if (ret.intval == 0){ // 没电池
		pr_info("battery  not present\n");
		return 0;
	}

	if (psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &ret)) {
		pr_info("get battery capcity fail\n");
		return 0;
	}
	pr_info("get battery capcity = %d\n", ret.intval);

	if (ret.intval == 100)// 充满电
		return 0;

	return -1;
}
#endif

static void owl_pm_power_off(int enter_charge)
{
	int wakeup_src;
	int mode = 0;

	/* default sleep mode and wakeup source */
	/*xyl :alarm wakesource has some problem, so forbidden it until it's OK */
	wakeup_src = OWL_PMIC_WAKEUP_SRC_RESET |
	    OWL_PMIC_WAKEUP_SRC_ONOFF_LONG | OWL_PMIC_WAKEUP_SRC_IR;

	if (enter_charge)
		wakeup_src |= OWL_PMIC_WAKEUP_SRC_WALL_IN |
		    OWL_PMIC_WAKEUP_SRC_VBUS_IN;

        if (!power_supply_is_system_supplied()) {
			printk("wall/usb not connect, ennter s4\n");
            		mode = 1;

        } else {
			printk("wall/usb  connect, ennter s3\n");
			mode = 0;
	}
	/* Power off system */
	pr_info("[PM] %s: Powering off (wakesrc: 0x%x)\n",
		__func__, wakeup_src);

	if (s_pmic_pm_ops != NULL ) {
		s_pmic_pm_ops->set_wakeup_src(OWL_PMIC_WAKEUP_SRC_ALL, wakeup_src);
	 	s_pmic_pm_ops->powerdown(mode , 0);
	}

	/* never return to here */
	pr_err("[PM] %s() failed\n", __func__);
	while (1) ;
}


static void owl_pm_sys_poweroff(void)
{
	union power_supply_propval ret = { 0, };
	struct power_supply *psy;
	int battery_online = 0;

	psy = power_supply_get_by_name("battery");
	if (psy && !psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
		battery_online = ret.intval;

	pr_info("[PM] %s() poweroff, battery online %d\n", __func__,
		battery_online);

	/* if battery exist, we need to enter charger mode */
	if (battery_online) {
		owl_pm_power_off(1);
	} else {
		owl_pm_power_off(0);
	}
}


static void owl_pm_sys_reset(char str, const char *cmd)
{
	unsigned int tgt = OWL_PMIC_REBOOT_TGT_NORMAL;
	pr_info("[PM] %s() cmd: %s\n", __func__, cmd ? cmd : "<null>");

	if (cmd) {
		if (!strcmp(cmd, "recovery")) {
			pr_info("cmd:%s----restart------\n", cmd);
			tgt = OWL_PMIC_REBOOT_TGT_RECOVERY;
		} else if (!strcmp(cmd, "adfu")) {
			pr_info("cmd:%s----restart------\n", cmd);
			tgt = OWL_PMIC_REBOOT_TGT_ADFU;
		} else if (!strcmp(cmd, "reboot")) {
			pr_info("cmd:%s----restart------\n", cmd);
			tgt = OWL_PMIC_REBOOT_TGT_SYS;	/* no charger */
		} else if (!strcmp(cmd, "upgrade_reboot")) {
			pr_info("cmd:%s----upgrade_reboot------\n", cmd);
			if (s_pmic_pm_ops != NULL )
				s_pmic_pm_ops->shutdown_prepare_upgrade();
			tgt =  OWL_PMIC_REBOOT_TGT_SYS;	/* no charger */
		} else if (!strcmp(cmd, "upgrade_halt")) {
			pr_info("cmd:%s----halt------\n", cmd);
			if (s_pmic_pm_ops != NULL )
				s_pmic_pm_ops->shutdown_prepare_upgrade();
			owl_pm_power_off(0);
		} else if (!strcmp(cmd, "bootloader")) {
			pr_info("cmd:%s----restart------\n", cmd);
			tgt = OWL_PMIC_REBOOT_TGT_BOOTLOADER;
		} else if (!strcmp(cmd, "fastboot")) {
			pr_info("cmd:%s----restart------\n", cmd);
			tgt = OWL_PMIC_REBOOT_TGT_FASTBOOT;
		}
	}

	if (s_pmic_pm_ops != NULL )
		 s_pmic_pm_ops->reboot(tgt);

	pr_err("[PM] %s() failed\n", __func__);
}


static int __init ats3605_pm_init(void)
{
	pr_info("[ats3605] pm init\n");

	/* create /sys/power/suspend/type */
	suspend_kobj = kobject_create_and_add("suspend", power_kobj);
	if (suspend_kobj) {
		if (sysfs_create_file(suspend_kobj,
				      &suspend_mode_attribute.attr))
			pr_err("%s: sysfs_create_file suspend type failed!\n",
			       __func__);
	}
	suspend_set_ops(&owl_suspend_ops);

	arm_pm_restart = owl_pm_sys_reset;
	pm_power_off = owl_pm_sys_poweroff;

	return 0;

}

late_initcall(ats3605_pm_init);
