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

#define LOG_TAG "Intel3AResult"

#include "Intel3AResult.h"

#include <string.h>

#include "iutils/Utils.h"
#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "AiqUtils.h"

namespace icamera {

Intel3AResult::Intel3AResult()
{
    LOG3A("@%s", __func__);
}

Intel3AResult::~Intel3AResult()
{
    LOG3A("@%s", __func__);
}

int Intel3AResult::deepCopyAeResults(const ia_aiq_ae_results &src, ia_aiq_ae_results *dst)
{
    dumpAeResults(src);
    return AiqUtils::deepCopyAeResults(src, dst);
}

int Intel3AResult::deepCopyAfResults(const ia_aiq_af_results &src, ia_aiq_af_results *dst)
{
    dumpAfResults(src);
    return AiqUtils::deepCopyAfResults(src, dst);
}

int Intel3AResult::deepCopyAwbResults(const ia_aiq_awb_results &src, ia_aiq_awb_results *dst)
{
    dumpAwbResults(src);
    return AiqUtils::deepCopyAwbResults(src, dst);
}

int Intel3AResult::dumpAeResults(const ia_aiq_ae_results &aeResult)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) {
        return OK;
    }

    LOG3A("@%s", __func__);

    if (aeResult.exposures) {
        for (unsigned int i = 0; i < aeResult.num_exposures; i++) {
            if (aeResult.exposures[i].exposure) {
                LOG3A(" AE exp[%d] result ag %2.1f dg %2.1f Fn %2.1f exp time %dus total %d filter[%s] iso %d", i,
                        aeResult.exposures[i].exposure->analog_gain,
                        aeResult.exposures[i].exposure->digital_gain,
                        aeResult.exposures[i].exposure->aperture_fn,
                        aeResult.exposures[i].exposure->exposure_time_us,
                        aeResult.exposures[i].exposure->total_target_exposure,
                        aeResult.exposures[i].exposure->nd_filter_enabled? "YES": "NO",
                        aeResult.exposures[i].exposure->iso);
            }
            if (aeResult.exposures[i].sensor_exposure) {
                LOG3A(" AE sensor exp[%d] result ag %d dg %d coarse: %d fine: %d llp:%d fll:%d", i,
                        aeResult.exposures[i].sensor_exposure->analog_gain_code_global,
                        aeResult.exposures[i].sensor_exposure->digital_gain_global,
                        aeResult.exposures[i].sensor_exposure->coarse_integration_time,
                        aeResult.exposures[i].sensor_exposure->fine_integration_time,
                        aeResult.exposures[i].sensor_exposure->line_length_pixels,
                        aeResult.exposures[i].sensor_exposure->frame_length_lines);
            }
            LOG3A(" AE Converged : %s", aeResult.exposures[i].converged ? "YES" : "NO");
        }
    } else {
        LOGE("nullptr in StatsInputParams->frame_ae_parameters->exposures");
    }
    LOG3A(" AE bracket mode = %d %s", aeResult.multiframe,
               aeResult.multiframe == ia_aiq_bracket_mode_ull ? "ULL" : "HDR");

    if (aeResult.weight_grid && aeResult.weight_grid->width != 0 &&  aeResult.weight_grid->height != 0) {
        LOG3A(" AE weight grid = [%dx%d]", aeResult.weight_grid->width, aeResult.weight_grid->height);
        if (aeResult.weight_grid->weights) {
            for (int i = 0; i < 5 && i < aeResult.weight_grid->height; i++) {
                LOG3A(" AE weight_grid[%d] = %d ", aeResult.weight_grid->width/2,
                        aeResult.weight_grid->weights[aeResult.weight_grid->width/2]);
            }
        }
    }

    if (aeResult.aperture_control) {
        LOG3A(" AE aperture fn = %f, iris command = %d, code = %d", aeResult.aperture_control->aperture_fn,
                   aeResult.aperture_control->dc_iris_command, aeResult.aperture_control->code);
    }

    return OK;
}

int Intel3AResult::dumpAfResults(const ia_aiq_af_results &afResult)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) {
        return OK;
    }

    LOG3A("@%s", __func__);

    LOG3A("AF results current_focus_distance %d final_position_reached %s",
                afResult.current_focus_distance,
                afResult.final_lens_position_reached ? "TRUE":"FALSE");
    LOG3A("AF results driver_action %d, next_lens_position %d",
                afResult.lens_driver_action,
                afResult.next_lens_position);
    LOG3A("AF results use_af_assist %s",
          afResult.use_af_assist? "TRUE":"FALSE");

    switch (afResult.status) {
    case ia_aiq_af_status_local_search:
        LOG3A("AF result state _local_search");
        break;
    case ia_aiq_af_status_extended_search:
        LOG3A("AF result state extended_search");
        break;
    case ia_aiq_af_status_success:
        LOG3A("AF state success");
        break;
    case ia_aiq_af_status_fail:
        LOG3A("AF state fail");
        break;
    case ia_aiq_af_status_idle:
    default:
        LOG3A("AF state idle");
    }

    return OK;
}

int Intel3AResult::dumpAwbResults(const ia_aiq_awb_results &awbResult)
{
    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) {
        return OK;
    }

    LOG3A("@%s", __func__);

    LOG3A("AWB result: accurate_r/g %f, accurate_b/g %f final_r/g %f final_b/g %f",
            awbResult.accurate_r_per_g,
            awbResult.accurate_b_per_g,
            awbResult.final_r_per_g,
            awbResult.final_b_per_g);
    LOG3A("AWB result: cct_estimate %d, distance_from_convergence %f",
            awbResult.cct_estimate,
            awbResult.distance_from_convergence);

    return OK;
}

} /* namespace icamera */
