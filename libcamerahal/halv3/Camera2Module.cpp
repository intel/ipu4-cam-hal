/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2017 Intel Corporation.
 *
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

#define LOG_TAG "Camera2Module"
class Mutex;

// System dependencies
#include <stdlib.h>
#include "iutils/Errors.h"

// Camera dependencies
#include "camera3.h"
#include "Camera3HWI.h"
#include "Camera2Module.h"
#include "PlatformData.h"

static hw_module_t camera_common = {
    .tag                    = HARDWARE_MODULE_TAG,
    .module_api_version     = CAMERA_MODULE_API_VERSION_2_3,
    .hal_api_version        = HARDWARE_HAL_API_VERSION,
    .id                     = CAMERA_HARDWARE_MODULE_ID,
    .name                   = "Intel Camera HAL Module",
    .author                 = "Intel",
    .methods                = &android::camera2::Camera2Module::mModuleMethods,
    .dso                    = NULL,
    .reserved               = {0}
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common                 = camera_common,
    .get_number_of_cameras  = android::camera2::Camera2Module::get_number_of_cameras,
    .get_camera_info        = android::camera2::Camera2Module::get_camera_info,
    .set_callbacks          = android::camera2::Camera2Module::set_callbacks,
    .get_vendor_tag_ops     = android::camera2::Camera2Module::get_vendor_tag_ops,
    .open_legacy            = android::camera2::Camera2Module::open_legacy,
    .set_torch_mode         = android::camera2::Camera2Module::set_torch_mode,
    .init                   = NULL,
    .reserved               = {0}
};

