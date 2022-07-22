/*
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
#ifndef __IA_CSS_EXY_DEFS_H
#define __IA_CSS_EXY_DEFS_H

#include <type_support.h>

/*******************************************************************************
 * EXY parameter terminal sections
 ******************************************************************************/
#define IA_CSS_PSYS_PARAMETER_TERMINAL_EXY_SECTION_COUNT      (1)

#define IA_CSS_PSYS_PARAMETER_TERMINAL_EXY_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_isp_exy_params))

struct ia_css_isp_exy_params {
	uint16_t lpf_coef[2];
	uint16_t ns_shift;
	uint16_t padding;
};

#endif /* __IA_CSS_EXY_DEFS_H */
