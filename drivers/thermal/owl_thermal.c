/*
 * owl_thermal.c - Actions OWL TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2014 Actions (Zhuhai) Technology Co., Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_data/owl_thermal.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/delay.h>

static int debug_flag;

/* s500 specific registers */

/* In-kernel thermal framework related macros & definations */
#define THERMAL_CLOCK_RATE 12000000
#define SENSOR_NAME_LEN	20
#define MAX_TRIP_COUNT	8
#define MAX_COOLING_DEVICE 4

#define ACTIVE_INTERVAL 500
#define IDLE_INTERVAL 2000
#define MCELSIUS	1000

/* CPU Zone information */
#define PANIC_ZONE      4
#define WARN_ZONE       3
#define MONITOR_ZONE    2
#define SAFE_ZONE       1

#define GET_ZONE(trip) (trip + 2)
#define GET_TRIP(zone) (zone - 2)

#define OWL_ZONE_COUNT	3

#define MAX_THERMAL_NUM 9
struct owl_tmu_data {
	struct owl_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int id;
	/* int irq; */
	enum soc_type soc;
	/* struct work_struct irq_work; */
	struct delayed_work wait_cpufreq_ready_work;
	struct mutex lock;
	/* struct clk *clk; */
	u8 temp_error1, temp_error2;
	int temp_emu;
	struct clk *tmu_clk;
};

struct thermal_trip_point_conf {
	int trip_val[MAX_TRIP_COUNT];
	int trip_count;
	u8 trigger_falling;
};

struct thermal_cooling_conf {
	struct freq_clip_table freq_data[MAX_TRIP_COUNT];
	int freq_clip_count;
};

struct thermal_sensor_conf {
	char name[SENSOR_NAME_LEN];
	int (*read_temperature) (void *data);
	int (*write_emul_temp) (void *drv_data, unsigned long temp);
	struct thermal_trip_point_conf trip_data;
	struct thermal_cooling_conf cooling_data;
	void *private_data;
};

struct owl_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *owl_dev;
	struct thermal_sensor_conf *sensor_conf;
	bool bind;
};

static struct owl_thermal_zone *th_zone[MAX_THERMAL_NUM];
static struct thermal_sensor_conf *owl_sensor_conf[MAX_THERMAL_NUM];

static void owl_unregister_thermal(int device_id);
static int owl_register_thermal(struct thermal_sensor_conf *sensor_conf);
static int tmu_clk_enable(struct owl_tmu_data *data, bool enable);

/* Get mode callback functions for thermal zone */
static int owl_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;
	if (th_zone[index])
		*mode = th_zone[index]->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int owl_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (!th_zone[index]->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&th_zone[index]->therm_dev->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		th_zone[index]->therm_dev->polling_delay = IDLE_INTERVAL;
	else
		th_zone[index]->therm_dev->polling_delay = 0;

	mutex_unlock(&th_zone[index]->therm_dev->lock);

	th_zone[index]->mode = mode;
	thermal_zone_device_update(th_zone[index]->therm_dev);
	pr_info("thermal polling set for duration=%d msec\n",
		th_zone[index]->therm_dev->polling_delay);
	return 0;
}

