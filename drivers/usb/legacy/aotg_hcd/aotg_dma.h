#ifndef  __LINUX_USB_AOTG_DMA_H__
#define  __LINUX_USB_AOTG_DMA_H__

typedef void (*aotg_hcd_dma_func)(int, void *);

void aotg_hcd_dma_sync(void);
int aotg_hcd_dma_reset_2(int dma_nr);
int aotg_hcd_dma_enable_irq_2(int dma_nr);
int aotg_hcd_dma_enable_irq(int dma_nr, int enable);
int aotg_hcd_dma_is_irq_2(int dma_nr);

int aotg_hcd_dma_clear_pend(int dma_nr);
int aotg_hcd_dma_clear_pend_2(int dma_nr);
unsigned int aotg_hcd_dma_get_cmd(unsigned int dma_nr);
unsigned int aotg_hcd_dma_get_cnt(int dma_nr);
unsigned int aotg_hcd_dma_get_memaddr(int dma_nr);
int aotg_hcd_dma_stop(int dma_nr);
unsigned int aotg_hcd_dma_get_remain(int dma_nr);

int aotg_hcd_dma_set_mode(int dma_nr, unsigned char ep_select);
int aotg_hcd_dma_set_memaddr(int dma_nr, unsigned long addr);
int aotg_hcd_dma_set_cnt(int dma_nr, unsigned long dma_length);
int aotg_hcd_dma_start(int dma_nr);

/* discard functions. */
//int disable_dma_htcirq(int dma_nr);
//int clear_dma_htcirq_pend(int dma_nr)
//int pause_dma_for_usb(int dma_nr);
//int get_dma_mode(int dma_nr);

int aotg_hcd_dma_request_2(aotg_hcd_dma_func handler, void *hcd);
void aotg_hcd_dma_free_2(int dma_nr);

#endif /* __LINUX_USB_AOTG_DMA_H__ */