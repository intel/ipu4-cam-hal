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
#include "ia_ltm.h"

#include "AiqSetting.h"
#include "ImagingControl.h"

namespace icamera {

/**
 * \class AiqPlus
 * This class is used to run aiq plus related algorithms, such as
 * Gbce, Pa and Sa.
 */
class AiqPlus : public ImagingControl {
public:
    AiqPlus(int cameraId);
    ~AiqPlus();

    int init();
    int deinit();

    int configure(const vector<ConfigMode>& configMode);

    ia_aiq *getIaAiqHandle();

    int setFrameInfo(const ia_aiq_frame_params &frameParams);
    int setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics);

    int updateParameter(const aiq_parameter_t &param);

    int run(AiqResult *aiqResult, int algoType);

private:
    int runGbce(ia_aiq_gbce_results *gbceResults);
    int runPa(ia_aiq_pa_results_v1 *paResults,
              ia_aiq_awb_results *awbResults,
              ia_aiq_exposure_parameters *exposureParams,
              ia_aiq_advanced_ccm_t *preferredAcm);
    int runSa(ia_aiq_sa_results_v1 *saResults,
              ia_aiq_awb_results *awbResults);

    int initAiqPlusParams(void);
    int initIaAiqHandle(const vector<TuningMode>& tuningModes);
    int deinitIaAiqHandle(void);
    // debug dumpers
    int dumpPaResult(const ia_aiq_pa_results_v1* paResult);
    int dumpSaResult(const ia_aiq_sa_results_v1* saResult);

private:
    int mCameraId;
    ia_aiq *mIaAiqHandle[TUNING_MODE_MAX];
    bool mIaAiqHandleStatus[TUNING_MODE_MAX];

    enum AiqPlusState {
        AIQ_PLUS_NOT_INIT = 0,
        AIQ_PLUS_INIT,
        AIQ_PLUS_CONFIGURED,
        AIQ_PLUS_MAX
    } mAiqPlusState;

    ia_aiq_frame_params mFrameParams;

    ia_aiq_gbce_input_params mGbceParams;
    ia_aiq_pa_input_params mPaParams;
    ia_aiq_color_channels mPaColorGains;
    ia_aiq_sa_input_params_v1 mSaParams;

    bool mUseManualColorMatrix;
    camera_color_transform_t mColorMatrix;
    camera_color_gains_t mColorGains;

    TuningMode mTuningMode;
};

} /* namespace icamera */
