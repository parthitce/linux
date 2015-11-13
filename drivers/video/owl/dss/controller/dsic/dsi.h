#ifndef __OWL_DSI_H
#define __OWL_DSI_H

#include <linux/platform_device.h>
#include <video/owl_dss.h>

#define DSI_DEBUG_ENABLE
#define SUBSYS_NAME		"owl_dsi: "

struct dsi_data;

void dsihw_fs_dump_regs(struct device *dev);
void test_fs_dsi(struct device *dev);
void test_fs_longcmd(struct device *dev);

void dsihw_send_long_packet(struct dsi_data *dsi, int data_type, int word_cnt,
				int *send_data, int trans_mode);
void dsihw_send_short_packet(struct dsi_data *dsi,int data_type,
				int sp_data, int trans_mode);
/*
void owl_dsi_select_video_timings(struct owl_dss_device *dssdev, u32 num,
                           struct owl_video_timings *timings);
*/
bool dsihw_is_enable(struct platform_device *pdev);

/*dsi_sysfs.c*/
int owl_dsi_create_sysfs(struct device *dev);
void owl_dsi_remove_sysfs(struct device *dev);

/*dsi_cmd.c*/
void send_cmd(struct dsi_data *dsi);
void send_cmd_test(struct dsi_data *dsi);

#ifdef DSI_DEBUG_ENABLE
	extern int owl_dsi_debug;
	#define DSIDBG(format, ...) \
		do { \
			if (owl_dsi_debug > 1) \
				printk(KERN_DEBUG SUBSYS_NAME format, ## __VA_ARGS__); \
	        } while (0)

	#define DSIINFO(format, ...) \
		do { \
			if (owl_dsi_debug > 0) \
				printk(KERN_INFO SUBSYS_NAME format, ## __VA_ARGS__); \
	        } while (0)

#else
	#define DSIDBG(format, ...)
	#define DSIINFO(format, ...)
#endif

#define DSIERR(format, ...) \
	printk(KERN_ERR SUBSYS_NAME "error!" format, ## __VA_ARGS__);



#endif 
