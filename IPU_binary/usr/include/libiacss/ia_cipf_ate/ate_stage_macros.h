/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2017-2018 Intel Corporation. All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its suppliers
 * or licensors. Title to the Material remains with Intel Corporation or its
 * suppliers and licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and licensors. The
 * Material is protected by worldwide copyright and trade secret laws and
 * treaty provisions. No part of the Material may be used, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by Intel in writing.
 */

#ifndef IA_CAMERA_CIPF_ATE_CMAKEFILES_ATE_STAGE_MACROS_H_
#define IA_CAMERA_CIPF_ATE_CMAKEFILES_ATE_STAGE_MACROS_H_

#define UNUSED(x) (void)(x)

/* Avoid includes in this header, hence the fourcc define below. For now this
 * header can be safely exported and included. */
#define ate_fourcc(a, b, c, d) ((uint32_t)(d) | ((uint32_t)(c) << 8) \
                              | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

#define pg_id_to_ate_stage_uid(id) ate_fourcc(((id & 0xFF00) >> 8),id,'A','S')
#define ate_pg_id_from_uid(uid) ((uid & 0xFFFF0000) >> 16)
#define is_ate_stage_uid(uid) ((uid & 0x0000FFFF) == pg_id_to_ate_stage_uid(0))
#define ate_stage_simulation_property_uid ate_fourcc('A','S','S','P')
#define ate_stage_pgid_property_uid (ate_stage_simulation_property_uid + 1)
#define ate_stage_gc_property_uid (ate_stage_simulation_property_uid + 2)
#define ate_stage_controls_property_uid (ate_stage_simulation_property_uid + 3)

// Normally ate_video_pre_gdc_pg_id should come from manifest
// since ATE doesn't have manifest it is hard coded here
#define ate_video_pre_gdc_pg_id               185 // Needs to match to the pg_id in the graph descriptor
#define ate_video_pre_gdc_uid                 pg_id_to_ate_stage_uid(ate_video_pre_gdc_pg_id)

#endif /* IA_CAMERA_CIPF_ATE_CMAKEFILES_ATE_STAGE_MACROS_H_ */
