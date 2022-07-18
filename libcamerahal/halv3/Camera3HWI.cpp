/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Intel Corporation.
 *
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

#define LOG_TAG "Camera3HWI"
//#define LOG_NDEBUG 0

#define __STDC_LIMIT_MACROS

// System dependencies
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

// Camera dependencies
#include "Camera3HWI.h"
#include "iutils/Utils.h"
#include <ICamera.h>
#include <Utils.h>
#include <cutils/properties.h>
#include "IJpeg.h"
#include "iutils/CameraLog.h"
#include "Exif.h"
#if !defined(USE_CROS_GRALLOC)
#include <ufo/gralloc.h>
#endif // USE_CROS_GRALLOC
#include "CameraMetadata.h"

namespace android {
namespace camera2 {

#undef ALOGD
#define ALOGD(...) ((void*)0)

#define DATA_PTR(MEM_OBJ,INDEX) MEM_OBJ->getPtr( INDEX )
#define EMPTY_PIPELINE_DELAY 2
#define PARTIAL_RESULT_COUNT 2
#define FRAME_SKIP_DELAY     0

#define MAX_VALUE_8BIT ((1<<8)-1)
#define MAX_VALUE_10BIT ((1<<10)-1)
#define MAX_VALUE_12BIT ((1<<12)-1)

/* Batch mode is enabled only if FPS set is equal to or greater than this */
#define DEFAULT_VIDEO_FPS      (30.0)
// Set a threshold for detection of missing buffers //seconds
#define MISSING_REQUEST_BUF_TIMEOUT 3
#define FLUSH_TIMEOUT 3
#define METADATA_MAP_SIZE(MAP) (sizeof(MAP)/sizeof(MAP[0]))
/* Per configuration size for static metadata length*/
#define PER_CONFIGURATION_SIZE_3 (3)
#define TIMEOUT_NEVER -1

#define DEFAULT_ENTRY_CAP 256
#define DEFAULT_DATA_CAP 2048
#define JPEG_QUALITY 85

icamera::Parameters *gCamCapability[MAX_CAM_NUM];
camera_metadata_t *gStaticMetadata[MAX_CAM_NUM];
CameraMetadata gCameraMetadata[MAX_CAM_NUM];
extern uint8_t gNumCameraSessions;

camera3_device_ops_t Camera3HardwareInterface::mCameraOps = {
    .initialize                         = Camera3HardwareInterface::initialize,
    .configure_streams                  = Camera3HardwareInterface::configure_streams,
    .register_stream_buffers            = NULL,
    .construct_default_request_settings = Camera3HardwareInterface::construct_default_request_settings,
    .process_capture_request            = Camera3HardwareInterface::process_capture_request,
    .get_metadata_vendor_tag_ops        = NULL,
    .dump                               = Camera3HardwareInterface::dump,
    .flush                              = Camera3HardwareInterface::flush,
    .reserved                           = {0},
};

/*===========================================================================
 * FUNCTION   : Camera3HardwareInterface
 *
 * DESCRIPTION: constructor of Camera3HardwareInterface
 *
 * PARAMETERS :
 *   @cameraId  : camera ID
 *
 * RETURN     : none
 *==========================================================================*/
Camera3HardwareInterface::Camera3HardwareInterface(uint32_t cameraId,
        const camera_module_callbacks_t *callbacks)
    : m_pGralloc(NULL),
      mCameraId(cameraId),
      mCameraInitialized(false),
      mCallbackOps(NULL),
      mGenConvert(NULL),
      mFirstConfiguration(true),
      mFlush(false),
      mCallbacks(callbacks),
      mState(CLOSED)
{
    getLogLevel();
    mDeviceId = cameraId;
    mCameraDevice.common.tag = HARDWARE_DEVICE_TAG;
    mCameraDevice.common.version = CAMERA_DEVICE_API_VERSION_3_3;
    mCameraDevice.common.close = close_camera_device;
    mCameraDevice.ops = &mCameraOps;
    mCameraDevice.priv = this;
    mMainStreamInfo = NULL;
    mPendingLiveRequest = 0;
    mCurrentRequestId = -1;
    mIVPSupportedFmts[0] = V4L2_PIX_FMT_YUYV;
    mIVPSupportedFmts[1] = V4L2_PIX_FMT_RGB565;
    mIVPSupportedFmts[2] = V4L2_PIX_FMT_YVU420;
    mIVPSupportedFmts[3] = V4L2_PIX_FMT_NV12;
    mInputConfig.width = 0;
    mInputConfig.height = 0;
    mInputConfig.format = -1;

    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++)
        mDefaultMetadata[i] = NULL;

    // Getting system props of different kinds

    icamera::camera_hal_init();

    char value[PROPERTY_VALUE_MAX];
    const char* PROP_CAMERA_HAL_ID = "persist.camera.hal.id";
    if (property_get(PROP_CAMERA_HAL_ID, value, NULL)) {
        mDeviceId = atoi(value);
        ALOGI("Camera Device ID is 0x%x", mDeviceId);
    }

    setDeviceId(cameraId);
    mGenConvert = new GenImageConvert();
    if (mGenConvert == NULL)
        ALOGE("Failed to create Gen image converter");

    if (icamera::camera_jpeg_init() != NO_ERROR) {
        ALOGE("%s: failed to init jpeg!", __func__);
    }

