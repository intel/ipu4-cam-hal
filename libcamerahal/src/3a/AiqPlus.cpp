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

#define LOG_TAG "AiqPlus"

#include "AiqPlus.h"

#include "iutils/Utils.h"
#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "PlatformData.h"
#include "IntelMkn.h"
#include "AiqUtils.h"

#include "ia_log.h"

namespace icamera {

#define VALID_COLOR_GAINS(colorGains) (colorGains[0] > 0 && colorGains[1] > 0 && \
                                       colorGains[2] > 0 && colorGains[3] > 0)

AiqPlus::AiqPlus(int cameraId) :
    mCameraId(cameraId),
    mAiqPlusState(AIQ_PLUS_NOT_INIT),
    mUseManualColorMatrix(false),
    mTuningMode(TUNING_MODE_MAX)
{
    LOG3A("%s", __func__);

    CLEAR(mFrameParams);
    CLEAR(mGbceParams);
    CLEAR(mPaParams);
    CLEAR(mSaParams);
    CLEAR(mColorMatrix);
    CLEAR(mColorGains);
    CLEAR(mIaAiqHandle);
    CLEAR(mIaAiqHandleStatus);
    CLEAR(mPaColorGains);
}

AiqPlus::~AiqPlus()
{
    LOG3A("%s", __func__);
}

int AiqPlus::initAiqPlusParams(void)
{
    CLEAR(mFrameParams);
    CLEAR(mGbceParams);
    CLEAR(mPaParams);
    CLEAR(mPaColorGains);
    CLEAR(mSaParams);
    CLEAR(mColorMatrix);
    CLEAR(mColorGains);

    mUseManualColorMatrix = false;

    mGbceParams.gbce_level = ia_aiq_gbce_level_use_tuning;
    mGbceParams.frame_use = ia_aiq_frame_use_video;
    mGbceParams.ev_shift = 0;
    mGbceParams.tone_map_level = ia_aiq_tone_map_level_use_tuning;

    mPaParams.color_gains = nullptr;

    mSaParams.sensor_frame_params = &mFrameParams;
    /* use convergence time from tunings */
    mSaParams.manual_convergence_time = -1.0;

    return OK;
}

int AiqPlus::init()
{
    LOG3A("%s", __func__);

    initAiqPlusParams();

    LOGI("IA AIQ VERSION %s", ia_aiq_get_version());

    ia_env env;
    CLEAR(env);
    env.vdebug = &Log::ccaPrintDebug;
    env.verror = &Log::ccaPrintError;
    env.vinfo = &Log::ccaPrintInfo;
    ia_log_init(&env);

    mAiqPlusState = AIQ_PLUS_INIT;

    return OK;
}

int AiqPlus::deinit()
{
    LOG3A("%s", __func__);

    ia_log_deinit();

    deinitIaAiqHandle();

    mAiqPlusState = AIQ_PLUS_NOT_INIT;

    return OK;
}

int AiqPlus::configure(const vector<ConfigMode>& configModes)
{
    LOG3A("@%s", __func__);

    int ret = OK;
    bool allTuningModeConfiged = true;
    vector<TuningMode> tuningModes;
    for (auto cfg : configModes) {
        TuningMode tMode;
        ret = PlatformData::getTuningModeByConfigMode(mCameraId, cfg, tMode);
        Check(ret != OK, ret, "%s: unknown tuning mode for config mode %d", __func__, cfg);
        tuningModes.push_back(tMode);

        if (!mIaAiqHandle[tMode]) {
            allTuningModeConfiged = false;
        }
    }

    if (mAiqPlusState == AIQ_PLUS_CONFIGURED && allTuningModeConfiged) {
        return OK;
    }

    deinitIaAiqHandle();

    ret = initIaAiqHandle(tuningModes);
    if (ret == OK) {
        mAiqPlusState = AIQ_PLUS_CONFIGURED;
    }

    return ret;
}

int AiqPlus::initIaAiqHandle(const vector<TuningMode>& tuningModes)
{
    LOG3A("@%s", __func__);

    ia_binary_data aiqData;

    CpfStore* cpf = PlatformData::getCpfStore(mCameraId);
    Check((cpf == nullptr), NO_INIT, "@%s, No CPF for cameraId:%d", __func__, mCameraId);

    ia_mkn* mkn = IntelMkn::getInstance(mCameraId)->getMknHandle();
    Check((mkn == nullptr), NO_INIT, "Error in initing makernote");

    // Initialize mIaAiqHandle array based on different cpf data
    for (auto &tMode : tuningModes) {
        ia_cmc_t *cmc = nullptr;
        if (cpf->getDataAndCmc(nullptr, &aiqData, nullptr, &cmc, tMode) != OK) {
            return BAD_VALUE;
        }

        int statsNum = PlatformData::getExposureNum(mCameraId,
                                                    CameraUtils::isHdrPsysPipe(tMode));
        {
            PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_init", 1);
            mIaAiqHandle[tMode] = ia_aiq_init((ia_binary_data*)&(aiqData),
                                        nullptr,
                                        nullptr,
                                        MAX_STATISTICS_WIDTH,
                                        MAX_STATISTICS_HEIGHT,
                                        statsNum,
                                        cmc,
                                        mkn);
        }
        Check((mIaAiqHandle[tMode] == nullptr), NO_INIT, "%s: init aiq failed!", __func__);
        mIaAiqHandleStatus[tMode] = true;
    }

    return OK;
}

