/*
 * Copyright (C) 2017-2018 Intel Corporation
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

#define LOG_TAG "MetadataConvert"

#include <cmath>
#include <sstream>

#include "Errors.h"
#include "Utils.h"

#include "HALv3Utils.h"
#include "MetadataConvert.h"

#include "ParameterHelper.h"

namespace camera3 {

#define NSEC_PER_SEC 1000000000LLU

// Max resolution supported for preview/video is 1080P
#define MAX_VIDEO_RES (1920*1080)
// Min resolution supported for still capture is QVGA
#define MIN_STILL_RES (320*240)

struct ValuePair {
    int halValue;
    uint8_t androidValue;
};

static const ValuePair antibandingModesTable[] = {
    {icamera::ANTIBANDING_MODE_AUTO,  ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO},
    {icamera::ANTIBANDING_MODE_50HZ,  ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ},
    {icamera::ANTIBANDING_MODE_60HZ,  ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ},
    {icamera::ANTIBANDING_MODE_OFF,   ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF},
};

static const ValuePair aeModesTable[] = {
    {icamera::AE_MODE_AUTO,   ANDROID_CONTROL_AE_MODE_ON},
    {icamera::AE_MODE_MANUAL, ANDROID_CONTROL_AE_MODE_OFF},
};

static const ValuePair awbModesTable[] = {
    {icamera::AWB_MODE_AUTO,            ANDROID_CONTROL_AWB_MODE_AUTO},
    {icamera::AWB_MODE_INCANDESCENT,    ANDROID_CONTROL_AWB_MODE_INCANDESCENT},
    {icamera::AWB_MODE_FLUORESCENT,     ANDROID_CONTROL_AWB_MODE_FLUORESCENT},
    {icamera::AWB_MODE_DAYLIGHT,        ANDROID_CONTROL_AWB_MODE_DAYLIGHT},
    {icamera::AWB_MODE_FULL_OVERCAST,   ANDROID_CONTROL_AWB_MODE_TWILIGHT},
    {icamera::AWB_MODE_PARTLY_OVERCAST, ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT},
    {icamera::AWB_MODE_MANUAL_COLOR_TRANSFORM, ANDROID_CONTROL_AWB_MODE_OFF},
};

static const ValuePair afModesTable[] = {
    {icamera::AF_MODE_OFF,                ANDROID_CONTROL_AF_MODE_OFF},
    {icamera::AF_MODE_AUTO,               ANDROID_CONTROL_AF_MODE_AUTO},
    {icamera::AF_MODE_MACRO,              ANDROID_CONTROL_AF_MODE_MACRO},
    {icamera::AF_MODE_CONTINUOUS_VIDEO,   ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO},
    {icamera::AF_MODE_CONTINUOUS_PICTURE, ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE},
};

static const ValuePair afTriggerTable[] = {
    {icamera::AF_TRIGGER_START,  ANDROID_CONTROL_AF_TRIGGER_START},
    {icamera::AF_TRIGGER_CANCEL, ANDROID_CONTROL_AF_TRIGGER_CANCEL},
    {icamera::AF_TRIGGER_IDLE,   ANDROID_CONTROL_AF_TRIGGER_IDLE},
};

static const ValuePair dvsModesTable[] = {
    {icamera::VIDEO_STABILIZATION_MODE_OFF, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF},
    {icamera::VIDEO_STABILIZATION_MODE_ON,  ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON},
};

static const ValuePair effectModesTable[] = {
    {icamera::CAM_EFFECT_NONE,     ANDROID_CONTROL_EFFECT_MODE_OFF},
    {icamera::CAM_EFFECT_MONO,     ANDROID_CONTROL_EFFECT_MODE_MONO},
    {icamera::CAM_EFFECT_SEPIA,    ANDROID_CONTROL_EFFECT_MODE_SEPIA},
    {icamera::CAM_EFFECT_NEGATIVE, ANDROID_CONTROL_EFFECT_MODE_NEGATIVE},
};

static int getAndroidValue(int halValue, const ValuePair* table, int tableCount, uint8_t& mode)
{
    Check(!table, icamera::BAD_VALUE, "null table!");
    for (int i = 0; i < tableCount; i++) {
        if (halValue == table[i].halValue) {
            mode = table[i].androidValue;
            return icamera::OK;
        }
    }
    return icamera::BAD_VALUE;
}

static int getHalValue(uint8_t androidValue, const ValuePair* table, int tableCount, int& mode)
{
    Check(!table, icamera::BAD_VALUE, "null table!");
    for (int i = 0; i < tableCount; i++) {
        if (androidValue == table[i].androidValue) {
            mode = table[i].halValue;
            return icamera::OK;
        }
    }
    return icamera::BAD_VALUE;
}

MetadataConvert::MetadataConvert(int cameraId) :
    mCameraId(cameraId)
{
    LOG1("@%s", __func__);
}

MetadataConvert::~MetadataConvert()
{
    LOG1("@%s", __func__);
}

int MetadataConvert::constructDefaultMetadata(android::CameraMetadata *settings)
{
    LOG1("@%s", __func__);

    int maxRegions[3] = {1,0,1};
    settings->update(ANDROID_CONTROL_MAX_REGIONS, maxRegions, 3);

    // AE, AF region (AWB region is not supported)
    int meteringRegion[5] = {0, 0, 0, 0, 0};
    settings->update(ANDROID_CONTROL_AE_REGIONS, meteringRegion, 5);
    settings->update(ANDROID_CONTROL_AF_REGIONS, meteringRegion, 5);

    // Control AE, AF, AWB
    uint8_t mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    settings->update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
    int32_t ev = 0;
    settings->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ev, 1);
    uint8_t lock = ANDROID_CONTROL_AE_LOCK_OFF;
    settings->update(ANDROID_CONTROL_AE_LOCK, &lock, 1);
    mode = ANDROID_CONTROL_AE_MODE_ON;
    settings->update(ANDROID_CONTROL_AE_MODE, &mode, 1);
    int32_t fpsRange[] = { 10, 30 };
    settings->update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fpsRange, 2);
    mode = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    settings->update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &mode, 1);
    mode = ANDROID_CONTROL_AE_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AE_STATE, &mode, 1);

    mode = ANDROID_CONTROL_AF_MODE_OFF;
    settings->update(ANDROID_CONTROL_AF_MODE, &mode, 1);
    mode = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    settings->update(ANDROID_CONTROL_AF_TRIGGER, &mode, 1);
    mode = ANDROID_CONTROL_AF_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AF_STATE, &mode, 1);

    lock = ANDROID_CONTROL_AWB_LOCK_OFF;
    settings->update(ANDROID_CONTROL_AWB_LOCK, &lock, 1);
    mode = ANDROID_CONTROL_AWB_MODE_AUTO;
    settings->update(ANDROID_CONTROL_AWB_MODE, &mode, 1);
    mode = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    settings->update(ANDROID_CONTROL_AWB_STATE, &mode, 1);

    // Control others
    mode = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    settings->update(ANDROID_CONTROL_CAPTURE_INTENT, &mode, 1);
    mode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings->update(ANDROID_CONTROL_EFFECT_MODE, &mode, 1);
    mode = ANDROID_CONTROL_MODE_AUTO;
    settings->update(ANDROID_CONTROL_MODE, &mode, 1);
    mode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    settings->update(ANDROID_CONTROL_SCENE_MODE, &mode, 1);
    mode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &mode, 1);

    // Edge
    mode = ANDROID_EDGE_MODE_OFF;
    settings->update(ANDROID_EDGE_MODE, &mode, 1);

    // Noise reduction
    mode = ANDROID_NOISE_REDUCTION_MODE_OFF;
    settings->update(ANDROID_NOISE_REDUCTION_MODE, &mode, 1);

    // Flash
    mode = ANDROID_FLASH_MODE_OFF;
    settings->update(ANDROID_FLASH_MODE, &mode, 1);
    mode = ANDROID_FLASH_STATE_READY;
    settings->update(ANDROID_FLASH_STATE, &mode, 1);

    // Hot pixel
    mode = ANDROID_HOT_PIXEL_MODE_FAST;
    settings->update(ANDROID_HOT_PIXEL_MODE, &mode, 1);

    // Black level
    lock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings->update(ANDROID_BLACK_LEVEL_LOCK, &lock, 1);

    // Lens
    float value_f = 0.0;
    settings->update(ANDROID_LENS_FOCAL_LENGTH, &value_f, 1);
    settings->update(ANDROID_LENS_FOCUS_DISTANCE, &value_f, 1);
    settings->update(ANDROID_LENS_APERTURE, &value_f, 1);
    mode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &mode, 1);
    int64_t value_i64 = 0;
    settings->update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &value_i64, 1);

    // Sync
    int64_t frameNumber = ANDROID_SYNC_FRAME_NUMBER_UNKNOWN;
    settings->update(ANDROID_SYNC_FRAME_NUMBER, &frameNumber, 1);

    // Request
    mode = ANDROID_REQUEST_TYPE_CAPTURE;
    settings->update(ANDROID_REQUEST_TYPE, &mode, 1);
    mode = ANDROID_REQUEST_METADATA_MODE_NONE;
    settings->update(ANDROID_REQUEST_METADATA_MODE, &mode, 1);

    // Scale
    int32_t region[] = {0, 0, 0, 0};
    settings->update(ANDROID_SCALER_CROP_REGION, region, 4);

    // Statistics
    mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings->update(ANDROID_STATISTICS_FACE_DETECT_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &mode, 1);
    mode = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
    settings->update(ANDROID_STATISTICS_SCENE_FLICKER, &mode, 1);

    // Sensor
    value_i64 = 0;
    settings->update(ANDROID_SENSOR_EXPOSURE_TIME, &value_i64, 1);
    int32_t sensitivity = 0;
    settings->update(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);
    int64_t frameDuration = 33000000;
    settings->update(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);
    int32_t testPattern = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    settings->update(ANDROID_SENSOR_TEST_PATTERN_MODE, &testPattern, 1);

    // Jpeg
    uint8_t quality = 95;
    settings->update(ANDROID_JPEG_QUALITY, &quality, 1);
    quality = 90;
    settings->update(ANDROID_JPEG_THUMBNAIL_QUALITY, &quality, 1);

    camera_metadata_entry entry = settings->find(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    int32_t thumbSize[] = { 0, 0 };
    if (entry.count >= 4) {
        thumbSize[0] = entry.data.i32[2];
        thumbSize[1] = entry.data.i32[3];
    } else {
        LOGE("Thumbnail size should have more than 2 resolutions: 0x0 and non zero size. Debug.");
    }
    settings->update(ANDROID_JPEG_THUMBNAIL_SIZE, thumbSize, 2);

    entry = settings->find(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES);
    if (entry.count > 0) {
        mode = entry.data.u8[0];
        for (uint32_t i = 0; i < entry.count; i++) {
            if (entry.data.u8[i] == ANDROID_TONEMAP_MODE_HIGH_QUALITY) {
                mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
                break;
            }
        }
        settings->update(ANDROID_TONEMAP_MODE, &mode, 1);
    }

    // Color correction
    mode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    settings->update(ANDROID_COLOR_CORRECTION_MODE, &mode, 1);

    float colorTransform[9] = {1.0, 0.0, 0.0,
                               0.0, 1.0, 0.0,
                               0.0, 0.0, 1.0};
    camera_metadata_rational_t transformMatrix[9];
    for (int i = 0; i < 9; i++) {
        transformMatrix[i].numerator = colorTransform[i];
        transformMatrix[i].denominator = 1.0;
    }
    settings->update(ANDROID_COLOR_CORRECTION_TRANSFORM, transformMatrix, 9);

    float colorGains[4] = {1.0, 1.0, 1.0, 1.0};
    settings->update(ANDROID_COLOR_CORRECTION_GAINS, colorGains, 4);

    mode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &mode, 1);

    return icamera::OK;
}

int MetadataConvert::updateDefaultRequestSettings(int type, android::CameraMetadata *settings)
{
    uint8_t intent =
        (type == CAMERA3_TEMPLATE_PREVIEW)          ? ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW : \
        (type == CAMERA3_TEMPLATE_STILL_CAPTURE)    ? ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE : \
        (type == CAMERA3_TEMPLATE_VIDEO_RECORD)     ? ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD : \
        (type == CAMERA3_TEMPLATE_VIDEO_SNAPSHOT)   ? ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT : \
        (type == CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG) ? ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG : \
        (type == CAMERA3_TEMPLATE_MANUAL)           ? ANDROID_CONTROL_CAPTURE_INTENT_MANUAL : \
        ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
    settings->update(ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);

    uint8_t ctrlMode = ANDROID_CONTROL_MODE_AUTO;
    uint8_t aeMode   = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t awbMode  = ANDROID_CONTROL_AWB_MODE_AUTO;
    uint8_t afMode   = ANDROID_CONTROL_AF_MODE_OFF;

    switch (type) {
    case CAMERA3_TEMPLATE_MANUAL:
        ctrlMode = ANDROID_CONTROL_MODE_OFF;
        aeMode   = ANDROID_CONTROL_AE_MODE_OFF;
        awbMode  = ANDROID_CONTROL_AWB_MODE_OFF;
        afMode   = ANDROID_CONTROL_AF_MODE_OFF;
        break;
    case CAMERA3_TEMPLATE_STILL_CAPTURE:
    case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
    case CAMERA3_TEMPLATE_PREVIEW:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
    case CAMERA3_TEMPLATE_VIDEO_RECORD:
    case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
    default:
        break;
    }

    // Check if AF mode is supported or not.
    uint32_t tag = ANDROID_CONTROL_AF_AVAILABLE_MODES;
    camera_metadata_entry entry = settings->find(tag);
    bool found = false;
    if (entry.count > 0) {
       for (size_t i = 0; i < entry.count; i++) {
           if (afMode == entry.data.u8[i]) found = true;
       }
    }
    if (!found) afMode = ANDROID_CONTROL_AF_MODE_OFF;

    LOG2("%s, type %d, ctrlMode %d, aeMode %d, awbMode %d, afMode %d",
            __func__, type, ctrlMode, aeMode, awbMode, afMode);
    settings->update(ANDROID_CONTROL_MODE,     &ctrlMode, 1);
    settings->update(ANDROID_CONTROL_AE_MODE,  &aeMode,   1);
    settings->update(ANDROID_CONTROL_AWB_MODE, &awbMode,  1);
    settings->update(ANDROID_CONTROL_AF_MODE, &afMode,  1);

    return icamera::OK;
}

int MetadataConvert::requestMetadataToHALMetadata(const android::CameraMetadata &settings,
                                                  icamera::Parameters *parameter)
{
    LOG1("@%s: settings entry count %d", __func__, settings.entryCount());
    Check(parameter == nullptr, icamera::BAD_VALUE, "%s, parameter is nullptr", __func__);

    // ANDROID_COLOR_CORRECTION
    convertColorCorrectionMetadata(settings, parameter);

    // ANDROID_CONTROL
    convertControlMetadata(settings, parameter);

    // ANDROID_DEMOSAIC
    // ANDROID_EDGE
    // ANDROID_HOT_PIXEL
    // ANDROID_NOISE_REDUCTION
    // ANDROID_SHADING
    // ANDROID_TONEMAP
    // ANDROID_BLACK_LEVEL

    // ANDROID_FLASH

    // ANDROID_JPEG
    convertJpegMetadata(settings, parameter);

    // ANDROID_LENS

    // ANDROID_SCALER

    // ANDROID_SENSOR
    convertSensorMetadata(settings, parameter);

    // ANDROID_STATISTICS

    // ANDROID_LED

    // ANDROID_REPROCESS

     return icamera::OK;
}

int MetadataConvert::HALMetadataToRequestMetadata(const icamera::Parameters &parameter,
                                                  android::CameraMetadata *settings)
{
    LOG1("@%s", __func__);

    Check(settings == nullptr, icamera::BAD_VALUE, "%s, settings is nullptr", __func__);

    // ANDROID_COLOR_CORRECTION
    convertColorCorrectionParameter(parameter, settings);

    // ANDROID_CONTROL
    convertControlParameter(parameter, settings);

    // ANDROID_FLASH
    // ANDROID_FLASH_INFO
    convertFlashParameter(parameter, settings);

    // ANDROID_JPEG

    // ANDROID_LENS
    // ANDROID_LENS_INFO
    convertLensParameter(parameter, settings);

    // ANDROID_QUIRKS

    // ANDROID_REQUEST

    // ANDROID_SCALER

    // ANDROID_SENSOR
    // ANDROID_SENSOR_INFO
    convertSensorParameter(parameter, settings);

    // ANDROID_STATISTICS
    // ANDROID_STATISTICS_INFO
    convertStatisticsParameter(parameter, settings);

    // ANDROID_DEMOSAIC,  ANDROID_EDGE,  ANDROID_HOT_PIXEL, ANDROID_NOISE_REDUCTION
    // ANDROID_SHADING, ANDROID_TONEMAP, ANDROID_INFO, ANDROID_BLACK_LEVEL, ANDROID_SYNC
    convertAdvancedFeatureParameter(parameter, settings);

    // ANDROID_LED

    // ANDROID_REPROCESS

    // ANDROID_DEPTH

    LOG1("@%s: convert entry count %d", __func__, settings->entryCount());
    return icamera::OK;
}

int MetadataConvert::HALCapabilityToStaticMetadata(const icamera::Parameters &parameter,
                                                   android::CameraMetadata *settings)
{
    LOG1("@%s", __func__);

    Check(settings == nullptr, icamera::BAD_VALUE, "%s, settings is nullptr", __func__);

    // ANDROID_COLOR_CORRECTION
    uint8_t aberrationAvailable = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, &aberrationAvailable, 1);

    // ANDROID_CONTROL
    fillControlStaticMetadata(parameter, settings);

    // ANDROID_FLASH
    // ANDROID_FLASH_INFO
    uint8_t flashInfoAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    settings->update(ANDROID_FLASH_INFO_AVAILABLE, &flashInfoAvailable, 1);

    // ANDROID_JPEG
    fillJpegStaticMetadata(parameter, settings);

    // ANDROID_LENS
    // ANDROID_LENS_INFO
    fillLensStaticMetadata(parameter, settings);

    // ANDROID_QUIRKS

    // ANDROID_REQUEST
    fillRequestStaticMetadata(parameter, settings);

    // ANDROID_SCALER
    fillScalerStaticMetadata(parameter, settings);

    // ANDROID_SENSOR
    // ANDROID_SENSOR_INFO
    fillSensorStaticMetadata(parameter, settings);

    // ANDROID_STATISTICS
    // ANDROID_STATISTICS_INFO
    fillStatisticsStaticMetadata(parameter, settings);

    // ANDROID_LED
    uint8_t availLeds = ANDROID_LED_AVAILABLE_LEDS_TRANSMIT;
    settings->update(ANDROID_LED_AVAILABLE_LEDS, &availLeds, 1);

    // ANDROID_REPROCESS

    // ANDROID_DEPTH

    fillAdvancedFeatureStaticMetadata(parameter, settings);

    return icamera::OK;
}

int MetadataConvert::convertColorCorrectionMetadata(const android::CameraMetadata &settings,
                                                    icamera::Parameters *parameter)
{
    uint32_t tag = ANDROID_COLOR_CORRECTION_TRANSFORM;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 9) {
        icamera::camera_color_transform_t transform;
        for (size_t i = 0; i < entry.count; i++) {
            transform.color_transform[i / 3][i % 3] =
                    ((float)entry.data.r[i].numerator) / entry.data.r[i].denominator;
        }
        parameter->setColorTransform(transform);
    }

    tag = ANDROID_COLOR_CORRECTION_GAINS;
    entry = settings.find(tag);
    if (entry.count == 4) {
        icamera::camera_color_gains_t gains;
        for (size_t i = 0; i < entry.count; i++) {
            gains.color_gains_rggb[i] = entry.data.f[i];
        }
        parameter->setColorGains(gains);
    }

    return icamera::OK;
}

int MetadataConvert::convertColorCorrectionParameter(const icamera::Parameters &parameter,
                                                     android::CameraMetadata *settings)
{
    icamera::camera_color_transform_t transform;
    if (parameter.getColorTransform(transform) == 0) {
        camera_metadata_rational_t matrix[9];
        for (int i = 0; i < 9; i++) {
            matrix[i].numerator = round(transform.color_transform[i / 3][i % 3] * 1000);
            matrix[i].denominator = 1000;
        }
        settings->update(ANDROID_COLOR_CORRECTION_TRANSFORM, &matrix[0], 9);
    }

    icamera::camera_color_gains_t colorGains;
    if (parameter.getColorGains(colorGains) == 0) {
        settings->update(ANDROID_COLOR_CORRECTION_GAINS, &colorGains.color_gains_rggb[0], 4);
    }

    uint8_t aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings->update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &aberrationMode, 1);

    return icamera::OK;
}

int MetadataConvert::convertControlMetadata(const android::CameraMetadata &settings,
                                            icamera::Parameters *parameter)
{
    int ret = icamera::OK;
    int mode = 0;
    uint32_t tag = ANDROID_CONTROL_AE_MODE;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], aeModesTable, ARRAY_SIZE(aeModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setAeMode((icamera::camera_ae_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AE_LOCK;
    entry = settings.find(tag);
    if (entry.count == 1) {
        bool aeLock = (entry.data.u8[0] == ANDROID_CONTROL_AE_LOCK_ON);
        parameter->setAeLock(aeLock);
    }

    tag = ANDROID_CONTROL_AE_REGIONS;
    entry = settings.find(tag);
    icamera::camera_window_list_t windows;
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAeRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_AE_TARGET_FPS_RANGE;
    entry = settings.find(tag);
    if (entry.count == 2) {
        icamera::camera_range_t range;
        range.min = entry.data.i32[0];
        range.max = entry.data.i32[1];
        parameter->setFpsRange(range);
    }

    tag = ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setAeCompensation(entry.data.i32[0]);
    }

    tag = ANDROID_CONTROL_AE_ANTIBANDING_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], antibandingModesTable, ARRAY_SIZE(antibandingModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setAntiBandingMode((icamera::camera_antibanding_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], afModesTable, ARRAY_SIZE(afModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setAfMode((icamera::camera_af_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_TRIGGER;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], afTriggerTable, ARRAY_SIZE(afTriggerTable), mode);
        if (ret == icamera::OK) {
            parameter->setAfTrigger((icamera::camera_af_trigger_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AF_REGIONS;
    entry = settings.find(tag);
    windows.clear();
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAfRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_AWB_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], awbModesTable, ARRAY_SIZE(awbModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setAwbMode((icamera::camera_awb_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_AWB_LOCK;
    entry = settings.find(tag);
    if (entry.count == 1) {
        bool awbLock = (entry.data.u8[0] == ANDROID_CONTROL_AWB_LOCK_ON);
        parameter->setAwbLock(awbLock);
    }

    tag = ANDROID_CONTROL_AWB_REGIONS;
    entry = settings.find(tag);
    windows.clear();
    if (entry.count > 0) {
        if (convertToHalWindow(entry.data.i32, entry.count, &windows) == 0) {
            parameter->setAwbRegions(windows);
        }
    }

    tag = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], dvsModesTable, ARRAY_SIZE(dvsModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setVideoStabilizationMode((icamera::camera_video_stabilization_mode_t)mode);
        }
    }

    tag = ANDROID_CONTROL_EFFECT_MODE;
    entry = settings.find(tag);
    if (entry.count == 1) {
        ret = getHalValue(entry.data.u8[0], effectModesTable, ARRAY_SIZE(effectModesTable), mode);
        if (ret == icamera::OK) {
            parameter->setImageEffect((icamera::camera_effect_mode_t)mode);
        }
    }

    return icamera::OK;
}

int MetadataConvert::convertControlParameter(const icamera::Parameters &parameter,
                                             android::CameraMetadata *settings)
{
    int ret = icamera::OK;
    uint8_t mode = 0;
    icamera::camera_ae_mode_t aeMode;
    if (parameter.getAeMode(aeMode) == 0) {
        ret = getAndroidValue(aeMode,aeModesTable, ARRAY_SIZE(aeModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AE_MODE, &mode, 1);
        }
    }

    bool aeLock;
    if (parameter.getAeLock(aeLock) == 0) {
        uint8_t mode = aeLock ? ANDROID_CONTROL_AE_LOCK_ON
                              : ANDROID_CONTROL_AE_LOCK_OFF;
        settings->update(ANDROID_CONTROL_AE_LOCK, &mode, 1);
    }

    icamera::camera_window_list_t windows;
    parameter.getAeRegions(windows);
    int count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_range_t range;
    if (parameter.getFpsRange(range) == 0) {
        int fps[2] = {(int)range.min, (int)range.max};
        settings->update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &fps[0], 2);
    }

    int ev;
    if (parameter.getAeCompensation(ev) == 0) {
        settings->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ev, 1);
    }

    icamera::camera_antibanding_mode_t antiMode;
    if (parameter.getAntiBandingMode(antiMode) == 0) {
        ret = getAndroidValue(antiMode, antibandingModesTable, ARRAY_SIZE(antibandingModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
        }
    }

    icamera::camera_af_mode_t afMode;
    if (parameter.getAfMode(afMode) == 0) {
        ret = getAndroidValue(afMode, afModesTable, ARRAY_SIZE(afModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AF_MODE, &mode, 1);
        }
    }

    windows.clear();
    parameter.getAfRegions(windows);
    count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AF_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_awb_mode_t awbMode;
    if (parameter.getAwbMode(awbMode) == 0) {
        ret = getAndroidValue(awbMode, awbModesTable, ARRAY_SIZE(awbModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_AWB_MODE, &mode, 1);
        }
    }

    bool awbLock;
    if (parameter.getAwbLock(awbLock) == 0) {
        uint8_t mode = awbLock ? ANDROID_CONTROL_AWB_LOCK_ON
                               : ANDROID_CONTROL_AWB_LOCK_OFF;
        settings->update(ANDROID_CONTROL_AWB_LOCK, &mode, 1);
    }

    windows.clear();
    parameter.getAwbRegions(windows);
    count = windows.size() * 5;
    if (count > 0) {
        int regions[count];
        count = convertToMetadataRegion(windows, windows.size() * 5, regions);
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AWB_REGIONS, &regions[0], count);
        }
    }

    icamera::camera_video_stabilization_mode_t dvsMode;
    if (parameter.getVideoStabilizationMode(dvsMode) == 0) {
        ret = getAndroidValue(awbMode, dvsModesTable, ARRAY_SIZE(dvsModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &mode, 1);
        }
    }

    icamera::camera_effect_mode_t effectMode;
    if (parameter.getImageEffect(effectMode) == 0) {
        ret = getAndroidValue(awbMode, effectModesTable, ARRAY_SIZE(effectModesTable), mode);
        if (ret == icamera::OK) {
            settings->update(ANDROID_CONTROL_EFFECT_MODE, &mode, 1);
        }
    }

    return icamera::OK;
}

int MetadataConvert::convertJpegMetadata(const android::CameraMetadata &settings,
                                         icamera::Parameters *parameter)
{
    uint32_t tag = ANDROID_JPEG_GPS_COORDINATES;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 3) {
        parameter->setJpegGpsCoordinates(entry.data.d);
    }

    tag = ANDROID_JPEG_GPS_PROCESSING_METHOD;
    entry = settings.find(tag);
    if (entry.count >= 1) {
        char data[entry.count + 1];
        MEMCPY_S(data, sizeof(data), entry.data.u8, entry.count);
        data[entry.count] = 0;
        parameter->setJpegGpsProcessingMethod(data);
    }

    tag = ANDROID_JPEG_GPS_TIMESTAMP;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setJpegGpsTimeStamp(entry.data.i64[0]);
    }

    tag = ANDROID_JPEG_ORIENTATION;
    entry = settings.find(tag);
    if (entry.count == 1) {
        parameter->setJpegRotation(entry.data.i32[0]);
    }

    tag = ANDROID_JPEG_QUALITY;
    entry = settings.find(tag);
    if (entry.count == 1) {
        int quality = entry.data.u8[0];
        parameter->setJpegQuality(quality);
    }

    tag = ANDROID_JPEG_THUMBNAIL_QUALITY;
    entry = settings.find(tag);
    if (entry.count == 1) {
        int quality = entry.data.u8[0];
        parameter->setJpegThumbnailQuality(quality);
    }

    tag = ANDROID_JPEG_THUMBNAIL_SIZE;
    entry = settings.find(tag);
    if (entry.count == 2) {
        icamera::camera_resolution_t size;
        size.width  = entry.data.i32[0];
        size.height = entry.data.i32[1];
        parameter->setJpegThumbnailSize(size);
    }

    return icamera::OK;
}

int MetadataConvert::convertSensorMetadata(const android::CameraMetadata &settings,
                                           icamera::Parameters *parameter)
{
    // Check control mode
    bool manualAeControl = false;
    uint32_t tag = ANDROID_CONTROL_AE_MODE;
    camera_metadata_ro_entry entry = settings.find(tag);
    if (entry.count == 1 && entry.data.u8[0] == ANDROID_CONTROL_AE_MODE_OFF) {
        manualAeControl = true;
    }

    if (manualAeControl) {
        tag = ANDROID_SENSOR_EXPOSURE_TIME;
        entry = settings.find(tag);
        if (entry.count == 1) {
            parameter->setExposureTime(entry.data.i64[0] / 1000); // ns -> us
        }

        tag = ANDROID_SENSOR_SENSITIVITY;
        entry = settings.find(tag);
        if (entry.count == 1) {
            float sensitivity = log10(entry.data.i32[0]) * 20.0; // ISO -> db
            parameter->setSensitivityGain(sensitivity);
        }

        tag = ANDROID_SENSOR_FRAME_DURATION;
        entry = settings.find(tag);
        if (entry.count == 1) {
            float fps = NSEC_PER_SEC / entry.data.i64[0];
            parameter->setFrameRate(fps);
        }
    }

    return icamera::OK;
}

int MetadataConvert::convertSensorParameter(const icamera::Parameters &parameter,
                                            android::CameraMetadata *settings)
{
    int64_t exposure;
    if (parameter.getExposureTime(exposure) == 0) {
        int64_t time = exposure * 1000; // us -> ns
        settings->update(ANDROID_SENSOR_EXPOSURE_TIME, &time, 1);
    }

    float sensitivity;
    if (parameter.getSensitivityGain(sensitivity) == 0) {
        int32_t iso = round(pow(10, (sensitivity / 20))); // db -> ISO
        settings->update(ANDROID_SENSOR_SENSITIVITY, &iso, 1);
    }

    float fps;
    if (parameter.getFrameRate(fps) == icamera::OK) {
        int64_t duration = NSEC_PER_SEC / fps;
        settings->update(ANDROID_SENSOR_FRAME_DURATION, &duration, 1);
    }

    float focal = 0.0;
    parameter.getFocalLength(focal);
    if (focal < EPSILON) {
        icamera::CameraMetadata meta;
        icamera::ParameterHelper::copyMetadata(parameter, &meta);

        icamera_metadata_entry entry = meta.find(CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
        if (entry.count >= 1) {
            focal = entry.data.f[0];
        }
    }
    settings->update(ANDROID_LENS_FOCAL_LENGTH, &focal, 1);

    return icamera::OK;
}

int MetadataConvert::convertLensParameter(const icamera::Parameters &parameter,
                                          android::CameraMetadata *settings)
{
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_LENS_INFO_AVAILABLE_APERTURES;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_APERTURE, entry.data.f, 1);
    }

    return icamera::OK;
}

int MetadataConvert::convertStatisticsParameter(const icamera::Parameters & /*parameter*/,
                                                android::CameraMetadata *settings)
{
    uint8_t lensShadingMapMode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensShadingMapMode, 1);

    uint8_t fdMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings->update(ANDROID_STATISTICS_FACE_DETECT_MODE, &fdMode, 1);

    return icamera::OK;
}

