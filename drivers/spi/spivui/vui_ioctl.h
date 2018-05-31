#ifndef __VUI_IOCTL_H__
#define __VUI_IOCTL_H__

#include <linux/ioctl.h>

/* 定义设备类型 */
#define IOC_MAGIC  'c'

#define SET_VUI_SAMPLE_RATE _IOW(IOC_MAGIC, 0, int)
#define SET_VUI_CH          _IOW(IOC_MAGIC, 1, int)
#define SET_VUI_DATAWIDTH   _IOW(IOC_MAGIC, 2, int)

#define START_VUI_DMA       _IO(IOC_MAGIC, 3)
#define STOP_VUI_DMA        _IO(IOC_MAGIC, 4)


#define IOC_MAXNR  6

#endif /* __VUI_IOCTL_H__ */