namespace android {
namespace camera2 {

Camera2Module *gCamera2Module = NULL;

/*===========================================================================
 * FUNCTION   : Camera2Module
 *
 * DESCRIPTION: default constructor of Camera2Module
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
Camera2Module::Camera2Module()
{
    camera_info info;
    mCallbacks = NULL;
    mNumOfCameras = std::min(icamera::PlatformData::numberOfCameras(), MAX_CAM_NUM);

    mHalDescriptors = new hal_desc[mNumOfCameras];

    uint32_t cameraId = 0;

    for (int i = 0; i < mNumOfCameras ; i++, cameraId++) {
        mHalDescriptors[i].cameraId = cameraId;
        mHalDescriptors[i].device_version = CAMERA_DEVICE_API_VERSION_3_3;
        //Query camera at this point in order
        //to avoid any delays during subsequent
        //calls to 'getCameraInfo()'
        getCameraInfo(i, &info);
    }
}

/*===========================================================================
 * FUNCTION   : ~Camera2Module
 *
 * DESCRIPTION: deconstructor of Camera2Module
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
Camera2Module::~Camera2Module()
{
    if ( NULL != mHalDescriptors ) {
        delete [] mHalDescriptors;
    }
}

/*===========================================================================
 * FUNCTION   : get_number_of_cameras
 *
 * DESCRIPTION: static function to query number of cameras detected
 *
 * PARAMETERS : none
 *
 * RETURN     : number of cameras detected
 *==========================================================================*/
int Camera2Module::get_number_of_cameras()
{
    int numCameras = 0;

    if (!gCamera2Module) {
        gCamera2Module = new Camera2Module();
        if (!gCamera2Module) {
            ALOGE("Failed to allocate Camera2Module object");
            return 0;
        }
    }

    numCameras = gCamera2Module->getNumberOfCameras();

    ALOGD("num of cameras: %d", numCameras);
    return numCameras;
}

/*===========================================================================
 * FUNCTION   : get_camera_info
 *
 * DESCRIPTION: static function to query camera information with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @info      : ptr to camera info struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::get_camera_info(int camera_id, struct camera_info *info)
{
    int rc = NO_ERROR;

    rc =  gCamera2Module->getCameraInfo(camera_id, info);

    return rc;
}

/*===========================================================================
 * FUNCTION   : set_callbacks
 *
 * DESCRIPTION: static function to set callbacks function to camera module
 *
 * PARAMETERS :
 *   @callbacks : ptr to callback functions
 *
 * RETURN     : NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::set_callbacks(const camera_module_callbacks_t *callbacks)
{
    int rc = NO_ERROR;
    rc =  gCamera2Module->setCallbacks(callbacks);

    return rc;
}

/*===========================================================================
 * FUNCTION   : open_legacy
 *
 * DESCRIPTION: Function to open older hal version implementation
 *
 * PARAMETERS :
 *   @hw_device : ptr to struct storing camera hardware device info
 *   @camera_id : camera ID
 *   @halVersion: Based on camera_module_t.common.module_api_version
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::open_legacy(const struct hw_module_t* module,
            const char* id, uint32_t halVersion, struct hw_device_t** device)
{
    int rc = NO_ERROR;
    if (module != &HAL_MODULE_INFO_SYM.common) {
        ALOGE("Invalid module. Trying to open %p, expect %p",
            module, &HAL_MODULE_INFO_SYM.common);
        return INVALID_OPERATION;
    }
    if (!id) {
        ALOGE("Invalid camera id");
        return BAD_VALUE;
    }
    rc =  gCamera2Module->openLegacy(atoi(id), halVersion, device);

    return rc;
}

/*===========================================================================
 * FUNCTION   : set_torch_mode
 *
 * DESCRIPTION: Attempt to turn on or off the torch mode of the flash unit.
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @on        : Indicates whether to turn the flash on or off
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::set_torch_mode(const char* camera_id, bool on)
{
    return gCamera2Module->setTorchMode(camera_id, on);
}

/*===========================================================================
 * FUNCTION   : getNumberOfCameras
 *
 * DESCRIPTION: query number of cameras detected
 *
 * PARAMETERS : none
 *
 * RETURN     : number of cameras detected
 *==========================================================================*/
int Camera2Module::getNumberOfCameras()
{
    return mNumOfCameras;
}

/*===========================================================================
 * FUNCTION   : getCameraInfo
 *
 * DESCRIPTION: query camera information with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @info      : ptr to camera info struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::getCameraInfo(int camera_id, struct camera_info *info)
{
    int rc;

    if (!mNumOfCameras || camera_id >= mNumOfCameras || !info ||
        (camera_id < 0)) {
        ALOGE("Error getting camera info!! mNumOfCameras = %d,"
                "camera_id = %d, info = %p",
                 mNumOfCameras, camera_id, info);
        return -ENODEV;
    }

    if ( NULL == mHalDescriptors ) {
        ALOGE("Hal descriptor table is not initialized!");
        return NO_INIT;
    }

    if ( mHalDescriptors[camera_id].device_version ==
            CAMERA_DEVICE_API_VERSION_3_3 ) {
        rc = Camera3HardwareInterface::getCamInfo(
                mHalDescriptors[camera_id].cameraId, info);
    } else {
        ALOGE("Device version for camera id %d invalid %d",
              camera_id,
              mHalDescriptors[camera_id].device_version);
        return BAD_VALUE;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : setCallbacks
 *
 * DESCRIPTION: set callback functions to send asynchronous notifications to
 *              frameworks.
 *
 * PARAMETERS :
 *   @callbacks : callback function pointer
 *
 * RETURN     :
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::setCallbacks(const camera_module_callbacks_t *callbacks)
{
    int rc = NO_ERROR;
    mCallbacks = callbacks;

    //rc = CameraFlash::getInstance().registerCallbacks(callbacks);
    if (rc != 0) {
        ALOGE("Failed to register callbacks with flash module!");
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : cameraDeviceOpen
 *
 * DESCRIPTION: open a camera device with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::cameraDeviceOpen(int camera_id,
                    struct hw_device_t **hw_device)
{
    int rc = NO_ERROR;
    if (camera_id < 0 || camera_id >= mNumOfCameras)
        return -ENODEV;

    if ( NULL == mHalDescriptors ) {
        ALOGE("Hal descriptor table is not initialized!");
        return NO_INIT;
    }

    ALOGI("Open camera id %d API version %d",
            camera_id, mHalDescriptors[camera_id].device_version);

    if ( mHalDescriptors[camera_id].device_version == CAMERA_DEVICE_API_VERSION_3_3 ) {
        Camera3HardwareInterface *hw = new Camera3HardwareInterface(mHalDescriptors[camera_id].cameraId,
                mCallbacks);
        if (!hw) {
            ALOGE("Allocation of hardware interface failed");
            return NO_MEMORY;
        }
        rc = hw->openCamera(hw_device);
        if (rc != 0) {
            delete hw;
        }
    } else {
        ALOGE("Device version for camera id %d invalid %d",
              camera_id,
              mHalDescriptors[camera_id].device_version);
        return BAD_VALUE;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : HAL_camera_device_open
 *
 * DESCRIPTION: static function to open a camera device by its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::HAL_camera_device_open(
    const struct hw_module_t *module, const char *id,
    struct hw_device_t **hw_device)
{
    int rc = NO_ERROR;
    if (module != &HAL_MODULE_INFO_SYM.common) {
        ALOGE("Invalid module. Trying to open %p, expect %p",
            module, &HAL_MODULE_INFO_SYM.common);
        return INVALID_OPERATION;
    }
    if (!id) {
        ALOGE("Invalid camera id");
        return BAD_VALUE;
    }

    rc = gCamera2Module->cameraDeviceOpen(atoi(id), hw_device);

    return rc;
}

struct hw_module_methods_t Camera2Module::mModuleMethods = {
    .open = Camera2Module::HAL_camera_device_open,
};

/*===========================================================================
 * FUNCTION   : openLegacy
 *
 * DESCRIPTION: Function to open older hal version implementation
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @halVersion: Based on camera_module_t.common.module_api_version
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::openLegacy(
        int32_t cameraId, uint32_t halVersion, struct hw_device_t** hw_device)
{
    return BAD_VALUE;
}

void Camera2Module::get_vendor_tag_ops(vendor_tag_ops_t* ops)
{
    return ;
}
/*===========================================================================
 * FUNCTION   : setTorchMode
 *
 * DESCRIPTION: Attempt to turn on or off the torch mode of the flash unit.
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @on        : Indicates whether to turn the flash on or off
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera2Module::setTorchMode(const char* camera_id, bool on)
{
    return 0;
}

} // namespace camera2
} // namespace android

