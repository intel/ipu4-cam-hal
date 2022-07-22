/**
* INTEL CONFIDENTIAL
*
* Copyright (C) 2016 - 2017 Intel Corporation.
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

#ifndef __IA_CSS_TNR5_25_DEFS_H
#define __IA_CSS_TNR5_25_DEFS_H

#include "type_support.h"
#include "math_support.h"

#define	IA_CSS_TNR5_25_ARRAY_ACCESS_SIZE		32 /* 32 elements per access in an array */

#ifdef __ISP
#define	IA_CSS_TNR5_25_ARRAY(x, s)	tmemvectoru x[CEIL_DIV((s), IA_CSS_TNR5_25_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_TNR5_25_ARRAY(x, s)	uint16_t x[CEIL_DIV((s), IA_CSS_TNR5_25_ARRAY_ACCESS_SIZE)][IA_CSS_TNR5_25_ARRAY_ACCESS_SIZE]
#endif

/**
* TNR5_25 Parameter terminal sections
* TNR5_25 is mapped onto one ISP
*/
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_25_SECTION_COUNT          (2)

#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_25_MAX_SIZE_SECTION_0     (sizeof(struct ia_css_tnr5_25_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_25_MAX_SIZE_SECTION_1     (sizeof(struct ia_css_tnr5_25_vmem_param))

#define TNR5_25_LUT_BITS       (6)
#define TNR5_25_LUT_SIZE       (1 << TNR5_25_LUT_BITS)

#define TNR5_25_RAD_LUT_BITS   (7)
#define TNR5_25_RAD_LUT_SIZE   (1 << TNR5_25_RAD_LUT_BITS)

struct ia_css_tnr5_25_dmem_param {
	/* Noise Metric */
	int16_t nm_radial_r2_shift;                  /* Bit shift number to calculate index for nm_R_xcu  */
	int16_t nm_radial_y_center;                  /* Optical center y coordinate in chroma resolution */
	int16_t nm_radial_x_center;                  /* Optical center x coordinate in chroma resolution */
	int16_t nm_luma_log_est_min;                 /* Min value for log space Y noise model */
	int16_t nm_luma_log_est_max;                 /* Max value for log space Y noise model */
	int16_t nm_chroma_log_est_min;               /* Min value for log space C noise model */
	int16_t nm_chroma_log_est_max;               /* Max value for log space C noise model */
	int16_t nm_lut_frac;                         /* Fractional bit for noise model LUT */
	int16_t nm_idx_frac;                         /* bit number for adjusting to POW LUT index */
	int16_t nm_pow_frac;                         /* Fractional bit for POW LUT */
	int16_t nm_luma_alpha;                       /* Scaling coefficient for Y noise model */
	int16_t nm_chroma_alpha;                     /* Scaling coefficient for C noise model */
	int16_t nm_shift_num;                        /* nm_lut_frac - nm_idx_frac  */
	/* Apply Global Motion */
	int16_t g_mv_y;                              /* Global motion vector - Y component */
	int16_t g_mv_x;                              /* Global motion vector - X component */
	/* Trajectory Break Detection */
	int16_t tbd_sad_luma_gain;                   /* Y (Luma) SAD sensitivity gain */
	int16_t tbd_sim_arg_gain_m;                  /* Similarity argument gain for dynamic */
	int16_t tbd_rec_sim_thres;                   /* Similarity threshold for recursive filter */
	int16_t tbd_rec_gain0;                       /* Gain of recursive filter for static region */
	int16_t tbd_rec_gain1;                       /* Gain of recursive filter for dynamic region */
	/* Pixel Blending */
	int16_t pb_weight_pre_max;                   /* Max weight of temporal filter output */
	int16_t padding;                             /* for alignment purposes.*/
};

struct ia_css_tnr5_25_vmem_param {
	IA_CSS_TNR5_25_ARRAY(nm_radial_lut, TNR5_25_RAD_LUT_SIZE);           /**< Sqrt LUT for radius calculation */
	/* Noise Metric */
	IA_CSS_TNR5_25_ARRAY(nm_luma_y_lut, TNR5_25_LUT_SIZE);    /* LUT for Y noise model - Y dependency */
	IA_CSS_TNR5_25_ARRAY(nm_luma_c_lut, TNR5_25_LUT_SIZE);    /* LUT for Y noise model - C dependency */
	IA_CSS_TNR5_25_ARRAY(nm_chroma_y_lut, TNR5_25_LUT_SIZE);  /* LUT for C noise model - Y dependency */
	IA_CSS_TNR5_25_ARRAY(nm_chroma_c_lut, TNR5_25_LUT_SIZE);  /* LUT for C noise model - C dependency */
	IA_CSS_TNR5_25_ARRAY(nm_pow_lut, TNR5_25_LUT_SIZE);       /* LUT for power function */
	/* Trajectory Break Detection */
	IA_CSS_TNR5_25_ARRAY(tbd_sim_lut, TNR5_25_LUT_SIZE);      /* Similarity LUT */
};
#endif /* __IA_CSS_TNR5_25_DEFS_H */

