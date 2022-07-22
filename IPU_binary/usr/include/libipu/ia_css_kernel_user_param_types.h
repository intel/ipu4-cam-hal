/*
* INTEL CONFIDENTIAL
*
* Copyright (C) 2017 - 2017 Intel Corporation.
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

#ifndef __IA_CSS_KERNEL_USER_PARAM_TYPES_H
#define __IA_CSS_KERNEL_USER_PARAM_TYPES_H

#include "type_support.h"

#define KERNEL_USER_PARAM_S_PADDING 4

#define SIZE_OF_KERNEL_USER_PARAM_S ( \
	  (1 * IA_CSS_UINT64_T_BITS) \
	+ (1 * IA_CSS_UINT32_T_BITS) \
	+ (4 * IA_CSS_UINT16_T_BITS) \
	+ (KERNEL_USER_PARAM_S_PADDING * IA_CSS_UINT8_T_BITS))

/** \brief A kernel user parameter object.
 * Always use ia_css_kernel_user_param_get_descriptor_size or
 * ia_css_kernel_user_param_get_descriptor_size_using_kernel_info to get the
 * appropriate size for the kernel user parameter object.
 *
 * Use an init function like ia_css_kernel_user_param_init to initialize
 * the kernel user parameter object.
 *
 * Never instantiate this struct in a different way.
 */
typedef struct ia_css_kernel_user_param_s {
	/* Parameter buffer handle attached to the terminal */
	union {
		void *p;
		uint64_t not_used;  /* ensures same struct layout on 64 and 32 bit compilations */
	} payload_buffer;
	/* Buffer size */
	uint32_t payload_buffer_size;
	/* Points to the variable array of ia_css_kernel_user_param_desc_s */
	uint16_t kernel_desc_offset;
	/* Number of kernels in program group */
	uint16_t kernel_count;
	/* Number of fragments in frame */
	uint16_t fragment_count;
	/* Fragment stride in bytes */
	uint16_t fragment_stride;
	/* padding */
	uint8_t padding[KERNEL_USER_PARAM_S_PADDING];
} ia_css_kernel_user_param_t;

typedef struct ia_css_kernel_user_param_kernel_desc_s {
	/* kernel identifier */
	uint32_t kernel_id;
	/* Points to the variable array of ia_css_kernel_user_param_cfg_desc_t */
	uint16_t cfg_desc_offset;
	/* Number of configurations for this kernel */
	uint16_t cfg_desc_count;
} ia_css_kernel_user_param_kernel_desc_t;

typedef struct ia_css_kernel_user_param_cfg_desc_s {
	/* Offset of the parameter allocation in memory */
	uint32_t mem_offset;
	/* Memory allocation size needs of this parameter */
	uint32_t mem_size;
} ia_css_kernel_user_param_cfg_desc_t;

#endif /* __IA_CSS_KERNEL_USER_PARAM_TYPES_H */
