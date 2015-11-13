#ifndef __ISP_PWD_OWL_H_
#define __ISP_PWD_OWL_H_

void attach_sensor_pwd_info(struct device *dev,
	struct sensor_pwd_info *pi, int host_id);
void detach_sensor_pwd_info(struct device *dev,
	struct sensor_pwd_info *pi, int host_id);
void owl_isp_reset(struct device *dev, int host_id);
int owl_isp_power_on(int channel, int rear, int host_id);
int owl_isp_power_off(int channel, int rear, int host_id);
int parse_config_info(struct soc_camera_link *link,
	struct dts_sensor_config *dsc, const char *name);

#endif
