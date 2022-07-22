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

#pragma once

#include <limits.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

#include "V4l2DevBase.h"

namespace icamera {

enum EncodeBufferType {
    ENCODE_ISA_CONFIG  = 0,
    ENCODE_STATS = 1,
};

enum VideoNodeDirection {
    INPUT_VIDEO_NODE,   /*!< input video devices like cameras or capture cards */
    OUTPUT_VIDEO_NODE  /*!< output video devices like displays */
};

class V4l2Dev: public V4l2DevBase {
public:
    explicit V4l2Dev(int cameraId, VideoNodeType nodeType, VideoNodeDirection nodeDirection);
     ~V4l2Dev();

    int queryCap(struct v4l2_capability *cap);
    int setFormat(struct v4l2_format &aFormat);
    int requestBuffers(size_t numBuffers, int memType);
    int queryBuffer(int index, bool cached, struct v4l2_buffer *vbuf);
    int exportDmaBuf(struct v4l2_buffer &buf, int plane);
    int qbuf(struct v4l2_buffer &vbuf);
    int dqbuf(struct v4l2_buffer &vbuf);
    int grabFrame(struct v4l2_buffer &vbuf);
    int streamOn();
    int streamOff();
    int poll(int timeout);

    int openDev();
    int closeDev();

    // Buffer pool management -- DEPRECATED!
    void destroyBufferPool();
    VideoNodeType getType() const { return mType; }

private:
    int mCameraId;

    /**
     * Tracks the number of output buffers produced by the device. Running counter.
     * It is reset when we start the device.
     */
    uint64_t mFrameCounter;

    VideoNodeType     mType;
    v4l2_buf_type mBufType;
    int                mMemoryType;
    VideoNodeDirection mNodeDirection;
};

} //namespace icamera

