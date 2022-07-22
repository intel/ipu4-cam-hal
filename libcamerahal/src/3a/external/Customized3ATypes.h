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

#pragma once

#include "Parameters.h"

namespace icamera {

/**
 * enum type definition
 *
 * It contains NONE, AE, AF and AWB.
 */
typedef enum {
    CUSTOM_NONE = 0,
    CUSTOM_AE = 1,
    CUSTOM_AWB = 1 << 1,
    CUSTOM_AF = 1 << 2,
} Custom3AType;

/*
 * 3A related parameters
 */
struct custom_3a_parameter_t {
    camera_ae_mode_t aeMode;
    camera_awb_mode_t awbMode;
    camera_scene_mode_t sceneMode;
    int64_t manualExpTimeUs;
    float manualGain;
    float evShift;
    int fps;
    camera_antibanding_mode_t antibandingMode;
    camera_range_t cctRange;
    camera_coordinate_t whitePoint;
    camera_awb_gains_t awbManualGain;
    camera_awb_gains_t awbGainShift;
    camera_color_transform_t manualColorMatrix;
    camera_color_gains_t manualColorGains;
    camera_window_list_t aeRegions;
    camera_blc_area_mode_t blcAreaMode;
    camera_converge_speed_mode_t aeConvergeSpeedMode;
    camera_converge_speed_mode_t awbConvergeSpeedMode;
    camera_converge_speed_t aeConvergeSpeed;
    camera_converge_speed_t awbConvergeSpeed;
    uint8_t hdrLevel;
    camera_weight_grid_mode_t weightGridMode;
    camera_ae_distribution_priority_t aeDistributionPriority;
    void reset() {
        aeMode = AE_MODE_AUTO;
        awbMode = AWB_MODE_AUTO;
        sceneMode = SCENE_MODE_AUTO;
        manualExpTimeUs = -1;
        manualGain = -1;
        evShift = 0;
        fps = 0;
        antibandingMode = ANTIBANDING_MODE_AUTO;
        cctRange = { 0, 0 };
        whitePoint = { 0, 0 };
        awbManualGain = { 0, 0, 0 };
        awbGainShift = { 0, 0, 0 };
        CLEAR(manualColorMatrix);
        CLEAR(manualColorGains);
        aeRegions.clear();
        blcAreaMode = BLC_AREA_MODE_OFF;
        aeConvergeSpeedMode = CONVERGE_SPEED_MODE_AIQ;
        awbConvergeSpeedMode = CONVERGE_SPEED_MODE_AIQ;
        aeConvergeSpeed = CONVERGE_NORMAL;
        awbConvergeSpeed = CONVERGE_NORMAL;
        hdrLevel = 0;
        weightGridMode = WEIGHT_GRID_AUTO;
        aeDistributionPriority = DISTRIBUTION_AUTO;
    }
};

/* The Macro is defined Custom 3A Library and as a symbol to find struct Custom3AModule */
#define CUSTOMIZE_3A_MODULE_INFO_SYM C3AMI

/* string is used to get the address of the struct Custom3AModule. */
#define CUSTOMIZE_3A_MODULE_INFO_SYM_AS_STR "C3AMI"

} /* namespace icamera */
