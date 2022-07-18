/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
** Copyright 2015-2018, Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "CameraHardwareSoc"
class Mutex;

#include <utils/threads.h>
#include <fcntl.h>
#include <camera/Camera.h>
#include <MetadataBufferType.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include "iutils/CameraLog.h"
#include "SocCameraHWInterface.h"
#include "iutils/Utils.h"
#include "iutils/CameraDump.h"
#include "IJpeg.h"
#include "Exif.h"
#include "EXIFMetaData.h"
#include "Gfx.h"
#include "PlatformData.h"

#define FRONT_CAMERA_FOCUS_DISTANCES_STR           "0.20,0.25,Infinity"

using namespace icamera;
namespace android {

struct addrs {
    uint32_t type;  // make sure that this is 4 byte.
    unsigned int handle;
};

#define LOG2_FLAG  1 << 1
#define LOG1_FLAG  1

#define IS_YUV_FORMAT(format) (format == V4L2_PIX_FMT_NV12 || format == V4L2_PIX_FMT_YVU420 || \
                               format == V4L2_PIX_FMT_YUYV || format == V4L2_PIX_FMT_UYVY || \
                               format == V4L2_PIX_FMT_NV16)
#define IS_RGB_FORMAT(format) (format == V4L2_PIX_FMT_RGB565 || format == V4L2_PIX_FMT_RGB24 || \
                               format == V4L2_PIX_FMT_RGB32)


gralloc_module_t const* CameraHardwareSoc::mGrallocHal;

CameraHardwareSoc::CameraHardwareSoc(int cameraId, camera_device_t *dev)
        : mPreviewRunning(false),
          mPreviewStartDeferred(false),
          mExitPreviewThread(false),
          mCaptureInProgress(false),
          mExitAutoFocusThread(false),
          mNeedInternalBuf(false),
          mDisplayDisabled(false),
          mPerfEnabled(false),
          mParameters(),
          mNotifyCb(0),
          mDataCb(0),
          mDataCbTimestamp(0),
          mCallbackCookie(0),
          mMsgEnabled(0),
          mPreviewWidth(0),
          mPreviewHeight(0),
          mPictureWidth(0),
          mPictureHeight(0),
          mThumbnailWidth(0),
          mThumbnailHeight(0),
          mPreviewSize(0),
          mJpegQuality(0),
          mJpegThumbnailQuality(0),
          mNativeWindowStride(0),
          mUsage(GRALLOC_USAGE_SW_WRITE_OFTEN),
          mFormat(HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED),
          mV4l2Format(V4L2_PIX_FMT_NV12),
          mISysV4l2Format(V4L2_PIX_FMT_UYVY),
          mMinUndequeuedBuffers(0),
          mBufferCount(0),
          mCameraId(cameraId),
          mDeviceId(-1),
          mField(V4L2_FIELD_ALTERNATE),
          mDeinterlaceMode(DEINTERLACE_OFF),
          mRecordRunning(false),
          mGenConvert(NULL),
          mBufcount(0),
          mFps(0.0)
{
    ALOGI("%s :", __func__);
    int ret = 0;

    mWindow = NULL;
    CLEAR(mjcBuffers);

    CLEAR(mBase);
    for (int i = 0; i < MAX_BUFFERS; i++) {
        mRecordHeap[i] = NULL;
    }

    //Get the device id from property.
    char value [PROPERTY_VALUE_MAX];
    const char* PROP_CAMERA_HAL_ID = "camera.hal.id";
    if (property_get(PROP_CAMERA_HAL_ID, value, NULL)) {
        mDeviceId = atoi(value);
        ALOGI("Camera Device ID is 0x%x", mDeviceId);
    }
    if (mDeviceId == -1)
        setDeviceId(cameraId);

    if (!mGrallocHal) {
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
        if (ret)
            ALOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
    }
    mUsage = GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_CAMERA_READ |
             GRALLOC_USAGE_SW_WRITE_OFTEN;

#ifdef BYPASS_MODE
    mCanUsePsys = false;
#else
    mCanUsePsys = true;
#endif
    // get the best V4L2 Format which is supported by both Isys and iVP to avoid SW process
    getSupportedV4L2Formats();
    if (mSupportedV4L2Formats.size() > 0) {
        mV4l2Format = mSupportedV4L2Formats[0];
        mISysV4l2Format = mSupportedV4L2Formats[0];
        ALOGD("%s: mV4l2Format = %d", __func__, mV4l2Format);
    } else {
        vector<int> supportedFormat;
        PlatformData::getSupportedISysFormats(mDeviceId, supportedFormat);
        if (supportedFormat.size() > 0)
            mISysV4l2Format = supportedFormat[0];
        mV4l2Format = V4L2_PIX_FMT_NV12;
        ALOGD("%s: mISysV4l2Format = %d", __func__, mISysV4l2Format);
    }

    initDefaultParameters(cameraId);
    /* whether the PreviewThread is active in preview or stopped.
     * we create the thread but it is initially in stopped state.
     */
    mPreviewThread = new PreviewThread(this);
    mPictureThread = new PictureThread(this);
    mAutoFocusThread = new AutoFocusThread(this);

    ret = camera_hal_init();
    if (ret != OK ) {
        ALOGE("ERR(%s):Fail on HAL init", __func__);
    }

    const char* PROP_CAMERA_HAL_DEBUG = "camera.hal.debug";
    const char* DEBUG_KEY = "cameraDebug";
    if (property_get(PROP_CAMERA_HAL_DEBUG, value, NULL)) {
        gLogLevel = atoi(value);
        // to enable both LOG1 and LOG2 traces
        if (gLogLevel & LOG2_FLAG)
            gLogLevel |= LOG1_FLAG;
        ALOGI("Camera Device debug level is 0x%x", gLogLevel);
        setenv(DEBUG_KEY, value, 1);
    }

    const char* PROP_CAMERA_HAL_PERF = "camera.hal.perf";
    const char* PERF_KEY = "cameraPerf";
    int perfLevel = 0;
    if (property_get(PROP_CAMERA_HAL_PERF, value, NULL)) {
        perfLevel = atoi(value);
        ALOGI("Camera perf level is 0x%x", perfLevel);
        setenv(PERF_KEY, value, 1);
        mPerfEnabled = perfLevel > 0 ? true : false;
    }

    const char* PROP_CAMERA_HAL_DUMP = "camera.hal.dump";
    const char* DUMP_KEY      = "cameraDump";
    const char* DUMP_PATH_KEY = "cameraDumpPath";
    if (property_get(PROP_CAMERA_HAL_DUMP, value, NULL)) {
        gDumpType = atoi(value);
        ALOGI("Camera dump type is 0x%x", gDumpType);
        setenv(DUMP_KEY, value, 1);
    }
    int displayStatus = -1;
    const char* PROP_CAMERA_HAL_DISPLAY_FAKE = "camera.hal.display.fake";
    if (property_get(PROP_CAMERA_HAL_DISPLAY_FAKE, value, NULL)) {
        displayStatus = atoi(value);
        ALOGI("Camera display status is 0x%x", displayStatus);
        if (displayStatus == 1) {
            mDisplayDisabled = true;
        }
    }

    char release [PROPERTY_VALUE_MAX];
    const char* PROP_ANDROID_VERSION = "ro.build.version.release";
    if (property_get(PROP_ANDROID_VERSION, release, NULL)) {
        // version 6 is M, 7 is N
        int version = release[0] - '0';
        if (version >= 7)
            snprintf(gDumpPath, sizeof(gDumpPath), "%s", "data/misc/cameraserver");
        else
            snprintf(gDumpPath, sizeof(gDumpPath), "%s", "data/misc/media");
        setenv(DUMP_PATH_KEY, gDumpPath, 1);
    }

    // Set debug and dump level
    Log::setDebugLevel();

    mGenConvert = new GenImageConvert();

    mInputConfig.width = 0;
    mInputConfig.height = 0;
    mInputConfig.format = -1;
}

void CameraHardwareSoc::setDeviceId(int cameraId)
{
    if (cameraId >= MAX_CAMERAS || cameraId >= PlatformData::numberOfCameras()) {
        ALOGE("%s Requesting unsupported camera id %d", __func__, cameraId);
        return;
    }
    mDeviceId = cameraId;
}

int CameraHardwareSoc::getCameraId() const
{
    return mCameraId;
}

bool Comp(const camera_resolution_t &a,const camera_resolution_t &b)
{
    return a.width * a.height < b.width * b.height;
}

void CameraHardwareSoc::initDefaultParameters(int cameraId)
{
    ALOGI("%s:", __func__);
    CameraParameters p;
    CameraParameters ip;

    // Get stream supported stream config from xml
    icamera::camera_info_t info;
    CLEAR(info);

    get_camera_info(mDeviceId, info);
    supported_stream_config_array_t availableConfigs;
    info.capability->getSupportedStreamConfig(availableConfigs);
    supported_stream_config_t config;
    std::string resolution;

    if (!availableConfigs.size()) {
        ALOGE("No supported configs, check xml");
        return;
    }

    vector<camera_resolution_t> res;
    vector<camera_resolution_t>::iterator iter;

    for (size_t i = 0; i < availableConfigs.size(); i++) {
        config = availableConfigs[i];
        ALOGI("supported configs %dx%d format: %s, field: %d",
              config.width, config.height,
              CameraUtils::pixelCode2String(config.format),
              config.field);
        size_t j = 0;
        for (; j < res.size(); j++) {
            // Find the same size in vector
            if ((res[j].width == config.width) && (res[j].height == config.height))
                    break;
        }

        // Can't find the same size, insert it to vector
        if (j == res.size()) {
            camera_resolution_t size = {config.width, config.height};
            res.push_back(size);
        }

        if (config.format == mV4l2Format) {
            mField = config.field;
        }
    }
    // Sort the vector from small size to large size
    sort(res.begin(), res.end(), Comp);
    for(size_t i = 0; i < res.size(); i++) {
        resolution = resolution + std::to_string(res[i].width) + "x" + std::to_string(res[i].height) + ",";
    }

    if (resolution.length() > 0)
        resolution.replace((resolution.length() - 1) , 1, "");
    ALOGI("Resolution string %s", resolution.c_str());

    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
          resolution.c_str());
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
          resolution.c_str());

    p.getSupportedPreviewSizes(mSupportedPreviewSizes);
    p.getSupportedPictureSizes(mSupportedPictureSizes);

    String8 previewColorString;
    previewColorString = CameraParameters::PIXEL_FORMAT_YUV420SP;
    previewColorString.append(",");
    previewColorString.append(CameraParameters::PIXEL_FORMAT_YUV420P);
    previewColorString.append(",");
    previewColorString.append(CameraParameters::PIXEL_FORMAT_RGB565);
    previewColorString.append(",");
    previewColorString.append(CameraParameters::PIXEL_FORMAT_YUV422I);
    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, previewColorString.string());
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.setPreviewSize(res.back().width, res.back().height);

    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.setPictureSize(res.back().width, res.back().height);
    p.set(CameraParameters::KEY_JPEG_QUALITY, "100"); // maximum quality
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
          CameraParameters::PIXEL_FORMAT_JPEG);

    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, "0"); // no face detection
    p.set(CameraParameters::KEY_MAX_NUM_DETECTED_FACES_SW, "0"); // no face detection

    String8 parameterString;

    parameterString = CameraParameters::FOCUS_MODE_FIXED;
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
          parameterString.string());
    p.set(CameraParameters::KEY_FOCUS_MODE,
          CameraParameters::FOCUS_MODE_FIXED);
    p.set(CameraParameters::KEY_FOCUS_DISTANCES,
          FRONT_CAMERA_FOCUS_DISTANCES_STR);
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
          "160x120,0x0");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30");
    p.setPreviewFrameRate(30);

    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(30000,30000),(30000,60000)");
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "30000,30000");
    if (cameraId == 0)
       p.set(CameraParameters::KEY_FOCAL_LENGTH, "1.8");
    else
       p.set(CameraParameters::KEY_FOCAL_LENGTH, "4.31");

    parameterString = CameraParameters::WHITE_BALANCE_AUTO;
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
          parameterString.string());

    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");

    p.set(CameraParameters::KEY_ROTATION, 0);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);

    parameterString = CameraParameters::EFFECT_NONE;
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, parameterString.string());
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

    if (cameraId == 0) {
        p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "62.5");
        p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "52.5");
    } else {
        p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "54.8");
        p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "42.5");
    }

    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");

    parameterString = CameraParameters::ANTIBANDING_AUTO;
    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, parameterString.string());
    p.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_AUTO);

    p.setFloat(CameraParameters::KEY_GPS_LATITUDE, 0.0);
    p.setFloat(CameraParameters::KEY_GPS_LONGITUDE, 0.0);
    p.setFloat(CameraParameters::KEY_GPS_ALTITUDE, 0.0);
    p.set(CameraParameters::KEY_GPS_TIMESTAMP, 0);
    p.set(CameraParameters::KEY_GPS_PROCESSING_METHOD, "");

    mParameters = p;

    /* make sure mSoCCamera has all the settings we do.  applications
     * aren't required to call setParameters themselves (only if they
     * want to change something.
     */
    setParameters(p);
}

