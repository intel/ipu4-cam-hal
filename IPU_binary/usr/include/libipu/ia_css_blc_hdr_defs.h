/*
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

#ifndef __IA_CSS_BLC_HDR_DEFS_H
#define __IA_CSS_BLC_HDR_DEFS_H

#include "type_support.h"

/*
 * BLC_HDR parameter terminal sections
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_BLC_HDR_SECTION_COUNT	(1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_BLC_HDR_MAX_SIZE_SECTION_0 \
					(sizeof(struct ia_css_blc_hdr_dmem_param))

/*
 * BLC_HDR interface data structures
 */
struct ia_css_blc_hdr_dmem_param {
	uint32_t bl_cc00;	/**< Black level for GR pixels, u0.31, [0, 2^31-1], ineffective 0 */
	uint32_t bl_cc01;		/**< Black level for R pixels, u0.31, [0, 2^31-1], ineffective 0 */
	uint32_t bl_cc10;		/**< Black level for B pixels, u0.31, [0, 2^31-1], ineffective 0 */
	uint32_t bl_cc11;	/**< Black level for GB pixels, u0.31, [0, 2^31-1]], ineffective 0 */
};

#endif /* __IA_CSS_BLC_HDR_DEFS_H */
