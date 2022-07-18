/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2017 Intel Corporation.
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

#ifndef __CAMERA3HARDWAREINTERFACE_H__
#define __CAMERA3HARDWAREINTERFACE_H__

// System dependencies

#include "iutils/Errors.h"
#include <utils/KeyedVector.h>
#include <utils/List.h>

#include <utils/threads.h>
#include <utils/RefBase.h>
#include "camera3.h"

#if !defined(USE_CROS_GRALLOC)
#include <ufo/gralloc.h>
#endif // USE_CROS_GRALLOC
#include <ICamera.h>
// Camera dependencies
#include "Gfx.h"
#include "Camera3HALHeader.h"
#include "Camera3Channel.h"
#include "CameraMetadata.h"


using namespace icamera;

namespace android {
namespace camera2 {

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define CAMERA3_BUFFER_STATUS_INTERNAL 0x1234
#define MAX_CAM_NUM 8

/* Time related macros */
typedef int64_t nsecs_t;
#define NSEC_PER_SEC 1000000000LLU
#define NSEC_PER_USEC 1000LLU
#define NSEC_PER_33MSEC 33000000LLU
#define NSEC_PER_100MSEC 100000000LLU
#define MAX_NUM_STREAMS     3
#define MAX_BUFFERS 10
#define MAX_IVPFMT 4

typedef enum {
    SET_ENABLE,
    SET_CONTROLENABLE,
    SET_RELOAD_CHROMATIX,
    SET_STATUS,
} optype_t;

#define MODULE_ALL 0

typedef struct {
    int width;
    int height;
} Size;

typedef struct {
    stream_status_t status;
    Camera3Channel *channel;
    stream_type_t channelid;
    bool ivpconvert;
    bool jpgencoder;
    bool hwencoder;

    camera3_stream_t *reqstream;
    camera3_stream_t *main_hal_stream; //pass into libcamhal/ipu

    camera3_stream_buffer_t *jpgbuf;
    camera3_stream_buffer_t *ivpbuf;
    BufferPackage *main_hal_buf;

    int main_hal_bufnum;

    #if 0 //TODO, add into stream_info_t Class
    Vector<sp<CameraGfxBuffer>> preview_ptrs; // used for deleting
    Vector<sp<CameraGfxBuffer>> videord_ptrs; // used for deleting
    Vector<sp<CameraGfxBuffer>> picture_ptrs; // used for deleting
    #endif
} stream_info_t;

/* Data structure to store pending request */
typedef struct {
    camera3_stream_t *stream;
    camera3_stream_buffer_t *buffer;
    // metadata needs to be consumed by the corresponding stream
    // in order to generate the buffer.
    bool need_metadata;
    buffer_handle_t *handle;
} RequestedBufferInfo;

typedef struct {
    uint32_t frame_number;
    uint32_t num_buffers;
    int32_t request_id;
    camera_metadata_t *settings;
    List<RequestedBufferInfo> buffers;
    nsecs_t timestamp;
    uint8_t pipeline_depth;
    uint32_t partial_result_cnt;
    bool shutter_notified;
} PendingRequestInfo;

typedef struct {
    uint32_t frame_number;
    uint32_t stream_ID;
} PendingFrameDropInfo;

typedef struct {
    camera3_notify_msg_t notify_msg;
    camera3_stream_buffer_t buffer;
    uint32_t frame_number;
} PendingReprocessResult;

class Camera3HardwareInterface {
public:
    /* static variable and functions accessed by camera service */
    static camera3_device_ops_t mCameraOps;
    static int initialize(const struct camera3_device *,
                const camera3_callback_ops_t *callback_ops);
    static int configure_streams(const struct camera3_device *,
                camera3_stream_configuration_t *streamList);
    static const camera_metadata_t* construct_default_request_settings(
                                const struct camera3_device *, int type);
    static int process_capture_request(const struct camera3_device *,
                                camera3_capture_request_t *request);
    static void dump(const struct camera3_device *, int fd);
    static int flush(const struct camera3_device *);
    static int close_camera_device(struct hw_device_t* device);
    static int getCamInfo(uint32_t cameraId, struct camera_info *info);
    static int initCapabilities(uint32_t cameraId);
    static int initStaticMetadata(uint32_t cameraId);
    static void captureResultCb(icamera::Parameters *metadata,
                const camera3_stream_buffer_t *buffer, uint32_t frame_number, uint64_t timestamp, void *userdata);

public:
    Camera3HardwareInterface(uint32_t cameraId,
            const camera_module_callbacks_t *callbacks);
    virtual ~Camera3HardwareInterface();
    int openCamera(struct hw_device_t **hw_device);
    camera_metadata_t* translateCapabilityToMetadata(int type);
    int initialize(const camera3_callback_ops_t *callback_ops);
    int configureStreams(camera3_stream_configuration_t *streamList);
    int processCaptureRequest(camera3_capture_request_t *request);
    void dump(int fd);
    void captureResultCb(icamera::Parameters *metadata,
                const camera3_stream_buffer_t *buffer, uint32_t frame_number, uint64_t timestamp);

private:

