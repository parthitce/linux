/*
 * common power management functions for camera sensors
 */
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include "owl_isp.h"

/*
for bisp's tow soc camera host
*/
static struct sensor_pwd_info *g_spinfo[2] = {NULL, NULL};

static inline struct sensor_pwd_info *to_spinfo(int host_id)
{
	return g_spinfo[!!host_id];
}

/* should be called before register host using soc_camera_host_register() */
void attach_sensor_pwd_info(struct device *dev,
	struct sensor_pwd_info *pi, int host_id)
{
	int id = !!host_id;

	if (g_spinfo[id])
		dev_err(dev, "already register it [host id : %d]\n", host_id);

	g_spinfo[id] = pi;
}
EXPORT_SYMBOL(attach_sensor_pwd_info);

void detach_sensor_pwd_info(struct device *dev,
	struct sensor_pwd_info *pi, int host_id)
{
	int id = !!host_id;

	if (pi != g_spinfo[id])
		dev_err(dev, "sensor pwd info don't match with host id[%d]\n",
		host_id);

	g_spinfo[id] = NULL;
}
EXPORT_SYMBOL(detach_sensor_pwd_info);

static int get_parent_node_id(struct device_node *node,
	const char *property, const char *stem)
{
	struct device_node *pnode;
	unsigned int value = -ENODEV;

	pnode = of_parse_phandle(node, property, 0);
	if (NULL == pnode) {
		pr_err("err: fail to get node[%s]\n", property);
		return value;
	}
	value = of_alias_get_id(pnode, stem);

	return value;
}

int parse_config_info(struct soc_camera_link *link,
	struct dts_sensor_config *dsc, const char *name)
{
	struct device_node *fdt_node;


	fdt_node = of_find_compatible_node(NULL, NULL, name);
	if (NULL == fdt_node) {
		pr_err("err: no sensor [%s]\n", name);
		goto fail;
	}
	dsc->dn = fdt_node;


	dsc->i2c_adapter = get_parent_node_id(fdt_node, "i2c_adapter", "i2c");
	if (dsc->i2c_adapter < 0) {
		pr_err("err: fail to get i2c adapter id\n");
		goto fail;
	}

	return 0;

fail:
	return -EINVAL;
}
EXPORT_SYMBOL(parse_config_info);

/* module function */
static int __init isp_pwd_owl_init(void)
{
	unsigned int ret = 0;
	pr_info("isp_pwd_owl_init...\n");
	return ret;
}
module_init(isp_pwd_owl_init);

static void  __exit isp_pwd_owl_exit(void)
{
	pr_info("isp_pwd_owl_exit...\n");
}
module_exit(isp_pwd_owl_exit);

MODULE_DESCRIPTION("isp_pwd_owl driver");
MODULE_AUTHOR("Actions-semi");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
