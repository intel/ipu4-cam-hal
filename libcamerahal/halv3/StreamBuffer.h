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

#ifndef _CAMERA3_HAL_STREAM_BUFFER_H_
#define _CAMERA3_HAL_STREAM_BUFFER_H_

#include <sys/mman.h>
#include <utils/Trace.h>
#include <ui/Fence.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/GraphicBuffer.h>
#include <system/graphics.h>
#include "camera3.h"
#include <ICamera.h>
#include "Camera3HALHeader.h"

namespace android {
namespace camera2 {

/**
 * \class StreamBuffer
 *
 * This class is the buffer abstraction in the HAL. It can store buffers
 * provided by the framework or buffers allocated by the HAL.
 * Allocation in the HAL can be done via gralloc, malloc or mmap
 * in case of mmap the memory cannot be freed
 */
class StreamBuffer: public RefBase {
public:
    /**
     * default constructor
     * Used for buffers coming from the framework. The wrapper is initialized
     * using the method init
     */
    StreamBuffer(const camera3_stream_buffer *aBuffer, int stream_id, int frame_id);

    status_t lock();
    status_t unlock();

    status_t waitOnAcquireFence();
    //connect the HAL buffer with the buffer address(DMA FD) from the GFX buffer
    status_t connect();
    void check(icamera::camera_buffer_t *buf);
    int frameid() {return mFrameId; }
    int width() {return mWidth; }
    int height() {return mHeight; }
    int format() {return mFormat; }
    int v4l2Fmt() {return mV4L2Fmt; }
    bool needMetadata() {return mNeedMetadata; }
    const camera3_stream_buffer_t * getUserBuf() { return mUserBuffer; }
    icamera::camera_buffer_t * getHalBuf() { return &mHalBuffer; }
    buffer_handle_t getBufferHandle() { return mBufferHandle; }
    void updateBufferInfo(const camera3_stream_buffer_t *stream_buf, int frame_id);

private:  /* methods */
    /**
     * no need to delete a buffer since it is RefBase'd. Buffer will be deleted
     * when no reference to it exist.
     */
    virtual ~StreamBuffer();

private:
    const camera3_stream_buffer_t *mUserBuffer; /*!< Original structure passed by request */
    icamera::camera_buffer_t mHalBuffer; /*Buffer used by the PSL */
    int             mWidth;
    int             mHeight;
    int             mFormat;         /*!<  Gfx HAL PIXEL fmt */
    int             mV4L2Fmt;        /*!< V4L2 fourcc format code */
    bool            mLocked;         /*!< Use to track the lock status */
    int             mFrameId;
    buffer_handle_t mBufferHandle;     /*!< Structure provided by FrameWork */
    void*         mDataPtr;           /*!< if locked, here is the vaddr */
    int             mDmaFd;
    bool            mDma;
    bool            mNeedMetadata;
};

} // namespace camera2
} // namespace android

#endif // _CAMERA3_HAL_STREAM_BUFFER_H_
