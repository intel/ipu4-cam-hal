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

#define LOG_TAG "PostProcessor"

#include "HALv3Utils.h"
#include "PostProcessor.h"

namespace camera3 {

PostProcessor::PostProcessor(int cameraId, const camera3_stream_t &stream, int type) :
    mCameraId(cameraId),
    mStream(stream),
    mPostProcessType(type),
    mJpegProc(nullptr)
{
    mJpegProc = new JpegProcessor();
    LOG1("@%s, camera id %d, format %x, rotation %d",
        __func__, mCameraId, mStream.format, mStream.rotation);
}

PostProcessor::~PostProcessor()
{
    delete mJpegProc;
    LOG1("@%s", __func__);
}

icamera::status_t PostProcessor::encodeJpegFrame(icamera::camera_buffer_t &mainBuf,
                                                 icamera::camera_buffer_t &thumbBuf,
                                                 icamera::Parameters &parameter,
                                                 icamera::camera_buffer_t &jpegBuf)
{
    return mJpegProc->doJpegProcess(mainBuf, thumbBuf, parameter, jpegBuf);
}

} // namespace camera3
