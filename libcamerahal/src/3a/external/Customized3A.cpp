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

#define LOG_TAG "Customized3A"

#include <sys/stat.h>
#include <dlfcn.h>

#include <iutils/Utils.h>
#include <iutils/Errors.h>
#include <iutils/CameraLog.h>

#include "Customized3A.h"

#include "PlatformData.h"

namespace icamera {

Customized3A::Customized3A(int cameraId) :
    mCameraId(cameraId),
    mCustomized3AModuleHandle(nullptr),
    mCustom3AModule(nullptr),
    mCustomized3AState(CUSTOM_3A_NOT_INIT)
{
    LOG1("@%s", __func__);
}

Customized3A::~Customized3A()
{
    LOG1("@%s", __func__);

    if (mCustomized3AModuleHandle) {
        CameraUtils::dlcloseLibrary(mCustomized3AModuleHandle);
    }
}

int Customized3A::init()
{
    LOG1("@%s", __func__);

    string name = PlatformData::getCustomized3ALibraryName(mCameraId);
    if (name.empty()) {
        LOG1("%s, no custom 3A library configuration", __func__);
        return OK;
    }

    string libraryPath;
    const string path = "/usr/lib/";
    const string suffix = ".so";
    libraryPath.append(path);
    libraryPath.append(name);
    libraryPath.append(suffix);
    LOG2("%s, custom 3A library path %s", __func__, libraryPath.c_str());

    struct stat st;
    if (stat(libraryPath.c_str(), &st) != 0) {
        LOGD("custom 3A library %s is not available", libraryPath.c_str());
        return OK;
    }

    int flags = RTLD_NOW | RTLD_LOCAL;
    mCustomized3AModuleHandle = CameraUtils::dlopenLibrary(libraryPath.c_str(), flags);
    if (mCustomized3AModuleHandle == nullptr) {
        return BAD_VALUE;
    }

    mCustom3AModule = (Custom3AModule*)CameraUtils::dlsymLibrary(mCustomized3AModuleHandle,
            CUSTOMIZE_3A_MODULE_INFO_SYM_AS_STR);
    if (mCustom3AModule == nullptr) {
        return BAD_VALUE;
    }

    int ret = mCustom3AModule->init();
    if (ret != OK) {
        LOGE("%s, custom 3A init failed ret %d", __func__, ret);
        return BAD_VALUE;
    }

    mCustomized3AState = CUSTOM_3A_INIT;
    return OK;
}

int Customized3A::deinit()
{
    LOG1("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    int ret = mCustom3AModule->deinit();
    if (ret != OK) {
        LOGE("%s, custom 3A deinit failed ret %d", __func__, ret);
    }

    CameraUtils::dlcloseLibrary(mCustomized3AModuleHandle);

    mCustomized3AModuleHandle = nullptr;
    mCustom3AModule = nullptr;
    mCustomized3AState =  CUSTOM_3A_NOT_INIT;

    return OK;
}

bool Customized3A::isCustomAlgoSupported(Custom3AType type)
{
    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return false;

    return mCustom3AModule->custom3ACapability & type;
}

int Customized3A::getSupportedAlgoType()
{
    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return CUSTOM_NONE;

    return mCustom3AModule->custom3ACapability;
}

int Customized3A::setSensorInfo(const ia_aiq_exposure_sensor_descriptor& descriptor)
{
    LOG2("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    return mCustom3AModule->setSensorInfo(descriptor);
}

int Customized3A::updateParameter(const aiq_parameter_t& param)
{
    LOG2("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    custom_3a_parameter_t customParam;
    customParam.reset();

    convertAiqParamToCustomParam(param, &customParam);

    return mCustom3AModule->updateParameter(customParam);
}

int Customized3A::setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics)
{
    LOG2("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    return mCustom3AModule->setStatistics(ispStatistics);
}

int Customized3A::setGbceResults(const ia_aiq_gbce_results *gbceResults)
{
    LOG3A("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    return mCustom3AModule->setGbceResults(gbceResults);
}


int Customized3A::setLtmParams(const ia_ltm_input_params *ltmInputParams,
                               const ia_ltm_drc_params *drcParams)
{
    LOG3A("@%s", __func__);

    if (mCustomized3AState == CUSTOM_3A_NOT_INIT) return OK;

    return mCustom3AModule->setLtmParams(ltmInputParams, drcParams);
}

int Customized3A::run(AiqResult *aiqResult, int algoType)
{
    LOG3A("@%s", __func__);
    int ret = OK;

    if ((algoType & IMAGING_ALGO_AE) && isCustomAlgoSupported(CUSTOM_AE)) {
        ret |= runAe(&aiqResult->mAeResults);
    }
    if ((algoType & IMAGING_ALGO_AWB) && isCustomAlgoSupported(CUSTOM_AWB)) {
        ret |= runAwb(&aiqResult->mAwbResults);
    }
    if ((algoType & IMAGING_ALGO_AF) && isCustomAlgoSupported(CUSTOM_AF)) {
        ret |= runAf(&aiqResult->mAfResults);
    }
    return ret;
}

int Customized3A::runAe(ia_aiq_ae_results *aeResults)
{
    LOG2("@%s", __func__);

    return mCustom3AModule->runAe(aeResults);
}

int Customized3A::runAf(ia_aiq_af_results *afResults)
{
    LOG2("@%s", __func__);

    return mCustom3AModule->runAf(afResults);
}

int Customized3A::runAwb(ia_aiq_awb_results *awbResults)
{
    LOG2("@%s", __func__);

    return mCustom3AModule->runAwb(awbResults);
}

void Customized3A::convertAiqParamToCustomParam(const aiq_parameter_t& aiqParam,
                                                custom_3a_parameter_t *custom3AParam)
{
    LOG2("@%s", __func__);

    Check(custom3AParam == nullptr, VOID_VALUE, "%s, No custom_3a_parameter_t buffer", __func__);

    custom3AParam->aeMode                 = aiqParam.aeMode;
    custom3AParam->awbMode                = aiqParam.awbMode;
    custom3AParam->sceneMode              = aiqParam.sceneMode;
    custom3AParam->manualExpTimeUs        = aiqParam.manualExpTimeUs;
    custom3AParam->manualGain             = aiqParam.manualGain;
    custom3AParam->evShift                = aiqParam.evShift;
    custom3AParam->fps                    = aiqParam.fps;
    custom3AParam->antibandingMode        = aiqParam.antibandingMode;
    custom3AParam->cctRange               = aiqParam.cctRange;
    custom3AParam->whitePoint             = aiqParam.whitePoint;
    custom3AParam->awbManualGain          = aiqParam.awbManualGain;
    custom3AParam->awbGainShift           = aiqParam.awbGainShift;
    custom3AParam->manualColorMatrix      = aiqParam.manualColorMatrix;
    custom3AParam->manualColorGains       = aiqParam.manualColorGains;
    custom3AParam->aeRegions              = aiqParam.aeRegions;
    custom3AParam->blcAreaMode            = aiqParam.blcAreaMode;
    custom3AParam->aeConvergeSpeedMode    = aiqParam.aeConvergeSpeedMode;
    custom3AParam->awbConvergeSpeedMode   = aiqParam.awbConvergeSpeedMode;
    custom3AParam->aeConvergeSpeed        = aiqParam.aeConvergeSpeed;
    custom3AParam->awbConvergeSpeed       = aiqParam.awbConvergeSpeed;
    custom3AParam->hdrLevel               = aiqParam.hdrLevel;
    custom3AParam->weightGridMode         = aiqParam.weightGridMode;
    custom3AParam->aeDistributionPriority = aiqParam.aeDistributionPriority;
}

} /* namespace icamera */
