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

#ifndef __IPU_DEVICE_ACB_DEVICES_H
#define __IPU_DEVICE_ACB_DEVICES_H

enum ipu_device_acb_id {
	/* PSA accelerators */
	IPU_DEVICE_ACB_WBA_ID      = 0,
	IPU_DEVICE_ACB_RYNR_ID,
	IPU_DEVICE_ACB_DEMOSAIC_ID,
	IPU_DEVICE_ACB_ACM_ID,
	IPU_DEVICE_ACB_GTC_ID,
	IPU_DEVICE_ACB_YUV1_ID,
	IPU_DEVICE_ACB_DVS_ID,
	IPU_DEVICE_ACB_LACE_ID,
	/* ISA accelerators */
	IPU_DEVICE_ACB_ICA_ID,
	IPU_DEVICE_ACB_LSC_ID,
	IPU_DEVICE_ACB_DPC_ID,
	IPU_DEVICE_ACB_IDS_ID,
	IPU_DEVICE_ACB_AWB_ID,
	IPU_DEVICE_ACB_AF_ID,
	IPU_DEVICE_ACB_AE_ID,
	IPU_DEVICE_ACB_NUM_ACB
};

#define IPU_DEVICE_ACB_NUM_PSA_ACB (IPU_DEVICE_ACB_LACE_ID + 1)
#define IPU_DEVICE_ACB_NUM_ISA_ACB \
	(IPU_DEVICE_ACB_NUM_ACB - IPU_DEVICE_ACB_NUM_PSA_ACB)

#endif /* __IPU_DEVICE_ACB_DEVICES_H */