    setCamHalDebugEnv();
}

/*===========================================================================
 * FUNCTION   : ~Camera3HardwareInterface
 *
 * DESCRIPTION: destructor of Camera3HardwareInterface
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
Camera3HardwareInterface::~Camera3HardwareInterface()
{
    List<stream_info_t *>::iterator it;
    ALOGI("%s: Entry", __func__);
    AutoMutex l(mLock);

    if (mState != CLOSED)
        closeCamera();

    for (pendingRequestIterator i = mPendingRequestsList.begin();
            i != mPendingRequestsList.end();) {
        i = erasePendingRequest(i);
    }
    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++)
        if (mDefaultMetadata[i])
            free_camera_metadata(mDefaultMetadata[i]);

    cleanStreamInfo();
    releaseMainStream();
    if (mGenConvert)
        delete mGenConvert;

    icamera::camera_hal_deinit();
    ALOGI("%s: Exit", __func__);
}

/*===========================================================================
 * FUNCTION   : erasePendingRequest
 *
 * DESCRIPTION: function to erase a desired pending request after freeing any
 *              allocated memory
 *
 * PARAMETERS :
 *   @i       : iterator pointing to pending request to be erased
 *
 * RETURN     : iterator pointing to the next request
 *==========================================================================*/
Camera3HardwareInterface::pendingRequestIterator
        Camera3HardwareInterface::erasePendingRequest (pendingRequestIterator i)
{
    for (pendingBufferIterator j = i->buffers.begin(); j != i->buffers.end(); j++) {
        if (j->buffer) {
            delete j->buffer;
            j->buffer = NULL;
        }
    }
    free_camera_metadata(i->settings);
    return mPendingRequestsList.erase(i);
}

void Camera3HardwareInterface::copyYuvData(camera3_stream_buffer_t * src,camera3_stream_buffer_t * dst)
{
    void *pDataSrc = NULL;
    void *pDataDst = NULL;
    uint32_t src_uv_offset, dst_uv_offset;
#if defined(USE_CROS_GRALLOC)
    uint32_t src_width, src_height, src_stride, dst_width, dst_height, dst_stride;
    pDataSrc = camera3bufLock(src, &src_width, &src_height, &src_stride);
    pDataDst = camera3bufLock(dst, &dst_width, &dst_height, &dst_stride);
    src_uv_offset = src_stride * src_height;
    dst_uv_offset = dst_stride * dst_height;

    memcpy(pDataDst, pDataSrc, src_stride * src_height);
    memcpy((char *)pDataDst + dst_uv_offset, (char *)pDataSrc + src_uv_offset, src_stride * src_height / 2);
#else
    intel_ufo_buffer_details_t src_info, dst_info;
    uint32_t src_stride, dst_stride;
    pDataSrc = camera3bufLock(src, &src_info);
    pDataDst = camera3bufLock(dst, &dst_info);

    src_stride = src_info.pitch;
    dst_stride = dst_info.pitch;
    src_uv_offset = src_stride * src_info.height;
    dst_uv_offset = dst_stride * dst_info.allocHeight;

    memcpy(pDataDst, pDataSrc, src_stride * src_info.height);
    memcpy((char *)pDataDst + dst_uv_offset, (char *)pDataSrc + src_uv_offset, src_stride * src_info.height / 2);

#endif
    camera3bufUnlock(src);
    camera3bufUnlock(dst);
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS :
 *   @hw_device  : double ptr for camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera3HardwareInterface::openCamera(struct hw_device_t **hw_device)
{
    int rc = 0;
    if (mState != CLOSED) {
        *hw_device = NULL;
        return PERMISSION_DENIED;
    }

    ALOGI("%s: [KPI Perf]: E PROFILE_OPEN_CAMERA camera id %d", __func__,
             mCameraId);

    //hal_init is already called here
    rc = openCamera();
    if (rc == 0) {
        *hw_device = &mCameraDevice.common;
    } else {
        *hw_device = NULL;
    }

    ALOGI("%s: [KPI Perf]: X PROFILE_OPEN_CAMERA camera id %d, rc: %d", __func__,
             mCameraId, rc);

    if (rc == NO_ERROR) {
        mState = OPENED;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera3HardwareInterface::openCamera()
{
    int rc = 0;
    char value[PROPERTY_VALUE_MAX];

    icamera::camera_info_t info;
    CLEAR(info);

    icamera::get_camera_info(mDeviceId, info);

    int vc_number = info.vc_total_num;

    //Get the max virtual channel number
    memset(value, 0, sizeof(value));
    if (property_get("camera.vc.number", value, NULL)) {
        vc_number = atoi(value);
        ALOGI("%s: vc_number is %d", __func__, vc_number);
    }

    rc = icamera::camera_device_open(mDeviceId, vc_number);

    if (rc) {
        ALOGE("camera_open failed. rc = %d, mDeviceId = %d", rc, mDeviceId);
        return rc;
    }

    mFirstConfiguration = true;

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : closeCamera
 *
 * DESCRIPTION: close camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera3HardwareInterface::closeCamera()
{
    int rc = NO_ERROR;

    ALOGI("%s: [KPI Perf]: E PROFILE_CLOSE_CAMERA camera id %d", __func__,
             mDeviceId);

    icamera::camera_device_close(mDeviceId);

    mState = CLOSED;
    ALOGI("%s: [KPI Perf]: X PROFILE_CLOSE_CAMERA camera id %d, rc: %d", __func__,
         mDeviceId, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : initialize
 *
 * DESCRIPTION: Initialize frameworks callback functions, called after open
 *
 * PARAMETERS :
 *   @callback_ops : callback function to frameworks
 *
 * RETURN     :
 *
 *==========================================================================*/
int Camera3HardwareInterface::initialize(
        const struct camera3_callback_ops *callback_ops)
{
    int rc;

    ALOGI("E :mCameraId = %d mState = %d", mCameraId, mState);

    AutoMutex l(mLock);

    // Validate current state
    switch (mState) {
        case OPENED:
            /* valid state */
            break;

        case ERROR:
            handleCameraDeviceError();
            rc = -ENODEV;
            goto err1;

        default:
            ALOGE("Invalid state %d", mState);
            rc = -ENODEV;
            goto err1;
    }

    mCallbackOps = callback_ops;

    mCameraInitialized = true;
    mState = INITIALIZED;
    ALOGI("X");
    return 0;

err1:
    return rc;
}

/* fmt mapping shuold align with VPG*/
int Camera3HardwareInterface::HalFormat2V4L2Format(int HalFormat) {
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

int Camera3HardwareInterface::V4L2Format2HalFormat(int V4L2Format) {
    int format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;

    switch (V4L2Format) {
        case V4L2_PIX_FMT_NV12:
            format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            break;
        case V4L2_PIX_FMT_RGB565:
            format = HAL_PIXEL_FORMAT_RGB_565;
            break;
        case V4L2_PIX_FMT_YVU420:
            format = HAL_PIXEL_FORMAT_YV12;
            break;
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            format = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;
        case V4L2_PIX_FMT_NV16:
            format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
            break;
        default:
            format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            ALOGE("%s: Unsupported V4L2 format: %d, use default HAL format",
                __func__, V4L2Format);
    }

    ALOGD("%s: HAL format = 0x%x", __func__, format);
    return format;
}


int Camera3HardwareInterface::getExtraHeight(int w, int h, int gfxFmt, int v4l2Fmt)
{
    int extraHeight = 0;
    int usage = GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_CAMERA_READ |
             GRALLOC_USAGE_SW_WRITE_OFTEN;
    ALOGI("%s: E", __FUNCTION__);
    int size = icamera::CameraUtils::getFrameSize(v4l2Fmt, w, h);

    sp<CameraGfxBuffer> gfxBuf = allocateGraphicBuffer(w, h, gfxFmt, usage);
    if (gfxBuf == NULL) {
        ALOGE("Failed to allocate graphics HAL buffers, getExtraHeight return 0");
        return 0;
    }
    if ((int)gfxBuf->size() < size) {
        extraHeight = (size - (int)gfxBuf->size()) / gfxBuf->stride();
        if ((size - (int)gfxBuf->size()) % gfxBuf->stride() != 0)
            extraHeight = extraHeight + 1;
    }

    ALOGI("%s: X, get extraheight %d", __FUNCTION__, extraHeight);
    return extraHeight;
}

/**
  * The size of the HAL buffers will be based on the supported ISys
  * resolution. This buffers collect the output from the ISys
  * and will be used as input to the graphics scaler.
  */
camera3_stream_buffer_t* Camera3HardwareInterface::allocateMainBuf(
    icamera::stream_t *hwstream, stream_info_t *swstream)
{
    int field, width, height, gfxfmt, v4l2fmt, usage;

    if (swstream == NULL) {
        ALOGE("%s: error stream info is NULL!", __func__);
        return NULL;
    }

    field = hwstream->field;
    width = hwstream->width;
    height = hwstream->height;
    v4l2fmt = hwstream->format;
    gfxfmt = V4L2Format2HalFormat(hwstream->format);
    usage = GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_CAMERA_READ |
             GRALLOC_USAGE_SW_WRITE_OFTEN;

    camera3_stream_buffer_t *halbuf;
    icamera::camera_resolution_t bestIsysRes = {0, 0};
    icamera::CameraUtils::getBestISysResolution(mDeviceId,
                                    field, width, height, bestIsysRes);
    int srcWidth = bestIsysRes.width;
    int srcHeight = icamera::CameraUtils::getInterlaceHeight(field, bestIsysRes.height);

    /** This is a WA as Isys output needed buffer size shouldn't
    * be smaller than gfx allocate buffer size. When allocate gfx
    * buffer, add extra height to make sure the gfx buffer size
    * isn't smaller than Isys needed buffer size.
    */
    int extraHeight = getExtraHeight(srcWidth, srcHeight, gfxfmt, v4l2fmt);

    sp<CameraGfxBuffer> gfxBuf =
        allocateGraphicBuffer(srcWidth, srcHeight + extraHeight, gfxfmt, usage);
    if (gfxBuf == NULL) {
        ALOGE("%s: Failed to allocate graphics HAL buffers", __func__);
        return NULL;
    }

    halbuf = new camera3_stream_buffer_t;
    halbuf->stream = swstream->main_hal_stream;

    halbuf->acquire_fence = -1;
    halbuf->status = CAMERA3_BUFFER_STATUS_INTERNAL;
    halbuf->buffer = gfxBuf->getBufferHandle();
    halbuf->stream->width = srcWidth;
    halbuf->stream->height = srcHeight + extraHeight;
    halbuf->stream->format = gfxfmt;
    halbuf->stream->usage = usage;

    mpreview_ptrs.push_back(gfxBuf);

    ALOGI("%s: allocate hal buf %dx%d, handle = %p, fmt = 0x%x, stream = %p",
        __func__, halbuf->stream->width, halbuf->stream->height,
        halbuf->buffer, halbuf->stream->format, halbuf->stream);

    return halbuf;
}

void Camera3HardwareInterface::deallocateMainBuf(stream_info_t *streaminfo)
{
    int buf_num;
    BufferPackage *buf_pack;
    ALOGD("%s : E", __FUNCTION__);

    buf_num = streaminfo->main_hal_bufnum;
    buf_pack = streaminfo->main_hal_buf;

    for (int i = 0; i < buf_num; i++) {
        if(buf_pack[i].nativeHalBuf) {
            delete buf_pack[i].nativeHalBuf;
            buf_pack[i].nativeHalBuf = NULL;
        }
    }
    streaminfo->main_hal_bufnum = 0;
    mpreview_ptrs.clear();

    ALOGD("%s : X",__FUNCTION__);
}

camera3_stream_buffer_t* Camera3HardwareInterface::allocateJPEGBuf(
    stream_info_t *swstream)
{
    camera3_stream_buffer_t *halbuf;
    int width, height, gfxfmt, usage;

    width = swstream->reqstream->width;
    height = swstream->reqstream->height;
    gfxfmt = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    usage = GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_CAMERA_READ |
             GRALLOC_USAGE_SW_WRITE_OFTEN;

    sp<CameraGfxBuffer> gfxBuf =
        allocateGraphicBuffer(width, height, gfxfmt, usage);
    if (gfxBuf == NULL) {
        ALOGE("Failed to allocate graphics HAL buffers");
        return NULL;
    }

    halbuf = new camera3_stream_buffer_t;
    halbuf->stream = swstream->main_hal_stream;

    halbuf->buffer = gfxBuf->getBufferHandle();
    halbuf->stream->width = width;
    halbuf->stream->height = height;
    halbuf->stream->format = gfxfmt;
    halbuf->stream->usage = usage;

    ALOGI("%s: allocate jpg buf %dx%d, handle = %p, fmt = 0x%x",
        __func__, width, height, halbuf->buffer, gfxfmt);

    mpicture_ptrs.push_back(gfxBuf);
    return halbuf;
}

void Camera3HardwareInterface::deallocateJPEGBuf(stream_info_t *streaminfo)
{
    mpicture_ptrs.clear();
    if (streaminfo->jpgbuf)
        delete streaminfo->jpgbuf;
    ALOGI("%s : X",__FUNCTION__);
}

camera3_stream_buffer_t* Camera3HardwareInterface::allocateHALBuf(int width, int height)
{
    camera3_stream_buffer_t *halbuf;
    int gfxfmt, usage;

    gfxfmt = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    usage = GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_CAMERA_READ |
             GRALLOC_USAGE_SW_WRITE_OFTEN;

    sp<CameraGfxBuffer> gfxBuf =
        allocateGraphicBuffer(width, height, gfxfmt, usage);
    if (gfxBuf == NULL) {
        ALOGE("Failed to allocate graphics HAL buffers");
        return NULL;
    }

    halbuf = new camera3_stream_buffer_t;
    halbuf->stream = new camera3_stream_t;

    halbuf->stream->width = width;
    halbuf->stream->height = height;
    halbuf->stream->format = gfxfmt;
    halbuf->stream->usage = usage;
    halbuf->buffer = gfxBuf->getBufferHandle();

    ALOGI("%s: allocate hal buf %dx%d, handle = %p, fmt = 0x%x",
        __func__, width, height, halbuf->buffer, gfxfmt);

    mhalbuf_ptrs.push_back(gfxBuf);
    return halbuf;
}

void Camera3HardwareInterface::deallocateHALBuf(camera3_stream_buffer_t* buf)
{
    ALOGI("%s : E", __FUNCTION__);
    mhalbuf_ptrs.clear();
    delete buf->stream;
    delete buf;
    ALOGI("%s : X",__FUNCTION__);
}

camera3_stream_buffer_t* Camera3HardwareInterface::getMainHalBuf() {
    int i;
    stream_info_t* streaminfo = mMainStreamInfo;
    int buf_num;
    BufferPackage *buf_pack;

    if (streaminfo == NULL) {
        ALOGE("%s, can't get stream info", __func__);
        return NULL;
    }
    buf_num = streaminfo->main_hal_bufnum;
    buf_pack = streaminfo->main_hal_buf;

    if (buf_pack == NULL) {
        ALOGE("%s, stream without preview buffer", __func__);
        return NULL;
    }

    if (buf_num > MAX_BUFFERS) {
        ALOGE("%s: external buf num has exceed internal buf num!!", __func__);
        return NULL;
    }

    for(i = 0; i < buf_num; i++) {
        if (buf_pack[i].nativeHalBuf == NULL) {
            continue;
        }
        if(buf_pack[i].flag == 0) {
            buf_pack[i].flag = 1;
            return buf_pack[i].nativeHalBuf;
        }
    }

    if (i < MAX_BUFFERS) {
        streaminfo->main_hal_bufnum++;
    }

    buf_num = streaminfo->main_hal_bufnum;
    if(buf_num > 0) {
        buf_pack[buf_num-1].flag = 1;
        return buf_pack[buf_num-1].nativeHalBuf;
    }
    return NULL;
}

stream_info_t* Camera3HardwareInterface::getStreamInfo(camera3_stream_t *stream)
{
    List<stream_info_t *>::iterator it;
    stream_info_t *streaminfo = NULL;

    if (!stream) {
        ALOGE("%s: error no stream!", __func__);
        return NULL;
    }

    for (it = mStreamInfo.begin(); it != mStreamInfo.end(); it++) {
        if ((stream  == (*it)->reqstream) || (stream == (*it)->main_hal_stream)) {
            streaminfo = *it;
            if ((*it)->status != VALID) {
                ALOGE("%s: error steam is invalid!", __func__);
                streaminfo = NULL;
            }
            break;
        }
    }

    if (!streaminfo) {
        ALOGE("%s: failed to get streaminfo!", __func__);
        return NULL;
    }

    return streaminfo;
}

bool Camera3HardwareInterface::isSameStream(
        camera3_stream_t *src, camera3_stream_t *dst)
{
    if ((src == dst) &&
        (src->format == dst->format) &&
        (src->height == dst->height) &&
        (src->width == dst->width) &&
        (src->stream_type == dst->stream_type) &&
        (src->usage == dst->usage))
        return true;

    return false;
}


int Camera3HardwareInterface::constructStreamInfo(
        camera3_stream_configuration_t *streamList)
{
    size_t i = 0;
    camera3_stream_t *newStream = NULL;
    camera3_stream_t *oldStream = NULL;
    List<stream_info_t *>::iterator it;
    bool isSame = false;

    stream_type_t streamid = HW_CHANNEL0; //TODO: add HW_CHANNEL0/HW_CHANNEL1 id mapping
    int halMainFormat = V4L2Format2HalFormat(mStreams[streamid].format);
    // stream_info initialize
    for (it = mStreamInfo.begin(); it != mStreamInfo.end(); it++) {
            (*it)->status = INVALID;
            (*it)->channelid = NONE_CHANNEL;
            (*it)->channel = NULL;
    }

    // compare new streams with original ones
    for (i = 0; i < streamList->num_streams; i++) {
        newStream = streamList->streams[i];
        isSame = false;

        for (it = mStreamInfo.begin(); it != mStreamInfo.end(); it++) {
            oldStream = (*it)->reqstream;
            if (isSameStream(oldStream, newStream)) {
                (*it)->status = VALID;
                isSame = true;
                break;
            }
        }

        if (!isSame) {
            if (newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
                //TODO: ZSL

            } else if (newStream->stream_type != CAMERA3_STREAM_INPUT) {
                stream_info_t* stream_info;
                stream_info = new stream_info_t;
                if (!stream_info) {
                    ALOGE("%s: could not allocate stream info", __func__);
                    return NO_MEMORY;
                }

                memset(stream_info, 0, sizeof(stream_info_t));
                stream_info->reqstream = newStream;
                stream_info->status = VALID;
                stream_info->channel = NULL;
                stream_info->main_hal_stream = NULL;
                mStreamInfo.push_back(stream_info);
                ALOGI("%s: store stream %p in the list", __func__, newStream);
                stream_info->main_hal_stream = new camera3_stream_t;

                if (newStream->usage == GRALLOC_USAGE_HW_VIDEO_ENCODER)
                    stream_info->hwencoder = true;

                switch (newStream->format) {
                // YUV Video or Preview
                case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                // YUV Preview CallBack
                case HAL_PIXEL_FORMAT_YCbCr_420_888:
                    break;
                // JPEG Capture
                case HAL_PIXEL_FORMAT_BLOB:
                    stream_info->jpgbuf = allocateJPEGBuf(stream_info);
                    stream_info->jpgencoder = true;
                    break;
                default:
                    break;
                }

                // All stream need resized by IVP.
                if ((newStream->format != halMainFormat)
                    || ((int)newStream->width != mStreams[streamid].width)
                    || ((int)newStream->height != mStreams[streamid].height)) {
                    stream_info->ivpbuf =
                        allocateHALBuf(newStream->width, newStream->height);
                    stream_info->ivpconvert = true;
                }
            } else {
                ALOGE("%s: not support input stream, TODO!", __func__);
                return BAD_VALUE;
            }
        }
    }
    mStreamNum = streamList->num_streams;
    return NO_ERROR;
}

// find the fmt that is both supported by ivp and ipu, tend to use ivp for fmt convert, avoid sw
int Camera3HardwareInterface::getBestFormat(int format)
{
    vector<int> isysFmts;

    icamera::CameraUtils::getSupportedISysFormats(mDeviceId, isysFmts);

    for (size_t i = 0; i < MAX_IVPFMT; i++) {
        for (size_t j = 0; j < isysFmts.size(); j++) {
            if (mIVPSupportedFmts[i] == isysFmts[j]) {
                ALOGI("%s: found best ivp fmt 0x%x", __func__, isysFmts[j]);
                return isysFmts[j];
            }
        }
    }
    return format;
}

// set libcamhal debug parameter
void Camera3HardwareInterface::setCamHalDebugEnv()
{
    const char* PROP_CAMERA_HAL_DEBUG = "camera.hal.debug";
    const char* DEBUG_KEY = "cameraDebug";
    char value [PROPERTY_VALUE_MAX];
    char dumpPath[50];
    if (property_get(PROP_CAMERA_HAL_DEBUG, value, NULL)) {
        int logLevel = atoi(value);
        // to enable both LOG1 and LOG2 traces
        if (logLevel & (1 << 1))
            logLevel |= 1;
        ALOGI("Camera Device debug level is 0x%x", logLevel);
        setenv(DEBUG_KEY, value, 1);
    }

    const char* PROP_CAMERA_HAL_PERF = "camera.hal.perf";
    const char* PERF_KEY = "cameraPerf";
    if (property_get(PROP_CAMERA_HAL_PERF, value, NULL)) {
        int perfLevel = atoi(value);
        ALOGI("Camera perf level is 0x%x", perfLevel);
        setenv(PERF_KEY, value, 1);
    }

    const char* PROP_CAMERA_HAL_DUMP = "camera.hal.dump";
    const char* DUMP_KEY      = "cameraDump";
    const char* DUMP_PATH_KEY = "cameraDumpPath";
    if (property_get(PROP_CAMERA_HAL_DUMP, value, NULL)) {
        int dumpType = atoi(value);
        ALOGI("Camera dump type is 0x%x", dumpType);
        setenv(DUMP_KEY, value, 1);
    }

    snprintf(dumpPath, sizeof(dumpPath), "%s", "data/misc/cameraserver");
    setenv(DUMP_PATH_KEY, dumpPath, 1);

    // Set debug and dump level
    icamera::Log::setDebugLevel();
}
int Camera3HardwareInterface::getBestStream(
    int width, int height, int fmt, icamera::stream_t *match)
{
    if (!match) {
        ALOGE("%s: null stream!", __func__);
        return BAD_VALUE;
    }

    icamera::supported_stream_config_array_t availConfigs;
    icamera::camera_info_t info;
    CLEAR(info);
    icamera::get_camera_info(mDeviceId, info);

    info.capability->getSupportedStreamConfig(availConfigs);

    for (size_t i = 0; i < availConfigs.size(); i++) {
        ALOGI("%s: supported configs %dx%d format: %s, field: %d",
              __func__, availConfigs[i].width, availConfigs[i].height,
              icamera::CameraUtils::pixelCode2String(availConfigs[i].format),
              availConfigs[i].field);

        if ((availConfigs[i].width == width) &&
            (availConfigs[i].height == height) &&
            (availConfigs[i].format == fmt)) {
            match->format = availConfigs[i].format;
            match->width = availConfigs[i].width;
            match->height = availConfigs[i].height;
            match->field = availConfigs[i].field;
            match->stride = availConfigs[i].stride;
            match->size = availConfigs[i].size;
            memcpy(match, &availConfigs[i], sizeof(icamera::stream_t));
            return NO_ERROR;
        }
    }

    return BAD_VALUE;
}

int Camera3HardwareInterface::constructMainStream() {
    int streamid = HW_CHANNEL0;
    if(!mMainStreamInfo) {
        mMainStreamInfo = new stream_info_t;
    }
    memset(mMainStreamInfo, 0, sizeof(stream_info_t));

    mMainStreamInfo->status = VALID;
    mMainStreamInfo->channel = NULL;

    mMainStreamInfo->main_hal_stream = new camera3_stream_t;
    mMainStreamInfo->main_hal_buf = new BufferPackage[MAX_BUFFERS];
    for (int i = 0; i < MAX_BUFFERS; i++) {
        mMainStreamInfo->main_hal_buf[i].nativeHalBuf =
            allocateMainBuf(&mStreams[streamid], mMainStreamInfo);
        mMainStreamInfo->main_hal_buf[i].flag = 0;
    }
    mMainStreamInfo->main_hal_bufnum = 0;
    mMainStreamInfo->channel = new Camera3Channel(mDeviceId, &mStreams[HW_CHANNEL0], captureResultCb, this);
    return 0;
}

void Camera3HardwareInterface::releaseMainStream() {
    if (!mMainStreamInfo)
        return;
    if (mMainStreamInfo->channel) {
        mMainStreamInfo->channel->stop();
        delete mMainStreamInfo->channel;
    }
    if (mMainStreamInfo->main_hal_buf) {
        deallocateMainBuf(mMainStreamInfo);
        delete mMainStreamInfo->main_hal_buf;
    }

    delete mMainStreamInfo->main_hal_stream;
    delete mMainStreamInfo;
    mMainStreamInfo = NULL;
}
//TODO: what if there are more than one hw streams?
int Camera3HardwareInterface::constructHWStreams(
        camera3_stream_configuration_t *streamList)
{
    icamera::stream_t *matchStream;
    int format = V4L2_PIX_FMT_NV12;
    int memory = V4L2_MEMORY_USERPTR;
    int field = V4L2_FIELD_ANY;
    uint32_t width = 0, height = 0;
    icamera::camera_resolution_t bestISYSRes = {0, 0};
    int rc = NO_ERROR;
    int streamid = HW_CHANNEL0; //TODO, add hw channel mapping

    uint32_t i;
    camera3_stream_t *main = streamList->streams[0];
    camera3_stream_t *newStream = NULL;

    for (i = 0; i < streamList->num_streams; i++) {
        newStream = streamList->streams[i];
        if (IS_USAGE_PREVIEW(newStream->usage)) {
            main = newStream;
            break;
        }
    }
    if (main == NULL) {
        ALOGE("%s: main stream is NULL!", __func__);
        return BAD_VALUE;
    }

    format = HalFormat2V4L2Format(main->format);

    if ((0 == mInputConfig.width) || (0 == mInputConfig.height)) {
        icamera::CameraUtils::getBestISysResolution(
            mDeviceId, field, main->width, main->height, bestISYSRes);

        width = bestISYSRes.width;
        height = bestISYSRes.height;

        matchStream = (icamera::stream_t*)malloc(sizeof(icamera::stream_t));
        rc = getBestStream(width, height, format, matchStream);
        if (rc != NO_ERROR) {
            ALOGE("%s: failed to find a match stream!", __func__);
            free(matchStream);
            return rc;
        }
        field = matchStream->field;
        free(matchStream);
    } else {
        width = main->width;
        height = main->height;

        field = 0; //todo, hardcode
    }

    if (-1 == mInputConfig.format) {
        format = getBestFormat(format);
    }

    mStreams[streamid].format = format;
    mStreams[streamid].width = width;
    mStreams[streamid].height = height;
    mStreams[streamid].memType = memory;
    mStreams[streamid].field = field;
    mStreams[streamid].stride = icamera::CameraUtils::getStride(format, width);
    mStreams[streamid].size = icamera::CameraUtils::getFrameSize(format, width, SINGLE_FIELD(field) ? height/2 : height);
    mStreams[streamid].id = streamid;

    mStreamList.num_streams = 1;
    mStreamList.streams = mStreams;
    mStreamList.operation_mode = streamList->operation_mode;
    ALOGI("%s: hw stream %dx%d, stride %d, fmt 0x%x, frame size %d, field %d, stream id %d, streams number %d",
        __func__, mStreams[streamid].width, mStreams[streamid].height,
        mStreams[streamid].stride, mStreams[streamid].format,
        mStreams[streamid].size, mStreams[streamid].field,
        mStreams[streamid].id, mStreamList.num_streams);

    return NO_ERROR;
}

int Camera3HardwareInterface::checkStreams(
        camera3_stream_configuration_t *streamList)
{
    if (streamList == NULL) {
        ALOGE("%s: NULL stream configuration", __func__);
        return BAD_VALUE;
    }
    if (streamList->streams == NULL) {
        ALOGE("%s: NULL stream list", __func__);
        return BAD_VALUE;
    }

    if (streamList->num_streams < 1) {
        ALOGE("%s: Bad number of streams requested: %d",
                __func__, streamList->num_streams);
        return BAD_VALUE;
    }

    if (streamList->num_streams > MAX_NUM_STREAMS) {
        ALOGE("%s: Maximum number of streams %d exceeded: %d",
                __func__, MAX_NUM_STREAMS, streamList->num_streams);
        return BAD_VALUE;
    }

    return NO_ERROR;
}


void Camera3HardwareInterface::getInputConfig()
{
    char value[PROPERTY_VALUE_MAX];
    const char* PROP_CAMERA_INPUT_SIZE = "camera.input.config.size";
    const char* PROP_CAMERA_INPUT_FORMAT = "camera.input.config.format";
    /*const char* PROP_CAMERA_FIELD = "camera.hal.field";
    const char* PROP_CAMERA_DEINTERLACE_MODE = "camera.hal.deinterlace";*/

    mInputConfig.width = 0;
    mInputConfig.height = 0;
    mInputConfig.format = -1;


    // input: size: vga, 720p, 1080p, 480p, 576p
    //        format: uyvy, yuyv
    if (property_get(PROP_CAMERA_INPUT_SIZE, value, NULL)) {
        if (!strcmp(value, "vga")) {
            mInputConfig.width = 640;
            mInputConfig.height = 480;
        } else if (!strcmp(value, "480p")) {
            mInputConfig.width = 720;
            mInputConfig.height = 480;
        } else if (!strcmp(value, "576p")) {
            mInputConfig.width = 720;
            mInputConfig.height = 576;
        } else if (!strcmp(value, "720p")) {
            mInputConfig.width = 1280;
            mInputConfig.height = 720;
        } else if (!strcmp(value, "1080p")) {
            mInputConfig.width = 1920;
            mInputConfig.height = 1080;
        } else {

            mInputConfig.width = 0;
            mInputConfig.height = 0;
            ALOGD("Not supported the input config: %s, using the default input %d x %d", value,
                    mInputConfig.width, mInputConfig.height);
        }

        ALOGI("%s: InputConfig size %d x %d", __func__,  mInputConfig.width, mInputConfig.height);
    }

    if (property_get(PROP_CAMERA_INPUT_FORMAT, value, NULL)) {
        if (!strcmp(value, "uyvy")) {
            mInputConfig.format = V4L2_PIX_FMT_UYVY;
        } else if (!strcmp(value, "yuy2")) {
            mInputConfig.format = V4L2_PIX_FMT_YUYV;
        } else {
            mInputConfig.format = -1;
            ALOGD("Not supported the input format: %s, using the default input format 0x%x", value,
                    mInputConfig.format);
        }
        ALOGI("%s: InputConfig format 0x%x", __func__, mInputConfig.format);
    }

    /*if (property_get(PROP_CAMERA_FIELD, value, NULL)) {
        if (atoi(value) == 0) {
            mField = V4L2_FIELD_ANY;
        } else if (atoi(value) == 1) {
            mField = V4L2_FIELD_ALTERNATE;
        } else {
            ALOGD("%s: Invalid field or doesn't set field, use default field value", __func__);
        }
        ALOGD("%s: mField %d", __func__, mField);
    }

    if (property_get(PROP_CAMERA_DEINTERLACE_MODE, value, NULL)) {
        if (atoi(value) == 0) {
            mDeinterlaceMode = DEINTERLACE_OFF;
        } else if (atoi(value) == 1) {
            mDeinterlaceMode = DEINTERLACE_WEAVING;
        } else {
            ALOGD("%s: Invalid deinterlace mode, use default value: DEINTERLACE_OFF", __func__);
        }
        ALOGD("%s: mDeinterlaceMode: %d", __func__, mDeinterlaceMode);
    }*/
}


/*===========================================================================
 * FUNCTION   : configureStreams
 *
 * DESCRIPTION: Reset HAL camera device processing pipeline and set up new input
 *              and output streams.
 *
 * PARAMETERS :
 *   @stream_list : streams to be configured
 *
 * RETURN     :
 *
 *==========================================================================*/
int Camera3HardwareInterface::configureStreams(
        camera3_stream_configuration_t *streamList)
{
    int rc = 0;
    camera3_stream_t *newStream = NULL;
    if(streamList == NULL)
        return -1;
    rc = checkStreams(streamList);
    if (rc != NO_ERROR)
        return rc;

    AutoMutex l(mLock);

    cleanStreamInfo();
    // Check state
    switch (mState) {
        case INITIALIZED:
            break;
        case CONFIGURED:
        case STARTED:
            //TODO: add hw stop/start if need hw reconfigure
            break;
        case ERROR:
            ALOGE("%s: stream is in ERROR state %d", __func__, mState);
            handleCameraDeviceError();
            return -ENODEV;
        default:
            ALOGE("%s: Invalid state %d", __func__, mState);
            return -ENODEV;
    }


    for (size_t i = 0; i < streamList->num_streams; i++) {
        newStream = streamList->streams[i];
        ALOGI("%s: stream[%d] type = %d, format = 0x%x, width = %d, "
                "height = %d, rotation = %d, usage = 0x%x",
                __func__, i, newStream->stream_type, newStream->format,
                newStream->width, newStream->height, newStream->rotation,
                newStream->usage);

        if (newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL ||
                newStream->stream_type == CAMERA3_STREAM_OUTPUT) {

            newStream->max_buffers = MAX_INFLIGHT_REQUESTS;
            newStream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE;
            if (IS_USAGE_VIDEO(newStream->usage))
                newStream->usage |= GRALLOC_USAGE_SW_READ_RARELY |
                                    GRALLOC_USAGE_SW_WRITE_RARELY |
                                    GRALLOC_USAGE_HW_CAMERA_WRITE;
        } else {
            ALOGE("%s: todo, input stream not supported!!", __func__);
        }

    }

    getInputConfig();

    rc = constructHWStreams(streamList);
    if (rc != NO_ERROR)
        return rc;

    icamera::camera_device_config_streams(mDeviceId, &mStreamList, &mInputConfig);

    if(!mMainStreamInfo)
        constructMainStream();
    rc = constructStreamInfo(streamList);
    if (rc != NO_ERROR)
        return rc;

    if (mState != STARTED) {
        for (pendingRequestIterator i = mPendingRequestsList.begin();
                i != mPendingRequestsList.end();) {
            i = erasePendingRequest(i);
        }

        mState = CONFIGURED;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : validateCaptureRequest
 *
 * DESCRIPTION: validate a capture request from camera service
 *
 * PARAMETERS :
 *   @request : request from framework to process
 *
 * RETURN     :
 *
 *==========================================================================*/
int Camera3HardwareInterface::validateCaptureRequest(
                    camera3_capture_request_t *request)
{
    ssize_t idx = 0;
    const camera3_stream_buffer_t *b;
    CameraMetadata meta;

    /* Sanity check the request */
    if (request == NULL) {
        ALOGE("NULL capture request");
        return BAD_VALUE;
    }

    if ((request->settings == NULL) && (mState == CONFIGURED)) {
        /*settings cannot be null for the first request*/
        return BAD_VALUE;
    }

    uint32_t frameNumber = request->frame_number;
    if (request->num_output_buffers < 1 || request->output_buffers == NULL) {
        ALOGE("%s: Request %d: No output buffers provided!",
                __func__, frameNumber);
        return BAD_VALUE;
    }
    if (request->num_output_buffers > MAX_NUM_STREAMS) {
        ALOGE("Number of buffers %d equals or is greater than maximum number of streams %d!",
                 request->num_output_buffers, MAX_NUM_STREAMS);
        return BAD_VALUE;
    }
    if (request->input_buffer != NULL) {
        b = request->input_buffer;
        if (b->status != CAMERA3_BUFFER_STATUS_OK) {
            ALOGE("Request %d: Buffer %ld: Status not OK!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        if (b->release_fence != -1) {
            ALOGE("Request %d: Buffer %ld: Has a release fence!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        if (b->buffer == NULL) {
            ALOGE("Request %d: Buffer %ld: NULL buffer handle!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
    }

    // Validate all buffers
    b = request->output_buffers;
    do {
        if (b->status != CAMERA3_BUFFER_STATUS_OK) {
            ALOGE("Request %d: Buffer %ld: Status not OK!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        if (b->release_fence != -1) {
            ALOGE("Request %d: Buffer %ld: Has a release fence!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        if (b->buffer == NULL) {
            ALOGE("Request %d: Buffer %ld: NULL buffer handle!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        if (*(b->buffer) == NULL) {
            ALOGE("Request %d: Buffer %ld: NULL private handle!",
                     frameNumber, (long)idx);
            return BAD_VALUE;
        }
        ALOGI("%s: request total buf num %d, buf[%d], fmt 0x%x, size %dx%d, frame id %d",
            __func__, request->num_output_buffers, idx, b->stream->format,
            b->stream->width, b->stream->height, request->frame_number);
        idx++;
        b = request->output_buffers + idx;
    } while (idx < (ssize_t)request->num_output_buffers);

    return NO_ERROR;
}

camera_metadata_t* Camera3HardwareInterface::constructMetadata(
    int64_t capture_time, camera_metadata_t *camMeta)
{
    CameraMetadata camMetadata;
    CameraMetadata reqMeta;
    camera_metadata_t *resultMetadata;
    int64_t exposure_time = 333333;
    int32_t sensitivity = 100;
    float lensAperture = 0.0;
    float focal_length = 0.0;

    uint8_t flash_state = ANDROID_FLASH_STATE_UNAVAILABLE;
    uint8_t af_state = ANDROID_CONTROL_AF_STATE_INACTIVE;
    uint8_t pipeline_depth = 1; //hard code, fix me
    static uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    static uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
    static uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    static uint8_t controlMode = ANDROID_CONTROL_MODE_OFF;
    static uint8_t aeMode = ANDROID_CONTROL_AE_MODE_OFF;
    static uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    static uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    static icamera::camera_window_t aeRegions;

    reqMeta = camMeta;

    if (reqMeta.exists(ANDROID_CONTROL_AWB_MODE))
        awbMode = reqMeta.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
    camMetadata.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    if (reqMeta.exists(ANDROID_CONTROL_AE_MODE))
        aeMode = reqMeta.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
    camMetadata.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    if (reqMeta.exists(ANDROID_CONTROL_AF_MODE))
        afMode = reqMeta.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
    camMetadata.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);

    if (reqMeta.exists(ANDROID_CONTROL_EFFECT_MODE))
        effectMode = reqMeta.find(ANDROID_CONTROL_EFFECT_MODE).data.u8[0];
    camMetadata.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    if (reqMeta.exists(ANDROID_CONTROL_MODE)) {
        controlMode = reqMeta.find(ANDROID_CONTROL_MODE).data.u8[0];
    }
    camMetadata.update(ANDROID_CONTROL_MODE, &controlMode, 1);

    if (reqMeta.exists(ANDROID_STATISTICS_FACE_DETECT_MODE)) {
        faceDetectMode = reqMeta.find(ANDROID_STATISTICS_FACE_DETECT_MODE).data.u8[0];
    }
    camMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    if (reqMeta.exists(ANDROID_CONTROL_AE_REGIONS)) {
        int32_t x_min = reqMeta.find(ANDROID_CONTROL_AE_REGIONS).data.i32[0];
        int32_t y_min = reqMeta.find(ANDROID_CONTROL_AE_REGIONS).data.i32[1];
        int32_t x_max = reqMeta.find(ANDROID_CONTROL_AE_REGIONS).data.i32[2];
        int32_t y_max = reqMeta.find(ANDROID_CONTROL_AE_REGIONS).data.i32[3];
        if( x_min < x_max) {
            aeRegions.weight = reqMeta.find( ANDROID_CONTROL_AE_REGIONS).data.i32[4];
            aeRegions.left = x_min;
            aeRegions.top = y_min;
            aeRegions.right = x_max;
            aeRegions.bottom = y_max;
        }
    }
    if (aeRegions.left != -1)
        camMetadata.update(ANDROID_CONTROL_AE_REGIONS, (int32_t*)&aeRegions, 5);

    camMetadata.update(ANDROID_SENSOR_TIMESTAMP, &capture_time, 1);
    camMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME, &exposure_time, 1);
    camMetadata.update(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);
    camMetadata.update(ANDROID_LENS_APERTURE, &lensAperture, 1);
    camMetadata.update(ANDROID_LENS_FOCAL_LENGTH, &focal_length, 1);
    camMetadata.update(ANDROID_FLASH_MODE, &flashMode, 1);
    camMetadata.update(ANDROID_CONTROL_AF_STATE, &af_state, 1);
    camMetadata.update(ANDROID_REQUEST_PIPELINE_DEPTH, &pipeline_depth, 1);
    camMetadata.update(ANDROID_FLASH_STATE, &flash_state, 1);

    resultMetadata = camMetadata.release();
    ALOGD("constructMetadata: metadata %p", resultMetadata);
    return resultMetadata;
}

Size Camera3HardwareInterface::getMaxJpegResolution()
{
    int32_t maxJpegWidth = 0, maxJpegHeight = 0;
    Size jpgSize;
    jpgSize.height = 0;
    jpgSize.width = 0;
    const int STREAM_CONFIGURATION_SIZE = 4;
    const int STREAM_FORMAT_OFFSET = 0;
    const int STREAM_WIDTH_OFFSET = 1;
    const int STREAM_HEIGHT_OFFSET = 2;
    const int STREAM_IS_INPUT_OFFSET = 3;

    camera_metadata_entry availableStreamConfigs =
            gCameraMetadata[mCameraId].find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    if (availableStreamConfigs.count == 0 ||
            availableStreamConfigs.count % STREAM_CONFIGURATION_SIZE) {
        return jpgSize;
    }

    // Get max jpeg size (area-wise).
    for (size_t i=0; i < availableStreamConfigs.count; i+= STREAM_CONFIGURATION_SIZE) {
        int32_t format = availableStreamConfigs.data.i32[i + STREAM_FORMAT_OFFSET];
        int32_t width = availableStreamConfigs.data.i32[i + STREAM_WIDTH_OFFSET];
        int32_t height = availableStreamConfigs.data.i32[i + STREAM_HEIGHT_OFFSET];
        int32_t isInput = availableStreamConfigs.data.i32[i + STREAM_IS_INPUT_OFFSET];
        if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT
                && format == HAL_PIXEL_FORMAT_BLOB &&
                (width * height > maxJpegWidth * maxJpegHeight)) {
            maxJpegWidth = width;
            maxJpegHeight = height;
        }
    }

    jpgSize.width = maxJpegWidth;
    jpgSize.height = maxJpegHeight;
    return jpgSize;
}

ssize_t Camera3HardwareInterface::getJpegBufSize(uint32_t width, uint32_t height)
{
    // Get max jpeg size (area-wise).
    Size maxJpegResolution = getMaxJpegResolution();
    if (maxJpegResolution.width == 0) {
        ALOGE("%s: Can't find valid available jpeg sizes in static metadata!",
                __FUNCTION__);
        return BAD_VALUE;
    }

    // Get max jpeg buffer size
    ssize_t maxJpegBufferSize = 0;
    camera_metadata_entry jpegBufMaxSize =
        gCameraMetadata[mCameraId].find(ANDROID_JPEG_MAX_SIZE);
    if (jpegBufMaxSize.count == 0) {
        ALOGE("%s: Can't find maximum JPEG size in static metadata!",
            __FUNCTION__);
        return BAD_VALUE;
    }
    maxJpegBufferSize = jpegBufMaxSize.data.i32[0];
    assert(kMinJpegBufferSize < maxJpegBufferSize);

    // Calculate final jpeg buffer size for the given resolution.
    float scaleFactor = ((float) (width * height)) /
            (maxJpegResolution.width * maxJpegResolution.height);
    ssize_t jpegBufferSize =
        scaleFactor * (maxJpegBufferSize - minJpgBufSize) + minJpgBufSize;
    if (jpegBufferSize > maxJpegBufferSize) {
        jpegBufferSize = maxJpegBufferSize;
    }

    ALOGI("%dx%d, maxjpeg %dx%d, scalefactor %f, maxjpg %d, minjpg %d",
        width, height,
        maxJpegResolution.width, maxJpegResolution.height,
        scaleFactor, (int)maxJpegBufferSize, (int)minJpgBufSize);
    ALOGI("jpgebuffer size %d", (int)jpegBufferSize);


    return jpegBufferSize;
}

status_t Camera3HardwareInterface::setJpegParameters(CameraMetadata& meta)
{
    status_t ret = NO_ERROR;

    ALOGI("%s :", __func__);
    icamera::Parameters new_param;
    mJpegParameter = new_param;
    mJpegParameter.setJpegThumbnailQuality(JPEG_QUALITY);
    mJpegParameter.setJpegRotation(0);

    if (meta.exists(ANDROID_JPEG_GPS_COORDINATES)) {
        double* data  = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d;
        mJpegParameter.setJpegGpsCoordinates(data);
    }

    if (meta.exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
        int64_t data  = meta.find(ANDROID_JPEG_GPS_TIMESTAMP).data.i64[0];
        mJpegParameter.setJpegGpsTimeStamp(data);
    }

    if (meta.exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
        const char* data  = (const char*)meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8;
        if (data != NULL) {
            mJpegParameter.setJpegGpsProcessingMethod(data);
        }
    }
    mJpegParameter.setFocalLength(0);
    return ret;
}

int Camera3HardwareInterface::jpegSwEncode(
    camera3_stream_buffer_t* src, camera3_stream_buffer_t* dst)
{
    if(!(src && src->stream && dst && dst->stream)) {
        ALOGE("%s, invalid input parameters", __func__);
        return 0;
    }
    icamera::InputBuffer inBuf;
    icamera::OutputBuffer outBuf;
    void *srcdata, *dstdata;
#if defined(USE_CROS_GRALLOC)
    uint32_t width, height, stride;
#else
    intel_ufo_buffer_details_t src_info;
#endif // USE_CROS_GRALLOC

    uint32_t srcWidth, srcHeight, srcSize, srcFmt;
    uint32_t dstWidth, dstHeight, dstSize;

#if defined(USE_CROS_GRALLOC)
    srcdata = camera3bufLock(src, &width, &height, &stride);
    dstdata = camera3bufLock(dst, NULL, NULL, NULL);
#else
    srcdata = camera3bufLock(src, &src_info);
    dstdata = camera3bufLock(dst, NULL);
#endif // USE_CROS_GRALLOC

    srcWidth = src->stream->width;
    srcHeight = src->stream->height;
    srcSize = srcWidth * srcHeight * 2; //FIXME, 2 is a hardcode.
    srcFmt = HalFormat2V4L2Format(src->stream->format);

    dstWidth = dst->stream->width;
    dstHeight = dst->stream->height;
    dstSize = dstWidth * dstHeight * 2; //FIXME, 2 is a hardcode.

    inBuf.buf = (unsigned char*)srcdata;
    inBuf.width = srcWidth;
    inBuf.height = srcHeight;
    inBuf.fourcc = srcFmt;
    inBuf.size = srcSize;

#if defined(USE_CROS_GRALLOC)
    inBuf.stride = stride;

    ALOGI("%s: input %dx%d, size %d, fmt 0x%x, stirde %d, buf %p",
        __func__, srcWidth, srcHeight, srcSize, srcFmt, stride, srcdata);
#else
    inBuf.stride = src_info.pitch;

    ALOGI("%s: input %dx%d, size %d, fmt 0x%x, stirde %d, buf %p",
        __func__, srcWidth, srcHeight, srcSize, srcFmt, src_info.pitch, srcdata);
#endif // USE_CROS_GRALLOC

    outBuf.buf = (unsigned char*)dstdata;
    outBuf.width = dstWidth;
    outBuf.height = dstHeight;
    outBuf.quality = JPEG_QUALITY;
    outBuf.size = dstSize;

    ALOGI("%s: output %dx%d, size %d, buf %p",
        __func__, dstWidth, dstHeight, dstSize, dstdata);

    nsecs_t startTime = systemTime();
    int size = icamera::camera_jpeg_encode(inBuf, outBuf);

    camera3bufUnlock(src);
    camera3bufUnlock(dst);

    ALOGI("%s: encoding %dx%d need %ums, jpeg size %d, quality %d)", __FUNCTION__,
         outBuf.width, outBuf.height,
        (unsigned)((systemTime() - startTime) / 1000000), size, outBuf.quality);
    return size;
}

int Camera3HardwareInterface::exifMake(
    camera3_stream_buffer_t* src, camera3_stream_buffer_t* dst, int jpgsize)
{
    if(!(src && src->stream && dst && dst->stream)) {
        ALOGE("%s, invalid input parameters", __func__);
        return BAD_VALUE;
    }
    int ret;
    int srcWidth, srcHeight, srcSize;
    int dstWidth, dstHeight, dstSize;
    icamera::EncodePackage package;
    icamera::camera_buffer_t srcbuf, dstbuf;

    srcWidth = src->stream->width;
    srcHeight = src->stream->height;
    srcSize = srcWidth * srcHeight * 2; //FIXME, 2 is hard code.
    dstWidth = dst->stream->width;
    dstHeight = dst->stream->height;
    dstSize = getJpegBufSize(dstWidth, dstHeight);

#if defined(USE_CROS_GRALLOC)
    srcbuf.addr = camera3bufLock(src, NULL, NULL, NULL);
    dstbuf.addr = camera3bufLock(dst, NULL, NULL, NULL);
#else
    srcbuf.addr = camera3bufLock(src, NULL);
    dstbuf.addr = camera3bufLock(dst, NULL);
#endif // USE_CROS_GRALLOC

    package.main = &srcbuf;
    package.mainWidth = srcWidth;
    package.mainHeight = srcHeight;
    package.mainSize = srcSize;
    package.encodedDataSize = jpgsize;

    package.jpegOut = &dstbuf;
    package.jpegSize = dstSize;
    package.params = &mJpegParameter;

    icamera::ExifMetaData *exifData = new icamera::ExifMetaData;
    ret = icamera::camera_setupExifWithMetaData(package, exifData);
    if (ret != OK) {
        ALOGE("Set up exif Failed");
        return ret;
    }

    ALOGI("%s: package %dx%d, main size 0x%x, encode size %d, jpg size %d",
        __func__,
        package.mainWidth, package.mainHeight,
        package.mainSize, package.encodedDataSize,
        package.jpegSize);

    // create a full JPEG image with exif data
    ret = icamera::camera_jpeg_make(package);
    if (ret != NO_ERROR) {
        ALOGE("%s: Make Jpeg Failed !", __FUNCTION__);
    }

#if 0
    // First check for JPEG transport header at the end of the buffer
    header = (uint8_t*)(dstbuf->addr) + (dstSize - sizeof(struct camera3_jpeg_blob));
    blob = (struct camera3_jpeg_blob*)(header);
    blob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    blob->jpeg_size = package.encodedDataSize + EXIF_FILE_SIZE;
#endif

    camera3bufUnlock(src);
    camera3bufUnlock(dst);

    delete exifData;
    return ret;
}

#if defined(USE_CROS_GRALLOC)
void* Camera3HardwareInterface::camera3bufLock(camera3_stream_buffer_t* buf, uint32_t* pWidth,
                                               uint32_t* pHeight, uint32_t* pStride)
#else
void* Camera3HardwareInterface::camera3bufLock(camera3_stream_buffer_t* buf,  intel_ufo_buffer_details_t* buffer_info)
#endif // USE_CROS_GRALLOC
{
    void *data = NULL;
    int width;
    int height;
    status_t status = OK;

    if (buf == NULL) {
        ALOGE("%s: buffer is null!", __func__);
        return NULL;
    }

    int flags = buf->stream->usage & (GRALLOC_USAGE_SW_READ_MASK |
        GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_CAMERA_MASK);
    flags = flags | GRALLOC_USAGE_SW_READ_OFTEN |
        GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_COMPOSER;

    width = buf->stream->width;
    height = buf->stream->height;

    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    const Rect bounds(width, height);

    status = mapper.lock(*buf->buffer, flags, bounds, &data);
    if (status != NO_ERROR) {
       ALOGE("ERROR @%s: Failed to lock GraphicBufferMapper! %d", __func__, status);
       mapper.unlock(*buf->buffer);
       return NULL;
    }

#if defined(USE_CROS_GRALLOC)
    getNativeHandleDimensions(buf->buffer, pWidth, pHeight, pStride);
#else
    //TODO: find out a better way to get the buffer info in future
    if (!getBufferInfo(buf->buffer, buffer_info)) {
        ALOGE("%s: failed to retrieve the gralloc buffer info!", __func__);
    }
#endif // USE_CROS_GRALLOC
    return data;
}

void Camera3HardwareInterface::camera3bufUnlock(camera3_stream_buffer_t* buf)
{
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    mapper.unlock(*buf->buffer);
}

/*===========================================================================
 * FUNCTION   : handleMetadataWithLock
 *
 * DESCRIPTION: Handles metadata buffer callback with mLock held.
 *
 * PARAMETERS : @metadata_buf: metadata buffer
 *              @free_and_bufdone_meta_buf: Buf done on the meta buf and free
 *                 the meta buf in this method
 *
 * RETURN     :
 *
 *==========================================================================*/
void Camera3HardwareInterface::processCaptureResult(const camera3_stream_buffer_t *buf,
             uint32_t frame_number, uint64_t capture_time)
{
    PendingRequestInfo* req = NULL;
    pendingRequestIterator reqi;
    pendingBufferIterator j;
    CameraMetadata camMeta;
    camera3_stream_buffer_t *pMainBuffer = NULL;

    ALOGD("%s: Entry", __func__);

    /* Nothing to be done during error state */
    if ((ERROR == mState) || (DEINIT == mState)) {
        return;
    }

    mRequestCond.signal();

    if ((ERROR == mState) || (DEINIT == mState)) {
        //during flush do not send metadata from this thread
        ALOGI("%s: not sending metadata during flush or when mState is error", __func__);
        return;
    }

    for (reqi = mPendingRequestsList.begin(); reqi != mPendingRequestsList.end(); reqi++) {
        for (pendingBufferIterator j = reqi->buffers.begin();
            j != reqi->buffers.end(); j++) {
            if (j->handle == buf->buffer) {
                req = &(*reqi);
                break;
            }
        }
        if (req) {
            ALOGI("%s: found buf and its req, wait for graphic ready", __func__);
            for (pendingBufferIterator j = reqi->buffers.begin(); j != reqi->buffers.end(); j++) {
                int fence = j->buffer->acquire_fence;
                if (fence != -1) {
                    sp<Fence> bufferAcquireFence = new Fence(fence);
                    if (bufferAcquireFence->wait(2000) == ETIME) {
                        ALOGE("%s: Fence timed out after 2000 ms", __func__);
                    }
                    bufferAcquireFence = NULL;
                }
            }
            break;
        }
    }

    if(!req) {
        mState = ERROR;
        goto done_metadata;
    }

    if (mMainStreamInfo != NULL && mMainStreamInfo->main_hal_buf != NULL) {
        BufferPackage *buf_pack;
        buf_pack = mMainStreamInfo->main_hal_buf;

       if (buf->status == CAMERA3_BUFFER_STATUS_INTERNAL) {
            for(int i = 0; i < mMainStreamInfo->main_hal_bufnum; i++) {
                if(buf_pack[i].nativeHalBuf->buffer == buf->buffer) {
                    buf_pack[i].flag = 0;
                    break;
                }
            }
        }
    } else {
        ALOGE("%s: mMainStreamInfo may not initialized.", __func__);
    }
    camMeta = req->settings;

    if (req->frame_number != frame_number) {
        ALOGE("%s: Fatal: frame %d's result and request does not match!",
            __func__, req->frame_number);
        //mState = ERROR;
        //goto done_metadata;
    }

    camera3_capture_result_t result;
    camera3_notify_msg_t notify_msg;
    memset(&result, 0, sizeof(camera3_capture_result_t));
    memset(&notify_msg, 0, sizeof(camera3_notify_msg_t));

    mPendingLiveRequest--;

    notify_msg.type = CAMERA3_MSG_SHUTTER;
    notify_msg.message.shutter.frame_number = req->frame_number;
    notify_msg.message.shutter.timestamp = (uint64_t)capture_time;
    mCallbackOps->notify(mCallbackOps, &notify_msg);

    result.result = constructMetadata(capture_time, req->settings);
    if (!result.result) {
        ALOGE("%s: metadata is NULL", __func__);
        mState = ERROR;
        goto done_metadata;
    }

    result.partial_result = 1;
    result.frame_number = req->frame_number;
    result.input_buffer = NULL;
    result.num_output_buffers = 0;
    result.output_buffers = NULL;

    for (pendingBufferIterator j = req->buffers.begin();
                 j != req->buffers.end(); j++) {
        result.num_output_buffers++;
    }

    if (buf == NULL) {
        ALOGE("%s: main stream buffer is NULL", __func__);
        mState = ERROR;
        goto done_metadata;
    }
    pMainBuffer = (camera3_stream_buffer_t*)buf;

    if (result.num_output_buffers > 0) {
        camera3_stream_buffer_t *result_buffers =
            new camera3_stream_buffer_t[result.num_output_buffers];

        if (!result_buffers) {
            ALOGE("%s: failed to allocate result buffer!", __func__);
            mState = ERROR;
            goto done_metadata;
        }
        size_t buf_id = 0;
        int jpgsize;
        camera3_stream_buffer_t *jpgbuf;
        stream_info_t *streaminfo;
        for (pendingBufferIterator j = req->buffers.begin();
                j != req->buffers.end(); j++) {

            camera3_stream_buffer_t *pScaledBuffer = pMainBuffer;
            streaminfo = getStreamInfo(j->buffer->stream);
            if (streaminfo == NULL) {
                ALOGE("%s can not find stream info", __func__);
                continue;
            }
            if (streaminfo->ivpconvert) {
                BufferPackage bufpack;
                bufpack.nativeWinBuf = streaminfo->ivpbuf;
                bufpack.nativeHalBuf = pMainBuffer;
                mGenConvert->downScalingAndColorConversion(bufpack);
                pScaledBuffer = streaminfo->ivpbuf;
            }
            if (streaminfo->jpgencoder) {
                jpgbuf = streaminfo->jpgbuf;
                jpgsize = jpegSwEncode(pScaledBuffer, jpgbuf);
                exifMake(jpgbuf, j->buffer, jpgsize);
            } else if (streaminfo->hwencoder){
                copyYuvData(pScaledBuffer, j->buffer);
            } else {
                void *pDataSrc = NULL;
                void *pDataDst = NULL;

                pDataSrc = camera3bufLock(pScaledBuffer, NULL, NULL, NULL);
                pDataDst = camera3bufLock(j->buffer, NULL, NULL, NULL);
                memcpy(pDataDst, pDataSrc,
                    getNativeHandleSize(j->buffer->buffer, j->buffer->stream->format));

                camera3bufUnlock(pScaledBuffer);
                camera3bufUnlock(j->buffer);
            }

            result_buffers[buf_id] = *(j->buffer);
            result_buffers[buf_id].release_fence = -1;
            buf_id++;
        }

        result.output_buffers = result_buffers;
        mCallbackOps->process_capture_result(mCallbackOps, &result);
        ALOGI("%s: result frame %u, out frame num %d, capture_time = %lld,",
                __func__, result.frame_number, result.num_output_buffers, capture_time);

        free_camera_metadata((camera_metadata_t *)result.result);
        delete[] result_buffers;
        for (pendingBufferIterator j = req->buffers.begin();
                j != req->buffers.end(); j++) {
            delete j->buffer;
            req->buffers.erase(j);
        }
    } else {
        mCallbackOps->process_capture_result(mCallbackOps, &result);
        ALOGI("%s: result frame without buffer %u, out frame num %d, capture_time = %lld,",
                __func__, result.frame_number, result.num_output_buffers, capture_time);
        free_camera_metadata((camera_metadata_t *)result.result);
    }
    erasePendingRequest(reqi);

done_metadata:
    ALOGD("%s: mPendingLiveRequest = %d", __func__, mPendingLiveRequest);
    unblockRequestIfNecessary();
}

/*===========================================================================
 * FUNCTION   : unblockRequestIfNecessary
 *
 * DESCRIPTION: Unblock capture_request if max_buffer hasn't been reached. Note
 *              that mLock is held when this function is called.
 *
 * PARAMETERS :
 *
 * RETURN     :
 *
 *==========================================================================*/
void Camera3HardwareInterface::unblockRequestIfNecessary()
{
   // Unblock process_capture_request
   mRequestCond.signal();
}

/*===========================================================================
 * FUNCTION   : processCaptureRequest
 *
 * DESCRIPTION: process a capture request from camera service
 *
 * PARAMETERS :
 *   @request : request from framework to process
 *
 * RETURN     :
 *
 *==========================================================================*/
int Camera3HardwareInterface::processCaptureRequest(
                    camera3_capture_request_t *request)
{
    int rc = NO_ERROR;
    int32_t request_id;
    CameraMetadata camMeta;
    uint32_t minInFlightRequests = MIN_INFLIGHT_REQUESTS;

    AutoMutex   l(mLock);

    // Validate current state
    switch (mState) {
        case CONFIGURED:
        case STARTED:
            /* valid state */
            break;

        case ERROR:
            handleCameraDeviceError();
            return -ENODEV;

        default:
            ALOGE("Invalid state %d", mState);
            return -ENODEV;
    }

    rc = validateCaptureRequest(request);
    if (rc != NO_ERROR) {
        ALOGE("incoming request is not valid");
        return rc;
    }

    camMeta = request->settings;

    if (mState == CONFIGURED) {
        ALOGI("%s: First Request", __func__);
        mPendingLiveRequest = 0;
        mFirstConfiguration = true;
    }

    uint32_t frameNumber = request->frame_number;

    if (camMeta.exists(ANDROID_REQUEST_ID)) {
        request_id = camMeta.find(ANDROID_REQUEST_ID).data.i32[0];
        mCurrentRequestId = request_id;
        ALOGD("%s: Received request with id: %d", __func__, request_id);
    } else if (mState == CONFIGURED || mCurrentRequestId == -1){
        ALOGE("Unable to find request id field, \
                & no previous id available");
        return NAME_NOT_FOUND;
    } else {
        request_id = mCurrentRequestId;
        ALOGD("%s: Re-using old request id, %d", __func__, request_id);
    }

    if (request->input_buffer) {
        ALOGE("%s: input buffer is not supported!", __func__);
        return INVALID_OPERATION;
    }

    setJpegParameters(camMeta);

    /* Update pending request list and pending buffers map */
    PendingRequestInfo pendingRequest;
    pendingRequestIterator latestRequest;
    pendingRequest.frame_number = frameNumber;
    pendingRequest.num_buffers = request->num_output_buffers;
    pendingRequest.request_id = request_id;
    pendingRequest.timestamp = 0;

    pendingRequest.partial_result_cnt = 0;
    pendingRequest.shutter_notified = false;
    pendingRequest.settings = clone_camera_metadata(request->settings);

    buffer_handle_t* pHandle = NULL;
    if (request->num_output_buffers > 0) {
        camera3_stream_buffer_t* halbuf = getMainHalBuf();
        if (halbuf != NULL) {
            pHandle = halbuf->buffer;
            if(mMainStreamInfo) {
                Camera3Channel* channel = (Camera3Channel *)mMainStreamInfo->channel;
                if(channel)
                    channel->queueBuf(halbuf, channel->getStreamid(), frameNumber);
            }
        } else {
            ALOGE("%s: getMainHalBuf return null.", __func__);
        }
    }

    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        RequestedBufferInfo requestedBuf;
        memset(&requestedBuf, 0, sizeof(requestedBuf));
        requestedBuf.stream = request->output_buffers[i].stream;
        requestedBuf.buffer = new camera3_stream_buffer_t;
        memcpy(requestedBuf.buffer, &request->output_buffers[i], sizeof(camera3_stream_buffer_t));
        requestedBuf.handle = pHandle;
        pendingRequest.buffers.push_back(requestedBuf);
    }

    latestRequest = mPendingRequestsList.insert(
            mPendingRequestsList.end(), pendingRequest);
    if(mFlush) {
        return NO_ERROR;
    }
    if (mFirstConfiguration) {
        //start the device after queue the first buffer
        icamera::camera_device_start(mDeviceId);
        mFirstConfiguration = false;
        if (mMainStreamInfo) {
            Camera3Channel *channel = (Camera3Channel *)mMainStreamInfo->channel;
            if (channel != NULL)
                rc |= channel->start();//start to do the DQ

            if (rc < 0) {
                ALOGE("%s: channel init/start failed", __func__);
                return rc;
            }
        }
    }

    if(request->output_buffers != NULL) {
        mPendingLiveRequest++;
    }

    mState = STARTED;

    //Block on conditional variable
    while ((mPendingLiveRequest >= minInFlightRequests) &&
            (mState != ERROR) && (mState != DEINIT)) {
        ALOGW("%s: wait until the %d pending requests are handled...",
            __func__, mPendingLiveRequest);
        mRequestCond.waitRelative(mLock, kWaitDuration);
        if (rc == ETIMEDOUT) {
            rc = -ENODEV;
            ALOGE("%s: Unblocked on timeout!!!!", __func__);
            break;
        }
        ALOGW("%s: Unblocked", __func__);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
void Camera3HardwareInterface::dump(int fd)
{
    AutoMutex l(mLock);
    dprintf(fd, "\n Camera HAL3 information Begin \n");

    dprintf(fd, "\nNumber of pending requests: %zu \n",
        mPendingRequestsList.size());
    dprintf(fd, "-------+-------------------+-------------+----------+---------------------\n");
    dprintf(fd, " Frame | Number of Buffers |   Req Id:   | Blob Req | Input buffer present\n");
    dprintf(fd, "-------+-------------------+-------------+----------+---------------------\n");
    for(pendingRequestIterator i = mPendingRequestsList.begin();
            i != mPendingRequestsList.end(); i++) {
        dprintf(fd, " %5d | %17d | %11d \n",
        i->frame_number, i->num_buffers, i->request_id);
    }
    dprintf(fd, "-------+-----------\n");
    dprintf(fd, "\n Camera HAL3 information End \n");

    /* use dumpsys media.camera as trigger to send update debug level event */
    return;
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION: Calls stopAllChannels, notifyErrorForPendingRequests and
 *              conditionally restarts channels
 *
 * PARAMETERS :
 *  @ restartChannels: re-start all channels
 *
 *
 * RETURN     :
 *          0 on success
 *          Error code on failure
 *==========================================================================*/
int Camera3HardwareInterface::flush()
{
    int32_t rc = NO_ERROR;

    ALOGD("Unblocking Process Capture Request");
    mLock.lock();
    mFlush = true;
    mLock.unlock();

    rc = stopAllChannels();
    if (rc < 0) {
        ALOGE("stopAllChannels failed");
        return rc;
    }

    // Mutex Lock
    AutoMutex   l(mLock);

    // Unblock process_capture_request
    mPendingLiveRequest = 0;
    mRequestCond.signal();

    rc = notifyErrorForPendingRequests();
    if (rc < 0) {
        ALOGE("notifyErrorForPendingRequests failed");
        return rc;
    }

    mFlush = false;
    mFirstConfiguration = true;
    return 0;
}

/*===========================================================================
 * FUNCTION   : cleanStreamInfo
 *
 * DESCRIPTION: helper method to clean up invalid streams in stream_info,
 *
 * PARAMETERS : None
 *
 *==========================================================================*/
void Camera3HardwareInterface::cleanStreamInfo()
{
    /*clean up invalid streams*/
    List<stream_info_t*>::iterator it = mStreamInfo.begin();
    for (; it != mStreamInfo.end(); it++) {
        if ((*it)->ivpbuf)
            deallocateHALBuf((*it)->ivpbuf);
        if (IS_USAGE_SWREADER((*it)->reqstream->usage))
            deallocateJPEGBuf(*it);
        if ((*it)->main_hal_stream)
            delete (*it)->main_hal_stream;
        delete *it;
    }

    mStreamInfo.clear();
}

/*===========================================================================
 * FUNCTION   : startAllChannels
 *
 * DESCRIPTION: This function starts (equivalent to stream-on) all channels
 *
 * PARAMETERS : None
 *
 * RETURN     : NO_ERROR on success
 *              Error codes on failure
 *
 *==========================================================================*/
int32_t Camera3HardwareInterface::startAllChannels()
{
    int32_t rc = NO_ERROR;

    ALOGD("Start all channels ");

    for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        Camera3Channel *channel = (Camera3Channel *)(*it)->reqstream->priv;
        if (channel) {
            rc = channel->start();
            if (rc < 0) {
                ALOGE("channel start failed");
                return rc;
            }
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : stopAllChannels
 *
 * DESCRIPTION: This function stops (equivalent to stream-off) all channels
 *
 * PARAMETERS : None
 *
 * RETURN     : NO_ERROR on success
 *              Error codes on failure
 *
 *==========================================================================*/
int32_t Camera3HardwareInterface::stopAllChannels()
{
    int32_t rc = NO_ERROR;

    ALOGD("Stopping all channels");

    //stop the camera device
    icamera::camera_device_stop(mDeviceId);
    ALOGD("%s: camera device has been stopped", __func__);

    for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        (*it)->status = INVALID;
    }
    // stop the Streams/Channels
    if (mMainStreamInfo) {
        Camera3Channel *channel = (Camera3Channel *)mMainStreamInfo->channel;
        if (channel) {
            channel->stop();
            //delete channel;
        }
    }

    ALOGD("All channels stopped");
    return rc;
}

/*===========================================================================
 * FUNCTION   : handleCameraDeviceError
 *
 * DESCRIPTION: This function calls internal flush and notifies the error to
 *              framework and updates the state variable.
 *
 * PARAMETERS : None
 *
 * RETURN     : NO_ERROR on Success
 *              Error code on failure
 *==========================================================================*/
int32_t Camera3HardwareInterface::handleCameraDeviceError()
{
    int32_t rc = NO_ERROR;

    {
        AutoMutex l(mLock);
        if (mState != ERROR) {
            //if mState != ERROR, nothing to be done
            return NO_ERROR;
        }
    }

    rc = flush();
    if (NO_ERROR != rc) {
        ALOGE("internal flush to handle mState = ERROR failed");
    }

    mLock.lock();
    mState = DEINIT;
    mLock.unlock();

    camera3_notify_msg_t notify_msg;
    memset(&notify_msg, 0, sizeof(camera3_notify_msg_t));
    notify_msg.type = CAMERA3_MSG_ERROR;
    notify_msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
    notify_msg.message.error.error_stream = NULL;
    notify_msg.message.error.frame_number = 0;
    mCallbackOps->notify(mCallbackOps, &notify_msg);

    return rc;
}

/*===========================================================================
 * FUNCTION   : captureResultCb
 *
 * DESCRIPTION: Callback handler for all capture result
 *              (streams, as well as metadata)
 *
 * PARAMETERS :
 *   @metadata : metadata information
 *   @buffer   : actual gralloc buffer to be returned to frameworks.
 *               NULL if metadata.
 *
 * RETURN     : NONE
 *==========================================================================*/
void Camera3HardwareInterface::captureResultCb(icamera::Parameters *metadata_buf,
                const camera3_stream_buffer_t *buffer, uint32_t frame_number, uint64_t timestamp)
{
    AutoMutex l(mLock);

    if (metadata_buf) {
        processCaptureResult(buffer, frame_number, timestamp);
    } else {
        ALOGE("%s: metadata is missing!", __func__);
    }

    return;
}

/*===========================================================================
 * FUNCTION   : addStreamConfig
 *
 * DESCRIPTION: adds the stream configuration to the array
 *
 * PARAMETERS :
 * @available_stream_configs : pointer to stream configuration array
 * @scalar_format            : scalar format
 * @dim                      : configuration dimension
 * @config_type              : input or output configuration type
 *
 * RETURN     : NONE
 *==========================================================================*/
void Camera3HardwareInterface::addStreamConfig(Vector<int32_t> &available_stream_configs,
        int32_t scalar_format, const cam_dimension_t &dim, int32_t config_type)
{
    available_stream_configs.add(scalar_format);
    available_stream_configs.add(dim.width);
    available_stream_configs.add(dim.height);
    available_stream_configs.add(config_type);
}

/*===========================================================================
 * FUNCTION   : initStaticMetadata
 *
 * DESCRIPTION: initialize the static metadata
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              non-zero failure code
 *==========================================================================*/
int Camera3HardwareInterface::initStaticMetadata(uint32_t cameraId)
{
    int rc = 0;
    CameraMetadata staticInfo;

    bool limitedDevice = true;

    /* If sensor is YUV sensor (no raw support) or if per-frame control is not
     * guaranteed or if min fps of max resolution is less than 20 fps, its
     * advertised as limited device*/

    uint8_t supportedHwLvl = limitedDevice ?
                ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY :
                ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL;
    staticInfo.update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
            &supportedHwLvl, 1);

    uint8_t lensFacing = ANDROID_LENS_FACING_BACK;
    staticInfo.update(ANDROID_LENS_FACING, &lensFacing, 1);
    int32_t sensor_orientation = 0;
    staticInfo.update(ANDROID_SENSOR_ORIENTATION,&sensor_orientation, 1);

    int32_t max_output_streams[] = {0, 2, 1};
    staticInfo.update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                      max_output_streams,
                      sizeof(max_output_streams)/sizeof(max_output_streams[0]));
    uint8_t avail_leds = 0;
    staticInfo.update(ANDROID_LED_AVAILABLE_LEDS, &avail_leds, 0);

    int64_t max_frame_duration = NSEC_PER_100MSEC;
    staticInfo.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &max_frame_duration, 1);

    uint8_t avail_af_modes[1];
    avail_af_modes[0] = ANDROID_CONTROL_AF_MODE_OFF;
    staticInfo.update(ANDROID_CONTROL_AF_AVAILABLE_MODES, avail_af_modes, 1);

    uint8_t avail_ae_modes[1];
    avail_ae_modes[0] = ANDROID_CONTROL_AE_MODE_ON;
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_MODES, avail_ae_modes, 1);

    uint8_t avail_awb_modes[1];
    avail_awb_modes[0] = ANDROID_CONTROL_AWB_MODE_AUTO;
    staticInfo.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES, avail_awb_modes, 1);

    int32_t max_latency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
    staticInfo.update(ANDROID_SYNC_MAX_LATENCY, &max_latency, 1);

    float max_digital_zoom = 1;
    staticInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &max_digital_zoom, 1);

    uint8_t avail_scene_modes[1];
    avail_scene_modes[0] = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, avail_scene_modes, 1);

    int32_t max_jpeg_size = 13 * 1024 * 1024;
    staticInfo.update(ANDROID_JPEG_MAX_SIZE, &max_jpeg_size, 1);

    int32_t active_array_size[] = {0, 0, 640, 480};
    staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size, 4);

    int32_t ae_compensation_range[] = {-1, 1};
    staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE, ae_compensation_range, 2);

    camera_metadata_rational_t ae_compensation_step;
    ae_compensation_step.numerator = 1;
    ae_compensation_step.denominator = 2;
    staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_STEP, &ae_compensation_step, 1);

    uint8_t flash_info_available = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    staticInfo.update(ANDROID_FLASH_INFO_AVAILABLE, &flash_info_available, 1);

    int32_t ae_avail_target_fps[] = {10, 30, 30, 30, 30, 60, 60, 60, 10, 60};
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, ae_avail_target_fps, 10);

    float avail_focal_lengths[] = {0.0};
    staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, avail_focal_lengths, 1);

    int32_t avail_thumbnail_size[] = {0, 0, 180, 120, 270, 180, 360, 240};
    staticInfo.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, avail_thumbnail_size, 4);

    int32_t control_max_regions[] = {1, 0, 0};
    staticInfo.update(ANDROID_CONTROL_MAX_REGIONS, control_max_regions, 3);

    float physical_size[] = {640.0, 480.0};
    staticInfo.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physical_size, 2);

    int32_t pixel_array_size[] = {640, 480};
    staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array_size, 2);

    uint8_t avail_video_stablization_modes = 0; //OFF
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, &avail_video_stablization_modes, 1);

    uint8_t avail_face_detect_mode = 0;
    staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, &avail_face_detect_mode, 1);

