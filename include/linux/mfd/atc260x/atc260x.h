#ifndef __LINUX_MFD_ATC260X_H
#define __LINUX_MFD_ATC260X_H

#include <linux/regmap.h>
#include <linux/mfd/atc260x/registers_atc2603c.h>
#include <linux/mfd/atc260x/registers_atc2609a.h>

enum atc260x_variants {
	ATC2603A_ID = 0,
	ATC2603C_ID,
	ATC2609A_ID,
};

struct atc260x_dev {
	struct device			*dev;
	int				irq;
	unsigned long			irq_flags;
	struct regmap			*regmap;
	struct regmap_irq_chip_data	*regmap_irqc;
	long				variant;
	int                             nr_cells;
	const struct mfd_cell           *cells;
	const struct regmap_config	*regmap_cfg;
	const struct regmap_irq_chip	*regmap_irq_chip;
};
#endif
