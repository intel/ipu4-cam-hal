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

#ifndef __IA_CSS_WB_HDR_DEFS_H
#define __IA_CSS_WB_HDR_DEFS_H

#include "type_support.h"

/*
 * WB_HDR parameter terminal sections
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_WB_HDR_SECTION_COUNT	(1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_WB_HDR_MAX_SIZE_SECTION_0 \
					(sizeof(struct ia_css_wb_hdr_dmem_param))

/*
 * WB_HDR interface data structures
 */
struct ia_css_wb_hdr_dmem_param {
	int32_t gain_gr;	/**< Gain for GR pixels, */
	int32_t gain_r;         /**< Gain for R pixels, */
	int32_t gain_b;         /**< Gain for B pixels,  */
	int32_t gain_gb;        /**< Gain for GB pixels,  */
	int32_t gain_shift_gr;    /**< Shift for gain precision adjustment, */
	int32_t gain_shift_r;     /**< Shift for gain precision adjustment, */
	int32_t gain_shift_b;     /**< Shift for gain precision adjustment, */
	int32_t gain_shift_gb;    /**< Shift for gain precision adjustment, */
	int32_t clipval;
};

#endif /* __IA_CSS_WB_HDR_DEFS_H */
