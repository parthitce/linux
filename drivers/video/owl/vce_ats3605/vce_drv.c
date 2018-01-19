/*
 * Actions OWL SoCs VCE driver
 *
 * Copyright (c) 2015 Actions Semiconductor Co., ltd.
 * Jed Zeng <zengjie@actions-semi.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/compat.h>
#include <linux/cpu_cooling.h>

#include "vce_drv.h"
#include "vce_reg.h"

#define VCE_INTTERUPT_MODE
#define Enable_Fix_VCE_Drv   1
#define Enable_Fix_CLK_Drv   1
#define Enable_Debug_PrintK   0
#define DEVDRV_NAME_VCE      "vce"  //device_driver.h
#define DEVICE_VCE           "/dev/vce"    //major.h
#define MINOR_VCE        32
#define _MAX_HANDLES_ 1024
#define IC_TYPE_5202E           0x5207  
#define VOL_INIT                1100000
#define VOL_MAX                 1200000


#ifndef IC_TYPE_ATM7039
#define IRQ_VCE 79       //???????
#else
#define IRQ_VCE  IRQ_ASOC_VCE/*IRQ_ASOC_H264_JPEG_ENCODER*/
#endif

#define PUT_USER                        put_user
#define GET_USER                        get_user
#define GET_USER_STRUCT      copy_from_user
#define SET_USER_STRUCT       copy_to_user
#define MIN(x,y)  ((x)<(y)?(x):(y))

#if Enable_Fix_VCE_Drv
#include <linux/sched.h>
#define  WAIT_TIMEOUT HZ  /*1s <--> HZ*/
#define  WAIT_TIMEOUT_MS  3000 /*ms*/
#endif

enum{
	STOPED,
	RUNNING,
	ERR,
};

struct ic_info {
	int ic_type;
};

typedef struct{
	vce_input_t  vce_in;
	vce_output_t vce_out;
	unsigned int vce_count;
	unsigned int vce_status;
	vce_multi_freq_t vce_freq;
}vce_info_t;

static DEFINE_MUTEX(vce_ioctl_mutex);
static DEFINE_MUTEX(vce_reset_mutex);
static struct completion vce_complete;
static int vce_status = 0;
static int vce_irq_registered = 0;
static int vce_open_count = 0;
static int vce_last_handle = 0;
static void *pAbuf[_MAX_HANDLES_];



