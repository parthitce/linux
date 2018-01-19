/*
 * for Actions AOTG
 *
 */

#ifndef  __AOTG_REGS_H__
#define  __AOTG_REGS_H__

#define VBUS_THRESHOLD_3600		3600
#define VBUS_THRESHOLD_3800		3800


#define	DWC3_P0_CTL			0xB0158080

#define USB3_REGISTER_BASE          0xB0400000

#define DWC3_DCFG                   0xc700

#define DWC3_ACTIONS_REGS_START     (0xcd00)
#define DWC3_ACTIONS_REGS_END       (0xcd54)

#define DWC3_CMU_DEBUG_LDO     (0xcd20)
#define DWC3_CMU_PLL2_BISTDEBUG    (0xcd44)
#define DWC3_USB2_P0_VDCTL     (0xcd48)
#define DWC3_BACKDOOR          (0xcd4C)
#define DWC3_GCTL               (0xc110)


#define USB3_MOD_RST           (1 << 14)
#define CMU_BIAS_EN            (1 << 20)


#define BIST_QINIT(n)          ((n) << 24)
#define EYE_HEIGHT(n)          ((n) << 20)
#define PLL2_LOCK              (1 << 15)
#define PLL2_RS(n)             ((n) << 12)
#define PLL2_ICP(n)            ((n) << 10)
#define CMU_SEL_PREDIV         (1 << 9)
#define CMU_DIVX2              (1 << 8)
#define PLL2_DIV(n)            ((n) << 3)
#define PLL2_POSTDIV(n)        ((n) << 1)
#define PLL2_PU                (1 << 0)

#define DWC3_GCTL_CORESOFTRESET		(1 << 11)


#define DWC3_GCTL_PRTCAP(n)       (((n) & (3 << 12)) >> 12)
#define DWC3_GCTL_PRTCAPDIR(n)    ((n) << 12)
#define DWC3_GCTL_PRTCAP_HOST     1
#define DWC3_GCTL_PRTCAP_DEVICE   2
#define DWC3_GCTL_PRTCAP_OTG	    3

#define USB3_P0_CTL_VBUS_P0       5
#define USB3_P0_CTL_ID_P0         11
#define USB3_P0_CTL_DPPUEN_P0     14
#define USB3_P0_CTL_DMPUEN_P0     15
#define USB3_P0_CTL_DPPDDIS_P0    12
#define USB3_P0_CTL_DMPDDIS_P0    13
#define USB3_P0_CTL_SOFTIDEN_P0   8
#define USB3_P0_CTL_SOFTID_P0     9
#define USB3_P0_CTL_SOFTVBUSEN_P0 6
#define USB3_P0_CTL_SOFTVBUS_P0   7
#define USB3_P0_CTL_PLLLDOEN      28
#define USB3_P0_CTL_LDOVREFSEL_SHIFT  29


#define USB3_P0_CTL_LS_P0_SHIFT   3
#define USB3_P0_CTL_LS_P0_MASK    (0x3<<3)


//VBUS detection threshold control. reg USB3_P0_CTL [2:0]
#define VBUS_DET_THRESHOLD_LEVEL0     0x00    //100 4.75v
#define VBUS_DET_THRESHOLD_LEVEL1     0x01    //4.45v
#define VBUS_DET_THRESHOLD_LEVEL2     0x02    //4.00v
#define VBUS_DET_THRESHOLD_LEVEL3     0x03    //3.65v
#define VBUS_DET_THRESHOLD_LEVEL4     0x04    //3.00v

#define USB3_P0_CTL_VBUS_THRESHOLD		0x04


/*usb2.0
*be careful! it is different between few IC
*/

#define	USB2_0ECS			0xB0158090
#define	USB2_1ECS			0xB0158094

#define USB2_MOD_RST           (1 << 3)



#define	USB2_ECS_VBUS_P0		10
#define	USB2_ECS_ID_P0			12
#define USB2_ECS_LS_P0_SHIFT	8
#define USB2_ECS_LS_P0_MASK		(0x3<<8)
#define USB2_ECS_DPPUEN_P0     3
#define USB2_ECS_DMPUEN_P0     2
#define USB2_ECS_DMPDDIS_P0    1
#define USB2_ECS_DPPDDIS_P0    0
#define USB2_ECS_SOFTIDEN_P0   26
#define USB2_ECS_SOFTID_P0     27
#define USB2_ECS_SOFTVBUSEN_P0 24
#define USB2_ECS_SOFTVBUS_P0   25

#define USB2_ECS_VBUS_THRESHOLD		(0x04 << 4)
#define USB2_ECS_VBUS_THRESHOLD_MASK (7 << 4)
#endif  /* __AOTG_REGS_H__ */
