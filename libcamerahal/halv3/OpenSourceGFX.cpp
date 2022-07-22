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
#define LOG_TAG "OpenSourceGFX"

#include "Gfx.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"
#include <hardware/hardware.h>
#include <hardware/gralloc1.h>
#include <pthread.h>
#include <hardware/camera3.h>


#undef LOG1
#undef LOG2
#undef LOGR
#undef LOG3A
#undef LOGXML
#undef LOGE
#undef LOGI
#undef LOGD
#undef LOGW
#undef LOGV

#define LOG1(...) icamera::__camera_hal_log(icamera::gLogLevel & icamera::CAMERA_DEBUG_LOG_LEVEL1, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOG2(...) icamera::__camera_hal_log(icamera::gLogLevel & icamera::CAMERA_DEBUG_LOG_LEVEL2, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGR(...) icamera::__camera_hal_log(icamera::gLogLevel & icamera::CAMERA_DEBUG_LOG_REQ_STATE, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOG3A(...) icamera::__camera_hal_log(icamera::gLogLevel & icamera::CAMERA_DEBUG_LOG_AIQ, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGXML(...) icamera::__camera_hal_log(icamera::gLogLevel & icamera::CAMERA_DEBUG_LOG_XML, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#define LOGE(...) icamera::__camera_hal_log(true, ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) icamera::__camera_hal_log(true, ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) icamera::__camera_hal_log(true, ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) icamera::__camera_hal_log(true, ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGV(...) icamera::__camera_hal_log(true, ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)


using namespace icamera;
namespace android {

static pthread_once_t grallocIsInitialized = PTHREAD_ONCE_INIT;
static hw_module_t const* pGralloc = NULL;
static gralloc1_device_t *pGralloc1Dev = NULL;
static GRALLOC1_PFN_GET_STRIDE pGetStrideFn = NULL;
static GRALLOC1_PFN_GET_DIMENSIONS pGetDimensionsFn = NULL;
static GRALLOC1_PFN_GET_BACKING_STORE pGetBackingStoreFn = NULL;
static GRALLOC1_PFN_LOCK pLockFn = NULL;
static GRALLOC1_PFN_LOCK_FLEX pLockFlexFn = NULL;
static GRALLOC1_PFN_UNLOCK pUnlockFn = NULL;

extern "C" void initGrallocModule()
{
    int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (hw_module_t const**)&pGralloc);
    if (ret) {
        LOGE("@%s, call hw_get_module fail, ret=%d", __FUNCTION__, ret);
        return;
    }

    struct hw_device_t *pDev;
    ret = pGralloc->methods->open(pGralloc, GRALLOC_HARDWARE_MODULE_ID, &pDev);
    if (ret) {
        LOGE("@%s, call to Gralloc->open hw device failed, ret=%d", __FUNCTION__, ret);
        return;
    }
    pGralloc1Dev = (gralloc1_device_t *)pDev;

    pGetStrideFn = (GRALLOC1_PFN_GET_STRIDE)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_GET_STRIDE);

    pGetDimensionsFn = (GRALLOC1_PFN_GET_DIMENSIONS)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_GET_DIMENSIONS);

    pGetBackingStoreFn = (GRALLOC1_PFN_GET_BACKING_STORE)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_GET_BACKING_STORE);

    pLockFn = (GRALLOC1_PFN_LOCK)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_LOCK);

    pLockFlexFn = (GRALLOC1_PFN_LOCK_FLEX)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_LOCK_FLEX);

    pUnlockFn = (GRALLOC1_PFN_UNLOCK)\
             pGralloc1Dev->getFunction(pGralloc1Dev, GRALLOC1_FUNCTION_UNLOCK);
}

int getNativeHandleWidth(buffer_handle_t *handle)
{
    LOGE("%s", __FUNCTION__);
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetDimensionsFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    uint32_t width = 0, height = 0;
    int ret = pGetDimensionsFn(pGralloc1Dev, (buffer_handle_t)*handle, &width, &height);
    LOGE("@%s, ret: %d, width:%d, height:%d", __FUNCTION__, ret, width, height);
    return (0 == ret) ? width : -1;
}