int MetadataConvert::convertFlashParameter(const icamera::Parameters & /*parameter*/,
                                           android::CameraMetadata *settings)
{
    uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    settings->update(ANDROID_FLASH_MODE, &flashMode, 1);

    return icamera::OK;
}

int MetadataConvert::convertAdvancedFeatureParameter(const icamera::Parameters & /*parameter*/,
                                                     android::CameraMetadata *settings)
{
    // ANDROID_DEMOSAIC

    // ANDROID_EDGE
    uint8_t edgeMode = ANDROID_EDGE_MODE_OFF;
    settings->update(ANDROID_EDGE_MODE, &edgeMode, 1);

    // ANDROID_HOT_PIXEL

    // ANDROID_NOISE_REDUCTION
    uint8_t nrMode = ANDROID_NOISE_REDUCTION_MODE_OFF;
    settings->update(ANDROID_NOISE_REDUCTION_MODE, &nrMode, 1);

    // ANDROID_SHADING
    // ANDROID_TONEMAP
    // ANDROID_INFO
    // ANDROID_BLACK_LEVEL
    // ANDROID_SYNC

    return icamera::OK;
}

void MetadataConvert::fillControlStaticMetadata(const icamera::Parameters &parameter,
                                                android::CameraMetadata *settings)
{
    int ret = icamera::OK;
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    icamera_metadata_entry entry = meta.find(CAMERA_CONTROL_AVAILABLE_MODES);
    if (entry.count != 0) {
        settings->update(ANDROID_CONTROL_AVAILABLE_MODES, entry.data.u8, entry.count);
    }

    vector <icamera::camera_antibanding_mode_t> antibandingModes;
    parameter.getSupportedAntibandingMode(antibandingModes);
    if (antibandingModes.size() > 0) {
        int size = antibandingModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(antibandingModes[i], antibandingModesTable, ARRAY_SIZE(antibandingModesTable), data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, data, count);
        }
    } else {
        LOGW("No antibanding modes provided!");
    }

    vector <icamera::camera_ae_mode_t> availAeModes;
    parameter.getSupportedAeMode(availAeModes);
    if (availAeModes.size() > 0) {
        int size = availAeModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAeModes[i], aeModesTable, ARRAY_SIZE(aeModesTable), data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AE_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No ae modes provided!");
    }

    uint8_t aeLockAvailable = parameter.getAeLockAvailable() ? \
            ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE : ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
    settings->update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLockAvailable, 1);

    icamera::camera_range_array_t fpsRanges;
    if (parameter.getSupportedFpsRange(fpsRanges) == 0) {
        int count = fpsRanges.size() * 2;
        int32_t data[count];
        for (size_t i = 0; i < fpsRanges.size(); i++) {
            data[i * 2] = (int32_t)fpsRanges[i].min;
            data[i * 2 + 1] = (int32_t)fpsRanges[i].max;
        }
        settings->update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, data, count);
    } else {
        LOGW("No fps ranges provided!");
    }

    icamera::camera_range_t aeCompensationRange;
    if (parameter.getAeCompensationRange(aeCompensationRange) == 0) {
        int32_t data[2];
        data[0] = (int32_t)aeCompensationRange.min;
        data[1] = (int32_t)aeCompensationRange.max;
        settings->update(ANDROID_CONTROL_AE_COMPENSATION_RANGE, data, 2);
    } else {
        LOGW("No ae compensation range provided!");
    }

    icamera::camera_rational_t aeCompensationStep;
    if (parameter.getAeCompensationStep(aeCompensationStep) == 0) {
        camera_metadata_rational rational;
        rational.numerator = aeCompensationStep.numerator;
        rational.denominator = aeCompensationStep.denominator;
        settings->update(ANDROID_CONTROL_AE_COMPENSATION_STEP, &rational, 1);
    } else {
        LOGW("No ae compensation step provided!");
    }

    vector <icamera::camera_af_mode_t> availAfModes;
    parameter.getSupportedAfMode(availAfModes);
    if (availAfModes.size() > 0) {
        int size = availAfModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAfModes[i], afModesTable, ARRAY_SIZE(afModesTable), data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AF_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No af modes provided!");
    }

    uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings->update(ANDROID_CONTROL_AVAILABLE_EFFECTS, &effectMode, 1);

    uint8_t availSceneModes[1];
    availSceneModes[0] = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    settings->update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, availSceneModes, 1);

    icamera::camera_video_stabilization_list_t availDvsModes;
    parameter.getSupportedVideoStabilizationMode(availDvsModes);
    if (availDvsModes.size() > 0) {
        int size = availDvsModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availDvsModes[i], dvsModesTable, ARRAY_SIZE(dvsModesTable), data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, data, count);
        }
    } else {
        LOGW("No video stablization modes provided!");
    }

    vector <icamera::camera_awb_mode_t> availAwbModes;
    parameter.getSupportedAwbMode(availAwbModes);
    if (availAwbModes.size() > 0) {
        int size = availAwbModes.size();
        uint8_t data[size];
        int count = 0;
        for (int i = 0; i < size; i++) {
            ret = getAndroidValue(availAwbModes[i], awbModesTable, ARRAY_SIZE(awbModesTable), data[count]);
            if (ret == icamera::OK) {
                count++;
            }
        }
        if (count > 0) {
            settings->update(ANDROID_CONTROL_AWB_AVAILABLE_MODES, data, count);
        }
    } else {
        LOGW("No awb modes provided!");
    }

    uint8_t awbLockAvailable = parameter.getAwbLockAvailable() ? \
            ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE : ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
    settings->update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLockAvailable, 1);
}

