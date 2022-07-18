/*
 * Copyright (C) 2015-20188888888 Intel Corporation.
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

#include "iutils/Utils.h"
#include "iutils/RWLock.h"
#include "Parameters.h"

// LOCAL_TONEMAP_S
#include "AlgoTuning.h"
// LOCAL_TONEMAP_E
#include "AiqUtils.h"

namespace icamera {

// Imaging algorithms that are supported
typedef enum {
    IMAGING_ALGO_NONE = 0,
    IMAGING_ALGO_AE   = 1,
    IMAGING_ALGO_AWB  = 1 << 1,
    IMAGING_ALGO_AF   = 1 << 2,
    IMAGING_ALGO_GBCE = 1 << 3,
    IMAGING_ALGO_PA   = 1 << 4,
    IMAGING_ALGO_SA   = 1 << 5
} imaging_algorithm_t;

typedef enum {
    AEC_SCENE_NONE,
    AEC_SCENE_HDR,
    AEC_SCENE_ULL
} aec_scene_t;

typedef struct {
    char data[MAX_CUSTOM_CONTROLS_PARAM_SIZE];
    unsigned int length;
} custom_aic_param_t;

typedef enum {
    FRAME_USAGE_PREVIEW,
    FRAME_USAGE_VIDEO,
    FRAME_USAGE_STILL,
    FRAME_USAGE_CONTINUOUS,
} frame_usage_mode_t;

/*
 * aiq related parameters
 */
struct aiq_parameter_t {
    frame_usage_mode_t frameUsage;
    camera_ae_mode_t aeMode;
    bool aeForceLock;
    camera_awb_mode_t awbMode;
    bool awbForceLock;
    camera_af_mode_t afMode;
    camera_af_trigger_t afTrigger;
    camera_scene_mode_t sceneMode;
    int64_t manualExpTimeUs;
    float manualGain;
    float evShift;
    float fps;
    camera_antibanding_mode_t antibandingMode;
    camera_range_t cctRange;
    camera_coordinate_t whitePoint;
    camera_awb_gains_t awbManualGain;
    camera_awb_gains_t awbGainShift;
    camera_color_transform_t manualColorMatrix;
    camera_color_gains_t manualColorGains;
    camera_window_list_t aeRegions;
    camera_window_list_t afRegions;
    camera_blc_area_mode_t blcAreaMode;
    camera_converge_speed_mode_t aeConvergeSpeedMode;
    camera_converge_speed_mode_t awbConvergeSpeedMode;
    camera_converge_speed_t aeConvergeSpeed;
    camera_converge_speed_t awbConvergeSpeed;
    int run3ACadence;
    uint8_t hdrLevel;
    camera_weight_grid_mode_t weightGridMode;
    camera_ae_distribution_priority_t aeDistributionPriority;
    custom_aic_param_t customAicParam;
    camera_yuv_color_range_mode_t yuvColorRangeMode;
    camera_range_t exposureTimeRange;
    camera_range_t sensitivityGainRange;
    camera_video_stabilization_mode_t videoStabilizationMode;
    camera_resolution_t resolution;
    camera_ldc_mode_t ldcMode;
    camera_rsc_mode_t rscMode;
    camera_flip_mode_t flipMode;
    float digitalZoomRatio;

    TuningMode tuningMode;

    // LOCAL_TONEMAP_S
    bool mLtmTuningEnabled;
    ltm_tuning_data mLtmTuningData;
    // LOCAL_TONEMAP_E

    int lensPosition;
    unsigned long long lensMovementStartTimestamp;
    camera_makernote_mode_t makernoteMode;

    aiq_parameter_t() { reset(); }
    void reset();
    void dump();
};

/*
 * \class AiqSetting
 * This class is used for setting parameters to other aiq class
 * and return some useful status of aiq results
 */
class AiqSetting {

public:
    AiqSetting(int cameraId);
    ~AiqSetting();

    int init(void);
    int deinit(void);
    int configure(const stream_config_t *streamList);

    int setParameters(const Parameters& params);

    int getAiqParameter(aiq_parameter_t &param);

    void updateTuningMode(aec_scene_t aecScene);

private:
    void updateFrameUsage(const stream_config_t *streamList);

public:
    int mCameraId;

private:
    vector<TuningMode> mTuningModes;
    unsigned int mPipeSwitchFrameCount;
    aiq_parameter_t mAiqParam;

    RWLock mParamLock;
};

} /* namespace icamera */
