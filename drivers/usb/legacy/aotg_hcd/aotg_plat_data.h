#ifndef __AOTG_PLAT_DATA_H__
#define __AOTG_PLAT_DATA_H__

#include <linux/clk.h>

int aotg0_device_init(int power_only);
void aotg0_device_exit(int power_only);

int aotg1_device_init(int power_only);
void aotg1_device_exit(int power_only);

void aotg0_device_mod_init(void);
void aotg1_device_mod_init(void);

void aotg0_device_mod_exit(void);
void aotg1_device_mod_exit(void);
void aotg1_wakelock_init(int flag);

extern struct mutex aotg_onoff_mutex;

#endif
