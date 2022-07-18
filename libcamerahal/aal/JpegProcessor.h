/*
 * Copyright (C) 2018 Intel Corporation
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
#include "IJpeg.h"
#include <memory>

namespace camera3 {

class JpegProcessor {

public:
    JpegProcessor();
    ~JpegProcessor();

    icamera::status_t doJpegProcess(icamera::camera_buffer_t &mainBuf,
                                    icamera::camera_buffer_t &thumbBuf,
                                    icamera::Parameters &parameter,
                                    icamera::camera_buffer_t &jpegBuf);

private:
    DISALLOW_COPY_AND_ASSIGN(JpegProcessor);

    void attachJpegBlob(int finalJpegSize, icamera::EncodePackage &package);
#ifdef CAL_BUILD
    int doJpegEncode(const icamera::InputBuffer &in, const icamera::OutputBuffer *out);
#endif

private:
    // JpegCompressor needs YU12 format
    // and the ISP doesn't output YU12 directly.
    // so a temporary intermediate buffer is needed.
    std::unique_ptr<char[]> mInternalBuffer;
    unsigned int mInternalBufferSize;
};

} // namespace camera3
