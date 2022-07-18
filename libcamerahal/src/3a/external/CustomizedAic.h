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

#include "CustomizedAicTypes.h"
#include "CustomizedAicModule.h"

namespace icamera {
/*
 * \interface class CustomizedAic
 * This is an interface class for customized Aic.
 */
class CustomizedAic : public ImagingControl {

public:
    CustomizedAic(int cameraId);
    ~CustomizedAic();

    int init();
    int deinit();
    int updateParameter(const aiq_parameter_t &param);
    int run(AiqResult *aiqResult, int algoType);

private:
    int runExternalAic(const ia_aiq_ae_results &ae_results,
                       const ia_aiq_awb_results &awb_results,
                       ia_isp_custom_controls *custom_controls,
                       CustomAicPipe *pipe);

private:
    int mCameraId;
    void* mCustomizedAicModuleHandle;
    CustomAicModule* mCustomAicModule;

    enum CustomizedAicState {
        CUSTOM_AIC_NOT_INIT = 0,
        CUSTOM_AIC_INIT
    } mCustomizedAicState;
};

} /* namespace icamera */