void MetadataConvert::fillScalerStaticMetadata(const icamera::Parameters &parameter,
                                               android::CameraMetadata *settings)
{
// stream configuration: fmt, w, h, type
#define SIZE_OF_STREAM_CONFIG 4
// duration: fmt, w, h, ns
#define SIZE_OF_DURATION 4

    float maxDigitalZoom = 1.0;
    settings->update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxDigitalZoom, 1);

    uint8_t type = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
    settings->update(ANDROID_SCALER_CROPPING_TYPE, &type, 1);

    icamera::supported_stream_config_array_t configs;
    parameter.getSupportedStreamConfig(configs);
    if (configs.size() == 0) {
        LOGW("No stream configs provided!");
        return;
    }

    // Select one supported YUV format as implementation_defined
    int defaultImplementationDefined = -1;
    bool foundYuv = false;
    for (auto & cfg : configs) {
        // Use nv12 as default;
        if (cfg.format == V4L2_PIX_FMT_NV12) {
            defaultImplementationDefined = V4L2_PIX_FMT_NV12;
            break;
        }
        if (!foundYuv
            && (cfg.format == V4L2_PIX_FMT_NV21
               || cfg.format == V4L2_PIX_FMT_NV16
               || cfg.format == V4L2_PIX_FMT_YUYV
               || cfg.format == V4L2_PIX_FMT_UYVY
               || cfg.format == V4L2_PIX_FMT_YUV420
               || cfg.format == V4L2_PIX_FMT_YVU420
               || cfg.format == V4L2_PIX_FMT_YUV422P)) {
            defaultImplementationDefined = cfg.format;
            foundYuv = true;
        }
    }

    int* configData = new int[configs.size() * 3 * SIZE_OF_STREAM_CONFIG];
    int64_t* durationData = new int64_t[configs.size() * 3 * SIZE_OF_DURATION];
    int64_t* stallDurationData = new int64_t[configs.size() * 3 * SIZE_OF_DURATION];
    unsigned int configCount = 0;
    unsigned int durationCount = 0;
    unsigned int stallDurationCount = 0;

    // get available thumbnail sizes
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);
    icamera_metadata_entry entry = meta.find(CAMERA_JPEG_AVAILABLE_THUMBNAIL_SIZES);

    for (auto & cfg : configs) {
        // Currently icamera only support YUV/raw
        // and cal support YUV/JPEG.
        if (cfg.format != defaultImplementationDefined) {
            continue;
        }

        bool skip = false;
        // filter out the size that dedicated for thumbnail
        for (size_t i = 0; i < entry.count; i += 2) {
            if (cfg.width == entry.data.i32[i] && cfg.height == entry.data.i32[i + 1]) {
                LOG1("@%s skip cfg %dx%d that's for jpeg thumbnail", __func__, cfg.width, cfg.height);
                skip = true;
                break;
            }
        }
        if (skip)
            continue;

        int streamRes = cfg.width * cfg.height;
        // For implementation_defined
        if (streamRes <= MAX_VIDEO_RES) {
            configData[configCount * SIZE_OF_STREAM_CONFIG] = ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 1] = cfg.width;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 2] = cfg.height;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
            configCount++;
            durationData[durationCount * SIZE_OF_DURATION] = ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED;
            durationData[durationCount * SIZE_OF_DURATION + 1] = cfg.width;
            durationData[durationCount * SIZE_OF_DURATION + 2] = cfg.height;
            durationData[durationCount * SIZE_OF_DURATION + 3] = NSEC_PER_SEC/cfg.maxVideoFps;
            durationCount++;
        }

        if (streamRes >= MIN_STILL_RES) {
            // For blob
            configData[configCount * SIZE_OF_STREAM_CONFIG] = ANDROID_SCALER_AVAILABLE_FORMATS_BLOB;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 1] = cfg.width;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 2] = cfg.height;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
            configCount++;
            durationData[durationCount * SIZE_OF_DURATION] = ANDROID_SCALER_AVAILABLE_FORMATS_BLOB;
            durationData[durationCount * SIZE_OF_DURATION + 1] = cfg.width;
            durationData[durationCount * SIZE_OF_DURATION + 2] = cfg.height;
            durationData[durationCount * SIZE_OF_DURATION + 3] = NSEC_PER_SEC/cfg.maxVideoFps;
            durationCount++;
            stallDurationData[stallDurationCount * SIZE_OF_DURATION] = ANDROID_SCALER_AVAILABLE_FORMATS_BLOB;
            stallDurationData[stallDurationCount * SIZE_OF_DURATION + 1] = cfg.width;
            stallDurationData[stallDurationCount * SIZE_OF_DURATION + 2] = cfg.height;
            stallDurationData[stallDurationCount * SIZE_OF_DURATION + 3] = NSEC_PER_SEC/cfg.maxCaptureFps;
            stallDurationCount++;

            // For ycbcr_420_888
            configData[configCount * SIZE_OF_STREAM_CONFIG] = ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 1] = cfg.width;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 2] = cfg.height;
            configData[configCount * SIZE_OF_STREAM_CONFIG + 3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
            configCount++;
            durationData[durationCount * SIZE_OF_DURATION] = ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888;
            durationData[durationCount * SIZE_OF_DURATION + 1] = cfg.width;
            durationData[durationCount * SIZE_OF_DURATION + 2] = cfg.height;
            durationData[durationCount * SIZE_OF_DURATION + 3] = NSEC_PER_SEC/cfg.maxVideoFps;
            durationCount++;
        }
    }
    settings->update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configData, configCount * SIZE_OF_STREAM_CONFIG);
    settings->update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, durationData, durationCount * SIZE_OF_DURATION);
    settings->update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stallDurationData, stallDurationCount * SIZE_OF_DURATION);

    delete []configData;
    delete []durationData;
    delete []stallDurationData;
}

