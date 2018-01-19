#ifndef __VCE_REG_H__
#define __VCE_REG_H__

#define IC_TYPE_ATM7021 1
//#define IC_TYPE_ATM7039 1


#ifdef IC_TYPE_ATM7021
#define IC_TYPE_ATM7039 1 //atm7021ÔÚatm7039ÉÏÐÞ¸Ä

//#define VCE_BASE_ADDR    0xB0278000         //FIX 
#define VCE_ID                           (0) //0X78323634
#define VCE_OUTSTANDING  (8)
#define VCE_STATUS        (4) //swreg1
#define VCE_CFG              (16) //swreg4
#define VCE_PARAM0      (20)//swreg5
#define VCE_PARAM1      (24)
#define VCE_STRM             (28)
#define VCE_STRM_ADDR   (32)
#define VCE_YADDR             (36)
#define VCE_LIST0_ADDR  (40)
#define VCE_LIST1_ADDR  (44)
#define VCE_ME_PARAM    (48)//swreg12
#define VCE_SWIN                 (52)
#define VCE_SCALE_OUT    (56)
#define VCE_RECT                 (60)
#define VCE_RC_PARAM1   (64)
#define VCE_RC_PARAM2   (68)
#define VCE_RC_PARAM3   (72)
#define VCE_RC_HDBITS    (76)
#define VCE_TS_INFO          (80)
#define VCE_TS_HEADER   (84)
#define VCE_TS_BLUHD     (88)
#define VCE_REF_DHIT       (92)
#define VCE_REF_DMISS    (96)

#define UPS_YAS             (120)
#define UPS_CBCRAS     (124)
#define UPS_CRAS          (128)
#define UPS_IFORMAT   (132)
#define UPS_RATIO         (140)
#define UPS_IFS               (144)

#else

#ifdef  IC_TYPE_ATM7039
#define VCE_BASE_ADDR    0xB0288000         //FIX 
#else
#define VCE_BASE_ADDR    0xB0278000         //FIX 
#endif

#define VCE_ID          (VCE_BASE_ADDR + 0) //0X78323634
#define VCE_STATUS      (VCE_BASE_ADDR + 4) //swreg1
#define VCE_CFG         (VCE_BASE_ADDR + 16) //swreg4
#define VCE_PARAM0      (VCE_BASE_ADDR + 20)//swreg5
#define VCE_PARAM1      (VCE_BASE_ADDR + 24)
#define VCE_STRM        (VCE_BASE_ADDR + 28)
#define VCE_STRM_ADDR   (VCE_BASE_ADDR + 32)
#define VCE_YADDR             (VCE_BASE_ADDR + 36)
#define VCE_LIST0_ADDR  (VCE_BASE_ADDR + 40)
#define VCE_LIST1_ADDR  (VCE_BASE_ADDR + 44)
#define VCE_ME_PARAM    (VCE_BASE_ADDR + 48)//swreg12
#define VCE_SWIN                 (VCE_BASE_ADDR + 52)
#define VCE_SCALE_OUT    (VCE_BASE_ADDR + 56)
#define VCE_RECT                 (VCE_BASE_ADDR + 60)
#define VCE_RC_PARAM1   (VCE_BASE_ADDR + 64)
#define VCE_RC_PARAM2   (VCE_BASE_ADDR + 68)
#define VCE_RC_PARAM3   (VCE_BASE_ADDR + 72)
#define VCE_RC_HDBITS    (VCE_BASE_ADDR + 76)
#define VCE_TS_INFO          (VCE_BASE_ADDR + 80)
#define VCE_TS_HEADER   (VCE_BASE_ADDR + 84)
#define VCE_TS_BLUHD     (VCE_BASE_ADDR + 88)
#ifdef IC_TYPE_ATM7039
#define VCE_REF_DHIT       (VCE_BASE_ADDR + 92)
#define VCE_REF_DMISS    (VCE_BASE_ADDR + 96)
#endif

#define UPS_BASE        (VCE_BASE_ADDR + 0xc0)
#define UPS_CTL         (UPS_BASE + 0)//swreg48
#define UPS_IFS         (UPS_BASE + 4)
#define UPS_STR         (UPS_BASE + 8)
#define UPS_OFS         (UPS_BASE + 12)
#define UPS_RATH        (UPS_BASE + 16)
#define UPS_RATV        (UPS_BASE + 20)
#define UPS_YAS         (UPS_BASE + 24)
#define UPS_CBCRAS      (UPS_BASE + 28)
#define UPS_CRAS        (UPS_BASE + 32)
#define UPS_BCT         (UPS_BASE + 36)
#define UPS_DAB         (UPS_BASE + 40)
#define UPS_DWH         (UPS_BASE + 44)
#define UPS_SAB0        (UPS_BASE + 48)
#define UPS_SAB1        (UPS_BASE + 52)
#ifdef IC_TYPE_ATM7039
#define UPS_RGB32_SR        (UPS_BASE+56)  //swreg62
#define UPS_BLEND_W        (UPS_BASE+60)  //swreg63
#endif

#endif

#if 0
static int reg_write(unsigned int regaddr,unsigned int regval)
{
#if 1
  *(volatile unsigned int *)regaddr = regval;
#else
  act_writel(regval,regaddr);
#endif
  return 0;
}
static int reg_read(unsigned int regaddr,unsigned int *regval)
{
#if 1
  *regval = *(volatile unsigned int *)regaddr;
#else
  *regval = act_readl(regaddr);
#endif
  return 0;
}
#endif
#endif
