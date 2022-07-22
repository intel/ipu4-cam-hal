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

#include <mutex>
#include <vector>
#include <map>

#include <hardware/camera3.h>

#include "PostProcessor.h"
#include "ResultProcessor.h"

#include "Thread.h"

#include "Camera3Buffer.h"

namespace camera3 {

struct CaptureResult {
    uint32_t frameNumber;
    camera3_stream_buffer_t outputBuffer;
    buffer_handle_t handle;
    icamera::camera_buffer_t halBuffer;
};

struct MemoryBlob {
    void    *addr;
    bool     busy;
};

/**
 * \class StreamBufferPool
 *
 * Still capture stream needs local memory for capturing YUV images.
 * This class is to manage a local memory pool for still capture stream
 */
class StreamBufferPool {

public:
    StreamBufferPool();
    ~StreamBufferPool();

    int allocate(uint32_t blobSize, uint32_t numBlob);
    int destroy();
    MemoryBlob *getBlob();
    void        returnBlob(void *memAddr);

private:
    MemoryBlob *mBlobs;
    uint32_t    mNumBlob;
    std::mutex  mLock;
};

/**
 * \class Camera3Stream
 *
 * This class is used to handle requests. It has the following
 * roles:
 * - It instantiates ResultProcessor.
 */
class Camera3Stream : public icamera::Thread {

public:
    Camera3Stream(int cameraId, CallbackEventInterface *callback,
                  uint32_t maxNumReqInProc, const icamera::stream_t &halStream,
                  const camera3_stream_t &stream, const icamera::stream_t *shdStream = nullptr);
    virtual ~Camera3Stream();

    virtual bool threadLoop();
    virtual void requestExit();

    int processRequest(const camera3_stream_buffer_t &outputBuffer,
                       icamera::camera_buffer_t *buffer,
                       icamera::camera_buffer_t *shdBuffer = nullptr);

    void queueBufferDone(uint32_t frameNumber, const camera3_stream_buffer_t &outputBuffer,
                         const icamera::camera_buffer_t &halBuffer);
    void setActive(bool state);
    bool isActive() { return mStreamState; }

private:
    int getPostProcessType(const camera3_stream_t &stream);

private:
    const uint64_t kMaxDuration = 2000000000; // 2000ms

    int mCameraId;
    std::condition_variable mBufferDoneCondition;
    std::mutex mLock;

    CallbackEventInterface *mEventCallback;

    PostProcessor *mPostProcessor;

    bool mStreamState;
    icamera::stream_t  mHALStream;
    const icamera::stream_t *mThbStream; // the thumbnail stream
    uint32_t           mMaxNumReqInProc;
    StreamBufferPool   mStillBufPool;
    StreamBufferPool   mThumbBufPool;

    camera3_stream_t   mStream;

    std::vector<CaptureResult*> mCaptureResultVector;

    std::map<buffer_handle_t, Camera3Buffer*> mBuffers;
};

} // namespace camera3