    // State transition conditions:
    // "\" means not applicable
    // "x" means not valid
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |            |  CLOSED  |  OPENED  | INITIALIZED | CONFIGURED | STARTED | ERROR | DEINIT |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |  CLOSED    |    \     |   open   |     x       |    x       |    x    |   x   |   x    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |  OPENED    |  close   |    \     | initialize  |    x       |    x    | error |   x    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |INITIALIZED |  close   |    x     |     \       | configure  |   x     | error |   x    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // | CONFIGURED |  close   |    x     |     x       | configure  | request | error |   x    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |  STARTED   |  close   |    x     |     x       | configure  |    \    | error |   x    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |   ERROR    |  close   |    x     |     x       |     x      |    x    |   \   |  any   |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+
    // |   DEINIT   |  close   |    x     |     x       |     x      |    x    |   x   |   \    |
    // +------------+----------+----------+-------------+------------+---------+-------+--------+

    typedef enum {
        CLOSED,
        OPENED,
        INITIALIZED,
        CONFIGURED,
        STARTED,
        ERROR,
        DEINIT
    } State;
    typedef List<PendingRequestInfo>::iterator pendingRequestIterator;
    typedef List<RequestedBufferInfo>::iterator pendingBufferIterator;

    int openCamera();
    int closeCamera();
    int flush();
    void cleanStreamInfo();
    int startAllChannels();
    int stopAllChannels();
    static void addStreamConfig(Vector<int32_t> &available_stream_configs,
            int32_t scalar_format, const cam_dimension_t &dim,
            int32_t config_type);

    int validateCaptureRequest(camera3_capture_request_t *request);
    void processCaptureResult(const camera3_stream_buffer_t *buf,
            uint32_t frame_number, uint64_t timestamp);
    void unblockRequestIfNecessary();
    static void getLogLevel();

    int32_t notifyErrorForPendingRequests();
    int32_t handleCameraDeviceError();
    const char* getCurrentSensorName();
    void setDeviceId(int camera_Id);
    camera_metadata_t* constructMetadata(int64_t capture_time, camera_metadata_t* camMeta);

    int HalFormat2V4L2Format(int HalFormat);
    int V4L2Format2HalFormat(int V4L2Format);
    int getExtraHeight(int w, int h, int gfxFmt, int v4l2Fmt);

    camera3_stream_buffer_t* allocateMainBuf(icamera::stream_t *hwstream, stream_info_t *swstream);
    camera3_stream_buffer_t* allocateJPEGBuf(stream_info_t *swstream);
    camera3_stream_buffer_t* allocateHALBuf(int width, int height);

