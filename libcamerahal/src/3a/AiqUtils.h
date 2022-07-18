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

#pragma once

#include "PlatformData.h"
#include "Parameters.h"
#include "ia_aiq.h"
#include "ia_ltm_types.h"
#include "ia_dvs_types.h"
#include "ia_isp_bxt_statistics_types.h"

namespace icamera {

/*!> Top limit for the RGBS grid size */
static const unsigned int MAX_AE_GRID_SIZE = 2048;
/*!> Number of leds AEC algorithm provides output for */
static const unsigned int MAX_EXPOSURES_NUM = 3;
static const unsigned int NUM_FLASH_LEDS = 1;
static const unsigned int MAX_GAMMA_LUT_SIZE = 2048;
static const unsigned int MAX_TONEMAP_LUT_SIZE = 2048;

static const unsigned int MAX_STATISTICS_WIDTH = BXT_RGBS_GRID_MAX_WIDTH;
static const unsigned int MAX_STATISTICS_HEIGHT = BXT_RGBS_GRID_MAX_HEIGHT;

static const unsigned int MAX_LSC_WIDTH = 100;
static const unsigned int MAX_LSC_HEIGHT = 100;

static const unsigned int MAX_IR_WEIGHT_GRID_SIZE = 480;
static const unsigned int MAX_NUM_SECTORS = 36;

static const int MAX_BAYER_ORDER_NUM = 4;

/**
 *  The normalized awb gain range is (4.0, 1.0) which is just experimental.
 *  TODO: Maybe need put them in configuration file later.
 */
static const int AWB_GAIN_NORMALIZED_START = 4.0;
static const int AWB_GAIN_NORMALIZED_END = 1.0;
static const int AWB_GAIN_RANGE_NORMALIZED = AWB_GAIN_NORMALIZED_END - AWB_GAIN_NORMALIZED_START;

static const float AWB_GAIN_MIN = 0;
static const float AWB_GAIN_MAX = 255;
static const float AWB_GAIN_RANGE_USER = AWB_GAIN_MAX - AWB_GAIN_MIN;

static const int MAX_CUSTOM_CONTROLS_PARAM_SIZE = 1024;

namespace AiqUtils {

int deepCopyAeResults(const ia_aiq_ae_results& src, ia_aiq_ae_results* dst);
int deepCopyAfResults(const ia_aiq_af_results& src, ia_aiq_af_results* dst);
int deepCopyAwbResults(const ia_aiq_awb_results& src, ia_aiq_awb_results* dst);
int deepCopyGbceResults(const ia_aiq_gbce_results& src, ia_aiq_gbce_results* dst);
int deepCopyPaResults(const ia_aiq_pa_results_v1& src, ia_aiq_pa_results_v1* dst,
                      ia_aiq_advanced_ccm_t* preferredAcm);
int deepCopySaResults(const ia_aiq_sa_results_v1& src, ia_aiq_sa_results_v1* dst);
int deepCopyLtmResults(const ia_ltm_results& src, ia_ltm_results* dst);
int deepCopyLtmDRCParams(const ia_ltm_drc_params& src, ia_ltm_drc_params* dst);
int deepCopyDvsResults(const ia_dvs_morph_table& src, ia_dvs_morph_table* dst);
int deepCopyDvsResults(const ia_dvs_image_transformation& src, ia_dvs_image_transformation* dst);

int convertError(ia_err iaErr);

void convertToAiqFrameParam(const SensorFrameParams& sensor, ia_aiq_frame_params& aiq);

camera_coordinate_t convertCoordinateSystem(const camera_coordinate_system_t& srcSystem,
                                            const camera_coordinate_system_t& dstSystem,
                                            const camera_coordinate_t& srcCoordinate);
camera_coordinate_t convertToIaCoordinate(const camera_coordinate_system_t& srcSystem,
                                          const camera_coordinate_t& srcCoordinate);
camera_window_t convertToIaWindow(const camera_coordinate_system_t& srcSystem,
                                  const camera_window_t& srcWindow);
float normalizeAwbGain(int gain);
int convertToUserAwbGain(float normalizedGain);
float convertSpeedModeToTime(camera_converge_speed_t mode);
float convertSpeedModeToTimeForHDR(camera_converge_speed_t mode);
int getSensorDigitalGain(int cameraId, float realDigitalGain);
float getIspDigitalGain(int cameraId, float realDigitalGain);

ia_aiq_frame_use convertFrameUsageToIaFrameUsage(int frameUsage);

} // namespace AiqUtils

} // namespace icamera
