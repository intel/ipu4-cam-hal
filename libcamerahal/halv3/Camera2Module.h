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

#ifndef __CAMERA2MODUEL_H__
#define __CAMERA2MODUEL_H__

// Camera dependencies
#include <hardware/camera_common.h>

namespace android {
namespace camera2 {

typedef struct {
    uint32_t cameraId;
    uint32_t device_version;
} hal_desc;

class Camera2Module
{
public:
    Camera2Module();
    virtual ~Camera2Module();

    static int get_number_of_cameras();
    static int get_camera_info(int camera_id, struct camera_info *info);
    static int set_callbacks(const camera_module_callbacks_t *callbacks);
    static int open_legacy(const struct hw_module_t* module,
            const char* id, uint32_t halVersion, struct hw_device_t** device);
    static void get_vendor_tag_ops(vendor_tag_ops_t* ops);
    static int set_torch_mode(const char* camera_id, bool on);

private:
    int getNumberOfCameras();
    int getCameraInfo(int camera_id, struct camera_info *info);
    int setCallbacks(const camera_module_callbacks_t *callbacks);
    int cameraDeviceOpen(int camera_id, struct hw_device_t **hw_device);
    static int HAL_camera_device_open(const struct hw_module_t *module, const char *id,
                struct hw_device_t **hw_device);
    static int openLegacy(
            int32_t cameraId, uint32_t halVersion, struct hw_device_t** hw_device);
    int setTorchMode(const char* camera_id, bool on);

public:
    static struct hw_module_methods_t mModuleMethods;

private:
    int mNumOfCameras;
    hal_desc *mHalDescriptors;
    const camera_module_callbacks_t *mCallbacks;
};

} //namespace camera2
} //namespace android

#endif /* __CAMERA2MODUEL_H__ */