void MetadataConvert::fillSensorStaticMetadata(const icamera::Parameters &parameter,
                                               android::CameraMetadata *settings)
{
    icamera::camera_range_t timeRange;
    // Fill it if it is supported
    if (parameter.getSupportedSensorExposureTimeRange(timeRange) == 0) {
        int64_t range[2];
        range[0] = timeRange.min * 1000LLU; // us -> ns
        range[1] = timeRange.max * 1000LLU; // us -> ns
        settings->update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, range, 2);
        settings->update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &(range[1]), 1);
    } else {
        LOGW("No SensorExposureTimeRange provided!");
    }

    icamera::camera_range_t sensitivityRange;
    if (parameter.getSupportedSensorSensitivityRange(sensitivityRange) == 0) {
        int32_t range[2];
        range[0] = (int32_t)sensitivityRange.min;
        range[1] = (int32_t)sensitivityRange.max;
        settings->update(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, range, 2);
    } else {
        LOGW("No SensorSensitivityRange provided!");
    }

    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE;
    icamera_metadata_entry entry = meta.find(tag);
    // Check if the count is correct
    if (entry.count == 4) {
        settings->update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE;
    entry = meta.find(tag);
    if (entry.count == 2) {
        settings->update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_PHYSICAL_SIZE;
    entry = meta.find(tag);
    if (entry.count == 2) {
        settings->update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, entry.data.f, entry.count);
    }

    tag = CAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, entry.data.u8, entry.count);
    }

    tag = CAMERA_SENSOR_AVAILABLE_TEST_PATTERN_MODES;
    entry = meta.find(tag);
    if (entry.count != 0) {
        settings->update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, entry.data.i32, entry.count);
    }

    int32_t whiteLevel = 0;
    settings->update(ANDROID_SENSOR_INFO_WHITE_LEVEL, &whiteLevel, 1);

    int32_t blackLevelPattern[4] = {0, 0, 0, 0};
    settings->update(ANDROID_SENSOR_BLACK_LEVEL_PATTERN, blackLevelPattern, 4);

    uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    settings->update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestampSource, 1);

    camera_metadata_rational_t baseGainFactor = {0, 1};
    settings->update(ANDROID_SENSOR_BASE_GAIN_FACTOR, &baseGainFactor, 1);

    int32_t maxAnalogSensitivity = 0;
    settings->update(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, &maxAnalogSensitivity, 1);

    int32_t orientation = 0;
    tag = CAMERA_SENSOR_ORIENTATION;
    entry = meta.find(tag);
    if (entry.count == 1) {
        orientation = entry.data.u8[0];
    }
    settings->update(ANDROID_SENSOR_ORIENTATION, &orientation, 1);

    int32_t profileHueSatMapDimensions[3] = {0, 0, 0};
    settings->update(ANDROID_SENSOR_PROFILE_HUE_SAT_MAP_DIMENSIONS, profileHueSatMapDimensions, 3);
}