    uint8_t antibanding_mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, &antibanding_mode, 1);

    uint8_t max_pipeline_depth = (uint8_t)(MAX_INFLIGHT_REQUESTS + EMPTY_PIPELINE_DELAY + FRAME_SKIP_DELAY);
    staticInfo.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &max_pipeline_depth, 1);

    int32_t testPattenModes = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    staticInfo.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, &testPattenModes, 1);

    uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_EFFECTS, &effectMode, 1);

    int32_t streamConfigurationsBasic[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 800, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 352, 288, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 320, 240, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 176, 144, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,

        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 800, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 352, 288, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 320, 240, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 176, 144, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,

        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 1280, 800, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 352, 288, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 320, 240, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 176, 144, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT
    };
    size_t streamConfigurationsCnt =
            sizeof(streamConfigurationsBasic)/sizeof(streamConfigurationsBasic[0]);
    Vector<int32_t> streamConfigurations;
    streamConfigurations.appendArray(streamConfigurationsBasic, streamConfigurationsCnt);
    staticInfo.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            streamConfigurations.array(),
            streamConfigurations.size());

    int64_t minFrameDurationsBasic[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, 16646000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 800, 33320000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, 33320000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480, 33320000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 352, 288, 33320000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 320, 240, 33320000,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 176, 144, 33320000,

        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, 16646000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 800, 33320000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, 33320000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, 33320000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 352, 288, 33320000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 320, 240, 33320000,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 176, 144, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 1280, 800, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 352, 288, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 320, 240, 33320000,
        HAL_PIXEL_FORMAT_BLOB, 176, 144, 33320000
    };
    size_t minFrameDurationsBasicCnt =
            sizeof(minFrameDurationsBasic)/sizeof(minFrameDurationsBasic[0]);
    Vector<int64_t> minFrameDurations;
    minFrameDurations.appendArray(minFrameDurationsBasic, minFrameDurationsBasicCnt);
    staticInfo.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
            minFrameDurations.array(),
            minFrameDurations.size());

    int64_t stallDurationsBasic[] = {
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 1280, 800, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 352, 288, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 320, 240, 41666666,
        HAL_PIXEL_FORMAT_BLOB, 176, 144, 41666666
    };
    size_t stallDurationsBasicCnt =
            sizeof(stallDurationsBasic)/sizeof(stallDurationsBasic[0]);
    Vector<int64_t> stallDurations;
    stallDurations.appendArray(stallDurationsBasic, stallDurationsBasicCnt);
    staticInfo.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
            stallDurations.array(),
            stallDurations.size());

    Vector<uint8_t> available_capabilities;
    available_capabilities.add(ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
            available_capabilities.array(),
            available_capabilities.size());

    uint8_t aeLockAvailable=ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
    staticInfo.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLockAvailable, 1);

    uint8_t awbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
    staticInfo.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLockAvailable, 1);

    int32_t request_keys_basic[] = {
       ANDROID_CONTROL_AE_LOCK,
       ANDROID_CONTROL_AWB_LOCK,
       ANDROID_SENSOR_FRAME_DURATION,
       ANDROID_CONTROL_CAPTURE_INTENT,
       ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE
       };

    size_t request_keys_cnt =
            sizeof(request_keys_basic)/sizeof(request_keys_basic[0]);
    Vector<int32_t> available_request_keys;
    available_request_keys.appendArray(request_keys_basic, request_keys_cnt);

    staticInfo.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, available_request_keys.array(), available_request_keys.size());
    int32_t result_keys_basic[] = {
       ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE
       };
    size_t result_keys_cnt =
            sizeof(result_keys_basic)/sizeof(result_keys_basic[0]);

    Vector<int32_t> available_result_keys;
    available_result_keys.appendArray(result_keys_basic, result_keys_cnt);
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
            available_result_keys.array(), available_result_keys.size());

   int32_t characteristics_keys_basic[] = {
       ANDROID_SENSOR_ORIENTATION,
       ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
       ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
       ANDROID_REQUEST_PIPELINE_MAX_DEPTH, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
       ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
       ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
       ANDROID_CONTROL_AE_LOCK_AVAILABLE,
       ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
       ANDROID_SENSOR_FRAME_DURATION,
       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL
       };

    Vector<int32_t> available_characteristics_keys;
    available_characteristics_keys.appendArray(characteristics_keys_basic,
            sizeof(characteristics_keys_basic)/sizeof(int32_t));
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                      available_characteristics_keys.array(),
                      available_characteristics_keys.size());

    gStaticMetadata[cameraId] = staticInfo.release();
    gCameraMetadata[cameraId] = gStaticMetadata[cameraId];
    return rc;
}

