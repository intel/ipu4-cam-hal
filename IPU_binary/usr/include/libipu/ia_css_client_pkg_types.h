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

#ifndef __IA_CSS_CLIENT_PKG_TYPES_H
#define __IA_CSS_CLIENT_PKG_TYPES_H

#include "type_support.h"

typedef void ia_css_client_pkg_t;

struct ia_css_client_pkg_header_s {
	uint32_t prog_list_offset;
	uint32_t prog_list_size;
	uint32_t prog_desc_offset;
	uint32_t prog_desc_size;
	uint32_t pg_manifest_offset;
	uint32_t pg_manifest_size;
	uint32_t prog_bin_offset;
	uint32_t prog_bin_size;
};

struct ia_css_client_pkg_prog_s {
	uint32_t prog_id;
	uint32_t prog_offset;
	uint32_t prog_size;
};

struct ia_css_client_pkg_prog_list_s {
	uint32_t prog_desc_count;
	uint32_t prog_bin_count;
};

#endif /* __IA_CSS_CLIENT_PKG_TYPES_H */
