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

#define LOG_TAG "CustomizedAic"

#include <sys/stat.h>
#include <dlfcn.h>

#include <iutils/Utils.h>
#include <iutils/Errors.h>
#include <iutils/CameraLog.h>

#include "CustomizedAic.h"
#include "CustomizedAicModule.h"

#include "PlatformData.h"

namespace icamera {

CustomizedAic::CustomizedAic(int cameraId) :
    mCameraId(cameraId),
    mCustomizedAicModuleHandle(nullptr),
    mCustomAicModule(nullptr),
    mCustomizedAicState(CUSTOM_AIC_NOT_INIT)
{
    LOG1("@%s", __func__);
}

CustomizedAic::~CustomizedAic()
{
    LOG1("@%s", __func__);

    if (mCustomizedAicModuleHandle) {
        CameraUtils::dlcloseLibrary(mCustomizedAicModuleHandle);
    }
}

int CustomizedAic::init()
{
    LOG1("@%s", __func__);

    string name = PlatformData::getCustomizedAicLibraryName(mCameraId);
    if (name.empty()) {
        LOG1("%s, no custom Aic library configuration", __func__);
        return OK;
    }

    string libraryPath;
    const string path = "/usr/lib/";
    const string suffix = ".so";
    libraryPath.append(path);
    libraryPath.append(name);
    libraryPath.append(suffix);
    LOG2("%s, custom Aic library path %s", __func__, libraryPath.c_str());

    struct stat st;
    if (stat(libraryPath.c_str(), &st) != 0) {
        LOGD("custom Aic library %s is not available", libraryPath.c_str());
        return OK;
    }

    int flags = RTLD_NOW | RTLD_LOCAL;
    mCustomizedAicModuleHandle = CameraUtils::dlopenLibrary(libraryPath.c_str(), flags);
    if (mCustomizedAicModuleHandle == nullptr) {
        return BAD_VALUE;
    }

    mCustomAicModule = (CustomAicModule*)CameraUtils::dlsymLibrary(mCustomizedAicModuleHandle,
            CUSTOMIZE_AIC_MODULE_INFO_SYM_AS_STR);
    if (mCustomAicModule == nullptr) {
        return BAD_VALUE;
    }

    int ret = mCustomAicModule->init();
    if (ret != OK) {
        LOGE("%s, custom Aic init failed ret %d", __func__, ret);
        return BAD_VALUE;
    }

    mCustomizedAicState = CUSTOM_AIC_INIT;
    return OK;
}

int CustomizedAic::deinit()
{
    LOG1("@%s", __func__);

    if (mCustomizedAicState == CUSTOM_AIC_NOT_INIT) return OK;

    int ret = mCustomAicModule->deinit();
    if (ret != OK) {
        LOGE("%s, custom Aic deinit failed ret %d", __func__, ret);
    }

    CameraUtils::dlcloseLibrary(mCustomizedAicModuleHandle);

    mCustomizedAicModuleHandle = nullptr;
    mCustomAicModule = nullptr;
    mCustomizedAicState =  CUSTOM_AIC_NOT_INIT;

    return OK;
}

int CustomizedAic::updateParameter(const aiq_parameter_t &param)
{
    LOG2("@%s", __func__);

    if (mCustomizedAicState == CUSTOM_AIC_NOT_INIT) return OK;

    CustomAicParam aicParam;
    CLEAR(aicParam);

    aicParam.data = (void*)param.customAicParam.data;
    aicParam.length = param.customAicParam.length;

    return mCustomAicModule->setAicParam(aicParam);
}

int CustomizedAic::run(AiqResult *aiqResult, int algoType)
{
    LOG2("@%s", __func__);
    UNUSED(algoType);

    return runExternalAic(aiqResult->mAeResults, aiqResult->mAwbResults,
                          &aiqResult->mCustomControls, &aiqResult->mCustomAicPipe);
}

int CustomizedAic::runExternalAic(const ia_aiq_ae_results &ae_results,
                   const ia_aiq_awb_results &awb_results,
                   ia_isp_custom_controls *custom_controls,
                   CustomAicPipe *pipe)
{
    LOG2("@%s", __func__);

    if (mCustomizedAicState == CUSTOM_AIC_NOT_INIT) return OK;

    int ret = mCustomAicModule->runExternalAic(ae_results, awb_results, custom_controls, pipe);
    if (ret == 0) {
        for (int i = 0; i < custom_controls->count; i++) {
            LOG2("%s, parameter[%d] = %f", __func__, i, custom_controls->parameters[i]);
        }
    }

    return ret;
}

} /* namespace icamera */
