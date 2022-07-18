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

#define LOG_TAG "AiqCore"

#include <string.h>

#include "iutils/Utils.h"
#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "AiqCore.h"

namespace icamera {

AiqCore::AiqCore(int cameraId) :
    mCameraId(cameraId)
{
    LOG3A("@%s", __func__);

    AiqPlus *aiqPlus = new AiqPlus(cameraId);
    mIdToControllerMap[IMG_CTRL_AIQ_PLUS] = aiqPlus;
    mIdToControllerMap[IMG_CTRL_3A] = new Intel3A(cameraId, aiqPlus);

    // CUSTOMIZED_3A_S
    mIdToControllerMap[IMG_CTRL_CUSTOM_3A] = new Customized3A(cameraId);
    mIdToControllerMap[IMG_CTRL_CUSTOM_AIC] = new CustomizedAic(cameraId);
    // CUSTOMIZED_3A_E
}

AiqCore::~AiqCore()
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        delete executor.second;
    }
    mIdToControllerMap.clear();
}

int AiqCore::init()
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        int ret = executor.second->init();
        Check((ret != OK), ret, "Init imaging executor %d failed, ret = %d", executor.first, ret);
    }

    return OK;
}

int AiqCore::deinit()
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        executor.second->deinit();
    }

    return OK;
}

int AiqCore::configure(const vector<ConfigMode>& configModes)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        executor.second->configure(configModes);
    }

    return OK;
}

int AiqCore::setSensorInfo(const ia_aiq_frame_params &frameParams,
                           const ia_aiq_exposure_sensor_descriptor &descriptor)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        executor.second->setFrameInfo(frameParams);
        executor.second->setSensorInfo(descriptor);
    }

    return OK;
}

int AiqCore::updateParameter(const aiq_parameter_t &param)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        executor.second->updateParameter(param);
    }

    return OK;
}

int AiqCore::setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        int ret = executor.second->setStatistics(ispStatistics);
        Check((ret != OK), ret,
              "set statistics to imaging executor %d failed, ret = %d", executor.first, ret);
    }

    return OK;
}

int AiqCore::setGbceResults(const ia_aiq_gbce_results *gbceResults)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        int ret = executor.second->setGbceResults(gbceResults);
        Check((ret != OK), ret,
              "set GBCE results to imaging executor %d failed, ret = %d", executor.first, ret);
    }

    return OK;
}

int AiqCore::setLtmParams(const ia_ltm_input_params *ltmInputParams,
                          const ia_ltm_drc_params *drcParams)
{
    LOG3A("@%s", __func__);

    for (auto& executor: mIdToControllerMap) {
        int ret = executor.second->setLtmParams(ltmInputParams, drcParams);
        Check((ret != OK), ret,
              "set GBCE results to imaging executor %d failed, ret = %d", executor.first, ret);
    }

    return OK;
}

int AiqCore::runAiq(AiqResult *aiqResult)
{
    LOG3A("@%s", __func__);

    int ret = run3A(aiqResult);
    Check((ret != OK), ret, "run 3A failed, ret = %d", ret);

    ret = runAiqPlus(aiqResult);
    Check((ret != OK), ret, "run Aiq Plus failed, ret = %d", ret);

    // CUSTOMIZED_3A_S
    mIdToControllerMap[IMG_CTRL_CUSTOM_AIC]->run(aiqResult, 0);
    // CUSTOMIZED_3A_E

    return OK;
}

int AiqCore::run3A(AiqResult *aiqResult)
{
    LOG3A("@%s", __func__);
    int ret = OK;

    int aaaType = IMAGING_ALGO_AE | IMAGING_ALGO_AWB;
    if (PlatformData::getLensHwType(mCameraId) == LENS_VCM_HW) {
        aaaType |= IMAGING_ALGO_AF;
    }

    // CUSTOMIZED_3A_S
    ret |= mIdToControllerMap[IMG_CTRL_CUSTOM_3A]->run(aiqResult, aaaType);
    aaaType &= ~(mIdToControllerMap[IMG_CTRL_CUSTOM_3A]->getSupportedAlgoType());
    // CUSTOMIZED_3A_E

    ret |= mIdToControllerMap[IMG_CTRL_3A]->run(aiqResult, aaaType);

    return ret;
}

int AiqCore::runAiqPlus(AiqResult *aiqResult)
{
    LOG3A("@%s", __func__);

    int algoType = IMAGING_ALGO_GBCE | IMAGING_ALGO_PA | IMAGING_ALGO_SA;
    // CUSTOMIZED_3A_S
    if (mIdToControllerMap[IMG_CTRL_CUSTOM_3A]->getSupportedAlgoType() & CUSTOM_AE) {
        algoType &= ~IMAGING_ALGO_GBCE;
    }
    // CUSTOMIZED_3A_E

    int ret = mIdToControllerMap[IMG_CTRL_AIQ_PLUS]->run(aiqResult, algoType);

    return ret;
}

} /* namespace icamera */
