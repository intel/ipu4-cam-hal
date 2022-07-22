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

#define LOG_TAG "ParameterGenerator"

#include <math.h>

#include <iutils/Errors.h>
#include <iutils/CameraLog.h>
#include "iutils/Utils.h"

#include "IntelMkn.h"
#include "AiqResultStorage.h"
#include "AiqUtils.h"
#include "ParameterGenerator.h"

namespace icamera {

ParameterGenerator::ParameterGenerator(int cameraId) :
    mCameraId(cameraId),
    mCurrentIndex(-1)
{
    LOG1("%s, mCameraId = %d", __func__, mCameraId);
    reset();
}

ParameterGenerator::~ParameterGenerator()
{
    LOG1("%s, mCameraId = %d", __func__, mCameraId);
}

int ParameterGenerator::reset()
{
    LOG1("%s, mCameraId = %d", __func__, mCameraId);
    AutoMutex l(mParamsLock);
    mCurrentIndex = -1;
    for (int i = 0; i < kStorageSize; i++) {
        mParameters[i].reset();
    }
    return OK;
}

int ParameterGenerator::saveParameters(long sequence, const Parameters &param)
{
    LOG2("%s, sequence = %ld", __func__, sequence);
    AutoMutex l(mParamsLock);

    Check((mCurrentIndex >= 0 && mParameters[mCurrentIndex].sequence >= sequence),
        ALREADY_EXISTS, "Parameters of sequence %ld is already saved.", sequence);

    if (mCurrentIndex < 0 && sequence > 0) {
        // The 1st frame is skipped, use this parameters for it.
        saveParametersL(0, param);
    }
    saveParametersL(sequence, param);

    LOG2("%s, end sequence = %ld", __func__, sequence);
    return OK;
}

void ParameterGenerator::saveParametersL(long sequence, const Parameters &param)
{
    mCurrentIndex++;
    mCurrentIndex %= kStorageSize;

    mParameters[mCurrentIndex].sequence = sequence;
    *(mParameters[mCurrentIndex].user) = param;
}

int ParameterGenerator::getParameters(long sequence, Parameters *param,
                                      bool mergeResultOnly, bool still)
{
    LOG2("%s, sequence = %ld", __func__, sequence);
    AutoMutex l(mParamsLock);

    if (!mergeResultOnly) {
        // first get the saved user parameters.
        Check((mCurrentIndex < 0), UNKNOWN_ERROR, "%s No user parameters saved.", __func__);

        int index = getIndexBySequence(sequence);
        *param = *(mParameters[index].user);
    }

    if (still) {
        IntelMkn::getInstance(mCameraId)->acquireMakernoteData(sequence, param);
    }

    generateParametersL(sequence, param);

    return OK;
}

int ParameterGenerator::generateParametersL(long sequence, Parameters *params)
{
    if (PlatformData::isEnableAIQ(mCameraId)) {
        updateWithAiqResultsL(sequence, params);
    // LOCAL_TONEMAP_S
        updateWithLtmTuningDataL(params);
    // LOCAL_TONEMAP_E
    }
    return OK;
}

int ParameterGenerator::updateWithAiqResultsL(long sequence, Parameters *params)
{
    const AiqResult *aiqResult = AiqResultStorage::getInstance(mCameraId)->getAiqResult(sequence);
    Check((aiqResult == nullptr), UNKNOWN_ERROR,
           "%s Aiq result of sequence %ld does not exist", __func__, sequence);

    // Update AE related parameters
    camera_ae_state_t aeState = aiqResult->mAeResults.exposures[0].converged ?
            AE_STATE_CONVERGED : AE_STATE_NOT_CONVERGED;
    params->setAeState(aeState);

    if (CameraUtils::isHdrPsysPipe(aiqResult->mTuningMode) &&
        aiqResult->mAeResults.num_exposures > 1 && aiqResult->mAeResults.exposures[1].exposure) {
        params->setExposureTime(aiqResult->mAeResults.exposures[1].exposure->exposure_time_us);
    } else {
        params->setExposureTime(aiqResult->mAeResults.exposures[0].exposure->exposure_time_us);
    }
    params->setSensitivityGain(log10(aiqResult->mAeResults.exposures[0].exposure->analog_gain)*20.0);

    // Update AWB related parameters
    updateAwbGainsL(params, aiqResult->mAwbResults);
    camera_color_transform_t ccm;
    MEMCPY_S(ccm.color_transform, sizeof(ccm.color_transform),
             aiqResult->mPaResults.color_conversion_matrix,
             sizeof(aiqResult->mPaResults.color_conversion_matrix));
    params->setColorTransform(ccm);

    camera_color_gains_t colorGains;
    colorGains.color_gains_rggb[0] = aiqResult->mPaResults.color_gains.r;
    colorGains.color_gains_rggb[1] = aiqResult->mPaResults.color_gains.gr;
    colorGains.color_gains_rggb[2] = aiqResult->mPaResults.color_gains.gb;
    colorGains.color_gains_rggb[3] = aiqResult->mPaResults.color_gains.b;
    params->setColorGains(colorGains);

    camera_awb_state_t awbState = (fabs(aiqResult->mAwbResults.distance_from_convergence) < 0.001) ?
            AWB_STATE_CONVERGED : AWB_STATE_NOT_CONVERGED;
    params->setAwbState(awbState);

    // Update AF related parameters
    camera_af_state_t afState = \
            (aiqResult->mAfResults.status == ia_aiq_af_status_local_search) ? AF_STATE_LOCAL_SEARCH
          : (aiqResult->mAfResults.status == ia_aiq_af_status_extended_search) ? AF_STATE_EXTENDED_SEARCH
          : (aiqResult->mAfResults.status == ia_aiq_af_status_success) ? AF_STATE_SUCCESS
          : (aiqResult->mAfResults.status == ia_aiq_af_status_fail) ? AF_STATE_FAIL
          : AF_STATE_IDLE;
    params->setAfState(afState);

    bool lensMoving = false;
    if (afState == AF_STATE_LOCAL_SEARCH || afState == AF_STATE_EXTENDED_SEARCH) {
        lensMoving = (aiqResult->mAfResults.final_lens_position_reached == false);
    }
    params->setLensState(lensMoving);

    // Update scene mode
    camera_scene_mode_t sceneMode = SCENE_MODE_AUTO;
    params->getSceneMode(sceneMode);

    /* Use direct AE result to update sceneMode to reflect the actual mode AE want to have,
     * Besides needed by full pipe auto-switch, this is also necessary when user want to
     * switch pipe in user app according to AE result.
     */
    if (sceneMode == SCENE_MODE_AUTO) {
        if (aiqResult->mAeResults.multiframe== ia_aiq_bracket_mode_hdr) {
            sceneMode = SCENE_MODE_HDR;
        } else if (aiqResult->mAeResults.multiframe == ia_aiq_bracket_mode_ull) {
            sceneMode = SCENE_MODE_ULL;
        }
    }
    LOG2("%s, sceneMode:%d", __func__, sceneMode);
    params->setSceneMode(sceneMode);

    return OK;
}

int ParameterGenerator::updateAwbGainsL(Parameters *params, const ia_aiq_awb_results &result)
{
    camera_awb_gains_t awbGains;
    CLEAR(awbGains);
    float normalizedR, normalizedG, normalizedB;

    if (params->getAwbGains(awbGains) == OK) {
        // User manual AWB gains
        awbGains.g_gain = CLIP(awbGains.g_gain, AWB_GAIN_MAX, AWB_GAIN_MIN);
        normalizedG = AiqUtils::normalizeAwbGain(awbGains.g_gain);
    } else {
        // non-manual AWB gains, try to find a proper G that makes R/G/B all in the gain range.
        normalizedG = sqrt((AWB_GAIN_NORMALIZED_START * AWB_GAIN_NORMALIZED_END) / \
                           (result.accurate_r_per_g * result.accurate_b_per_g));
        awbGains.g_gain = AiqUtils::convertToUserAwbGain(normalizedG);
    }

    normalizedR = result.accurate_r_per_g * normalizedG;
    normalizedB = result.accurate_b_per_g * normalizedG;

    awbGains.r_gain = AiqUtils::convertToUserAwbGain(normalizedR);
    awbGains.b_gain = AiqUtils::convertToUserAwbGain(normalizedB);

    LOG2("awbGains [r, g, b] = [%d, %d, %d]", awbGains.r_gain, awbGains.g_gain, awbGains.b_gain);
    params->setAwbGains(awbGains);

    // Update the AWB result
    camera_awb_result_t awbResult;
    awbResult.r_per_g = result.accurate_r_per_g;
    awbResult.b_per_g = result.accurate_b_per_g;
    LOG2("awb result: %f, %f", awbResult.r_per_g, awbResult.b_per_g);
    params->setAwbResult(&awbResult);

    return OK;
}

// LOCAL_TONEMAP_S
int ParameterGenerator::updateWithLtmTuningDataL(Parameters *params)
{
    // Check if user set the ltm tuning data. If yes, no need to update.
    if (params->getLtmTuningData(nullptr) == OK)
        return OK;

    const ltm_tuning_data *ltmTuningData =
            AiqResultStorage::getInstance(mCameraId)->getLtmTuningData();
    if (ltmTuningData != nullptr) {
        // Update Ltm tuning data
        params->setLtmTuningData(ltmTuningData);
        LOG2("LTM tuning mode:%d, grid density:%d",
              ltmTuningData->algo_mode, ltmTuningData->grid_density);
    }
    return OK;
}
// LOCAL_TONEMAP_E

int ParameterGenerator::getIndexBySequence(long sequence)
{
    // Sequence id is -1 means user wants to get the latest result.
    if (sequence == -1) {
        return mCurrentIndex;
    }

    int index = -1;
    for (int i = 0; i < kStorageSize; i++) {
        // Search from the latest result
        int tmpIdx = (mCurrentIndex + kStorageSize - i) % kStorageSize;
        if (mParameters[tmpIdx].sequence < 0) {
            // No more stored parameters
            break;
        }

        if (mParameters[tmpIdx].sequence <= sequence) {
            index = tmpIdx;
            LOG2("%s, ask %ld, return %ld", __func__, sequence, mParameters[tmpIdx].sequence);
            break;
        }
    }

    if (index == -1) {
        index = mCurrentIndex;
        LOGW("%s: Do not find params of %ld, use %ld instead", __func__,
            sequence, mParameters[mCurrentIndex].sequence);
    }
    return index;
}

} /* namespace icamera */
