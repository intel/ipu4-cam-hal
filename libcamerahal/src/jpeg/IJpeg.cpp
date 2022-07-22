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

#define LOG_TAG "IJpeg"

#include "iutils/CameraLog.h"

#include "IJpeg.h"
#include "SWJpegEncoder.h"
#include "JpegMaker.h"

namespace icamera {

static JpegMaker * gJpegMaker = nullptr;

// SW_JPEG_ENCODE_S
static SWJpegEncoder * gSWJpegEncoder = nullptr;
// SW_JPEG_ENCODE_E

int camera_jpeg_init()
{
    HAL_TRACE_CALL(1);
    return gJpegMaker->init();
}

int camera_jpeg_deinit()
{
    HAL_TRACE_CALL(1);

    return OK;
}

// SW_JPEG_ENCODE_S
int camera_jpeg_encode(const InputBuffer &in, const OutputBuffer &out)
{
    HAL_TRACE_CALL(1);
    return gSWJpegEncoder->encode(in, out);
}
// SW_JPEG_ENCODE_E

int camera_jpeg_make(EncodePackage &package, int &finalSize)
{
    HAL_TRACE_CALL(1);
    return gJpegMaker->makeJpeg(package, finalSize);
}

int camera_setupExifWithMetaData(EncodePackage & package, ExifMetaData *metaData)
{
    HAL_TRACE_CALL(1);
    return gJpegMaker->setupExifWithMetaData(package, metaData);
}

//Create the HAL instance from here
__attribute__((constructor)) void initJPEG() {

    Log::setDebugLevel();
    gJpegMaker = new JpegMaker();
    // SW_JPEG_ENCODE_S
    gSWJpegEncoder = new SWJpegEncoder();
    // SW_JPEG_ENCODE_E
}

__attribute__((destructor)) void deinitJPEG() {
    if (gJpegMaker) {
        delete gJpegMaker;
        gJpegMaker = nullptr;

    }
    // SW_JPEG_ENCODE_S
    if (gSWJpegEncoder) {
        delete gSWJpegEncoder;
        gSWJpegEncoder = nullptr;

    }
    // SW_JPEG_ENCODE_E
}

} // namespace icamera
