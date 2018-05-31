/*
 * f_stub.h  --  Actions USB Stub Gadget Driver
 *
 * Copyright (C) 2018 Actions, Inc.
 * Author: Jinang Lv <lvjinang@actions-semi.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UAPI_LINUX_USB_F_STUB_H
#define _UAPI_LINUX_USB_F_STUB_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define STUB_MAX_POINTS	14
#define STUB_VERSION_INFO	"7059100"

struct stub_status {
	__u8 test_start;
	__u8 upload_data;
	__u8 volume_changed;	/* STUB_OP_READ_VOLUME */
	__u8 download_data;
	__u8 upload_case_info;	/* STUB_OP_WRITE_STATUS */
	__u8 main_switch_info;	/* STUB_OP_READ_MAIN_SWITCH */
	__u8 mode_changed;	/* STUB_OP_READ_MODE */
	__u8 reserved_9[9];
	__u8 eq_data_changed;	/* STUB_OP_READ_EQ */
	__u8 vbass_data_changed;	/* STUB_OP_READ_VBASS */
	__u8 te_data_changed;	/* STUB_OP_READ_TE */
	__u8 limiter_data_changed;	/* STUB_OP_READ_LIMITER */
	__u8 spk_comp_data_changed;	/* STUB_OP_READ_SPKCMP */
	__u8 surr_data_changed;	/* STUB_OP_READ_SURR */
	__u8 drc_data_changed;	/* STUB_OP_READ_DRC */
	__u8 lfrc_data_changed;	/* STUB_OP_READ_LFRC */
	__u8 see_data_changed;	/* STUB_OP_READ_SEE */
	__u8 sew_data_changed;	/* STUB_OP_READ_SEW */
	__u8 sd_data_changed;	/* STUB_OP_READ_SIGNAL_DETE */
	__u8 rfrc_data_changed;	/* STUB_OP_READ_RFRC */
	__u8 compressor_data_changed;	/* STUB_OP_READ_COMPRESSOR */
	__u8 cpdrc_data_changed;	/* STUB_OP_READ_CPDRC */
	__u8 reserved_2[2];
};

struct stub_case_config_data {
	__s8 eq_v_1_0;
	__s8 vbass_v_1_0;
	__s8 te_v_1_0;
	__s8 surr_v_1_0;
	__s8 limiter_v_1_0;
	__s8 mdrc_v_1_0;
	__s8 src_v_1_0;
	__s8 see_v_1_0;
	__s8 sew_v_1_0;
	__s8 sd_v_1_0;
	__s8 eq_v_1_1;
	__s8 ms_v_1_0;
	__s8 vbass_v_1_1;
	__s8 te_1_1;
	__s8 eq_v_1_2;
	__s8 mdrc_v_1_1;
	__s8 compressor_v_1_0;
	__s8 reserved[119];
	/* Version info: Must be "7059100" */
	__s8 version[8];
};

/* For Write Status */
struct stub_case_info {
	/* Should be STUB_MAX_POINTS */
	__s8 max_points;
	/* Flag */
	__s8 data_over;
	__s8 aux_mode;
	__s8 reserved[29];
	struct stub_case_config_data config;
};

struct stub_volume_data {
	__s32 volume;
};

struct stub_eq_point_data {
	__s16 freq;
	__s16 gain;
	__s32 qvalue;
	__s16 type;
	/* 0: disabeld, 1: Speaker EQ, 2: Post EQ */
	__s8 status;
	__s8 reserved[1];
};

#define STUB_EQ_MAX_POINTS	14

/* For Read EQ */
struct stub_eq_data {
	__s16 main_gain;
	__s16 eq_gain;
	bool main_en;
	bool eq_en;
	__s8 npoint;
	__s8 max_point;
	struct stub_eq_point_data list[STUB_EQ_MAX_POINTS];
};

/*
 * NOTE: if STUB_MAX_POINTS > STUB_EQ_MAX_POINTS, need do
 * "Read EQ2/Read EQ 3" after "Read EQ".
 */

/* For Read EQ2 and Read EQ3 */
struct stub_eq2_data {
	__s8 total;
	__s8 reserved[3];
	struct stub_eq_point_data list[STUB_EQ_MAX_POINTS];
};