int AiqPlus::deinitIaAiqHandle(void)
{
    LOG3A("@%s", __func__);

    for (int mode = 0; mode < TUNING_MODE_MAX; mode++) {
        if (mIaAiqHandle[mode]) {
            ia_aiq_deinit(mIaAiqHandle[mode]);
            mIaAiqHandle[mode]= nullptr;
        }
    }
    CLEAR(mIaAiqHandleStatus);

    return OK;
}

ia_aiq *AiqPlus::getIaAiqHandle()
{
    if (!mIaAiqHandleStatus[mTuningMode]) {
        LOGE("%s, mTuningMode is wrong mode %d", __func__, mTuningMode);
    }

    return mIaAiqHandle[mTuningMode];
}

int AiqPlus::setFrameInfo(const ia_aiq_frame_params &frameParams)
{
    LOG3A("@%s", __func__);

    mFrameParams = frameParams;

    return OK;
}

int AiqPlus::setStatistics(const ia_aiq_statistics_input_params_v4 *ispStatistics)
{
    LOG3A("@%s", __func__);

    int ret = OK;

    if (ispStatistics != nullptr) {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_statistics_set", 1);
        ia_err iaErr = ia_aiq_statistics_set_v4(getIaAiqHandle(), ispStatistics);
        ret |= AiqUtils::convertError(iaErr);
        Check((ret != OK), ret, "Error setting statistics, ret = %d", ret);
    }

    return ret;
}

int AiqPlus::updateParameter(const aiq_parameter_t &param)
{
    LOG3A("%s, param.tuningMode = %d", __func__, param.tuningMode);

    mUseManualColorMatrix = (param.awbMode == AWB_MODE_MANUAL_COLOR_TRANSFORM);
    mColorMatrix = param.manualColorMatrix;
    mColorGains = param.manualColorGains;
    mTuningMode = param.tuningMode;

    mGbceParams.frame_use = AiqUtils::convertFrameUsageToIaFrameUsage(param.frameUsage);

    // In still frame use force update by setting convergence time to 0.
    // in other cases use tunings.
    mSaParams.manual_convergence_time = (param.frameUsage == FRAME_USAGE_STILL) ? 0.0 : -1.0;

    return OK;
}

int AiqPlus::run(AiqResult *aiqResult, int algoType)
{
    LOG3A("@%s", __func__);
    int ret = OK;

    if (algoType & IMAGING_ALGO_GBCE) {
        ret |= runGbce(&aiqResult->mGbceResults);
    }
    if (algoType & IMAGING_ALGO_PA) {
        ret |= runPa(&aiqResult->mPaResults, &aiqResult->mAwbResults,
                           aiqResult->mAeResults.exposures[0].exposure,
                           &aiqResult->mPreferredAcm);
    }
    if (algoType & IMAGING_ALGO_SA) {
        ret |= runSa(&aiqResult->mSaResults, &aiqResult->mAwbResults);
    }
    return ret;
}

int AiqPlus::runGbce(ia_aiq_gbce_results *gbceResults)
{
    LOG3A("%s", __func__);
    PERF_CAMERA_ATRACE();
    ia_aiq_gbce_results *newGbceResults = nullptr;

    ia_err iaErr;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_gbce_run", 1);
        iaErr = ia_aiq_gbce_run(getIaAiqHandle(), &mGbceParams, &newGbceResults);
    }

    int ret = AiqUtils::convertError(iaErr);
    Check(ret != OK || newGbceResults == nullptr, ret, "Error running GBCE");

    return AiqUtils::deepCopyGbceResults(*newGbceResults, gbceResults);
}