/* Get trip type callback functions for thermal zone */
static int owl_get_trip_type(struct thermal_zone_device *thermal, int trip,
			     enum thermal_trip_type *type)
{
	switch (GET_ZONE(trip)) {
	case MONITOR_ZONE:
	case WARN_ZONE:
		*type = THERMAL_TRIP_ACTIVE;
		break;
	case PANIC_ZONE:
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int owl_get_trip_temp(struct thermal_zone_device *thermal, int trip,
			     unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	*temp = th_zone[index]->sensor_conf->trip_data.trip_val[trip];

	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Set trip temperature callback functions for thermal zone */
static int owl_set_trip_temp(struct thermal_zone_device *thermal, int trip,
			     unsigned long temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	pr_notice("set temp%d= %ld\n", trip, temp);
	th_zone[index]->sensor_conf->trip_data.trip_val[trip] = temp / MCELSIUS;
	return 0;
}

/* Get trip hyst temperature callback functions for thermal zone */
static int owl_get_trip_hyst(struct thermal_zone_device *thermal, int trip,
			     unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	*temp = th_zone[index]->sensor_conf->trip_data.trigger_falling;
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int owl_get_crit_temp(struct thermal_zone_device *thermal,
			     unsigned long *temp)
{
	int ret;

	/* Panic zone */
	pr_info("owl_get_crit_temp\n");
	ret = owl_get_trip_temp(thermal, GET_TRIP(PANIC_ZONE), temp);
	return ret;
}

/* Bind callback functions for thermal zone */
static int owl_bind(struct thermal_zone_device *thermal,
		    struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size, level;
	int index;
	struct owl_tmu_data *data_tmu;
	data_tmu = (struct owl_tmu_data *)(thermal->devdata);
	index = data_tmu->id;

	struct freq_clip_table *tab_ptr, *clip_data;
	struct thermal_sensor_conf *data;
	data = th_zone[index]->sensor_conf;
	tab_ptr = (struct freq_clip_table *)data->cooling_data.freq_data;
	tab_size = data->cooling_data.freq_clip_count;

	if (tab_ptr == NULL || tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered */
	for (i = 0; i < th_zone[index]->cool_dev_size; i++)
		if (cdev == th_zone[index]->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone[index]->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		clip_data = (struct freq_clip_table *)&(tab_ptr[i]);
		level = cpufreq_cooling_get_level(0, clip_data->freq_clip_max);
		if (level == THERMAL_CSTATE_INVALID)
			return 0;
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_bind_cooling_device
			    (thermal, i, cdev, level, 0)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
			}
			th_zone[index]->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int owl_unbind(struct thermal_zone_device *thermal,
		      struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size;
	int index;
	struct owl_tmu_data *data_tmu;
	data_tmu = (struct owl_tmu_data *)(thermal->devdata);
	index = data_tmu->id;

	struct thermal_sensor_conf *data;
	data = th_zone[index]->sensor_conf;

	if (th_zone[index]->bind == false)
		return 0;

	tab_size = data->cooling_data.freq_clip_count;

	if (tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered */
	for (i = 0; i < th_zone[index]->cool_dev_size; i++)
		if (cdev == th_zone[index]->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone[index]->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_unbind_cooling_device
			    (thermal, i, cdev)) {
				pr_err("error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone[index]->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

/* Get temperature callback functions for thermal zone */
static int owl_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	int index;
	struct owl_tmu_data *data;
	data = (struct owl_tmu_data *)(thermal->devdata);
	index = data->id;

	if (!th_zone[index]->sensor_conf) {
		pr_info("Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone[index]->sensor_conf->private_data;
	*temp = th_zone[index]->sensor_conf->read_temperature(data);
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;
	return 0;
}

/* Get the temperature trend */
static int owl_get_trend(struct thermal_zone_device *thermal, int trip,
			 enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = owl_get_trip_temp(thermal, trip, &trip_temp);
	if (ret < 0)
		return ret;
	/*TODO: */
	if (thermal->temperature >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}

/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops owl_dev_ops = {
	.bind = owl_bind,
	.unbind = owl_unbind,
	.get_temp = owl_get_temp,
	.get_trend = owl_get_trend,
	.get_mode = owl_get_mode,
	.set_mode = owl_set_mode,
	.get_trip_type = owl_get_trip_type,
	.get_trip_temp = owl_get_trip_temp,
	.set_trip_temp = owl_set_trip_temp,
	.get_trip_hyst = owl_get_trip_hyst,
	.get_crit_temp = owl_get_crit_temp,
};

/* Register with the in-kernel thermal management */
static int owl_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;
#if 0
	struct cpumask mask_val;
#endif
	struct owl_tmu_data *data = sensor_conf->private_data;
	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone[data->id] =
	    kzalloc(sizeof(struct owl_thermal_zone), GFP_KERNEL);
	if (!th_zone[data->id])
		return -ENOMEM;

	th_zone[data->id]->sensor_conf = sensor_conf;
#if 0
	cpumask_set_cpu(0, &mask_val);
	th_zone->cool_dev[0] = cpufreq_cooling_register(&mask_val);
	if (IS_ERR(th_zone->cool_dev[0])) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	}
	th_zone->cool_dev_size++;
#endif
	pr_info("Begin to register thermal zone ,id = %d\n", data->id);

	th_zone[data->id]->therm_dev =
	    thermal_zone_device_register(sensor_conf->name, OWL_ZONE_COUNT, 0x7,
					 (void *)(sensor_conf->private_data),
					 &owl_dev_ops, NULL, 0, IDLE_INTERVAL);

	if (IS_ERR(th_zone[data->id]->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = PTR_ERR(th_zone[data->id]->therm_dev);
		goto err_unregister;
	}

	th_zone[data->id]->mode = THERMAL_DEVICE_ENABLED;

	pr_info("OWL: Kernel Thermal management registered\n");

	return 0;

      err_unregister:
	owl_unregister_thermal(data->id);
	return ret;
}

/* Un-Register with the in-kernel thermal management */
static void owl_unregister_thermal(int device_id)
{
#if 1
	int i;

	if (!th_zone[device_id])
		return;

	if (th_zone[device_id]->therm_dev)
		thermal_zone_device_unregister(th_zone[device_id]->therm_dev);

	for (i = 0; i < th_zone[device_id]->cool_dev_size; i++) {
		if (th_zone[device_id]->cool_dev[i])
			cpufreq_cooling_unregister(th_zone[device_id]->
						   cool_dev[i]);
	}

	kfree(th_zone[device_id]);
	pr_info("OWL: Kernel Thermal management unregistered\n");
#endif
}

static int tmu_clk_enable(struct owl_tmu_data *data, bool enable)
{

	if (enable)
		clk_prepare_enable(data->tmu_clk);
	else
		clk_disable_unprepare(data->tmu_clk);
	return 0;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 * T = 838.45*7.894/(1024*12/count+7.894)-275+offset
 */
/*static int offset;*/
static int code_to_temp(struct owl_tmu_data *data, u32 temp_code)
{
#if 0
	int tmp1, tmp2;
	tmp1 = 83845 * 7894;
	tmp2 = (1024 * 12 * 100000 / temp_code) + 789400;
	tmp1 = tmp1 / tmp2;
	tmp1 = tmp1 - 275 + offset;
	/*
	   pr_info("temp:%d\n", tmp1);
	 */
#endif

	int temp1, temp2;
	/*4562 25 centigrade 330，substitute 2890 to chipid */
	temp1 = 318326 * 2890;
	temp2 = temp_code * 10000;
	temp1 = temp1 / temp2;
	temp1 = 296 - temp1;
	return temp1;
}

#if 0
static int owl_tmu_initialize(struct platform_device *pdev)
{
	u32 ret = 0;
	void __iomem *base;
#if 0
	base = ioremap(0xe01600a4, 4);
	ret = readl(base);
	ret = ret | (1 << 2);
	writel(ret, base);
	ret = readl(base);
	pr_info("HUANGXU:THERMAL SENSOR CLOCK  initialize= 0x%x\n", ret);
#endif
	base = ioremap(0xe01600c0, 4);	/*clock */
	ret = readl(base);
	ret = ret | (1 << 8);	/*freq div = 2 */
	writel(ret, base);
	return 0;
}
#endif

static void owl_tmu_control(struct platform_device *pdev, bool on)
{

}

static const char *devname[10] = {
	"CPU0",
	"CPU1",
	"CPU2",
	"CPU3",
	"GPU0",
	"GPU1",
	"HDE",
	"VDE",
	"CoreLogic",
	"Unknown",
};

#define TRY_TEMP_CNT	5
#define TRY_FAIL_COUNT  5
/*clk=24M,divide = 2,temp data = 500,
so readtime=(1/(24M/2) )*500 *10= 0.4ms,
we set 1ms*/
#define TIME_OUT 1
static u32 owl_tmu_temp_code_read(struct owl_tmu_data *data)
{

	u32 tmp, temp_code, temp_arry[TRY_TEMP_CNT];
	int ready, i, j, k;
	void __iomem *TSCR0;
	TSCR0 = data->base;
	temp_code = 0;
#if 0
	base = ioremap(0xe01600a4, 4);

	ret = readl(base);
	ret = ret | (1 << 2);
	writel(ret, base);
#endif
	tmu_clk_enable(data, true);
	for (i = 0; i < TRY_TEMP_CNT; i++) {
		tmp = readl(TSCR0);
		tmp = tmp | (1 << 24);	/* enable tsensor */
		writel(tmp, TSCR0);
		ready = 0;
		k = 0;
		do {
			tmp = readl(TSCR0);
			ready = tmp & (1 << 12);
			k++;
			msleep(TIME_OUT);
			if (k == TRY_FAIL_COUNT)
				break;
			/*pr_info("HUANGXU:ready= %d\n ",ready); */
			/*pr_info("HUANGXU:k= %d\n ",k); */
		} while ((ready == 0));
		if (k == TRY_FAIL_COUNT) {
			if (debug_flag)
				pr_info
				    ("%s not ready, use default tempurature\n ",
				     devname[data->id]);
			/*25 centigrade */
			temp_code = 340;
			goto finish;
		} else {
			if (debug_flag) {
				pr_info("%s ready, after fail count %d\n ",
					devname[data->id], k);
			}
		}
		temp_arry[i] = tmp & 0x3ff;
		tmp = readl(TSCR0);
		tmp = tmp & (~(1 << 24));	/* disable tsensor */
		writel(tmp, TSCR0);
	}
#if 1
	/* sort temp arry */
	for (i = 0; i < TRY_TEMP_CNT - 1; i++) {
		for (j = i + 1; j < TRY_TEMP_CNT; j++) {
			if (temp_arry[j] < temp_arry[i]) {
				tmp = temp_arry[i];
				temp_arry[i] = temp_arry[j];
				temp_arry[j] = tmp;
			}
		}
	}

	/* discard min & max, then take ther average */
	for (i = 1; i < TRY_TEMP_CNT - 1; i++)
		temp_code += temp_arry[i];

	temp_code = temp_code / (TRY_TEMP_CNT - 2);
	if (debug_flag)
		pr_info("%s temp_code:%d\n", devname[data->id], temp_code);
#endif

      finish:
	tmu_clk_enable(data, false);
	/*temp_code = temp_arry[0]; */
	return temp_code;
}

static int owl_tmu_read(struct owl_tmu_data *data)
{
	int temp_code;
	int temp;
	mutex_lock(&data->lock);
	temp_code = owl_tmu_temp_code_read(data);
	temp = code_to_temp(data, temp_code);
	if (debug_flag) {
		pr_info("%s:Current tempurature is %d centigrade\n",
			devname[data->id], temp);
	}
	mutex_unlock(&data->lock);
	return temp;
}

#if 0
static ssize_t show_debug(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%d\n", debug_flag);
}

static ssize_t store_debug(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	if (sscanf(buf, "%d", &debug_flag) != 1)
		return -EINVAL;
	return count;
}

static DEVICE_ATTR(debug, 0644, show_debug, store_debug);

static struct attribute *thermal_sysfs_entries[] = {
	&dev_attr_debug.attr,
	NULL,
};
#endif

module_param(debug_flag, uint, 0644);
MODULE_PARM_DESC(debug_flag, "module debug switch (0=off,1=on)");

static struct owl_tmu_platform_data s900_default_tmu_data = {
	.threshold_falling = 10,
	.threshold = 100,
	.trigger_levels[0] = 0,
	.trigger_levels[1] = 10,
	.trigger_levels[2] = 20,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 0,
	.freq_tab[0] = {
			.freq_clip_max = 624 * 1000,
			},
	.freq_tab[1] = {
			.freq_clip_max = 624 * 1000,
			},
	.freq_tab_count = 2,
	.type = SOC_ARCH_OWL,
};

#define S900_TMU_DRV_DATA (&s900_default_tmu_data)

#ifdef CONFIG_OF
static const struct of_device_id owl_tmu_match[] = {
	{
	 .compatible = "actions,s900-thermal",
	 .data = (void *)S900_TMU_DRV_DATA,
	 },
	{},
};

MODULE_DEVICE_TABLE(of, owl_tmu_match);
#endif

static struct platform_device_id owl_tmu_driver_ids[] = {
	{
	 .name = "s900-tmu",
	 .driver_data = (kernel_ulong_t) S900_TMU_DRV_DATA,
	 },
	{},
};

MODULE_DEVICE_TABLE(platform, owl_tmu_driver_ids);

static inline struct owl_tmu_platform_data *owl_get_driver_data(struct
								platform_device
								*pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(owl_tmu_match, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct owl_tmu_platform_data *)match->data;
	}
#endif
	return (struct owl_tmu_platform_data *)
	    platform_get_device_id(pdev)->driver_data;
}

/*
wait for cpufreq ready
 */
#if 0
static void wait_cpufreq_ready(struct work_struct *work)
{
	int ret = 0;
	struct owl_tmu_data *data;
	struct cpufreq_frequency_table *table;

	data = (&owl_sensor_conf)->private_data;
#if 0
	table = cpufreq_frequency_get_table(0);
	if (NULL == table) {
		schedule_delayed_work(&data->wait_cpufreq_ready_work, HZ);
		pr_notice("--------cpufreq not ready--------\n");
		return;
	}
#endif
	pr_notice("++++++++cpufreq ready++++++++\n");

	ret = owl_register_thermal(&owl_sensor_conf);
	if (ret)
		pr_err("Failed to register thermal interface\n");
	return;
}
#endif

#if 0
static void output_temp(struct work_struct *work)
{
#if 0
	int ret = 0;
	struct owl_tmu_data *data;
	struct cpufreq_frequency_table *table;

	data = (&owl_sensor_conf)->private_data;
	table = cpufreq_frequency_get_table(0);
	if (NULL == table) {
		schedule_delayed_work(&data->wait_cpufreq_ready_work, HZ);
		pr_notice("--------cpufreq not ready--------\n");
		return;
	}
	pr_notice("++++++++cpufreq ready++++++++\n");

	ret = owl_register_thermal(&owl_sensor_conf);
	if (ret)
		pr_err("Failed to register thermal interface\n");
#endif
	int ret = 0;

	struct owl_tmu_data *data;
	data = (&owl_sensor_conf)->private_data;
#if 1
	ret = owl_tmu_read(data);
#endif
	schedule_delayed_work(&data->wait_cpufreq_ready_work,
			      msecs_to_jiffies(4000));
	return;
}
#endif

/*
set tmu threshold according to opt_flag
 */
int set_tmu_threshold(struct owl_tmu_platform_data *pdata)
{
#if 0
	int opt_flag;
	opt_flag = owl_get_board_opt_flags();

	if (opt_flag) {		/* weld resistor of adc option */
		pdata->threshold = 95;
		pdata->trigger_levels[0] = 0;	/* 95° */
		pdata->trigger_levels[1] = 10;	/* 105° */
		pdata->trigger_levels[2] = 20;	/* 115° */
	} else {		/* no resistor of adc option */
#endif

		pdata->threshold = 150;
		pdata->trigger_levels[0] = 0;	/* 80° */
		pdata->trigger_levels[1] = 10;	/* 90° */
		pdata->trigger_levels[2] = 20;	/* 100° */
#if 0
	}
#endif
	return 0;
}

static int owl_tmu_probe(struct platform_device *pdev)
{
	struct owl_tmu_data *data;
	struct owl_tmu_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	/*struct clk *parent_clk; */
	int ret, i;
	ret = 0;
	if (!pdata)
		pdata = owl_get_driver_data(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	set_tmu_threshold(pdata);

	data =
	    devm_kzalloc(&pdev->dev, sizeof(struct owl_tmu_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(np, "id", &pdev->id)) {
		dev_err(&pdev->dev, "HUANGXU:Failed to get pdev->id!\n");
		return -EINVAL;
	}

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pr_info("PDEVID = %d\n", pdev->id);
	pr_info("tmu-mem:start=0x%lu\n", data->mem->start);

	data->base = devm_ioremap_resource(&pdev->dev, data->mem);
	data->id = pdev->id;

	data->tmu_clk = devm_clk_get(&pdev->dev, "thermal_sensor");

	if (IS_ERR(data->tmu_clk)) {
		/*DSSERR("can't get de clk\n"); */
		return PTR_ERR(data->tmu_clk);
	}
#if 0
	parent_clk = devm_clk_get(&pdev->dev, "hosc");
	if (IS_ERR(parent_clk)) {
		/*DSSERR("%s, get parent clk error\n", __func__); */
		return PTR_ERR(parent_clk);
	}
	clk_set_parent(data->tmu_clk, parent_clk);
#endif

	clk_set_rate(data->tmu_clk, THERMAL_CLOCK_RATE);
	pr_info("tmu-mem:data->base=0x%p\n", data->base);

	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	if (pdata->type == SOC_ARCH_OWL || pdata->type == SOC_ARCH_OWL_2)
		data->soc = pdata->type;
	else {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);
	/*ret = owl_tmu_initialize(pdev); */

	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	owl_tmu_control(pdev, true);

	owl_sensor_conf[data->id] =
	    kzalloc(sizeof(struct thermal_sensor_conf), GFP_KERNEL);

	snprintf(owl_sensor_conf[data->id]->name, SENSOR_NAME_LEN,
		 "owl-thermal-%s", devname[data->id]);

	owl_sensor_conf[data->id]->read_temperature =
	    (int (*)(void *))owl_tmu_read;

	(owl_sensor_conf[data->id])->private_data = data;

	owl_sensor_conf[data->id]->trip_data.trip_count =
	    pdata->trigger_level0_en + pdata->trigger_level1_en +
	    pdata->trigger_level2_en + pdata->trigger_level3_en;

	for (i = 0; i < owl_sensor_conf[data->id]->trip_data.trip_count; i++)
		owl_sensor_conf[data->id]->trip_data.trip_val[i] =
		    pdata->threshold + pdata->trigger_levels[i];

	owl_sensor_conf[data->id]->trip_data.trigger_falling =
	    pdata->threshold_falling;

	owl_sensor_conf[data->id]->cooling_data.freq_clip_count =
	    pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		owl_sensor_conf[data->id]->cooling_data.freq_data[i].
		    freq_clip_max = pdata->freq_tab[i].freq_clip_max;
		owl_sensor_conf[data->id]->cooling_data.freq_data[i].
		    temp_level = pdata->freq_tab[i].temp_level;
	}
#if 0
	INIT_DELAYED_WORK(&data->wait_cpufreq_ready_work, wait_cpufreq_ready);
	schedule_delayed_work(&data->wait_cpufreq_ready_work, HZ);
#endif
#if 1
	/* Register the sensor with thermal management interface */
	ret = owl_register_thermal(owl_sensor_conf[data->id]);

	if (ret) {
		pr_err("Failed to register thermal interface\n");
		return ret;
	}
#endif
#if 0
	INIT_DELAYED_WORK(&data->wait_cpufreq_ready_work, output_temp);
	schedule_delayed_work(&data->wait_cpufreq_ready_work,
			      msecs_to_jiffies(2000));
#endif

#if 0
	ret = device_create_file(&pdev->dev, &dev_attr_debug);

	if (ret) {
		pr_err("Failed to register thermal platform sysfs interface\n");
		return ret;
	}
#endif
	return 0;
      err_clk:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int owl_tmu_remove(struct platform_device *pdev)
{
	owl_tmu_control(pdev, false);

	owl_unregister_thermal(pdev->id);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int owl_tmu_suspend(struct device *dev)
{
	/*struct owl_tmu_data *data = dev_get_drvdata(dev); */
	/*owl_tmu_initialize(pdev); */
	/*tmu_clk_enable(data, false); */
	owl_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int owl_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	/*struct owl_tmu_data *data = dev_get_drvdata(dev); */
	/*owl_tmu_initialize(pdev); */
	/*tmu_clk_enable(data, true); */

	owl_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(owl_tmu_pm, owl_tmu_suspend, owl_tmu_resume);
#define OWL_TMU_PM	(&owl_tmu_pm)
#else
#define OWL_TMU_PM	NULL
#endif

static struct platform_driver owl_tmu_driver = {
	.driver = {
		   .name = "owl-tmu",
		   .owner = THIS_MODULE,
		   .pm = OWL_TMU_PM,
		   .of_match_table = of_match_ptr(owl_tmu_match),
		   },
	.probe = owl_tmu_probe,
	.remove = owl_tmu_remove,
	.id_table = owl_tmu_driver_ids,
};

module_platform_driver(owl_tmu_driver);

MODULE_DESCRIPTION("OWL TMU Driver");
MODULE_AUTHOR("Actions (Zhuhai) Technology Co., Limited");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:owl-tmu");
