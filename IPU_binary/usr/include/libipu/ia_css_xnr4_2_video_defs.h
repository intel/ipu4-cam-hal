/**
* INTEL CONFIDENTIAL
*
* Copyright (C) 2016 - 2020 Intel Corporation.
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
#ifndef __IA_CSS_XNR4_2_VIDEO_DEFS_H
#define __IA_CSS_XNR4_2_VIDEO_DEFS_H

#include "ia_css_xnr4_defs.h"

/*******************************************************************************
 * XNR4_2 Video parameter terminal sections
 ******************************************************************************/
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_2_VIDEO_SECTION_COUNT      (4)

/* Both of these 2 sections are required for 2 ISPs */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_2_VIDEO_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_xnr4m_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_2_VIDEO_MAX_SIZE_SECTION_1 (sizeof(struct ia_css_xnr4m_vmem_param))
/* Below 2 sections are required */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_2_VIDEO_MAX_SIZE_SECTION_2 (sizeof(struct ia_css_xnr4_2_h_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_2_VIDEO_MAX_SIZE_SECTION_3 (sizeof(struct ia_css_xnr4_2_h_vmem_param))

/*******************************************************************************
 * XNR4_2 Video program terminal sections
 ******************************************************************************/
#define IA_CSS_PSYS_PROGRAM_TERMINAL_XNR4_2_VIDEO_SECTION_COUNT      (1)

/* Below section is required for 2 ISPs */
#define IA_CSS_PSYS_PROGRAM_TERMINAL_XNR4_2_VIDEO_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_xnr4_fragment_param))

/*******************************************************************************
 * XNR4_2 High Frequency DMEM Parameters
 * For Still this structure is at ISP1 and ISP2 DMEM
 ******************************************************************************/
struct ia_css_xnr4_2_h_dmem_param {
	struct ia_css_xnr4_common_dmem_param common_param;
	uint16_t xcu_lcs_exp;		/* XCU ed exponent */
	uint16_t xcu_lcs_slp_a_res;	/* XCU ed slope precision */
	uint16_t xcu_lcs_x_max;		/* XCU ed - maximal x value */
	int16_t  blend_hf_power_y;	/* HF denoise blending power luma */
	int16_t  blend_hf_power_c;	/* HF denoise blending power chroma */
	uint16_t padding;		/* Pad to align struct to 4 bytes */
};

/*******************************************************************************
 * XNR4_2 High Frequency VMEM Parameters
 * For Still this structure is at ISP1 and ISP2 VMEM
 ******************************************************************************/
struct ia_css_xnr4_2_h_vmem_param {
	struct ia_css_xnr4_common_vmem_param common_param;
	IA_CSS_XNR4_ARRAY(xcu_lcs_offset,            XNR4_XCU_LUT_SIZE);      /* XCU ed offset(y) vector */
	IA_CSS_XNR4_ARRAY(xcu_lcs_slope,             XNR4_XCU_LUT_SIZE);      /* XCU ed slope vector */
	IA_CSS_XNR4_ARRAY(xcu_lcs_x,                 XNR4_XCU_LUT_SIZE);      /* XCU ed x points vector */
};

#endif /* __IA_CSS_XNR4_2_VIDEO_DEFS_H */
