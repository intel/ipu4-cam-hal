/*
 * Copyright (C) 2015-2016 Intel Corporation
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
#define LOG_TAG "GfxGen"

#include "Gfx.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"
#include <hardware/hardware.h>
#include <ufo/graphics.h>
#include <ufo/gralloc.h>
#include <pthread.h>

using namespace icamera;
namespace android {

#define BYTES_PER_PIXEL_YCbCr_422_I 2
#define BYTES_PER_PIXEL_RAW16 2

static pthread_once_t grallocIsInitialized = PTHREAD_ONCE_INIT;
static gralloc_module_t *pGralloc = NULL;

extern "C" void initGrallocModule()
{
    int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (hw_module_t const**)&pGralloc);
    if (ret) {
        LOGE("@%s, call hw_get_module fail, ret=%d", __FUNCTION__, ret);
    }
}

static bool getBufferInfo(buffer_handle_t *handle, intel_ufo_buffer_details_t *info)
{
    if (NULL == handle || NULL == info) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return false;
    }

    CLEAR(*info);
    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc) {
        return false;
    }
#ifdef INTEL_UFO_GRALLOC_BUFFER_DETAILS_LEVEL
    info->magic = sizeof(*info);
#endif
    int ret = pGralloc->perform(pGralloc, INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO, (buffer_handle_t)*handle, info);
    if (ret) {
        LOGE("@%s, call perform fail", __FUNCTION__);
        return false;
    }

    return true;
}

int getNativeHandleWidth(buffer_handle_t *handle)
{
    intel_ufo_buffer_details_t info;
    if (getBufferInfo(handle, &info)) {
        LOG2("@%s, w:%d, h:%d, size:%d, f:%d, stride:%d", __FUNCTION__, info.width, info.height, info.size, info.format, info.pitch);
        return info.width;
    }
    return 0;
}

int getNativeHandleIonFd(buffer_handle_t *handle)
{
    UNUSED(handle);
    return -1;
}

int getNativeHandleDmaBufFd(buffer_handle_t *handle)
{
    int prime = -1;

    if (NULL == handle) {
        LOGE("Passed handle is NULL");
        return -1;
     }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc) {
        return -1;
    }

    int ret = pGralloc->perform(pGralloc, INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_PRIME, (buffer_handle_t)*handle, &prime);
    if (ret) {
        LOGE("Call perform get bo prime fail");
        return -1;
    }

    return prime;
}


/**
 * getNativeHandleSize
 *
 * \param handle the buffer handle
 * \return size of the allocated buffer, -1 if unknown
 */
int getNativeHandleSize(buffer_handle_t *handle)
{
    intel_ufo_buffer_details_t info;
    if (getBufferInfo(handle, &info)) {
        return info.size;
    }

    LOGE("Couldn't get buffer info");
    return -1;
}

int getNativeHandleStride(buffer_handle_t *handle)
{
    intel_ufo_buffer_details_t info;
    if (getBufferInfo(handle, &info)) {
        switch (info.format) {
        case HAL_PIXEL_FORMAT_NV12_LINEAR_CAMERA_INTEL:
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
            LOG2("@%s, w:%d, h:%d, size:%d, f:%d, pitch:%d, stride:%d", __FUNCTION__,
                  info.width, info.height, info.size, info.format, info.pitch, ALIGN_64(info.width));
            // pitch stands for offset to the start of next line
            return info.pitch;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            LOG2("@%s, w:%d, h:%d, size:%d, f:%d, pitch:%d, stride:%d", __FUNCTION__,
                  info.width, info.height, info.size, info.format, info.pitch, ALIGN_32(info.width));
            //for YUV422I, pitch = width * BYTES_PER_PIXEL_YCbCr_422_I
            return info.pitch / BYTES_PER_PIXEL_YCbCr_422_I;
        case HAL_PIXEL_FORMAT_RAW16:
            LOG2("@%s, w:%d, h:%d, size:%d, f:%d, pitch:%d", __FUNCTION__,
                  info.width, info.height, info.size, info.format, info.pitch);
            // RAW16 stride is defined to be pixel stride, not byte stride
            return info.pitch / BYTES_PER_PIXEL_RAW16;
        case HAL_PIXEL_FORMAT_BLOB:
            LOG2("@%s, w:%d, h:%d, size:%d, f:%d, pitch:%d", __FUNCTION__,
                  info.width, info.height, info.size, info.format, info.pitch);
            return info.pitch;
        default:
            LOGE("@%s,unknown format for GEN w:%d, h:%d, size:%d, f:%d, pitch:%d", __FUNCTION__,
                  info.width, info.height, info.size, info.format, info.pitch);
            break;
        }
    }
    return 0;
}