int getNativeHandleDmaBufFd(buffer_handle_t *handle)
{
    gralloc1_backing_store_t fd = -1;

    if (NULL == handle) {
        LOGE("Passed handle is NULL");
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetBackingStoreFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    int ret = pGetBackingStoreFn(pGralloc1Dev, (buffer_handle_t)*handle, &fd);
    if (ret) {
       LOGE("GetBackingStore failed");
       return -1;
    }

    return fd;
}

/**
 * getNativeHandleSize
 *
 * \param handle the buffer handle
 * \return size of the allocated buffer, -1 if unknown
 */

#define PAGE_SIZE 4096
int getNativeHandleSize(buffer_handle_t *handle, int halFormat)
{
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetDimensionsFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    uint32_t width = 0, height = 0;
    int size = 0;
    int ret = pGetDimensionsFn(pGralloc1Dev, (buffer_handle_t)*handle, &width, &height);
    int v4l2Format = HalFormat2V4L2Format(halFormat);
    int alignedBpl = CameraUtils::getStride(v4l2Format, width);
    int bufferHeight = CameraUtils::isPlanarFormat(v4l2Format) ? (height * CameraUtils::getBpp(v4l2Format) / 8) : height;

    size = alignedBpl * bufferHeight;
    if (alignedBpl * bufferHeight % PAGE_SIZE != 0)
        size = (alignedBpl * bufferHeight + PAGE_SIZE)/PAGE_SIZE * PAGE_SIZE;

    LOG1("%s: get %p buffer %dx%d, v4l2fmt=0x%x, size = %d",
        __FUNCTION__, handle, width, height, v4l2Format, size);

    return size;
}

int getNativeHandleStride(buffer_handle_t *handle)
{
    LOG1("%s", __FUNCTION__);
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetStrideFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    uint32_t stride = 0;
    int ret = pGetStrideFn(pGralloc1Dev, (buffer_handle_t)*handle, &stride);
    LOG1("@%s, ret: %d, stride:%d", __FUNCTION__, ret, stride);
    return (0 == ret) ? stride : -1;
}

int HalFormat2V4L2Format(int HalFormat) {
    int format = V4L2_PIX_FMT_NV12;

    switch (HalFormat) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            format = V4L2_PIX_FMT_NV12;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            format = V4L2_PIX_FMT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_YV12:
            format = V4L2_PIX_FMT_YVU420;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            format = V4L2_PIX_FMT_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            format = V4L2_PIX_FMT_NV16;
            break;
        default:
            format = V4L2_PIX_FMT_NV12;
            ALOGE("%s: Unsupported HAL format: %d, use default V4L2 format",
                __func__, HalFormat);
    }

    ALOGD("%s: V4L2 format = 0x%x", __func__, format);
    return format;
}

int lockBuffer(buffer_handle_t *handle, int format, uint64_t producerUsage, uint64_t consumerUsage,
               int width, int height, void **pAddr, int acquireFence)
{
    LOG1("%s", __FUNCTION__);
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    struct android_flex_layout outFlexLayout;
    gralloc1_rect_t rect{};
    rect.left = 0;
    rect.top = 0;
    rect.width = width;
    rect.height = height;

    int error = -1;
    if (HAL_PIXEL_FORMAT_YCbCr_420_888 == format || HAL_PIXEL_FORMAT_YV12 ||
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED == format) {
        if (pLockFlexFn) {
            error = pLockFlexFn(pGralloc1Dev, (buffer_handle_t)*handle, producerUsage,
                            consumerUsage, &rect, &outFlexLayout, -1);
            // Y Component
            *pAddr = outFlexLayout.planes[0].top_left;
            LOG1("%s - num_planes: %d", __FUNCTION__, outFlexLayout.num_planes);
        }
    } else if (pLockFn) {
        error = pLockFn(pGralloc1Dev, (buffer_handle_t)*handle,
                static_cast<gralloc1_producer_usage_t>(producerUsage),
                static_cast<gralloc1_consumer_usage_t>(consumerUsage),
                &rect, pAddr, acquireFence);
    }
    LOG1("@%s, error: %d", __FUNCTION__, error);
    return (GRALLOC1_ERROR_NONE == error) ? NO_ERROR : -1;
}

int unlockBuffer(buffer_handle_t *handle, int *pOutReleaseFence)
{
    LOG1("%s", __FUNCTION__);
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pUnlockFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    int releaseFence = 0;
    int ret = pUnlockFn(pGralloc1Dev, (buffer_handle_t)*handle, &releaseFence);
    if (pOutReleaseFence) {
            *pOutReleaseFence = releaseFence;
    }
    LOG1("@%s, ret: %d", __FUNCTION__, ret);
    return (GRALLOC1_ERROR_NONE == ret) ? NO_ERROR : -1;
}

int getNativeHandleIonFd(buffer_handle_t *handle)
{
    UNUSED(handle);
    return -1;
}

int setBufferColorRange(buffer_handle_t *handle, bool fullRange)
{
    UNUSED(handle);
    UNUSED(fullRange);
    return -1;
}