struct stub_vbass_data {
	__s16 cutoff_frequency;
	__s16 virtual_gain;
	bool enable;
	__s8 reserved[3];
};

struct stub_te_data {
	__s16 treble_gain;
	__s16 cutoff_frequency;
	bool enable;
	__s8 reserved[3];
};

struct stub_surr_data {
	__s16 head_angle;
	__s16 surround_gain;
	bool enable;
	__s8 reserved[3];
};

struct stub_limiter_data {
	__s16 threshold;
	__s16 attack_time;
	__s16 release_time;
	bool enable;
	__s8 index;
};

struct stub_spk_comp_data {
	__s16 order;
	__s16 fixed_qvalue;
	void *left;
	void *right;
	bool enable;
};

struct stub_drc_val_data {
	__s16 threshold;
	__s16 attack_time;
	__s16 release_time;
};

#define STUB_DRC_DATA_FLAG	0x5a

struct stub_drc_data {
	struct stub_drc_val_data val[3];
	bool enable;
	/* If STUB_DRC_DATA_FLAG, more data is coming (drc2) */
	__s8 data_flag;
	__s16 index;
	__s8 reserved[2];
};

struct stub_drc2_data {
	__s16 precut_max;
	__s16 param_volume;
	__s16 max_mid;
	__s16 mid_min;
	__s8 reserved[8];
};

struct stub_cpdrc_val_data {
	__s16 threshold_1;
	/* Compression Ratio */
	__s16 ratio_1;
	__s16 threshold_2;
	/* Compression Ratio */
	__s16 ratio_2;
	__s16 average_time;
	__s16 attack_time;
	__s16 release_time;
};

#define STUB_CPDRC_DATA_FLAG	0x5a

struct stub_cpdrc_data {
	struct stub_cpdrc_val_data val[3];
	bool enable;
	/* If STUB_DRC_DATA_FLAG, more data is coming (drc2) */
	__s8 data_flag;
	__s16 index;
	__s16 gain[3];
	__s8 reserved[8];
};

struct stub_cpdrc2_data {
	__s16 signal_precut;
	__s16 volume;
	__s16 max_mid;
	__s16 mid_min;
	/* Difference between MDRC and Limiter */
	__s16 diff;
	__s16 value;
	__s16 qvalue;
	__s16 start_time;
	__s16 release_time;
	/* Gain Compensation */
	__s16 gain_comp;
	__s8 filter_switch;
	__s8 reserved[11];
};

struct stub_frc_data {
	int order;
	int coeff;
	__u16 total_size;
	__u16 cur_size;
	__s8 buf[200];
};

struct stub_lfrc_data {
	struct stub_frc_data info;
	__s8 reserved[7];
	bool enabled;
};

struct stub_rfrc_data {
	struct stub_frc_data info;
	__s8 next_id;
	__s8 reserved[6];
	bool enabled;
};

/* High Pass */
struct stub_see_hp_data {
	__s16 cutoff_frequency;
	__s16 reserved;
	__s16 start_val;
	__s16 hold_val;
	__s16 range;
};

/* Low Frequency */
struct stub_see_lf_data {
	__s16 cutoff_frequency;
	__s16 enhance_vbass;
	__s16 start_val;
	__s16 hold_val;
	__s16 range;
};

/* High Frequency */
struct stub_see_hf_data {
	__s16 cutoff_frequency;
	__s16 enhance_te;
	__s16 start_val;
	__s16 hold_val;
	__s16 range;
};

/* Any Frequency */
struct stub_see_af_data {
	__s16 point;
	__s16 reserved;
	__s16 start_val;
	__s16 hold_val;
	__s16 range;
};

struct stub_see_data {
	struct stub_see_hp_data hp_info;	/* High Pass weaken */
	struct stub_see_lf_data lf_info;	/* Low Frequency enhance */
	struct stub_see_hf_data hf_info;	/* High Frequency enhance */
	struct stub_see_af_data af_info[3];	/* 3 Any Frequency */
	bool enable;
	__s8 reserved[3];
};

struct stub_sew_data {
	struct stub_see_hp_data hp_info;	/* High Pass enhance */
	struct stub_see_lf_data lf_info;	/* Low Frequency weaken */
	struct stub_see_hf_data hf_info;	/* High Frequency weaken */
	struct stub_see_af_data af_info[3];	/* 3 Any Frequency */
	bool enable;
	__s8 reserved[3];
};