void MetadataConvert::fillLensStaticMetadata(const icamera::Parameters &parameter,
                                             android::CameraMetadata *settings)
{
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_LENS_INFO_AVAILABLE_APERTURES;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_APERTURES, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_AVAILABLE_FILTER_DENSITIES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_HYPERFOCAL_DISTANCE;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_AVAILABLE_FILTER_DENSITIES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_MINIMUM_FOCUS_DISTANCE;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, entry.data.f, entry.count);
    }

    tag = CAMERA_LENS_INFO_SHADING_MAP_SIZE;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_LENS_INFO_SHADING_MAP_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, entry.data.u8, entry.count);
    }

    tag = CAMERA_LENS_FACING;
    entry = meta.find(tag);
    uint8_t lensFacing = ANDROID_LENS_FACING_BACK;
    if (entry.count == 1) {
        lensFacing = entry.data.u8[0];
    }
    settings->update(ANDROID_LENS_FACING, &lensFacing, 1);

    uint8_t availableOpticalStabilization = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings->update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, &availableOpticalStabilization, 1);
}

void MetadataConvert::fillRequestStaticMetadata(const icamera::Parameters &parameter,
                                                android::CameraMetadata *settings)
{
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_REQUEST_MAX_NUM_OUTPUT_STREAMS;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, entry.data.i32, entry.count);
    }

    tag = CAMERA_REQUEST_PIPELINE_MAX_DEPTH;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, entry.data.u8, entry.count);
    }

    tag = CAMERA_REQUEST_AVAILABLE_CAPABILITIES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, entry.data.u8, entry.count);
    }

    int32_t maxNumInputStreams = 0;
    settings->update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &maxNumInputStreams, 1);

    int32_t partialResultCount = 1;
    settings->update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partialResultCount, 1);

    int32_t requestKeysBasic[] = {
        ANDROID_CONTROL_AE_LOCK,
        ANDROID_CONTROL_AWB_LOCK,
        ANDROID_SENSOR_FRAME_DURATION,
        ANDROID_CONTROL_CAPTURE_INTENT,
        ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE
    };
    size_t requestKeysCnt =
            sizeof(requestKeysBasic)/sizeof(requestKeysBasic[0]);
    settings->update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, requestKeysBasic, requestKeysCnt);

    int32_t resultKeysBasic[] = {
        ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE
    };
    size_t resultKeysCnt =
            sizeof(resultKeysBasic)/sizeof(resultKeysBasic[0]);
    settings->update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
            resultKeysBasic, resultKeysCnt);

    int32_t characteristicsKeysBasic[] = {
        ANDROID_SENSOR_ORIENTATION,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
        ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
        ANDROID_REQUEST_PIPELINE_MAX_DEPTH, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
        ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
        ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
        ANDROID_CONTROL_AE_LOCK_AVAILABLE,
        ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
        ANDROID_SENSOR_FRAME_DURATION,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL
    };
    settings->update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                      characteristicsKeysBasic,
                      sizeof(characteristicsKeysBasic)/sizeof(int32_t));
}