int getNativeHandleDimensions(buffer_handle_t *handle, uint32_t *pWidth,
                              uint32_t *pHeight, uint32_t *pStride)
{
    LOGE("%s", __FUNCTION__);
    if (NULL == handle) {
        LOGE("@%s, passed parameter is NULL", __FUNCTION__);
        return -1;
    }

    if (NULL == pWidth || NULL == pHeight || NULL == pStride) {
        LOGE("@%s, Invalid parameters", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetDimensionsFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    int ret = pGetDimensionsFn(pGralloc1Dev, (buffer_handle_t)*handle, pWidth, pHeight);
    LOGE("@%s, ret: %d, width:%d, height:%d", __FUNCTION__, ret, *pWidth, *pHeight);

    ret = pGetStrideFn(pGralloc1Dev, (buffer_handle_t)*handle, pStride);
    LOG1("@%s, ret: %d, stride:%d", __FUNCTION__, ret, *pStride);
    return ret;
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
                                 GraphicBuffer *gfxBuf,
                                 void * ptr) :
    mWidth(w)
    ,mHeight(h)
    ,mSize(0)
    ,mFormat(format)
    ,mStride(s)
    ,mGfxBuffer(gfxBuf)
    ,mDataPtr(ptr)
    ,mInuse(false)
{
    if (gfxBuf && ptr) {
        mSize = getNativeHandleSize(getBufferHandle(), format);
        LOG1("%s: Gfx buffer alloc size %d", __FUNCTION__, mSize);
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
 * \return the sp<CameraBuffer> object
 */
CameraGfxBuffer *allocateGraphicBuffer(int w, int h, int gfxFmt, uint32_t usage)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = OK;

    void *mapperPointer = NULL;

    LOG1("%s with these properties: (%dx%d) gfx format %d usage %x", __FUNCTION__,
         w, h, gfxFmt, usage);
    sp<GraphicBuffer> gfxBuffer =
            new GraphicBuffer(w, h, gfxFmt, usage);

    if (gfxBuffer == NULL || gfxBuffer->initCheck() != NO_ERROR) {
        LOGE("No memory to allocate graphic buffer");
        return NULL;
    }

    ANativeWindowBuffer *nativeWinBuffer = gfxBuffer->getNativeBuffer();
    status = gfxBuffer->lock(usage, &mapperPointer);
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
        new CameraGfxBuffer(w, h, nativeWinBuffer->stride, gfxFmt, &*gfxBuffer, mapperPointer);
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
         bp.nativeHalBuf->stream->format,
         bp.nativeHalBuf->stream->width,
         bp.nativeHalBuf->stream->height,
         bp.nativeWinBuf->stream->format,
         bp.nativeWinBuf->stream->width,
         bp.nativeWinBuf->stream->height);

    //Clear the destination buffer
    int size = getNativeHandleSize(bp.nativeWinBuf->buffer, bp.nativeWinBuf->stream->format);
    //memset(bp.nativeWinBuffer.addr, 0, size);
    LOG2("win buf 0x%x, win *buf 0x%x", bp.nativeWinBuf->buffer, *(bp.nativeWinBuf->buffer));
    LOG2("hal buf 0x%x, hal *buf 0x%x", bp.nativeHalBuf->buffer, *(bp.nativeHalBuf->buffer));
    LOG2("win buf size = %d", size);
    /*
     * Use iVP to do both downscale and color conversion if needed
     * and use iVP do copy when src and dest buffer properties are identical
     */
    if (iVPColorConversion(bp) != NO_ERROR) {
        LOGE("%s: not implement for color conversion 0x%x -> 0x%x!",
                __FUNCTION__,
                bp.nativeHalBuf->stream->format,
                bp.nativeWinBuf->stream->format);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t GenImageConvert::cameraBuffer2iVPLayer(const camera3_stream_buffer_t* cameraBuffer,
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
    iVPLayer->srcRect->width = iVPLayer->destRect->width = cameraBuffer->stream->width - 2 * left;
    iVPLayer->srcRect->height = iVPLayer->destRect->height = cameraBuffer->stream->height - 2 * top;
    if (left != 0 || top != 0)
        LOG2("buffersize (%dx%d, %dx%d)",
                cameraBuffer->stream->width,
                cameraBuffer->stream->height,
                left,
                top);
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

    float dstRatio =
        (float)bp.nativeWinBuf->stream->width / bp.nativeWinBuf->stream->height;
    float srcRatio =
        (float)bp.nativeHalBuf->stream->width / bp.nativeHalBuf->stream->height;
    int left = 0;
    int top = 0;

    if (dstRatio > srcRatio) {
        top =
            (bp.nativeHalBuf->stream->height - bp.nativeHalBuf->stream->width / dstRatio) / 2;
    } else {
        left =
            (bp.nativeWinBuf->stream->width - dstRatio * bp.nativeWinBuf->stream->height) / 2;
    }

    status_t status = cameraBuffer2iVPLayer(bp.nativeHalBuf, bp.nativeHalBuf->buffer, &src, left, top);
    if (status != NO_ERROR)
        return status;

    status = cameraBuffer2iVPLayer(bp.nativeWinBuf, bp.nativeWinBuf->buffer, &dst);
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