/*===========================================================================
 * FUNCTION   : getCamInfo
 *
 * DESCRIPTION: query camera capabilities
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *   @info      : camera info struct to be filled in with camera capabilities
 *
 * RETURN     : int type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int Camera3HardwareInterface::getCamInfo(uint32_t cameraId,
        struct camera_info *info)
{
    int rc = 0;

    ALOGD("%s", __func__);
    if (NULL == gStaticMetadata[cameraId]) {
        rc = initStaticMetadata(cameraId);
        if (rc < 0) {
            return rc;
        }
    }

    info->facing = CAMERA_FACING_BACK;
    info->orientation = 0;
    info->device_version = CAMERA_DEVICE_API_VERSION_3_3;
    info->static_camera_characteristics = gStaticMetadata[cameraId];

    //For now assume both cameras can operate independently.
    info->conflicting_devices = NULL;
    info->conflicting_devices_length = 0;

    return rc;
}

/*===========================================================================
 * FUNCTION   : translateCapabilityToMetadata
 *
 * DESCRIPTION: translate the capability into camera_metadata_t
 *
 * PARAMETERS : type of the request
 *
 *
 * RETURN     : success: camera_metadata_t*
 *              failure: NULL
 *
 *==========================================================================*/
camera_metadata_t* Camera3HardwareInterface::translateCapabilityToMetadata(int type)
{
    AutoMutex l(mLock);
    if (mDefaultMetadata[type] != NULL) {
        return mDefaultMetadata[type];
    }
    //first time we are handling this request
    //fill up the metadata structure using the wrapper class
    CameraMetadata settings;
    //translate from cam_capability_t to camera_metadata_tag_t
    static const uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
    settings.update(ANDROID_REQUEST_TYPE, &requestType, 1);
    int32_t defaultRequestID = 0;
    settings.update(ANDROID_REQUEST_ID, &defaultRequestID, 1);

    uint8_t controlIntent = 0;
    uint8_t focusMode;
    uint8_t vsMode;
    uint8_t optStabMode;
    uint8_t cacMode;
    uint8_t edge_mode;
    uint8_t noise_red_mode;
    uint8_t tonemap_mode;
    vsMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    switch (type) {
      case CAMERA3_TEMPLATE_PREVIEW:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
        edge_mode = ANDROID_EDGE_MODE_HIGH_QUALITY;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        tonemap_mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        edge_mode = ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_MANUAL:
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
        focusMode = ANDROID_CONTROL_AF_MODE_OFF;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
        break;
      default:
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
        break;
    }
    settings.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &cacMode, 1);
    settings.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);
    settings.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vsMode, 1);
    focusMode = ANDROID_CONTROL_AF_MODE_OFF;
    settings.update(ANDROID_CONTROL_AF_MODE, &focusMode, 1);

    optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);

    static const int ev=0;
    settings.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ev, 1);

    static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);

    static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

    static const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    settings.update(ANDROID_CONTROL_MODE, &controlMode, 1);

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    settings.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    settings.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    /*flash*/
    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    settings.update(ANDROID_FLASH_MODE, &flashMode, 1);

    static const uint8_t flashFiringLevel = 0;
    settings.update(ANDROID_FLASH_FIRING_POWER,
            &flashFiringLevel, 1);

    /* lens */
    float default_aperture = 0;
    settings.update(ANDROID_LENS_APERTURE, &default_aperture, 1);


    float default_focal_length = 0;
    settings.update(ANDROID_LENS_FOCAL_LENGTH, &default_focal_length, 1);

    float default_focus_distance = 0;
    settings.update(ANDROID_LENS_FOCUS_DISTANCE, &default_focus_distance, 1);

    static const uint8_t demosaicMode = ANDROID_DEMOSAIC_MODE_FAST;
    settings.update(ANDROID_DEMOSAIC_MODE, &demosaicMode, 1);

    static const uint8_t hotpixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
    settings.update(ANDROID_HOT_PIXEL_MODE, &hotpixelMode, 1);

    /* face detection (default to OFF) */
    static const uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    settings.update(ANDROID_STATISTICS_HISTOGRAM_MODE, &histogramMode, 1);

    static const uint8_t sharpnessMapMode = ANDROID_STATISTICS_SHARPNESS_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, &sharpnessMapMode, 1);

    static const uint8_t hotPixelMapMode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hotPixelMapMode, 1);

    static const uint8_t lensShadingMode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensShadingMode, 1);

    static const uint8_t blackLevelLock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings.update(ANDROID_BLACK_LEVEL_LOCK, &blackLevelLock, 1);

    /* Exposure time(Update the Min Exposure Time)*/
    int64_t default_exposure_time = 0;
    settings.update(ANDROID_SENSOR_EXPOSURE_TIME, &default_exposure_time, 1);

    /* frame duration */
    static const int64_t default_frame_duration = NSEC_PER_33MSEC;
    settings.update(ANDROID_SENSOR_FRAME_DURATION, &default_frame_duration, 1);

    /* sensitivity */
    static const int32_t default_sensitivity = 100;
    settings.update(ANDROID_SENSOR_SENSITIVITY, &default_sensitivity, 1);

    /*edge mode*/
    settings.update(ANDROID_EDGE_MODE, &edge_mode, 1);

    /*noise reduction mode*/
    settings.update(ANDROID_NOISE_REDUCTION_MODE, &noise_red_mode, 1);

    /*color correction mode*/
    static const uint8_t color_correct_mode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    settings.update(ANDROID_COLOR_CORRECTION_MODE, &color_correct_mode, 1);

    /*transform matrix mode*/
    settings.update(ANDROID_TONEMAP_MODE, &tonemap_mode, 1);

    int32_t scaler_crop_region[4];
    scaler_crop_region[0] = 0;
    scaler_crop_region[1] = 0;
    scaler_crop_region[2] = 4096;
    scaler_crop_region[3] = 4096;
    settings.update(ANDROID_SCALER_CROP_REGION, scaler_crop_region, 4);

    static const uint8_t antibanding_mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding_mode, 1);

    /*focus distance*/
    float focus_distance = 0.0;
    settings.update(ANDROID_LENS_FOCUS_DISTANCE, &focus_distance, 1);

    /*target fps range: use maximum range for picture, and maximum fixed range for video*/
    int32_t fps_range[2] = {30, 60};
    settings.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range, 2);

    /*precapture trigger*/
    uint8_t precapture_trigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    settings.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &precapture_trigger, 1);

    /*af trigger*/
    uint8_t af_trigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    settings.update(ANDROID_CONTROL_AF_TRIGGER, &af_trigger, 1);

    /* ae & af regions */
    int active_region[5] = {0,0,0,0,0};
    settings.update(ANDROID_CONTROL_AE_REGIONS, active_region,
            sizeof(active_region) / sizeof(active_region[0]));
    settings.update(ANDROID_CONTROL_AF_REGIONS, active_region,
            sizeof(active_region) / sizeof(active_region[0]));

    /* black level lock */
    uint8_t blacklevel_lock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings.update(ANDROID_BLACK_LEVEL_LOCK, &blacklevel_lock, 1);

    /* lens shading map mode */
    uint8_t shadingmap_mode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingmap_mode, 1);

    //special defaults for manual template
    if (type == CAMERA3_TEMPLATE_MANUAL) {
        static const uint8_t manualControlMode = ANDROID_CONTROL_MODE_OFF;
        settings.update(ANDROID_CONTROL_MODE, &manualControlMode, 1);

        static const uint8_t manualFocusMode = ANDROID_CONTROL_AF_MODE_OFF;
        settings.update(ANDROID_CONTROL_AF_MODE, &manualFocusMode, 1);

        static const uint8_t manualAeMode = ANDROID_CONTROL_AE_MODE_OFF;
        settings.update(ANDROID_CONTROL_AE_MODE, &manualAeMode, 1);

        static const uint8_t manualAwbMode = ANDROID_CONTROL_AWB_MODE_OFF;
        settings.update(ANDROID_CONTROL_AWB_MODE, &manualAwbMode, 1);

        static const uint8_t manualTonemapMode = ANDROID_TONEMAP_MODE_FAST;
        settings.update(ANDROID_TONEMAP_MODE, &manualTonemapMode, 1);

        static const uint8_t manualColorCorrectMode = ANDROID_COLOR_CORRECTION_MODE_TRANSFORM_MATRIX;
        settings.update(ANDROID_COLOR_CORRECTION_MODE, &manualColorCorrectMode, 1);
    }


    mDefaultMetadata[type] = settings.release();

    return mDefaultMetadata[type];
}

