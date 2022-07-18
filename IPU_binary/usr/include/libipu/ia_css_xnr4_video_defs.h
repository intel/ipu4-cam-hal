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
#ifndef __IA_CSS_XNR4_VIDEO_DEFS_H
#define __IA_CSS_XNR4_VIDEO_DEFS_H

#include "ia_css_xnr4_defs.h"

/*******************************************************************************
 * XNR4 Video parameter terminal sections
 ******************************************************************************/
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_VIDEO_SECTION_COUNT      (2)

/* Both of these 2 sections are required for 2 ISPs */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_VIDEO_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_xnr4m_dmem_param))
#define IA_CSS_PSYS_PARAMETER_TERMINAL_XNR4_VIDEO_MAX_SIZE_SECTION_1 (sizeof(struct ia_css_xnr4m_vmem_param))

/*******************************************************************************
 * XNR4 Video program terminal sections
 ******************************************************************************/
#define IA_CSS_PSYS_PROGRAM_TERMINAL_XNR4_VIDEO_SECTION_COUNT      (1)

/* Below section is required for 2 ISPs */
#define IA_CSS_PSYS_PROGRAM_TERMINAL_XNR4_VIDEO_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_xnr4_fragment_param))

#endif /* __IA_CSS_XNR4_VIDEO_DEFS_H */
