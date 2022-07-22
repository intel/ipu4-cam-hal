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

#ifndef __IA_CSS_TNR5_DEFS_H
#define __IA_CSS_TNR5_DEFS_H

#include "type_support.h"

/** @file
 * Contains TNR5 run time parameters
 **/

#include "math_support.h" /* for CEIL_DIV   */

/*
 * WARNINGS
 * - "IA_CSS_TNR4_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32". Please do not change
 *   it to a different value.
 */
#define	IA_CSS_TNR5_ARRAY_ACCESS_SIZE		32 /* 32 elements per access in an array */

#ifdef __ISP
#define	IA_CSS_TNR5_ARRAY(x, s)	tmemvectoru x[CEIL_DIV((s), IA_CSS_TNR5_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_TNR5_ARRAY(x, s)	uint16_t x[CEIL_DIV((s), IA_CSS_TNR5_ARRAY_ACCESS_SIZE)][IA_CSS_TNR5_ARRAY_ACCESS_SIZE]
#endif

/*
 * TNR5 Parameter terminal sections
 * TNR5 is mapped onto two ISPs which we call ISP0 and ISP1 for brevity
 * Hence we club parameters meant for ISP0 and ISP1 in separate structures
 *
 * TODO: Add Program terminals
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_SECTION_COUNT          (4)

/* Below two sections are meant for ISP0
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_MAX_SIZE_SECTION_0     (sizeof(struct ia_css_tnr5_p0_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_MAX_SIZE_SECTION_1     (sizeof(struct ia_css_tnr5_p0_vmem_param))

/* Below two sections are meant for ISP1
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_MAX_SIZE_SECTION_2     (sizeof(struct ia_css_tnr5_p1_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_TNR5_MAX_SIZE_SECTION_3     (sizeof(struct ia_css_tnr5_p1_vmem_param))

#define TNR5_LUT_BITS           (6)
#define TNR5_LUT_SIZE		(1 << TNR5_LUT_BITS)

#define TNR5_RAD_LUT_BITS       (7)
#define TNR5_RAD_LUT_SIZE	(1 << TNR5_RAD_LUT_BITS)

#define TNR5_DIST_WEIGHTS_ROWS 	(8)
#define TNR5_DIST_WEIGHTS_COLS 	(16)
#define TNR5_DIST_WEIGHT_SIZE   (TNR5_DIST_WEIGHTS_ROWS * TNR5_DIST_WEIGHTS_COLS)

struct ia_css_tnr5_p0_dmem_param {
	int16_t nm_radial_x_center;                             /**< X co ordinate of the centre block of the frame in terms of 16x16 blocks */
	int16_t nm_radial_y_center;                             /**< Y co ordinate of the centre block of the frame in terms of 16x16 blocks */
	uint16_t nm_radial_lut[TNR5_RAD_LUT_SIZE];              /**< Sqrt LUT for radius calculation */
	uint16_t nm_luma_lut[TNR5_LUT_SIZE];                    /**< Noise metric table for Luma */
	uint16_t nm_chroma_lut[TNR5_LUT_SIZE];                  /**< Noise metric table for chroma */
	uint16_t nm_radial_r2_shift;                            /**< Radial square normalisation */
	uint16_t co_content_shift;                              /**< Shift right for content */
	uint16_t co_max_content;                                /**< Maximal value for the content */
	uint16_t fm_sad_shift;                                  /**< Chroma sim normalisation shift */
	uint16_t fm_sim_arg_shift;                              /**< Shift for FMVE similarity values */
	uint16_t mvd_sad_shift;                                 /**< Shift for the SAD values in the MVD sub block (using realigning) */
	uint16_t mvd_arg_g_static;                              /**< Gain for the recursive similarity in case of static MV */
	uint16_t mvd_arg_g_static_local;                        /**< Gain for the recursive similarity in case of static MV without neigh. */
	uint16_t mvd_arg_g_move;                                /**< Gain for the recursive similarity in case of non static MV */
	uint16_t mvd_arg_g_move_local;                          /**< Gain for the recursive similarity in case of non static MV without neigh. */
	int16_t g_mv_x;                                         /**< Global motion vector for Luma, horizontal offset */
	int16_t g_mv_y;                                         /**< Global motion vector for Luma, vertical offset */
};

struct ia_css_tnr5_p0_vmem_param {
	IA_CSS_TNR5_ARRAY(nm_luma_lut, TNR5_LUT_SIZE);                 /**< Noise metric table for Luma */
	IA_CSS_TNR5_ARRAY(nm_chroma_lut, TNR5_LUT_SIZE);               /**< Noise metric table for chroma */
	IA_CSS_TNR5_ARRAY(fm_dist_weights, TNR5_DIST_WEIGHT_SIZE);     /**< 2D array of distance weight parameter values */
	IA_CSS_TNR5_ARRAY(fm_sim_lut, TNR5_LUT_SIZE);                  /**< Similarity LUT for FMVE */
	IA_CSS_TNR5_ARRAY(mvd_sim_lut, TNR5_LUT_SIZE);                 /**< Array of similarity values for MVD */
};

struct ia_css_tnr5_p1_dmem_param {
	int16_t nm_radial_x_center;                             /**< X co ordinate of the centre block of the frame in terms of 16x16 blocks */
	int16_t nm_radial_y_center;                             /**< Y co ordinate of the centre block of the frame in terms of 16x16 blocks */
	uint16_t nm_radial_r2_shift;                            /**< Radial square normalisation */
	uint16_t pb_weight_pre_max;                             /**< Maximal SPTNR weight */
	uint16_t pb_weight_in_min;                              /**< Minimal weight of the input frame at the blended output image */
	uint16_t padding_bytes;                                 /**< padding to have size multiple of 4 bytes */
};

struct ia_css_tnr5_p1_vmem_param {
	IA_CSS_TNR5_ARRAY(nm_radial_lut, TNR5_RAD_LUT_SIZE);           /**< Sqrt LUT for radius calculation */
	IA_CSS_TNR5_ARRAY(spnr_lut_v, TNR5_LUT_SIZE);                  /**< SPNR BF LUT for vertical direction */
	IA_CSS_TNR5_ARRAY(spnr_lut_h, TNR5_LUT_SIZE);                  /**< SPNR BF LUT for horizontal direction */
	IA_CSS_TNR5_ARRAY(nm_spnr_lut, TNR5_LUT_SIZE);                 /**< SPNR specific noise metric table for luma */
};

#endif /* __IA_CSS_TNR5_DEFS_H */
