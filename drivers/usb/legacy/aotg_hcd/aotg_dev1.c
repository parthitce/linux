#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/usb/otg.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#include <linux/regulator/consumer.h>

#include "aotg_regs.h"
#include "aotg_plat_data.h"


static bool wifi_power_off = false;

// for platform bus match
static const char driver_name[] = "aotg_hcd";
extern const struct of_device_id aotg_hcd_of_match[];

static struct platform_device *aotg_dev1;

static struct regulator *wifi33_regulator;

struct wake_lock aotg_dev1_wakelock;
static int usb_gpio_vbus_pin = -1;
static int aotg1_initialized;

static unsigned long aotg_last_jiffies = 0;

void aotg1_wakelock_init(int flag)
{
	if (flag) {
		pr_info("init aotg_dev1 wakelock!\n");
		wake_lock_init(&aotg_dev1_wakelock, WAKE_LOCK_SUSPEND, "aotg_dev1");
	} else if (!flag) {
		pr_info("destroy aotg_dev1 wakelock!\n");
		wake_lock_destroy(&aotg_dev1_wakelock);
	}
}

static int usb_wifi_get_power(void)
{
	if (usb_gpio_vbus_pin < 0)
		goto use_wifi33;
	if (usb_gpio_vbus_pin == 0)
		return -1;

	gpio_direction_output(usb_gpio_vbus_pin, 0);
	gpio_set_value_cansleep(usb_gpio_vbus_pin, 1);
	pr_debug("usb wifi power on\n");

	return 0;

use_wifi33:
	//wifi33_regulator = regulator_get(NULL, "wifi33");
	wifi33_regulator = regulator_get(NULL, "ldo1");

	if (IS_ERR(wifi33_regulator)) {
		pr_err("failed to get regulator %s\n", "ldo1");
		return -ENODEV;
	}
	//wifi12_regulator = regulator_get(NULL, "wifi12");

	/*enable gl5302 power for wifi*/
	//regulator_enable(wifi12_regulator);
	regulator_set_voltage(wifi33_regulator, 3300000, 3300000);
	return regulator_enable(wifi33_regulator);
//	return 0;
}

static int usb_wifi_free_power(void)
{
	if (wifi_power_off)
		return 0;

	if (usb_gpio_vbus_pin < 0)
		goto exit_use_wifi33;
	if (usb_gpio_vbus_pin == 0) {
		pr_info("wifi power not enabled!\n");
		return 0;
	}
	pr_info("wifi power off\n");
	gpio_set_value_cansleep(usb_gpio_vbus_pin, 0);

	wifi_power_off = true;

	return 0;

exit_use_wifi33:
	/* disable gl5302 power for wifi */
	//regulator_disable(wifi12_regulator);
	regulator_disable(wifi33_regulator);

	if (wifi33_regulator != NULL)
		regulator_put(wifi33_regulator);

	wifi_power_off = true;

	return 0;
}

/* Refactor: get device info from dts instead of static configurations */
int aotg1_device_init(int power_only)
{
	int ret;
	int i;
	struct platform_device *tmp;
	struct device_node *node;

	if (power_only) {
		usb_wifi_get_power();
		return 0;
	}

	mutex_lock(&aotg_onoff_mutex);
	if (aotg1_initialized > 0) {
		aotg1_initialized++;
		aotg_last_jiffies = jiffies;
		pr_info("aotg1 initialized allready! cnt: %d\n", aotg1_initialized);
		mutex_unlock(&aotg_onoff_mutex);
		return 0;
	}
	aotg1_initialized = 1;

	/* when excuting the platform_device_unregister(), waits 1s to be done */
	i = 0;
	while (aotg_dev1 && i < 80) {
		msleep(10);
		i++;
	}
	if (aotg_dev1) {
		dev_err(&aotg_dev1->dev, "aotg_dev1 exists already\n");
		mutex_unlock(&aotg_onoff_mutex);
		return 0;
	}

	if (jiffies_to_msecs(jiffies - aotg_last_jiffies) < 500) {
		pr_info("aotg1 close not long before, wait a moment!\n");
		msleep(300);
	}
	aotg_last_jiffies = jiffies;

	//wake_lock(&aotg_dev1_wakelock);

	usb_wifi_get_power();

	node = of_find_matching_node(NULL, &aotg_hcd_of_match[1]);
	if (!node) {
		pr_err("%s can't find device node.\n", __func__);
		return -ENODEV;
	}

	tmp = of_find_device_by_node(node);
	if (!tmp) {
		pr_err("%s can't find platform device\n", __func__);
		return -ENODEV;
	}

	pr_err("tmp->num_resources: %d\n", tmp->num_resources);

	/*
	 * We have to alloc platform_device by ourself instead of using
	 * the one created by device model which has null name.
	 * Also, we can't bind aotg_hcd device and driver at boot time.
	 */
	aotg_dev1 = platform_device_alloc("aotg_hcd", 1);
	if (!aotg_dev1) {
		pr_err("couldn't allocate aotg_hcd device\n");
		return -ENOMEM;
	}

	aotg_dev1->dev.of_node = of_node_get(node);

	// FIXME: need to set dma_mask & coherent_dma_mask?

	ret = platform_device_add_resources(aotg_dev1, tmp->resource,
		tmp->num_resources);
	if (ret) {
		dev_err(&aotg_dev1->dev, "couldn't add resources to aotg_hcd\n");
		goto err;
	}

	ret = platform_device_add(aotg_dev1);
	if (ret) {
		dev_err(&aotg_dev1->dev, "failed to register aotg_hcd\n");
		goto err;
	}

	/* delay for power on stable, if no delay, net device
	 * will not be registered after insmod 8192cu.ko
	 */
	msleep(100);
	mutex_unlock(&aotg_onoff_mutex);

	return 0;

err:
	platform_device_put(aotg_dev1);
	aotg_dev1 = NULL;
	//wake_unlock(&aotg_dev1_wakelock);
	mutex_unlock(&aotg_onoff_mutex);

	return ret;
}

