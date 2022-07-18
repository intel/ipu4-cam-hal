/*
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2018 - 2018 Intel Corporation.
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

#ifndef __IA_CSS_DRC2_DEFS_H
#define __IA_CSS_DRC2_DEFS_H

#include "type_support.h"
#include "math_support.h"

#define DRC2_GTM_LUT_SIZE               (32)
#define DRC2_GCE_LUT_SIZE               (64)
/* IA_CSS_DRC2_ARRAY_ACCESS_SIZE is designated to have fixed value 32 ( = ISP_NWAY). Please do not change it */
#define IA_CSS_DRC2_ARRAY_ACCESS_SIZE   (32)

#ifdef __ISP
#define IA_CSS_DRC2_ARRAY(x, s) tmemvectors x[CEIL_DIV((s), IA_CSS_DRC2_ARRAY_ACCESS_SIZE)]
#else
#define IA_CSS_DRC2_ARRAY(x, s) int16_t x[CEIL_DIV((s), IA_CSS_DRC2_ARRAY_ACCESS_SIZE)][IA_CSS_DRC2_ARRAY_ACCESS_SIZE]
#endif


/*****************************************************************************
 * DRC2 parameter terminal sections
 *****************************************************************************/
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC2_SECTION_COUNT       (2)
/* Below two sections are meant for both PROG0 and PROG1 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC2_MAX_SIZE_SECTION_0  (sizeof(struct ia_css_drc2_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_DRC2_MAX_SIZE_SECTION_1  (sizeof(struct ia_css_drc2_vmem_param))

/******************************************************************************
 * DRC2 DMEM parameters
 ******************************************************************************/
struct ia_css_drc2_dmem_param {
	int16_t bypass;               /**< Bypass filter                      */
	int16_t gtm_resolution;       /**< GTM LXCU slope resolution          */
	int16_t gtm_x_cord_max;       /**< GTM LXCU maximum x                 */
	int16_t gtm_exponent;         /**< GTM LXCU exponent                  */
	int16_t gce_resolution;       /**< GCE-Defog XCU resolution           */
	int16_t gce_defog_x_cord_max; /**< GCE-Defog XCU maximum x            */
	int16_t gce_defog_exponent;   /**< GCE-Defog XCU exponent             */
	int16_t sharp_amount_l;       /**< Large sharpening strength          */
	int16_t sharp_max_l;          /**< Large maximum sharpening           */
	int16_t sharp_thr_l;          /**< Large sharpening threshold         */
	int16_t sharp_amount_s;       /**< Small sharpening strength          */
	int16_t sharp_max_s;          /**< Small maximum sharpening           */
	int16_t sharp_thr_s;          /**< Small sharpening threshold         */
	int16_t ltm_enable;           /**< Enable LTM                         */
	int16_t ltm_weight;           /**< LTM weight                         */
	int16_t padding_bytes;        /**< Padding to have size multiple of 4 */

};

/******************************************************************************
 * DRC2 VMEM parameters
 ******************************************************************************/
struct ia_css_drc2_vmem_param {
	IA_CSS_DRC2_ARRAY(gtm_slope,  DRC2_GTM_LUT_SIZE);       /**< GTM LXCU slope             */
	IA_CSS_DRC2_ARRAY(gtm_offset, DRC2_GTM_LUT_SIZE);       /**< GTM LXCU offset            */
	IA_CSS_DRC2_ARRAY(gtm_x_cord, DRC2_GTM_LUT_SIZE);       /**< GTM LXCU x coordinate      */
	IA_CSS_DRC2_ARRAY(gce_defog_slope,  DRC2_GCE_LUT_SIZE); /**< GCE-Defog XCU slope        */
	IA_CSS_DRC2_ARRAY(gce_defog_offset, DRC2_GCE_LUT_SIZE); /**< GCE-Defog XCU offset       */
	IA_CSS_DRC2_ARRAY(gce_defog_xcord,  DRC2_GCE_LUT_SIZE); /**< GCE-Defog XCU x coordinate */
};

#endif /* __IA_CSS_DRC2_DEFS_H */

