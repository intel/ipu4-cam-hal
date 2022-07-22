/**
* INTEL CONFIDENTIAL
*
* Copyright (C) 2015 - 2020 Intel Corporation.
* All Rights Reserved.
*
* The source code contained or described herein and all documents
* related to the source code ("Material") are owned by Intel Corporation
* or licensors. Title to the Material remains with Intel
* Corporation or its licensors. The Material contains trade
* secrets and proprietary and confidential information of Intel or its
* licensors. The Material is protected by worldwide copyright
* and trade secret laws and treaty provisions. No part of the Material may
* be used, copied, reproduced, modified, published, uploaded, posted,
* transmitted, distributed, or disclosed in any way without Intel's prior
* express written permission.
*
* No License under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or
* delivery of the Materials, either expressly, by implication, inducement,
* estoppel or otherwise. Any license under such intellectual property rights
* must be express and approved by Intel in writing.
*/

#ifndef __IA_CSS_TNR5_2_1_DEFS_H
#define __IA_CSS_TNR5_2_1_DEFS_H

#include "type_support.h"

/** @file
 * Contains TNR5_2_1 run time parameters
 **/

#include "math_support.h" /* for CEIL_DIV   */

/*
 * WARNINGS
 * - "IA_CSS_TNR5_2_1_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32". Please do not change
 *   it to a different value.
 */
#define	IA_CSS_TNR5_2_1_ARRAY_ACCESS_SIZE		32 /* 32 elements per access in an array */

#ifdef __ISP
#define	IA_CSS_TNR5_2_1_ARRAY(x, s)	tmemvectoru x[CEIL_DIV((s), IA_CSS_TNR5_2_1_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_TNR5_2_1_ARRAY(x, s)	uint16_t x[CEIL_DIV((s), IA_CSS_TNR5_2_1_ARRAY_ACCESS_SIZE)][IA_CSS_TNR5_2_1_ARRAY_ACCESS_SIZE]
#endif

/*
 * TNR5_2_1 Parameter terminal sections
 * TNR5_2_1 is mapped onto two ISPs which we call ISP0 and ISP1 for brevity
 * Hence we club parameters meant for ISP0 and ISP1 in separate structures
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_2_1_SECTION_COUNT          (4)

/* Below two sections are meant for ISP0
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_2_1_MAX_SIZE_SECTION_0     (sizeof(struct ia_css_tnr5_2_1_p0_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_2_1_MAX_SIZE_SECTION_1     (sizeof(struct ia_css_tnr5_2_1_p0_vmem_param))

/* Below two sections are meant for ISP1
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_2_1_MAX_SIZE_SECTION_2     (sizeof(struct ia_css_tnr5_2_1_p1_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_2_1_MAX_SIZE_SECTION_3     (sizeof(struct ia_css_tnr5_2_1_p1_vmem_param))


#define TNR5_2_1_LUT_BITS       (6)
#define TNR5_2_1_LUT_SIZE       (1 << TNR5_2_1_LUT_BITS)

#define TNR5_2_1_RAD_LUT_BITS   (7)
#define TNR5_2_1_RAD_LUT_SIZE   (1 << TNR5_2_1_RAD_LUT_BITS)

#define TWO_TIMES_ISP_NWAY					(64) /* 2 * ISP_NWAY */
#define THREE_TIMES_ISP_NWAY					(96) /* 3 * ISP_NWAY */

/* If frame_id == 1 or 2 , previous frame is unavailable, and
 * subsequently, sptnr can't be calculated by TBD module.
 * If frame_id == 3, sptnr can be generated by TBD and can be
 * used by pixel blend.
 * frame_id can only have 1,2 or 3 values as set by driver.
*/
typedef enum {
	TNR5_2_1_FRAME_ID_1 = 1,
	TNR5_2_1_FRAME_ID_2,
	TNR5_2_1_FRAME_ID_3
} tnr5_2_1_frame_id_t;

struct ia_css_tnr5_2_1_p0_dmem_param {

