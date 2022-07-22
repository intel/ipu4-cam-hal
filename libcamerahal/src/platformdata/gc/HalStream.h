/*
 * Copyright (C) 2016-2018 Intel Corporation
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

namespace icamera {

// Temporary solution
enum StreamUseCase {
    USE_CASE_COMMON = 0,
    USE_CASE_PREVIEW,        // For HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
    USE_CASE_VIDEO,          // For HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
    USE_CASE_STILL_CAPTURE,  // For HAL_PIXEL_FORMAT_BLOB/HAL_PIXEL_FORMAT_YCbCr_420_888
    USE_CASE_RAW,            // For HAL_PIXEL_FORMAT_RAW16/HAL_PIXEL_FORMAT_RAW_OPAQUE
    USE_CASE_ZSL,            // For ZSL stream
    USE_CASE_INPUT           // For input stream
};

struct streamProps {
    uint32_t width;
    uint32_t height;
    int format;
    StreamUseCase useCase;
};

class HalStream
{
public:
    HalStream(struct streamProps &props, void *priv):
        mWidth(props.width),
        mHeight(props.height),
        mFormat(props.format),
        mUseCase(props.useCase)
    {
        maxBuffers = 0;
        mPrivate = priv;
    }

    ~HalStream() { }

    uint32_t width() { return mWidth; }
    uint32_t height() { return mHeight; }
    int format() { return mFormat; }
    StreamUseCase useCase() { return mUseCase; }
    void *priv() { return mPrivate; }

private:
    uint32_t mWidth;
    uint32_t mHeight;
    int mFormat;  // TODO: use v4l2 definition
    StreamUseCase mUseCase;

    int maxBuffers;
    void *mPrivate;
};

} /* namespace icamera */
