/*
 * Copyright (C) 2013-2017 Intel Corporation
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

#define LOG_TAG "StreamBuffer"
#include "StreamBuffer.h"
#include "Gfx.h"

#include <linux/videodev2.h>
#include <unistd.h>
#include <hardware/hardware.h>
#include <pthread.h>

namespace android {
namespace camera2 {
#undef ALOGD
#define ALOGD(...) ((void*)0)
////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
////////////////////////////////////////////////////////////////////

/**
 * StreamBuffer
 *
 * Construct the StreamBuffer from the frameworks camera3_stream_buffer
 */
StreamBuffer::StreamBuffer(
    const camera3_stream_buffer *aBuffer,
    int stream_id,
    int frame_id)
{
    mUserBuffer = aBuffer;
    mWidth = aBuffer->stream->width;
    mHeight = aBuffer->stream->height;
    mFormat = aBuffer->stream->format;
    mFrameId = frame_id;
    mLocked = false;
    mDataPtr = NULL;
    mDma = false;
    mDmaFd = -1;

    mBufferHandle = *mUserBuffer->buffer;

    if (IS_USAGE_VIDEO(mUserBuffer->stream->usage))
        mDma = true;
    else
        mDma = false;

    // set color range to GFX
    setBufferColorRange(aBuffer->buffer, false);
    ALOGD("%s constructor for buf %p", __func__, this);
    CLEAR(mHalBuffer);

    //Return metadata for stream 0 only
    if (stream_id == 0)
        mNeedMetadata = true;
    else
        mNeedMetadata = false;
}

StreamBuffer::~StreamBuffer()
{
    if (mLocked) {
        unlock();
    }

    ALOGD("%s destroying buf %p", __FUNCTION__, this);
}

status_t StreamBuffer::connect()
{
    //Use dma-buf only for video encoder buffers
    if (mDma) {
        int dma_buf_fd = getNativeHandleDmaBufFd(&mBufferHandle);
        if (dma_buf_fd < 0) {
            ALOGE("Error getting valid dma buf handle");
            return BAD_VALUE;
        }
        mHalBuffer.dmafd = dma_buf_fd;
        return OK;
    }

    //Lock the user buffers for userptr
    return lock();
}

status_t StreamBuffer::waitOnAcquireFence()
{
    ALOGI("%s: Fence in HAL is %d", __FUNCTION__, mUserBuffer->acquire_fence);

    if (mUserBuffer->acquire_fence != -1) {
        sp<Fence> bufferAcquireFence = new Fence(mUserBuffer->acquire_fence);
        int res = bufferAcquireFence->wait(2000);  // 2s
        if (res == TIMED_OUT) {
            ALOGE("%s: Buffer %p: Fence timed out after %d ms",
                    __FUNCTION__, this, 2000);
            return BAD_VALUE;
        }
    }
    return NO_ERROR;
}

/**
 * lock
 *
 * lock the gralloc buffer with specified usage
 *
 */
status_t StreamBuffer::lock()
{
    void * data = NULL;
    status_t status = OK;
    int flags = mUserBuffer->stream->usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK |
        GRALLOC_USAGE_HW_CAMERA_MASK);
//    flags = flags | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_COMPOSER;

    if (!flags) {
        ALOGE("@%s:trying to lock a buffer with no flags", __func__);
        return INVALID_OPERATION;
    }

    if (mLocked) {
        ALOGE("Warning @%s: Already locked", __func__);
        return OK;
    }

    GraphicBufferMapper &mapper = GraphicBufferMapper::get();

    ALOGD("%s: Lock with %dx%d", __func__, mWidth, mHeight);

    const Rect bounds(mWidth, mHeight);
    status = mapper.lock(mBufferHandle, flags, bounds, &data);

    if (status != NO_ERROR) {
       ALOGE("ERROR @%s: Failed to lock GraphicBufferMapper! %d", __func__, status);
       mapper.unlock(mBufferHandle);
       return status;
    }

    mLocked = true;
    mDataPtr = data;
    mHalBuffer.addr = data;

    ALOGD("%s: lock dataptr @%p", __func__, data);

    return status;
}

status_t StreamBuffer::unlock()
{
    if (!mLocked) {
        ALOGW("Warning @%s: Unlocked a unlocked buffer", __func__);
        return NO_ERROR;
    }
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    mapper.unlock(mBufferHandle);
    mLocked = false;
    return NO_ERROR;
}

void StreamBuffer::check(icamera::camera_buffer_t *buf)
{
    if (mDma) {
        if (buf->dmafd != mDmaFd) {
            ALOGE("Error @%s: Wrong DMA fd in gFX buffer.", __func__);
        }
    } else {
        if (buf->addr != mDataPtr) {
            ALOGE("Error @%s: Wrong userptr in gFX buffer.", __func__);
        }
    }
}

void StreamBuffer::updateBufferInfo(const camera3_stream_buffer_t *stream_buf, int frame_id)
{
    mUserBuffer = stream_buf;
    mFrameId = frame_id;
    mBufferHandle = *mUserBuffer->buffer;
}

} // namespace camera2
} // namespace android
