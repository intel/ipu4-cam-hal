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

#ifndef __IA_CSS_TERMINAL_MANIFEST_BASE_TYPES_H
#define __IA_CSS_TERMINAL_MANIFEST_BASE_TYPES_H

#include "ia_css_terminal_defs.h"

#define N_PADDING_UINT8_IN_TERMINAL_MAN_STRUCT	5
#define SIZE_OF_TERMINAL_MANIFEST_STRUCT_IN_BITS \
	(IA_CSS_UINT16_T_BITS \
	+ IA_CSS_TERMINAL_ID_BITS \
	+ IA_CSS_TERMINAL_TYPE_BITS \
	+ IA_CSS_UINT32_T_BITS \
	+ (N_PADDING_UINT8_IN_TERMINAL_MAN_STRUCT*IA_CSS_UINT8_T_BITS))

/* ==================== Base Terminal Manifest - START ==================== */
struct ia_css_terminal_manifest_s {
	ia_css_terminal_type_t				terminal_type;		/**< Type ia_css_terminal_type_t */
	int16_t						parent_offset;		/**< Offset to the program group manifest */
	uint16_t					size;			/**< Size of this whole terminal-manifest layout-structure */
	ia_css_terminal_ID_t				ID;
	uint8_t						padding[N_PADDING_UINT8_IN_TERMINAL_MAN_STRUCT];
};

typedef struct ia_css_terminal_manifest_s
	ia_css_terminal_manifest_t;

/* ==================== Base Terminal Manifest - END ==================== */

#endif /* __IA_CSS_TERMINAL_MANIFEST_BASE_TYPES_H */

