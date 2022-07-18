/*
 * Copyright (C) 2013-2018 Intel Corporation
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
#include <cros-camera/camera_buffer_manager.h>
#include <memory>

namespace camera3 {

// Forward declaration to  avoid extra include
class Camera3Stream;

/**
 * \class Camera3Buffer
 *
 * This class is the buffer abstraction in the HAL. It can store buffers
 * provided by the framework or buffers allocated by the HAL.
 * Allocation in the HAL can be done via gralloc, malloc or mmap
 * in case of mmap the memory cannot be freed
 */
class Camera3Buffer {
public:
    enum BufferType {
        BUF_TYPE_HANDLE,
        BUF_TYPE_MALLOC,
        BUF_TYPE_MMAP,
    };

public:
    /**
     * default constructor
     * Used for buffers coming from the framework. The wrapper is initialized
     * using the method init
     */
    Camera3Buffer();

    /**
     * no need to delete a buffer since it is RefBase'd. Buffer will be deleted
     * when no reference to it exist.
     */
    ~Camera3Buffer();

    /**
     * constructor for the HAL-allocated buffer
     * These are used via the utility methods in the MemoryUtils namespace
     */
    Camera3Buffer(int w, int h, int s, int v4l2fmt, void* usrPtr, int cameraId, int dataSizeOverride = 0);
    Camera3Buffer(int w, int h, int s, int fd, int dmaBufFd, int length, int v4l2fmt,
                 int offset, int prot, int flags);
    /**
     * initialization for the wrapper around the framework buffers
     */
    icamera::status_t init(const camera3_stream_buffer *aBuffer, int cameraId);

    /**
     * initialization for the fake framework buffer (allocated by the HAL)
     */
    icamera::status_t init(const camera3_stream_t* stream, buffer_handle_t buffer,
                  int cameraId);

    /**
     * deinitialization for the wrapper around the framework buffers
     */
    icamera::status_t deinit();

    void* data() { return mDataPtr; };

    icamera::status_t lock();
    icamera::status_t lock(int flags);
    icamera::status_t unlock();

    bool isLocked() const { return mLocked; };
    buffer_handle_t * getBufferHandle() { return &mHandle; };
    icamera::status_t waitOnAcquireFence();

    void dump();
    void dumpImage(int frameNumber, const int type, int format);
    void dumpImage(const void *data, int frameNumber, const int size, int width, int height,
                   int format) const;
    int v4L2Fmt2GFXFmt(int v4l2Fmt);
    Camera3Stream * getOwner() const { return mOwner; }
    int width() {return mWidth; }
    int height() {return mHeight; }
    int stride() {return mStride; }
    unsigned int size() {return mSize; }
    int format() {return mFormat; }
    int v4l2Fmt() {return mV4L2Fmt; }
    struct timeval timeStamp() {return mTimestamp; }
    void setTimeStamp(struct timeval timestamp) {mTimestamp = timestamp; }
    icamera::status_t getFence(camera3_stream_buffer* buf);
    int dmaBufFd() {return mType == BUF_TYPE_HANDLE ? mHandle->data[0] : mDmaBufFd;}
    int status() { return mUserBuffer.status; }

private:
    icamera::status_t registerBuffer();
    icamera::status_t deregisterBuffer();

private:
     /*!< Original structure passed by request */
    camera3_stream_buffer_t mUserBuffer = {0, 0, 0, .acquire_fence=-1, .release_fence=-1};
    int             mWidth;
    int             mHeight;
    unsigned int    mSize;           /*!< size in bytes, this is filled when we
                                           lock the buffer */
    int             mFormat;         /*!<  HAL PIXEL fmt */
    int             mV4L2Fmt;        /*!< V4L2 fourcc format code */
    int             mStride;
    int             mUsage;
    struct timeval  mTimestamp = {};
    bool            mInit;           /*!< Boolean to check the integrity of the
                                          buffer when it is created*/
    bool            mLocked;         /*!< Use to track the lock status */
    bool            mRegistered;     /*!< Use to track the buffer register status */

    BufferType mType;
    cros::CameraBufferManager* mGbmBufferManager;
    buffer_handle_t mHandle = {};
    buffer_handle_t* mHandlePtr = nullptr;
    Camera3Stream *mOwner;             /*!< Stream this buffer belongs to */
    void*         mDataPtr;           /*!< if locked, here is the vaddr */

    int mCameraId;
    int mDmaBufFd;                    /*!< file descriptor for dmabuf */
};

namespace MemoryUtils {

std::shared_ptr<Camera3Buffer>
allocateHeapBuffer(int w,
                   int h,
                   int s,
                   int v4l2Fmt,
                   int cameraId,
                   int dataSizeOverride = 0);

std::shared_ptr<Camera3Buffer>
allocateHandleBuffer(int w,
                     int h,
                     int gfxFmt,
                     int usage,
                     int cameraId);
};

}