/*===========================================================================
 * FUNCTION   : captureResultCb
 *
 * DESCRIPTION: Callback handler for all channels (streams, as well as metadata)
 *
 * PARAMETERS :
 *   @frame  : frame information from mm-camera-interface
 *   @buffer : actual gralloc buffer to be returned to frameworks. NULL if metadata.
 *   @userdata: userdata
 *
 * RETURN     : NONE
 *==========================================================================*/
void Camera3HardwareInterface::captureResultCb(icamera::Parameters *metadata,
                const camera3_stream_buffer_t *buffer,
                uint32_t frame_number, uint64_t timestamp, void *userdata)
{
    Camera3HardwareInterface *hw = (Camera3HardwareInterface *)userdata;
    if (hw == NULL) {
        ALOGE("Invalid hw %p", hw);
        return;
    }

    hw->captureResultCb(metadata, buffer, frame_number, timestamp);
    return;
}


/*===========================================================================
 * FUNCTION   : initialize
 *
 * DESCRIPTION: Pass framework callback pointers to HAL
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success : 0
 *              Failure: -ENODEV
 *==========================================================================*/

int Camera3HardwareInterface::initialize(const struct camera3_device *device,
                                  const camera3_callback_ops_t *callback_ops)
{
    ALOGD("%s: Entry", __func__);
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return -ENODEV;
    }

    int rc = hw->initialize(callback_ops);
    ALOGD("%s: Exit", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configure_streams
 *
 * DESCRIPTION: Configure Device with the streams that are to be requested
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success: 0
 *              Failure: -EINVAL (if stream configuration is invalid)
 *                       -ENODEV (fatal error)
 *==========================================================================*/

int Camera3HardwareInterface::configure_streams(
        const struct camera3_device *device,
        camera3_stream_configuration_t *stream_list)
{
    ALOGD("%s: Entry", __func__);
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return -ENODEV;
    }

    int rc = hw->configureStreams(stream_list);
    ALOGD("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : construct_default_request_settings
 *
 * DESCRIPTION: Configure a settings buffer to meet the required use case
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success: Return valid metadata
 *              Failure: Return NULL
 *==========================================================================*/
const camera_metadata_t* Camera3HardwareInterface::
    construct_default_request_settings(const struct camera3_device *device,
                                        int type)
{
    ALOGD("%s: Entry", __func__);
    camera_metadata_t* fwk_metadata = NULL;
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return NULL;
    }

    fwk_metadata = hw->translateCapabilityToMetadata(type);

    ALOGD("%s: X", __func__);
    return fwk_metadata;
}

/*===========================================================================
 * FUNCTION   : process_capture_request
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
int Camera3HardwareInterface::process_capture_request(
                    const struct camera3_device *device,
                    camera3_capture_request_t *request)
{
    ALOGD("%s: Entry", __func__);
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return -EINVAL;
    }

    int rc = hw->processCaptureRequest(request);
    ALOGD("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/

void Camera3HardwareInterface::dump(
                const struct camera3_device *device, int fd)
{
    /* Log level property is read when "adb shell dumpsys media.camera" is
       called so that the log level can be controlled without restarting
       the media server */
    getLogLevel();

    ALOGD("%s: Entry", __func__);
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return;
    }

    hw->dump(fd);
    ALOGD("%s: Exit", __func__);
    return;
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/

