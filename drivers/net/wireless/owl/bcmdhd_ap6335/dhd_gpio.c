
#ifdef CUSTOMER_HW
#include <osl.h>
#include <dngl_stats.h>
#include <dhd.h>

#ifdef CONFIG_MACH_ODROID_4210
/*
#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/sdhci.h>
#include <plat/devs.h>	// modifed plat-samsung/dev-hsmmcX.c EXPORT_SYMBOL(s3c_device_hsmmcx) added
#define	sdmmc_channel	s3c_device_hsmmc0
*/

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_PLATFORM_OWL
#include <linux/gpio.h>
#include "../../../../mmc/host/owl_wlan_plat_data.h"
extern struct wlan_plat_data *owl_get_wlan_plat_data(void);
#endif

struct resource dhd_wlan_resources = {0};
struct wifi_platform_data dhd_wlan_control = {0};

#ifdef CUSTOMER_OOB
uint bcm_wlan_get_oob_irq(void)
{
	uint host_oob_irq = 0;

#ifdef CONFIG_MACH_ODROID_4210
	struct device_node *np = NULL;
	uint wifi_wakeup_host_gpios = 0;

	np = of_find_compatible_node(NULL, NULL, "wifi,bt,power,ctl");
	if (NULL == np) {
		printk("%s \"wifi,bt,power,ctl\" node found in dts\n", __FUNCTION__);
	} else {
		if (of_find_property(np, "wifi_wakeup_host_gpios", NULL)) {
			wifi_wakeup_host_gpios = of_get_named_gpio(np, "wifi_wakeup_host_gpios", 0);

			if (gpio_is_valid(wifi_wakeup_host_gpios)) {
				printk("GPIO(WL_HOST_WAKE) wifi_wakeup_host_gpios = %d\n", wifi_wakeup_host_gpios);
				host_oob_irq = gpio_to_irq(wifi_wakeup_host_gpios);
				gpio_direction_input(wifi_wakeup_host_gpios);
			} else {
				printk("%s gpio for wifi_wakeup_host_gpios invalid\n", __FUNCTION__);
			}
		}
	}
/*
	printk("GPIO(WL_HOST_WAKE) = EXYNOS4_GPX0(7) = %d\n", EXYNOS4_GPX0(7));
	host_oob_irq = gpio_to_irq(EXYNOS4_GPX0(7));
	gpio_direction_input(EXYNOS4_GPX0(7));
*/
  printk("host_oob_irq: %d \r\n", host_oob_irq);
#endif
	
	return host_oob_irq;
}

uint bcm_wlan_get_oob_irq_flags(void)
{
	uint host_oob_irq_flags = 0;

#ifdef CONFIG_MACH_ODROID_4210
#ifdef HW_OOB
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE;
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE | IORESOURCE_IRQ_SHAREABLE;
#endif
#endif
	printk("host_oob_irq_flags=%d\n", host_oob_irq_flags);

	return host_oob_irq_flags;
}
#endif

int bcm_wlan_set_power(bool on)
{
	int err = 0;
#ifdef CONFIG_PLATFORM_OWL
	struct wlan_plat_data *pdata = owl_get_wlan_plat_data();
#endif

	if (on) {
		printk("======== PULL WL_REG_ON HIGH! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		gpio_set_value(pdata->wifi_en_gpios, 1);
#endif
		/* Lets customer power to get stable */
		mdelay(100);
	} else {
		printk("======== PULL WL_REG_ON LOW! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		gpio_set_value(pdata->wifi_en_gpios, 0);
#endif
	}

	return err;
}

int bcm_wlan_set_carddetect(bool present)
{
	int err = 0;
#ifdef CONFIG_PLATFORM_OWL
	struct wlan_plat_data *pdata = owl_get_wlan_plat_data();

	if (!(pdata && pdata->set_carddetect)) {
		return -1;
	}
#endif

	if (present) {
		printk("======== Card detection to detect SDIO card! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		err = pdata->set_carddetect(1);
#endif
	} else {
		printk("======== Card detection to remove SDIO card! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		err = pdata->set_carddetect(0);
#endif
	}

	return err;
}

int bcm_wlan_get_mac_address(unsigned char *buf)
{
	int err = 0;
	
	printk("======== %s ========\n", __FUNCTION__);
#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return err;
}
#ifdef CONFIG_DHD_USE_STATIC_BUF
extern void *bcmdhd_mem_prealloc(int section, unsigned long size);
void* bcm_wlan_prealloc(int section, unsigned long size)
{
	void *alloc_ptr = NULL;
	alloc_ptr = bcmdhd_mem_prealloc(section, size);
	if (alloc_ptr) {
		printk("success alloc section %d, size %ld\n", section, size);
		if (size != 0L)
			bzero(alloc_ptr, size);
		return alloc_ptr;
	}
	printk("can't alloc section %d\n", section);
	return NULL;
}
#endif

int bcm_wlan_set_plat_data(void) {
	printk("======== %s ========\n", __FUNCTION__);
	dhd_wlan_control.set_power = bcm_wlan_set_power;
	dhd_wlan_control.set_carddetect = bcm_wlan_set_carddetect;
	dhd_wlan_control.get_mac_addr = bcm_wlan_get_mac_address;
#ifdef CONFIG_DHD_USE_STATIC_BUF
	dhd_wlan_control.mem_prealloc = bcm_wlan_prealloc;
#endif
	return 0;
}

#endif /* CUSTOMER_HW */