void MetadataConvert::fillStatisticsStaticMetadata(const icamera::Parameters &parameter,
                                                   android::CameraMetadata *settings)
{
    uint8_t availFaceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, &availFaceDetectMode, 1);

    int32_t histogramBucketCount = 0;
    settings->update(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT, &histogramBucketCount, 1);

    int32_t maxFaceCount = 0;
    settings->update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &maxFaceCount, 1);

    int32_t maxHistogramCount = 0;
    settings->update(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT, &maxHistogramCount, 1);

    int32_t maxSharpnessMapValue = 0;
    settings->update(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE, &maxSharpnessMapValue, 1);

    int32_t sharpnessMapSize[2] = {0, 0};
    settings->update(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE, sharpnessMapSize, 2);

    uint8_t availableHotPixelMapModes = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES, &availableHotPixelMapModes, 1);

    uint8_t availableLensShadingMapModes = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings->update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES, &availableLensShadingMapModes, 1);
}

void MetadataConvert::fillJpegStaticMetadata(const icamera::Parameters &parameter,
                                             android::CameraMetadata *settings)
{
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    uint32_t tag = CAMERA_JPEG_MAX_SIZE;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_JPEG_MAX_SIZE, entry.data.i32, entry.count);
    }

    tag = CAMERA_JPEG_AVAILABLE_THUMBNAIL_SIZES;
    entry = meta.find(tag);
    if (entry.count >= 2) {
        settings->update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, entry.data.i32, entry.count);
    }
}

