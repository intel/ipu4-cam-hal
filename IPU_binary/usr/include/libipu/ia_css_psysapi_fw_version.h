/*
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2014 - 2017 Intel Corporation.
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

#ifndef __IA_CSS_PSYSAPI_FW_VERSION_H
#define __IA_CSS_PSYSAPI_FW_VERSION_H

/* PSYSAPI FW VERSION is taken from Makefile for FW tests */
#define BXT_FW_RELEASE_VERSION PSYS_FIRMWARE_VERSION

enum ia_css_process_group_protocol_version {
	/*
	 * Legacy protocol
	 */
	IA_CSS_PROCESS_GROUP_PROTOCOL_LEGACY = 0,
	/*
	 * Persistent process group support protocol
	 */
	IA_CSS_PROCESS_GROUP_PROTOCOL_PPG,
	IA_CSS_PROCESS_GROUP_N_PROTOCOLS
};

#endif /* __IA_CSS_PSYSAPI_FW_VERSION_H */
