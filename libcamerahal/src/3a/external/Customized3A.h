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

#include "AiqSetting.h"

#include "ImagingControl.h"

#include "Customized3ATypes.h"
#include "Customized3AModule.h"

namespace icamera {

/*
 * \class Customized3A
 * This class is used to set parameter and sensor information to customized 3A.
 * It also set statictics to 3A and run Ae, Af and Awb.
 */
class Customized3A : public ImagingControl {

public:
    Customized3A(int cameraId);
    ~Customized3A();

    // init / deinit
    int init();
    int deinit();

    // set sensor info
    int setSensorInfo(const ia_aiq_exposure_sensor_descriptor& descriptor);

    // update 3a parameters
    int updateParameter(const aiq_parameter_t& param);

    // set statistics
    int setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics);

    // set GBCE results
    int setGbceResults(const ia_aiq_gbce_results *gbceResults);

    // set LTM parameters
    int setLtmParams(const ia_ltm_input_params *ltmInputParams,
                     const ia_ltm_drc_params *drcParams);

    // Calculate specified 3A algorithm types
    int run(AiqResult *aiqResult, int algoType);

    // get the supported custom 3A types.
    int getSupportedAlgoType();

private:
    // ae, af, awb interfaces
    int runAe(ia_aiq_ae_results *aeResults);
    int runAf(ia_aiq_af_results *afResults);
    int runAwb(ia_aiq_awb_results *awbResults);

    // check if Algo supported or not. If supported, return true.
    bool isCustomAlgoSupported(Custom3AType type);

    void convertAiqParamToCustomParam(const aiq_parameter_t& aiqParam,
                                      custom_3a_parameter_t *custom3AParam);

private:
    int mCameraId;
    void* mCustomized3AModuleHandle;
    Custom3AModule* mCustom3AModule;

    enum Customized3AState {
        CUSTOM_3A_NOT_INIT = 0,
        CUSTOM_3A_INIT
    } mCustomized3AState;
};

} /* namespace icamera */
