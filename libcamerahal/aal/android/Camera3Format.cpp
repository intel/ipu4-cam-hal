/*
 * Copyright (C) 2017-2018 Intel Corporation.
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

#define LOG_TAG "Camera3Format"

#include "Camera3Format.h"
#include "camera3.h"
#include <ICamera.h>
#include <linux/videodev2.h>
#include <map>
#include <utils/Log.h>

using namespace std;

static map<int, int> v4l2HalFormat = {
    {V4L2_PIX_FMT_NV12, HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED},
    {V4L2_PIX_FMT_RGB565, HAL_PIXEL_FORMAT_RGB_565},
    {V4L2_PIX_FMT_YVU420, HAL_PIXEL_FORMAT_YV12},
    {V4L2_PIX_FMT_YUYV, HAL_PIXEL_FORMAT_YCbCr_422_I},
    {V4L2_PIX_FMT_UYVY, HAL_PIXEL_FORMAT_YCbCr_422_I},
    {V4L2_PIX_FMT_NV16, HAL_PIXEL_FORMAT_YCbCr_422_SP},
};

static map<int, int> hal2V4lFormat = {
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,  V4L2_PIX_FMT_NV12},
    {HAL_PIXEL_FORMAT_YCBCR_420_888,  V4L2_PIX_FMT_NV12},
    {HAL_PIXEL_FORMAT_RGB_565,  V4L2_PIX_FMT_RGB565},
    {HAL_PIXEL_FORMAT_YV12,  V4L2_PIX_FMT_YVU420},
    {HAL_PIXEL_FORMAT_YCbCr_422_I,  V4L2_PIX_FMT_YUYV},
    {HAL_PIXEL_FORMAT_YCbCr_422_SP,  V4L2_PIX_FMT_NV16},
};

static vector<int> halSupportFormats = {
    HAL_PIXEL_FORMAT_RGB_565,
    HAL_PIXEL_FORMAT_YV12,
    HAL_PIXEL_FORMAT_RAW_OPAQUE,
    HAL_PIXEL_FORMAT_BLOB,
    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
    HAL_PIXEL_FORMAT_YCBCR_420_888,
    HAL_PIXEL_FORMAT_YCbCr_422_SP,
    HAL_PIXEL_FORMAT_YCbCr_422_I,
    HAL_PIXEL_FORMAT_JPEG,
};
bool Camera3Format::checkHalFormat(int format) {
    auto iter = find(halSupportFormats.begin(), halSupportFormats.end(), format);
    if (iter == halSupportFormats.end())
        return false;
    return true;
}

/* fmt mapping shuold align with VPG*/
int Camera3Format::HalFormat2V4L2Format(int HalFormat) {
    int format = V4L2_PIX_FMT_NV12;
    auto iter = hal2V4lFormat.find(HalFormat);
    if (iter == hal2V4lFormat.end()) {
        ALOGE("%s: Unsupported HAL format: %d, use default V4L2 format",
            __func__, HalFormat);
        return format;
    }
    return hal2V4lFormat[HalFormat];
}

int Camera3Format::V4L2Format2HalFormat(int V4L2Format) {
    int format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    auto iter = v4l2HalFormat.find(V4L2Format);
    if (iter == v4l2HalFormat.end()) {
        ALOGE("%s: Unsupported V4L2 format: %d, use default HAL format",
                __func__, V4L2Format);
        return format;
    }
    return v4l2HalFormat[V4L2Format];
}

