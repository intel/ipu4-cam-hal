/*
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

#ifndef __IA_CSS_DRC_DEFS_H
#define __IA_CSS_DRC_DEFS_H

#include "type_support.h"

/* @file
 * This file contains DRC runtime parameters
 */

#include "math_support.h" /* for CEIL_DIV */

#define DRC_KERNEL_VERSION(x, y, z)     (((x) << 8) + ((y) << 4) + (z))
/*
 * WARNINGS
 * - "IA_CSS_DRC_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32" (equal to ISP_NWAY).
 *   Please do not change it to a different value.
 */
#define	IA_CSS_DRC_ARRAY_ACCESS_SIZE					(32)

#ifdef __ISP
#define	IA_CSS_DRC_ARRAY(x, s)	tmemvectoru x[CEIL_DIV((s), IA_CSS_DRC_ARRAY_ACCESS_SIZE)]
#define	IA_CSS_DRC_ARRAY_SIGNED(x, s)	tmemvectors x[CEIL_DIV((s), IA_CSS_DRC_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_DRC_ARRAY(x, s)	uint16_t x[CEIL_DIV((s), IA_CSS_DRC_ARRAY_ACCESS_SIZE)][IA_CSS_DRC_ARRAY_ACCESS_SIZE]
#define	IA_CSS_DRC_ARRAY_SIGNED(x, s)	int16_t x[CEIL_DIV((s), IA_CSS_DRC_ARRAY_ACCESS_SIZE)][IA_CSS_DRC_ARRAY_ACCESS_SIZE]
#endif

/*
 * TODO: Add program terminals
 */

/*
 * DRC parameter terminal sections
 *
 * DRC is mapped onto two ISPs which are called ISP0 and ISP1 for brevity
 * Hence, parameters meant for ISP0 and ISP1 are packed into separate structures
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC_SECTION_COUNT		(4)

/* Below two sections are meant for ISP0 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC_MAX_SIZE_SECTION_0		(sizeof(struct ia_css_drc_1_1_p0_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC_MAX_SIZE_SECTION_1		(sizeof(struct ia_css_drc_1_1_p0_vmem_param))

/* Below two sections are meant for ISP1 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC_MAX_SIZE_SECTION_2		(sizeof(struct ia_css_drc_1_1_p1_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC_MAX_SIZE_SECTION_3		(sizeof(struct ia_css_drc_1_1_p1_vmem_param))

/*
 * DRC spatial parameter terminal sections
 *
 * Spatial terminal memory layout is depicted below. Note that each of the maps (luma, gain, and weight)
 * has its own section within the spatial terminal. The actaul size of the maps (i.e. spatial terminal)
 * is defined by the image resolution and the downscale factor parameter defined by the PAL library and/or
 * tuning and passed to the DRC kernel via parameter terminals. Element size for each of the sections
 * is the same and is equal to the maximal pixel depth (16 bits in IPU4i).
 *
 * Note that the width of each map (N) needs to be multiple of vector length (ISP_NWAY). Padding with
 * zero is required when this condition is not satisfied.
 *
 * The users of the spatial parameter terminals needs to ensure that a proper stride is set during the
 * memory allocation. Offset for each of the sections is defined by the element size, width, and height.
 *
 * <----------------------------- N ---------------------------->
 *
 * --------------------------------------------------------------   ^
 * | luma[0][0]     |            ...            | luma[0][N-1]   |  |
 * --------------------------------------------------------------   |
 *                                .                                 |
 *                                .                                 M
 *                                .                                 |
 * --------------------------------------------------------------   |
 * | luma[M-1][0]   |            ...            | luma[M-1][N-1] |  |
 * --------------------------------------------------------------   v
 * | gain[0][0]     |            ...            | gain[0][N-1]   |
 * --------------------------------------------------------------
 *                                .
 *                                .
 *                                .
 * --------------------------------------------------------------
 * | gain[M-1][0]   |            ...            | gain[M-1][N-1] |
 * --------------------------------------------------------------
 * | wght[0][0]     |            ...            | wght[0][N-1]   |
 * --------------------------------------------------------------
 *                                .
 *                                .
 *                                .
 * --------------------------------------------------------------
 * | weight[M-1][0] |            ...            | wght[M-1][N-1] |
 * --------------------------------------------------------------
 *
 */
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_SECTION_COUNT	(3)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ID_SECTION_0		(0)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ELEM_SIZE_SECTION_0	(sizeof(uint16_t))
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ID_SECTION_1		(1)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ELEM_SIZE_SECTION_1	(sizeof(uint16_t))
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ID_SECTION_2		(2)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_DRC_ELEM_SIZE_SECTION_2	(sizeof(uint16_t))

