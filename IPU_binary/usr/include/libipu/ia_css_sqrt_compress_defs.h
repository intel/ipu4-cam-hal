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

#ifndef __IA_CSS_SQRT_COMPRESS_DEFS_H
#define __IA_CSS_SQRT_COMPRESS_DEFS_H

#include <type_support.h>
#include <math_support.h>

/*
 * WARNINGS
 * - "IA_CSS_SQRT_COMPRESS_ARRAY_ACCESS_SIZE" is designated to have a fixed value as "32".
 *   Please do not change it to a different value. It defines the number of elements per access
 *   in an array, in case of IPU4, it is 32.
 */
#define	IA_CSS_SQRT_COMPRESS_ARRAY_ACCESS_SIZE	(32)

#ifdef __ISP
#define	IA_CSS_SQRT_COMPRESS_ARRAY(x, s)	\
	tmemvectoru x[CEIL_DIV((s), IA_CSS_SQRT_COMPRESS_ARRAY_ACCESS_SIZE)]
#else
#define	IA_CSS_SQRT_COMPRESS_ARRAY(x, s)	\
	uint16_t x[CEIL_DIV((s), IA_CSS_SQRT_COMPRESS_ARRAY_ACCESS_SIZE)][IA_CSS_SQRT_COMPRESS_ARRAY_ACCESS_SIZE]
#endif

/*
 * SQRT_COMPRESS parameter terminal sections
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_SQRT_COMPRESS_SECTION_COUNT	(1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_SQRT_COMPRESS_MAX_SIZE_SECTION_0 \
					(sizeof(struct ia_css_sqrt_compress_dmem_param))

struct ia_css_sqrt_compress_dmem_param {
	uint32_t sqrt_compress_disable;	/**< disable SQRT_COMPRESS */
};

#endif /* __IA_CSS_SQRT_COMPRESS_DEFS_H */