int Camera3HardwareInterface::flush(
                const struct camera3_device *device)
{
    ALOGD("%s: Entry", __func__);
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return -EINVAL;
    }

    {
        // Validate current state
        switch (hw->mState) {
            case STARTED:
                /* valid state */
                break;

            case ERROR:
                hw->handleCameraDeviceError();
                return -ENODEV;

            default:
                ALOGI("Flush returned during state %d", hw->mState);
                return 0;
        }
    }

    nsecs_t startTime = systemTime();
    nsecs_t interval = 0;

    // wait 1000ms at most while there are requests in the HAL
    while (hw->mPendingLiveRequest > 0 && interval / 1000 <= 1000000) {
         usleep(10000); // wait 10ms
         interval = systemTime() - startTime;
    }

    if (interval / 1000 > 1000000) {
        ALOGE("@%s, the flush() > 1000ms, time spend:%" PRId64 "us",
                    __func__, interval / 1000);
    }

    ALOGD("%s: X", __func__);
    return OK;
}

/*===========================================================================
 * FUNCTION   : close_camera_device
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
int Camera3HardwareInterface::close_camera_device(struct hw_device_t* device)
{
    int ret = NO_ERROR;
    Camera3HardwareInterface *hw =
        reinterpret_cast<Camera3HardwareInterface *>(
            reinterpret_cast<camera3_device_t *>(device)->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return BAD_VALUE;
    }

    ALOGI("%s: [KPI Perf]: E camera id %d", __func__, hw->mCameraId);
    delete hw;
    ALOGI("%s: [KPI Perf]: X", __func__);
    return ret;
}


/*===========================================================================
* FUNCTION   : getLogLevel
*
* DESCRIPTION: Reads the log level property into a variable
*
* PARAMETERS :
*   None
*
* RETURN     :
*   None
*==========================================================================*/
void Camera3HardwareInterface::getLogLevel()
{

    return;

}