CameraHardwareSoc::~CameraHardwareSoc()
{
    ALOGI("%s", __func__);
    camera_hal_deinit();
    if (mGenConvert)
        delete mGenConvert;
}

status_t CameraHardwareSoc::setPreviewWindow(preview_stream_ops *w)
{
    mWindow = w;
    ALOGI("%s: mWindow %p", __func__, mWindow);

    if (!w) {
        ALOGE("preview window is NULL!");
        return OK;
    }

    mPreviewLock.lock();

    if (mPreviewRunning && !mPreviewStartDeferred) {
        ALOGI("stop preview (window change)");
        stopPreviewInternal();
    }

    mParameters.getPreviewSize(&mPreviewWidth, &mPreviewHeight);

    if (mPreviewRunning && mPreviewStartDeferred) {
        ALOGI("start/resume preview");
        status_t ret = startPreviewInternal();
        if (ret == OK) {
            mPreviewStartDeferred = false;
            mPreviewCondition.signal();
        }
    }
    mPreviewLock.unlock();

    return OK;
}

void CameraHardwareSoc::setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void *user)
{
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemoryCb = get_memory;
    mCallbackCookie = user;
}

void CameraHardwareSoc::enableMsgType(int32_t msgType)
{
    ALOGI("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled |= msgType;

    ALOGI("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

void CameraHardwareSoc::disableMsgType(int32_t msgType)
{
    ALOGI("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled &= ~msgType;
    ALOGI("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

bool CameraHardwareSoc::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

int CameraHardwareSoc::previewThreadWrapper()
{
    ALOGI("%s: starting", __func__);
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            ALOGI("%s: calling mSoCCamera->stopPreview() and waiting", __func__);
            /* signal that we're stopping */
            mPreviewStoppedCondition.signal();
            mPreviewCondition.wait(mPreviewLock);
            ALOGI("%s: return from wait", __func__);
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
            ALOGI("%s: exiting", __func__);
            return 0;
        }
        previewThread();
    }
}

int CameraHardwareSoc::previewThread()
{
    camera_buffer_t *buf;

    memset(&buf, 1, sizeof(buf));
    if (mWindow && mGrallocHal) {

        int ret;
        int found = -1;
        struct timeval tv1, tv2, tv3, tv4, tv5;
        CLEAR(tv1);
        CLEAR(tv2);
        CLEAR(tv3);
        CLEAR(tv4);
        CLEAR(tv5);

        gettimeofday(&tv1, 0);
        if (mBase.tv_sec == 0 && mBufcount != 0) {
            mBase = tv1;
        }

        ret = camera_stream_dqbuf(mDeviceId, mStreams[0].id, &buf);
        if (ret < 0) {
            ALOGE("%s: get stream buffers failed", __func__);
            return -1;
        }
        mBufcount++;

        if (mPerfEnabled) {
            ALOGD("CAM_PERF: mCameraID: %d, mBufCount: %d timestamp(ns)=%llu (buf sequence %ld)\n",
                    mCameraId, mBufcount, buf->timestamp, buf->sequence);
        }
        gettimeofday(&tv2, 0);
        for (int i = 0; i< mBufferCount; i++) {
            if (mNeedInternalBuf) {
                if (mBufferPackage[i].nativeHalBuffer.addr == buf->addr) {
                    found = i;
                    LOG1("%s: DQbuffers %d, addr: %p",__func__, i, mBufferPackage[i].nativeHalBuffer.addr);
                    break;
                }
            } else {
                if (mBufferPackage[i].nativeWinBuffer.addr == buf->addr) {
                    LOG1("%s: DQbuffers %d, addr: %p",__func__, i, mBufferPackage[i].nativeWinBuffer.addr);
                    found = i;
                    break;
                }
            }
        }

        if (found < 0) {
            ALOGE("%s: dqbuf error", __func__);
            return UNKNOWN_ERROR;
        }

        // Do conversion and scaling in to Native Window buffer
        if (mNeedInternalBuf) {
            ret = mGenConvert->downScalingAndColorConversion(mBufferPackage[found]);
            Check(ret != OK, ret, "@%s: Gfx Downscaling failed", __func__);
        }

        gettimeofday(&tv3, 0);

        if (mRecordRunning == true) {
            // Copy handle to data
            struct addrs *addrs = (struct addrs *)mRecordHeap[found]->data;
            addrs->type = kMetadataBufferTypeGrallocSource;
            MEMCPY_S(&addrs->handle, sizeof(addrs->handle),
                     mBufferPackage[found].nativeWinBuffHandle,
                     sizeof(mBufferPackage[found].nativeWinBuffHandle));

            if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
                nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
                mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap[found], 0, mCallbackCookie);
            }
        }

        // When the CAMERA_MSG_COMPRESSED_IMAGE flag is set the CAMERA_MSG_PREVIEW_FRAME
        // gets disabled
        if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) && mDataCb != NULL &&
                !(mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {

            //To meet CTS requirments, it need the buffer size is w x h x bpp / 8
            //1080p/Nv12: buffer size 1920x1080x12/8
            int size = calculateBufferSize(mPreviewWidth, mPreviewHeight, mFormat);
            LOG1("DataCb buffer size: %d, wxh = %dx%d, native win size= %d", size, mPreviewWidth, mPreviewHeight,
                    mBufferPackage[found].nativeWinBuffer.s.size);
            camera_memory_t* cam_buff = mGetMemoryCb(-1, size, 1, NULL);
            if (NULL != cam_buff && NULL != cam_buff->data) {
                void *dst = cam_buff->data;
                void *src = mBufferPackage[found].nativeWinBuffer.addr;
                if (mPreviewWidth == mBufferPackage[found].nativeWinBuffer.s.width &&
                    mPreviewHeight == mBufferPackage[found].nativeWinBuffer.s.height) {
                    MEMCPY_S(dst, size, src, size);
                } else {
                    copyBufForDataCallback(dst, src, found);
                }

                //To meet CTS verifier preview format test requirment, when application set preview format NV21,
                //HAL should return NV21 buffer, so need software convert NV12 to NV21.
                if (mFormat == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
                    char *p = (char *)cam_buff->data;
                    char tmp;
                    p += mPreviewWidth * mPreviewHeight;
                    for (int i = 0; i < mPreviewHeight / 2; i++) {
                        for (int j = 0; j < mPreviewWidth; j += 2 ) {
                            tmp = *p;
                            *p = *(p + 1);
                            *(p + 1) = tmp;
                            p += 2;
                        }
                    }
                }

                if (previewEnabled()) {
                    mDataCb(CAMERA_MSG_PREVIEW_FRAME, cam_buff, 0, NULL, mCallbackCookie);
                }
                cam_buff->release(cam_buff);
            } else {
                ALOGE("%s: Memory failure in CAMERA_MSG_PREVIEW_FRAME", __FUNCTION__);
            }
        }

        PERF_CAMERA_ATRACE_PARAM1("buffer enqueue to GFX: ", found);
        //enqueue to GFX, and get another buffer out
        int next = -1;
        if (mDisplayDisabled) {
            next = found;
        } else {
            next = displayBuffer(found);
        }
        gettimeofday(&tv4, 0);
        if (next < 0 || next >= mBufferCount) {
            ALOGE("%s: displayBuffer error with next %d", __func__, next);
            return UNKNOWN_ERROR;
        }

        //Q the buffers to ISP
        camera_buffer_t *buffer = NULL;
        if (mNeedInternalBuf) {
            LOG1("%s: Qbuffers %d, addr: %p",__func__, next, mBufferPackage[next].nativeHalBuffer.addr);
            buffer = &mBufferPackage[next].nativeHalBuffer;
        } else {
            LOG1("%s: Qbuffers %d, addr: %p",__func__, next, mBufferPackage[next].nativeWinBuffer.addr);
            buffer = &mBufferPackage[next].nativeWinBuffer;
        }
        camera_stream_qbuf(mDeviceId, &buffer);

        gettimeofday(&tv5, 0);

        // print the performance logs every 10 frames
        if (mBufcount % 10 == 0) {
            long t12 = cal_diff(tv1, tv2);
            long t23 = cal_diff(tv2, tv3);
            long t34 = cal_diff(tv3, tv4);
            long t45 = cal_diff(tv4, tv5);
            //calcluate the total FPS
            long t =  cal_diff(mBase, tv5);
            mFps = (float) (mBufcount - 1) / t * 1000;

            ALOGD("CAM_PERF: DQ from ISP consume %ldms, gfx scaling and color conversion consume %ldms,"
                "display consume %ldms, Qbuf to ISP consume %ldms, total fps is %f, buffcount is %ld",
                t12, t23, t34, t45, mFps, mBufcount);
        }

    }
    return NO_ERROR;
}

void CameraHardwareSoc::getSupportedV4L2Formats() {
    int iVPSupportedFormat[4] = {V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YVU420,
                                 V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_YUYV};
    for (unsigned int i = 0; i < sizeof(iVPSupportedFormat) / sizeof(int); i++) {
        if (PlatformData::isISysSupportedFormat(mDeviceId, iVPSupportedFormat[i])) {
            mSupportedV4L2Formats.push_back(iVPSupportedFormat[i]);
        }
    }
}

void CameraHardwareSoc::copyBufForDataCallback(void *dst_buf, void *src_buf, int found)
{
    char *dst = (char *)dst_buf;
    char *src = (char *)src_buf;
    if (mFormat == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, mPreviewWidth, src,
                    mPreviewWidth);
            dst += mPreviewWidth;
            src += mBufferPackage[found].nativeWinBuffer.s.width;
        }
        src += mBufferPackage[found].nativeWinBuffer.s.width *
            (mBufferPackage[found].nativeWinBuffer.s.height - mPreviewHeight);
        for (int i = 0; i < mPreviewHeight / 2; i++) {
            MEMCPY_S(dst, mPreviewWidth, src, mPreviewWidth);
            dst += mPreviewWidth;
            src += mBufferPackage[found].nativeWinBuffer.s.width;
        }
    } else if (mFormat == HAL_PIXEL_FORMAT_YV12) {
        int stride = ALIGN(mPreviewWidth, 16);
        int c_stride = ALIGN(stride / 2, 16);
        int c_size = c_stride * mPreviewHeight / 2;
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, stride, src, stride);
            dst += stride;
            src += mBufferPackage[found].nativeWinBuffer.s.width;
        }
        src += mBufferPackage[found].nativeWinBuffer.s.width *
            (mBufferPackage[found].nativeWinBuffer.s.height - mPreviewHeight);
        for (int i = 0; i < mPreviewHeight / 2; i++) {
            MEMCPY_S(dst, c_stride, src, c_stride);
            dst += c_stride;
            src += mBufferPackage[found].nativeWinBuffer.s.width / 2;
        }
        src += (mBufferPackage[found].nativeWinBuffer.s.width / 2) *
            (mBufferPackage[found].nativeWinBuffer.s.height / 2 - mPreviewHeight / 2);
        for (int i = 0; i < mPreviewHeight / 2; i++) {
            MEMCPY_S(dst, c_stride, src, c_stride);
            dst += c_stride;
            src += mBufferPackage[found].nativeWinBuffer.s.width / 2;
        }
    } else if (mFormat == HAL_PIXEL_FORMAT_RGB_565) {
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, mPreviewWidth * 2, src, mPreviewWidth * 2);
            dst += mPreviewWidth * 2;
            src += mBufferPackage[found].nativeWinBuffer.s.width * 2;
        }
    } else if (mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I) {
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, mPreviewWidth * 2, src, mPreviewWidth * 2);
            dst += mPreviewWidth * 2;
            src += mBufferPackage[found].nativeWinBuffer.s.width * 2;
        }
    } else if (mFormat == HAL_PIXEL_FORMAT_YCbCr_422_SP) {
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, mPreviewWidth, src,
                    mPreviewWidth);
            dst += mPreviewWidth;
            src += mBufferPackage[found].nativeWinBuffer.s.width;
        }
        src += mBufferPackage[found].nativeWinBuffer.s.width *
            (mBufferPackage[found].nativeWinBuffer.s.height - mPreviewHeight);
        for (int i = 0; i < mPreviewHeight; i++) {
            MEMCPY_S(dst, mPreviewWidth, src, mPreviewWidth);
            dst += mPreviewWidth;
            src += mBufferPackage[found].nativeWinBuffer.s.width;
        }
    } else {
        ALOGE("Unsupported preview color format: %d", mFormat);
    }
}