void MetadataConvert::fillAdvancedFeatureStaticMetadata(const icamera::Parameters &parameter,
                                                        android::CameraMetadata *settings)
{
    icamera::CameraMetadata meta;
    icamera::ParameterHelper::copyMetadata(parameter, &meta);

    // ANDROID_DEMOSAIC

    // ANDROID_EDGE
    uint32_t tag = CAMERA_EDGE_AVAILABLE_EDGE_MODES;
    icamera_metadata_entry entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_EDGE_AVAILABLE_EDGE_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_HOT_PIXEL
    tag = CAMERA_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_NOISE_REDUCTION
    tag = CAMERA_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_SHADING

    // ANDROID_TONEMAP
    tag = CAMERA_TONEMAP_MAX_CURVE_POINTS;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_TONEMAP_MAX_CURVE_POINTS, entry.data.i32, entry.count);
    }

    tag = CAMERA_TONEMAP_AVAILABLE_TONE_MAP_MODES;
    entry = meta.find(tag);
    if (entry.count >= 1) {
        settings->update(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, entry.data.u8, entry.count);
    }

    // ANDROID_INFO
    tag = CAMERA_INFO_SUPPORTED_HARDWARE_LEVEL;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, entry.data.u8, entry.count);
    }

    // ANDROID_BLACK_LEVEL

    // ANDROID_SYNC
    tag = CAMERA_SYNC_MAX_LATENCY;
    entry = meta.find(tag);
    if (entry.count == 1) {
        settings->update(ANDROID_SYNC_MAX_LATENCY, entry.data.i32, entry.count);
    }
}

