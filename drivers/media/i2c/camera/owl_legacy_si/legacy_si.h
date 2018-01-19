/*
* Automatically generated register definition: don't edit
* GL5202E Spec Version_V2.00
* Sat 03-13-2014  10:22:15
*/
#ifndef __LEGACY_SI_REG_H___
#define __LEGACY_SI_REG_H___

/*--------------Bits Location------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define    MODULE_BASE                          0xB0210000
#define    SI_SYNC_PRIORITY	                    (MODULE_BASE+0x0)
#define    SI_ROW_RANGE			                (MODULE_BASE+0x4)
#define    SI_COL_RANGE			                (MODULE_BASE+0x8)
#define    SI_FORMAT			                (MODULE_BASE+0xC)
#define    SI_ADDR0			                    (MODULE_BASE+0x10)
#define    SI_ADDR1			                    (MODULE_BASE+0x14)
#define    SI_CON_STAT			                (MODULE_BASE+0x18)

/*--------------Bits Location------------------------------------------*/
/*--------------CMU-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define     CMU_BASE                                                          0xB0150000
#define     CMU_COREPLL                                                       (CMU_BASE+0x0000)
#define     CMU_DEVPLL                                                        (CMU_BASE+0x0004)
#define     CMU_DDRPLL                                                        (CMU_BASE+0x0008)
#define     CMU_NANDPLL                                                       (CMU_BASE+0x000C)
#define     CMU_DISPLAYPLL                                                    (CMU_BASE+0x0010)
#define     CMU_BUSCLK                                                        (CMU_BASE+0x001C)
#define     CMU_SENSORCLK                                                     (CMU_BASE+0x0020)
#define     CMU_LCDCLK                                                        (CMU_BASE+0x0024)
#define     CMU_DSICLK                                                        (CMU_BASE+0x0028)
#define     CMU_SICLK                                                         (CMU_BASE+0x0034)
#define     CMU_DEVCLKEN0                                                     (CMU_BASE+0x00A0)
#define     CMU_DEVCLKEN1                                                     (CMU_BASE+0x00A4)
#define     CMU_DEVRST0                                                       (CMU_BASE+0x00A8)
#define     CMU_DEVRST1                                                       (CMU_BASE+0x00AC)

/*--------------Bits Location------------------------------------------*/
/*--------------GPIO_MFP_PWM-------------------------------------------*/
/*--------------Register Address---------------------------------------*/
#define     GPIO_MFP_PWM_BASE                                                 0xB01C0000
#define     GPIO_COUTEN                                                       (GPIO_MFP_PWM_BASE+0x0018)
#define     GPIO_CINEN                                                        (GPIO_MFP_PWM_BASE+0x001C)
#define     GPIO_CDAT                                                         (GPIO_MFP_PWM_BASE+0x0020)
#define     MFP_CTL0                                                          (GPIO_MFP_PWM_BASE+0x0040)
#define     MFP_CTL1                                                          (GPIO_MFP_PWM_BASE+0x0044)
#define     MFP_CTL2                                                          (GPIO_MFP_PWM_BASE+0x0048)
#define     MFP_CTL3                                                          (GPIO_MFP_PWM_BASE+0x004C)
#define     PAD_PULLCTL0                                                      (GPIO_MFP_PWM_BASE+0x0060)
#define     PAD_PULLCTL1                                                      (GPIO_MFP_PWM_BASE+0x0064)
#define     PAD_PULLCTL2                                                      (GPIO_MFP_PWM_BASE+0x0068)
#define     PAD_CTL                                                           (GPIO_MFP_PWM_BASE+0x0074)
#define     PAD_DRV0                                                          (GPIO_MFP_PWM_BASE+0x0080)
#define     PAD_DRV1                                                          (GPIO_MFP_PWM_BASE+0x0084)
#define     PAD_DRV2                                                          (GPIO_MFP_PWM_BASE+0x0088)

/*--------------Bits Location------------------------------------------*/
/*---------------------------------------------------------------------*/
#endif
