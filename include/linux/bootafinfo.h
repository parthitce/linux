/*
 * arch/arm/mach-leopard/include/mach/bootafinfo.h
 *
 * Boot AFInfo interface
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_ARCH_BOOTAFINFO_H
#define __ASM_ARCH_BOOTAFINFO_H

#define OWL_NORMAL   0
#define OWL_PRODUCE    1
#define OWL_MNICHARGE 2
#define OWL_RECOVERY  3
/*
 * get boot afinfo 
 */
struct chipid_data {
	char gpu_sensor[2];
	char cpu_sensor[2];
};
extern unsigned char *owl_get_boot_afinfo(void);
extern int owl_get_boot_afinfo_len(void);
extern int owl_get_boot_mode(void);
extern  void owl_afi_get_sensor(struct chipid_data *data);


#endif /* __ASM_ARCH_BOOTAFINFO_H ss*/
