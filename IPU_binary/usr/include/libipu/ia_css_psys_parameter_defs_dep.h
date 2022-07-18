/*
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2015 - 2020 Intel Corporation.
 * All Rights Reserved.
 *
 * The source code contained or described herein and all documents
 * related to the source code ("Material") are owned by Intel Corporation
 * or licensors. Title to the Material remains with Intel Corporation
 * or its licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its licensors. The Material
 * is protected by worldwide copyright and trace secret laws and treaty
 * provisions. No part of the Material may beused, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel's prior express written permission.
 *
 * No License under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or
 * delivery of the Materials, either expressly, by implication, inducement,
 * estoppel or otherwise. Any license under such intellectual property rights
 * must be express and approved by Intel in writing.
 *
 */

#ifndef __IA_CSS_PSYS_PARAMETER_DEFS_DEP_H
#define __IA_CSS_PSYS_PARAMETER_DEFS_DEP_H

#include "ia_css_isa_parameter_defs_dep.h"
#include "ia_css_psa_parameter_defs_dep.h"

/* Please be aware that below enums are not released userspace
 * The only reason to still have them is that test application
 * and the bxt_pss code has dependency to these enums
 */

enum ia_css_psys_kernel_id {
	IA_CSS_PSYS_KERNEL_ID_WBA_WBA,
	IA_CSS_PSYS_KERNEL_ID_RYNR_SPLITTER,
	IA_CSS_PSYS_KERNEL_ID_RYNR_COLLECTOR,
	IA_CSS_PSYS_KERNEL_ID_RYNR_BNLM,
	IA_CSS_PSYS_KERNEL_ID_RYNR_VCUD,
	IA_CSS_PSYS_KERNEL_ID_DEMOSAIC_DEMOSAIC,
	IA_CSS_PSYS_KERNEL_ID_ACM_CCM,
	IA_CSS_PSYS_KERNEL_ID_ACM_ACM,
	IA_CSS_PSYS_KERNEL_ID_GTC_CSC_CDS,
	IA_CSS_PSYS_KERNEL_ID_GTC_GTM,
	IA_CSS_PSYS_KERNEL_ID_YUV1_SPLITTER,
	IA_CSS_PSYS_KERNEL_ID_YUV1_IEFD,
	IA_CSS_PSYS_KERNEL_ID_YUV1_YDS,
	IA_CSS_PSYS_KERNEL_ID_YUV1_TCC,
	IA_CSS_PSYS_KERNEL_ID_DVS_YBIN,
	IA_CSS_PSYS_KERNEL_ID_DVS_DVS,
	IA_CSS_PSYS_KERNEL_ID_DVS_IN_L0,
	IA_CSS_PSYS_KERNEL_ID_DVS_IN_L1,
	IA_CSS_PSYS_KERNEL_ID_DVS_IN_L2,
	IA_CSS_PSYS_KERNEL_ID_DVS_OUT_L0,
	IA_CSS_PSYS_KERNEL_ID_DVS_OUT_L1,
	IA_CSS_PSYS_KERNEL_ID_DVS_OUT_L2,
	IA_CSS_PSYS_KERNEL_ID_LACE_LACE,
	IA_CSS_PSYS_KERNEL_ID_OFS_OF,
	IA_CSS_PSYS_KERNEL_ID_OFS_SC0,
	IA_CSS_PSYS_KERNEL_ID_OFS_SC1,
	IA_CSS_PSYS_KERNEL_ID_OFS_ISP,
	N_IA_CSS_PSYS_KERNEL_ID
};


enum ia_css_psys_acc_id {
	IA_CSS_PSYS_ACC_ID_WBA,
	IA_CSS_PSYS_ACC_ID_RYNR,
	IA_CSS_PSYS_ACC_ID_DEMOSAIC,
	IA_CSS_PSYS_ACC_ID_ACM,
	IA_CSS_PSYS_ACC_ID_GTC,
	IA_CSS_PSYS_ACC_ID_YUV1,
	IA_CSS_PSYS_ACC_ID_DVS,
	IA_CSS_PSYS_ACC_ID_LACE,
	IA_CSS_PSYS_ACC_ID_OFS,
	N_IA_CSS_PSYS_ACC_ID
};

#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_RYNR_SPLITTER                    (IA_CSS_PSYS_ACC_ID_RYNR)
#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_RYNR_COLLECTOR                   (IA_CSS_PSYS_ACC_ID_RYNR)
#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_RYNR_BNLM                        (IA_CSS_PSYS_ACC_ID_RYNR)
#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_RYNR_VCUD                        (IA_CSS_PSYS_ACC_ID_RYNR)
#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_ACM_CCM                          (IA_CSS_PSYS_ACC_ID_ACM)
#define IA_CSS_PSYS_KERNEL_BELONGS_TO_ACC_ACM_ACM                          (IA_CSS_PSYS_ACC_ID_ACM)

/*END of section that is referred by "Please be aware that below enums are not released userspace" comment. */

#endif /* __IA_CSS_PSYS_PARAMETER_DEFS_DEP_H */
