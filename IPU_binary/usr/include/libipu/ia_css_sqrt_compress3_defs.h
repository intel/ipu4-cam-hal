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

#ifndef __IA_CSS_SQRT_COMPRESS3_DEFS_H
#define __IA_CSS_SQRT_COMPRESS3_DEFS_H

#include "type_support.h"
#include "math_support.h"

#define IA_CSS_PSYS_PARAMETER_TERMINAL_SQRT_COMPRESS3_SECTION_COUNT      (2)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_SQRT_COMPRESS3_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_sqrt_compress3_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_SQRT_COMPRESS3_MAX_SIZE_SECTION_1 (sizeof(struct ia_css_sqrt_compress3_vmem_param))

#define IA_CSS_ISQRT_ARRAY_ACCESS_SIZE		       (32)

#ifdef __ISP
#define IA_CSS_ISQRT_ARRAY(x, s)                       tmemvectoru x[CEIL_DIV((s), IA_CSS_ISQRT_ARRAY_ACCESS_SIZE)]
#define IA_CSS_ISQRT_ARRAY_SIGNED(x, s)                tmemvectors x[CEIL_DIV((s), IA_CSS_ISQRT_ARRAY_ACCESS_SIZE)]
#else
#define IA_CSS_ISQRT_ARRAY(x, s)                       uint16_t x[CEIL_DIV((s), IA_CSS_ISQRT_ARRAY_ACCESS_SIZE)][IA_CSS_ISQRT_ARRAY_ACCESS_SIZE]
#define IA_CSS_ISQRT_ARRAY_SIGNED(x, s)                int16_t x[CEIL_DIV((s), IA_CSS_ISQRT_ARRAY_ACCESS_SIZE)][IA_CSS_ISQRT_ARRAY_ACCESS_SIZE]
#endif

#define ISQRT_LUT_SIZE                          (128)
#define ISQRT_INPUT_BITS_PER_ELEMENT            (31)
#define ISQRT_OFFSET_BITS_PER_ELEMENT           (31)
#define ISQRT_LUT_RESOLUTION                    (30)
#define ISQRT_SLOPE_BITS_PER_ELEMENT            (15)
#define ISQRT_INPUT_MIN_VALUE                   (0)
#define ISQRT_INPUT_MAX_VALUE                   (~(1 << ISQRT_INPUT_BITS_PER_ELEMENT))
#define ISQRT_OFFSET_MIN_VALUE                  (0)
#define ISQRT_OFFSET_MAX_VALUE                  ((1 << (ISQRT_OFFSET_BITS_PER_ELEMENT-1))-1)
#define ISQRT_SLOPE_MIN_VALUE                   -(1 << ISQRT_SLOPE_BITS_PER_ELEMENT)
#define ISQRT_SLOPE_MAX_VALUE                   ((1 << ISQRT_SLOPE_BITS_PER_ELEMENT) - 1)
#define ISQRT_LUT_BITS_PER_ELEMENT              (15)
#define ISQRT_LUT_MIN_VALUE                     (0)
#define ISQRT_LUT_MAX_VALUE                     ((1 << ISQRT_LUT_BITS_PER_ELEMENT) - 1)
#define ISQRT_X_CORD_MIN_VALUE                  (0)
#define ISQRT_X_CORD_MAX_VALUE                  (~(1 << ISQRT_INPUT_BITS_PER_ELEMENT))

struct ia_css_sqrt_compress3_dmem_param {
	uint16_t bypass;                                            /* Bypass sqrt_compress3 kernel */
	uint16_t isqrt_exponent;
	int32_t  isqrt_x_cord_max;
	uint16_t isqrt_resolution;
	uint16_t padding;
};

struct ia_css_sqrt_compress3_vmem_param {
	IA_CSS_ISQRT_ARRAY(isqrt_offset_low,  ISQRT_LUT_SIZE);      /* Low halves of XCU offset for inverse SQRT LUT */
	IA_CSS_ISQRT_ARRAY(isqrt_offset_high, ISQRT_LUT_SIZE);      /* High halves of XCU offset for inverse SQRT LUT */
	IA_CSS_ISQRT_ARRAY(isqrt_slope,       ISQRT_LUT_SIZE);      /* XCU slope for inverse SQRT LUT */
	IA_CSS_ISQRT_ARRAY(isqrt_x_cord_low,  ISQRT_LUT_SIZE);      /* Low halves of XCU X coordinate for invserse SQRT LUT */
	IA_CSS_ISQRT_ARRAY(isqrt_x_cord_high, ISQRT_LUT_SIZE);      /* high halves of XCU X coordinate for inverse SQRT LUT */
};

#endif /* __IA_CSS_SQRT_COMPRESS3_DEFS_H */
