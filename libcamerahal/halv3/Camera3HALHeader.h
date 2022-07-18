/*
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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

#ifndef __CAMERA_HALHEADER_H__
#define __CAMERA_HALHEADER_H__

// System dependencies
#include <hardware/gralloc.h>

// Camera dependencies

namespace android {
namespace camera2 {


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define IS_USAGE_VIDEO(usage)  (((usage) & (GRALLOC_USAGE_HW_VIDEO_ENCODER)) \
        == (GRALLOC_USAGE_HW_VIDEO_ENCODER))
#define IS_USAGE_PREVIEW(usage) (((usage) & (GRALLOC_USAGE_HW_TEXTURE)) \
        == (GRALLOC_USAGE_HW_TEXTURE))
#define IS_USAGE_SWREADER(usage) (((usage) & (GRALLOC_USAGE_SW_READ_OFTEN)) \
        == (GRALLOC_USAGE_SW_READ_OFTEN))

#define GPS_PROCESSING_METHOD_SIZE 33
#define EXIF_IMAGE_DESCRIPTION_SIZE 100

#define MAX_INFLIGHT_REQUESTS  6
#define MAX_INFLIGHT_BLOB      3
#define MIN_INFLIGHT_REQUESTS  4
#define MAX_INFLIGHT_REPROCESS_REQUESTS 1
#define MAX_INFLIGHT_HFR_REQUESTS (48)
#define MIN_INFLIGHT_HFR_REQUESTS (40)

// valid, means configed by Android framework
typedef enum {
    INVALID,
    VALID,
} stream_status_t;

// ipu4 support 2 hw output at most
typedef enum {
    HW_CHANNEL0,
    HW_CHANNEL1,
    NONE_CHANNEL,
} stream_type_t;

typedef struct {
    int32_t width;
    int32_t height;
} cam_dimension_t;

typedef enum {
   REPROCESS_TYPE_NONE,
   REPROCESS_TYPE_JPEG,
   REPROCESS_TYPE_YUV,
   REPROCESS_TYPE_PRIVATE,
   REPROCESS_TYPE_RAW
} reprocess_type_t;

typedef struct {
    uint32_t out_buf_index;
    int32_t jpeg_orientation;
    uint8_t jpeg_quality;
    uint8_t jpeg_thumb_quality;
    cam_dimension_t thumbnail_size;
    uint8_t gps_timestamp_valid;
    int64_t gps_timestamp;
    uint8_t gps_coordinates_valid;
    double gps_coordinates[3];
    char gps_processing_method[GPS_PROCESSING_METHOD_SIZE];
    uint8_t image_desc_valid;
    char image_desc[EXIF_IMAGE_DESCRIPTION_SIZE];
} jpeg_settings_t;

} //namespace camera2
} //namespace android

#endif
