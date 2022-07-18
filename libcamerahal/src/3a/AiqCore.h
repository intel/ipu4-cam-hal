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
#include "AiqResult.h"

#include "ImagingControl.h"
#include "AiqPlus.h"
#include "intel3a/Intel3A.h"
// CUSTOMIZED_3A_S
#include "external/Customized3A.h"
#include "external/CustomizedAic.h"
// CUSTOMIZED_3A_E

namespace icamera {

/*
 * \class AiqCore
 * This class is used to set parameter, statistics and run Ae,
 * Af, Awb, Gbce, Pa, Sa.
 */
class AiqCore {

public:
    AiqCore(int cameraId);
    ~AiqCore();

    /**
     * \brief AiqCore init
     *
     * Init AiqPlus and AAAObject
     */
    int init();

    /**
     * \brief AiqCore deinit
     *
     * Deinit AiqPlus and AAAObject
     */
    int deinit();

    /**
     * \brief AiqCore configure
     *
     * Configure AiqPlus ConfigMode
     */
    int configure(const vector<ConfigMode>& configModes);

    /**
     * \brief Set sensor and frame info
     *
     * \param frameParams: the frame info parameter
     * \param descriptor: the sensor info parameter
     */
    int setSensorInfo(const ia_aiq_frame_params &frameParams,
                      const ia_aiq_exposure_sensor_descriptor &descriptor);

    /**
     * \brief update param and set converge speed
     *
     * \param param: the parameter update to AiqPlus and Aiq3A or custom 3A
     */
    int updateParameter(const aiq_parameter_t &param);

    /**
     * \brief Set ispStatistics to AiqPlus and Aiq3A or custom 3A
     */
    int setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics);

    /**
     * \brief Set gbceResults to AiqPlus and Aiq3A or custom 3A
     */
    int setGbceResults(const ia_aiq_gbce_results *gbceResults);

    /**
     * \brief Set ltmInputParams and drcParams to AiqPlus and Aiq3A or custom 3A
     */
    int setLtmParams(const ia_ltm_input_params *ltmInputParams,
                     const ia_ltm_drc_params *drcParams);

    /**
     * \brief run 3A and AiqPlus
     *
     * \return OK if succeed, other value indicates failed
     */
    int runAiq(AiqResult *aiqResult);

private:
    DISALLOW_COPY_AND_ASSIGN(AiqCore);

    int run3A(AiqResult *aiqResult);
    int runAiqPlus(AiqResult *aiqResult);
    int setAeConvergeSpeed(camera_converge_speed_t speed);
    int setAwbConvergeSpeed(camera_converge_speed_t speed);

private:
    int mCameraId;

    // Imaging control IDs
    typedef enum {
        IMG_CTRL_3A = 0,
        IMG_CTRL_AIQ_PLUS,
        // CUSTOMIZED_3A_S
        IMG_CTRL_CUSTOM_3A,
        IMG_CTRL_CUSTOM_AIC,
        // CUSTOMIZED_3A_E
        IMG_CTRL_MAX
    } imaging_controller_t;

    map<int, ImagingControl*> mIdToControllerMap;
};

} /* namespace icamera */