/*===========================================================================
 * FUNCTION   : notifyErrorForPendingRequests
 *
 * DESCRIPTION: This function sends error for all the pending requests/buffers
 *
 * PARAMETERS : None
 *
 * RETURN     : Error codes
 *              NO_ERROR on success
 *
 *==========================================================================*/
int32_t Camera3HardwareInterface::notifyErrorForPendingRequests()
{
    int32_t rc = NO_ERROR;
    camera3_capture_result_t result;
    camera3_stream_buffer_t *pStream_Buf = NULL;

    memset(&result, 0, sizeof(camera3_capture_result_t));

    if (mPendingRequestsList.size() == 0) {
        return rc;
    }

    // Go through the pending requests info and send error request to framework
    for (pendingRequestIterator i = mPendingRequestsList.begin(); i != mPendingRequestsList.end(); ) {
        // Send error notify to frameworks
        camera3_notify_msg_t notify_msg;
        memset(&notify_msg, 0, sizeof(camera3_notify_msg_t));
        notify_msg.type = CAMERA3_MSG_ERROR;
        notify_msg.message.error.error_code = CAMERA3_MSG_ERROR_REQUEST;
        notify_msg.message.error.error_stream = NULL;
        notify_msg.message.error.frame_number = i->frame_number;
        mCallbackOps->notify(mCallbackOps, &notify_msg);

        pStream_Buf = new camera3_stream_buffer_t[i->buffers.size()];
        if (NULL == pStream_Buf) {
            ALOGE("No memory for pending buffers array");
            return NO_MEMORY;
        }
        memset(pStream_Buf, 0, sizeof(camera3_stream_buffer_t)*i->buffers.size());

        result.result = NULL;
        result.frame_number = i->frame_number;
        result.input_buffer = NULL;
        result.num_output_buffers = i->buffers.size();
        result.output_buffers = pStream_Buf;

        size_t index = 0;
        for (auto info = i->buffers.begin(); info != i->buffers.end(); ) {
            pStream_Buf[index].acquire_fence = -1;
            pStream_Buf[index].release_fence = -1;
            pStream_Buf[index].buffer = info->buffer->buffer;
            pStream_Buf[index].status = CAMERA3_BUFFER_STATUS_ERROR;
            pStream_Buf[index].stream = info->stream;
            index++;
            // Remove buffer from list
            info = i->buffers.erase(info);
        }

        ALOGD("%s: Returning frame %d in mPendingRequestsList", __func__, i->frame_number);
        mCallbackOps->process_capture_result(mCallbackOps, &result);
        delete [] pStream_Buf;
        i = erasePendingRequest(i);
    }

    ALOGD("Cleared all the pending buffers ");

    return rc;
}

