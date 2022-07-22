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

#define LOG_TAG "SensorManager"

#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "AiqUtils.h"
#include "SensorManager.h"
#include "PlatformData.h"

namespace icamera {

SensorManager::SensorManager(int cameraId, SensorHwCtrl *sensorHw) :
    mCameraId(cameraId),
    mSensorHwCtrl(sensorHw),
    mModeSwitched(false),
    mPerframeControl(false),
    mLastSofSequence(-1),
    mGainDelay(0),
    mSensorExposureHistoryIndex(-1)
{
    LOG1("@%s mCameraId = %d", __func__, mCameraId);

    CLEAR(mWdrModeSetting);

    if (PlatformData::getGainLag(mCameraId) > 0) {
        mGainDelay = PlatformData::getExposureLag(mCameraId) - PlatformData::getGainLag(mCameraId);
    }
}

SensorManager::~SensorManager()
{
    LOG1("@%s mCameraId = %d", __func__, mCameraId);
}

int SensorManager::init()
{
    AutoMutex l(mLock);
    LOG1("@%s mCameraId = %d", __func__, mCameraId);

    mSensorExposures.reserve(kMaxSensorExposures);
    reset();
    return OK;
}

int SensorManager::deinit()
{
    AutoMutex l(mLock);
    LOG1("@%s mCameraId = %d", __func__, mCameraId);

    return OK;
}

void SensorManager::reset()
{
    mPerframeControl = false,
    mLastSofSequence = -1;

    mModeSwitched = false;

    for (int i = 0; i < kMaxExposureHistory; i++) {
        mSensorExposureHistory[i].clear();
    }

    mSensorExposureHistoryIndex = -1;

    CLEAR(mWdrModeSetting);
    mWdrModeSetting.tuningMode = TUNING_MODE_MAX;

    mSofEventInfo.clear();
}

EventListener *SensorManager::getSofEventListener()
{
    AutoMutex l(mLock);
    LOG1("@%s mCameraId = %d", __func__, mCameraId);

    return this;
}

void SensorManager::handleEvent(EventData eventData)
{
    AutoMutex l(mLock);
    LOG3A("@%s", __func__);

    if (eventData.type == EVENT_ISYS_SOF) {
        LOG3A("sequence = %ld, timestamp = %ld",
                eventData.data.sync.sequence,
                TIMEVAL2USECS(eventData.data.sync.timestamp));
        handleSensorExposure();
        mLastSofSequence = eventData.data.sync.sequence;

        handleSensorModeSwitch(eventData.data.sync.sequence);

        SofEventInfo info;
        info.sequence = eventData.data.sync.sequence;
        info.timestamp = ((long)eventData.data.sync.timestamp.tv_sec) * 1000000
                         + eventData.data.sync.timestamp.tv_usec;
        if (mSofEventInfo.size() >= kMaxSofEventInfo) {
            mSofEventInfo.erase(mSofEventInfo.begin());
        }
        mSofEventInfo.push_back(info);
    }
}

uint64_t SensorManager::getSofTimestamp(long sequence)
{
    AutoMutex l(mLock);

    for (auto info : mSofEventInfo) {
        if (info.sequence == sequence) {
            return info.timestamp;
        }
    }
    return 0;
}

int SensorManager::convertTuningModeToWdrMode(TuningMode tuningMode)
{
    return ((tuningMode == TUNING_MODE_VIDEO_HDR) || (tuningMode == TUNING_MODE_VIDEO_HDR2)) ? 1 : 0;
}

void SensorManager::handleSensorModeSwitch(long sequence)
{
    if (!PlatformData::isEnableHDR(mCameraId) || !mModeSwitched) {
        return;
    }

    LOG3A("@%s, TuningMode %d sequence %ld, sof %ld", __func__, mWdrModeSetting.tuningMode,
              mWdrModeSetting.sequence, sequence);

    if (mWdrModeSetting.sequence <= sequence) {
        int wdrMode = convertTuningModeToWdrMode(mWdrModeSetting.tuningMode);
        LOG3A("@%s, set wdrMode %d sequence %ld, sof %ld", __func__, wdrMode,
              mWdrModeSetting.sequence, sequence);

        if (mSensorHwCtrl->setWdrMode(wdrMode) == OK) {
            mModeSwitched = false;
        }
    }
}

void SensorManager::handleSensorExposure()
{
    if (!mSensorExposures.empty()) {
        SensorExpGroup& exposures = mSensorExposures[0];
        setFrameDuration(exposures[0].sensorParam.line_length_pixels, exposures[0].sensorParam.frame_length_lines);
        setSensorExposureAndGains(exposures);
        mSensorExposures.erase(mSensorExposures.begin());
    } else {
        if (mGainDelay > 0 && mSensorExposureHistoryIndex >= mGainDelay) {
            // If gain setting is postponed, set last gain setting to sensor driver
            int index = mSensorExposureHistoryIndex % kMaxExposureHistory;
            setSensorGains(index);
        }
        mPerframeControl = false;
    }
}

int SensorManager::getCurrentExposureAppliedDelay()
{
    AutoMutex l(mLock);

    return mSensorExposures.size() + PlatformData::getExposureLag(mCameraId);
}

uint32_t SensorManager::updateSensorExposure(SensorExpGroup sensorExposures, bool useSof)
{
    AutoMutex l(mLock);

    long appliedSeq = mLastSofSequence < 0 ? 0 : \
                      mLastSofSequence + PlatformData::getExposureLag(mCameraId);

    if (sensorExposures.empty()) {
        LOGW("%s: No exposure parameter", __func__);
        return ((uint32_t)appliedSeq);
    }

    if (useSof) {
        if (PlatformData::isStatsTriggered(mCameraId)) {
            mSensorExposures.clear();
        }
        mSensorExposures.push_back(sensorExposures);
        mPerframeControl = true;
        appliedSeq += mSensorExposures.size();
    } else if (!mPerframeControl) {
        setFrameDuration(sensorExposures[0].sensorParam.line_length_pixels, sensorExposures[0].sensorParam.frame_length_lines);
        setSensorExposureAndGains(sensorExposures);
    }

    LOG3A("@%s, useSof:%d, mLastSofSequence:%ld, appliedSeq %ld", __func__, useSof,
           mLastSofSequence, appliedSeq);
    return ((uint32_t)appliedSeq);
}

void SensorManager::setSensorExposureAndGains(SensorExpGroup sensorExposures)
{
    mSensorExposureHistoryIndex++;
    mSensorExposureHistory[mSensorExposureHistoryIndex % kMaxExposureHistory] = sensorExposures;

    vector<int> coarseExposures;
    vector<int> fineExposures;
    for (auto exp : sensorExposures) {
        coarseExposures.push_back(exp.sensorParam.coarse_integration_time);
        fineExposures.push_back(exp.sensorParam.fine_integration_time);
    }
    mSensorHwCtrl->setExposure(coarseExposures, fineExposures);

    int64_t index = mSensorExposureHistoryIndex % kMaxExposureHistory;

    // If exposure and gain lag are different, gain setting need to be postponed.
    if (mGainDelay > 0 && mSensorExposureHistoryIndex >= mGainDelay) {
        index = (mSensorExposureHistoryIndex - mGainDelay) % kMaxExposureHistory;
    }

    setSensorGains(index);
}

void SensorManager::setSensorGains(int index)
{
    vector<int> analogGains;
    vector<int> digitalGains;
    for (auto exp : mSensorExposureHistory[index]) {
        analogGains.push_back(exp.sensorParam.analog_gain_code_global);

        int digitalGain = exp.sensorParam.digital_gain_global;
        if (PlatformData::isUsingIspDigitalGain(mCameraId)) {
            digitalGain = AiqUtils::getSensorDigitalGain(mCameraId, exp.realDigitalGain);
        }
        digitalGains.push_back(digitalGain);
    }

    mSensorHwCtrl->setGains(analogGains, digitalGains);
}

int SensorManager::setWdrMode(TuningMode tuningMode, long sequence)
{
    if (!PlatformData::isEnableHDR(mCameraId)) {
        return OK;
    }

    AutoMutex l(mLock);
    LOG3A("@%s, tuningMode %d, sequence %ld", __func__, tuningMode, sequence);
    int ret = OK;

    // Set Wdr Mode after running AIQ first time.
    if (mWdrModeSetting.tuningMode == TUNING_MODE_MAX) {
        int wdrMode = convertTuningModeToWdrMode(tuningMode);
        ret = mSensorHwCtrl->setWdrMode(wdrMode);
        mWdrModeSetting.tuningMode = tuningMode;
        return ret;
    }

    if (mWdrModeSetting.tuningMode != tuningMode) {
        // Save WDR mode and update this mode to driver in SOF event handler.
        //So we know which frame is corrupted and we can skip the corrupted frames.
        LOG3A("@%s, tuningMode %d, sequence %ld", __func__, tuningMode, sequence);
        mWdrModeSetting.tuningMode = tuningMode;
        mWdrModeSetting.sequence = sequence;
        mModeSwitched = true;
    }

    return ret;
}

int SensorManager::setFrameDuration(int lineLengthPixels, int frameLengthLines)
{
    return mSensorHwCtrl->setFrameDuration(lineLengthPixels, frameLengthLines);
}

int SensorManager::setFrameRate(float fps)
{
    return mSensorHwCtrl->setFrameRate(fps);
}

int SensorManager::getSensorInfo(ia_aiq_frame_params &frameParams,
                                 ia_aiq_exposure_sensor_descriptor &sensorDescriptor)
{
    LOG3A("@%s", __func__);
    SensorFrameParams sensorFrameParams;
    CLEAR(sensorFrameParams);

    int ret = PlatformData::calculateFrameParams(mCameraId, sensorFrameParams);
    if (ret == OK) {
        AiqUtils::convertToAiqFrameParam(sensorFrameParams, frameParams);
    }

    ret |= getSensorModeData(sensorDescriptor);

    LOG3A("ia_aiq_frame_params=[%d, %d, %d, %d, %d, %d, %d, %d]",
        frameParams.horizontal_crop_offset,
        frameParams.vertical_crop_offset,
        frameParams.cropped_image_height,
        frameParams.cropped_image_width,
        frameParams.horizontal_scaling_numerator,
        frameParams.horizontal_scaling_denominator,
        frameParams.vertical_scaling_numerator,
        frameParams.vertical_scaling_denominator);

    LOG3A("ia_aiq_exposure_sensor_descriptor=[%f, %d, %d, %d, %d, %d, %d, %d]",
        sensorDescriptor.pixel_clock_freq_mhz,
        sensorDescriptor.pixel_periods_per_line,
        sensorDescriptor.line_periods_per_field,
        sensorDescriptor.line_periods_vertical_blanking,
        sensorDescriptor.coarse_integration_time_min,
        sensorDescriptor.coarse_integration_time_max_margin,
        sensorDescriptor.fine_integration_time_min,
        sensorDescriptor.fine_integration_time_max_margin);

    return ret;
}

/**
 * get sensor mode data (sensor descriptor) from sensor driver
 *
 * \return OK if successfully.
 */
int SensorManager::getSensorModeData(ia_aiq_exposure_sensor_descriptor& sensorData)
{
    int pixel = 0;
    int status =  mSensorHwCtrl->getPixelRate(pixel);
    Check(status != OK, status, "Failed to get pixel clock ret:%d", status);
    sensorData.pixel_clock_freq_mhz = (float)pixel / 1000000;

    int width = 0, height = 0, pixelCode = 0;
    status = mSensorHwCtrl->getActivePixelArraySize(width, height, pixelCode);
    Check(status != OK, status, "Failed to get active pixel array size ret:%d", status);

    int pixel_periods_per_line, line_periods_per_field;
    status = mSensorHwCtrl->getFrameDuration(pixel_periods_per_line, line_periods_per_field);
    Check(status != OK, status, "Failed to get frame Durations ret:%d", status);

    sensorData.pixel_periods_per_line = CLIP(pixel_periods_per_line, USHRT_MAX, 0);
    sensorData.line_periods_per_field = CLIP(line_periods_per_field, USHRT_MAX, 0);

    int coarse_int_time_min, integration_step = 0, integration_max = 0;
    status = mSensorHwCtrl->getExposureRange(coarse_int_time_min, integration_max, integration_step);
    Check(status != OK, status, "Failed to get Exposure Range ret:%d", status);

    sensorData.coarse_integration_time_min = CLIP(coarse_int_time_min, USHRT_MAX, 0);
    sensorData.coarse_integration_time_max_margin = PlatformData::getCITMaxMargin(mCameraId);

    // fine integration is not supported by v4l2
    sensorData.fine_integration_time_min = 0;
    sensorData.fine_integration_time_max_margin = sensorData.pixel_periods_per_line;
    int vblank;
    status = mSensorHwCtrl->getVBlank(vblank);
    Check(status != OK, status, "Failed to get vblank ret:%d", status);
    sensorData.line_periods_vertical_blanking = CLIP(vblank, USHRT_MAX, 0);

    return OK;
}

} /* namespace icamera */
