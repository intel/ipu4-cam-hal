/*
 * Copyright (C) 2018 Intel Corporation.
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
#define LOG_TAG "SensorOB"

#include "ia_ob.h"

#include "SensorOB.h"
#include "PlatformData.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

namespace icamera {

SensorOB::SensorOB(int cameraId) :
    mCameraId(cameraId)
{
    LOG3A("%s", __func__);
    mOB = ia_ob_init();
}

SensorOB::~SensorOB()
{
    LOG3A("%s", __func__);
    ia_ob_deinit(mOB);
}

int SensorOB::runOB(ConfigMode configMode, const shared_ptr<CameraBuffer> frameBuf,
                    IspSettings* ispSettings)
{
    // Check sensor OB is enabled or not and load related OB setting
    OBSetting sensorOBSetting;
    ispSettings->useSensorOB = PlatformData::getSensorOBSetting(mCameraId, configMode, sensorOBSetting);
    if (!ispSettings->useSensorOB) {
        LOG3A("%s: No sensor OB for real config mode %d", __func__, configMode);
        return OK;
    }

    LOG3A("%s: useSensorOB %d, left %d, top %d, section height %d, interleave step %d",
          __func__, ispSettings->useSensorOB, sensorOBSetting.left, sensorOBSetting.top,
          sensorOBSetting.sectionHeight, sensorOBSetting.interleaveStep);

    ia_ob_input obInput;
    obInput.frame_data = (short int*)frameBuf->getBufferAddr();
    obInput.frame_width = ALIGN_32(frameBuf->getWidth());
    obInput.frame_height = frameBuf->getHeight();
    LOG3A("%s: frame width %d, height %d", __func__, obInput.frame_width, obInput.frame_height);
    obInput.ob_top = sensorOBSetting.top;
    obInput.ob_left = sensorOBSetting.left;
    obInput.ob_width = frameBuf->getWidth() - sensorOBSetting.left;
    obInput.ob_height = sensorOBSetting.sectionHeight;
    switch (sensorOBSetting.interleaveStep) {
        case 0: obInput.interleave_step = ia_ob_interleave_none; break;
        case 1: obInput.interleave_step = ia_ob_interleave_two; break;
        default: obInput.interleave_step = ia_ob_interleave_none; break;
    }
    LOG3A("%s: OB top %d, left %d, width %d, height %d, step %d", __func__,
          obInput.ob_top, obInput.ob_left, obInput.ob_width, obInput.ob_height, obInput.interleave_step);

    ia_err status = ia_ob_run(mOB, &obInput, &ispSettings->obOutput);

    Check(status != ia_err_none, UNKNOWN_ERROR, "Failed to run OB");

    LOG3A("%s, img_data(00:%d, 01:%d, 10:%d, 11:%d)", __func__,
            obInput.frame_data[0], obInput.frame_data[1],
            obInput.frame_data[2], obInput.frame_data[3]);

    LOG3A("@%s, ob_out(00:%.3f, 01:%.3f, 10:%.3f, 11:%.3f)", __func__,
            ispSettings->obOutput.cc00, ispSettings->obOutput.cc01,
            ispSettings->obOutput.cc10, ispSettings->obOutput.cc11);

    return OK;
}

}
