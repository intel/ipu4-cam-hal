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

#pragma once

#include <vector>
#include <mutex>

#include <hardware/camera3.h>

#include "HALv3Header.h"
#include "HALv3Interface.h"
#include "ResultProcessor.h"
#include "Camera3Stream.h"

namespace camera3 {

struct Camera3Request {
    uint32_t frameNumber;
    android::CameraMetadata settings;
};

/**
 * \class RequestManager
 *
 * This class is used to handle requests. It has the following
 * roles:
 * - It instantiates ResultProcessor.
 */
class RequestManager : public RequestManagerCallback {

public:
    RequestManager(int cameraId);
    virtual ~RequestManager();

    int init(const camera3_callback_ops_t *callback_ops);

    int deinit();

    int configureStreams(camera3_stream_configuration_t *stream_list);

    int constructDefaultRequestSettings(int type, const camera_metadata_t **meta);

    int processCaptureRequest(camera3_capture_request_t *request);

    void dump(int fd);

    int flush();

    void returnRequestDone(uint32_t frameNumber);
private:
    int HALFormatToV4l2Format(int halFormat);
    int fillHWStreams(const camera3_stream_t &camera3Stream, icamera::stream_t &stream);
    int fillShadowStream(const icamera::stream_t &srcStream, icamera::stream_t &shdStream);
    void deteteStreams(bool inactiveOnly);
    void increaseRequestCount();
    int waitProcessRequest();
    int getAvailableCameraBufferInfoIndex();

private:
    static const int kMaxStreamNum = 4; // PREVIEW, VIDEO, STILL and POSTVIEW
    static const int kMaxProcessRequestNum = 10;
    const uint64_t kMaxDuration = 2000000000; // 2000ms

    struct CameraBufferInfo {
        icamera::camera_buffer_t halBuffer[kMaxStreamNum]; // camera_buffer_t info queued to hal
        uint32_t frameNumber;                              // frame_number in request
        bool frameInProcessing;                            // if frame of frame_number is used
    };

    int mCameraId;
    const camera3_callback_ops_t *mCallbackOps;

    bool mCameraDeviceStarted;

    ResultProcessor *mResultProcessor;

    // mLock is used to protect mRequestManagerState
    std::mutex mLock;
    enum RequestManagerState {
        IDLE = 0,
        INIT,
        CONFIGURE_STREAMS,
        PROCESS_CAPTURE_REQUEST,
        FLUSH
    } mRequestManagerState;

    std::map<int, android::CameraMetadata> mDefaultRequestSettings;

    icamera::stream_t mHALStream[kMaxStreamNum];

    std::vector<Camera3Stream*> mCamera3StreamVector;

    std::condition_variable mRequestCondition;
    // mRequestLock is used to protect mRequestInProgress and mCameraBufferInfo
    std::mutex mRequestLock;
    uint32_t mRequestInProgress;
    struct CameraBufferInfo mCameraBufferInfo[kMaxProcessRequestNum];

    android::CameraMetadata mLastSettings;
};

} // namespace camera3