#define DRC_BLUS_SP_COEF_TBL_SIZE					(1 << 8)

#define DRC_XCU_GTM_TBL_SIZE						32

struct ia_css_drc_1_1_p0_dmem_param {
	uint16_t blus_bypass;						/**< Bypass dynamic range compression */
	uint16_t blus_factor;						/**< Bilateral upscale factor */
	uint16_t blus_sim_min;						/**< Minimal similarity in bilateral upscale */
	uint16_t padding;						/**< Make sure that the size is multiple of 4 */
	uint16_t xcu_gtm_x_cord_max;					/**< GTM maximum of x coordinate */
	uint16_t xcu_gtm_exponent;					/**< GTM exponent */
	uint16_t xcu_gtm_slope_resolution;				/**< GTM slope resolution */
	uint16_t gtm_gain_frac_bit;					/**< GTM gain map fraction bit */
};

struct ia_css_drc_1_1_p0_vmem_param {
	IA_CSS_DRC_ARRAY(blus_sp_coef_a, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale top left coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_b, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale top right coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_c, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale bottom left coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_d, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale bottom right coefs */
	IA_CSS_DRC_ARRAY_SIGNED(xcu_gtm_slope_vec, DRC_XCU_GTM_TBL_SIZE); /**< GTM slope LUT */
	IA_CSS_DRC_ARRAY(xcu_gtm_offset_vec, DRC_XCU_GTM_TBL_SIZE);	/**< GTM offset LUT */
	IA_CSS_DRC_ARRAY(xcu_gtm_x_cord_vec, DRC_XCU_GTM_TBL_SIZE);	/**< GTM x coordinate LUT */
};

struct ia_css_drc_1_1_p1_dmem_param {
	uint16_t blus_bypass;						/**< Bypass dynamic range compression */
	uint16_t blus_factor;						/**< Bilateral upscale factor */
	uint16_t blus_sim_min;						/**< Minimal similarity in bilateral upscale */
	uint16_t lce_bypass;						/**< Bypass local contract enhancement */
	int16_t  lce_model_slope;					/**< Model slope for delta stream extraction */
	int16_t  lce_model_offset;					/**< Model offset for delta stream extraction */
	int16_t  lce_viir_sp_weight;					/**< Spatial weight for vertical IIR filter */
	uint16_t lce_delta_deadzone;					/**< Delta stream dead zone threashold */
	uint16_t lce_delta_k;						/**< Delta stream boost zone slope */
	uint16_t lce_delta_clip;					/**< Delta stream clip zone threshold */
	uint16_t xcu_gtm_x_cord_max;					/**< GTM maximum of x coordinate */
	uint16_t xcu_gtm_exponent;					/**< GTM exponent */
	uint16_t xcu_gtm_slope_resolution;				/**< GTM slope resolution */
	uint16_t gtm_gain_frac_bit;					/**< GTM gain map fraction bit */
};

struct ia_css_drc_1_1_p1_vmem_param {
	IA_CSS_DRC_ARRAY(blus_sp_coef_a, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale top left coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_b, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale top right coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_c, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale bottom left coefs */
	IA_CSS_DRC_ARRAY(blus_sp_coef_d, DRC_BLUS_SP_COEF_TBL_SIZE);	/**< Bilateral upscale bottom right coefs */
	IA_CSS_DRC_ARRAY_SIGNED(xcu_gtm_slope_vec, DRC_XCU_GTM_TBL_SIZE); /**< GTM slope LUT */
	IA_CSS_DRC_ARRAY(xcu_gtm_offset_vec, DRC_XCU_GTM_TBL_SIZE);	/**< GTM offset LUT */
	IA_CSS_DRC_ARRAY(xcu_gtm_x_cord_vec, DRC_XCU_GTM_TBL_SIZE);	/**< GTM x coordinate LUT */
};
#endif /* __IA_CSS_DRC_DEFS_H */
