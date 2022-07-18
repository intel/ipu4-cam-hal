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
#define LOG_TAG "Camera3HALModule"

#include <dlfcn.h>
#include <stdlib.h>
#include <mutex>
#include <hardware/camera3.h>
#include <hardware/hardware.h>

#include "Utils.h"
#include "ICamera.h"

#include "MetadataConvert.h"
#include "Camera3HAL.h"
#include "HALv3Utils.h"

namespace camera3 {

#define MAX_CAMERAS 2

/**
 * \macro VISIBILITY_PUBLIC
 *
 * Controls the visibility of symbols in the shared library.
 * In production builds all symbols in the shared library are hidden
 * except the ones using this linker attribute.
 */
#define VISIBILITY_PUBLIC __attribute__ ((visibility ("default")))

static int hal_dev_close(hw_device_t* device);

/**********************************************************************
 * Camera Module API (C API)
 **********************************************************************/

static bool sInstances[MAX_CAMERAS] = {false, false};
static int sInstanceCount = 0;
// sCameraMetadata buffer won't be free in CAL
static android::CameraMetadata *sCameraMetadata[MAX_CAMERAS] = { nullptr };

/**
 * Global mutex used to protect sInstanceCount and sInstances
 */
static std::mutex sCameraHalMutex;

int openCameraHardware(int id, const hw_module_t* module, hw_device_t** device)
{
    LOG1("@%s", __func__);

    if (sInstances[id])
        return 0;

    Camera3HAL* halDev = new Camera3HAL(id, module);

    if (halDev->init() != 0) {
        LOGE("HAL initialization fail!");
        delete halDev;
        return -EINVAL;
    }
    camera3_device_t *cam3Device = halDev->getDeviceStruct();

    cam3Device->common.close = hal_dev_close;
    *device = &cam3Device->common;

    sInstanceCount++;
    sInstances[id] = true;

    LOG1("@%s end", __func__);
    return 0;
}

static int hal_get_number_of_cameras(void)
{
    LOG1("@%s", __func__);

    return icamera::get_number_of_cameras();
}

static int hal_get_camera_info(int cameraId, struct camera_info *cameraInfo)
{
    LOG1("@%s", __func__);

    if (cameraId < 0 || !cameraInfo ||
          cameraId >= hal_get_number_of_cameras())
        return -EINVAL;

    icamera::camera_info_t info;
    icamera::get_camera_info(cameraId, info);

    if (sCameraMetadata[cameraId] == nullptr) {
        sCameraMetadata[cameraId] = new android::CameraMetadata;
        MetadataConvert::HALCapabilityToStaticMetadata(*(info.capability),
                                                       sCameraMetadata[cameraId]);
    }
    int32_t tag = ANDROID_LENS_FACING;
    camera_metadata_entry entry = sCameraMetadata[cameraId]->find(tag);
    if (entry.count == 1) {
        info.facing = entry.data.u8[0];
    }
    tag = ANDROID_SENSOR_ORIENTATION;
    entry = sCameraMetadata[cameraId]->find(tag);
    if (entry.count == 1) {
        info.orientation = entry.data.u8[0];
    }
    cameraInfo->facing = info.facing ? CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
    cameraInfo->device_version = CAMERA_DEVICE_API_VERSION_3_3;
    cameraInfo->orientation = info.orientation;
    const camera_metadata_t *settings = sCameraMetadata[cameraId]->getAndLock();
    cameraInfo->static_camera_characteristics = settings;
    sCameraMetadata[cameraId]->unlock(settings);

    return 0;
}

static int hal_set_callbacks(const camera_module_callbacks_t *callbacks)
{
    LOG1("@%s", __func__);

    UNUSED(callbacks);
    return 0;
}

static int hal_dev_open(const hw_module_t* module, const char* name,
                        hw_device_t** device)
{
    icamera::Log::setDebugLevel();

    LOG1("@%s", __func__);

    int status = -EINVAL;
    int camera_id;

    if (!name || !module || !device) {
        LOGE("Camera name is nullptr");
        return status;
    }

    LOG1("%s, camera id: %s", __func__, name);
    camera_id = atoi(name);
    if (camera_id < 0 || camera_id >= hal_get_number_of_cameras()) {
        LOGE("%s: Camera id %d is out of bounds, num. of cameras (%d)",
             __func__, camera_id, hal_get_number_of_cameras());
        return -ENODEV;
    }

    std::lock_guard<std::mutex> l(sCameraHalMutex);

    if (sInstanceCount > 0 && sInstances[camera_id]) {
        LOGW("Camera already has been opened!");
        return -EUSERS;
    }

    return openCameraHardware(camera_id, module, device);
}

static int hal_dev_close(hw_device_t* device)
{
    LOG1("@%s", __func__);

    if (!device || sInstanceCount == 0) {
        LOGW("hal close, instance count %d", sInstanceCount);
        return -EINVAL;
    }

    camera3_device_t *camera3_dev = (struct camera3_device *)device;
    Camera3HAL* camera_priv = static_cast<Camera3HAL*>(camera3_dev->priv);

    if (camera_priv != nullptr) {
        std::lock_guard<std::mutex> l(sCameraHalMutex);
        camera_priv->deinit();
        int id = camera_priv->getCameraId();
        delete camera_priv;
        sInstanceCount--;
        sInstances[id] = false;
    }

    LOG1("%s, instance count %d", __func__, sInstanceCount);

    return 0;
}

static int hal_set_torch_mode (const char* camera_id, bool enabled){
    LOG1("@%s", __func__);

    UNUSED(camera_id);
    UNUSED(enabled);
    return -ENOSYS;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = hal_dev_open
};

static hw_module_t camera_common = {
    .tag                = HARDWARE_MODULE_TAG,
    .module_api_version = CAMERA_MODULE_API_VERSION_2_3,
    .hal_api_version    = HARDWARE_HAL_API_VERSION,
    .id                 = CAMERA_HARDWARE_MODULE_ID,
    .name               = "Intel Camera3HAL Module",
    .author             = "Intel",
    .methods            = &hal_module_methods,
    .dso                = nullptr,
    .reserved           = {0}
};

extern "C" {
camera_module_t VISIBILITY_PUBLIC HAL_MODULE_INFO_SYM = {
    .common                = camera_common,
    .get_number_of_cameras = hal_get_number_of_cameras,
    .get_camera_info       = hal_get_camera_info,
    .set_callbacks         = hal_set_callbacks,
    .get_vendor_tag_ops    = nullptr,
    .open_legacy           = nullptr,
    .set_torch_mode        = hal_set_torch_mode,
    .init                  = nullptr,
    .reserved              = {0}
};
}

} //namespace camera3