int setBufferColorRange(buffer_handle_t *handle, bool fullRange)
{
    if (NULL == handle || NULL == pGralloc) {
        return 0;
    }

    uint32_t colorRange = INTEL_UFO_BUFFER_COLOR_RANGE_FULL;
    if (!fullRange) {
        colorRange = INTEL_UFO_BUFFER_COLOR_RANGE_LIMITED;
    }

    int ret = pGralloc->perform(pGralloc, INTEL_UFO_GRALLOC_MODULE_PERFORM_SET_BO_COLOR_RANGE, (buffer_handle_t)*handle, colorRange);
    if (ret) {
        LOGE("@%s, call perform fail", __FUNCTION__);
    }

    return 0;
}

/**
 * CameraBuffer
 *
 * Constructor for buffers allocated using MemoryUtils::allocateGraphicBuffer
 *
 * \param w [IN] width
 * \param h [IN] height
 * \param s [IN] stride
 * \param format [IN] Gfx format
 * \param gfxBuf [IN] gralloc buffer
 * \param ptr [IN] mapperPointer
 */
CameraGfxBuffer::CameraGfxBuffer(int w,
                                 int h,
                                 int s,
                                 int format,
                                 int v4l2Fmt,
                                 GraphicBuffer *gfxBuf,
                                 void * ptr,
                                 int usage) :
    mWidth(w)
    ,mHeight(h)
    ,mSize(0)
    ,mFormat(format)
    ,mV4L2Fmt(v4l2Fmt)
    ,mStride(s)
    ,mGfxBuffer(gfxBuf)
    ,mDataPtr(ptr)
    ,mInuse(false)
{
    LOG1("@%s", __FUNCTION__);

    if (gfxBuf && ptr) {
        mSize = getNativeHandleSize(getBufferHandle());
        LOG1("Gfx buffer alloc size %d", mSize);
    } else
        LOGE("%s: NULL input pointer!", __FUNCTION__);
}

CameraGfxBuffer::~CameraGfxBuffer()
{
    LOG1("@%s", __FUNCTION__);
    mGfxBuffer->unlock();
    mGfxBuffer->decStrong(this);
    mGfxBuffer = NULL;

    LOG1("%s destroying buf %p", __FUNCTION__, this);
}

/**
 * This function allocates memory from graphics and returns it wrapped into
 * a sp<CameraBuffer>.
 *
 * \param[in] w width of the image buffer
 * \param[in] h height of the image buffer
 * \param[in] gfxFmt graphics format of the image buffer
 * \param[in] usage the gralloc flags which get given for CameraBuffer object
 *            which uses them for implementation defined format queries. Also
 *            added on top of createUsage flags when buffer is created.
 * \param[in] lockUsage the set of gralloc flags that are used during buffer
 *            locking.
 * \param[in] createUsage the set of gralloc flags which are used during
 *            buffer creation. Note that "usage" flags are also added during
 *            buffer creation.
 * \return the sp<CameraBuffer> object
 */
CameraGfxBuffer *allocateGraphicBuffer(int w, int h, int gfxFmt,
                                       int v4l2Fmt,
                                       uint32_t usage,
                                       uint32_t lockUsage,
                                       uint32_t createUsage)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = OK;

    void *mapperPointer = NULL;

    LOG1("%s with these properties: (%dx%d) gfx format %d usage %x lockUsage %x createUsage %x", __FUNCTION__,
         w, h, gfxFmt, usage, lockUsage, createUsage);
    sp<GraphicBuffer> gfxBuffer =
            new GraphicBuffer(w, h, gfxFmt, createUsage | usage);

    if (gfxBuffer == NULL || gfxBuffer->initCheck() != NO_ERROR) {
        LOGE("No memory to allocate graphic buffer");
        return NULL;
    }

    ANativeWindowBuffer *nativeWinBuffer = gfxBuffer->getNativeBuffer();

    status = gfxBuffer->lock(lockUsage, &mapperPointer);
    if (status != NO_ERROR) {
        LOGE("@%s: Failed to lock GraphicBuffer! %d", __FUNCTION__, status);
        return NULL;
    }

    if (w != nativeWinBuffer->stride) {
        LOG1("%s: potential bpl problem requested %d, Gfx requries %d",
            __FUNCTION__, w, nativeWinBuffer->stride);
    } else {
        LOG1("%s bpl from Gfx is %d", __FUNCTION__, nativeWinBuffer->stride);
    }


    CameraGfxBuffer *buf =
        new CameraGfxBuffer(w, h, nativeWinBuffer->stride, gfxFmt, v4l2Fmt, &*gfxBuffer,
                         mapperPointer, usage);
    gfxBuffer->incStrong(buf);

    return buf;
}


GenImageConvert::GenImageConvert()
    : miVPCtxValid(false)
{
    // Width and height are not important for us, hence the 1, 1
    if (iVP_create_context(&miVPCtx, 1, 1, 0) == IVP_STATUS_SUCCESS) {
        miVPCtxValid = true;
    } else {
        ALOGE("Failed to create iVP context");
    }
}