/*===========================================================================
 * FUNCTION   : getCurrentSensorName
 *
 * DESCRIPTION: This function retrieves the string of sensor name
 *
 * PARAMETERS : None
 *
 * RETURN     : string
 *
 *==========================================================================*/

const char* Camera3HardwareInterface::getCurrentSensorName()
{
    const char* PROP_CAMERA_HAL_INPUT = "camera.hal.input";
    char value[PROPERTY_VALUE_MAX];
    const char* CAMERA_INPUT  = "cameraInput";
    if (property_get(PROP_CAMERA_HAL_INPUT, value, NULL)) {
        ALOGI("Camera input is %s", value);
        if (!strcmp(value, "ov10640")) {
            setenv(CAMERA_INPUT, value, sizeof(value));
            return "ov10640";
        } else if (!strcmp(value, "ov10635")) {
            setenv(CAMERA_INPUT, value, sizeof(value));
            return "ov10635";
        } else if (!strcmp(value, "tpg")) {
            setenv(CAMERA_INPUT, value, sizeof(value));
            return "tpg";
        } else {
            ALOGW("set sensor name: %s not be supported, use default(mondello)", value);
            return NULL;
        }
    } else {
        ALOGI("Camera input not been set, return NULL, use default sensor config");
        return NULL;
    }
}


/*===========================================================================
 * FUNCTION   : setDeviceId
 *
 * DESCRIPTION: This function sets the device id which will be opened later
 *
 * PARAMETERS : camera id
 *
 * RETURN     : null
 *
 *==========================================================================*/
const char *FIRST_CAMERA_DEVICE_ID = "mondello";
const char *SECOND_CAMERA_DEVICE_ID = "mondello-2";
const char *BACK_CAMERA_DEVICE_ID = "mondello-3";
const char *FRONT_CAMERA_DEVICE_ID = "mondello-4";
const char *SURROUNDING_1_CAMERA_DEVICE_ID = "ov10635-vc";
const char *SURROUNDING_2_CAMERA_DEVICE_ID = "ov10635-vc-2";
const char *SURROUNDING_3_CAMERA_DEVICE_ID = "ov10635-vc-3";
const char *SURROUNDING_4_CAMERA_DEVICE_ID = "ov10635-vc-4";
const char *SURROUNDING2_1_CAMERA_DEVICE_ID = "ov10635-2-vc";
const char *SURROUNDING2_2_CAMERA_DEVICE_ID = "ov10635-2-vc-2";
const char *SURROUNDING2_3_CAMERA_DEVICE_ID = "ov10635-2-vc-3";
const char *SURROUNDING2_4_CAMERA_DEVICE_ID = "ov10635-2-vc-4";

void Camera3HardwareInterface::setDeviceId(int cameraId)
{
    int multiCameraNumber = 0;
    bool isLeafHill = false;

    char value [PROPERTY_VALUE_MAX];
    const char* PROP_PRODUCT_DEVICE = "ro.product.device";
    if (property_get(PROP_PRODUCT_DEVICE, value, NULL)) {
        if (!strcmp(value, "leaf_hill"))
            isLeafHill = true;
        ALOGI("Product Device is %s", value);
    }

    const char* PROP_MULTI_CAMERA = "multi.camera.number";
    if (property_get(PROP_MULTI_CAMERA, value, NULL)) {
        multiCameraNumber = atoi(value);
        ALOGI("Multi camera number is %d", multiCameraNumber);
    }

    int deviceId = -1;
    switch (cameraId) {
    case 0:
        if (multiCameraNumber > 0)
            deviceId = icamera::CameraUtils::findXmlId(SURROUNDING_1_CAMERA_DEVICE_ID);
        else if (isLeafHill) {
            //if config property: camera.hal.input use the current config
            if (!getCurrentSensorName()) {
                deviceId = icamera::CameraUtils::findXmlId(FIRST_CAMERA_DEVICE_ID);
            } else {
                deviceId = icamera::CameraUtils::findXmlId(getCurrentSensorName());
            }
        }
        else
            deviceId = icamera::CameraUtils::findXmlId(BACK_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 1:
        if (multiCameraNumber > 0)
            deviceId = icamera::CameraUtils::findXmlId(SURROUNDING_2_CAMERA_DEVICE_ID);
        else if (isLeafHill)
            deviceId = icamera::CameraUtils::findXmlId(SECOND_CAMERA_DEVICE_ID);
        else
            deviceId = icamera::CameraUtils::findXmlId(FRONT_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 2:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING_3_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 3:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING_4_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 4:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING2_1_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 5:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING2_2_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 6:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING2_3_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    case 7:
        deviceId = icamera::CameraUtils::findXmlId(SURROUNDING2_4_CAMERA_DEVICE_ID);
        if (deviceId == -1) {
            ALOGE("Failed to find sensor config in xml");
            return;
        }
        mDeviceId = deviceId;
        ALOGI("%s Setting device id %d", __func__, mDeviceId);
        break;
    default:
        ALOGE("%s Requesting unsupported camera id %d", __func__, cameraId);
    }
}
} //namespace camera2
} //namespace android