int CameraHardwareSoc::getBitsPerPixel(int format)
{
    int bpp;
    switch (format) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
        case HAL_PIXEL_FORMAT_YV12:
            bpp = 12;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            bpp = 16;
            break;
        default:
            ALOGE("unsupported format: %d", format);
            return -1;
    }
    return bpp;
}

int CameraHardwareSoc::calculateBufferSize(int width, int height, int format)
{
    LOG1("calculateBufferSize for datacallback: width x height = %d x %d, format:%d", width, height, format);
    int bpp = getBitsPerPixel(format);
    if (format == HAL_PIXEL_FORMAT_YV12) {
        /* To meet CTS requirements
         * http://developer.android.com/reference/android/graphics/ImageFormat.html#YV12
         */
        int stride = ALIGN(width, 16);
        int y_size = stride * height;
        int c_stride = ALIGN(stride / 2, 16);
        int c_size = c_stride * height / 2;
        int size = y_size + c_size * 2;
        return size;
    } else {
        if (bpp > 0) {
            return width * height * bpp / 8;
        } else {
            ALOGE("getBitsPerPixel is incorrect, return buffer size is 0");
            return 0;
        }
    }
}


//return time diff in ms
long CameraHardwareSoc::cal_diff(const struct timeval t1, const struct timeval t2)
{
    long sdiff = t2.tv_sec - t1.tv_sec; //sec
    long udiff = t2.tv_usec - t1.tv_usec; //usec

    if (t2.tv_usec < t1.tv_usec) { //overflow
        udiff = (t2.tv_usec + 1000000) - t1.tv_usec;
        sdiff--;
    }

    return (sdiff*1000 + udiff/1000); //return msec
}

status_t CameraHardwareSoc::startPreview()
{
    int ret = 0;

    ALOGI("%s :", __func__);

    if (waitCaptureCompletion() != NO_ERROR) {
        return TIMED_OUT;
    }

    mPreviewLock.lock();
    if (mPreviewRunning) {
        // already running
        ALOGE("%s : preview thread already running", __func__);
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }

    mPreviewRunning = true;
    mPreviewStartDeferred = false;

    if (!mWindow) {
        ALOGI("%s : deferring", __func__);
        mPreviewStartDeferred = true;
        mPreviewLock.unlock();
        return NO_ERROR;
    }

    ret = startPreviewInternal();
    if (ret == OK)
        mPreviewCondition.signal();

    mPreviewLock.unlock();
    return ret;
}

void CameraHardwareSoc::getInputConfig()
{
    char value[PROPERTY_VALUE_MAX];
    const char* PROP_CAMERA_INPUT_SIZE = "camera.input.config.size";
    const char* PROP_CAMERA_INPUT_FORMAT = "camera.input.config.format";
    const char* PROP_CAMERA_FIELD = "camera.hal.field";
    const char* PROP_CAMERA_DEINTERLACE_MODE = "camera.hal.deinterlace";
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
            ALOGD("Not supported the input config: %s, using the default input %d x %d", value,
                    mInputConfig.width, mInputConfig.height);
        }
        ALOGD("%s: InputConfig size %d x %d", __func__,  mInputConfig.width, mInputConfig.height);
    }

    if (property_get(PROP_CAMERA_INPUT_FORMAT, value, NULL)) {
        if (!strcmp(value, "uyvy")) {
            mInputConfig.format = V4L2_PIX_FMT_UYVY;
        } else if (!strcmp(value, "yuy2")) {
            mInputConfig.format = V4L2_PIX_FMT_YUYV;
        } else {
            ALOGD("Not supported the input format: %s, using the default input format %d", value,
                    mInputConfig.format);
        }
        ALOGD("%s: InputConfig format %d", __func__, mInputConfig.format);
    }

    if (property_get(PROP_CAMERA_FIELD, value, NULL)) {
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
    }
}

status_t CameraHardwareSoc::configStreams()
{
    ALOGI("%s", __func__);
    status_t ret = NO_ERROR;

    if (SINGLE_FIELD(mField) && mDeinterlaceMode == DEINTERLACE_WEAVING) {
        //set HW weaving by camera_set_parameters
        Parameters param;
        param.setDeinterlaceMode(DEINTERLACE_WEAVING);
        camera_set_parameters(mCameraId, param);
    }

    camera_resolution_t bestIsysRes = {0, 0};
    CameraUtils::getBestISysResolution(mDeviceId, mField, mPreviewWidth, mPreviewHeight, bestIsysRes);
    int previwewV4l2format = HalFormat2V4L2Format(mFormat);
    //if the preview size/format is supported by isys, use bypass mode
    bool byPass = (bestIsysRes.width == mPreviewWidth &&
                   bestIsysRes.height == mPreviewHeight &&
                   PlatformData::isISysSupportedFormat(mCameraId, previwewV4l2format));
    if (!mCanUsePsys || mInputConfig.format == -1) {
        mNeedInternalBuf = (mField == V4L2_FIELD_ALTERNATE) ||
                           (mV4l2Format == V4L2_PIX_FMT_RGB565) ||
                           (!byPass);
    } else {
        mNeedInternalBuf = false;
    }
    if (mNeedInternalBuf) {
        ALOGD("Using internal Hal buffers for convertion");
        allocateHalBuffers(mBufferCount);
        mStreams[0].format = mV4l2Format;
        mStreams[0].width = bestIsysRes.width;
        mStreams[0].height = bestIsysRes.height;
    } else {
        ALOGD("Not using internal Hal buffers for convertion");
        mStreams[0].format = previwewV4l2format;
        mStreams[0].width = mPreviewWidth;
        mStreams[0].height = mPreviewHeight;
    }

    mStreams[0].id = 0;
    mStreams[0].field = mField;
    mStreams[0].memType = V4L2_MEMORY_USERPTR;

    mStreams[0].stride = CameraUtils::getStride(mStreams[0].format, mStreams[0].width);
    mStream_list.num_streams = 1;
    mStream_list.operation_mode = CAMERA_STREAM_CONFIGURATION_MODE_ULL;

    mStream_list.streams = mStreams;
    mStreams[0].size = CameraUtils::getFrameSize(mStreams[0].format,
                       mStreams[0].width,
                       SINGLE_FIELD(mField) ? mStreams[0].height/2 : mStreams[0].height);
    ALOGD("config_stream: format:%d, stride %d, size %d, w x h = %d x %d", mStreams[0].format,
            mStreams[0].stride, mStreams[0].size, mStreams[0].width, mStreams[0].height);
    ret = camera_device_config_sensor_input(mDeviceId, &mInputConfig);
    ret |= camera_device_config_streams(mDeviceId, &mStream_list);
    return ret;
}

status_t CameraHardwareSoc::startPreviewInternal()
{
    ALOGI("%s", __func__);

    CLEAR(mBase);
    mBufcount = 0;
    mFps = 0.0;
    // get buffers from native windows and saved locally
    if (allocateGfxBuf(kBufferCount) != NO_ERROR) {
        ALOGE("%s: Allocate buffer failed", __func__);
        return INVALID_OPERATION;
    }

    icamera::camera_info_t info;
    CLEAR(info);
    get_camera_info(mDeviceId, info);
    int ret = camera_device_open(mDeviceId, info.vc.total_num);
    Check(ret != OK, ret, "@%s: Camera device open failed", __func__);

    //config_streams
    ret = configStreams();
    Check(ret != OK, ret, "@%s: config Streams failed", __func__);

    // initialize and Q the buffers
    for (int i = 0; i < mBufferCount; i++)
    {
        // Update values for the native window buffers
        mBufferPackage[i].nativeWinBuffer.s = mStreams[0];
        mBufferPackage[i].nativeWinBuffer.s.format = HalFormat2V4L2Format(mFormat);
        mBufferPackage[i].nativeWinBuffer.s.width = mPreviewWidth;
        mBufferPackage[i].nativeWinBuffer.s.height = mPreviewHeight;
        mBufferPackage[i].nativeWinBuffer.s.stride = mNativeWindowStride;
        mBufferPackage[i].nativeWinBuffer.s.size = mPreviewSize;

        if (mNeedInternalBuf) {
            // update the stream for the HAL buffers to allocated values
            mBufferPackage[i].nativeHalBuffer.s = mStreams[0];
            mBufferPackage[i].nativeHalBuffer.s.format = mStreams[0].format;
            mBufferPackage[i].nativeHalBuffer.s.width = mStreams[0].width;
            mBufferPackage[i].nativeHalBuffer.s.height = CameraUtils::getInterlaceHeight(mField, mStreams[0].height);
            mBufferPackage[i].nativeHalBuffer.s.stride = mStreams[0].stride;
            mBufferPackage[i].nativeHalBuffer.s.size = getNativeHandleSize(mBufferPackage[i].nativeHalBuffHandle);
            ALOGI("Hal stride %d, size %d", mBufferPackage[i].nativeHalBuffer.s.stride, mBufferPackage[i].nativeHalBuffer.s.size);
        }
        if (mLocalFlag[i] == BUFFER_OWNED) {
            camera_buffer_t *buffer = NULL;
            if (mNeedInternalBuf) {
                ALOGD("%s: Qbuffers  from start %d, addr: %p",__func__, i, mBufferPackage[i].nativeHalBuffer.addr);
                buffer= &mBufferPackage[i].nativeHalBuffer;
            } else {
                ALOGD("%s: Qbuffers %d, addr: %p",__func__, i, mBufferPackage[i].nativeWinBuffer.addr);
                buffer = &mBufferPackage[i].nativeWinBuffer;
            }
            ret = camera_stream_qbuf(mDeviceId, &buffer);
            Check(ret != OK, ret, "@%s: Camera stream qbuf failed", __func__);
        }
    }

    //start device
    ret = camera_device_start(mDeviceId);
    Check(ret != OK, ret, "@%s: Camera device start failed", __func__);

    return NO_ERROR;
}

void CameraHardwareSoc::stopPreviewInternal()
{
    ALOGI("%s :", __func__);

    /* request that the preview thread stop. */
    if (mPreviewRunning) {
        mPreviewRunning = false;
        if (!mPreviewStartDeferred) {
            mPreviewCondition.signal();
            /* wait until preview thread is stopped */
            mPreviewStoppedCondition.wait(mPreviewLock);
        }
        else
            ALOGI("%s : preview running but deferred, doing nothing", __func__);
    } else
        ALOGI("%s : preview not running, doing nothing", __func__);
    camera_device_stop(mDeviceId);
    camera_device_close(mDeviceId);
    deallocateGfxBuf();
    deallocateHalBuffers();
    mNeedInternalBuf = false;
}

void CameraHardwareSoc::stopPreview()
{
    ALOGI("%s :", __func__);

    /* request that the preview thread stop. */
    mPreviewLock.lock();
    stopPreviewInternal();
    mPreviewLock.unlock();
}

