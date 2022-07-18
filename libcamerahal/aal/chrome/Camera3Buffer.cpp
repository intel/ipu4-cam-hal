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

#define LOG_TAG "Camera3Buffer"

#include <sys/mman.h>
#include <unistd.h>
#include <sync/sync.h>

#include "iutils/CameraLog.h"
#include "iutils/Errors.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"
#include "Camera3Buffer.h"
#include "Camera3Stream.h"

using namespace icamera;

namespace camera3 {
////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
////////////////////////////////////////////////////////////////////

/**
 * Camera3Buffer
 *
 * Default constructor
 * This constructor is used when we pre-allocate the Camera3Buffer object
 * The initialization will be done as a second stage wit the method
 * init(), where we initialize the wrapper with the gralloc buffer provided by
 * the framework
 */
Camera3Buffer::Camera3Buffer() :  mWidth(0),
                                mHeight(0),
                                mSize(0),
                                mFormat(0),
                                mV4L2Fmt(0),
                                mStride(0),
                                mUsage(0),
                                mInit(false),
                                mLocked(false),
                                mRegistered(false),
                                mType(BUF_TYPE_HANDLE),
                                mGbmBufferManager(nullptr),
                                mOwner(nullptr),
                                mDataPtr(nullptr),
                                mCameraId(0),
                                mDmaBufFd(-1)
{
    LOG1("%s default constructor for buf %p", __FUNCTION__, this);

}

/**
 * Camera3Buffer
 *
 * Constructor for buffers allocated using MemoryUtils::allocateHeapBuffer
 *
 * \param w [IN] width
 * \param h [IN] height
 * \param s [IN] stride
 * \param v4l2fmt [IN] V4l2 format
 * \param usrPtr [IN] Data pointer
 * \param cameraId [IN] id of camera being used
 * \param dataSizeOverride [IN] buffer size input. Default is 0 and frameSize()
                                is used in that case.
 */
Camera3Buffer::Camera3Buffer(int w,
                           int h,
                           int s,
                           int v4l2fmt,
                           void* usrPtr,
                           int cameraId,
                           int dataSizeOverride):
        mWidth(w),
        mHeight(h),
        mSize(0),
        mFormat(0),
        mV4L2Fmt(v4l2fmt),
        mStride(s),
        mUsage(0),
        mInit(false),
        mLocked(true),
        mRegistered(false),
        mType(BUF_TYPE_MALLOC),
        mGbmBufferManager(nullptr),
        mOwner(nullptr),
        mDataPtr(nullptr),
        mCameraId(cameraId),
        mDmaBufFd(-1)
{
    LOG1("%s create malloc camera buffer %p", __FUNCTION__, this);
    if (usrPtr != nullptr) {
        mDataPtr = usrPtr;
        mInit = true;
        mSize = dataSizeOverride ? dataSizeOverride : CameraUtils::getFrameSize(mV4L2Fmt, mStride, mHeight);
        mFormat = v4L2Fmt2GFXFmt(v4l2fmt);
    } else {
        LOGE("Tried to initialize a buffer with nullptr ptr!!");
    }
}

/**
 * Camera3Buffer
 *
 * Constructor for buffers allocated using mmap
 *
 * \param w [IN] width
 * \param h [IN] height
 * \param s [IN] stride
 * \param fd [IN] File descriptor to map
 * \param dmaBufFd [IN] File descriptor for dmabuf
 * \param length [IN] amount of data to map
 * \param v4l2fmt [IN] Pixel format in V4L2 enum
 * \param offset [IN] offset from the begining of the file (mmap param)
 * \param prot [IN] memory protection (mmap param)
 * \param flags [IN] flags (mmap param)
 *
 * Success of the mmap can be queried by checking the size of the resulting
 * buffer
 */
Camera3Buffer::Camera3Buffer(int w, int h, int s, int fd, int dmaBufFd, int length,
                           int v4l2fmt, int offset, int prot, int flags):
        mWidth(w),
        mHeight(h),
        mSize(length),
        mFormat(0),
        mV4L2Fmt(v4l2fmt),
        mStride(s),
        mUsage(0),
        mInit(false),
        mLocked(false),
        mRegistered(false),
        mType(BUF_TYPE_MMAP),
        mGbmBufferManager(nullptr),
        mOwner(nullptr),
        mDataPtr(nullptr),
        mCameraId(-1),
        mDmaBufFd(dmaBufFd)
{
    LOG1("%s create mmap camera buffer %p", __FUNCTION__, this);
    mLocked = true;
    mInit = true;

    mDataPtr = mmap(nullptr, length, prot, flags, fd, offset);
    if (CC_UNLIKELY(mDataPtr == MAP_FAILED)) {
        LOGE("Failed to MMAP the buffer %s", strerror(errno));
        mDataPtr = nullptr;
        return;
    }
    LOG1("mmaped address for %p length %d", mDataPtr, mSize);
}

/**
 * init
 *
 * Constructor to wrap a camera3_stream_buffer
 *
 * \param aBuffer [IN] camera3_stream_buffer buffer
 */
icamera::status_t Camera3Buffer::init(const camera3_stream_buffer *aBuffer, int cameraId)
{
    mType = BUF_TYPE_HANDLE;
    mGbmBufferManager = cros::CameraBufferManager::GetInstance();
    mHandle = *aBuffer->buffer;
    mHandlePtr = aBuffer->buffer;
    mWidth = aBuffer->stream->width;
    mHeight = aBuffer->stream->height;
    mFormat = aBuffer->stream->format;
    mV4L2Fmt = mGbmBufferManager->GetV4L2PixelFormat(mHandle);
    // Use actual width from platform native handle for stride
    mStride = mGbmBufferManager->GetPlaneStride(*aBuffer->buffer, 0);
    mSize = 0;
    mLocked = false;
    mOwner = static_cast<Camera3Stream*>(aBuffer->stream->priv);
#if 0
    mUsage = mOwner->usage();
#else
    mUsage = 0x20003; // TODO: hard code the usage, fix me later.
#endif
    mInit = true;
    mDataPtr = nullptr;
    mUserBuffer = *aBuffer;
    mUserBuffer.release_fence = -1;
    mCameraId = cameraId;
    LOG2("@%s, mHandle:%p, mFormat:%d, mWidth:%d, mHeight:%d, mStride:%d",
        __FUNCTION__, mHandle, mFormat, mWidth, mHeight, mStride);

    if (mHandle == nullptr) {
        LOGE("@%s: invalid buffer handle", __FUNCTION__);
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
        return BAD_VALUE;
    }

    int ret = registerBuffer();
    if (ret) {
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
        return UNKNOWN_ERROR;
    }

    /* TODO: add some consistency checks here and return an error */
    return icamera::OK;
}

icamera::status_t Camera3Buffer::init(const camera3_stream_t* stream,
                            buffer_handle_t handle,
                            int cameraId)
{
    mType = BUF_TYPE_HANDLE;
    mGbmBufferManager = cros::CameraBufferManager::GetInstance();
    mHandle = handle;
    mWidth = stream->width;
    mHeight = stream->height;
    mFormat = stream->format;
    mV4L2Fmt = mGbmBufferManager->GetV4L2PixelFormat(mHandle);
    // Use actual width from platform native handle for stride
    mStride = mGbmBufferManager->GetPlaneStride(handle, 0);
    mSize = 0;
    mLocked = false;
    mOwner = nullptr;
    mUsage = stream->usage;
    mInit = true;
    mDataPtr = nullptr;
    mCameraId = cameraId;
    LOG2("@%s, mHandle:%p, mFormat:%d, mWidth:%d, mHeight:%d, mStride:%d",
        __FUNCTION__, mHandle, mFormat, mWidth, mHeight, mStride);

    return icamera::OK;
}

icamera::status_t Camera3Buffer::deinit()
{
    return deregisterBuffer();
}

Camera3Buffer::~Camera3Buffer()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (mInit) {
        switch(mType) {
        case BUF_TYPE_MALLOC:
            free(mDataPtr);
            mDataPtr = nullptr;
            break;
        case BUF_TYPE_MMAP:
            if (mDataPtr != nullptr)
                munmap(mDataPtr, mSize);
            mDataPtr = nullptr;
            mSize = 0;
            close(mDmaBufFd);
            break;
        case BUF_TYPE_HANDLE:
            // Allocated by the HAL
            if (!(mUserBuffer.stream)) {
                LOG1("release internal buffer");
                mGbmBufferManager->Free(mHandle);
            }
            break;
        default:
            break;
        }
    }
    LOG1("%s destroying buf %p", __FUNCTION__, this);
}

