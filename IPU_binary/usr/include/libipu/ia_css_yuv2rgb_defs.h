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

#ifndef __IA_CSS_YUV2RGB_DEFS_H
#define __IA_CSS_YUV2RGB_DEFS_H

#include "type_support.h"

/** @file
 * Contains TNR5_2_1 run time parameters
 **/

#include "math_support.h" /* for CEIL_DIV   */

/*
 * WARNINGS
 * - "IA_CSS_YUV2RGB_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32". Please do not change
 *   it to a different value.
 */
#define	IA_CSS_YUV2RGB_ARRAY_ACCESS_SIZE		32 /* 32 elements per access in an array */

/* dmem params */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_YUV2RGB_SECTION_COUNT          (1)

#define IA_CSS_PSYS_PARAMETER_TERMINAL_YUV2RGB_MAX_SIZE_SECTION_0     (sizeof(struct ia_css_yuv2rgb_dmem_param))

/* DMEM Parameters */
struct ia_css_yuv2rgb_dmem_param {
	unsigned int coef_shift; /** < coef_shift to avoid overflow */
	int32_t yuv_calibration[3]; /** < yuv calibration value to restore Y, U and V */
	int32_t yuv2rgb_coef[9]; /** < yuv2rgb_coef: 3 coef for R, G and B each */
};

#endif /* __IA_CSS_YUV2RGB_DEFS_H */