bool CameraHardwareSoc::previewEnabled()
{
    AutoMutex lock(mPreviewLock);
    ALOGI("%s : %d", __func__, mPreviewRunning);
    return mPreviewRunning;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSoc::startRecording()
{
    ALOGI("%s :", __func__);

    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (mRecordHeap[i]) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = NULL;
        }

        mRecordHeap[i] = mGetMemoryCb(-1, sizeof(struct addrs), 1, NULL);
        if (!mRecordHeap[i]) {
            ALOGE("ERR(%s): Record heap creation fail", __func__);
        }
    }

    AutoMutex lock(mRecordLock);
    mRecordRunning = true;

    return NO_ERROR;
}

void CameraHardwareSoc::stopRecording()
{
    ALOGI("%s :", __func__);

    AutoMutex lock(mRecordLock);
    mRecordRunning = false;
}

bool CameraHardwareSoc::recordingEnabled()
{
    ALOGI("%s :", __func__);

    return mRecordRunning;
}

void CameraHardwareSoc::releaseRecordingFrame(const void *opaque)
{
}

// ---------------------------------------------------------------------------
int CameraHardwareSoc::autoFocusThread()
{
    int count =0;
    int af_status =0 ;

    ALOGI("%s : starting", __func__);

    /* block until we're told to start.  we don't want to use
     * a restartable thread and requestExitAndWait() in cancelAutoFocus()
     * because it would cause deadlock between our callbacks and the
     * caller of cancelAutoFocus() which both want to grab the same lock
     * in CameraServices layer.
     */
    mFocusLock.lock();
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGI("%s : exiting on request0", __func__);
        return NO_ERROR;
    }
    mFocusCondition.wait(mFocusLock);
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGI("%s : exiting on request1", __func__);
        return NO_ERROR;
    }
    mFocusLock.unlock();

    usleep(5000);
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);

    ALOGI("%s : exiting with no error", __func__);
    return NO_ERROR;
}

status_t CameraHardwareSoc::autoFocus()
{
    ALOGI("%s :", __func__);
    /* signal autoFocusThread to run once */
    mFocusCondition.signal();
    return NO_ERROR;
}

status_t CameraHardwareSoc::cancelAutoFocus()
{
    ALOGI("%s :", __func__);

    return NO_ERROR;
}

// ---------------------------------------------------------------------------

int CameraHardwareSoc::pictureThread()
{
    ALOGI("%s :", __func__);
    int ret = NO_ERROR;

    ret = camera_jpeg_init();
    Check(ret != OK, ret, "@%s: jpeg init fail", __func__);

    // use 1 allocated hal buffer for capturing image data
    ret = allocateBuffJpeg();
    Check(ret != OK, ret, "@%s: allocate gfx buf for jpeg fail", __func__);

    int pictureSize = mjcBuffers.scalerOutBuf->size();
    ALOGI("Picture size = %d", pictureSize);

    // buffer for jpeg encode output
    camera_buffer_t postViewHeap;
    camera_buffer_t *postViewHeapPtr = &postViewHeap;
    CLEAR(postViewHeap);
    ret = posix_memalign(&postViewHeapPtr->addr, getpagesize(), pictureSize);
    if (ret != OK || postViewHeapPtr->addr == NULL) {
        ALOGE("ERR(%s): post view heap creation fail", __func__);
        return UNKNOWN_ERROR;
    }

    int thumbnailSize = 0;
    // Buffer for jpeg encode output
    camera_buffer_t postViewHeap2;
    camera_buffer_t *postViewHeapPtr2;

    if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
        thumbnailSize = mjcBuffers.scalerOutBuf2->size();
        ALOGI("thumbnail size = %d", thumbnailSize);

        // Buffer for jpeg encode output
        postViewHeapPtr2 = &postViewHeap2;
        CLEAR(postViewHeap2);
        ret = posix_memalign(&postViewHeapPtr2->addr, getpagesize(), thumbnailSize);
        if (ret != OK || postViewHeapPtr2->addr == NULL) {
            ALOGE("ERR(%s): post view heap2 creation fail", __func__);
            free(postViewHeapPtr->addr);
            return UNKNOWN_ERROR;
        }
    }

    camera_resolution_t bestIsysRes = {0, 0};
    CameraUtils::getBestISysResolution(mDeviceId, mField, mPictureWidth, mPictureHeight, bestIsysRes);

    // We use the supported ISys resolution to set the stream resolution
    int width = bestIsysRes.width;
    int height = bestIsysRes.height;

    camera_buffer_t mCamBuf;
    camera_buffer_t *buf;
    CLEAR(mCamBuf);

    // config_streams
    ret = camera_device_open(mDeviceId);
    Check(ret != OK, ret, "@%s: Camera device open failed", __func__);

    mStreams[0].id = 0;
    mStreams[0].format = mV4l2Format;
    mStreams[0].width = width;
    mStreams[0].height = height;
    mStreams[0].field = mField;
    mStreams[0].memType = V4L2_MEMORY_USERPTR;
    mStreams[0].stride = CameraUtils::getStride(mISysV4l2Format, width);
    mStreams[0].size = CameraUtils::getFrameSize(mISysV4l2Format, width, mField ? height/2 : height);
    mStream_list.num_streams = 1;
    mStream_list.streams = mStreams;
    ALOGI("stride %d, size %d", mStreams[0].stride, mStreams[0].size);

    ret = camera_device_config_streams(mDeviceId,  &mStream_list);
    Check(ret != OK, ret, "@%s: Camera device config streams failed", __func__);

    mCamBuf.s = mStreams[0];
    mCamBuf.addr = mjcBuffers.scalerInBuf->data();

    ALOGI("%s: Qbuffer addr: %p",__func__, mCamBuf.addr);
    buf = &mCamBuf;
    ret = camera_stream_qbuf(mDeviceId, &buf);
    if (ret != OK) {
        ALOGE("qbuf failed");
        return UNKNOWN_ERROR;
    }
    // start device
    ret = camera_device_start(mDeviceId);
    Check(ret != OK, ret, "@%s: Camera device start failed", __func__);

    ret = camera_stream_dqbuf(mDeviceId, mStreams[0].id, &buf);
    if (buf == NULL) {
        ALOGI("Failed to dequeue buf");
    }

    ALOGI("dqbuf success");

    // do scaling and color conversion
    BufferPackage gfxBuffPackage;
    CLEAR(gfxBuffPackage);
    gfxBuffPackage.nativeHalBuffer.addr = mjcBuffers.scalerInBuf->data();
    gfxBuffPackage.nativeHalBuffHandle = mjcBuffers.scalerInBuf->getBufferHandle();
    gfxBuffPackage.nativeHalBuffer.s.width = mjcBuffers.scalerInBuf->width();
    gfxBuffPackage.nativeHalBuffer.s.height = mjcBuffers.scalerInBuf->height();
    gfxBuffPackage.nativeHalBuffer.s.stride = mjcBuffers.scalerInBuf->stride();
    gfxBuffPackage.nativeHalBuffer.s.size = mjcBuffers.scalerInBuf->size();

    gfxBuffPackage.nativeWinBuffer.addr = mjcBuffers.scalerOutBuf->data();
    gfxBuffPackage.nativeWinBuffHandle = mjcBuffers.scalerOutBuf->getBufferHandle();
    gfxBuffPackage.nativeWinBuffer.s.width = mjcBuffers.scalerOutBuf->width();
    gfxBuffPackage.nativeWinBuffer.s.height = mjcBuffers.scalerOutBuf->height();
    gfxBuffPackage.nativeWinBuffer.s.stride = mjcBuffers.scalerOutBuf->stride();
    gfxBuffPackage.nativeWinBuffer.s.size = mjcBuffers.scalerOutBuf->size();

    ALOGI("Picture wxh: %dx%d", mPictureWidth, mPictureHeight);
    ret = mGenConvert->downScalingAndColorConversion(gfxBuffPackage);
    Check(ret != OK, ret, "@%s: Gfx Downscaling failed", __func__);

    if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
        gfxBuffPackage.nativeWinBuffer.addr = mjcBuffers.scalerOutBuf2->data();
        gfxBuffPackage.nativeWinBuffHandle = mjcBuffers.scalerOutBuf2->getBufferHandle();
        gfxBuffPackage.nativeWinBuffer.s.width = mjcBuffers.scalerOutBuf2->width();
        gfxBuffPackage.nativeWinBuffer.s.height = mjcBuffers.scalerOutBuf2->height();
        gfxBuffPackage.nativeWinBuffer.s.stride = mjcBuffers.scalerOutBuf2->stride();
        gfxBuffPackage.nativeWinBuffer.s.size = mjcBuffers.scalerOutBuf2->size();

        ALOGI("Thumbnail wxh: %dx%d", mThumbnailWidth, mThumbnailHeight);
        ret = mGenConvert->downScalingAndColorConversion(gfxBuffPackage);
        Check(ret != OK, ret, "@%s: Gfx Downscaling failed", __func__);
    }

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {

        InputBuffer inBuf;
        OutputBuffer outBuf;
        InputBuffer inBuf2;
        OutputBuffer outBuf2;

        inBuf.buf = (unsigned char*)mjcBuffers.scalerOutBuf->data();
        inBuf.width = mPictureWidth;
        inBuf.height = mPictureHeight;
        inBuf.fourcc = mV4l2Format;
        inBuf.size = pictureSize;
        inBuf.stride = mjcBuffers.scalerOutBuf->stride();

        outBuf.buf = (unsigned char*)postViewHeapPtr->addr;
        outBuf.width = mPictureWidth;
        outBuf.height = mPictureHeight;
        outBuf.quality = mJpegQuality;
        outBuf.size = pictureSize;

        nsecs_t startTime = systemTime();
        int size = camera_jpeg_encode(inBuf, outBuf);
        ALOGI("%s: encoding %dx%d need %ums, jpeg size %d, quality %d)", __FUNCTION__,
             outBuf.width, outBuf.height,
            (unsigned)((systemTime() - startTime) / 1000000), size, outBuf.quality);

        if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
            inBuf2.buf = (unsigned char*)mjcBuffers.scalerOutBuf2->data();
            inBuf2.width = mThumbnailWidth;
            inBuf2.height = mThumbnailHeight;
            inBuf2.fourcc = mV4l2Format;
            inBuf2.size = thumbnailSize;
            inBuf2.stride = mjcBuffers.scalerOutBuf2->stride();

            outBuf2.buf = (unsigned char*)postViewHeapPtr2->addr;
            outBuf2.width = mThumbnailWidth;
            outBuf2.height = mThumbnailHeight;
            outBuf2.quality = mJpegThumbnailQuality;
            outBuf2.size = thumbnailSize;

            startTime = systemTime();
            int size2 = camera_jpeg_encode(inBuf2, outBuf2);
            ALOGI("%s: encoding thumbnail %dx%d need %ums, thumbnail size %d, quality %d)", __FUNCTION__,
                 outBuf2.width, outBuf2.height,
                (unsigned)((systemTime() - startTime) / 1000000), size2, outBuf2.quality);
        }


        // Buffer for jpeg + exif
        camera_buffer_t jpegHeap;
        camera_buffer_t *jpegHeapPtr = &jpegHeap;
        CLEAR(jpegHeap);
        ret = posix_memalign(&jpegHeapPtr->addr, getpagesize(), pictureSize + EXIF_FILE_SIZE);
        if (ret != OK || jpegHeapPtr->addr == NULL) {
            ALOGE("ERR(%s): post view heap creation fail", __func__);
            free(postViewHeapPtr->addr);
            if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
                free(postViewHeapPtr2->addr);
            }
            return UNKNOWN_ERROR;
        }

        EncodePackage package;
        package.main = postViewHeapPtr;
        package.mainWidth = outBuf.width;
        package.mainHeight = outBuf.height;
        package.mainSize = pictureSize;
        package.encodedDataSize = size;
        if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
            package.thumb = postViewHeapPtr2;
            package.thumbWidth = outBuf2.width;
            package.thumbHeight = outBuf2.height;
            package.thumbSize = thumbnailSize;
        }
        package.jpegOut = jpegHeapPtr;
        package.jpegSize = EXIF_FILE_SIZE + pictureSize;
        package.params = &mInternalParameters;

        ExifMetaData *exifData = new ExifMetaData;
        ret = camera_setupExifWithMetaData(package, exifData);
        if (ret != OK) {
            ALOGE("Set up exif Failed");
        }

        // create a full JPEG image with exif data
        ret = camera_jpeg_make(package);
        if (ret != NO_ERROR) {
            ALOGE("%s: Make Jpeg Failed !", __FUNCTION__);
        }

        // buffer for call back
        camera_memory_t *jpegCBHeap = NULL;
        jpegCBHeap = mGetMemoryCb(-1, pictureSize + EXIF_FILE_SIZE, 1, 0);
        memcpy(jpegCBHeap->data, package.jpegOut->addr, pictureSize + EXIF_FILE_SIZE);

        if (mMsgEnabled & CAMERA_MSG_SHUTTER) {
            LOG1("Sending message: CAMERA_MSG_SHUTTER");
            mNotifyCb(CAMERA_MSG_SHUTTER, 1, 0, mCallbackCookie);
        }

        if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY) && mNotifyCb != NULL) {
            LOG1("Sending message: CAMERA_MSG_RAW_IMAGE_NOTIFY");
            mNotifyCb(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0, 0, mCallbackCookie);
        }

        if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) && mDataCb != NULL) {
            LOG1("Sending message: CAMERA_MSG_RAW_IMAGE");
            camera_memory_t *rawCBHeap  = mGetMemoryCb(-1, pictureSize, 1, 0);
            MEMCPY_S(rawCBHeap->data, pictureSize, postViewHeapPtr->addr, pictureSize);
            mDataCb(CAMERA_MSG_RAW_IMAGE, rawCBHeap, 0, NULL, mCallbackCookie);
            rawCBHeap->release(rawCBHeap);
        }

        if(mDataCb != NULL)
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, jpegCBHeap, 0, NULL, mCallbackCookie);

        free(jpegHeapPtr->addr);
        jpegCBHeap->release(jpegCBHeap);
    }

    ALOGI("%s : pictureThread end", __func__);

    ret = camera_device_stop(mDeviceId);
    Check(ret != OK, ret, "@%s: Camera device stop failed", __func__);

    camera_device_close(mDeviceId);

    free(postViewHeapPtr->addr);
    if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
        free(postViewHeapPtr2->addr);
    }

    deallocateBuffJpeg();
    Check(ret != OK, ret, "@%s: allocate gfx buf for jpeg fail", __func__);
    mCaptureLock.lock();
    mCaptureInProgress = false;
    mCaptureCondition.broadcast();
    mCaptureLock.unlock();

    return ret;

}

