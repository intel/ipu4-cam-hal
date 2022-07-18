/*
 * Copyright (C) 2016-2018 Intel Corporation.
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

#include "iutils/Thread.h"
#include "PlatformData.h"
#include "Parameters.h"
#include "CameraDevice.h"

namespace icamera {

/*
 * The RequestThread is used to assist CameraDevice to handle request(qbuf/dqbuf).
 */
class RequestThread : public Thread, public EventSource, public EventListener {
public:
    RequestThread(int cameraId);
    ~RequestThread();

    bool threadLoop();
    void requestExit();

    void handleEvent(EventData eventData);

    /**
     * \Clear pending requests.
     */
    void clearRequests();

    /**
     * \Accept requests from user.
     */
    int processRequest(int bufferNum, camera_buffer_t **ubuffer, const Parameters * params);

    /**
     * \Accept user parameters.
     */
    int processParameters(const Parameters& param);

    int waitFrame(int streamId, camera_buffer_t **ubuffer);

    /**
     * \Block the caller until the first request is processed.
     */
    int wait1stRequestDone();

    /**
     * \brief configure the streams, devices and post processor.
     *
     * \param streamList: all the streams info
     *
     * \return OK if succeed and BAD_VALUE if failed
     */
    int configure(stream_config_t *streamList);

    /**
     * \brief get stream config in request thread.
     */
    stream_config_t getStreamConfig() { return mStreamConfig; };

    /**
     * \brief set request configure mode by parameters.
     */
    void setConfigureModeByParam(const Parameters& param);

private:
    int mCameraId;

    struct CameraRequest {
        CameraRequest() : mBufferNum(0), mParams(nullptr) {
            CLEAR(mBuffer);
        }

        int mBufferNum;
        camera_buffer_t *mBuffer[MAX_STREAM_NUMBER];
        shared_ptr<Parameters> mParams;
    };

    shared_ptr<Parameters> copyRequestParams(const Parameters *params);

    /**
     * \Fetch one request from pending request Q for processing.
     */
    bool fetchNextRequest(CameraRequest &request);
    bool isReadyForRequestProcess() const;
    void waitForProcessRequest();
    bool isReadyForReconfigure();
    bool isReconfigurationNeeded();

    static const int kMaxRequests = MAX_BUFFER_COUNT;
    static const nsecs_t kWaitFrameDuration = 10000000000; // 10s
    static const nsecs_t kWaitDuration = 2000000000; // 2s
    static const nsecs_t kWaitFirstRequestDoneDuration = 1000000000; // 1s

    //Guard for all the pending requests
    Mutex mPendingReqLock;
    Condition mRequestSignal;
    queue <CameraRequest> mPendingRequests;
    queue <shared_ptr<Parameters> > mReqParamsPool;
    int mRequestsInProcessing;

    // Guard for the first request.
    Mutex mFirstRequestLock;
    Condition mFirstRequestSignal;
    bool mFirstRequest;

    // Internal used for restart function
    ConfigMode mRequestConfigMode; // the ConfigMode is gotten from parameters set from user or AE result
    ConfigMode mUserConfigMode; // user specified ConfigMode during initial configure
    // Whether pipe need to reconfigure
    bool mNeedReconfigPipe;
    // Score indicate the num of consecutive configure mode settings, to make switch stable.
    unsigned int  mReconfigPipeScore;
    stream_config_t mStreamConfig;
    stream_t mConfiguredStreams[MAX_STREAM_NUMBER];

    struct FrameQueue {
        Mutex mFrameMutex;
        Condition mFrameAvailableSignal;
        CameraBufQ mFrameQueue;
    };
    FrameQueue mOutputFrames[MAX_STREAM_NUMBER];
    bool mActive;

private:
    int mWaitFrameDurationOverride;
};

} //namespace icamera
