/*
 * ION driver for Actions OWL SoC family
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/of.h>

#include "../ion.h"
#include "../ion_priv.h"
#include "../../uapi/ats3605_ion.h"

#define OWL_ION_HEAP_NUM	3
static struct ion_platform_heap owl_ion_heaps[OWL_ION_HEAP_NUM] = {
	{
		.type = ION_HEAP_TYPE_CARVEOUT,
		.id = ION_HEAP_ID_FB,
		.name = "ion_fb",
		.base = 0,
		.size = 0,
	},

	{
#ifdef CONFIG_CMA
		.type = ION_HEAP_TYPE_DMA,
#else
		.type = ION_HEAP_TYPE_CARVEOUT,
#endif
		.id = ION_HEAP_ID_PMEM,
		.name = "ion_pmem",
		.base = 0,
		.size = 0,
	},

	{
		.type = ION_HEAP_TYPE_SYSTEM,
		.id = ION_HEAP_ID_SYSTEM,
		.name = "ion_system",
	},
};

static struct ion_platform_data owl_ion_data = {
	.nr = OWL_ION_HEAP_NUM,
	.heaps = owl_ion_heaps,
};

struct ion_device *owl_ion_device;
EXPORT_SYMBOL(owl_ion_device);

static int num_heaps;
static struct ion_heap **heaps;

static int owl_ion_get_phys(struct ion_client *client,
					unsigned int cmd,
					unsigned long arg)
{
	struct owl_ion_phys_data data;
	struct owl_ion_phys_data *user_data =
				(struct owl_ion_phys_data *)arg;
	struct ion_handle *handle;
	struct ion_buffer *buffer;
	int ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	handle = ion_handle_get_by_id(client, data.handle);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	buffer = ion_handle_buffer(handle);
	ret = ion_phys(client, handle, &data.phys_addr, &data.size);
	ion_handle_put(handle);
	if (ret < 0)
		return ret;

	if (copy_to_user(user_data, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}
static struct device  dummy_dev;
static long owl_ion_ioctl(struct ion_client *client,
				   unsigned int cmd,
				   unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case OWL_ION_GET_PHY:
		ret = owl_ion_get_phys(client, cmd, arg);
		break;
	default:
		WARN(1, "Unknown custom ioctl\n");
		return -EINVAL;
	}
	return ret;
}

extern int cma_activate_area_already_reaserved(struct device *dev,
				phys_addr_t size, phys_addr_t base);

static int owl_ion_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	u32 ion_heap_fb[2] = {0, 0};
	u32 ion_heap_pmem[2] = {0, 0};
	u32 ion_reserved[2] = {0, 0};
	int err;
	int i;
	printk("owl_ion_probe\n");
	num_heaps = owl_ion_data.nr;
	heaps = kzalloc(sizeof(struct ion_heap *) * num_heaps, GFP_KERNEL);

	owl_ion_device = ion_device_create(owl_ion_ioctl);
	if (IS_ERR_OR_NULL(owl_ion_device)) {
		kfree(heaps);
		return PTR_ERR(owl_ion_device);
	}

	if (np) {
		of_property_read_u32_array(np, "actions,ion_heap_fb",
			ion_heap_fb, 2);

		pr_info("[OWL] ion_heap_fb: base 0x%x, size 0x%x\n",
			ion_heap_fb[0], ion_heap_fb[1]);

		of_property_read_u32_array(np, "actions,ion_heap_pmem",
			ion_heap_pmem, 2);

		pr_info("[OWL] ion_heap_pmem: base 0x%x, size 0x%x\n",
			ion_heap_pmem[0], ion_heap_pmem[1]);

		of_property_read_u32_array(np, "actions,ion_reserved",
			ion_reserved, 2);


		pr_info("[OWL] ion_heap_reserved: base 0x%x, size 0x%x\n",
			ion_reserved[0], ion_reserved[1]);
	}

	if (ion_reserved[1]) {
		cma_activate_area_already_reaserved(&dummy_dev,
			ion_reserved[1], ion_reserved[0]);
	}
	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &owl_ion_data.heaps[i];

		if (heap_data->id == ION_HEAP_ID_FB) {
			if (ion_heap_fb[1] == 0)
				continue;
			heap_data->base = ion_heap_fb[0];
			heap_data->size = ion_heap_fb[1];
		} else if (heap_data->id == ION_HEAP_ID_PMEM) {
			heap_data->base = ion_heap_pmem[0];
			heap_data->size = ion_heap_pmem[1];

#ifdef CONFIG_CMA
			if (heap_data->type == ION_HEAP_TYPE_DMA) {
				cma_activate_area_already_reaserved(&pdev->dev,
					ion_heap_pmem[1], ion_heap_pmem[0]);
				heap_data->priv = &pdev->dev;
			}
#endif
		}

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(owl_ion_device, heaps[i]);
	}

	platform_set_drvdata(pdev, owl_ion_device);
	return 0;

err:
	for (i = 0; i < num_heaps; i++) {
		if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}
	kfree(heaps);
	return err;
}

static int owl_ion_remove(struct platform_device *pdev)
{
	struct ion_device *owl_ion_device = platform_get_drvdata(pdev);
	int i;

	ion_device_destroy(owl_ion_device);
	for (i = 0; i < num_heaps; i++)
		ion_heap_destroy(heaps[i]);
	kfree(heaps);
	return 0;
}

static const struct of_device_id owl_ion_of_match[] = {
	{.compatible = "actions,ats3605-ion", },

	{ },
};

static struct platform_driver owl_ion_driver = {
	.probe = owl_ion_probe,
	.remove = owl_ion_remove,
	.driver = {
		.name = "ion-owl",
		.of_match_table = owl_ion_of_match
	}
};

static int __init owl_ion_init(void)
{
	printk("owl_ion_init\n");
	return platform_driver_register(&owl_ion_driver);
}

static void __exit owl_ion_exit(void)
{
	platform_driver_unregister(&owl_ion_driver);
}

subsys_initcall(owl_ion_init);
module_exit(owl_ion_exit);