status_t CameraHardwareSoc::waitCaptureCompletion() {
    // 5 seconds timeout
    nsecs_t endTime = 5000000000LL + systemTime(SYSTEM_TIME_MONOTONIC);
    AutoMutex lock(mCaptureLock);
    while (mCaptureInProgress) {
        nsecs_t remainingTime = endTime - systemTime(SYSTEM_TIME_MONOTONIC);
        if (remainingTime <= 0) {
            ALOGE("Timed out waiting picture thread.");
            return TIMED_OUT;
        }
        ALOGD("Waiting for picture thread to complete.");
        mCaptureCondition.waitRelative(mCaptureLock, remainingTime);
    }
    return NO_ERROR;
}

status_t CameraHardwareSoc::takePicture()
{
    ALOGI("%s :", __func__);

    stopPreview();

    if (waitCaptureCompletion() != NO_ERROR) {
        return TIMED_OUT;
    }

    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) != NO_ERROR) {
        ALOGE("%s : couldn't run picture thread", __func__);
        return INVALID_OPERATION;
    }
    mCaptureLock.lock();
    mCaptureInProgress = true;
    mCaptureLock.unlock();


    return NO_ERROR;
}

status_t CameraHardwareSoc::cancelPicture()
{
    ALOGI("%s", __func__);

    if (mPictureThread.get()) {
        ALOGI("%s: waiting for picture thread to exit", __func__);
        mPictureThread->requestExitAndWait();
        ALOGI("%s: picture thread has exited", __func__);
    }

    return NO_ERROR;
}
bool CameraHardwareSoc::isSupportedPreviewSize(const int width,
                                               const int height) const
{
    unsigned int i;

    for (i = 0; i < mSupportedPreviewSizes.size(); i++) {
        if (mSupportedPreviewSizes[i].width == width &&
                mSupportedPreviewSizes[i].height == height)
            return true;
    }

    return false;
}

bool CameraHardwareSoc::isSupportedPictureSize(const int width,
                                               const int height) const
{
    unsigned int i;

    for (i = 0; i < mSupportedPictureSizes.size(); i++) {
        if (mSupportedPictureSizes[i].width == width &&
            mSupportedPictureSizes[i].height == height)
            return true;
    }

    return false;
}

status_t CameraHardwareSoc::setParameters(const CameraParameters& params)
{
    ALOGI("%s :", __func__);

    status_t ret = NO_ERROR;
    const char *flash_mode = params.get(CameraParameters::KEY_FLASH_MODE);
    if ((flash_mode != NULL) && (strcmp(flash_mode, CameraParameters::FLASH_MODE_OFF)))
        return BAD_VALUE;

    const char *focus_mode = params.get(CameraParameters::KEY_FOCUS_MODE);
    if ((focus_mode != NULL) && (strcmp(focus_mode, CameraParameters::FOCUS_MODE_FIXED)))
        return BAD_VALUE;

    // preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;

    params.getPreviewSize(&new_preview_width, &new_preview_height);

    if (new_preview_width <= 0 || new_preview_height <= 0)
        return BAD_VALUE;

    /* if someone calls us while picture thread is running, it could screw
     * up the sensor quite a bit so return error.
     */
    if (waitCaptureCompletion() != NO_ERROR) {
        return TIMED_OUT;
    }

    int min_fps = 0, max_fps = 0;
    params.getPreviewFpsRange(&min_fps, &max_fps);

    if (max_fps == 60000 && min_fps == 30000)
        mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "30000,60000");
    else if (max_fps == 30000 && min_fps == 30000)
        mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "30000,30000");

    const char *new_str_preview_format = params.getPreviewFormat();
    mFormat = previewFormat2HalEnum(new_str_preview_format);
    if (mFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP &&
        (false == isSupportedStreamFormat(HAL_PIXEL_FORMAT_YCrCb_420_SP))) {
        //when nv21 not supported, using IMPLEMENTATION_DEFINED(NV12) format
        mFormat = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    }
    if (mFormat > 0) {
        ALOGD("%s : new_preview_width x new_preview_height = %dx%d, format = %s",
                __func__, new_preview_width, new_preview_height, new_str_preview_format);
    } else {
        ALOGE("The preview format is NULL");
        return BAD_VALUE;
    }

    // Update mV4l2Format according to preview format
    int V4L2Format = HalFormat2V4L2Format(mFormat);
    for (unsigned int i = 0; i < mSupportedV4L2Formats.size(); i++) {
        if (V4L2Format == mSupportedV4L2Formats[i]) {
            mV4l2Format = V4L2Format;//mSupportedV4L2Formats[i];
            mISysV4l2Format = V4L2Format;//mSupportedV4L2Formats[i];
            ALOGD("%s: mV4l2Format = %d", __func__, mV4l2Format);
        }
    }
    // If can't find the supported V4l2 format according to the preview format,
    // choose V4l2 format according to the preview foramt is YUV or RGB
    if (mV4l2Format != V4L2Format) {
        if (V4L2Format == V4L2_PIX_FMT_NV12 || V4L2Format == V4L2_PIX_FMT_YVU420 ||
            V4L2Format == V4L2_PIX_FMT_YUYV) {
            for (unsigned int i = 0; i < mSupportedV4L2Formats.size(); i++) {
                if (IS_YUV_FORMAT(mSupportedV4L2Formats[i])) {
                    mV4l2Format = mSupportedV4L2Formats[i];
                    mISysV4l2Format = mSupportedV4L2Formats[i];
                    ALOGD("%s: mV4l2Format = %d", __func__, mV4l2Format);
                 }
             }
        } else if (V4L2Format == V4L2_PIX_FMT_RGB565) {
            for (unsigned int i = 0; i < mSupportedV4L2Formats.size(); i++) {
                if (IS_RGB_FORMAT(mSupportedV4L2Formats[i])) {
                    mV4l2Format = mSupportedV4L2Formats[i];
                    mISysV4l2Format = mSupportedV4L2Formats[i];
                    ALOGD("%s: mV4l2Format = %d", __func__, mV4l2Format);
                 }
             }
        }
    }

    getInputConfig();
    // Update mField according to the value of "CameraHalField", 0 is V4L2_FIELD_ANY, 1 is V4L2_FIELD_ALTERNATE
    int field = params.getInt("CameraHalField");
    if (field == 0)
        mField = V4L2_FIELD_ANY;
    else if (field == 1)
        mField = V4L2_FIELD_ALTERNATE;
    else
        ALOGD("%s: Invalid field or app doesn't set field, use default field value", __func__);
    ALOGD("%s: mField is %d", __func__, mField);

    // Update mDeinterlaceMode according to the value of "CameraHalDeinterlaced",
    // 0 means DEINTERLACE_OFF
    // 1 means DEINTERLACE_WEAVING
    int deinterlaceMode = params.getInt("CameraHalDeinterlaced");
    if (deinterlaceMode == 0)
        mDeinterlaceMode = DEINTERLACE_OFF;
    else if (deinterlaceMode == 1)
        mDeinterlaceMode = DEINTERLACE_WEAVING;
    else
        ALOGD("%s: Invalid deinterlace mode or doesn't set field, use default field value", __func__);

    int width = params.getInt("CameraHalInputWidth");
    int height = params.getInt("CameraHalInputHeight");
    const char* format = params.get("CameraHalInputFormat");
    if (width > 0 && height > 0) {
        mInputConfig.width = width;
        mInputConfig.height = height;
        ALOGD("%s: mInputConfig size is %d x %d", __func__, width, height);
    }
    if (imageFormat2HalEnum(format) > 0) {
        int halInputFormat = imageFormat2HalEnum(format);
        mInputConfig.format = HalFormat2V4L2Format(halInputFormat);
        ALOGD("%s: mInputConfig format is %d(%s)", __func__, mInputConfig.format, format);
    }

    if (0 < new_preview_width && 0 < new_preview_height &&
            new_str_preview_format != NULL &&
            isSupportedPreviewSize(new_preview_width, new_preview_height)) {
        mPreviewWidth = new_preview_width;
        mPreviewHeight = new_preview_height;
        mParameters.setPreviewSize(new_preview_width, new_preview_height);
        mParameters.setVideoSize(new_preview_width, new_preview_height);
        mParameters.setPreviewFormat(new_str_preview_format);
    } else {
        ALOGE("%s: Invalid preview size(%dx%d)",
                __func__, new_preview_width, new_preview_height);

        ret = NO_ERROR;
    }

    int new_picture_width  = 0;
    int new_picture_height = 0;
    params.getPictureSize(&new_picture_width, &new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height &&
        isSupportedPreviewSize(new_picture_width, new_picture_height)) {
        mPictureWidth = new_picture_width;
        mPictureHeight = new_picture_height;
        mParameters.setPictureSize(new_picture_width, new_picture_height);
    }

    mThumbnailWidth = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    mThumbnailHeight = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, mThumbnailWidth);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, mThumbnailHeight);

    mJpegQuality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    mJpegThumbnailQuality = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    mParameters.set(CameraParameters::KEY_JPEG_QUALITY, mJpegQuality);
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, mJpegThumbnailQuality);

    int rotation = params.getInt(CameraParameters::KEY_ROTATION);
    mParameters.set(CameraParameters::KEY_ROTATION, rotation);

    float gps_latitude = 0.0;
    if (params.get(CameraParameters::KEY_GPS_LATITUDE) != NULL) {
        gps_latitude = params.getFloat(CameraParameters::KEY_GPS_LATITUDE);
    }
    float gps_longitude = 0.0;
    if (params.get(CameraParameters::KEY_GPS_LONGITUDE) != NULL) {
        gps_longitude = params.getFloat(CameraParameters::KEY_GPS_LONGITUDE);
    }
    float gps_altitude = 0.0;
    if (params.get(CameraParameters::KEY_GPS_ALTITUDE) != NULL) {
        gps_altitude = params.getFloat(CameraParameters::KEY_GPS_ALTITUDE);
    }
    int gps_timestamp = 0;
    if (params.get(CameraParameters::KEY_GPS_TIMESTAMP) != NULL) {
        gps_timestamp = params.getInt(CameraParameters::KEY_GPS_TIMESTAMP);
    }
    char gps_processing_method[MAX_NUM_GPS_PROCESSING_METHOD];
    memset(gps_processing_method, 0, sizeof(gps_processing_method));
    const char* gps_method = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    if (gps_method != NULL) {
        MEMCPY_S(gps_processing_method, MAX_NUM_GPS_PROCESSING_METHOD - 1, gps_method, strlen(gps_method));
    }
    mParameters.setFloat(CameraParameters::KEY_GPS_LATITUDE, gps_latitude);
    mParameters.setFloat(CameraParameters::KEY_GPS_LONGITUDE, gps_longitude);
    mParameters.setFloat(CameraParameters::KEY_GPS_ALTITUDE, gps_altitude);
    mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, gps_timestamp);
    mParameters.set(CameraParameters::KEY_GPS_PROCESSING_METHOD, gps_processing_method);

    float focal_length = params.getFloat(CameraParameters::KEY_FOCAL_LENGTH);
    mParameters.setFloat(CameraParameters::KEY_FOCAL_LENGTH, focal_length);

    setInternalParameters();

    return ret;
}

