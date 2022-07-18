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

#include "Customized3ATypes.h"

namespace icamera {

typedef struct {
    /**
     * The version of current Custom 3A Module
     */
    int custom3AModuleVersion;
    /**
     * The supported algorithms defined in Custom3AType.
     * Such as if AE and AWB are supported only, it should be (CUSTOM_AE | CUSTOM_AWB);
     */
    int custom3ACapability;
    /**
     * Initialize the Custom 3A Module
     *
     * \return 0 if successful, otherwise return -1.
     */
    int (*init) (void);
    /**
     * Deinitialize the Custom 3A Module
     *
     * \return 0 if successful, otherwise return -1.
     */
    int (*deinit) (void);
    /**
     * Set sensor information
     *
     * \param[in] descriptor: struct ia_aiq_exposure_sensor_descriptor for sensor information.
     * \return 0 if successful, otherwise return -1.
     */
    int (*setSensorInfo) (const ia_aiq_exposure_sensor_descriptor& descriptor);
    /**
     * Updata 3a parameters
     *
     * \param[in] param: struct custom_3a_parameter_t for 3a parameter
     * \return 0 if successful, otherwise return -1.
     */
    int (*updateParameter) (const custom_3a_parameter_t& param);
    /**
     * Set statistics
     *
     * \param[in] ispStatistics: struct ia_aiq_statistics_input_params_v4 for statictics
     * \return 0 if successful, otherwise return -1.
     */
    int (*setStatistics) (const ia_aiq_statistics_input_params_v4 *ispStatistics);
    /**
     * Set GBCE results
     *
     * \param[in] gbceResults: struct ia_aiq_gbce_results for GBCE results
     * \return 0 if successful, otherwise return -1.
     */
    int (*setGbceResults) (const ia_aiq_gbce_results *gbceResults);
    /**
    * Set LTM parameters
    *
    * \param[in] ltmInputParams: struct ia_ltm_input_params for LTM input parameters
    * \param[in] drcParams: struct ia_ltm_drc_params for DRC parameters
    * \return 0 if successful, otherwise return -1.
    */
    int (*setLtmParams) (const ia_ltm_input_params *ltmInputParams,
                         const ia_ltm_drc_params *drcParams);
    /**
     * Run Ae algorithm
     *
     * \param[out] aeResults: struct ia_aiq_ae_results for AE results
     * \return 0 if successful, otherwise return -1.
     */
    int (*runAe) (ia_aiq_ae_results *aeResults);
    /**
     * Run Af algorithm
     *
     * \param[out] afResults: struct ia_aiq_af_results for AF results
     * \return 0 if successful, otherwise return -1.
     */
    int (*runAf) (ia_aiq_af_results *afResults);
    /**
     * Run Awb algorithm
     *
     * \param[out] awbResults: struct ia_aiq_awb_results for AWB results
     * \return 0 if successful, otherwise return -1.
     */
    int (*runAwb) (ia_aiq_awb_results *awbResults);
} Custom3AModule;

} /* namespace icamera */