	int32_t frame_id;         /* if (frame_id == 3) previous similarity should
                                   * be considered in calculating current frame's
                                   * similarity
                                  */
	/* Noise Metric */
	int32_t nm_radial_r2_shift;                  /* Bit shift number to calculate index for nm_R_xcu  */
	int32_t nm_radial_y_center;                  /* Optical center y coordinate in chroma resolution */
	int32_t nm_radial_x_center;                  /* Optical center x coordinate in chroma resolution */
	int32_t nm_radial_lut[TNR5_2_1_RAD_LUT_SIZE];   /* LUT for radial noise model */
	int32_t nm_luma_log_est_min;                 /* Min value for log space Y noise model */
	int32_t nm_luma_log_est_max;                 /* Max value for log space Y noise model */
	int32_t nm_chroma_log_est_min;               /* Min value for log space C noise model */
	int32_t nm_chroma_log_est_max;               /* Max value for log space C noise model */
	int32_t nm_lut_frac;                         /* Fractional bit for noise model LUT */
	int32_t nm_idx_frac;                         /* bit number for adjusting to POW LUT index */
	int32_t nm_pow_frac;                         /* Fractional bit for POW LUT */
	int32_t nm_luma_alpha;                       /* Scaling coefficient for Y noise model */
	int32_t nm_chroma_alpha;                     /* Scaling coefficient for C noise model */
	int32_t nm_shift_num;                        /* nm_lut_frac - nm_idx_frac  */
	/* Content Detection */
	int32_t co_var_thres;                        /* Variance threshold */
	int32_t co_var_gain;                         /* Variance gain */
	/* Apply Global Motion */
	int32_t g_mv_y;                              /* Global motion vector - Y component */
	int32_t g_mv_x;                              /* Global motion vector - X component */
	/* Trajectory Break Detection */
	int32_t tbd_sad_luma_gain;                   /* Y (Luma) SAD sensitivity gain */
	int32_t tbd_sim_arg_gain_m;                  /* Similarity argument gain for dynamic */
	int32_t tbd_con_thres;                       /* Content level threshold */
	int32_t tbd_con_gain;                        /* Content level gain */
	int32_t tbd_rec_sim_thres;                   /* Similarity threshold for recursive filter */
	int32_t tbd_rec_gain0;                       /* Gain of recursive filter for static region */
	int32_t tbd_rec_gain1;                       /* Gain of recursive filter for dynamic region */
	/* Normalize Noise Stream */
	int32_t ns_bypass;                           /* NS bypass */
	int32_t ns_norm_coef;                        /* NS equalization linear function coef */
	int32_t ns_norm_bias;                        /* NS equalization linear function bias */
};

struct ia_css_tnr5_2_1_p0_vmem_param {
	/* Noise Metric */
	IA_CSS_TNR5_2_1_ARRAY(nm_luma_y_lut, TNR5_2_1_LUT_SIZE);    /* LUT for Y noise model - Y dependency */
	IA_CSS_TNR5_2_1_ARRAY(nm_luma_c_lut, TNR5_2_1_LUT_SIZE);    /* LUT for Y noise model - C dependency */
	IA_CSS_TNR5_2_1_ARRAY(nm_chroma_y_lut, TNR5_2_1_LUT_SIZE);  /* LUT for C noise model - Y dependency */
	IA_CSS_TNR5_2_1_ARRAY(nm_chroma_c_lut, TNR5_2_1_LUT_SIZE);  /* LUT for C noise model - C dependency */
	IA_CSS_TNR5_2_1_ARRAY(nm_pow_lut, TNR5_2_1_LUT_SIZE);       /* LUT for power function */
	/* Trajectory Break Detection */
	IA_CSS_TNR5_2_1_ARRAY(tbd_sim_lut, TNR5_2_1_LUT_SIZE);      /* Similarity LUT */
};

struct ia_css_tnr5_2_1_p1_dmem_param {
	int32_t frame_id;         /* if (frame_id == 3) similarity should
                                   * be considered in calculating current frame's
                                   * pixel blend output
                                  */
	/* Pixel Blending */
	int32_t pb_weight_in;                 /* input weight */
	int32_t pb_weight_pre_max;            /* Max weight of temporal filter output */
	int32_t pb_weight_sp_thres;           /* Spatial filter weight threshold */
	int32_t pb_weight_sp_gain;            /* Spatial filter weight gain */
	int32_t pb_weight_sp_max0;            /* Spatial filter weight max at high similarity region */
	int32_t pb_weight_sp_max1;            /* Spatial filter weight max at low similarity region */
	int32_t pb_ns_out_max;                /* Max value for clipping noise stream */
	int32_t pb_ns_bypass;                 /* NS bypass, needed inside pixel blend */
	/* Simple Edge Enhanc */
	int32_t see_edge_max;                 /* Max edge value for clamping */
	int32_t see_edge_thres;               /* Threshold for coring */
};

struct ia_css_tnr5_2_1_p1_vmem_param {
	/* Pixel Blending */
	IA_CSS_TNR5_2_1_ARRAY(pb_ns_weight_lut, TNR5_2_1_LUT_SIZE); /* LUT for noise stream weight control by similarity */
	IA_CSS_TNR5_2_1_ARRAY(pb_ns_conlv_w_lut, TNR5_2_1_LUT_SIZE);/* LUT for noise stream weight control by content level */
	/* Simple Edge Enhanc */
	IA_CSS_TNR5_2_1_ARRAY(see_alpha_lut, TNR5_2_1_LUT_SIZE);    /* LUT for edge enhance gain */
};

#endif /* __IA_CSS_TNR5_2_1_DEFS_H */
