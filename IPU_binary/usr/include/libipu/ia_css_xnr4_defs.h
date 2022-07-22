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
#ifndef __IA_CSS_XNR4_DEFS_H
#define __IA_CSS_XNR4_DEFS_H

#include "type_support.h"

/** @file
 * Contains XNR4 parameters
 **/

#include "math_support.h" /* for CEIL_DIV   */

/*
 * WARNINGS
 * - "IA_CSS_XNR4_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32". Please do not change
 *   it to a different value.
 */
#define	IA_CSS_XNR4_ARRAY_ACCESS_SIZE	32 /* 32 elements per access in an array */

#ifdef __ISP
#define IA_CSS_XNR4_ARRAY(x, s) tmemvectoru x[CEIL_DIV((s), IA_CSS_XNR4_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_XNR4_ARRAY(x, s) uint16_t x[CEIL_DIV((s), IA_CSS_XNR4_ARRAY_ACCESS_SIZE)][IA_CSS_XNR4_ARRAY_ACCESS_SIZE]
#endif /* __ISP */

#define XNR4_RAD_LUT_SIZE             128
#define XNR4_BFA_KWAY                 49
#define XNR4_BFA_RW_LUT_SIZE          7
#define XNR4_DIVSQRT_LUT_SIZE         64
#define XNR4_LXCU_LUT_SIZE            16
#define XNR4_XCU_LUT_SIZE             32
#define XNR4_HF_BLEND_LUT_SIZE        64

typedef enum {
	XNR4_LANE_NOISE_SIGMA_MF_Y_Y,
	XNR4_LANE_NOISE_SIGMA_MF_Y_S,
	XNR4_LANE_NOISE_SIGMA_MF_C_Y,
	XNR4_LANE_NOISE_SIGMA_MF_C_U,
	XNR4_LANE_NOISE_SIGMA_MF_C_V,
	XNR4_LANE_NOISE_EPSILON_MF_Y_Y,
	XNR4_LANE_NOISE_EPSILON_MF_C_Y,
	XNR4_LANE_NOISE_EPSILON_MF_C_U,
	XNR4_LANE_NOISE_EPSILON_MF_C_V,
	XNR4_LANE_BLEND_MF_POWER_Y,
	XNR4_LANE_BLEND_MF_POWER_C,

	XNR4_LANE_LCS_TH_LOW,
	XNR4_LANE_LCS_TH_HIGH_DELTA,
	XNR4_LANE_NOISE_SIGMA_HF_C_Y,
	XNR4_LANE_NOISE_SIGMA_HF_C_U,
	XNR4_LANE_NOISE_SIGMA_HF_C_V,
	XNR4_LANE_NOISE_EPSILON_HF_C_Y,
	XNR4_LANE_NOISE_EPSILON_HF_C_U,
	XNR4_LANE_NOISE_EPSILON_HF_C_V,
	XNR4_LANE_HF_LUMA_POWER,

	XNR4_MAX_LANE_RAD_PARAM
} s_xnr4_rad_param_lane;

/*******************************************************************************
 * XNR4 Fragment Dependent Parameters
 * All ISPs need to have copy of this structure in DMEM for both video and still
 ******************************************************************************/
struct ia_css_xnr4_fragment_param {
	int16_t  rad_center_x; /* Radial center X loc relative to the top left */
	int16_t  rad_center_y; /* Radial center Y loc relative to the top left */
};

/*******************************************************************************
 * XNR4 Common DMEM Parameters
 ******************************************************************************/
struct ia_css_xnr4_common_dmem_param {
	uint16_t rad_exp;                    /* Radial scale bit into the Lut */
	uint16_t rad_lut[XNR4_RAD_LUT_SIZE]; /* Radial LUT */
	int16_t  noise_beta_y_0;             /* Noise model for y - coef 0, Q12 */
	int16_t  noise_beta_u_0;             /* Noise model for u - coef 0, Q12 */
	int16_t  noise_beta_u_1;             /* Noise model for u - coef 1, Q12 */
	int16_t  noise_beta_v_0;             /* Noise model for v - coef 0, Q12 */
	int16_t  noise_beta_v_2;             /* Noise model for v - coef 2, Q12 */
	uint16_t xcu_divsqrt_exp;            /* XCU divsqrt exponent */
	uint16_t xcu_divsqrt_slp_a_res;      /* XCU divsqrt slope precision */
	uint16_t xcu_divsqrt_x_max;          /* XCU divsqrt maximal x value */
	uint16_t padding_bytes;              /* padding to have size multiple of 4 bytes */
};

/*******************************************************************************
 * XNR4 Medium Frequency DMEM Parameters
 * For Video this structure is at ISP0 and ISP1 DMEM
 * For Still this structure is at ISP0 DMEM
 ******************************************************************************/