#define enable_more_print 1	/* show more print */
#if enable_more_print
#define vce_info(fmt, ...) printk(KERN_WARNING "vce_drv: " fmt, ## __VA_ARGS__);
#else
#define vce_info(fmt, ...) {}
#endif
#define vce_warning(fmt, ...) \
	printk(KERN_WARNING "vce_drv: warning: " fmt, ## __VA_ARGS__)
#define vce_err(fmt, ...) \
	printk(KERN_ERR "vce_drv: %s(L%d) error: " fmt, __func__, __LINE__, ## __VA_ARGS__)


#if Enable_Fix_CLK_Drv
/*调频*/
#define  VCE_DEFAULT_WIDTH   320
#define  VCE_DEFAULT_HEIGHT  240
#define  VCE_DEFAULT_FREQ    110/*660/6*//*MHZ*/
static int vce_clk_isEnable = 0;
static vce_multi_freq_t vce_freq_multi = {VCE_DEFAULT_WIDTH,VCE_DEFAULT_HEIGHT,VCE_DEFAULT_FREQ}; 
static int vce_freq_is_init = 0;
static int ic_type = -1;

/*调压*/
#define  VCE_VDD_1080P_ATM7021_L_1  975000/*uv*/
#define  VCE_VDD_1080P_ATM7021_L_3  1025000/*uv*/
#define  VCE_VDD_1080P_ATM7029_L_21 975000/*uv*/
#define  VCE_VDD_1080P_DEFAULT  975000/*uv*/
static int vce_vdd_dvfslevel;
static int vce_vdd_limit_init = 0;
static int vce_vdd_limit_cnt = 0;
static int vce_vdd_1080p_x_type = VCE_VDD_1080P_DEFAULT;
static int vce_vdd_current = VCE_VDD_1080P_DEFAULT;
static int g_power_on_off_count = 0;
static int vce_power_is_enable = 0;
static int vce_resumed_flag = 0;
static unsigned int irq_vce = -1;


static int vce_vddtofreq_basevdd_table[7] = {1100000,1100000,1100000,1100000,1100000,1150000,1200000};
static int vce_vddtofreq_maxfreq_table_atm7021[7] = {348,384,426,468,522,558,600};
static int vce_vddtofreq_maxfreq_table_atm7029b[7] = {336,372,414,450,480,528,564};
static unsigned long iobase_vce;	/* VCE_BASE_ADDR */
static unsigned long iobase_cmu_devclken0;	/* CMU_DEVCLKEN0 */
static unsigned long vce_clk_en;
static unsigned long iobase_sps_pg_ctl;	/* SPS_PG_CTL */
static unsigned long vce_pg_en;
static struct regulator *vol_set;	/* voltage regulation */
static struct clk *vce_clk;	/* clk */
static struct reset_control *reset;	/* reset */
static struct device *vce_device;	/* for powergate and clk */

/*降温相关*/
static int vce_cooling_flag = 0;
static int vce_cooling_max_vdd = 0;
#endif

static wait_queue_head_t  vce_wait;
static int gPownOnOffCount = 0;
static unsigned int gVceStatus = 0;

static void *vce_malloc(int32_t size) 
{
	return kzalloc(size, GFP_KERNEL | GFP_DMA);
}

static void vce_free(void *ptr) 
{
	kfree(ptr);
	ptr = NULL;
}

static unsigned int Re_Reg(unsigned int reg)
{
	unsigned int value = readl((void *)(iobase_vce + reg));
	return value;
}

static void Wr_Reg(unsigned int reg,unsigned int value)
{
	writel(value, (void *)(iobase_vce + reg));
}

/*
  SPS_PG_CTL,b4 VCE&BISP&AISP power on enable
  SPS_PG_ACK,b4 VCE&BISP&AISP power on enable
  CMU_VCECLK 
  CMU_DEVCLKEN0 bit26 VCE interface clock enable
*/
#ifndef IC_TYPE_ATM7039
/*sram的控制权*/
#define Share_Mem_REG  0xB0200004
/*实现 Reset*/
#define CMU_DEVRST0   (0xB0150000 + 0xA8)    /*redefined*/
#define VCE_RESET          0x00100000
/*实现power gating*/
#define SPS_PG_CTL                     0xB01C0100       /*redefined*/
#define VCE_BISP_POWERON   0x00000010
#define SPS_PG_ACK                    0xB01C0104
/*实现clk切换到vce中*/
#define CMU_DEVCLKEN0   (0xB0150000 + 0xA0) /*redefined*/
/*CMU_VCECLK选择时钟源和设置分频数*/
#define  CMU_DEVPLL           (0xB0150000 + 0x04)  /*redefined*/
#define  CMU_DDRPLL          (0xB0150000 + 0x08)  /*redefined*/
#define  CMU_DISPLAYPLL  (0xB0150000 + 0x10)  /*redefined*/
#define  CMU_VCECLK          (0xB0150000 + 0x44)  /*redefined*/
#else
/*sram的控制权*/
#define Share_Mem_REG  0xB0240004
/*实现 Reset*/
/*#define CMU_DEVRST0   (0xB0160000 + 0xA8) */   /*redefined*/
#define VCE_RESET          0x00100000
/*实现power gating*/
/*#define SPS_PG_CTL                     0xB01B0100*/        /*redefined*/
#define VCE_BISP_POWERON   0x00000002
/*#define SPS_PG_ACK                    0xB01B0100*/        /*redefined*/
/*实现clk切换到vce中*/
/*#define CMU_DEVCLKEN0   (0xB0160000 + 0xA0) */ /*redefined*/
/*CMU_VCECLK选择时钟源和设置分频数*/
/*#define  CMU_DEVPLL           (0xB0160000 + 0x04)*/  /*redefined*/
/*#define  CMU_DDRPLL          (0xB0160000 + 0x08)*/  /*redefined*/
/*#define  CMU_DISPLAYPLL  (0xB0160000 + 0x10) */ /*redefined*/
/*#define  CMU_VCECLK          (0xB0160000 + 0x44)*/  /*redefined*/
#endif


static int query_status(void)
{
	int ret_value = 0;
	int vce_status = Re_Reg(VCE_STATUS);
	gVceStatus = vce_status;

	if((vce_status & 0x1)&&((vce_status & 0x100) == 0))
	{
		ret_value =  VCE_BUSY;//codec is runing
	}
	else if(((vce_status & 0x1) == 0)&&(vce_status & 0x100)&&(vce_status & 0x1000))
	{
		ret_value =  VCE_READY;
	}
	else if(((vce_status & 0x1) == 0)&&((vce_status & 0x100) == 0))
	{
		ret_value =  VCE_IDLE;
	}
	else if((vce_status & 0x100)&&(vce_status & 0x8000))
	{
		ret_value =  VCE_ERR_STM_FULL;
	}
	else if((vce_status & 0x100)&&(vce_status & 0x10000))
	{
		ret_value =  VCE_ERR_STM;
	}
	else if((vce_status & 0x100)&&(vce_status & 0x40000))
	{
		ret_value =  VCE_ERR_TIMEOUT;
	}

	return ret_value;
}

static void vce_reset(void) 
{   
	vce_info("vce_reset\n");
	reset_control_reset(reset);
}

static void vce_stop(void)
{
	if (vce_power_is_enable)
	{
		int i = 0x10000000;
		while((VCE_BUSY == query_status())){
			mdelay(1);
			if(i-- < 0) break;
			//printk(KERN_ERR"vce_drv:ASOC_POWERGATE_VCE_BISP is PownOn when VCE_BUSY\n");
		}
	}
}

static void vce_power_on(void)
{
	vce_info("vce_power_on in\n");
	vce_stop();

	if (vce_power_is_enable)
		vce_info("OWL_POWERGATE_VCE_BISP is PownOn\n");

	pm_runtime_get_sync(vce_device);
	g_power_on_off_count++;
	vce_info("vce_power_on out %d\n", g_power_on_off_count);
}

static void vce_power_off(void)
{    
	vce_info("vce_power_off in\n");
	if (vce_power_is_enable)
		vce_info("OWL_POWERGATE_VCE_BISP is PownOn\n");

	pm_runtime_put_sync(vce_device);
	g_power_on_off_count--;
	vce_info("vce_power_off out %d\n", g_power_on_off_count);
}

#if (!Enable_Fix_CLK_Drv)
static void vce_drv_cfg(void)
{
	unsigned int value;

	value = Re_Reg(CMU_DEVRST0);
	value = value & (~ VCE_RESET);
	Wr_Reg(CMU_DEVRST0, value);

	value = Re_Reg(CMU_DEVCLKEN0);
	value = value | 0x04000000;
	Wr_Reg(CMU_DEVCLKEN0,value);

	value = Re_Reg(SPS_PG_CTL);
	value = value | VCE_BISP_POWERON;
	Wr_Reg(SPS_PG_CTL, value);

loop:
	value = Re_Reg(SPS_PG_ACK);
	value = value & VCE_BISP_POWERON;
	if(value == 0) goto loop;

	value = Re_Reg(CMU_DEVRST0);
	value = value | VCE_RESET;
	Wr_Reg(CMU_DEVRST0, value);
}

static void vce_drv_cfg_close(void)
{
	unsigned int value;
	value = Re_Reg(SPS_PG_CTL);
	value = value & (~VCE_BISP_POWERON);
	Wr_Reg(SPS_PG_CTL, value);
}
#endif

static void share_mem_reg_enable(void)
{
#ifndef IC_TYPE_ATM7021
	unsigned int value;
	value = Re_Reg(Share_Mem_REG/*0xb0200004*/);
#ifdef IC_TYPE_ATM7039
	value = value | 0x2;
#else
	value = value | 0xef000;
#endif
	Wr_Reg(Share_Mem_REG/*0xb0200004*/, value);
#endif
	vce_clk_isEnable = 1;
}

static void share_mem_reg_disable(void)
{
#ifndef IC_TYPE_ATM7021
	unsigned int value;
	value = Re_Reg(Share_Mem_REG/*0xb0200004*/);
#ifdef IC_TYPE_ATM7039
	value = value & (~0x2);
#else
	value = value & (~0xef000);
#endif

	Wr_Reg(Share_Mem_REG/*0xb0200004*/, value );
#endif
	vce_clk_isEnable = 0;
}

static void vce_clk_enable(void)
{    
#if Enable_Fix_CLK_Drv 
	vce_power_on();
	if ((vce_open_count == 1) || (vce_resumed_flag == 1))
		vce_reset();
	if (vce_open_count == 1) {
		/* the first instance enable powergate */
		vce_power_is_enable = 1;
		vce_info("vce powergate enable!\n");
	}
	clk_prepare_enable(vce_clk);
#else
	vce_drv_cfg();
#endif
}

static void vce_clk_disable(void)
{    
#if Enable_Fix_CLK_Drv 
	clk_disable_unprepare(vce_clk);

	if (vce_open_count == 0) {
		/* free the last instance, disable powergate */
		vce_power_is_enable = 0;
		vce_info("vce powergate not enable!\n");
	}

	vce_power_off();
#else
	vce_drv_cfg_close(); /*10.18*/
#endif
}

#if Enable_Fix_CLK_Drv
//================== 调压相关  =================
/* 获取ic */
//static int vce_get_ic(void)
//{
//	int dvfslevel,ic;
//	dvfslevel = asoc_get_dvfslevel();
//	ic =  ASOC_GET_IC(dvfslevel);
//	//printk("vce_get_ic:%x\n",ic);
//	return ic;
//}
//
///* 获取ictype */
//static int vce_get_dvfslevel(void)
//{
//	int dvfslevel,ic,version,type;
//	int vce_dvfslevel;
//	dvfslevel = asoc_get_dvfslevel();
//	ic =  ASOC_GET_IC(dvfslevel);
//	version = ASOC_GET_VERSION(dvfslevel);
//	type = ASOC_GET_LEVEL(dvfslevel);
//	vce_dvfslevel = ASOC_DVFSLEVEL(ic, version, type);
//	//printk("vce_get_dvfslevel:%x,%x,%x\n",ic,version,type);
//	return vce_dvfslevel;
//}

/* 调压：初始化 */
//static void vce_set_vdd_voltage_init(void)
//{
//	vce_vdd_dvfslevel = vce_get_dvfslevel();
//	if(vce_vdd_dvfslevel == ATM7021_L_1)
//	{
//		vce_vdd_1080p_x_type = VCE_VDD_1080P_ATM7021_L_1;
//	}
//	else if (vce_vdd_dvfslevel == ATM7021_L_3)
//	{
//		vce_vdd_1080p_x_type = VCE_VDD_1080P_ATM7021_L_3;
//	}
//	else if (vce_vdd_dvfslevel == ATM7029_L_21)
//	{
//		vce_vdd_1080p_x_type = VCE_VDD_1080P_ATM7029_L_21;
//	}
//	else
//	{
//		vce_vdd_1080p_x_type = VCE_VDD_1080P_DEFAULT;
//	}
//	
//	vce_vdd_limit_init = 0;
//	vce_vdd_limit_cnt = 0;
//	vce_vdd_current = VCE_VDD_1080P_DEFAULT;
//	
//	//printk("vce_drv vce_set_vdd_voltage_init!vce_vdd_dvfslevel:%x,%x,%x,%x\n",vce_vdd_dvfslevel,ATM7021_L_1,ATM7021_L_3,ATM7029_L_21);
//}

/* 调压：反初始化 */
//static int vce_set_vdd_voltage_deinit(void)
//{
//	int ret_vdd = 0;
//	//printk("vce_drv vce_set_vdd_voltage_deinit!vce_vdd_limit_init,:%d,vce_vdd_limit_cnt:%d\n",vce_vdd_limit_init,vce_vdd_limit_cnt);
//	
//	if( (vce_vdd_dvfslevel == ATM7021_L_1) || (vce_vdd_dvfslevel == ATM7021_L_3) || (vce_vdd_dvfslevel == ATM7029_L_21) )
//	{
//		if( (vce_vdd_limit_init == 1) && (vce_vdd_limit_cnt > 0) )
//		{
//			if(vce_vdd_limit_cnt != 1) printk("vce_drv warning!vce_vdd_limit_cnt(%d) is not 1!%d\n",vce_vdd_limit_cnt,__LINE__);
//
//			ret_vdd = vdd_voltage_limit_control(CLR_VDD_LIMIT,vce_vdd_current);
//			if(ret_vdd < 0) printk("vce_drv warning!vdd_voltage_limit_control fail!%d,%d,%d\n",vce_vdd_current,ret_vdd,__LINE__);
//		}
//	}
//	
//	vce_vdd_limit_init = 0;
//	vce_vdd_limit_cnt = 0;
//	vce_vdd_current = VCE_VDD_1080P_DEFAULT;
//	
//	return ret_vdd;
//}

/* 调压：设置 */
static int vce_set_vdd_voltage_run(void)
{
	int vce_vdd_target,ret_vdd = 0;
	if(1)
	{
		if(vce_freq_multi.width * vce_freq_multi.height > 1920*1080)
		{
			/*与降温相关*/
			if(vce_cooling_flag == 0)
				vce_vdd_target = vce_vdd_1080p_x_type;
			else
				vce_vdd_target = MIN(vce_vdd_1080p_x_type,vce_cooling_max_vdd);
			
			if(vce_vdd_limit_init == 0)
			{
				vce_vdd_limit_init = 1;
				vce_vdd_current = vce_vdd_target;
				vce_vdd_limit_cnt++;
				ret_vdd = regulator_set_voltage(vol_set, vce_vdd_current, VOL_MAX);
				//ret_vdd = vdd_voltage_limit_control(SET_VDD_LIMIT,vce_vdd_current);
				if(ret_vdd < 0) printk("vce_drv warning!vdd_voltage_limit_control set fail!%d,%d,%d\n",vce_vdd_current,ret_vdd,__LINE__);
			}
			else if(vce_vdd_current != vce_vdd_target)
			{
				if(vce_vdd_limit_cnt != 1) printk("vce_drv warning!vce_vdd_limit_cnt(%d) is not 1!%d\n",vce_vdd_limit_cnt,__LINE__);

				vce_vdd_limit_cnt--;
				ret_vdd = regulator_set_voltage(vol_set, vce_vdd_current, VOL_MAX);
				//ret_vdd = vdd_voltage_limit_control(CLR_VDD_LIMIT,vce_vdd_current);
				if(ret_vdd < 0) printk("vce_drv warning!vdd_voltage_limit_control clr fail!%d,%d,%d\n",vce_vdd_current,ret_vdd,__LINE__);
				
				vce_vdd_current = vce_vdd_target;
				vce_vdd_limit_cnt++;
				ret_vdd = regulator_set_voltage(vol_set, vce_vdd_current, VOL_MAX);
				//ret_vdd = vdd_voltage_limit_control(SET_VDD_LIMIT,vce_vdd_current);
				if(ret_vdd < 0) printk("vce_drv warning!vdd_voltage_limit_control set fail!%d,%d,%d\n",vce_vdd_current,ret_vdd,__LINE__);
			}
			//printk("vce_drv vce_set_vdd_voltage_run!vdd_voltage:%d\n",vce_vdd_current);
		}
		else
		{
			//printk("vce_drv vce_set_vdd_voltage_run!vce_vdd_limit_init:%d,vce_vdd_limit_cnt:%d\n",vce_vdd_limit_init,vce_vdd_limit_cnt);

			if((vce_vdd_limit_init == 1) && (vce_vdd_limit_cnt > 0))
			{
				if(vce_vdd_limit_cnt != 1) printk("vce_drv warning!vce_vdd_limit_cnt(%d) is not 1!%d\n",vce_vdd_limit_cnt,__LINE__);
				
				ret_vdd = regulator_set_voltage(vol_set, vce_vdd_current, VOL_MAX);
				//ret_vdd = vdd_voltage_limit_control(CLR_VDD_LIMIT,vce_vdd_current);
				if(ret_vdd < 0) printk("vce_drv warning!vdd_voltage_limit_control clr fail!%d,%d,%d\n",vce_vdd_current,ret_vdd,__LINE__);
				
				vce_vdd_limit_init = 0;
				vce_vdd_limit_cnt = 0;
				vce_vdd_current = VCE_VDD_1080P_DEFAULT;
			}
		}
	}
	
	return ret_vdd;
}

//================== 调频相关  =================
/* 设置vce频率，返回实际设置成功的频率点，返回-1表示设置失败；*/
static unsigned long  vce_setFreq(vce_info_t *info,vce_multi_freq_t* freq_MHz) 
{
	//static struct clk *vce_clk;
	unsigned long rate, new_rate = freq_MHz->freq;
	vce_info_t* multi_info;
	int ret,i,maxfreq;

	//printk("vce_drv vce_setFreq!width:%d,height:%d,freq:%d,vce_open_count:%d\n", 
	//freq_MHz->width,freq_MHz->height,(int)freq_MHz->freq,vce_open_count);  

	if(new_rate > 600)
	{
		printk("vce_drv err!cannot set vce freq to : %d MHz\n",(int)new_rate);             
		return -1;
	}

	if(vce_clk_isEnable == 0)
	{
		printk("vce_drv warning!vce clk is not enable yet\n");   
	}

//	vce_clk = clk_get_sys((const char *)CLK_NAME_VCE_CLK, NULL); /*根据clk_name获取clk结构体*/
//	if (IS_ERR(vce_clk))
//	{
//		printk("vce_drv : clk_get_sys(CLK_NAME_VCE_CLK, NULL) failed\n");
//		return -1;
//	}

	/*Multi-Instances*/
	info->vce_freq.width = freq_MHz->width;
	info->vce_freq.height = freq_MHz->height;
	info->vce_freq.freq = new_rate;
	if( (vce_open_count >1) && (vce_freq_is_init == 1)) 
	{
		/*找最大分辨率*/
		multi_info = (vce_info_t*)pAbuf[0];
		vce_freq_multi.width = multi_info->vce_freq.width;
		vce_freq_multi.height = multi_info->vce_freq.height;
		vce_freq_multi.freq = multi_info->vce_freq.freq;
		for(i = 1; i < vce_open_count;i++)
		{
			multi_info = (vce_info_t*)pAbuf[i];
			if(multi_info->vce_freq.width*multi_info->vce_freq.height > vce_freq_multi.width*vce_freq_multi.height )
			{
				vce_freq_multi.width = multi_info->vce_freq.width;
				vce_freq_multi.height = multi_info->vce_freq.height;
				vce_freq_multi.freq = multi_info->vce_freq.freq;
			}
		}
		/*应该设置的频率*/
		new_rate = vce_freq_multi.freq;
	}
	else
	{
		vce_freq_multi.width = freq_MHz->width;
		vce_freq_multi.height = freq_MHz->height;
		vce_freq_multi.freq = new_rate;
		vce_freq_is_init = 1;
	}
	
	/*stop*/
	vce_stop();
	
	/*调压*/
	vce_set_vdd_voltage_run();
	
	/*最大电压对应最大频率*/
	//printk("vce_drv vce_setFreq!init:%d,flag:%d\n",vce_vdd_limit_init,vce_cooling_flag); 
	if(vce_vdd_limit_init && vce_cooling_flag)
	{
		for(i = 0; i < 7; i++)
		{
			if(vce_vdd_current < vce_vddtofreq_basevdd_table[i])break;
		}
		if(i > 0) i = i - 1;
		
		if(1)
		{
			maxfreq = vce_vddtofreq_maxfreq_table_atm7021[i];
		}
		else /*atm7029b*/
		{
			maxfreq = vce_vddtofreq_maxfreq_table_atm7029b[i];
		}
		
		//printk("vce_drv vce_setFreq!i:%d,ic:%x,maxfreq:%d,new_rate:%ld\n",i,vce_get_ic(),maxfreq,new_rate); 
		
		if(new_rate > maxfreq)new_rate = maxfreq;
	}
	
	/*调频*/
	rate = clk_round_rate(vce_clk, new_rate * 1000 * 1000);
	if (rate > 0) {
		ret = clk_set_rate(vce_clk, rate);	/* set clk hz */
		if (ret != 0) {
			vce_err("clk_set_rate failed\n");
			return -1;
		}
	}

	rate = clk_get_rate(vce_clk);	/*get clk current frequency */
	printk(KERN_NOTICE "vce_drv: new rate (%ld MHz) is set\n", rate / (1000 * 1000));    

	return (rate/(1000*1000));
}

/* 返回vce频率，返回0表示失败*/
static unsigned long vce_getFreq(void) 
{
	//static struct clk *vce_clk;
	unsigned long rate;

	if(vce_clk_isEnable == 0) {
		printk("vce_drv Warning: vce clk is not enable yet\n");
	}
   
	rate = clk_get_rate(vce_clk);
	vce_info("cur vce freq : %ld MHz\n", rate / (1000 * 1000));

	return rate / (1000 * 1000);
}

//================== 降温相关  =================
/*根据当前温度，限制最大电压和频率*/
/****
static int vce_thermal_notifier(struct notifier_block *nb,
					unsigned long event, void *data)
{
	vce_info_t* info;
	int ret_value = 0;
	struct freq_clip_table *notify_table = (struct freq_clip_table *)data;
	
	//根据vdd上限值,进行降压和降频
	if(event == CPUFREQ_COOLING_START)
	{
		printk("vce_drv : CPUFREQ_COOLING_START,vdd_clip_max:%d\n", notify_table->vdd_clip_max);
		vce_cooling_flag = 1;
		vce_cooling_max_vdd = notify_table->vdd_clip_max;
	}
		
	//恢复电压和频率
	if(event == CPUFREQ_COOLING_STOP)
	{
		printk("vce_drv : CPUFREQ_COOLING_STOP\n");
		vce_cooling_flag = 0;
		vce_cooling_max_vdd = 0;
	}
	
	//调节
	mutex_lock(&vce_ioctl_mutex);
	printk("vce_drv : vce_thermal_notifier!(%d)\n",vce_open_count);
	if(vce_open_count > 0)
	{
		info = (vce_info_t*)pAbuf[0];
		ret_value = vce_setFreq(info,&info->vce_freq);
	}
	mutex_unlock(&vce_ioctl_mutex);
	
	return 0;
}
***/
#endif

/* enable int */
static inline void enable_vce_irq(void)
{
}
    
static inline void disable_vce_irq(void)
{
	int vce_status;
	vce_status = Re_Reg(VCE_STATUS);
	//printk(KERN_ERR"b4 disable_vce_irq ... vce_status:%x\n",vce_status);
	vce_status = vce_status & (~0x100);	
	Wr_Reg(VCE_STATUS,vce_status);
	gVceStatus = vce_status;
}

#ifdef VCE_INTTERUPT_MODE
irqreturn_t vce_ISR(int irq, void *dev_id)
{
    disable_vce_irq();
    complete(&vce_complete);
	wake_up_interruptible(&vce_wait);

    return IRQ_HANDLED;
}
#endif

#ifdef IC_TYPE_ATM7021
typedef struct{
	unsigned int vce_status;
	unsigned int vce_outstanding;
	unsigned int vce_cfg;
	unsigned int vce_param0;
	unsigned int vce_param1;
	unsigned int vce_strm;
	unsigned int vce_strm_addr;
	unsigned int vce_yaddr;
	unsigned int vce_list0;
	unsigned int vce_list1;
	unsigned int vce_me_param;
	unsigned int vce_swindow;
	unsigned int vce_scale_out;
	unsigned int vce_rect;
	unsigned int vce_rc_param1;
	unsigned int vce_rc_param2;
	unsigned int vce_rc_hdbits;
	unsigned int vce_ts_info;
	unsigned int vce_ts_header;
	unsigned int vce_ts_blu;
	unsigned int vce_ref_dhit;
	unsigned int vce_ref_dmiss;

	unsigned int ups_yas;
	unsigned int ups_cacras;
	unsigned int ups_cras;
	unsigned int ups_ifomat;
	unsigned int ups_ratio;
	unsigned int ups_ifs;
}vce_input_t_7021;

static int set_registers_7021(vce_input_t_7021  *vce_in)
{
	Wr_Reg(UPS_YAS,vce_in->ups_yas);//FIX PHY ADDR
	Wr_Reg(UPS_CBCRAS,vce_in->ups_cacras);//FIX PHY ADDR
	Wr_Reg(UPS_CRAS,vce_in->ups_cras);//FIX PHY ADDR
	Wr_Reg(UPS_IFORMAT,vce_in->ups_ifomat);
	Wr_Reg(UPS_RATIO,vce_in->ups_ratio);
	Wr_Reg(UPS_IFS,vce_in->ups_ifs); 

	Wr_Reg(VCE_CFG,vce_in->vce_cfg);
	Wr_Reg(VCE_PARAM0,vce_in->vce_param0);
	Wr_Reg(VCE_OUTSTANDING,vce_in->vce_outstanding);
	Wr_Reg(VCE_PARAM1,vce_in->vce_param1);
	Wr_Reg(VCE_STRM,vce_in->vce_strm);
	Wr_Reg(VCE_STRM_ADDR,vce_in->vce_strm_addr);
	Wr_Reg(VCE_YADDR,vce_in->vce_yaddr);
	Wr_Reg(VCE_LIST0_ADDR,vce_in->vce_list0);
	Wr_Reg(VCE_LIST1_ADDR,vce_in->vce_list1);
	Wr_Reg(VCE_ME_PARAM,vce_in->vce_me_param);
	Wr_Reg(VCE_SWIN,vce_in->vce_swindow);
	Wr_Reg(VCE_SCALE_OUT,vce_in->vce_scale_out);
	Wr_Reg(VCE_RECT,vce_in->vce_rect);
	Wr_Reg(VCE_RC_PARAM1,vce_in->vce_rc_param1);
	Wr_Reg(VCE_RC_PARAM2,vce_in->vce_rc_param2);
	Wr_Reg(VCE_TS_INFO,vce_in->vce_ts_info);
	Wr_Reg(VCE_TS_HEADER,vce_in->vce_ts_header);
	Wr_Reg(VCE_TS_BLUHD,vce_in->vce_ts_blu);
	Wr_Reg(VCE_RC_HDBITS,vce_in->vce_rc_hdbits);
	Wr_Reg(VCE_REF_DHIT,vce_in->vce_ref_dhit);
	Wr_Reg(VCE_REF_DMISS,vce_in->vce_ref_dmiss);

	return 0;
}

static unsigned int get_ups_ifomat_7021(vce_input_t  *vce_in)
{
	unsigned int ups_ifomat = 0;
	unsigned int stride = (vce_in->ups_str &0x3ff)  *8;
	unsigned int input_fomat = vce_in->input_fomat;
	ups_ifomat = input_fomat |  ( stride  << 16) ;

	return ups_ifomat;
}

static unsigned int get_ups_ratio_7021(vce_input_t  *vce_in)
{
	unsigned int ups_ratio = 0;
	unsigned int ups_ifs= vce_in->ups_ifs;
	unsigned int ups_ofs = vce_in->ups_ofs;
	int srcw = (ups_ifs & 0xffff)*8;
	int srch =  ((ups_ifs>>16) & 0xffff)*8;
	int dstw = (ups_ofs & 0xffff)*16;
	int dsth = ((ups_ofs>>16) & 0xffff)*16;

	unsigned int upscale_factor_h_int,upscale_factor_v_int;
	if(dstw<=srcw)
		upscale_factor_h_int =  (srcw*8192) / dstw;
	else
		upscale_factor_h_int = (srcw/2 - 1)*8192  / (dstw/2 - 1) ;

	if(dsth <= srch)
		upscale_factor_v_int =  (srch*8192) / dsth;
	else
		upscale_factor_v_int = (srch/2 - 1)*8192  / (dsth/2 - 1) ;
	ups_ratio = (upscale_factor_v_int & 0xffff) <<16 | (upscale_factor_h_int & 0xffff);

	return ups_ratio;
}

static void registers_7039_to_7021(vce_input_t  *vce_in,vce_input_t_7021  *vce_in_7021)
{
	vce_in_7021->ups_ifomat = get_ups_ifomat_7021(vce_in);
	vce_in_7021->ups_ratio = get_ups_ratio_7021(vce_in);
	vce_in_7021->ups_ifs = vce_in->ups_ifs<<3;  //*
	vce_in_7021->ups_yas = vce_in->ups_yas;
	vce_in_7021->ups_cacras = vce_in->ups_cacras;
	vce_in_7021->ups_cras = vce_in->ups_cras;
	vce_in_7021->vce_outstanding = (1<<31) |  (12<<16) | (1<<15) | 128;

	vce_in_7021->vce_status = vce_in->vce_status;
	vce_in_7021->vce_cfg = vce_in->vce_cfg;
	vce_in_7021->vce_param0 = vce_in->vce_param0;
	vce_in_7021->vce_param1 = vce_in->vce_param1;
	vce_in_7021->vce_strm = vce_in->vce_strm;
	vce_in_7021->vce_strm_addr = vce_in->vce_strm_addr;
	vce_in_7021->vce_yaddr = vce_in->vce_yaddr;
	vce_in_7021->vce_list0 = vce_in->vce_list0;
	vce_in_7021->vce_list1 = vce_in->vce_list1;
	vce_in_7021->vce_me_param = vce_in->vce_me_param;
	vce_in_7021->vce_swindow = vce_in->vce_swindow;
	vce_in_7021->vce_scale_out = vce_in->vce_scale_out;
	vce_in_7021->vce_rect = vce_in->vce_rect;
	vce_in_7021->vce_rc_param1 = vce_in->vce_rc_param1;
	vce_in_7021->vce_rc_param2 = vce_in->vce_rc_param2;
	vce_in_7021->vce_ts_info = vce_in->vce_ts_info;
	vce_in_7021->vce_ts_header = vce_in->vce_ts_header;
	vce_in_7021->vce_ts_blu = vce_in->vce_ts_blu;
	vce_in_7021->vce_rc_hdbits = vce_in->vce_rc_hdbits;
	vce_in_7021->vce_ref_dhit = vce_in->vce_ref_dhit;
	vce_in_7021->vce_ref_dmiss = vce_in->vce_ref_dmiss;
}

#if Enable_Debug_PrintK
static void print_vce_input_t_7021(vce_input_t_7021* vce_input)
{
	printk(KERN_ERR"ko.vce_input_t1!vce_status:%x,vce_cfg:%x,vce_param0:%x,vce_param1:%x\n", \
		vce_input->vce_status,vce_input->vce_cfg,vce_input->vce_param0,vce_input->vce_param1);

	printk(KERN_ERR"ko.vce_input_t2!vce_strm:%x,vce_strm_addr:%x,vce_yaddr:%x,vce_list0:%x\n", \
		vce_input->vce_strm,vce_input->vce_strm_addr,vce_input->vce_yaddr,vce_input->vce_list0);

	printk(KERN_ERR"ko.vce_input_t3!vce_list1:%x,vce_me_param:%x,vce_swindow:%x,vce_scale_out:%x\n", \
		vce_input->vce_list1,vce_input->vce_me_param,vce_input->vce_swindow,vce_input->vce_scale_out);

	printk(KERN_ERR"ko.vce_input_t4!vce_rect:%x,vce_rc_param1:%x,vce_rc_param2:%x,vce_rc_hdbits:%x\n", \
		vce_input->vce_rect,vce_input->vce_rc_param1,vce_input->vce_rc_param2,vce_input->vce_rc_hdbits);

	printk(KERN_ERR"ko.vce_input_t5!vce_ts_info:%x,vce_ts_header:%x,vce_ts_blu:%x\n", \
		vce_input->vce_ts_info,vce_input->vce_ts_header,vce_input->vce_ts_blu);

	printk(KERN_ERR"ko.vce_input_t6!ups_ifomat:%x,ups_ratio:%x,ups_ifs:%x\n", \
		vce_input->ups_ifomat,vce_input->ups_ratio,vce_input->ups_ifs);

	printk(KERN_ERR"ko.vce_input_t7!ups_yas:%x,ups_cacras:%x,ups_cras:%x,\n", \
		vce_input->ups_yas,vce_input->ups_cacras,vce_input->ups_cras);

	printk(KERN_ERR"ko.vce_input_t8!vce_ref_dhit:%x,vce_ref_dmiss:%x\n", \
		vce_input->vce_ref_dhit,vce_input->vce_ref_dmiss); 
}
#endif

static int set_registers_atm7021(vce_input_t  *vce_in)
{
	vce_input_t_7021  vce_in_7021;
	registers_7039_to_7021(vce_in,&vce_in_7021);
	//print_vce_input_t_7021(&vce_in_7021);
	set_registers_7021(&vce_in_7021);
	return 0;
}
#endif

#ifndef IC_TYPE_ATM7021
static int set_registers(vce_input_t  *vce_in)
{
	Wr_Reg(UPS_IFS,vce_in->ups_ifs);
	Wr_Reg(UPS_STR,vce_in->ups_str);
	Wr_Reg(UPS_OFS,vce_in->ups_ofs);
	Wr_Reg(UPS_RATH,vce_in->ups_rath);
	Wr_Reg(UPS_RATV,vce_in->ups_ratv); 
	Wr_Reg(UPS_YAS,vce_in->ups_yas);//FIX PHY ADDR
	Wr_Reg(UPS_CBCRAS,vce_in->ups_cacras);//FIX PHY ADDR
	Wr_Reg(UPS_CRAS,vce_in->ups_cras);//FIX PHY ADDR
	Wr_Reg(UPS_DWH,vce_in->ups_dwh);
	Wr_Reg(UPS_BCT,vce_in->ups_bct);
	Wr_Reg(UPS_SAB0,vce_in->ups_sab0);
	Wr_Reg(UPS_SAB1,vce_in->ups_sab1);
	Wr_Reg(UPS_DAB,vce_in->ups_dab);
	Wr_Reg(UPS_CTL,vce_in->ups_ctl);//enable blding
#ifdef IC_TYPE_ATM7039
	Wr_Reg(UPS_RGB32_SR,vce_in->ups_rgb32_sr);
	Wr_Reg(UPS_BLEND_W,vce_in->ups_blend_w); 
#endif

	Wr_Reg(VCE_CFG,vce_in->vce_cfg);
	Wr_Reg(VCE_PARAM0,vce_in->vce_param0);
	Wr_Reg(VCE_PARAM1,vce_in->vce_param1);
	Wr_Reg(VCE_STRM,vce_in->vce_strm);
	Wr_Reg(VCE_STRM_ADDR,vce_in->vce_strm_addr);
	Wr_Reg(VCE_YADDR,vce_in->vce_yaddr);
	Wr_Reg(VCE_LIST0_ADDR,vce_in->vce_list0);
	Wr_Reg(VCE_LIST1_ADDR,vce_in->vce_list1);
	Wr_Reg(VCE_ME_PARAM,vce_in->vce_me_param);
	Wr_Reg(VCE_SWIN,vce_in->vce_swindow);
	Wr_Reg(VCE_SCALE_OUT,vce_in->vce_scale_out);
	Wr_Reg(VCE_RECT,vce_in->vce_rect);
	Wr_Reg(VCE_RC_PARAM1,vce_in->vce_rc_param1);
	Wr_Reg(VCE_RC_PARAM2,vce_in->vce_rc_param2);
	Wr_Reg(VCE_TS_INFO,vce_in->vce_ts_info);
	Wr_Reg(VCE_TS_HEADER,vce_in->vce_ts_header);
	Wr_Reg(VCE_TS_BLUHD,vce_in->vce_ts_blu);
	Wr_Reg(VCE_RC_HDBITS,vce_in->vce_rc_hdbits);
#ifdef IC_TYPE_ATM7039
	Wr_Reg(VCE_REF_DHIT,vce_in->vce_ref_dhit);
	Wr_Reg(VCE_REF_DMISS,vce_in->vce_ref_dmiss);
#endif

	return 0;
}
#endif

static int get_registers(vce_output_t *vce_out)
{
	vce_out->vce_strm = Re_Reg(VCE_STRM);
	vce_out->vce_rc_param3 = Re_Reg(VCE_RC_PARAM3);
	vce_out->vce_rc_hdbits = Re_Reg(VCE_RC_HDBITS);  

#if Enable_Fix_Drv
	vce_out->strm_addr = Re_Reg(VCE_STRM_ADDR);
	vce_out->i_ts_offset = Re_Reg(VCE_TS_INFO);
	vce_out->i_ts_header = Re_Reg(VCE_TS_HEADER);  
#endif

#ifdef IC_TYPE_ATM7039
	vce_out->vce_ref_dhit = Re_Reg(VCE_REF_DHIT);
	vce_out->vce_ref_dmiss =Re_Reg(VCE_REF_DMISS);
#endif
	return 0;
}

static void print_all_regs(char*s)
{
	printk(s);

#ifndef IC_TYPE_ATM7021
	printk(KERN_ERR"ShareSRam_CTL:%x\n",Re_Reg(0xb0200004));
	printk(KERN_ERR"VCE_ID:%x,VCE_STATUS:%x,VCE_CFG:%x\n",
		Re_Reg(VCE_ID),Re_Reg(VCE_STATUS),Re_Reg(VCE_CFG));
	printk(KERN_ERR"VCE_PARAM0:%x,VCE_PARAM1:%x,VCE_STRM:%x\n",
		Re_Reg(VCE_PARAM0),Re_Reg(VCE_PARAM1),Re_Reg(VCE_STRM));
	printk(KERN_ERR"VCE_STRM_ADDR:%x,VCE_YADDR:%x,VCE_LIST0_ADDR:%x\n",
		Re_Reg(VCE_STRM_ADDR),Re_Reg(VCE_YADDR),Re_Reg(VCE_LIST0_ADDR));
	printk(KERN_ERR"VCE_LIST1_ADDR:%x,VCE_ME_PARAM:%x,VCE_SWIN:%x\n",
		Re_Reg(VCE_LIST1_ADDR),Re_Reg(VCE_ME_PARAM),Re_Reg(VCE_SWIN));
	printk(KERN_ERR"VCE_SCALE_OUT:%x,VCE_RECT:%x,VCE_RC_PARAM1:%x\n",
		Re_Reg(VCE_SCALE_OUT),Re_Reg(VCE_RECT),Re_Reg(VCE_RC_PARAM1));
	printk(KERN_ERR"VCE_RC_PARAM2:%x,VCE_RC_PARAM3:%x,VCE_RC_HDBITS:%x\n",
		Re_Reg(VCE_RC_PARAM2),Re_Reg(VCE_RC_PARAM3),Re_Reg(VCE_RC_HDBITS));
	printk(KERN_ERR"VCE_TS_INFO:%x,VCE_TS_HEADER:%x,VCE_TS_BLUHD:%x\n",
		Re_Reg(VCE_TS_INFO),Re_Reg(VCE_TS_HEADER),Re_Reg(VCE_TS_BLUHD));
	printk(KERN_ERR"UPS_CTL:%x,UPS_IFS:%x,UPS_STR:%x\n",
		Re_Reg(UPS_CTL),Re_Reg(UPS_IFS),Re_Reg(UPS_STR));
	printk(KERN_ERR"UPS_OFS:%x,UPS_RATH:%x,UPS_RATV:%x\n",
		Re_Reg(UPS_OFS),Re_Reg(UPS_RATH),Re_Reg(UPS_RATV));
	printk(KERN_ERR"UPS_YAS:%x,UPS_CBCRAS:%x,UPS_CRAS:%x\n",
		Re_Reg(UPS_YAS),Re_Reg(UPS_CBCRAS),Re_Reg(UPS_CRAS));
	printk(KERN_ERR"UPS_BCT:%x,UPS_DAB:%x,UPS_DWH:%x\n",
		Re_Reg(UPS_BCT),Re_Reg(UPS_DAB),Re_Reg(UPS_DWH));
	printk(KERN_ERR"UPS_SAB0:%x,UPS_SAB1:%x\n",
		Re_Reg(UPS_SAB0),Re_Reg(UPS_SAB1));
	printk(s);
#endif
}

#if Enable_Debug_PrintK
static void print_vce_input_t(vce_input_t* vce_input)
{
	printk(KERN_ERR"ko.vce_input_t1!vce_status:%x,vce_cfg:%x,vce_param0:%x,vce_param1:%x\n",
		vce_input->vce_status,vce_input->vce_cfg,vce_input->vce_param0,vce_input->vce_param1);
	printk(KERN_ERR"ko.vce_input_t2!vce_strm:%x,vce_strm_addr:%x,vce_yaddr:%x,vce_list0:%x\n",
		vce_input->vce_strm,vce_input->vce_strm_addr,vce_input->vce_yaddr,vce_input->vce_list0);
	printk(KERN_ERR"ko.vce_input_t3!vce_list1:%x,vce_me_param:%x,vce_swindow:%x,vce_scale_out:%x\n",
		vce_input->vce_list1,vce_input->vce_me_param,vce_input->vce_swindow,vce_input->vce_scale_out);
	printk(KERN_ERR"ko.vce_input_t4!vce_rect:%x,vce_rc_param1:%x,vce_rc_param2:%x,vce_rc_hdbits:%x\n",
		vce_input->vce_rect,vce_input->vce_rc_param1,vce_input->vce_rc_param2,vce_input->vce_rc_hdbits);
	printk(KERN_ERR"ko.vce_input_t5!vce_ts_info:%x,vce_ts_header:%x,vce_ts_blu:%x\n",
		vce_input->vce_ts_info,vce_input->vce_ts_header,vce_input->vce_ts_blu);
	printk(KERN_ERR"ko.vce_input_t6!ups_ctl:%x,ups_ifs:%x,ups_str:%x,ups_ofs:%x\n",
		vce_input->ups_ctl,vce_input->ups_ifs,vce_input->ups_str,vce_input->ups_ofs);
	printk(KERN_ERR"ko.vce_input_t7!ups_rath:%x,ups_ratv:%x,ups_yas:%x,ups_cacras:%x\n",
		vce_input->ups_rath,vce_input->ups_ratv,vce_input->ups_yas,vce_input->ups_cacras);
	printk(KERN_ERR"ko.vce_input_t8!ups_cras:%x,ups_bct:%x,ups_dab:%x,ups_dwh:%x\n",
		vce_input->ups_cras,vce_input->ups_bct,vce_input->ups_dab,vce_input->ups_dwh);
	printk(KERN_ERR"ko.vce_input_t9!ups_sab0:%x,ups_sab1:%x\n",
		vce_input->ups_sab0,vce_input->ups_sab1);
#ifdef IC_TYPE_ATM7039
	printk(KERN_ERR"ko.vce_input_t10!vce_ref_dhit:%x,vce_ref_dmiss:%x,ups_rgb32_sr:%x,ups_blend_w:%x\n",
		vce_input->vce_ref_dhit,vce_input->vce_ref_dmiss,vce_input->ups_rgb32_sr,vce_input->ups_blend_w); 
#endif
}

static void print_vce_output_t(vce_output_t* vce_output)
{
	printk(KERN_ERR"ko.vce_output_t1!vce_strm:%x,vce_rc_param3:%x,vce_rc_hdbits:%x\n",
		vce_output->vce_strm,vce_output->vce_rc_param3,vce_output->vce_rc_hdbits);
	printk(KERN_ERR"ko.vce_output_t2!strm_addr:%x,i_ts_offset:%x,i_ts_header:%x\n",
		vce_output->strm_addr,vce_output->i_ts_offset,vce_output->i_ts_header);
}

static void print_ShareSRam_CTL(char* str)
{
	printk(KERN_ERR"%s!ShareSRam_CTL:%x\n",str,Re_Reg(0xb0200004));
}

void print_CMU_Reg(char*s)
{
	printk(s);
	printk("Share_Mem_REG:%x  CMU_DEVRST0:%x  SPS_PG_CTL:%x\n",
		Re_Reg(Share_Mem_REG),Re_Reg(CMU_DEVRST0),Re_Reg(SPS_PG_CTL));
	printk("DEV:%x,DDR:%x,DISPLAY:%x\n",
		Re_Reg(CMU_DEVPLL),Re_Reg(CMU_DDRPLL),Re_Reg(CMU_DISPLAYPLL));
	printk("VCECLK:%x,DEVCLKEN0:%x\n",
		Re_Reg(CMU_VCECLK),Re_Reg(CMU_DEVCLKEN0));
}
#endif

static void pAbuf_release(int vce_count)
{
	int i;
	vce_info_t *info = NULL;
	if(vce_count >= 1 && vce_count <= vce_open_count)
	{
		/*delete该节点*/
		for(i = vce_count; i < vce_open_count;i++)
		{
			pAbuf[i-1] = pAbuf[i];              /*用后面往前挪动*/
			info = (vce_info_t*)pAbuf[i-1];
			info->vce_count--;                   /*注意-1*/
		}
		pAbuf[vce_open_count - 1] = NULL;

		if(vce_last_handle == vce_count)
			vce_last_handle = 0;
		else if(vce_last_handle > vce_count )
			vce_last_handle--;
	}
	else
	{
		printk(KERN_ERR"vce drv:Warning!vce_count(%d) is out of range(%d)!\n",vce_count,vce_open_count);
	}
};

long vce_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	vce_info_t *info = (vce_info_t*) filp->private_data;
	int ret_value = 0;
	long time_rest = 0;
	int cur_status = 0;
	void __user *from;
	void __user *to;
	vce_multi_freq_t  vce_freq;

#ifndef IC_TYPE_ATM7039
	if(gPownOnOffCount<= 0)
	{
		printk(KERN_ERR"vce_dev Warning! power off at vce_ioctl now!\n");
	}
#endif

	switch (cmd)
	{
	case VCE_CHECK_VERSION:
	{
		mutex_lock(&vce_ioctl_mutex);
		if(Re_Reg(VCE_ID) != 0x78323634){
			printk("kernal ERR....");
		}
		mutex_unlock(&vce_ioctl_mutex);
	}
	break;
	
	case VCE_CMD_ENC_RUN:
		{
			mutex_lock(&vce_ioctl_mutex);
			from = (void __user *)arg;			

			//*若超时*/
			cur_status = query_status();	
			//printk(KERN_ERR"vce_ioctl ...VCE_CMD_ENC_RUN=%d.!,%x,%x,%x\n",(info->vce_status == STOPED),cur_status,info,gVceStatus);
			time_rest = wait_event_interruptible_timeout(vce_wait,(!((gVceStatus & 0x1)==1)&&((gVceStatus & 0x100) == 0)), WAIT_TIMEOUT);
			cur_status = query_status();

			if(time_rest <= 0 && (cur_status == VCE_BUSY))
			{
				print_all_regs("vce_dev timeout when runing!reginfo:");
				vce_reset();
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_BUSY;
			}
			else
			{  			
				//already done,clear all & save irqs
				//*把上一个驱动保存结果
				if(vce_last_handle >= 1 && vce_last_handle < _MAX_HANDLES_)  //*new add
				{
					vce_info_t *pinfo = (vce_info_t *) pAbuf[vce_last_handle - 1];
					if(pinfo != NULL)
					{
						get_registers(&pinfo->vce_out);
						pinfo->vce_status = STOPED;
					}
				}
			}
			if( GET_USER_STRUCT(&info->vce_in,from,sizeof(vce_input_t)) )
			{
				printk(KERN_ERR"err!copy_from_user fail!%d\n",__LINE__);
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_UNKOWN;
			}

#if Enable_Debug_PrintK
			print_vce_input_t(&info->vce_in);
#endif

#ifdef VCE_INTTERUPT_MODE 
			/* init completion var */
			init_completion(&vce_complete);
#endif      

#ifdef IC_TYPE_ATM7021
			set_registers_atm7021(&info->vce_in);
#else
			set_registers(&info->vce_in);
#endif

#if Enable_Debug_PrintK
			print_ShareSRam_CTL("b4 vce run");
#endif
			Wr_Reg(VCE_STATUS,0);

			vce_status = info->vce_in.vce_status;//Re_Reg(VCE_STATUS);
			vce_status = (vce_status|0x1)&(~(0x1<<8));
			Wr_Reg(VCE_STATUS,vce_status);   //*newW
			vce_last_handle = info->vce_count;  
			info->vce_status = RUNNING;
			mutex_unlock(&vce_ioctl_mutex);
			break;
		}

	case VCE_GET_ENC_STATUS:
		{
			mutex_lock(&vce_ioctl_mutex);
			ret_value = query_status();
			mutex_unlock(&vce_ioctl_mutex);
			break;
		}

	case VCE_CMD_QUERY_FINISH:
		{
			unsigned long timeout;
			int left_time;
			unsigned long expire;

			mutex_lock(&vce_ioctl_mutex);
			to = (void __user *)arg;

			if(info->vce_status == STOPED)
			{
				if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
				{
					printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
					mutex_unlock(&vce_ioctl_mutex);
					return VCE_ERR_UNKOWN;
				}
				mutex_unlock(&vce_ioctl_mutex);
				return 0;
			}

#if Enable_Debug_PrintK
			print_ShareSRam_CTL("b4 vce finish");
#endif

#ifdef VCE_INTTERUPT_MODE
			//已解完一帧
			ret_value = query_status();

			if(ret_value != VCE_BUSY)
			{
				get_registers(&info->vce_out);
				if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
				{
					printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
					mutex_unlock(&vce_ioctl_mutex);
					return VCE_ERR_UNKOWN;
				}
				info->vce_status = STOPED;
				mutex_unlock(&vce_ioctl_mutex);
				goto out;
			}

			timeout = msecs_to_jiffies(WAIT_TIMEOUT_MS) + 1;
			//printk(KERN_ERR"timeout:%d  jiffies ...\n",timeout); 

			if (vce_irq_registered) 
			{
				enable_vce_irq();
				left_time = wait_for_completion_timeout(&vce_complete, timeout);
				if (unlikely(left_time == 0)) 
				{
					vce_status = Re_Reg(VCE_STATUS);
					printk(KERN_ERR"err!time out!\n"); 

					if(vce_status & 0x100)
					{
						ret_value = 0;
						get_registers(&info->vce_out);
						if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
						{
							printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
							mutex_unlock(&vce_ioctl_mutex);
							return VCE_ERR_UNKOWN;
						}

						info->vce_status = STOPED;
						disable_vce_irq();
						mutex_unlock(&vce_ioctl_mutex);
						goto out;
					}

					info->vce_status = STOPED;            
					ret_value = VCE_ERR_TIMEOUT;
					disable_vce_irq();
					printk(KERN_ERR"vce_dev timeout when QUERY_FINISH \n");
					vce_reset();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				} 
				else 
				{
					/* normal case */
					ret_value = 0;
					get_registers(&info->vce_out);
					if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
					{
						printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
						mutex_unlock(&vce_ioctl_mutex);
						return VCE_ERR_UNKOWN;
					}

					info->vce_status = STOPED;
					disable_vce_irq();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}
			}
			
			//全局变量jiffies取值为自操作系统启动以来的时钟滴答的数目，在头文件<linux/sched.h>中定义,数据类型为unsigned long volatile
			//系统中采用jiffies来计算时间，但由于jiffies溢出可能造成时间比较的错误，因而强烈建议在编码中使用time_after等宏来比较时间先后关系，这些宏可以放心使用
			expire = timeout + jiffies;
			do 
			{
				ret_value = query_status();

				if(ret_value != VCE_BUSY)
				{
					get_registers(&info->vce_out);
					if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
					{
						printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
						mutex_unlock(&vce_ioctl_mutex);
						return VCE_ERR_UNKOWN;
					}

					info->vce_status = STOPED;
					disable_vce_irq();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}

				if (time_after(jiffies, expire)) {
					ret_value = VCE_ERR_TIMEOUT;
					info->vce_status = STOPED;
					disable_vce_irq();
					printk(KERN_ERR"vce_dev timeout when QUERY_FINISH jiffies\n");
					vce_reset();
					mutex_unlock(&vce_ioctl_mutex);
					goto out;
				}
			}while(1);

			mutex_unlock(&vce_ioctl_mutex);
#else
			{
				int time_counts = 0xfffffff;
				do{
					ret_value = query_status();  
					if(time_counts-- < 0)
					{
						ret_value = VCE_ERR_TIMEOUT;
						info->vce_status = STOPED;
						disable_vce_irq();
						mutex_unlock(&vce_ioctl_mutex);
						//printk(KERN_ERR"vce_ioctl  here!10,ret_value:%x\n",ret_value);
						goto out;
					}
				}while(ret_value == VCE_BUSY);

				get_registers(&info->vce_out);
				if( SET_USER_STRUCT(to,&info->vce_out,sizeof(vce_output_t)) )
				{
					printk(KERN_ERR"err!copy_to_user fail!%d\n",__LINE__);
					mutex_unlock(&vce_ioctl_mutex);
					return VCE_ERR_UNKOWN;
				}
#if Enable_Debug_PrintK
				//printk(KERN_ERR"vce_ioctl  here!8,ret_value:%x\n",ret_value);
				print_vce_output_t(&info->vce_out);
#endif
				printk(KERN_ERR"VCE has encode one frame now\n");
				info->vce_status = STOPED;
				disable_vce_irq();
				mutex_unlock(&vce_ioctl_mutex);
			}
#endif   
		}    
		break;

	case VCE_SET_DISABLE_CLK:
		//printk(KERN_ERR"vce_ioctl get clk disable cmd!\n");
		vce_clk_disable();
		break;

	case VCE_SET_ENABLE_CLK:
		//printk(KERN_ERR"vce_ioctl get clk enable cmd!\n");
		vce_clk_enable();
		break;

	case VCE_SET_FREQ:
		{
			mutex_lock(&vce_ioctl_mutex);
			from = (void __user *)arg;
			if( GET_USER_STRUCT(&vce_freq,from,sizeof(vce_multi_freq_t)) )
			{
				printk(KERN_ERR"err!copy_from_user fail!%d\n",__LINE__);
				mutex_unlock(&vce_ioctl_mutex);
				return VCE_ERR_UNKOWN;
			}
			ret_value = vce_setFreq(info,&vce_freq);
			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	case VCE_GET_FREQ:
		{
			mutex_lock(&vce_ioctl_mutex);
			ret_value = vce_getFreq();
			mutex_unlock(&vce_ioctl_mutex);
		}
		break;

	default:
		printk(KERN_ERR"err!vce_drv: no such cmd ...\n");
		return -EIO;
	}

out:
	return ret_value;
}

long compat_vce_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;	
}


int vce_open(struct inode *inode, struct file *filp)
{
	vce_info_t *info = NULL;
	int ret_frep;
	mutex_lock(&vce_ioctl_mutex);
	vce_open_count++;
	filp->private_data = NULL;
		
	printk("vce_open ------- \n");
	
	if (vce_open_count > _MAX_HANDLES_)
	{
		vce_open_count--;
		printk(KERN_ERR"max vce_drv_open ...%d.\n",vce_open_count);
		mutex_unlock(&vce_ioctl_mutex);
		return -1;
	}

	info = (vce_info_t*)vce_malloc(sizeof(vce_info_t));
	//printk(KERN_ERR"vce_drv,vce_open!info:%p,%d!\n",info,vce_open_count);
	if(info == NULL)
	{
		vce_open_count--;
		printk(KERN_ERR"vce info malloc failed!...\n");
		mutex_unlock(&vce_ioctl_mutex);
		return -1;
	}
	
#ifdef VCE_INTTERUPT_MODE    
	/* init completion var */
	if(vce_open_count == 1)
	{
		init_completion(&vce_complete);
	}
#endif

	share_mem_reg_enable();
	vce_clk_enable();
	disable_vce_irq();  
	
	info->vce_freq.width = VCE_DEFAULT_WIDTH;
	info->vce_freq.height = VCE_DEFAULT_HEIGHT;
	info->vce_freq.freq = VCE_DEFAULT_FREQ;
		
	if(vce_open_count == 1)
	{
		if (vce_power_is_enable)
		{
			//printk(KERN_ERR"vce_power_on in reset \n");
			//上电时默认要复位
			vce_reset();
		}
		else
		{
			printk(KERN_ERR"vce_dev Warning!vce is not powered!\n");
		}

#if Enable_Fix_CLK_Drv
		//vce_set_vdd_voltage_init();

		vce_freq_multi.width = VCE_DEFAULT_WIDTH;
		vce_freq_multi.height = VCE_DEFAULT_HEIGHT;
		vce_freq_multi.freq = VCE_DEFAULT_FREQ;
		ret_frep = vce_setFreq(info,&info->vce_freq);
		//printk(KERN_ERR"vcedrv:freq_init to %d MHZ!!\n",ret_frep);
		if(ret_frep < 0)
		{
			vce_freq_is_init = 0;
			vce_free(info);
			share_mem_reg_disable();
			vce_clk_disable();
			vce_open_count--;
			mutex_unlock(&vce_ioctl_mutex);
			printk(KERN_ERR"vcedrv err:freq_init to %d MHZ fail %d\n",VCE_DEFAULT_FREQ,vce_open_count);
			return -1;
		}
#endif
	} 
	pAbuf[vce_open_count - 1] = (void*)info;
	info->vce_count = vce_open_count;   /*当前vce info序列号，从1开始*/
	info->vce_status = STOPED;
	filp->private_data = (void*)info;
	mutex_unlock(&vce_ioctl_mutex);
	//printk(KERN_ERR"out of vce_drv_open ...%d.\n",vce_open_count);

	return 0;
}

int vce_release(struct inode *inode, struct file *filp)
{
	vce_info_t *info = (vce_info_t *) filp->private_data;
	//printk(KERN_ERR"vce_drv_release ...count:%d,info:%p\n",vce_open_count,info);
	
	if(info == NULL){
		printk(KERN_ERR"vce_drv ERR:Vce Info is Null ,return \n");
		return 0;
	}
	mutex_lock(&vce_ioctl_mutex);
	
	if (vce_open_count >= 1) 
		pAbuf_release(info->vce_count);
	
	vce_open_count--;
	if (vce_open_count >= 0) 
	{
		vce_free(info);
		info = filp->private_data = NULL;
	} 
	else if (vce_open_count < 0) 
	{
		printk(KERN_ERR"vce_drv ERR:count %d,%p\n",vce_open_count,info);
		vce_open_count = 0;
	}

	if(vce_open_count == 0)
	{		
#if  Enable_Fix_CLK_Drv
		vce_freq_multi.width = VCE_DEFAULT_WIDTH;
		vce_freq_multi.height = VCE_DEFAULT_HEIGHT;
		vce_freq_multi.freq = VCE_DEFAULT_FREQ;
		vce_freq_is_init = 0;		
		//vce_set_vdd_voltage_deinit();
#endif
		vce_stop();
		disable_vce_irq();
		vce_last_handle = 0;	
		share_mem_reg_disable();
	}
	
	vce_clk_disable();
	mutex_unlock(&vce_ioctl_mutex);

	return 0;
}

/* 
*进入低功耗之前，必须保证vce已经编码完当前帧
 */
int vce_suspend (struct platform_device * dev, pm_message_t state)
{    
	//printk(KERN_ERR"vce_suspend in %d,%d\n",vce_clk_isEnable,vce_open_count);
	mutex_lock(&vce_ioctl_mutex);
	if( (vce_open_count > 0) && (vce_clk_isEnable == 1) )
	{
		vce_stop();
		disable_vce_irq();
		//printk(KERN_ERR"vce_suspend!clk disable!\n");
		share_mem_reg_disable();
		vce_clk_disable();
	}
	mutex_unlock(&vce_ioctl_mutex);
	//printk(KERN_ERR"vce_suspend out %d,%d\n",vce_clk_isEnable,vce_open_count);
	return 0;
}

int vce_resume (struct platform_device * dev)
{
	//printk(KERN_ERR"vce_resume in %d,%d\n",vce_clk_isEnable,vce_open_count);
	mutex_lock(&vce_ioctl_mutex);
	
	if( (vce_open_count > 0) && (vce_clk_isEnable == 0) )
	{
		share_mem_reg_enable();
		vce_clk_enable();
	}
	mutex_unlock(&vce_ioctl_mutex);
	//printk(KERN_ERR"vce_resume out %d,%d\n",vce_clk_isEnable,vce_open_count);
  return 0;
}


static struct ic_info ls360f_data = {
	.ic_type = IC_TYPE_5202E,
};


static const struct of_device_id owl_vce_of_match[] = {
	{.compatible = "actions,ats3605-vce", .data = &ls360f_data},
	{}
};

MODULE_DEVICE_TABLE(of, owl_vce_of_match);

static int vce_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	printk(" -- vce-prob start ----------------------------\n");
	const struct of_device_id *id = of_match_device(owl_vce_of_match, &pdev->dev);
	if (id != NULL) {
		struct ic_info *info = (struct ic_info *)id->data;
		if (info != NULL) {
			ic_type = info->ic_type;
			vce_info("ic_type(0x%x)!\n", ic_type);
		} else {
			vce_info("info is null!\n");
		}
	} else {
		vce_err("id is null!\n");
	}
	
	vce_device = &pdev->dev;
	pm_runtime_enable(vce_device);	/* pm enable */
	vce_info("aft powergate ...\n");
	
	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(reset)) {
		vce_err("devm_reset_control_get(&pdev->dev, NULL) failed\n");
		return PTR_ERR(reset);
	}
	vce_info("aft reset_get ...\n");
	
	/* get clk */
	vce_clk = devm_clk_get(&pdev->dev, "vce");
	if (IS_ERR(vce_clk)) {
		vce_err("devm_clk_get(&pdev->dev, 'vce') failed\n");
		return PTR_ERR(vce_clk);
	}
	vce_info("aft clk_get ...\n");

	/* get irq */
	irq_vce = platform_get_irq(pdev, 0);
	vce_info("vce->irq =%d\n", irq_vce);
	if (irq_vce < 0)
		return irq_vce;
	
	ret = devm_request_irq(&pdev->dev, irq_vce, (void *)vce_ISR, 0, "vce_isr", 0);
	if (ret) {
		vce_err("register vce irq failed!...\n");
		vce_irq_registered = 0;
		return ret;
	} else {
		vce_irq_registered = 1;
	}	
	vce_info("aft irq_request ...\n");

	/* get vol regulator */
	vol_set = devm_regulator_get(&pdev->dev, "dcdc1");
	vce_info("vol_set:%p\n", vol_set);
	if (IS_ERR(vol_set)){
		vce_warning
		    ("cann't get vol regulator, may be this board not need, or lost in dts!\n");
	}else{
		vce_info(" -- init vdd2:%d\n", VOL_INIT);
		if (regulator_set_voltage(vol_set, VOL_INIT, VOL_MAX)) {
				vce_err("cannot set corevdd to %duV !\n", VOL_INIT);
				return -1;
		}
		vce_info("init vdd:%d\n", VOL_INIT);
		if (regulator_enable(vol_set) != 0) {
				vce_err("vdd regulator err!\n");
				return -1;
		}
		vce_info("vdd regulator enable!\n");
	}	
	printk(" ---  test -----!\n");		
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res != NULL) {
		if (request_mem_region(res->start, resource_size(res), "vce") != NULL) {
			vce_info("vce_probe!start = %p,size = %d!\n",(void *)res->start, (int)resource_size(res));
			iobase_vce =(unsigned long)ioremap(res->start,resource_size(res));
			vce_info("vce_probe!iobase_vce = %p!\n",(void *)iobase_vce);
			if (iobase_vce == 0) {
				vce_err("iobase_vce is NULL!\n");
				goto err;
			}
			switch (ic_type) {
			case IC_TYPE_5202E:
				iobase_cmu_devclken0 = (unsigned long)ioremap(0xB01500A0, 4);
				vce_clk_en = 0x04000000;
				iobase_sps_pg_ctl = (unsigned long)ioremap(0xE012E000, 4);
				vce_pg_en = 0x00000010;
				break;
			default:
				vce_err("unsupported ic type!\n");
				goto err;
			}
			vce_info("vce_probe!iobase_cmu_devclken0 = %p!\n",(void *)iobase_cmu_devclken0);
			if (iobase_cmu_devclken0 == 0) {
				vce_err("iobase_cmu_devclken0 is NULL!\n");
				goto err;
			}
			vce_info("vce_probe!iobase_sps_pg_ctl = %p!\n",(void *)iobase_sps_pg_ctl);
			if (iobase_sps_pg_ctl == 0) {
				vce_err("iobase_sps_pg_ctl is NULL!\n");
				goto err;
			}
		} else {
			vce_err("request_mem_region is fail!\n");
			return -1;
		}
	} else {
		vce_err("res is null!\n");
		return -1;
	}
	return 0;
