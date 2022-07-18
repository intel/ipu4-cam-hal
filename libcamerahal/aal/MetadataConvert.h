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

#pragma once

#include <hardware/camera3.h>
#include "HALv3Header.h"

#include "Parameters.h"

namespace camera3 {

/**
 * \class MetadataConvert
 *
 * This class is used to convert application metadata to HAL metadata.
 *
 */
class MetadataConvert {

public:
    MetadataConvert(int cameraId);
    virtual ~MetadataConvert();

    static int constructDefaultMetadata(android::CameraMetadata *settings);
    static int updateDefaultRequestSettings(int type, android::CameraMetadata *settings);

    static int requestMetadataToHALMetadata(const android::CameraMetadata &settings,
                                            icamera::Parameters *parameter);
    static int HALMetadataToRequestMetadata(const icamera::Parameters &parameter,
                                            android::CameraMetadata *settings);
    static int HALCapabilityToStaticMetadata(const icamera::Parameters &parameter,
                                             android::CameraMetadata *settings);
    static void dumpMetadata(const camera_metadata_t *meta);
private:
    DISALLOW_COPY_AND_ASSIGN(MetadataConvert);

    // Parameters -> Android dynamic metadata
    static int convertColorCorrectionParameter(const icamera::Parameters &parameter,
                                               android::CameraMetadata *settings);
    static int convertControlParameter(const icamera::Parameters &parameter,
                                       android::CameraMetadata *settings);
    static int convertSensorParameter(const icamera::Parameters &parameter,
                                      android::CameraMetadata *settings);
    static int convertLensParameter(const icamera::Parameters &parameter,
                                      android::CameraMetadata *settings);
    static int convertStatisticsParameter(const icamera::Parameters & /*parameter*/,
                                          android::CameraMetadata *settings);
    static int convertFlashParameter(const icamera::Parameters & /*parameter*/,
                                     android::CameraMetadata *settings);
    static int convertAdvancedFeatureParameter(const icamera::Parameters & /*parameter*/,
                                               android::CameraMetadata *settings);

    // Android control metadata -> parameters
    static int convertColorCorrectionMetadata(const android::CameraMetadata &settings,
                                              icamera::Parameters *parameter);
    static int convertControlMetadata(const android::CameraMetadata &settings,
                                      icamera::Parameters *parameter);
    static int convertJpegMetadata(const android::CameraMetadata &settings,
                                   icamera::Parameters *parameter);
    static int convertSensorMetadata(const android::CameraMetadata &settings,
                                     icamera::Parameters *parameter);

    // Capabilities -> Android static metadata
    static void fillControlStaticMetadata(const icamera::Parameters &parameter,
                                          android::CameraMetadata *settings);
    static void fillScalerStaticMetadata(const icamera::Parameters &parameter,
                                         android::CameraMetadata *settings);
    static void fillSensorStaticMetadata(const icamera::Parameters &parameter,
                                         android::CameraMetadata *settings);
    static void fillLensStaticMetadata(const icamera::Parameters &parameter,
                                       android::CameraMetadata *settings);
    static void fillRequestStaticMetadata(const icamera::Parameters &parameter,
                                          android::CameraMetadata *settings);
    static void fillStatisticsStaticMetadata(const icamera::Parameters &parameter,
                                             android::CameraMetadata *settings);
    static void fillJpegStaticMetadata(const icamera::Parameters &parameter,
                                       android::CameraMetadata *settings);
    static void fillAdvancedFeatureStaticMetadata(const icamera::Parameters &parameter,
                                                  android::CameraMetadata *settings);

    static int convertToHalWindow(const int32_t* data, int dataCount,
                                  icamera::camera_window_list_t* windows);
    static int convertToMetadataRegion(const icamera::camera_window_list_t& windows,
                                       int dataCount, int32_t* data);

private:
    int mCameraId;
};

} // namespace camera3