void aotg1_device_exit(int power_only)
{
	if (power_only) {
		usb_wifi_free_power();
		return;
	}
	mutex_lock(&aotg_onoff_mutex);
	if (!aotg1_initialized) {
		pr_info("aotg1 exit allready!\n");
		mutex_unlock(&aotg_onoff_mutex);
		return;
	}

	aotg1_initialized--;
	if (aotg1_initialized > 0) {
		aotg_last_jiffies = jiffies;
		pr_info("aotg1_exit cnt:%d\n", aotg1_initialized);
		mutex_unlock(&aotg_onoff_mutex);
		return;
	} else
		aotg1_initialized = 0;

	if (jiffies_to_msecs(jiffies - aotg_last_jiffies) < 500) {
		pr_info("aotg1 opened not long before, wait a moment!\n");
		msleep(400);
	}
	aotg_last_jiffies = jiffies;

	if (aotg_dev1 != NULL) {
		of_node_put(aotg_dev1->dev.of_node);
		platform_device_unregister(aotg_dev1);
		aotg_dev1 = NULL;
	}
	msleep(100);
	usb_wifi_free_power();
	mutex_unlock(&aotg_onoff_mutex);

	return;
}

void aotg1_device_mod_init(void)
{
	int ret;
	struct device_node *fdt_node;
	const __be32 *prop;

	fdt_node = of_find_compatible_node(NULL, NULL,
		"actions,ats3605-usb2.0-1");
	if (fdt_node == NULL) {
		pr_warn("%s couldn't find device node!\n", __func__);
		return;
	}

	prop = of_get_property(fdt_node, "power_en_gpio", NULL);
	if (prop == NULL) {
		pr_warn("%s couldn't find power_en_gpio!\n", __func__);
		prop = of_get_property(fdt_node, "wifi_power_en_gpio", NULL);
		if (prop == NULL) {
			pr_warn("%s couldn't find wifi_power_en_gpio!\n", __func__);
			prop = of_get_property(fdt_node, "wifi_gpio", NULL);
			if (prop == NULL) {
				pr_warn("%s couldn't find wifi_gpio!\n", __func__);
				return;
			} else
				usb_gpio_vbus_pin = be32_to_cpup(prop);
		} else
			usb_gpio_vbus_pin = be32_to_cpup(prop);
	} else
		usb_gpio_vbus_pin = be32_to_cpup(prop);

	pr_info("%s usb_gpio_vbus_pin: %d\n", __func__, usb_gpio_vbus_pin);

	ret = gpio_request(usb_gpio_vbus_pin, "aotg1 vbus");
	if (ret < 0) {
		usb_gpio_vbus_pin = 0;
		pr_err("%s usb gpio vbus request err!\n", __func__);
		return;
	}

	/* disable vbus first */
	gpio_direction_output(usb_gpio_vbus_pin, 0);
	gpio_set_value_cansleep(usb_gpio_vbus_pin, 0);

	return;
}


void aotg1_device_mod_exit(void)
{
	if (usb_gpio_vbus_pin < 0)
		return;
	if (usb_gpio_vbus_pin == 0)
		return;
	gpio_set_value_cansleep(usb_gpio_vbus_pin, 0);
	gpio_free(usb_gpio_vbus_pin);

	return;
}