CameraParameters CameraHardwareSoc::getParameters() const
{
    ALOGI("%s :", __func__);
    return mParameters;
}

status_t CameraHardwareSoc::setInternalParameters()
{
    status_t ret = NO_ERROR;

    ALOGI("%s :", __func__);
    int new_jpeg_quality  = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
    mInternalParameters.setJpegQuality(new_jpeg_quality);

    int new_jpeg_thumb_quality  = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    mInternalParameters.setJpegThumbnailQuality(new_jpeg_thumb_quality);

    int new_rotation = mParameters.getInt(CameraParameters::KEY_ROTATION);
    mInternalParameters.setJpegRotation(new_rotation);

    double new_gps_latitude = 0.0;
    if (mParameters.get(CameraParameters::KEY_GPS_LATITUDE) != NULL) {
        new_gps_latitude = (double)mParameters.getFloat(CameraParameters::KEY_GPS_LATITUDE);
    }
    double new_gps_longitude = 0.0;
    if (mParameters.get(CameraParameters::KEY_GPS_LONGITUDE) != NULL) {
        new_gps_longitude = (double)mParameters.getFloat(CameraParameters::KEY_GPS_LONGITUDE);
    }
    double new_gps_altitude = 0.0;
    if (mParameters.get(CameraParameters::KEY_GPS_ALTITUDE) != NULL) {
        new_gps_altitude = (double)mParameters.getFloat(CameraParameters::KEY_GPS_ALTITUDE);
    }
    int64_t new_gps_timestamp = 0;
    if (mParameters.get(CameraParameters::KEY_GPS_TIMESTAMP) != NULL) {
        new_gps_timestamp = (int64_t)mParameters.getInt(CameraParameters::KEY_GPS_TIMESTAMP);
    }
    char new_gps_processing_method[MAX_NUM_GPS_PROCESSING_METHOD] = "\0";
    const char* new_gps_method = mParameters.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    if (new_gps_method != NULL) {
        MEMCPY_S(new_gps_processing_method, MAX_NUM_GPS_PROCESSING_METHOD - 1, new_gps_method, strlen(new_gps_method));
        size_t size = std::min((size_t)(MAX_NUM_GPS_PROCESSING_METHOD - 1), strlen(new_gps_method));
        new_gps_processing_method[size] = '\0';
    }
    double new_gps[3] = {new_gps_latitude, new_gps_longitude, new_gps_altitude};
    mInternalParameters.setJpegGpsCoordinates(new_gps);
    mInternalParameters.setJpegGpsTimeStamp(new_gps_timestamp);
    mInternalParameters.setJpegGpsProcessingMethod(new_gps_processing_method);

    float new_focal_length = mParameters.getFloat(CameraParameters::KEY_FOCAL_LENGTH);
    mInternalParameters.setFocalLength(new_focal_length);

    return ret;

}

status_t CameraHardwareSoc::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardwareSoc::release()
{
    ALOGI("%s", __func__);

    /* shut down any threads we have that might be running.  do it here
     * instead of the destructor.  we're guaranteed to be on another thread
     * than the ones below.  if we used the destructor, since the threads
     * have a reference to this object, we could wind up trying to wait
     * for ourself to exit, which is a deadlock.
     */
    if (mPreviewThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable or running.  signal it so it wakes
         * up and can exit.
         */
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
        ALOGI("Preview thread released");
    }

    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
        ALOGI("Picture thread released");
    }

    if (mAutoFocusThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable.  signal it so it wakes up and can exit.
         */
        mFocusLock.lock();
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mFocusCondition.signal();
        mFocusLock.unlock();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
        ALOGI("AutoFocus thread released");
    }

    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (mRecordHeap[i]) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = NULL;
        }
    }
}

status_t CameraHardwareSoc::storeMetaDataInBuffers(bool enable)
{
    ALOGI("%s", __func__);

    if (!enable) {
        ALOGE("Non-metadata buffer mode is not supported!");
        return INVALID_OPERATION;
    }

    return OK;
}

int CameraHardwareSoc::displayBuffer(int index)
{
    int err = NO_ERROR;
    int dequeueIdx = BAD_INDEX;
    buffer_handle_t *buffer_handle = (buffer_handle_t *)mBufferPackage[index].nativeWinBuffHandle;

    if (BUFFER_NOT_OWNED == mLocalFlag[index]) {
        ALOGE("%s: buffer to be enqueued is not owned", __func__);
        return INVALID_OPERATION;
    }

    mGrallocHal->unlock(mGrallocHal, *buffer_handle);
    err = mWindow->enqueue_buffer(mWindow, buffer_handle);
    if(err != 0) {
        ALOGE("%s: enqueue_buffer failed, err = %d", __func__, err);
    } else {
        LOG1("%s: enqueue_buffer hdl=%p", __func__, mBufferPackage[index].nativeWinBuffHandle);
        mLocalFlag[index] = BUFFER_NOT_OWNED;
    }

    buffer_handle = NULL;
    int stride = 0;
    err = mWindow->dequeue_buffer(mWindow, &buffer_handle, &stride);
    if (err == NO_ERROR && buffer_handle != NULL) {
        for(int i = 0; i < mBufferCount; i++) {
            if(mBufferPackage[i].nativeWinBuffHandle == buffer_handle) {
                LOG1("%s: Found buffer in idx:%d", __func__, i);
                mLocalFlag[i] = BUFFER_OWNED;
                dequeueIdx = i;
                void *vaddr = NULL;
                if (mGrallocHal->lock(mGrallocHal, *buffer_handle, mUsage, 0, 0,
                                      mPreviewWidth, mPreviewHeight + 1, &vaddr) !=
                    NO_ERROR) {
                    ALOGE("%s: could not obtain gralloc buffer", __func__);
                    err = mWindow->cancel_buffer(mWindow, buffer_handle);
                    return err;
                }
                mBufferPackage[i].nativeWinBuffer.addr = vaddr;
                break;
            }
        }
    } else {
        ALOGD("%s: dequeue_buffer, no free buffer from display now", __func__);
    }

    return dequeueIdx;
}

int CameraHardwareSoc::allocateGfxBuf(int count)
{
    int err = 0;
    status_t ret = NO_ERROR;

    ALOGI(" %s : E , width:%d, height:%d", __FUNCTION__, mPreviewWidth, mPreviewHeight);

    if (!mWindow) {
        ALOGE("Invalid native window");
        return INVALID_OPERATION;
    }

    if ((mPreviewWidth <= 0) || (mPreviewHeight <= 0)) {
        ALOGE("Invalid preview size");
        return INVALID_OPERATION;
    }

    // increment buffer count by min undequeued buffer.
    err = mWindow->get_min_undequeued_buffer_count(mWindow,&mMinUndequeuedBuffers);
    if (err != 0) {
        ALOGE("get_min_undequeued_buffer_count  failed: %s (%d)", strerror(-err), -err);
        ret = UNKNOWN_ERROR;
        goto end;
    }
    count += mMinUndequeuedBuffers;

    if (count >= MAX_BUFFERS) {
        ALOGE("%s: Too many buffers failed: %d", __func__, count);
        ret = UNKNOWN_ERROR;
        goto end;
    }

    err = mWindow->set_buffer_count(mWindow, count);
    if (err != 0) {
        ALOGE("set_buffer_count failed: %s (%d)", strerror(-err), -err);
        ret = UNKNOWN_ERROR;
        goto end;
    }
    ALOGD("%s: set buffer count to %d, minUnDequeuedBuffer is %d", __func__, count, mMinUndequeuedBuffers);

    err = mWindow->set_usage(mWindow, mUsage);
    if(err != 0) {
        ALOGE("%s: set_usage rc = %d", __func__, err);
        ret = UNKNOWN_ERROR;
        goto end;
    }

    err = mWindow->set_buffers_geometry(mWindow, mPreviewWidth, mPreviewHeight, mFormat);
    if (err != 0) {
        ALOGE("%s: set_buffers_geometry failed: %s (%d)", __func__, strerror(-err), -err);
        ret = UNKNOWN_ERROR;
        goto end;
    }
    mWindow->set_crop(mWindow, 0, 0, mPreviewWidth, mPreviewHeight);

    ALOGD("%s: usage = %d, geometry: Windows:%p, (%dx%d), format: %d",__func__, mUsage, mWindow,
          mPreviewWidth, mPreviewHeight, mFormat);

    // allocate cnt number of buffers from native window
    for (int cnt = 0; cnt < count; cnt++) {
        int stride;
        err = mWindow->dequeue_buffer(mWindow, &mBufferPackage[cnt].nativeWinBuffHandle, &stride);
        if(!err) {
            ALOGI("dequeue buf hdl =%p", mBufferPackage[cnt].nativeWinBuffHandle);
            mLocalFlag[cnt] = BUFFER_OWNED;
        } else {
            mLocalFlag[cnt] = BUFFER_NOT_OWNED;
            ALOGE("%s: dequeue_buffer idx = %d err = %d", __func__, cnt, err);
        }

        LOG1("%s: dequeue buf: %p stride %d\n", __func__, mBufferPackage[cnt].nativeWinBuffHandle, stride);

        if(err != 0) {
            ALOGE("%s: dequeue_buffer failed: %s (%d)", __func__, strerror(-err), -err);
            ret = UNKNOWN_ERROR;
            for(int i = 0; i < cnt; i++) {
                if(mLocalFlag[i] != BUFFER_NOT_OWNED) {
                    err = mWindow->cancel_buffer(mWindow, mBufferPackage[i].nativeWinBuffHandle);
                    ALOGD("%s: cancel_buffer: hdl =%p", __func__, (*mBufferPackage[i].nativeWinBuffHandle));
                }
                mLocalFlag[i] = BUFFER_NOT_OWNED;
                mBufferPackage[i].nativeWinBuffHandle = NULL;
            }
            goto end;
        }
        void *vaddr = NULL;
        if (mGrallocHal->lock(mGrallocHal, *mBufferPackage[cnt].nativeWinBuffHandle, mUsage, 0, 0,
                              mPreviewWidth, mPreviewHeight , &vaddr) != NO_ERROR) {
            ALOGE("%s: could not obtain gralloc buffer", __func__);
            err = mWindow->cancel_buffer(mWindow, mBufferPackage[cnt].nativeWinBuffHandle);
            return err;
        }

        if (vaddr == NULL) {
            ALOGE("%s: Locked a NULL buffer", __func__);
            return -1;
        }

        mPreviewSize = getNativeHandleSize(mBufferPackage[cnt].nativeWinBuffHandle);
        mNativeWindowStride = stride;
        ALOGI("configure mBuffer[%d] to %p size %d stride %d", cnt, vaddr, mPreviewSize, stride);
        mBufferPackage[cnt].nativeWinBuffer.addr = vaddr;
    }

    mBufferCount = count;

    //Cancel min_undequeued_buffer buffers back to the window
    for (int i = 0; i < mMinUndequeuedBuffers; i ++) {
        err = mWindow->cancel_buffer(mWindow, mBufferPackage[i].nativeWinBuffHandle);
        mLocalFlag[i] = BUFFER_NOT_OWNED;
        mBufferPackage[i].nativeWinBuffer.addr = NULL;
    }
end:
    ALOGI(" %s : X ", __func__);
    return ret;
}
bool CameraHardwareSoc::isSupportedStreamFormat(int halFormat)
{
    icamera::camera_info_t info;
    CLEAR(info);

    get_camera_info(mDeviceId, info);
    supported_stream_config_array_t availableConfigs;
    info.capability->getSupportedStreamConfig(availableConfigs);
    supported_stream_config_t config;
    if (!availableConfigs.size()) {
        ALOGE("No supported configs, check xml");
        return false;
    }
    for (size_t i = 0; i < availableConfigs.size(); i++) {
        config = availableConfigs[i];
        if (config.format == HalFormat2V4L2Format(halFormat)) {
            return true;
        }
    }
    return false;
}