struct stub_sd_data {
	__s16 dete_period;
	__s16 dete_num;
	__s16 des_value;
	__s16 des_num;
	bool enable;
	__s8 reserved[7];
};

struct stub_main_switch {
	__s32 value;
};

/* Support PC Tool Suspend */
struct stub_tool_suspend {
	__s32 timeout;
};

enum {
	aux_line_out = 0,
	aux_line_in
};

struct stub_aux_data {
	__s8 mode;
	__s8 reserved[7];
};

struct stub_compressor_data {
	__s16 threshold_1;
	/* Compression Ratio */
	__s16 ratio_1;
	__s16 threshold_2;
	/* Compression Ratio */
	__s16 ratio_2;
	__s16 average_time;
	__s16 attack_time;
	__s16 release_time;
	bool enable;
	/* Flag for aux mode */
	__s8 index;
	__s8 reserved[16];
};

/* Algorithm mode */
enum {
	mode_intelligent = 0,
	mode_standard
};

struct stub_alg_mode_data {
	__s32 mode;
};


#define STUB_READ_STATUS         _IOR('S', 0, struct stub_status)
#define STUB_READ_VOLUME         _IOR('S', 1, struct stub_volume_data)
#define STUB_READ_EQ             _IOR('S', 2, struct stub_eq_data)
#define STUB_READ_VBASS          _IOR('S', 3, struct stub_vbass_data)
#define STUB_READ_TE             _IOR('S', 4, struct stub_te_data)
#define STUB_READ_SURR           _IOR('S', 5, struct stub_surr_data)
#define STUB_READ_LIMITER        _IOR('S', 6, struct stub_limiter_data)
#define STUB_READ_SPKCMP         _IOR('S', 7, struct stub_spk_comp_data)
#define STUB_READ_DRC            _IOR('S', 8, struct stub_drc_data)
#define STUB_READ_DRC2           _IOR('S', 9, struct stub_drc2_data)
#define STUB_READ_LFRC           _IOR('S', 10, struct stub_lfrc_data)
#define STUB_READ_RFRC           _IOR('S', 11, struct stub_rfrc_data)
#define STUB_READ_SEE            _IOR('S', 12, struct stub_see_data)
#define STUB_READ_SEW            _IOR('S', 13, struct stub_sew_data)
#define STUB_READ_SD             _IOR('S', 14, struct stub_sd_data)
#define STUB_MAIN_SWITCH         _IOR('S', 15, struct stub_main_switch)
#define STUB_READ_EQ2            _IOR('S', 16, struct stub_eq2_data)
#define STUB_READ_EQ3            _IOR('S', 17, struct stub_eq2_data)

#define STUB_WRITE_STATUS        _IOW('S', 18, struct stub_case_info)
#define STUB_WRITE_VOLUME        _IOW('S', 19, struct stub_volume_data)
#define STUB_WRITE_EQ            _IOW('S', 20, struct stub_eq_data)
#define STUB_WRITE_VBASS         _IOW('S', 21, struct stub_vbass_data)
#define STUB_WRITE_TE            _IOW('S', 22, struct stub_te_data)
#define STUB_WRITE_SURR          _IOW('S', 23, struct stub_surr_data)
#define STUB_WRITE_LIMITER       _IOW('S', 24, struct stub_limiter_data)
#define STUB_WRITE_SPKCMP        _IOW('S', 25, struct stub_spk_comp_data)
#define STUB_TOOL_SUSPEND        _IOW('S', 26, struct stub_tool_suspend)

#define STUB_WRITE_AUX           _IOW('S', 27, struct stub_aux_data)
#define STUB_READ_CMPRESSOR      _IOR('S', 28, struct stub_compressor_data)
#define STUB_READ_CPDRC          _IOR('S', 29, struct stub_cpdrc_data)
#define STUB_READ_CPDRC2         _IOR('S', 30, struct stub_cpdrc2_data)
#define STUB_READ_MODE           _IOR('S', 31, struct stub_alg_mode_data)

#endif	/* _UAPI_LINUX_USB_F_STUB_H */
