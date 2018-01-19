/*
 * NAU85L40.h  --  NAU85L40 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _NAU85L40_H
#define _NAU85L40_H

#include <sound/soc.h>

#define NAU85L40_BCLK  1

/*
 * Register values.
 */
#define NAU85L40_SW_RESET                         0x00
#define NAU85L40_POWER_MANAGEMENT                 0x01
#define NAU85L40_CLOCK_CTRL                       0x02
#define NAU85L40_CLOCK_SRC                        0x03
#define NAU85L40_FLL1                             0x04
#define NAU85L40_FLL2                             0x05
#define NAU85L40_FLL3                             0x06
#define NAU85L40_FLL4                             0x07
#define NAU85L40_FLL5                             0x08
#define NAU85L40_FLL6                             0x09
#define NAU85L40_FLL_VCO_RSV                      0x0A
#define NAU85L40_PCM_CTRL0                        0x10
#define NAU85L40_PCM_CTRL1                        0x11
#define NAU85L40_PCM_CTRL2                        0x12
#define NAU85L40_PCM_CTRL3                        0x13
#define NAU85L40_PCM_CTRL4                        0x14
#define NAU85L40_ALC_CONTROL_1                    0x20
#define NAU85L40_ALC_CONTROL_2                    0x21
#define NAU85L40_ALC_CONTROL_3                    0x22
#define NAU85L40_ALC_CONTROL_4                    0x23
#define NAU85L40_ALC_CONTROL_5                    0x24
#define NAU85L40_ALC_GAIN_CH12                    0x2D
#define NAU85L40_ALC_GAIN_CH34                    0x2E
#define NAU85L40_ALC_STATUS                       0x2F
#define NAU85L40_NOTCH_FIL1_CH1                   0x30
#define NAU85L40_NOTCH_FIL2_CH1                   0x31
#define NAU85L40_NOTCH_FIL1_CH2                   0x32
#define NAU85L40_NOTCH_FIL2_CH2                   0x33
#define NAU85L40_NOTCH_FIL1_CH3                   0x34
#define NAU85L40_NOTCH_FIL2_CH3                   0x35
#define NAU85L40_NOTCH_FIL1_CH4                   0x36
#define NAU85L40_NOTCH_FIL2_CH4                   0x37
#define NAU85L40_HPF_FILTER_CH12                  0x38
#define NAU85L40_HPF_FILTER_CH34                  0x39
#define NAU85L40_ADC_SAMPLE_RATE                  0x3A
#define NAU85L40_DIGITAL_GAIN_CH1                 0x40
#define NAU85L40_DIGITAL_GAIN_CH2                 0x41
#define NAU85L40_DIGITAL_GAIN_CH3                 0x42
#define NAU85L40_DIGITAL_GAIN_CH4                 0x43
#define NAU85L40_DIGITAL_MUX                      0x44
#define NAU85L40_P2P_CH1                          0x48
#define NAU85L40_P2P_CH2                          0x49
#define NAU85L40_P2P_CH3                          0x4A
#define NAU85L40_P2P_CH4                          0x4B
#define NAU85L40_PEAK_CH1                         0x4C
#define NAU85L40_PEAK_CH2                         0x4D
#define NAU85L40_PEAK_CH3                         0x4E
#define NAU85L40_PEAK_CH4                         0x4F
#define NAU85L40_GPIO_CTRL                        0x50
#define NAU85L40_MISC_CTRL                        0x51
#define NAU85L40_I2C_CTRL                         0x52
#define NAU85L40_I2C_DEVICE_ID                    0x58
#define NAU85L40_RST                              0x5A
#define NAU85L40_VMID_CTRL                        0x60
#define NAU85L40_MUTE                             0x61
#define NAU85L40_ANALOG_ADC1                      0x64
#define NAU85L40_ANALOG_ADC2                      0x65
#define NAU85L40_ANALOG_PWR                       0x66
#define NAU85L40_MIC_BIAS                         0x67
#define NAU85L40_REFERENCE                        0x68
#define NAU85L40_FEPGA1                           0x69
#define NAU85L40_FEPGA2                           0x6A
#define NAU85L40_FEPGA3                           0x6B
#define NAU85L40_FEPGA4                           0x6C
#define NAU85L40_PWR                              0x6D






/*
 * Field Definitions.
 */

#endif