int AiqPlus::runPa(ia_aiq_pa_results_v1 *paResults,
                   ia_aiq_awb_results *awbResults,
                   ia_aiq_exposure_parameters *exposureParams,
                   ia_aiq_advanced_ccm_t *preferredAcm)
{
    LOG3A("%s", __func__);
    PERF_CAMERA_ATRACE();
    ia_aiq_pa_results_v1 *newPaResults = nullptr;

    mPaParams.awb_results = awbResults;
    mPaParams.exposure_params = exposureParams;
    mPaParams.color_gains = nullptr;

    ia_err iaErr;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_pa_run", 1);
        iaErr = ia_aiq_pa_run_v1(getIaAiqHandle(), &mPaParams, &newPaResults);
    }

    int ret = AiqUtils::convertError(iaErr);
    Check(ret != OK || newPaResults == nullptr, ret, "Error running PA");

    dumpPaResult(newPaResults);

    // Override color_conversion_matrix and color_gains
    // when application requires manual color transform.
    if (mUseManualColorMatrix) {
        MEMCPY_S(&newPaResults->color_conversion_matrix, sizeof(newPaResults->color_conversion_matrix),
                 &mColorMatrix.color_transform, sizeof(mColorMatrix.color_transform));

        if (VALID_COLOR_GAINS(mColorGains.color_gains_rggb)) {
            newPaResults->color_gains.r  = mColorGains.color_gains_rggb[0];
            newPaResults->color_gains.gr = mColorGains.color_gains_rggb[1];
            newPaResults->color_gains.gb = mColorGains.color_gains_rggb[2];
            newPaResults->color_gains.b  = mColorGains.color_gains_rggb[3];
        }

        //Override advanced color conversion matrix also if it enabled
        if (newPaResults->preferred_acm) {
            for (unsigned int i = 0; i < newPaResults->preferred_acm->sector_count; i++) {
                MEMCPY_S(&newPaResults->preferred_acm->advanced_color_conversion_matrices[i],
                         sizeof(newPaResults->preferred_acm->advanced_color_conversion_matrices[0]),
                         &mColorMatrix.color_transform, sizeof(mColorMatrix.color_transform));
            }
        }
    }

    return AiqUtils::deepCopyPaResults(*newPaResults, paResults, preferredAcm);
}

int AiqPlus::runSa(ia_aiq_sa_results_v1 *saResults,
                   ia_aiq_awb_results *awbResults)
{
    LOG3A("%s", __func__);
    PERF_CAMERA_ATRACE();

    ia_aiq_sa_results_v1 *newSaResults = nullptr;

    mSaParams.awb_results = awbResults;

    ia_err iaErr;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_aiq_sa_run", 1);
        iaErr = ia_aiq_sa_run_v2(getIaAiqHandle(), &mSaParams, &newSaResults);
    }

    int ret = AiqUtils::convertError(iaErr);
    Check(ret != OK || newSaResults == nullptr, ret, "Error running SA");

    dumpSaResult(newSaResults);
    return AiqUtils::deepCopySaResults(*newSaResults, saResults);
}

int AiqPlus::dumpPaResult(const ia_aiq_pa_results_v1 *paResult)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) return OK;

    LOG3A("%s", __func__);

    if (paResult) {
        LOG3A("   PA results brightness %f saturation %f",
                paResult->brightness_level,
                paResult->saturation_factor);
        LOG3A("   PA results black level row 0: %f %f %f  %f ",
                paResult->black_level_4x4[0][0],
                paResult->black_level_4x4[0][1],
                paResult->black_level_4x4[0][2],
                paResult->black_level_4x4[0][3]);
        LOG3A("   PA results black level row 1: %f %f %f  %f ",
                paResult->black_level_4x4[1][0],
                paResult->black_level_4x4[1][1],
                paResult->black_level_4x4[1][2],
                paResult->black_level_4x4[1][3]);
        LOG3A("   PA results black level row 2: %f %f %f  %f ",
                paResult->black_level_4x4[2][0],
                paResult->black_level_4x4[2][1],
                paResult->black_level_4x4[2][2],
                paResult->black_level_4x4[2][3]);
        LOG3A("   PA results black level row 3: %f %f %f  %f ",
                paResult->black_level_4x4[3][0],
                paResult->black_level_4x4[3][1],
                paResult->black_level_4x4[3][2],
                paResult->black_level_4x4[3][3]);
        LOG3A("   PA results color gains %f %f %f  %f ",
                    paResult->color_gains.r,
                    paResult->color_gains.gr,
                    paResult->color_gains.gb,
                    paResult->color_gains.b);
        LOG3A("   PA results linearization table size %d",
                        paResult->linearization.size);

        for(int i = 0; i < 3; i++) {
            LOG3A("   PA results color matrix  [%.3f %.3f %.3f] ",
                    paResult->color_conversion_matrix[i][0],
                    paResult->color_conversion_matrix[i][1],
                    paResult->color_conversion_matrix[i][2]);
        }

        if (paResult->preferred_acm) {
            LOG3A("   PA results advanced ccm sector count %d ", paResult->preferred_acm->sector_count);
        }
        if (paResult->ir_weight) {
            LOG3A("   PA results ir weight grid [ %d x %d ] ", paResult->ir_weight->width, paResult->ir_weight->height);
        }

    }
    return OK;
}

int AiqPlus::dumpSaResult(const ia_aiq_sa_results_v1 *saResult)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) return OK;

    LOG3A("%s", __func__);

    if (saResult) {
        LOG3A("   SA results lsc Update %d size %dx%d",
                saResult->lsc_update,
                saResult->width,
                saResult->height);
    }

    return OK;
}

} /* namespace icamera */
