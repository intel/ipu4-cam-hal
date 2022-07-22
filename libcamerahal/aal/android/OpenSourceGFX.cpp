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
#include "Camera3Format.h"
#ifndef ENABLE_IVP
#include <va/va_android.h>
#include <ufo/graphics.h>
#endif

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

namespace android {

static pthread_once_t grallocIsInitialized = PTHREAD_ONCE_INIT;
static hw_module_t const* pGralloc = nullptr;
static gralloc1_device_t *pGralloc1Dev = nullptr;
static GRALLOC1_PFN_GET_STRIDE pGetStrideFn = nullptr;
static GRALLOC1_PFN_GET_DIMENSIONS pGetDimensionsFn = nullptr;
static GRALLOC1_PFN_GET_BACKING_STORE pGetBackingStoreFn = nullptr;
static GRALLOC1_PFN_LOCK pLockFn = nullptr;
static GRALLOC1_PFN_LOCK_FLEX pLockFlexFn = nullptr;
static GRALLOC1_PFN_UNLOCK pUnlockFn = nullptr;

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
    LOGD("%s", __FUNCTION__);
    if (nullptr == handle) {
        LOGE("@%s, passed parameter is nullptr", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetDimensionsFn) {
        LOGE("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    uint32_t width = 0, height = 0;
    int ret = pGetDimensionsFn(pGralloc1Dev, (buffer_handle_t)*handle, &width, &height);
    LOGD("@%s, ret: %d, width:%d, height:%d", __FUNCTION__, ret, width, height);
    return (0 == ret) ? width : -1;
}

int getNativeHandleDmaBufFd(buffer_handle_t *handle)
{
    gralloc1_backing_store_t fd = -1;

    if (nullptr == handle) {
        LOGD("Passed handle is nullptr");
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
    if (nullptr == handle) {
        LOGE("@%s, passed parameter is nullptr", __FUNCTION__);
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
    int v4l2Format = Camera3Format::HalFormat2V4L2Format(halFormat);
    int alignedBpl = icamera::CameraUtils::getStride(v4l2Format, width);
    int bufferHeight = icamera::CameraUtils::isPlanarFormat(v4l2Format) ?
                       (height * icamera::CameraUtils::getBpp(v4l2Format) / 8) : height;

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
    if (nullptr == handle) {
        LOGE("@%s, passed parameter is nullptr", __FUNCTION__);
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

int lockBuffer(buffer_handle_t *handle, int format, uint64_t producerUsage, uint64_t consumerUsage,
               int width, int height, void **pAddr, int acquireFence)
{
    LOG1("%s", __FUNCTION__);
    if (nullptr == handle) {
        LOGE("@%s, passed parameter is nullptr", __FUNCTION__);
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
    if (nullptr == handle) {
        LOGE("@%s, passed parameter is nullptr", __FUNCTION__);
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
    LOG1("%s", __FUNCTION__);
    if (nullptr == handle) {
        LOG1("@%s, passed parameter is nullptr", __FUNCTION__);
        return -1;
    }

    if (nullptr == pWidth || nullptr == pHeight || nullptr == pStride) {
        LOG1("@%s, Invalid parameters", __FUNCTION__);
        return -1;
    }

    pthread_once(&grallocIsInitialized, initGrallocModule);
    if (!pGralloc || !pGralloc1Dev || !pGetDimensionsFn) {
        LOG1("%s invalid gralloc pointers", __FUNCTION__);
        return -1;
    }

    int ret = pGetDimensionsFn(pGralloc1Dev, (buffer_handle_t)*handle, pWidth, pHeight);
    LOG1("@%s, ret: %d, width:%d, height:%d", __FUNCTION__, ret, *pWidth, *pHeight);

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
        LOGE("%s: nullptr input pointer!", __FUNCTION__);
}

CameraGfxBuffer::~CameraGfxBuffer()
{
    LOG1("@%s", __FUNCTION__);
    mGfxBuffer->unlock();
    mGfxBuffer->decStrong(this);
    mGfxBuffer = nullptr;

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

    void *mapperPointer = nullptr;

    LOG1("%s with these properties: (%dx%d) gfx format %d usage %x", __FUNCTION__,
         w, h, gfxFmt, usage);
    sp<GraphicBuffer> gfxBuffer =
            new GraphicBuffer(w, h, gfxFmt, usage);

    if (gfxBuffer == nullptr || gfxBuffer->initCheck() != NO_ERROR) {
        LOGE("No memory to allocate graphic buffer");
        return nullptr;
    }

    ANativeWindowBuffer *nativeWinBuffer = gfxBuffer->getNativeBuffer();
    status = gfxBuffer->lock(usage, &mapperPointer);
    if (status != NO_ERROR) {
        LOGE("@%s: Failed to lock GraphicBuffer! %d", __FUNCTION__, status);
        return nullptr;
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

#ifndef ENABLE_IVP
VAStatus destroyVideoProcPipeline(struct VideoProcContext *ctx)
{
    VAStatus vaStatus;

    if(ctx->vaDisplay) {
        if (ctx->vaContext != VA_INVALID_ID) {
            vaStatus = vaDestroyContext(ctx->vaDisplay, ctx->vaContext);
            if (vaStatus != VA_STATUS_SUCCESS)
                LOGE("Failed vaDestroyContext ret=%x", vaStatus);
            }
        }

    if (ctx->vaConfig != VA_INVALID_ID) {
    vaStatus = vaDestroyConfig(ctx->vaDisplay, ctx->vaConfig);
    if (vaStatus != VA_STATUS_SUCCESS)
        LOGE("Failed vaDestroyConfig ret=%x\n", vaStatus);
    }

    vaStatus = vaTerminate(ctx->vaDisplay);
    if (vaStatus != VA_STATUS_SUCCESS)
        LOGE("Failed vaTerminate ret = %x.", vaStatus);

    free(ctx);
    return vaStatus;
}

#define CHECK_RET_INIT_VIDEO(ret, str, ctx) \
    if(ret != VA_STATUS_SUCCESS)            \
    {                                       \
        LOGE("%s ret=%#x.\n", str, ret);    \
        destroyVideoProcPipeline(ctx);      \
        return ret;                         \
    }

#define CHECK_RET_VIDEO_RENDER(ret, str, ctx)           \
    if(ret != VA_STATUS_SUCCESS)                        \
    {                                                   \
        LOGE("%s ret=%#x.\n", str, ret);                \
        vaDestroyBuffer(ctx->vaDisplay, ctx->srcBuffer);\
        VideoProcDestroySurfaces(ctx);                  \
        return ret;                                     \
    }

VAStatus initVideoProcPipeline(struct VideoProcContext *ctx,
                    unsigned int width,
                    unsigned int height,
                    unsigned int flag)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    VAConfigAttrib attrib;
    int major, minor;
    unsigned int native_display = ANDROID_DISPLAY_HANDLE;

    ctx->vaDisplay = vaGetDisplay(&native_display);
    if (!ctx->vaDisplay) {
        ALOGE("VA Get Display Failed.\n");
        return VA_STATUS_ERROR_INVALID_DISPLAY;
    }

    vaStatus = vaInitialize(ctx->vaDisplay, &major, &minor);

    CHECK_RET_INIT_VIDEO(vaStatus, "VA Initialize Failed", ctx);

    attrib.type = VAConfigAttribRTFormat;
    attrib.value = VA_RT_FORMAT_YUV420;

    vaStatus = vaCreateConfig(ctx->vaDisplay,
                        VAProfileNone, VAEntrypointVideoProc,
                        &attrib, 1, &ctx->vaConfig);
    CHECK_RET_INIT_VIDEO(vaStatus, "VA Create Config Failed", ctx);

    vaStatus = vaCreateContext(ctx->vaDisplay,
                            ctx->vaConfig,
                            width, height, flag,
                            nullptr, 0, &ctx->vaContext);
    CHECK_RET_INIT_VIDEO(vaStatus, "VA Create Context Failed", ctx);

    return vaStatus;
}

void HalFormatToVAFormat(unsigned int format,
                unsigned int  *fourcc, int *type)
{
    switch (format) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            *fourcc = VA_FOURCC_NV12;
            *type   = VA_RT_FORMAT_YUV420;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            *fourcc = VA_FOURCC_R5G6B5;
            *type   = VA_FOURCC_R5G6B5;
            break;
        case HAL_PIXEL_FORMAT_YV12:
            *fourcc = VA_FOURCC_YV12;
            *type   = VA_RT_FORMAT_YUV420;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            *fourcc = VA_FOURCC_YUY2;
            *type   = VA_RT_FORMAT_YUV422;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            ALOGE("%s: HAL_PIXEL_FORMAT: %d got deprecated\n", __func__, format);
            break;
        default:
            ALOGE("%s: Unsupported HAL_PIXEL_FORMAT: %d, need to check it\n", __func__, format);
    }
}

VAStatus VideoProcCreateSurfaces(struct VideoProcContext *ctx,
                const camera3_stream_buffer_t *cameraBuffer,
                VASurfaceID *surface, VARectangle rect)
{
    VASurfaceAttribExternalBuffers  externBuf;
    VASurfaceAttrib list[2];
    VAStatus     vaStatus;
    int          type   = VA_RT_FORMAT_RGB32;
    unsigned int fourcc = VA_FOURCC_ARGB;

    memset(&externBuf, 0, sizeof(externBuf));
    memset(&(list[0]),0,2*sizeof(VASurfaceAttrib));

    HalFormatToVAFormat(cameraBuffer->stream->format, &fourcc, &type);

    // Create vaSurfaces from external DRM Buffer
    externBuf.pixel_format = fourcc;
    externBuf.width        = rect.width;
    externBuf.height       = rect.height;

    externBuf.pitches[0]   = getNativeHandleStride(cameraBuffer->buffer);
    externBuf.buffers      = (long unsigned int*)cameraBuffer->buffer;
    externBuf.num_buffers  = 1;
    externBuf.flags        = 0; //flag - its for secure layer;

    list[0].type          = VASurfaceAttribMemoryType;
    list[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    list[0].value.type    = VAGenericValueTypeInteger;
    list[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_ANDROID_GRALLOC;

    list[1].type          =
    VASurfaceAttribExternalBufferDescriptor;//VASurfaceAttribPixelFormat;
    list[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    list[1].value.type    =
    VAGenericValueTypePointer;//VAGenericValueTypeInteger;
    list[1].value.value.p = (void *)&externBuf;

    vaStatus = vaCreateSurfaces(ctx->vaDisplay, type, rect.width,
                                rect.height, surface, 1, list, 2);
    return vaStatus;
}

VAStatus VideoProcDestroySurface(struct VideoProcContext *ctx,
                                VASurfaceID *surface)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    if (VA_INVALID_ID != *surface) {
        vaStatus = vaDestroySurfaces(ctx->vaDisplay, surface, 1);
        return vaStatus;
    }
    return VA_INVALID_ID;
}

VAStatus VideoProcDestroySurfaces(struct VideoProcContext *ctx)
{
    VAStatus vaStatus;

    vaStatus = VideoProcDestroySurface(ctx, &ctx->dstSurface);

    if (vaStatus) {
    LOGE("VA Destroy Dst Surfaces Failed, ret = %x\n", vaStatus);
    }

    vaStatus = VideoProcDestroySurface(ctx, &ctx->srcSurface);
    if (vaStatus) {
        LOGE("VA Destroy Src Surfaces Failed, ret = %x\n", vaStatus);
    }
    return VA_STATUS_SUCCESS;
}

VAStatus VideoProcCreateBuffer(struct VideoProcContext *ctx,
                    VABufferID  *buffer, VASurfaceID surface,
                    VARectangle rect)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    VAProcPipelineParameterBuffer param;
    VARectangle surface_region;

    memset(&param, 0, sizeof(VAProcPipelineParameterBuffer));

    surface_region.x = rect.x;
    surface_region.y = rect.y;
    surface_region.width = rect.width;
    surface_region.height = rect.height;
    param.surface_region = &surface_region;

    param.surface = surface;
    param.surface_color_standard = VAProcColorStandardBT601;
    param.output_color_standard = VAProcColorStandardBT601;
    param.num_filters = 0;
    param.filters = nullptr;
    param.filter_flags = VA_FRAME_PICTURE;

    //create buffer
    vaStatus = vaCreateBuffer(ctx->vaDisplay, ctx->vaContext, VAProcPipelineParameterBufferType,
                            sizeof(VAProcPipelineParameterBuffer), 1, &param, buffer);
    return vaStatus;
}

VAStatus VideoProcRendering(struct VideoProcContext *ctx,
                            BufferPackage &bp)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    vaStatus = VideoProcCreateSurfaces(ctx, bp.nativeWinBuf, &ctx->dstSurface, ctx->dstRect);
    if (vaStatus) {
        LOGE("VA Create Dst Surface Failed %x\n", vaStatus);
        return vaStatus;
    }

    //Allocate src surface
    vaStatus = VideoProcCreateSurfaces(ctx, bp.nativeHalBuf, &ctx->srcSurface, ctx->srcRect);
    if (vaStatus) {
        LOGE("VA Create Src Surface Failed %x\n", vaStatus);
        VideoProcDestroySurface(ctx, &ctx->dstSurface);
        return vaStatus;
    }

    vaStatus = VideoProcCreateBuffer(ctx, &ctx->srcBuffer, ctx->srcSurface, ctx->srcRect);
    if (vaStatus) {
        LOGE("VA Create Src Buffer Failed %x\n", vaStatus);
        VideoProcDestroySurfaces(ctx);
        return vaStatus;
    }

    vaStatus = vaBeginPicture(ctx->vaDisplay, ctx->vaContext, ctx->dstSurface);
    CHECK_RET_VIDEO_RENDER(vaStatus, "VA Begin Picture Failed", ctx);

    vaStatus = vaRenderPicture(ctx->vaDisplay, ctx->vaContext, &ctx->srcBuffer, 1);
    CHECK_RET_VIDEO_RENDER(vaStatus, "VA Render Picture Failed", ctx);

    vaStatus = vaEndPicture(ctx->vaDisplay, ctx->vaContext);
    CHECK_RET_VIDEO_RENDER(vaStatus, "VA End Picture Failed", ctx);

    vaStatus = vaSyncSurface(ctx->vaDisplay, ctx->dstSurface);
    if (vaStatus)
        LOGE("VA Sync Surface Failed, ret %x\n", vaStatus);

    vaStatus = vaDestroyBuffer(ctx->vaDisplay, ctx->srcBuffer);
    if (vaStatus)
        LOGE("VA Destroy Buffer Failed, ret %x\n", vaStatus);

    VideoProcDestroySurfaces(ctx);

    return VA_STATUS_SUCCESS;
}
#endif


GenImageConvert::GenImageConvert()
#ifdef ENABLE_IVP
    : miVPCtxValid(false)
#endif
{
    // Width and height are not important for us, hence the 1, 1
#ifdef ENABLE_IVP
    if (iVP_create_context(&miVPCtx, 1, 1, 0) == IVP_STATUS_SUCCESS) {
        miVPCtxValid = true;
    } else {
        ALOGE("Failed to create iVP context");
    }
#else
    if (initVideoProcPipeline(&vaContext, 1, 1, 0) != VA_STATUS_SUCCESS) {
        ALOGE("Failed to create VP context");
    }
#endif
}

GenImageConvert::~GenImageConvert()
{
#ifdef ENABLE_IVP
    if (miVPCtxValid)
        iVP_destroy_context(&miVPCtx);
#else
    destroyVideoProcPipeline(&vaContext);
#endif
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
    if (gralloc_handle == nullptr) {
        LOGE("Sending non-gralloc buffer to iVP that does not work, aborting color conversion");
        return INVALID_OPERATION;
    }
    iVPLayer->gralloc_handle = *gralloc_handle;

    return NO_ERROR;
}

status_t GenImageConvert::iVPColorConversion(BufferPackage &bp)
{
#ifdef ENABLE_IVP
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
#else
    VideoProcContext *ctx = &vaContext;
    VAStatus vaStatus;

    if (!ctx) {
        LOGE("error from %s and line %d\n", __FUNCTION__, __LINE__);
        return UNKNOWN_ERROR;
    }
#endif

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
#ifdef ENABLE_IVP
    status_t status = cameraBuffer2iVPLayer(bp.nativeHalBuf, bp.nativeHalBuf->buffer, &src, left, top);
    if (status != NO_ERROR)
        return status;

    status = cameraBuffer2iVPLayer(bp.nativeWinBuf, bp.nativeWinBuf->buffer, &dst);
    if (status != NO_ERROR)
        return status;

    // Src dst rect is the operations dst rect
    srcDstRect = dstDstRect;
    iVP_status iVPstatus = iVP_exec(&miVPCtx, &src, nullptr, 0, &dst, true);

    if (iVPstatus != IVP_STATUS_SUCCESS)
        return UNKNOWN_ERROR;
#else
    ctx->srcRect.x = left;
    ctx->srcRect.y = top;
    ctx->srcRect.width  = bp.nativeHalBuf->stream->width - 2 * left;
    ctx->srcRect.height = bp.nativeHalBuf->stream->height - 2 * top;
    ctx->dstRect.x = ctx->dstRect.y = 0;
    ctx->dstRect.width  = bp.nativeWinBuf->stream->width - 2 * left;
    ctx->dstRect.height = bp.nativeWinBuf->stream->height - 2 * top;
    vaStatus = VideoProcRendering(ctx, bp);

    if (vaStatus) {
        ALOGE("Video Processing Failure vaStatus =%x\n", vaStatus);
        return UNKNOWN_ERROR;
    }
#endif
    return NO_ERROR;
}
} // namespace android

