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

#define LOG_TAG "AiqUtils"

#include <math.h>
#include <iutils/Utils.h>
#include <iutils/Errors.h>
#include <iutils/CameraLog.h>

#include "AiqUtils.h"
#include "AiqSetting.h"

#include "ia_coordinate.h"

namespace icamera {

int AiqUtils::deepCopyAeResults(const ia_aiq_ae_results& src, ia_aiq_ae_results* dst)
{
    LOG3A("@%s", __func__);

    /**
     * lets check that all the pointers are there
     * in the source and in the destination
     */
    Check(!dst||!dst->exposures||!dst->flashes||!dst->weight_grid||!dst->weight_grid->weights
        ,BAD_VALUE ,"Failed to deep copy AE result- invalid destination");

    Check(!src.exposures||!src.flashes||!src.weight_grid||!src.weight_grid->weights
        ,BAD_VALUE ,"Failed to deep copy AE result- invalid source");

    dst->lux_level_estimate = src.lux_level_estimate;
    dst->flicker_reduction_mode = src.flicker_reduction_mode;
    dst->multiframe = src.multiframe;
    dst->num_flashes = src.num_flashes;
    dst->num_exposures = src.num_exposures;
    if (src.aperture_control) {
        *dst->aperture_control = *src.aperture_control;
    }
    for (unsigned int i = 0; i < dst->num_exposures; i++)
    {
        dst->exposures[i].converged = src.exposures[i].converged;
        dst->exposures[i].distance_from_convergence = src.exposures[i].distance_from_convergence;
        dst->exposures[i].exposure_index = src.exposures[i].exposure_index;
        if (src.exposures[i].exposure) {
            *dst->exposures[i].exposure = *src.exposures[i].exposure;
        }
        if (src.exposures[i].sensor_exposure) {
            *dst->exposures[i].sensor_exposure = *src.exposures[i].sensor_exposure;
        }
    }

    // Copy weight grid
    dst->weight_grid->width = src.weight_grid->width;
    dst->weight_grid->height = src.weight_grid->height;

    unsigned int gridElements  = src.weight_grid->width *
                                 src.weight_grid->height;
    gridElements = CLIP(gridElements, MAX_AE_GRID_SIZE, 1);
    MEMCPY_S(dst->weight_grid->weights, gridElements*sizeof(char),
             src.weight_grid->weights, gridElements*sizeof(char));

    // Copy the flash info structure
    MEMCPY_S(dst->flashes, NUM_FLASH_LEDS*sizeof(ia_aiq_flash_parameters),
             src.flashes, NUM_FLASH_LEDS*sizeof(ia_aiq_flash_parameters));

    return OK;
}

int AiqUtils::deepCopyAfResults(const ia_aiq_af_results& src, ia_aiq_af_results* dst)
{
    LOG3A("@%s", __func__);

    Check(!dst, BAD_VALUE, "Failed to deep copy Af result- invalid destination or Source");

    MEMCPY_S(dst, sizeof(ia_aiq_af_results), &src, sizeof(ia_aiq_af_results));
    return OK;
}

int AiqUtils::deepCopyAwbResults(const ia_aiq_awb_results& src, ia_aiq_awb_results* dst)
{
    LOG3A("@%s", __func__);

    Check(!dst, BAD_VALUE, "Failed to deep copy Awb result- invalid destination or Source");

    MEMCPY_S(dst, sizeof(ia_aiq_awb_results), &src, sizeof(ia_aiq_awb_results));
    return OK;
}

int AiqUtils::deepCopyGbceResults(const ia_aiq_gbce_results& src, ia_aiq_gbce_results* dst)
{
    LOG3A("%s", __func__);

    Check(!dst||!dst->r_gamma_lut||!dst->g_gamma_lut||!dst->b_gamma_lut||!dst->tone_map_lut
        ,BAD_VALUE ,"Failed to deep copy GBCE result- invalid destination");
    Check(!src.r_gamma_lut||!src.g_gamma_lut||!src.b_gamma_lut
        ,BAD_VALUE ,"Failed to deep copy GBCE result- invalid source");

    MEMCPY_S(dst->r_gamma_lut, src.gamma_lut_size*sizeof(float),
             src.r_gamma_lut, src.gamma_lut_size*sizeof(float));

    MEMCPY_S(dst->g_gamma_lut, src.gamma_lut_size*sizeof(float),
             src.g_gamma_lut, src.gamma_lut_size*sizeof(float));

    MEMCPY_S(dst->b_gamma_lut, src.gamma_lut_size*sizeof(float),
             src.b_gamma_lut, src.gamma_lut_size*sizeof(float));

    dst->gamma_lut_size = src.gamma_lut_size;

    // Copy tone mapping table
    if (src.tone_map_lut != nullptr)
    {
        MEMCPY_S(dst->tone_map_lut, src.tone_map_lut_size * sizeof(float),
                 src.tone_map_lut, src.tone_map_lut_size * sizeof(float));

    }
    dst->tone_map_lut_size = src.tone_map_lut_size; // zero indicates GBCE is ineffective.

    return OK;
}

int AiqUtils::deepCopyPaResults(const ia_aiq_pa_results_v1& src, ia_aiq_pa_results_v1* dst,
                                ia_aiq_advanced_ccm_t* preferredAcm)
{
    LOG3A("%s", __func__);

    Check(!dst, BAD_VALUE ,"Failed to deep copy PA result- invalid destination");

    MEMCPY_S(dst->color_conversion_matrix, sizeof(dst->color_conversion_matrix),
             src.color_conversion_matrix, sizeof(src.color_conversion_matrix));
    for (unsigned int i = 0; i < 4; i++)
        for (unsigned int j = 0; j < 4; j++)
            dst->black_level_4x4[i][j] = src.black_level_4x4[i][j];
    dst->color_gains = src.color_gains;
    dst->saturation_factor = src.saturation_factor;
    dst->brightness_level = src.brightness_level;

    if (src.ir_weight) {
        unsigned long int irSize = src.ir_weight->width * src.ir_weight->height;
        if (irSize) {
            LOG3A("%s irSize = %ld", __func__, irSize);
            MEMCPY_S(dst->ir_weight->ir_weight_grid_R, irSize * sizeof(unsigned short),
                     src.ir_weight->ir_weight_grid_R, irSize * sizeof(unsigned short));
            MEMCPY_S(dst->ir_weight->ir_weight_grid_G, irSize * sizeof(unsigned short),
                     src.ir_weight->ir_weight_grid_G, irSize * sizeof(unsigned short));
            MEMCPY_S(dst->ir_weight->ir_weight_grid_B, irSize * sizeof(unsigned short),
                     src.ir_weight->ir_weight_grid_B, irSize * sizeof(unsigned short));
            dst->ir_weight->width = src.ir_weight->width;
            dst->ir_weight->height = src.ir_weight->height;
        }
    }

    if (src.preferred_acm && src.preferred_acm->sector_count) {
        dst->preferred_acm = preferredAcm;

        LOG3A("%s advanced ccm sector count = %d", __func__, src.preferred_acm->sector_count);
        MEMCPY_S(dst->preferred_acm->hue_of_sectors,
                 src.preferred_acm->sector_count * sizeof(unsigned int),
                 src.preferred_acm->hue_of_sectors,
                 src.preferred_acm->sector_count * sizeof(unsigned int));
        MEMCPY_S(dst->preferred_acm->advanced_color_conversion_matrices,
                 src.preferred_acm->sector_count * sizeof(float[3][3]),
                 src.preferred_acm->advanced_color_conversion_matrices,
                 src.preferred_acm->sector_count  * sizeof(float[3][3]));
        dst->preferred_acm->sector_count = src.preferred_acm->sector_count;
    } else {
        dst->preferred_acm = nullptr;
    }

    /* current linearization.size is zero, set related pointers to nullptr */
    dst->linearization.r = nullptr;
    dst->linearization.gr = nullptr;
    dst->linearization.gb = nullptr;
    dst->linearization.b = nullptr;
    dst->linearization.size = 0;

    return OK;
}

int AiqUtils::deepCopyLtmResults(const ia_ltm_results& src, ia_ltm_results* dst)
{
    LOG3A("%s", __func__);

    Check(!dst, BAD_VALUE ,"Failed to deep copy LTM result- invalid destination");

    // Copy only necessary information.
    // NOTE: If ia_ltm_lut ltm_luts[MAX_NUM_LUT] is needed for project, please implement zero-copy to fix PnP issue.
    dst->ltm_gain = src.ltm_gain;
    dst->dynamic_range = src.dynamic_range;

    return OK;
}

int AiqUtils::deepCopyLtmDRCParams(const ia_ltm_drc_params& src, ia_ltm_drc_params* dst)
{
    LOG3A("%s", __func__);

    Check(!dst, BAD_VALUE ,"Failed to deep copy LTM DRC params- invalid destination");

    MEMCPY_S(dst, sizeof(ia_ltm_drc_params), &src, sizeof(ia_ltm_drc_params));

    return OK;
}

int AiqUtils::deepCopySaResults(const ia_aiq_sa_results_v1& src, ia_aiq_sa_results_v1* dst)
{
    LOG3A("%s", __func__);

    Check(!dst, BAD_VALUE, "Failed to deep copy SA result- invalid destination");

    const size_t gridSize = src.width * src.height;
    if ((size_t)(dst->width * dst->height) < gridSize) {
        LOG3A("%s: increases the size of LSC table from %dx%d to %dx%d.",
              __func__, dst->width, dst->height, src.width, src.height);

        // allocated buffer is too small to accomodate what SA returns.
        for (int i = 0; i < MAX_BAYER_ORDER_NUM; ++i) {
            for (int j = 0; j < MAX_BAYER_ORDER_NUM; ++j) {
                // re-allocate
                delete [] dst->lsc_grid[i][j];
                dst->lsc_grid[i][j] = new unsigned short[gridSize];

                // copy a table
                if (src.lsc_grid[i][j]) {
                    MEMCPY_S(dst->lsc_grid[i][j], gridSize * sizeof(unsigned short),
                             src.lsc_grid[i][j], gridSize * sizeof(unsigned short));
                }
            }
        }
    } else {
        // copy tables
        for (int i = 0; i < MAX_BAYER_ORDER_NUM; i++) {
            for (int j = 0; j < MAX_BAYER_ORDER_NUM; j++) {
                if (dst->lsc_grid[i][j] && src.lsc_grid[i][j]) {
                    MEMCPY_S(dst->lsc_grid[i][j], gridSize * sizeof(unsigned short),
                             src.lsc_grid[i][j], gridSize * sizeof(unsigned short));
                }
            }
        }
    }

    dst->width = src.width;
    dst->height = src.height;
    dst->lsc_update = src.lsc_update;
    dst->fraction_bits = src.fraction_bits;
    dst->color_order = src.color_order;

    MEMCPY_S(dst->light_source, sizeof(dst->light_source), src.light_source, sizeof(src.light_source));
    MEMCPY_S(&dst->frame_params, sizeof(dst->frame_params), &src.frame_params, sizeof(src.frame_params));

    return OK;
}

int AiqUtils::deepCopyDvsResults(const ia_dvs_morph_table& src, ia_dvs_morph_table* dst)
{
    LOG3A("%s", __func__);

    Check(!dst || !dst->xcoords_y || !dst->ycoords_y
          || !dst->xcoords_uv || !dst->ycoords_uv
          || !dst->xcoords_uv_float || !dst->ycoords_uv_float
          ,BAD_VALUE ,"Failed to deep copy DVS result- invalid destination");

    Check(!src.xcoords_y || !src.ycoords_y
          || !src.xcoords_uv || !src.ycoords_uv
          || !src.xcoords_uv_float || !src.ycoords_uv_float
          ,BAD_VALUE ,"Failed to deep copy DVS result- invalid source");

    Check(src.width_y == 0 || src.height_y == 0 || src.width_uv == 0 || src.height_uv == 0
          ,BAD_VALUE ,"Failed to deep copy DVS result- invalid source size y[%dx%d] uv[%dx%d]",
          src.width_y, src.height_y, src.width_uv, src.height_uv);

    dst->width_y = src.width_y;
    dst->height_y = src.height_y;
    dst->width_uv = src.width_uv;
    dst->height_uv = src.height_uv;
    dst->morph_table_changed = src.morph_table_changed;
    unsigned int SizeY = dst->width_y  * dst->height_y * sizeof(int32_t);
    unsigned int SizeUV = dst->width_uv * dst->height_uv * sizeof(int32_t);
    MEMCPY_S(dst->xcoords_y, SizeY, src.xcoords_y, SizeY);
    MEMCPY_S(dst->ycoords_y, SizeY, src.ycoords_y, SizeY);
    MEMCPY_S(dst->xcoords_uv, SizeUV, src.xcoords_uv, SizeUV);
    MEMCPY_S(dst->ycoords_uv, SizeUV, src.ycoords_uv, SizeUV);

    SizeUV = dst->width_uv * dst->height_uv * sizeof(float);
    MEMCPY_S(dst->xcoords_uv_float, SizeUV, src.xcoords_uv_float, SizeUV);
    MEMCPY_S(dst->ycoords_uv_float, SizeUV, src.ycoords_uv_float, SizeUV);

    return OK;
}

int AiqUtils::deepCopyDvsResults(const ia_dvs_image_transformation& src, ia_dvs_image_transformation* dst)
{
    LOG3A("%s", __func__);

    Check(!dst,BAD_VALUE ,"Failed to deep copy DVS result- invalid destination");

    dst->num_homography_matrices = src.num_homography_matrices;
    MEMCPY_S(dst->matrices, sizeof(dst->matrices) * DVS_HOMOGRAPHY_MATRIX_MAX_COUNT,
        src.matrices, sizeof(src.matrices) * DVS_HOMOGRAPHY_MATRIX_MAX_COUNT);

    return OK;
}


int AiqUtils::convertError(ia_err iaErr)
{
    LOG3A("%s, iaErr = %d", __func__, iaErr);
    switch (iaErr) {
    case ia_err_none:
        return OK;
    case ia_err_general:
        return UNKNOWN_ERROR;
    case ia_err_nomemory:
        return NO_MEMORY;
    case ia_err_data:
        return BAD_VALUE;
    case ia_err_internal:
        return INVALID_OPERATION;
    case ia_err_argument:
        return BAD_VALUE;
    default:
        return UNKNOWN_ERROR;
    }
}

/**
 * Convert SensorFrameParams defined in PlatformData to ia_aiq_frame_params in aiq
 */
void AiqUtils::convertToAiqFrameParam(const SensorFrameParams &sensor, ia_aiq_frame_params &aiq)
{
    aiq.cropped_image_height = sensor.cropped_image_height;
    aiq.cropped_image_width = sensor.cropped_image_width;
    aiq.horizontal_crop_offset = sensor.horizontal_crop_offset;
    aiq.horizontal_scaling_denominator = sensor.horizontal_scaling_denominator;
    aiq.horizontal_scaling_numerator = sensor.horizontal_scaling_numerator;
    aiq.vertical_crop_offset = sensor.vertical_crop_offset;
    aiq.vertical_scaling_denominator = sensor.vertical_scaling_denominator;
    aiq.vertical_scaling_numerator = sensor.vertical_scaling_numerator;
}

camera_coordinate_t AiqUtils::convertCoordinateSystem(const camera_coordinate_system_t& srcSystem,
                                                      const camera_coordinate_system_t& dstSystem,
                                                      const camera_coordinate_t& srcCoordinate)
{
    int dstWidth = dstSystem.right - dstSystem.left;
    int dstHeight = dstSystem.bottom - dstSystem.top;
    int srcWidth = srcSystem.right - srcSystem.left;
    int srcHeight = srcSystem.bottom - srcSystem.top;

    camera_coordinate_t result;
    result.x = (srcCoordinate.x - srcSystem.left) * dstWidth / srcWidth + dstSystem.left;
    result.y = (srcCoordinate.y - srcSystem.top) * dstHeight / srcHeight + dstSystem.top;

    return result;
}

camera_coordinate_t AiqUtils::convertToIaCoordinate(const camera_coordinate_system_t& srcSystem,
                                                    const camera_coordinate_t& srcCoordinate)
{
    camera_coordinate_system_t iaCoordinate = {IA_COORDINATE_LEFT, IA_COORDINATE_TOP,
                                               IA_COORDINATE_RIGHT, IA_COORDINATE_BOTTOM};

    return convertCoordinateSystem(srcSystem, iaCoordinate, srcCoordinate);
}

camera_window_t AiqUtils::convertToIaWindow(const camera_coordinate_system_t& srcSystem,
                                            const camera_window_t& srcWindow)
{
    camera_coordinate_t leftTop;
    camera_coordinate_t rightBottom;
    leftTop.x     = srcWindow.left;
    leftTop.y     = srcWindow.top;
    rightBottom.x = srcWindow.right;
    rightBottom.y = srcWindow.bottom;
    leftTop       = convertToIaCoordinate(srcSystem, leftTop);
    rightBottom   = convertToIaCoordinate(srcSystem, rightBottom);

    camera_window_t result;
    result.left   = leftTop.x;
    result.top    = leftTop.y;
    result.right  = rightBottom.x;
    result.bottom = rightBottom.y;
    result.weight = srcWindow.weight;
    return result;
}

/**
 * Map user input manual gain(0, 255) to (AWB_GAIN_NORMALIZED_START, AWB_GAIN_NORMALIZED_END)
 */
float AiqUtils::normalizeAwbGain(int gain)
{
    gain = CLIP(gain, AWB_GAIN_MAX, AWB_GAIN_MIN);
    return AWB_GAIN_NORMALIZED_START + (float)(gain - AWB_GAIN_MIN) * \
                                       AWB_GAIN_RANGE_NORMALIZED / AWB_GAIN_RANGE_USER;
}

int AiqUtils::convertToUserAwbGain(float normalizedGain)
{
    normalizedGain = CLIP(normalizedGain, AWB_GAIN_NORMALIZED_START, AWB_GAIN_NORMALIZED_END);
    return AWB_GAIN_MIN + (normalizedGain - AWB_GAIN_NORMALIZED_START) * \
                          AWB_GAIN_RANGE_USER / AWB_GAIN_RANGE_NORMALIZED;
}

float AiqUtils::convertSpeedModeToTime(camera_converge_speed_t mode)
{
    float convergenceTime = -1;
    /*
     * The unit of manual_convergence_time is second, and 3.0 means 3 seconds.
     * The default value can be changed based on customer requirement.
     */
    switch (mode) {
        case CONVERGE_MID:
            convergenceTime = 3.0;
            break;
        case CONVERGE_LOW:
            convergenceTime = 5.0;
            break;
        case CONVERGE_NORMAL:
        default:
            convergenceTime = -1;
            break;
    }
    return convergenceTime;
}

float AiqUtils::convertSpeedModeToTimeForHDR(camera_converge_speed_t mode)
{
    float convergenceTime = -1;
    /*
     * The unit of manual_convergence_time is second, and 1.0 means 1 second.
     * The default value can be changed based on customer requirement.
     */
    switch (mode) {
        case CONVERGE_MID:
            convergenceTime = 0.6;
            break;
        case CONVERGE_LOW:
            convergenceTime = 1.0;
            break;
        case CONVERGE_NORMAL:
        default:
            convergenceTime = -1;
            break;
    }
    return convergenceTime;
}

/*
 * Get sensor value for the digital gain.
 *
 * Since the calculation formula may be different between sensors,
 * so we need to get this value based on sensor digital gain type.
 * For imx274, the magnification = 2^x (x is the register value).
 *
 * Need to specify the sensorDgType, maxSensorDg and useIspDigitalGain in xml.
 */
int AiqUtils::getSensorDigitalGain(int cameraId, float realDigitalGain)
{
    int sensorDg = 0;
    int maxSensorDg = PlatformData::getMaxSensorDigitalGain(cameraId);

    if (PlatformData::sensorDigitalGainType(cameraId) == SENSOR_DG_TYPE_2_X) {
        int index = 0;
        while(pow(2, index) <= realDigitalGain) {
            sensorDg = index;
            index++;
        }
        sensorDg = CLIP(sensorDg, maxSensorDg, 0);
    } else {
        LOGE("%s, don't support the sensor digital gain type: %d",
                __func__, PlatformData::sensorDigitalGainType(cameraId));
    }

    return sensorDg;
}

/*
 * Get the isp gain
 *
 * Separate real digital to sensorDg and ispDg, and the ispDg >= 1
 */
float AiqUtils::getIspDigitalGain(int cameraId, float realDigitalGain)
{
    float ispDg = 1.0f;
    int sensorDg = getSensorDigitalGain(cameraId, realDigitalGain);

    if (PlatformData::sensorDigitalGainType(cameraId) == SENSOR_DG_TYPE_2_X) {
        ispDg = realDigitalGain / pow(2, sensorDg);
        ispDg = CLIP(ispDg, ispDg, 1.0);
    } else {
        LOGE("%s, don't support the sensor digital gain type: %d",
                __func__, PlatformData::sensorDigitalGainType(cameraId));
    }

    return ispDg;
}

/*
 * Get ia_aiq_frame_use
 *
 * Convert frame usage to ia_aiq_frame_use
 */
ia_aiq_frame_use AiqUtils::convertFrameUsageToIaFrameUsage(int frameUsage)
{
    switch (frameUsage) {
        case FRAME_USAGE_VIDEO:
            return ia_aiq_frame_use_video;
        case FRAME_USAGE_STILL:
            return ia_aiq_frame_use_still;
        case FRAME_USAGE_CONTINUOUS:
            return ia_aiq_frame_use_continuous;
    }
    return ia_aiq_frame_use_preview;
}

} /* namespace icamera */
