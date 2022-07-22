/*
 * Copyright (C) 2015-2018 Intel Corporation.
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

#include "AiqSetting.h"

#include "AiqPlus.h"

#include "ImagingControl.h"
#include "Intel3AParameter.h"
#include "Intel3AResult.h"

namespace icamera {

/*
 * \class Intel3A
 * This class is used to set parameter and sensor information to
 * related parameter class. It also set statictics to 3A and run Ae,
 * Af and Awb.
 */
class Intel3A : public ImagingControl {

public:
    Intel3A(int cameraId, AiqPlus *aiqPlus);
    ~Intel3A();

    // init / deinit
    int init();
    int deinit();

    // set sensor info
    int setSensorInfo(const ia_aiq_exposure_sensor_descriptor &descriptor);

    // update 3a parameters
    int updateParameter(const aiq_parameter_t &param);

    // Calculate specified 3A algorithm types
    int run(AiqResult *aiqResult, int algoType);

private:
    Intel3A(const Intel3A& other);
    Intel3A& operator=(const Intel3A& other);

    // ae, af, awb interfaces
    int runAe(ia_aiq_ae_results *aeResults);
    int runAf(ia_aiq_af_results *afResults);
    int runAwb(ia_aiq_awb_results *awbResults);

private:
    int mCameraId;
    AiqPlus *mAiqPlus;

    bool mAeForceLock;
    bool mAwbForceLock;
    bool mAfForceLock;

    Intel3AParameter *mIntel3AParameter;
    Intel3AResult *mIntel3AResult;

    // Original AeResult class arrays are kept in 3a engine which is safely used here.
    ia_aiq_ae_results *mLastAeResult;
    ia_aiq_awb_results *mLastAwbResult;
    ia_aiq_af_results *mLastAfResult;

    int mAeRunTime;
    int mAwbRunTime;
};

} /* namespace icamera */