icamera::status_t Camera3Buffer::waitOnAcquireFence()
{
    const int WAIT_TIME_OUT_MS = 300;
    const int BUFFER_READY = -1;

    if (mUserBuffer.acquire_fence != BUFFER_READY) {
        LOG2("%s: Fence in HAL is %d", __FUNCTION__, mUserBuffer.acquire_fence);
        int ret = sync_wait(mUserBuffer.acquire_fence, WAIT_TIME_OUT_MS);
        if (ret) {
            mUserBuffer.release_fence = mUserBuffer.acquire_fence;
            mUserBuffer.acquire_fence = -1;
            mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
            LOGE("Buffer sync_wait fail!");
            return TIMED_OUT;
        } else {
            close(mUserBuffer.acquire_fence);
        }
        mUserBuffer.acquire_fence = BUFFER_READY;
    }

    return icamera::OK;
}

/**
 * getFence
 *
 * return the fecne to request result
 */
icamera::status_t Camera3Buffer::getFence(camera3_stream_buffer* buf)
{
    if (!buf)
        return BAD_VALUE;

    buf->acquire_fence = mUserBuffer.acquire_fence;
    buf->release_fence = mUserBuffer.release_fence;

    return icamera::OK;
}

icamera::status_t Camera3Buffer::registerBuffer()
{
    int ret = mGbmBufferManager->Register(mHandle);
    if (ret) {
        LOGE("@%s: call Register fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
        return UNKNOWN_ERROR;
    }

    mRegistered = true;
    return icamera::OK;
}

icamera::status_t Camera3Buffer::deregisterBuffer()
{
    if (mRegistered) {
        int ret = mGbmBufferManager->Deregister(mHandle);
        if (ret) {
            LOGE("@%s: call Deregister fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
            return UNKNOWN_ERROR;
        }
        mRegistered = false;
    }

    return icamera::OK;
}

/**
 * lock
 *
 * lock the gralloc buffer with specified flags
 *
 * \param aBuffer [IN] int flags
 */
icamera::status_t Camera3Buffer::lock(int flags)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    mDataPtr = nullptr;
    mSize = 0;
    int ret = 0;
    uint32_t planeNum = mGbmBufferManager->GetNumPlanes(mHandle);
    LOG2("@%s, planeNum:%d, mHandle:%p, mFormat:%d", __FUNCTION__, planeNum, mHandle, mFormat);

    if (planeNum == 1) {
        void* data = nullptr;
        ret = (mFormat == HAL_PIXEL_FORMAT_BLOB)
                ? mGbmBufferManager->Lock(mHandle, 0, 0, 0, mStride, 1, &data)
                : mGbmBufferManager->Lock(mHandle, 0, 0, 0, mWidth, mHeight, &data);
        if (ret) {
            LOGE("@%s: call Lock fail, mHandle:%p", __FUNCTION__, mHandle);
            return UNKNOWN_ERROR;
        }
        mDataPtr = data;
    } else if (planeNum > 1) {
        struct android_ycbcr ycbrData;
        ret = mGbmBufferManager->LockYCbCr(mHandle, 0, 0, 0, mWidth, mHeight, &ycbrData);
        if (ret) {
            LOGE("@%s: call LockYCbCr fail, mHandle:%p", __FUNCTION__, mHandle);
            return UNKNOWN_ERROR;
        }
        mDataPtr = ycbrData.y;
    } else {
        LOGE("ERROR @%s: planeNum is 0", __FUNCTION__);
        return UNKNOWN_ERROR;
    }
    if (ret) {
        LOGE("ERROR @%s: Failed to lock buffer! %d", __FUNCTION__, ret);
        return UNKNOWN_ERROR;
    }

    for (uint32_t i = 0; i < planeNum; i++) {
        mSize += mGbmBufferManager->GetPlaneSize(mHandle, i);
    }
    LOG2("@%s, mDataPtr:%p, mSize:%d", __FUNCTION__, mDataPtr, mSize);
    if (!mSize) {
        LOGE("ERROR @%s: Failed to GetPlaneSize, it's 0", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    mLocked = true;

    return icamera::OK;
}

icamera::status_t Camera3Buffer::lock()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    icamera::status_t status;
    int lockMode;

    if (!mInit) {
        LOGE("@%s: Error: Cannot lock now this buffer, not initialized", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (mType != BUF_TYPE_HANDLE) {
         mLocked = true;
         return icamera::OK;
    }

    if (mLocked) {
        LOGE("@%s: Error: Cannot lock buffer from stream(%p), already locked",
             __FUNCTION__,mOwner);
        return INVALID_OPERATION;
    }

    lockMode = mUsage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK |
        GRALLOC_USAGE_HW_CAMERA_MASK);
    if (!lockMode) {
        LOGW("@%s:trying to lock a buffer with no flags", __FUNCTION__);
        return INVALID_OPERATION;
    }

    status = lock(lockMode);
    if (status != icamera::OK) {
        mUserBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
    }

    return status;
}

icamera::status_t Camera3Buffer::unlock()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    if (mLocked && mType != BUF_TYPE_HANDLE) {
         mLocked = false;
         return icamera::OK;
    }

    if (mLocked) {
        LOG2("@%s, mHandle:%p, mFormat:%d", __FUNCTION__, mHandle, mFormat);
        int ret = mGbmBufferManager->Unlock(mHandle);
        if (ret) {
            LOGE("@%s: call Unlock fail, mHandle:%p, ret:%d", __FUNCTION__, mHandle, ret);
            return UNKNOWN_ERROR;
        }

        mLocked = false;
        return icamera::OK;
    }
    LOGW("@%s:trying to unlock a buffer that is not locked", __FUNCTION__);
    return INVALID_OPERATION;
}

void Camera3Buffer::dump()
{
    if (mInit) {
        LOG1("Buffer dump: handle %p: locked :%d: dataPtr:%p",
            (void*)&mHandle, mLocked, mDataPtr);
    } else {
        LOG1("Buffer dump: Buffer not initialized");
    }
}

void Camera3Buffer::dumpImage(int frameNumber, const int type, int format)
{
    if (CameraDump::isDumpTypeEnable(type))
        dumpImage(mDataPtr, frameNumber, mSize, mWidth, mHeight, format);
}

void Camera3Buffer::dumpImage(const void *data, int frameNumber,
                              const int size, int width, int height,
                              int format) const
{
#ifdef DUMP_IMAGE
    static unsigned int count = 0;
    count++;

    std::string fileName(gDumpPath);
    const char *extName = CameraUtils::format2string(format);
    fileName += "dump_" + std::to_string(width) +"x" + std::to_string(height)
                             + "_frame#" + std::to_string(count)
                             + "_req#" + std::to_string(frameNumber)
                             + "." + extName;
    LOG2("%s filename is %s", __FUNCTION__, fileName.data());

    FILE *fp = fopen (fileName.data(), "w+");
    if (fp == nullptr) {
        LOGE("open file failed");
        return;
    }
    LOG1("Begin write image %s", fileName.data());

    if ((fwrite(data, size, 1, fp)) != 1)
        LOGW("Error or short count writing %d bytes to %s", size, fileName.data());
    fclose (fp);
#endif
}

int Camera3Buffer::v4L2Fmt2GFXFmt(int v4l2Fmt)
{
    int gfxFmt = -1;

    switch (v4l2Fmt) {
    case V4L2_PIX_FMT_JPEG:
        gfxFmt = HAL_PIXEL_FORMAT_BLOB;
        break;
    case V4L2_PIX_FMT_SBGGR8:
    case V4L2_PIX_FMT_SRGGB8:
    case V4L2_PIX_FMT_SGRBG8:
    case V4L2_PIX_FMT_SRGGB10:
    case V4L2_PIX_FMT_SGRBG10:
    case V4L2_PIX_FMT_SGRBG12:
    case V4L2_PIX_FMT_SBGGR10:
#ifdef V4L2_PIX_FMT_SBGGR10P
    case V4L2_PIX_FMT_SBGGR10P:
#endif
#ifdef V4L2_PIX_FMT_SGBRG10P
    case V4L2_PIX_FMT_SGBRG10P:
#endif
#ifdef V4L2_PIX_FMT_SGRBG10P
    case V4L2_PIX_FMT_SGRBG10P:
#endif
#ifdef V4L2_PIX_FMT_SRGGB10P
    case V4L2_PIX_FMT_SRGGB10P:
#endif
    case V4L2_PIX_FMT_SBGGR12:
    case V4L2_PIX_FMT_SGBRG12:
    case V4L2_PIX_FMT_SRGGB12:
#ifdef V4L2_PIX_FMT_SGRBG12V32
    case V4L2_PIX_FMT_SGRBG12V32:
#endif
#ifdef V4L2_PIX_FMT_CIO2_SRGGB10
    case V4L2_PIX_FMT_CIO2_SRGGB10:
#endif
        gfxFmt = HAL_PIXEL_FORMAT_RAW16;
        break;
    case V4L2_PIX_FMT_YVU420:
#ifdef V4L2_PIX_FMT_YUYV420_V32
    case V4L2_PIX_FMT_YUYV420_V32:
#endif
        gfxFmt = HAL_PIXEL_FORMAT_YV12;
        break;
#if 0
    case V4L2_PIX_FMT_IPU3_SBGGR10:
    case V4L2_PIX_FMT_IPU3_SGBRG10:
    case V4L2_PIX_FMT_IPU3_SGRBG10:
    case V4L2_PIX_FMT_IPU3_SRGGB10:
        gfxFmt = HAL_PIXEL_FORMAT_RAW10;
        break;
    case V4L2_META_FMT_IPU3_PARAMS:
    case V4L2_META_FMT_IPU3_STAT_3A:
    case V4L2_META_FMT_IPU3_STAT_DVS:
    case V4L2_META_FMT_IPU3_STAT_LACE:
        gfxFmt = HAL_PIXEL_FORMAT_RAW_OPAQUE;
        break;
    case V4L2_PIX_FMT_NV21:
        gfxFmt = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        break;
    case V4L2_PIX_FMT_NV12:
        gfxFmt = HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL;
        break;
#endif
    case V4L2_PIX_FMT_YUYV:
        gfxFmt = HAL_PIXEL_FORMAT_YCbCr_422_I;
        break;
    default:
        LOGE("%s: no gfx format for v4l2 0x%x, %s!",
             __FUNCTION__, v4l2Fmt,
             CameraUtils::format2string(v4l2Fmt));
        break;
    }

    return gfxFmt;
}

/**
 * Utility methods to allocate Camera3Buffer from HEAP or Gfx memory
 */
namespace MemoryUtils {

/**
 * Allocates the memory needed to store the image described by the parameters
 * passed during construction
 */
std::shared_ptr<Camera3Buffer>
allocateHeapBuffer(int w,
                   int h,
                   int s,
                   int v4l2Fmt,
                   int cameraId,
                   int dataSizeOverride)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    void *dataPtr;

    int dataSize = dataSizeOverride ? dataSizeOverride : CameraUtils::getFrameSize(v4l2Fmt, s, h);
    LOG1("@%s, dataSize:%d", __FUNCTION__, dataSize);

    int ret = posix_memalign(&dataPtr, sysconf(_SC_PAGESIZE), dataSize);
    if (dataPtr == nullptr || ret != 0) {
        LOGE("Could not allocate heap camera buffer of size %d", dataSize);
        return nullptr;
    }

    return std::shared_ptr<Camera3Buffer>(new Camera3Buffer(w, h, s, v4l2Fmt, dataPtr, cameraId, dataSizeOverride));
}

/**
 * Allocates internal GBM buffer
 */
std::shared_ptr<Camera3Buffer>
allocateHandleBuffer(int w,
                     int h,
                     int gfxFmt,
                     int usage,
                     int cameraId)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    cros::CameraBufferManager* bufManager = cros::CameraBufferManager::GetInstance();
    buffer_handle_t handle;
    uint32_t stride = 0;

    LOG1("%s, [wxh] = [%dx%d], format 0x%x, usage 0x%x",
          __FUNCTION__, w, h, gfxFmt, usage);
    int ret = bufManager->Allocate(w, h, gfxFmt, usage, cros::GRALLOC, &handle, &stride);
    if (ret != 0) {
        LOGE("Allocate handle failed! %d", ret);
        return nullptr;
    }

    std::shared_ptr<Camera3Buffer> buffer(new Camera3Buffer());
    camera3_stream_t stream{};
    stream.width = w;
    stream.height = h;
    stream.format = gfxFmt;
    stream.usage = usage;
    ret = buffer->init(&stream, handle, cameraId);
    if (ret != icamera::OK) {
        // buffer handle will free in Camera3Buffer destructure function
        return nullptr;
    }

    return buffer;
}

}

} // namespace camera3