int CameraHardwareSoc::V4L2Format2HalFormat(int V4L2Format) {
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
            format = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;
        case V4L2_PIX_FMT_NV16:
            format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
            break;
        default:
            format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            ALOGE("Unsupported V4L2 format: %d, use default HAL format", V4L2Format);
    }

    ALOGD("%s: HAL format = %d", __func__, format);
    return format;
}

int CameraHardwareSoc::previewFormat2HalEnum(const char * format) {
    if (!format) {
        ALOGE("format is NULL, use the default value");
        return -1;
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_YUV420SP)) {
        return HAL_PIXEL_FORMAT_YCrCb_420_SP;   //NV21
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_YUV420P)) {
        return HAL_PIXEL_FORMAT_YV12;                     //YV12
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_RGB565)) {
        return HAL_PIXEL_FORMAT_RGB_565;                  //RGB565
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_YUV422SP)) {
        return HAL_PIXEL_FORMAT_YCbCr_422_SP;             //NV16
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_YUV422I)) {
        return HAL_PIXEL_FORMAT_YCbCr_422_I;              //YUY2
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_RGBA8888)) {
        return HAL_PIXEL_FORMAT_RGBA_8888;              //RGB8888
    } else if (!strcmp(format, CameraParameters::PIXEL_FORMAT_BAYER_RGGB)) {
        return HAL_PIXEL_FORMAT_RAW16;              //Raw sensor data
    } else {
        ALOGE("Unsupported format by android: %s, using the HAL_DEFINED format", format);
        return HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    }
}

int CameraHardwareSoc::imageFormat2HalEnum(const char * format) {
    if (!format) {
        ALOGE("format is NULL, use the default value");
        return -1;
    } else if (!strcmp(format, "NV21")) {
        return HAL_PIXEL_FORMAT_YCrCb_420_SP;   //NV21
    } else if (!strcmp(format, "YV12")) {
        return HAL_PIXEL_FORMAT_YV12;                     //YV12
    } else if (!strcmp(format, "RGB_565")) {
        return HAL_PIXEL_FORMAT_RGB_565;                  //RGB565
    } else if (!strcmp(format, "NV16")) {
        return HAL_PIXEL_FORMAT_YCbCr_422_SP;             //NV16
    } else if (!strcmp(format, "YUY2")) {
        return HAL_PIXEL_FORMAT_YCbCr_422_I;              //YUY2
    } else {
        ALOGE("Unsupported format: %s", format);
        return -1;
    }
}

int CameraHardwareSoc::HalFormat2V4L2Format(int HalFormat) {
    int format = V4L2_PIX_FMT_NV12;

    switch (HalFormat) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            format = V4L2_PIX_FMT_NV12;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            format = V4L2_PIX_FMT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
            format = V4L2_PIX_FMT_RGB32;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            format = V4L2_PIX_FMT_NV21;
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
            ALOGE("Unsupported HAL format: %d, use default V4L2 format", HalFormat);
    }

    ALOGD("%s: V4L2 format = %d", __func__, format);
    return format;
}

int CameraHardwareSoc::getExtraHeight(int w, int h, int gfxFmt, int v4l2Fmt)
{
    int extraHeight = 0;
    int size = CameraUtils::getFrameSize(v4l2Fmt, w, h);
    sp<CameraGfxBuffer> gfxBuf = allocateGraphicBuffer(w, h, gfxFmt, v4l2Fmt);
    if (gfxBuf == NULL) {
        LOGE("Failed to allocate graphics HAL buffers, getExtraHeight return 0");
        return 0;
    }
    if ((int)gfxBuf->size() < size) {
        extraHeight = (size - (int)gfxBuf->size()) / gfxBuf->stride();
        if ((size - (int)gfxBuf->size()) % gfxBuf->stride() != 0)
            extraHeight = extraHeight + 1;
    }
    ALOGD("Qbuf request buffer size %d, Gfx Hal buffer size %d, extraHeight = %d", size, (int)gfxBuf->size(), extraHeight);
    return extraHeight;
}

/**
  * The size of the HAL buffers will be based on the supported ISys
  * resolution. This buffers collect the output from the ISys
  * and will be used as input to the graphics scaler.
  */
int CameraHardwareSoc::allocateHalBuffers(int count)
{
    ALOGI("%s:", __FUNCTION__);

    camera_resolution_t bestIsysRes = {0, 0};
    CameraUtils::getBestISysResolution(mDeviceId, mField, mPreviewWidth, mPreviewHeight, bestIsysRes);

    int srcFmt = mV4l2Format;
    int srcWidth = bestIsysRes.width;
    int srcHeight = CameraUtils::getInterlaceHeight(mField, bestIsysRes.height);

    int format = V4L2Format2HalFormat(mV4l2Format);
    /** This is a WA as Isys output needed buffer size shouldn't
    * be smaller than gfx allocate buffer size. When allocate gfx
    * buffer, add extra height to make sure the gfx buffer size
    * isn't smaller than Isys needed buffer size.
    */
    int extraHeight = getExtraHeight(srcWidth, srcHeight, format, srcFmt);
    for (int cnt = 0; cnt < count; cnt++) {
        sp<CameraGfxBuffer> gfxBuf = allocateGraphicBuffer(srcWidth, srcHeight + extraHeight, format, srcFmt);
        if (gfxBuf == NULL) {
            ALOGE("Failed to allocate graphics HAL buffers");
            return UNKNOWN_ERROR;
        }

        mBufferPackage[cnt].nativeHalBuffer.addr = gfxBuf->data();
        mBufferPackage[cnt].nativeHalBuffHandle = gfxBuf->getBufferHandle();
        mGfxPtrs.push_back(gfxBuf);
    }
    return 0;
}

int CameraHardwareSoc::allocateBuffJpeg()
{
    ALOGI("%s:", __FUNCTION__);

    camera_resolution_t bestIsysRes = {0, 0};
    CameraUtils::getBestISysResolution(mDeviceId, mField, mPictureWidth, mPictureHeight, bestIsysRes);
    int srcFmt = mV4l2Format;
    int srcWidth = bestIsysRes.width;

    int srcHeight = 0;
    if (mField == V4L2_FIELD_ALTERNATE) {
        srcHeight = CameraUtils::getInterlaceHeight(mField, bestIsysRes.height);
    } else {
        srcHeight = bestIsysRes.height;
    }

    int format = V4L2Format2HalFormat(mV4l2Format);
    /** This is a WA as Isys output needed buffer size shouldn't
    * be smaller than gfx allocate buffer size. When allocate gfx
    * buffer, add extra height to make sure the gfx buffer size
    * isn't smaller than Isys needed buffer size.
    */
    int extraHeight = getExtraHeight(srcWidth, srcHeight, format, srcFmt);
    sp<CameraGfxBuffer> gfxBuf = allocateGraphicBuffer(srcWidth, srcHeight + extraHeight,
                                                       format, srcFmt);

    if (gfxBuf == NULL) {
        ALOGE("Failed to allocate graphics buffer for Jpeg");
        return UNKNOWN_ERROR;
    }

    mjcBuffers.scalerInBuf = gfxBuf;


    int destFmt = mV4l2Format;
    int destWidth = mPictureWidth;
    int destHeight = mPictureHeight;

    sp<CameraGfxBuffer> gfxBuf2 = allocateGraphicBuffer(destWidth, destHeight,
                                                        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                                                        destFmt);

    if (gfxBuf == NULL) {
        ALOGE("Failed to allocate graphics buffer for Jpeg");
        return UNKNOWN_ERROR;
    }
    mjcBuffers.scalerOutBuf = gfxBuf2;

    destFmt = mV4l2Format;
    destWidth = mThumbnailWidth;
    destHeight = mThumbnailHeight;
    if ((destWidth > 0) && (destHeight > 0)) {
        sp<CameraGfxBuffer> gfxBuf3 = allocateGraphicBuffer(destWidth, destHeight,
                                                            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                                                            destFmt);

        if (gfxBuf == NULL) {
            LOGE("Failed to allocate graphics buffer for Jpeg");
            return UNKNOWN_ERROR;
        }
        mjcBuffers.scalerOutBuf2 = gfxBuf3;
    }

    return OK;

}


void CameraHardwareSoc::deallocateGfxBuf()
{
    ALOGI("%s: E ", __FUNCTION__);

    for (int cnt = 0; cnt < mBufferCount; cnt++) {
        if(mLocalFlag[cnt] != BUFFER_NOT_OWNED) {
            if (mWindow) {
                mWindow->cancel_buffer(mWindow, mBufferPackage[cnt].nativeWinBuffHandle);
                ALOGD("cancel_buffer: hdl =%p", (*mBufferPackage[cnt].nativeWinBuffHandle));
            } else {
                ALOGE("Preview window is NULL, cannot cancel_buffer: hdl =%p",(*mBufferPackage[cnt].nativeWinBuffHandle));
            }
        }
        mLocalFlag[cnt] = BUFFER_NOT_OWNED;
        mBufferPackage[cnt].nativeWinBuffer.addr = NULL;
    }

    ALOGI(" %s : X ",__FUNCTION__);
}

void CameraHardwareSoc::deallocateHalBuffers()
{
    ALOGI("%s:", __FUNCTION__);
    mGfxPtrs.clear();
    mBufferCount = 0;
    ALOGI(" %s : X ",__FUNCTION__);
}

void CameraHardwareSoc::deallocateBuffJpeg()
{
    ALOGI("%s:", __FUNCTION__);
    mjcBuffers.scalerInBuf.clear();
    mjcBuffers.scalerOutBuf.clear();
    if ((mThumbnailWidth > 0) && (mThumbnailHeight > 0)) {
        mjcBuffers.scalerOutBuf2.clear();
    }
    ALOGI(" %s : X ",__FUNCTION__);
}

void CameraHardwareSoc::writeData(const void* data, int size, const char* fileName)
{
    Check((data == NULL || size == 0 || fileName == NULL), VOID_VALUE, "Nothing needs to be dumped");

    FILE *fp = fopen (fileName, "w+");
    Check(fp == NULL, VOID_VALUE, "open dump file %s failed", fileName);

    LOG1("Write data to file:%s", fileName);
    if ((fwrite(data, size, 1, fp)) != 1)
        LOGW("Error or short count writing %d bytes to %s", size, fileName);
    fclose (fp);
}

const char* CameraHardwareSoc::getCurrentSensorName()
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
            LOGW("set sensor name: %s not be supported, use default(mondello)", value);
            return NULL;
        }
    } else {
        ALOGI("Camera input not been set, return NULL, use default sensor config");
        return NULL;
    }
}

