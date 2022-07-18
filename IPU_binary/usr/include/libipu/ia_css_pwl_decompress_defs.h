/*
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

#ifndef __IA_CSS_PWL_DECOMPRESS_DEFS_H
#define __IA_CSS_PWL_DECOMPRESS_DEFS_H

/** @file
 * Contains PWL decompression runtime parameters
 **/

#include "type_support.h"
#include "math_support.h"

/*
 * WARNINGS
 * - "IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE" is designated to have a fixed value as "32".
 *   Please do not change it to a different value. It defines the number of elements per access
 *   in an array, in case of IPU4, it is 32.
 */
#define	IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE	(32)
/* Default uses 32-LUT */
#if !defined(PWL_DECOMPRESS_USE_64LUT)
#define PWL_DECOMPRESS_USE_64LUT		(0)
#endif
#define PWL_DECOMPRESS_LUT_BITS			(6)
#define PWL_DECOMPRESS_LUT_SIZE			(1 << PWL_DECOMPRESS_LUT_BITS)
#define PWL_DECOMPRESS_MAX_OUTPUT		((1 << (IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE - 1)) - 1)
#if !defined(PWL_DECOMPRESS_USE_OFFSET)
#define PWL_DECOMPRESS_USE_OFFSET		(0)
#endif

#if PWL_DECOMPRESS_USE_64LUT
#define PWL_DECOMPRESS_LUT_MAXIDX		((1 << (6)) - 1)
#else
#define PWL_DECOMPRESS_LUT_MAXIDX		((1 << (5)) - 1)
#endif

#ifdef __ISP
#define	IA_CSS_PWL_DECOMPRESS_ARRAY(x, s)	\
	tmemvectoru x[CEIL_DIV((s), IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_PWL_DECOMPRESS_ARRAY(x, s)	\
	uint16_t x[CEIL_DIV((s), IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE)][IA_CSS_PWL_DECOMPRESS_ARRAY_ACCESS_SIZE]
#endif

/*
 * PWL_DECOMPRESS parameter terminal sections
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_PWL_DECOMPRESS_SECTION_COUNT	(2)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_PWL_DECOMPRESS_MAX_SIZE_SECTION_0 \
					(sizeof(struct ia_css_pwl_decompress_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_PWL_DECOMPRESS_MAX_SIZE_SECTION_1 \
					(sizeof(struct ia_css_pwl_decompress_vmem_param))

/*
 * PWL_DECOMPRESS interface data structures
 *
 * The following operation is performed based to the fact to which segment the current input pixel belongs:
 *
 * output = (input - delta[i]) << shift[i] + offset[i]; i = 0..63
 *
 * The number of segments is hard-coded to up-to 64. Output is always in double-precision and the output range
 * can be further stretched when required. In order to determine a segment to which certain pixel belongs,
 * maximal input value and a difference between maximal supported input value and LUT size are needed.
 */
struct ia_css_pwl_decompress_dmem_param {
	uint32_t idx_shift;	/**< Bit depth difference between maximal supported input value and LUT size */
	uint32_t range_shift;   /**< Range adjustment shift for the final result */
	uint32_t use64;		/**< In FW use64 is not used added to match PAL and FW and FW does it at compile time*/
};

struct ia_css_pwl_decompress_vmem_param {
	IA_CSS_PWL_DECOMPRESS_ARRAY(delta_lut,  PWL_DECOMPRESS_LUT_SIZE);	/**< Input subtract value */
	IA_CSS_PWL_DECOMPRESS_ARRAY(mult_lut,   PWL_DECOMPRESS_LUT_SIZE);	/**< Shift coefficient */
	IA_CSS_PWL_DECOMPRESS_ARRAY(offset_lut, PWL_DECOMPRESS_LUT_SIZE);	/**< Output pixel offset */
};

#endif /* __IA_CSS_PWL_DECOMPRESS_DEFS_H */