struct ia_css_xnr4m_dmem_param {
	uint16_t jbl_mf_bypass_y;    /* Bypass JBL MF Luma */
	uint16_t jbl_mf_bypass_c;    /* Bypass MF Chroma */
	uint16_t jbl_weight_th_mf_y; /* Luma Weight threshold of MF JBL */
	uint16_t jbl_weight_th_mf_c; /* Chroma Weight threshold of MF JBL */
	struct ia_css_xnr4_common_dmem_param common_param;
};

/*******************************************************************************
 * XNR4 High Frequency DMEM Parameters
 * For Still this structure is at ISP1 and ISP2 DMEM
 ******************************************************************************/
struct ia_css_xnr4h_dmem_param {
	struct ia_css_xnr4_common_dmem_param common_param;
	uint16_t jbl_hf_bypass_c;          /* Bypass HF Chroma */
	uint16_t jbl_weight_th_hf_c;       /* Chroma Weight threshold of HF JBL */
	uint16_t xcu_lcs_exp;              /* XCU ed exponent */
	uint16_t xcu_lcs_slp_a_res;        /* XCU ed slope precision */
	uint16_t xcu_lcs_x_max;            /* XCU ed - maximal x value */
	uint16_t nncu_hf_luma_blend_exp;   /* NNCU HF Luma blend  exponent */
	uint16_t nncu_hf_luma_blend_x_0;   /* NNCU HF Luma blend first X cordinate */
	uint16_t nncu_hf_luma_blend_x_max; /* NNCU HF Luma blend last X cordinate */
	int16_t blend_hf_power_c;          /* HF denoise blending power chroma */
	uint16_t padding_bytes;            /* Padding to have size multiple of 4 bytes */
};

/*******************************************************************************
 * XNR4 Common Frequency VMEM Parameters
 ******************************************************************************/
struct ia_css_xnr4_common_vmem_param {
	IA_CSS_XNR4_ARRAY(rad_param_center,   XNR4_MAX_LANE_RAD_PARAM); /* Radial dependent param - Center */
	IA_CSS_XNR4_ARRAY(rad_param_delta,    XNR4_MAX_LANE_RAD_PARAM); /* Radial dependent param - Delta */
	IA_CSS_XNR4_ARRAY(one_div_64_lut,     XNR4_DIVSQRT_LUT_SIZE);   /* 1/SQRT(x), first 64 values LUT */
	IA_CSS_XNR4_ARRAY(xcu_divsqrt_offset, XNR4_LXCU_LUT_SIZE);      /* XCU divsqrt offset(y) vector */
	IA_CSS_XNR4_ARRAY(xcu_divsqrt_slope,  XNR4_LXCU_LUT_SIZE);      /* XCU divsqrt slope vector */
	IA_CSS_XNR4_ARRAY(xcu_divsqrt_x,      XNR4_LXCU_LUT_SIZE);      /* XCU divsqrt x points vector */
	IA_CSS_XNR4_ARRAY(range_weight_lut,   XNR4_BFA_RW_LUT_SIZE);    /* JBL range Weight LUT */
};

/*******************************************************************************
 * XNR4 Medium Frequency VMEM Parameters
 * For Video this structure is at ISP0 and ISP1 VMEM
 * For Still this structure is at ISP0 VMEM
 ******************************************************************************/
struct ia_css_xnr4m_vmem_param {
	IA_CSS_XNR4_ARRAY(lpf_mf_coef_y,      XNR4_BFA_KWAY); /* MF JBL Spatial coefs for Luma */
	IA_CSS_XNR4_ARRAY(lpf_mf_coef_c,      XNR4_BFA_KWAY); /* MF JBL Spatial coefs for Chroma */
	struct ia_css_xnr4_common_vmem_param common_param;
};

/*******************************************************************************
 * XNR4 High Frequency VMEM Parameters
 * For Still this structure is at ISP1 and ISP2 VMEM
 ******************************************************************************/
struct ia_css_xnr4h_vmem_param {
	struct ia_css_xnr4_common_vmem_param common_param;
	IA_CSS_XNR4_ARRAY(lpf_hf_coef_c,             XNR4_BFA_KWAY);          /* HF JBL Spatial coefs for UV */
	IA_CSS_XNR4_ARRAY(xcu_lcs_offset,            XNR4_XCU_LUT_SIZE);      /* XCU ed offset(y) vector */
	IA_CSS_XNR4_ARRAY(xcu_lcs_slope,             XNR4_XCU_LUT_SIZE);      /* XCU ed slope vector */
	IA_CSS_XNR4_ARRAY(xcu_lcs_x,                 XNR4_XCU_LUT_SIZE);      /* XCU ed x points vector */
	IA_CSS_XNR4_ARRAY(nncu_hf_luma_blend_offset, XNR4_HF_BLEND_LUT_SIZE); /* NNCU HF Y blend offset vec */
};
#endif /* __IA_CSS_XNR4_DEFS_H */