err:
	if (iobase_vce != 0) {
		iounmap((void *)iobase_vce);
		iobase_vce = 0;
	}
	if (iobase_cmu_devclken0 != 0) {
		iounmap((void *)iobase_cmu_devclken0);
		iobase_cmu_devclken0 = 0;
	}
	if (iobase_sps_pg_ctl != 0) {
		iounmap((void *)iobase_sps_pg_ctl);
		iobase_sps_pg_ctl = 0;
	}
	return -1;	
}

static int vce_remove(struct platform_device *pdev)
{	
	struct resource *res;

	/* disable vol regulator */
	if (!IS_ERR(vol_set)) {
		regulator_disable(vol_set);
		vce_info("vdd regulator disable!\n");
	}

	/* other resource, such as clk, reset, iqr, vol regulator, use those functions like
	   devm_XXX_get(). Its will be released automatically. */

	/* free resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		vce_info("vce_remove!start = %p,size = %d!\n",
			 (void *)res->start, (int)resource_size(res));
		release_mem_region(res->start, resource_size(res));
	} else {
		printk("res is null!\n");
	}

	vce_info("vce_remove!iobase_vce = %p!\n", (void *)iobase_vce);
	if (iobase_vce != 0) {
		iounmap((void *)iobase_vce);
		iobase_vce = 0;
	}
	if (iobase_cmu_devclken0 != 0) {
		iounmap((void *)iobase_cmu_devclken0);
		iobase_cmu_devclken0 = 0;
	}
	if (iobase_sps_pg_ctl != 0) {
		iounmap((void *)iobase_sps_pg_ctl);
		iobase_sps_pg_ctl = 0;
	}
	return 0;
}

static struct file_operations vce_fops = 
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = vce_ioctl,
	.compat_ioctl = compat_vce_ioctl,
	.open = vce_open,
	.release = vce_release,
};




static struct platform_driver vce_platform_driver = {
	.driver = {
		   .name = DEVDRV_NAME_VCE,
		   .owner = THIS_MODULE,
		   .of_match_table = owl_vce_of_match,
		   },
	.probe = vce_probe,
	.remove = vce_remove,
	.suspend = vce_suspend,
	.resume = vce_resume,
};

static struct miscdevice vce_miscdevice = 
{
  .minor = MISC_DYNAMIC_MINOR,
  .name = DEVDRV_NAME_VCE,
  .fops = &vce_fops,
};

//static struct notifier_block thermal_notifier_block = 
//{
//	.notifier_call = vce_thermal_notifier,
//};

static int vce_init(void)
{
  int ret;	
  //自动insmod，注册设备
  ret = misc_register(&vce_miscdevice);
  if (ret) 
  {
    printk(KERN_ERR"register vce misc device failed!...\n");
    goto err0;
  }
  printk(KERN_INFO"vce_drv_init ....\n"); 
  ret = platform_driver_register(&vce_platform_driver);
  if (ret) 
  {
    printk(KERN_ERR"register vce platform driver error!...\n");
    goto err2;
   } 
#if Enable_Fix_VCE_Drv
	init_waitqueue_head(&vce_wait);
#endif
  //cputherm_register_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);
  return 0;
err2:	
err1:
	misc_deregister(&vce_miscdevice);	
err0:
  return ret;
}

static void vce_exit(void)
{
	printk(KERN_ERR"vce_drv_exit ....\n");
	//cputherm_unregister_notifier(&thermal_notifier_block, CPUFREQ_COOLING_START);
	misc_deregister(&vce_miscdevice);
	platform_driver_unregister(&vce_platform_driver);
}

module_init(vce_init);
module_exit(vce_exit);

MODULE_LICENSE("GPL");

