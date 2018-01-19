#ifndef  __USB_AOTG_HUB_H__
#define  __USB_AOTG_HUB_H__

typedef void (* aotg_hub_symbol_func_t)(int);

extern void aotg_force_init_hub(int port_num);
extern void aotg_force_exit_hub(int port_num);

extern void aotg_hub_notify_enter(int state);
extern void aotg_hub_notify_exit(int state);

extern int ahcd_hub_init(void);
extern int ahcd_hub_exit(void);

#endif /* __USB_AOTG_HUB_H__ */
