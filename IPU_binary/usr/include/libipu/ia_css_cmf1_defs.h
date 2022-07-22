/**
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2020 Intel Corporation.
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

#ifndef __IA_CSS_CMF1_DEFS_H
#define __IA_CSS_CMF1_DEFS_H

#include "type_support.h"

#define IA_CSS_PSYS_PARAMETER_TERMINAL_CMF1_SECTION_COUNT      (1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_CMF1_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_cmf1_dmem_param))

/**
 * CMF DMEM parameters.
 * This struct needs to be aligned to 4 bytes.
 */
struct ia_css_cmf1_dmem_param {
	int16_t bypass;                   /* Bypass Chroma MF filter. */
	int16_t blend_power;              /* Blending weight. */
};

#endif /* __IA_CSS_CMF1_DEFS_H */