GenImageConvert::~GenImageConvert()
{
    if (miVPCtxValid)
        iVP_destroy_context(&miVPCtx);
}

status_t GenImageConvert::downScalingAndColorConversion(BufferPackage &bp)
{
    LOG2("%s srcBuf: format()=0x%x, width=%d, height=%d; "
         "destBuf: format()=0x%x, width=%d, height=%d",
         __FUNCTION__,
         bp.nativeHalBuffer.s.format, bp.nativeHalBuffer.s.width, bp.nativeHalBuffer.s.height,
         bp.nativeWinBuffer.s.format, bp.nativeWinBuffer.s.width, bp.nativeWinBuffer.s.height);

    //Clear the destination buffer
    int size = getNativeHandleSize(bp.nativeWinBuffHandle);
    memset(bp.nativeWinBuffer.addr, 0, size);
    /*
     * Use iVP to do both downscale and color conversion if needed
     * and use iVP do copy when src and dest buffer properties are identical
     */
    if (iVPColorConversion(bp) != NO_ERROR) {
        LOGE("%s: not implement for color conversion 0x%x -> 0x%x!",
                __FUNCTION__, bp.nativeHalBuffer.s.format, bp.nativeWinBuffer.s.format);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t GenImageConvert::cameraBuffer2iVPLayer(icamera::camera_buffer_t cameraBuffer,
                                                buffer_handle_t *buffHandle,
                                                iVP_layer_t *iVPLayer, int left, int top)
{
    /*
     * set srcRect and destRect with the cropped area
     *         cameraBuffer->width()
     *   _______________________________
     *   |    |                         |
     *   |    |top                      |
     *   |____|____________________     |
     *   |left|                    |    |
     *   |    |                    |    |
     *   |    |                    |    |
     *   |    |                    |    | cameraBuffer->height()
     *   |    |                    |    |
     *   |    |                    |    |
     *   |    |                    |    |
     *   |    |                    |left|
     *   |    |____________________|____|
     *   |                         |    |
     *   |                      top|    |
     *   |_________________________|____|
     *
     */
    iVPLayer->srcRect->left = iVPLayer->destRect->left = left;
    iVPLayer->srcRect->top = iVPLayer->destRect->top = top;
    iVPLayer->srcRect->width = iVPLayer->destRect->width = cameraBuffer.s.width - 2 * left;
    iVPLayer->srcRect->height = iVPLayer->destRect->height = cameraBuffer.s.height - 2 * top;
    if (left != 0 || top != 0)
        LOG2("buffersize (%dx%d, %dx%d)", cameraBuffer.s.width, cameraBuffer.s.height, left, top);
    iVPLayer->bufferType = IVP_GRALLOC_HANDLE;
    buffer_handle_t *gralloc_handle = buffHandle;
    if (gralloc_handle == NULL) {
        LOGE("Sending non-gralloc buffer to iVP that does not work, aborting color conversion");
        return INVALID_OPERATION;
    }
    iVPLayer->gralloc_handle = *gralloc_handle;

    return NO_ERROR;
}

status_t GenImageConvert::iVPColorConversion(BufferPackage &bp)
{
    if (!miVPCtxValid)
        return UNKNOWN_ERROR;

    iVP_rect_t srcSrcRect, srcDstRect, dstSrcRect, dstDstRect;
    iVP_layer_t src, dst;
    CLEAR(src);
    CLEAR(dst);
    src.srcRect = &srcSrcRect;
    src.destRect = &srcDstRect;
    dst.srcRect = &dstSrcRect;
    dst.destRect = &dstDstRect;

    float dstRatio = (float)bp.nativeWinBuffer.s.width / bp.nativeWinBuffer.s.height;
    float srcRatio = (float)bp.nativeHalBuffer.s.width / bp.nativeHalBuffer.s.height;
    int left = 0;
    int top = 0;

    if (dstRatio > srcRatio) {
        top = (bp.nativeHalBuffer.s.height - bp.nativeHalBuffer.s.width / dstRatio) / 2;
    } else {
        left = (bp.nativeWinBuffer.s.width - dstRatio * bp.nativeWinBuffer.s.height) / 2;
    }
    status_t status = cameraBuffer2iVPLayer(bp.nativeHalBuffer, bp.nativeHalBuffHandle, &src, left, top);
    if (status != NO_ERROR)
        return status;
    status = cameraBuffer2iVPLayer(bp.nativeWinBuffer, bp.nativeWinBuffHandle, &dst);
    if (status != NO_ERROR)
        return status;
    // Src dst rect is the operations dst rect
    srcDstRect = dstDstRect;

    iVP_status iVPstatus = iVP_exec(&miVPCtx, &src, NULL, 0, &dst, true);
    if (iVPstatus != IVP_STATUS_SUCCESS)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

} // namespace android