int MetadataConvert::convertToHalWindow(const int32_t* data, int dataCount,
                                        icamera::camera_window_list_t* windows)
{
    windows->clear();
    Check((!data), icamera::BAD_VALUE, "null data to convert hal window!");
    Check((dataCount % 5 != 0), icamera::BAD_VALUE, "wrong data count %d!", dataCount);

    icamera::camera_window_t window;
    for (int i = 0; i < dataCount / 5; i+=5) {
        window.left   = data[i];
        window.top    = data[i + 1];
        window.right  = data[i + 2];
        window.bottom = data[i + 3];
        window.weight = data[i + 4];
        windows->push_back(window);
    }
    return icamera::OK;
}

int MetadataConvert::convertToMetadataRegion(const icamera::camera_window_list_t& windows,
                                                 int dataCount, int32_t* data)
{
    size_t num = windows.size();
    Check((!data), 0, "null data to convert Metadata region!");
    Check(((unsigned int)dataCount < num * 5), 0, "small dataCount!");

    for (size_t i = 0; i < windows.size(); i++) {
        data[i * 5]     = windows[i].left;
        data[i * 5 + 1] = windows[i].top;
        data[i * 5 + 2] = windows[i].right;
        data[i * 5 + 3] = windows[i].bottom;
        data[i * 5 + 4] = windows[i].weight;
    }

    return num * 5;
}

void MetadataConvert::dumpMetadata(const camera_metadata_t *meta)
{
    if (!meta || !icamera::Log::isDebugLevelEnable(icamera::CAMERA_DEBUG_LOG_LEVEL2)) {
        return;
    }

    LOG2("%s", __func__);
    int entryCount = get_camera_metadata_entry_count(meta);

    for (int i = 0; i < entryCount; i++) {
        camera_metadata_entry_t entry;
        if (get_camera_metadata_entry((camera_metadata_t *)meta, i, &entry)) {
            continue;
        }

        // Print tag & type
        const char *tagName, *tagSection;
        tagSection = get_camera_metadata_section_name(entry.tag);
        if (tagSection == nullptr) {
            tagSection = "unknownSection";
        }
        tagName = get_camera_metadata_tag_name(entry.tag);
        if (tagName == nullptr) {
            tagName = "unknownTag";
        }
        const char *typeName;
        if (entry.type >= NUM_TYPES) {
            typeName = "unknown";
        } else {
            typeName = camera_metadata_type_names[entry.type];
        }
        LOG2("(%d)%s.%s (%05x): %s[%zu], type: %d\n",
             i,
             tagSection,
             tagName,
             entry.tag,
             typeName,
             entry.count,
             entry.type);

        // Print data
        size_t j;
        const uint8_t *u8;
        const int32_t *i32;
        const float   *f;
        const int64_t *i64;
        const double  *d;
        const camera_metadata_rational_t *r;
        std::ostringstream stringStream;
        stringStream << "[";

        switch (entry.type) {
        case TYPE_BYTE:
            u8 = entry.data.u8;
            for (j = 0; j < entry.count; j++)
                stringStream << (int32_t)u8[j] << " ";
            break;
        case TYPE_INT32:
            i32 = entry.data.i32;
            for (j = 0; j < entry.count; j++)
                stringStream << " " << i32[j] << " ";
            break;
        case TYPE_FLOAT:
            f = entry.data.f;
            for (j = 0; j < entry.count; j++)
                stringStream << " " << f[j] << " ";
            break;
        case TYPE_INT64:
            i64 = entry.data.i64;
            for (j = 0; j < entry.count; j++)
                stringStream << " " << i64[j] << " ";
            break;
        case TYPE_DOUBLE:
            d = entry.data.d;
            for (j = 0; j < entry.count; j++)
                stringStream << " " << d[j] << " ";
            break;
        case TYPE_RATIONAL:
            r = entry.data.r;
            for (j = 0; j < entry.count; j++)
                stringStream << " (" << r[j].numerator << ", " << r[j].denominator << ") ";
            break;
        }
        stringStream << "]";
        std::string str = stringStream.str();
        LOG2("%s", str.c_str());
    }
}

} // namespace camera3
