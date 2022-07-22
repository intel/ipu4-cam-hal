/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2018 Intel Corporation. All Rights Reserved.
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

#ifndef BURSTISP_STAGE_H_
#define BURSTISP_STAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ia_cipf/ia_cipf_stage.h>
#include <ia_cipf_common.h>

#define burstisp_stage_id               100
#define burstisp_stage_uid              ia_cipf_external_stage_uid(burstisp_stage_id)
#define burstisp_input_terminal_uid     (burstisp_stage_uid + 1)
#define burstisp_output_terminal_uid    (burstisp_stage_uid + 2)
#define burstisp_param_in_terminal_uid  (burstisp_stage_uid + 3)

#define burstisp_stage_buffer_release_property_uid ia_fourcc('B','I','B','R')
#define burstisp_stage_tuning_params_property_uid  (burstisp_stage_buffer_release_property_uid + 1)

ia_cipf_stage_t* burstisp_stage_create(void);

// For passing the buffer release function ptr to the stage
typedef struct
{
    void (*buffer_release_cb)(void *ctx);
    void *release_cb_ctx;
} burstisp_release_property_t;

#ifdef __cplusplus
}
#endif

#endif
