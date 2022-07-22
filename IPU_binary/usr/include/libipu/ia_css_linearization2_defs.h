/**
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2018 Intel Corporation.
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

#ifndef __IA_CSS_LINEARIZATION2_DEFS_H
#define __IA_CSS_LINEARIZATION2_DEFS_H

#include "type_support.h"
#include "math_support.h"

#define IA_CSS_PSYS_PARAMETER_TERMINAL_LINEARIZATION2_SECTION_COUNT      (2)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_LINEARIZATION2_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_linearization2_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_LINEARIZATION2_MAX_SIZE_SECTION_1 (sizeof(struct ia_css_linearization2_vmem_param))

#define IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE		(32)

#ifdef __ISP
#define IA_CSS_LINEARIZATION2_ARRAY(x, s)               tmemvectoru x[CEIL_DIV((s), IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE)]
#define IA_CSS_LINEARIZATION2_ARRAY_SIGNED(x, s)        tmemvectors x[CEIL_DIV((s), IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE)]
#else
#define IA_CSS_LINEARIZATION2_ARRAY(x, s)               uint16_t x[CEIL_DIV((s), IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE)][IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE]
#define IA_CSS_LINEARIZATION2_ARRAY_SIGNED(x, s)        int16_t x[CEIL_DIV((s), IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE)][IA_CSS_LINEARIZATION2_ARRAY_ACCESS_SIZE]
#endif

#define LINEARIZATION2_LUT_SIZE                         (128)
#define LINEARIZATION2_BASE_LEVEL_LENGTH                (7)
#define LINEARIZATION2_INPUT_BITS_PER_ELEMENT		(15)
#define LINEARIZATION2_LUT_BITS_PER_ELEMENT		(30)
#define LINEARIZATION2_INPUT_MIN_VALUE		        (0)
#define LINEARIZATION2_INPUT_MAX_VALUE		        ((1 << LINEARIZATION2_INPUT_BITS_PER_ELEMENT) - 1)
#define LINEARIZATION2_LUT_MIN_VALUE			(0)
#define LINEARIZATION2_LUT_MAX_VALUE			((1 << LINEARIZATION2_LUT_BITS_PER_ELEMENT) - 1)
#define LINEARIZATION2_SLOPE_MIN_VALUE			(-(1 << (LINEARIZATION2_LUT_BITS_PER_ELEMENT - 1)))
#define LINEARIZATION2_SLOPE_MAX_VALUE			(((1 << LINEARIZATION2_LUT_BITS_PER_ELEMENT) - 1) - 1)

struct ia_css_linearization2_dmem_param {
	uint16_t bypass;                                            /* Bypass linearization2 kernel */
	uint16_t padding;
};

struct ia_css_linearization2_vmem_param {
	IA_CSS_LINEARIZATION2_ARRAY(base_level,           LINEARIZATION2_BASE_LEVEL_LENGTH);   /* Base level */
	IA_CSS_LINEARIZATION2_ARRAY(start_bin,            LINEARIZATION2_BASE_LEVEL_LENGTH);   /* LUT offset */
	IA_CSS_LINEARIZATION2_ARRAY(step,                 LINEARIZATION2_BASE_LEVEL_LENGTH);   /* Log2 of step size of each stage  */
	IA_CSS_LINEARIZATION2_ARRAY(slope_resolution,     LINEARIZATION2_BASE_LEVEL_LENGTH);   /* Slope resolution of each stage */
	IA_CSS_LINEARIZATION2_ARRAY(lut_00_low,           LINEARIZATION2_LUT_SIZE);            /* Low halves of combined LUT for PWL, BLC, WB, Linearization for channel 00 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_00_high,          LINEARIZATION2_LUT_SIZE);            /* High halves of Combined LUT for PWL, BLC, WB, Linearization for channel 00 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_01_low,           LINEARIZATION2_LUT_SIZE);            /* Low halves of combined LUT for PWL, BLC, WB, Linearization for channel 01 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_01_high,          LINEARIZATION2_LUT_SIZE);            /* High halves of Combined LUT for PWL, BLC, WB, Linearization for channel 01 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_10_low,           LINEARIZATION2_LUT_SIZE);            /* Low halves of combined LUT for PWL, BLC, WB, Linearization for channel 10 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_10_high,          LINEARIZATION2_LUT_SIZE);            /* High halves of Combined LUT for PWL, BLC, WB, Linearization for channel 10 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_11_low,           LINEARIZATION2_LUT_SIZE);            /* Low halves of combined LUT for PWL, BLC, WB, Linearization for channel 11 */
	IA_CSS_LINEARIZATION2_ARRAY(lut_11_high,          LINEARIZATION2_LUT_SIZE);            /* High halves of Combined LUT for PWL, BLC, WB, Linearization for channel 11 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_00_low,         LINEARIZATION2_LUT_SIZE);            /* Low halves of slope vector for channel 00 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_00_high,        LINEARIZATION2_LUT_SIZE);            /* High halves of slope vector for channel 00 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_01_low,         LINEARIZATION2_LUT_SIZE);            /* Low halves of slope vector for channel 01 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_01_high,        LINEARIZATION2_LUT_SIZE);            /* High halves of slope vector for channel 01 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_10_low,         LINEARIZATION2_LUT_SIZE);            /* Low halves of slope vector for channel 10 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_10_high,        LINEARIZATION2_LUT_SIZE);            /* High halves of slope vector for channel 10 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_11_low,         LINEARIZATION2_LUT_SIZE);            /* Low halves of slope vector for channel 11 */
	IA_CSS_LINEARIZATION2_ARRAY(slope_11_high,        LINEARIZATION2_LUT_SIZE);            /* High halves of slope vector for channel 11 */
};

#endif /* __IA_CSS_LINEARIZATION2_DEFS_H */
