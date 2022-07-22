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

#ifndef __IPU_DEVICE_GP_DEVICES_H
#define __IPU_DEVICE_GP_DEVICES_H
#include "math_support.h"
#include "type_support.h"

enum ipu_device_gp_id {
	IPU_DEVICE_GP_PSA = 0,	/* PSA */
	IPU_DEVICE_GP_ISA_STATIC,		/* ISA Static */
	IPU_DEVICE_GP_ISA_RUNTIME,		/* ISA Runtime */
	IPU_DEVICE_GP_ISL,		/* ISL */
	IPU_DEVICE_GP_NUM_GP
};

enum ipu_device_gp_psa_mux_id {
	/* Post RYNR/CCN: 0-To ACM (Video), 1-To Demosaic (Stills)*/
	IPU_DEVICE_GP_PSA_STILLS_MODE_MUX = 0,
	/* Post Vec2Str 4: 0-To GTC, 1-To ACM  */
	IPU_DEVICE_GP_PSA_V2S_RGB_4_DEMUX,
	/* Post DM and pre ACM	0-CCM/ACM: 1-DM Component Splitter */
	IPU_DEVICE_GP_PSA_S2V_RGB_F_MUX,
	/* Pre ACM/CCM: 0-To CCM/ACM, 1-To str2vec id_f */
	IPU_DEVICE_GP_PSA_ACM_DEMUX,
	IPU_DEVICE_GP_PSA_MUX_NUM_MUX
};

enum ipu_device_gp_isa_static_mux_id {
	IPU_DEVICE_GP_ISA_STATIC_MUX_SEL = 0,
	IPU_DEVICE_GP_ISA_STATIC_PORTA_BLK,
	IPU_DEVICE_GP_ISA_STATIC_PORTB_BLK,
	IPU_DEVICE_GP_ISA_STATIC_PORTC_BLK,
	IPU_DEVICE_GP_ISA_STATIC_AWB_MUX_SEL,
	IPU_DEVICE_GP_ISA_STATIC_AWB_MUX_INPUT_CORR_PORT_BLK,
	IPU_DEVICE_GP_ISA_STATIC_AWB_MUX_DPC_PORT_BLK,
	IPU_DEVICE_GP_ISA_STATIC_MUX_NUM_MUX
};

enum ipu_device_gp_isa_runtime_mux_id {
	IPU_DEVICE_GP_ISA_RUNTIME_FRAME_SIZE = 0,
	IPU_DEVICE_GP_ISA_RUNTIME_SCALED_FRAME_SIZE,
	IPU_DEVICE_GP_ISA_RUNTIME_MUX_NUM_MUX
};

enum ipu_device_gp_isl_mux_id {
	IPU_DEVICE_GP_ISL_MIPI_BE_MUX = 0,
	IPU_DEVICE_GP_ISL_MUX_NUM_MUX
};

#define IPU_DEVICE_GP_MAX_NUM MAX4((uint32_t)IPU_DEVICE_GP_PSA_MUX_NUM_MUX, \
	(uint32_t)IPU_DEVICE_GP_ISA_STATIC_MUX_NUM_MUX,                     \
	(uint32_t)IPU_DEVICE_GP_ISA_RUNTIME_MUX_NUM_MUX,                    \
	(uint32_t)IPU_DEVICE_GP_ISL_MUX_NUM_MUX)

#endif /* __IPU_DEVICE_GP_DEVICES_H */
