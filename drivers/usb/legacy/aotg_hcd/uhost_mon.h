#ifndef __AOTG_UHOST_MON_H__
#define __AOTG_UHOST_MON_H__

void aotg_dev_plugout_msg(int id);

void aotg_uhost_mon_init(int aotg0_config, int aotg1_config);
void aotg_uhost_mon_exit(void);

extern unsigned int port0_plug_en;
extern unsigned int port1_plug_en;

#endif /* __AOTG_UHOST_MON_H__ */

