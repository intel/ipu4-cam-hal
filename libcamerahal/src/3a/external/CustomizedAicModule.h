/*
 * Copyright (C) 2016-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "ia_aiq.h"
#include "ia_isp_types.h"

#include "CustomizedAicTypes.h"

namespace icamera {

typedef struct {
    /**
     * The version of current Custom Aic Module
     */
    int customAicModuleVersion;
    /**
     * Initialize the Custom Aic Module
     *
     * \return 0 if successful, otherwise return -1.
     */
    int (*init) (void);
    /**
     * Deinitialize the Custom Aic Module
     *
     * \return 0 if successful, otherwise return -1.
     */
    int (*deinit) (void);
    /**
     * Set Custom Aic Parameters
     *
     * \param[in] customAicParam: the Custom Aic Parameters.
     * \return 0 if successful, otherwise return -1.
     */
    int (*setAicParam) (const CustomAicParam &customAicParam);
    /**
     * Run Custom Aic Module
     *
     * \param[in] ae_results: struct ia_aiq_ae_results gotten from AE algo
     * \param[in] awb_results: struct ia_aiq_awb_results gotten from AWB algo
     * \param[out] custom_controls: struct ia_isp_custom_controls
     * \param[out] pipe: enum CustomAicPipe
     * \return 0 if successful, otherwise return -1.
     */
    int (*runExternalAic) (const ia_aiq_ae_results &ae_results,
                           const ia_aiq_awb_results &awb_results,
                           ia_isp_custom_controls *custom_controls,
                           CustomAicPipe *pipe);
} CustomAicModule;

} /* namespace icamera */