static camera_info sCameraInfo[] = {
    {
        0,  /* cameraId */
        0,  /* orientation */
    },
    {
        1,  /* cameraId */
        0,  /* orientation */
    },
    {
        2,  /* cameraId */
        0,  /* orientation */
    },
    {
        3,  /* cameraId */
        0,  /* orientation */
    },
    {
        4,  /* cameraId */
        0,  /* orientation */
    },
    {
        5,  /* cameraId */
        0,  /* orientation */
    },
    {
        6,  /* cameraId */
        0,  /* orientation */
    },
    {
        7,  /* cameraId */
        0,  /* orientation */
    }
};

/** Close this device */

static camera_device_t *g_cam_device[MAX_CAMERAS];

static int HAL_camera_device_close(struct hw_device_t* device)
{
    ALOGI("%s", __func__);
    if (device) {
        camera_device_t *cam_device = (camera_device_t *)device;
        delete static_cast<CameraHardwareSoc *>(cam_device->priv);
        for (int i = 0; i < MAX_CAMERAS; i++) {
            if (g_cam_device[i] == cam_device) {
                delete cam_device;
                g_cam_device[i] = NULL;
                break;
            }
        }
    }
    return 0;
}

static inline CameraHardwareSoc *obj(struct camera_device *dev)
{
    return reinterpret_cast<CameraHardwareSoc *>(dev->priv);
}

/** Set the preview_stream_ops to which preview frames are sent */
static int HAL_camera_device_set_preview_window(struct camera_device *dev,
                                                struct preview_stream_ops *buf)
{
    ALOGI("%s", __func__);
    return obj(dev)->setPreviewWindow(buf);
}

/** Set the notification and data callbacks */
static void HAL_camera_device_set_callbacks(struct camera_device *dev,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
    ALOGI("%s", __func__);
    obj(dev)->setCallbacks(notify_cb, data_cb, data_cb_timestamp,
                           get_memory,
                           user);
}

/**
 * The following three functions all take a msg_type, which is a bitmask of
 * the messages defined in include/ui/Camera.h
 */

/**
 * Enable a message, or set of messages.
 */
static void HAL_camera_device_enable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGI("%s", __func__);
    obj(dev)->enableMsgType(msg_type);
}

/**
 * Disable a message, or a set of messages.
 *
 * Once received a call to disableMsgType(CAMERA_MSG_VIDEO_FRAME), camera
 * HAL should not rely on its client to call releaseRecordingFrame() to
 * release video recording frames sent out by the cameral HAL before and
 * after the disableMsgType(CAMERA_MSG_VIDEO_FRAME) call. Camera HAL
 * clients must not modify/access any video recording frame after calling
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME).
 */
static void HAL_camera_device_disable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGI("%s", __func__);
    obj(dev)->disableMsgType(msg_type);
}

/**
 * Query whether a message, or a set of messages, is enabled.  Note that
 * this is operates as an AND, if any of the messages queried are off, this
 * will return false.
 */
static int HAL_camera_device_msg_type_enabled(struct camera_device *dev, int32_t msg_type)
{
    ALOGI("%s", __func__);
    return obj(dev)->msgTypeEnabled(msg_type);
}

/**
 * Start preview mode.
 */
static int HAL_camera_device_start_preview(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->startPreview();
}

/**
 * Stop a previously started preview.
 */
static void HAL_camera_device_stop_preview(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    obj(dev)->stopPreview();
}

/**
 * Returns true if preview is enabled.
 */
static int HAL_camera_device_preview_enabled(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->previewEnabled();
}

/**
 * Request the camera HAL to store meta data or real YUV data in the video
 * buffers sent out via CAMERA_MSG_VIDEO_FRAME for a recording session. If
 * it is not called, the default camera HAL behavior is to store real YUV
 * data in the video buffers.
 *
 * This method should be called before startRecording() in order to be
 * effective.
 *
 * If meta data is stored in the video buffers, it is up to the receiver of
 * the video buffers to interpret the contents and to find the actual frame
 * data with the help of the meta data in the buffer. How this is done is
 * outside of the scope of this method.
 *
 * Some camera HALs may not support storing meta data in the video buffers,
 * but all camera HALs should support storing real YUV data in the video
 * buffers. If the camera HAL does not support storing the meta data in the
 * video buffers when it is requested to do do, INVALID_OPERATION must be
 * returned. It is very useful for the camera HAL to pass meta data rather
 * than the actual frame data directly to the video encoder, since the
 * amount of the uncompressed frame data can be very large if video size is
 * large.
 *
 * @param enable if true to instruct the camera HAL to store
 *      meta data in the video buffers; false to instruct
 *      the camera HAL to store real YUV data in the video
 *      buffers.
 *
 * @return OK on success.
 */
static int HAL_camera_device_store_meta_data_in_buffers(struct camera_device *dev, int enable)
{
    ALOGI("%s", __func__);
    return obj(dev)->storeMetaDataInBuffers(enable);
}

/**
 * Start record mode. When a record image is available, a
 * CAMERA_MSG_VIDEO_FRAME message is sent with the corresponding
 * frame. Every record frame must be released by a camera HAL client via
 * releaseRecordingFrame() before the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME). After the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames,
 * and the client must not modify/access any video recording frames.
 */
static int HAL_camera_device_start_recording(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->startRecording();
}

/**
 * Stop a previously started recording.
 */
static void HAL_camera_device_stop_recording(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    obj(dev)->stopRecording();
}

/**
 * Returns true if recording is enabled.
 */
static int HAL_camera_device_recording_enabled(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->recordingEnabled();
}

/**
 * Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
 *
 * It is camera HAL client's responsibility to release video recording
 * frames sent out by the camera HAL before the camera HAL receives a call
 * to disableMsgType(CAMERA_MSG_VIDEO_FRAME). After it receives the call to
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames.
 */
static void HAL_camera_device_release_recording_frame(struct camera_device *dev,
                                const void *opaque)
{
    ALOGI("%s", __func__);
    obj(dev)->releaseRecordingFrame(opaque);
}

/**
 * Start auto focus, the notification callback routine is called with
 * CAMERA_MSG_FOCUS once when focusing is complete. autoFocus() will be
 * called again if another auto focus is needed.
 */
static int HAL_camera_device_auto_focus(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->autoFocus();
}

/**
 * Cancels auto-focus function. If the auto-focus is still in progress,
 * this function will cancel it. Whether the auto-focus is in progress or
 * not, this function will return the focus position to the default.  If
 * the camera does not support auto-focus, this is a no-op.
 */
static int HAL_camera_device_cancel_auto_focus(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->cancelAutoFocus();
}

/**
 * Take a picture.
 */
static int HAL_camera_device_take_picture(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->takePicture();
}

/**
 * Cancel a picture that was started with takePicture. Calling this method
 * when no picture is being taken is a no-op.
 */
static int HAL_camera_device_cancel_picture(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    return obj(dev)->cancelPicture();
}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported.
 */
static int HAL_camera_device_set_parameters(struct camera_device *dev,
                                            const char *parms)
{
    ALOGI("%s", __func__);
    String8 str(parms);
    CameraParameters p(str);
    return obj(dev)->setParameters(p);
}

/** Return the camera parameters. */
char *HAL_camera_device_get_parameters(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    String8 str;
    CameraParameters parms = obj(dev)->getParameters();
    str = parms.flatten();
    return strdup(str.string());
}

void HAL_camera_device_put_parameters(struct camera_device *dev, char *parms)
{
    ALOGI("%s", __func__);
    free(parms);
}

/**
 * Send command to camera driver.
 */
static int HAL_camera_device_send_command(struct camera_device *dev,
                    int32_t cmd, int32_t arg1, int32_t arg2)
{
    ALOGI("%s", __func__);
    return obj(dev)->sendCommand(cmd, arg1, arg2);
}

/**
 * Release the hardware resources owned by this object.  Note that this is
 * *not* done in the destructor.
 */
static void HAL_camera_device_release(struct camera_device *dev)
{
    ALOGI("%s", __func__);
    obj(dev)->release();
}

/**
 * Dump state of the camera hardware
 */
static int HAL_camera_device_dump(struct camera_device *dev, int fd)
{
    ALOGI("%s", __func__);
    return 0;
}

static int HAL_getNumberOfCameras()
{
    ALOGI("%s", __func__);

    int numofCameras = std::min(PlatformData::numberOfCameras(), MAX_CAMERAS);

    ALOGD("num of camera = %x", numofCameras);
    return numofCameras;
}

static int HAL_getCameraInfo(int cameraId, struct camera_info *cameraInfo)
{
    ALOGI("%s", __func__);

    MEMCPY_S(cameraInfo, sizeof(camera_info), &sCameraInfo[cameraId], sizeof(camera_info));

    return 0;
}

#define SET_METHOD(m) .m= HAL_camera_device_##m

static camera_device_ops_t camera_device_ops = {
        SET_METHOD(set_preview_window),
        SET_METHOD(set_callbacks),
        SET_METHOD(enable_msg_type),
        SET_METHOD(disable_msg_type),
        SET_METHOD(msg_type_enabled),
        SET_METHOD(start_preview),
        SET_METHOD(stop_preview),
        SET_METHOD(preview_enabled),
        SET_METHOD(store_meta_data_in_buffers),
        SET_METHOD(start_recording),
        SET_METHOD(stop_recording),
        SET_METHOD(recording_enabled),
        SET_METHOD(release_recording_frame),
        SET_METHOD(auto_focus),
        SET_METHOD(cancel_auto_focus),
        SET_METHOD(take_picture),
        SET_METHOD(cancel_picture),
        SET_METHOD(set_parameters),
        SET_METHOD(get_parameters),
        SET_METHOD(put_parameters),
        SET_METHOD(send_command),
        SET_METHOD(release),
        SET_METHOD(dump),
};

#undef SET_METHOD

#define NAMED_FIELD_INITIALIZER(x) .x=

static int HAL_camera_device_open(const struct hw_module_t* module,
                                  const char *id,
                                  struct hw_device_t** device)
{
    ALOGI("%s", __func__);

    int cameraId = atoi(id);
    if (cameraId < 0 || cameraId >= HAL_getNumberOfCameras()) {
        ALOGE("Invalid camera ID %s", id);
        return -EINVAL;
    }

    if (g_cam_device[cameraId]) {
        if (obj(g_cam_device[cameraId])->getCameraId() == cameraId) {
            ALOGI("returning existing camera ID %s", id);
            goto done;
        } else {
            ALOGE("Cannot open camera %d. camera %d is already running!",
                    cameraId, obj(g_cam_device[cameraId])->getCameraId());
            return -ENOSYS;
        }
    }

    g_cam_device[cameraId] = new (camera_device_t);

    g_cam_device[cameraId]->common.tag     = HARDWARE_DEVICE_TAG;
    g_cam_device[cameraId]->common.version = 1;
    g_cam_device[cameraId]->common.module  = const_cast<hw_module_t *>(module);
    g_cam_device[cameraId]->common.close   = HAL_camera_device_close;

    g_cam_device[cameraId]->ops = &camera_device_ops;

    ALOGI("%s: open camera %s", __func__, id);

    g_cam_device[cameraId]->priv = new CameraHardwareSoc(cameraId, g_cam_device[cameraId]);

done:
    *device = (hw_device_t *)g_cam_device[cameraId];
    ALOGI("%s: opened camera %s (%p)", __func__, id, *device);
    return 0;
}

static hw_module_methods_t camera_module_methods = {
            NAMED_FIELD_INITIALIZER(open) HAL_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
        NAMED_FIELD_INITIALIZER(common) {
            NAMED_FIELD_INITIALIZER(tag) HARDWARE_MODULE_TAG,
            NAMED_FIELD_INITIALIZER(version_major) 1,
            NAMED_FIELD_INITIALIZER(version_minor) 0,
            NAMED_FIELD_INITIALIZER(id) CAMERA_HARDWARE_MODULE_ID,
            NAMED_FIELD_INITIALIZER(name ) "IVI V1 camera HAL",
            NAMED_FIELD_INITIALIZER(author) "Intel",
            NAMED_FIELD_INITIALIZER(methods) &camera_module_methods,
        },
      NAMED_FIELD_INITIALIZER(get_number_of_cameras) HAL_getNumberOfCameras,
      NAMED_FIELD_INITIALIZER(get_camera_info) HAL_getCameraInfo
    };
}

}; // namespace android