    void deallocateMainBuf(stream_info_t *streaminfo);
    void deallocateJPEGBuf(stream_info_t *streaminfo);
    void deallocateHALBuf(camera3_stream_buffer_t* buf);
    camera3_stream_buffer_t* getMainHalBuf();
#if defined(USE_CROS_GRALLOC)
    void* camera3bufLock(camera3_stream_buffer_t* buf, uint32_t* pWidth, uint32_t* pHeight,
                         uint32_t* pStride);
#else
    void* camera3bufLock(camera3_stream_buffer_t* buf, intel_ufo_buffer_details_t* buffer_info = NULL);
#endif // USE_CROS_GRALLOC
    void camera3bufUnlock(camera3_stream_buffer_t* buf);

    status_t setJpegParameters(CameraMetadata& meta);
    int jpegSwEncode(camera3_stream_buffer_t* src, camera3_stream_buffer_t* dst);
    int exifMake(camera3_stream_buffer_t* src, camera3_stream_buffer_t* dst, int jpgsize);
    Size getMaxJpegResolution();
    ssize_t getJpegBufSize(uint32_t width, uint32_t height);
    camera3_stream_t* findMainStreams(camera3_stream_configuration_t *streamList);
    stream_info_t* getStreamInfo(camera3_stream_t *stream);
    bool isSameStream(camera3_stream_t *src, camera3_stream_t *dst);
    int constructChannels(camera3_stream_configuration_t *streamList);
    int constructStreamInfo(camera3_stream_configuration_t *streamList);
    int constructMainStream();
    void releaseMainStream();
    int constructHWStreams(camera3_stream_configuration_t *streamList);
    int checkStreams(camera3_stream_configuration_t *streamList);
    void getInputConfig();
    int getBestStream(int width, int height, int fmt, icamera::stream_t *match);
    int getBestFormat(int format);
    void setCamHalDebugEnv();
    pendingRequestIterator erasePendingRequest(pendingRequestIterator i);
    void copyYuvData(camera3_stream_buffer_t* src, camera3_stream_buffer_t* dst);

    gralloc_module_t*  m_pGralloc;
    camera3_device_t   mCameraDevice;
    uint32_t           mCameraId;
    uint32_t           mDeviceId;
    bool               mCameraInitialized;
    camera_metadata_t *mDefaultMetadata[CAMERA3_TEMPLATE_COUNT];
    const camera3_callback_ops_t *mCallbackOps;
    GenImageConvert *mGenConvert;
    Vector<sp<CameraGfxBuffer>> mpreview_ptrs; // used for deleting
    Vector<sp<CameraGfxBuffer>> mpicture_ptrs; // used for deleting
    Vector<sp<CameraGfxBuffer>> mhalbuf_ptrs; // used for deleting

    icamera::Parameters mJpegParameter;

    //First request yet to be processed after configureStreams
    bool mFirstConfiguration;
    bool mFlush;
    icamera::Parameters mParameters;
    icamera::Parameters mPrevParameters;

    // minimal jpeg buffer size: 256KB + blob header
    ssize_t minJpgBufSize = (256*1024) + sizeof(camera3_jpeg_blob);

    List<PendingRequestInfo> mPendingRequestsList;

    uint32_t mPendingLiveRequest;
    int32_t mCurrentRequestId;

    //mutex for serialized access to camera3_device_ops_t functions
    mutable Mutex       mLock;
    mutable Condition   mRequestCond;
    static const nsecs_t kWaitDuration = 5000000000; // 5000ms

    //Save all the configured streams
    List<stream_info_t*> mStreamInfo;
    uint32_t mStreamNum;

    const camera_module_callbacks_t *mCallbacks;

    State mState;
    icamera::stream_config_t mStreamList;
    icamera::stream_t mStreams[MAX_NUM_STREAMS];
    icamera::stream_t mInputConfig;
    stream_info_t* mMainStreamInfo;
    int mIVPSupportedFmts[MAX_IVPFMT];
};

} // namespace camera2
} // namespace android

#endif /* __CAMERA3HARDWAREINTERFACE_H__ */
