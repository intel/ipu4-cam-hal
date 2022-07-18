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
#include <memory>
#include "ICamera.h"
#include "Utils.h"
#include "Errors.h"
#include "IJpeg.h"
#include "JpegProcessor.h"

namespace camera3 {

enum PostProcessType {
    PROCESS_NONE          = 0,
    PROCESS_ROTATE        = 1 << 0,
    PROCESS_JPEG_ENCODING = 1 << 1
};

/**
 * \class PostProcessor
 *
 * This class is used to encode JPEG and rotate image.
 *
 */
class PostProcessor {

public:
    PostProcessor(int cameraId, const camera3_stream_t &stream, int type);
    virtual ~PostProcessor();

    icamera::status_t encodeJpegFrame(icamera::camera_buffer_t &mainBuf,
                                      icamera::camera_buffer_t &thumbBuf,
                                      icamera::Parameters &parameter,
                                      icamera::camera_buffer_t &jpegBuf);

private:
    DISALLOW_COPY_AND_ASSIGN(PostProcessor);

private:
    int mCameraId;
    camera3_stream_t mStream;
    int mPostProcessType;
    JpegProcessor *mJpegProc;
};

} // namespace camera3
