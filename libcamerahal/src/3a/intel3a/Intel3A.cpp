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

#define LOG_TAG "Intel3A"

#include "Intel3A.h"

#include <string.h>

#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "AiqUtils.h"

namespace icamera {

Intel3A::Intel3A(int cameraId, AiqPlus *aiqPlus) :
    mCameraId(cameraId),
    mAiqPlus(aiqPlus),
    mAeForceLock(false),
    mAwbForceLock(false),
    mAfForceLock(false),
    mIntel3AResult(nullptr),
    mLastAeResult(nullptr),
    mLastAwbResult(nullptr),
    mLastAfResult(nullptr),
    mAeRunTime(0),
    mAwbRunTime(0)
{
    LOG3A("@%s", __func__);

    mIntel3AParameter = new Intel3AParameter(cameraId);
    mIntel3AResult = new Intel3AResult();
}

Intel3A::~Intel3A()
{
    LOG3A("@%s", __func__);

    delete mIntel3AParameter;

    delete mIntel3AResult;
}

int Intel3A::init()
{
    LOG3A("@%s", __func__);

    int ret = mIntel3AParameter->init();
    Check((ret != OK), ret, "Init 3a parameter failed ret:%d", ret);

    mLastAeResult = nullptr;
    mLastAwbResult = nullptr;
    mLastAfResult = nullptr;
    mAeRunTime = 0;
    mAwbRunTime = 0;

    return ret;
}

int Intel3A::deinit()
{
    LOG3A("@%s", __func__);
    return OK;
}

int Intel3A::setSensorInfo(const ia_aiq_exposure_sensor_descriptor &descriptor)
{
    LOG3A("@%s", __func__);

    mIntel3AParameter->setSensorInfo(descriptor);
    return OK;
}

int Intel3A::updateParameter(const aiq_parameter_t &param)
{
    LOG3A("@%s", __func__);

    mIntel3AParameter->updateParameter(param);
    mAeForceLock = param.aeForceLock;
    mAwbForceLock = param.awbForceLock;
    mAfForceLock = mIntel3AParameter->mAfForceLock;
    return OK;
}

int Intel3A::run(AiqResult *aiqResult, int algoType)
{
    LOG3A("@%s", __func__);
    int ret = OK;

    if (algoType & IMAGING_ALGO_AE) {
        ret |= runAe(&aiqResult->mAeResults);
    }
    if (algoType & IMAGING_ALGO_AWB) {
        ret |= runAwb(&aiqResult->mAwbResults);
    }
    if (algoType & IMAGING_ALGO_AF) {
        ret |= runAf(&aiqResult->mAfResults);
    }
    return ret;
}

int Intel3A::runAe(ia_aiq_ae_results* aeResults)
{
    LOG3A("@%s", __func__);
    PERF_CAMERA_ATRACE();

    int ret = OK;
    ia_aiq_ae_results *newAeResults = mLastAeResult;

    if (!mAeForceLock && (mAeRunTime % mIntel3AParameter->mAePerTicks == 0)) {
        LOG3A("AEC frame_use: %d", mIntel3AParameter->mAeParams.frame_use);
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_ae_run", 1);
        ia_err iaErr = ia_aiq_ae_run(mAiqPlus->getIaAiqHandle(),
                           &(mIntel3AParameter->mAeParams), &newAeResults);

        ret = AiqUtils::convertError(iaErr);
        Check((ret != OK || newAeResults == nullptr), ret, "Error running AE %d", ret);
    }

    mIntel3AParameter->updateAeResult(newAeResults);

    ret = mIntel3AResult->deepCopyAeResults(*newAeResults, aeResults);

    mLastAeResult = aeResults;
    ++mAeRunTime;

    return ret;
}

int Intel3A::runAf(ia_aiq_af_results *afResults)
{
    LOG3A("@%s", __func__);
    PERF_CAMERA_ATRACE();

    ia_aiq_af_results *newAfResults = mLastAfResult;

    int ret = OK;
    if (!mAfForceLock) {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_af_run", 1);
        ia_err iaErr = ia_aiq_af_run(mAiqPlus->getIaAiqHandle(),
                           &(mIntel3AParameter->mAfParams), &newAfResults);

        ret = AiqUtils::convertError(iaErr);
        Check((ret != OK || newAfResults == nullptr), ret, "Error running AF %d", ret);
    }

    ret = mIntel3AResult->deepCopyAfResults(*newAfResults, afResults);

    mLastAfResult = afResults;
    mIntel3AParameter->fillAfTriggerResult(newAfResults);
    return ret;
}

int Intel3A::runAwb(ia_aiq_awb_results* awbResults)
{
    LOG3A("@%s", __func__);
    PERF_CAMERA_ATRACE();

    int ret = OK;
    ia_aiq_awb_results *newAwbResults = mLastAwbResult;

    if (!mAwbForceLock && (mAwbRunTime % mIntel3AParameter->mAwbPerTicks == 0)) {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_awb_run", 1);
        ia_err iaErr = ia_aiq_awb_run(mAiqPlus->getIaAiqHandle(),
                           &(mIntel3AParameter->mAwbParams), &newAwbResults);

        ret = AiqUtils::convertError(iaErr);
        Check((ret != OK || newAwbResults == nullptr), ret, "Error running AWB %d", ret);
    }

    mIntel3AParameter->updateAwbResult(newAwbResults);

    ret = mIntel3AResult->deepCopyAwbResults(*newAwbResults, awbResults);

    mLastAwbResult = awbResults;
    ++mAwbRunTime;

    return ret;
}

} /* namespace icamera */
