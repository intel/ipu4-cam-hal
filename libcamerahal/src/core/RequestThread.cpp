/*
 * Copyright (C) 2015-2018 Intel Corporation.
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

#define LOG_TAG "RequestThread"

#include "iutils/Errors.h"
#include "iutils/CameraLog.h"

#include "RequestThread.h"

namespace icamera {

RequestThread::RequestThread(int cameraId) :
    mCameraId(cameraId),
    mRequestsInProcessing(0),
    mFirstRequest(true),
    mRequestConfigMode(CAMERA_STREAM_CONFIGURATION_MODE_END),
    mUserConfigMode(CAMERA_STREAM_CONFIGURATION_MODE_END),
    mNeedReconfigPipe(false),
    mReconfigPipeScore(0),
    mActive(true)
{
    CLEAR(mStreamConfig);
    CLEAR(mConfiguredStreams);

    mStreamConfig.operation_mode = CAMERA_STREAM_CONFIGURATION_MODE_END;

    const char *CAMERA_HAL_WAIT_FRAME_DURATION = "cameraFrameDuration";
    char *frameDurationStr = getenv(CAMERA_HAL_WAIT_FRAME_DURATION);
    mWaitFrameDurationOverride = 0;
    if (frameDurationStr) {
        mWaitFrameDurationOverride = atoi(frameDurationStr); // ms
    }
    if (mWaitFrameDurationOverride > 0) {
        LOGW("Frame druation is overriden to %d ms", mWaitFrameDurationOverride);
    } else {
        mWaitFrameDurationOverride = 0; // override is disabled
    }
}

RequestThread::~RequestThread()
{
    while (!mReqParamsPool.empty()) {
        mReqParamsPool.pop();
    }
}

void RequestThread::requestExit()
{
    clearRequests();

    Thread::requestExit();
    AutoMutex l(mPendingReqLock);
    mRequestSignal.signal();
}

void RequestThread::clearRequests()
{
    LOG1("%s", __func__);

    mActive = false;
    for (int streamId = 0; streamId < MAX_STREAM_NUMBER; streamId++) {
        FrameQueue& frameQueue = mOutputFrames[streamId];
        AutoMutex lock(frameQueue.mFrameMutex);
        while (!frameQueue.mFrameQueue.empty()) {
            frameQueue.mFrameQueue.pop();
        }
        frameQueue.mFrameAvailableSignal.broadcast();
    }

    AutoMutex l(mPendingReqLock);
    mRequestsInProcessing = 0;
    while (!mPendingRequests.empty()) {
        mPendingRequests.pop();
    }
    mFirstRequest = true;
}

void RequestThread::setConfigureModeByParam(const Parameters& param)
{
    camera_scene_mode_t sceneMode = SCENE_MODE_MAX;
    if (param.getSceneMode(sceneMode) != OK) {
        return;
    }

    ConfigMode configMode = CameraUtils::getConfigModeBySceneMode(sceneMode);
    LOG2("@%s, sceneMode %d, configMode %d", __func__, sceneMode, configMode);

    if (configMode == CAMERA_STREAM_CONFIGURATION_MODE_END) {
        LOG2("%s: no valid config mode, skip setting", __func__);
        return;
    }

    /* Reset internal mode related settings if requested mode is same as
     * the mode currently running for better stability.
     */
    if (mStreamConfig.operation_mode == configMode) {
        LOG2("%s: config mode %d keep unchanged.", __func__, configMode);
        mNeedReconfigPipe = false;
        mReconfigPipeScore = 0;
        mRequestConfigMode = configMode;
        return;
    }

    if (mRequestConfigMode != configMode) {
        if (mRequestConfigMode != CAMERA_STREAM_CONFIGURATION_MODE_END) {
            mNeedReconfigPipe = true;
            mReconfigPipeScore = 0;
            LOG2("%s: request configure mode changed, reset score %d", __func__, mReconfigPipeScore);
        }
        LOG2("%s: mRequestConfigMode updated from %d to %d", __func__, mRequestConfigMode, configMode);
        mRequestConfigMode = configMode;
    } else if (mReconfigPipeScore < PlatformData::getPipeSwitchDelayFrame(mCameraId)) {
        mReconfigPipeScore ++;
        LOG2("%s: request configure mode unchanged, current score %d", __func__, mReconfigPipeScore);
    }
}

int RequestThread::configure(stream_config_t *streamList)
{
    mStreamConfig.num_streams = streamList->num_streams;
    mStreamConfig.operation_mode = streamList->operation_mode;
    mUserConfigMode = (ConfigMode)streamList->operation_mode;
    LOG2("%s: user specified Configmode: %d", __func__, mUserConfigMode);
    for (int i = 0; i < streamList->num_streams; i++) {
        mConfiguredStreams[i] = streamList->streams[i];
    }

    mStreamConfig.streams = mConfiguredStreams;

    //Use concrete mode in RequestThread, HDR as initial default
    if ((ConfigMode)mStreamConfig.operation_mode == CAMERA_STREAM_CONFIGURATION_MODE_AUTO) {
        vector <ConfigMode> configModes;
        int ret = PlatformData::getConfigModesByOperationMode(mCameraId, mStreamConfig.operation_mode, configModes);
        Check((ret != OK || configModes.empty()), ret, "%s, get real ConfigMode failed %d", __func__, ret);
        mRequestConfigMode = configModes[0];
        LOG2("%s: use concrete mode %d as default initial mode for auto op mode", __func__, mRequestConfigMode);
        mStreamConfig.operation_mode = mRequestConfigMode;
    }

    LOG2("%s: mRequestConfigMode initial value: %d", __func__, mRequestConfigMode);

    return OK;
}

int RequestThread::processParameters(const Parameters& param)
{
    setConfigureModeByParam(param);

    return OK;
}

int RequestThread::processRequest(int bufferNum, camera_buffer_t **ubuffer, const Parameters * params)
{
    mActive = true;
    AutoMutex l(mPendingReqLock);
    CameraRequest request;
    request.mBufferNum = bufferNum;
    for (int id = 0; id < bufferNum; id++) {
        request.mBuffer[id] = ubuffer[id];
    }
    request.mParams = copyRequestParams(params);

    mPendingRequests.push(request);
    mRequestSignal.signal();

    return OK;
}

shared_ptr<Parameters>
RequestThread::copyRequestParams(const Parameters *srcParams)
{
    if (srcParams == nullptr)
        return nullptr;

    if (mReqParamsPool.empty()) {
        shared_ptr<Parameters> sParams = make_shared<Parameters>();
        Check(!sParams, nullptr, "%s: no memory!", __func__);
        mReqParamsPool.push(sParams);
    }

    shared_ptr<Parameters> sParams = mReqParamsPool.front();
    mReqParamsPool.pop();
    *sParams = *srcParams;
    return sParams;
}

int RequestThread::waitFrame(int streamId, camera_buffer_t **ubuffer)
{
    FrameQueue& frameQueue = mOutputFrames[streamId];
    ConditionLock lock(frameQueue.mFrameMutex);

    while (frameQueue.mFrameQueue.empty()) {
        int ret = frameQueue.mFrameAvailableSignal.waitRelative(lock,
            mWaitFrameDurationOverride ? mWaitFrameDurationOverride * 1000000 : kWaitFrameDuration);
        if (!mActive) return INVALID_OPERATION;

        if (ret == TIMED_OUT) {
            LOGW("@%s, mCameraId:%d, time out happens, wait recovery", __func__, mCameraId);
            return ret;
        }
    }

    shared_ptr<CameraBuffer> camBuffer = frameQueue.mFrameQueue.front();
    frameQueue.mFrameQueue.pop();
    *ubuffer = camBuffer->getUserBuffer();

    LOG2("@%s, frame returned. camera id:%d, stream id:%d", __func__, mCameraId, streamId);

    return OK;
}

int RequestThread::wait1stRequestDone()
{
    LOG1("%s", __func__);
    int ret = OK;
    ConditionLock lock(mFirstRequestLock);
    if (mFirstRequest) {
        LOG1("%s, waiting the first request done", __func__);
        ret = mFirstRequestSignal.waitRelative(lock, kWaitFirstRequestDoneDuration);
        if (ret == TIMED_OUT)
            LOGE("@%s: Wait 1st request timed out", __func__);
    }

    return ret;
}

void RequestThread::handleEvent(EventData eventData)
{
    /* Notes: There should be only one of EVENT_PSYS_FRAME and EVENT_ISYS_FRAME registered */
    if (!PlatformData::isStatsTriggered(mCameraId)
        && (eventData.type == EVENT_PSYS_FRAME || eventData.type == EVENT_ISYS_FRAME)) {
        AutoMutex l(mPendingReqLock);
        if (mRequestsInProcessing > 0)
            mRequestsInProcessing--;
        mRequestSignal.signal();
        LOG2("%s, event type %d, mRequestsInProcessing %d, sequence %ld", __func__, eventData.type,
              mRequestsInProcessing, eventData.data.frame.sequence);
    } else if (PlatformData::isStatsTriggered(mCameraId)
               && eventData.type == EVENT_PSYS_STATS_BUF_READY) {
        AutoMutex l(mPendingReqLock);
        if (mRequestsInProcessing > 0)
            mRequestsInProcessing--;
        mRequestSignal.signal();
        LOG2("%s, event type %d, mRequestsInProcessing %d, sequence %ld", __func__, eventData.type,
              mRequestsInProcessing, eventData.data.frame.sequence);
    } else if (eventData.type == EVENT_FRAME_AVAILABLE) {
        int streamId = eventData.data.frameDone.streamId;
        FrameQueue& frameQueue = mOutputFrames[streamId];

        AutoMutex lock(frameQueue.mFrameMutex);
        bool needSignal = frameQueue.mFrameQueue.empty();
        frameQueue.mFrameQueue.push(eventData.buffer);
        if (needSignal) {
            frameQueue.mFrameAvailableSignal.signal();
        }
    }
}

/*
 * Fetch the next request waiting for processsing.
 * Return true if fetching succeeds.
 * Return false if no request need to be processed
 */
bool RequestThread::fetchNextRequest(CameraRequest &request)
{
    AutoMutex l(mPendingReqLock);
    if (isReadyForRequestProcess()) {
        request = mPendingRequests.front();
        mRequestsInProcessing++;
        mPendingRequests.pop();
        LOG2("@%s, mRequestsInProcessing %d", __func__, mRequestsInProcessing);
        return true;
    }

    // Thread is woken up to exit
    return false;
}

/**
 * Wait for processing request.
 * There are 2 cases which need to wait and doesn't process request:
 * 1, not ready for processing request (mPendingRequests is empty and enough buffers in flight);
 * 2, not ready for reconfiguring streams (mPendingRequests is empty and there is buffer in flight).
 */
void RequestThread::waitForProcessRequest()
{
    ConditionLock lock(mPendingReqLock);
    if (!isReadyForRequestProcess() || (isReconfigurationNeeded() && !isReadyForReconfigure())) {
        int ret = mRequestSignal.waitRelative(lock, kWaitDuration);
        if (ret == TIMED_OUT) {
            LOGW("%s: wait request time out", __func__);
        }
    }
}

/**
 * Check if ready for processing request.
 * Return ture if mPendingRequests is empty and not enough buffers in flight.
 */
bool RequestThread::isReadyForRequestProcess() const
{
    int maxRequestsInflight = PlatformData::getMaxRequestsInflight(mCameraId);
    return (!mPendingRequests.empty() && mRequestsInProcessing < maxRequestsInflight);
}

/**
 * Check if ConfigMode is changed or not.
 * If new ConfigMode is different with previous configured ConfigMode,
 * return true.
 */
bool RequestThread::isReconfigurationNeeded()
{
    bool needReconfig = (mUserConfigMode == CAMERA_STREAM_CONFIGURATION_MODE_AUTO &&
                         PlatformData::getAutoSwitchType(mCameraId) == AUTO_SWITCH_FULL &&
                         mNeedReconfigPipe &&
                         (mReconfigPipeScore >= PlatformData::getPipeSwitchDelayFrame(mCameraId)));
    LOG2("%s: need reconfigure %d, score %d, decision %d",
         __func__, mNeedReconfigPipe, mReconfigPipeScore, needReconfig);
    return needReconfig;
}

/**
 * If reconfiguration is needed, there are 2 extra conditions for reconfiguration:
 * 1, there is no buffer in processing; 2, there is buffer in mPendingRequests.
 * Return true if reconfiguration is ready.
 */
bool RequestThread::isReadyForReconfigure()
{
    return (!mPendingRequests.empty() && mRequestsInProcessing == 0);
}


bool RequestThread::threadLoop()
{
    CameraRequest request;

    waitForProcessRequest();

    // check if need to reconfigure
    bool restart = isReconfigurationNeeded();
    bool processRequest = false;

    // fetch next request if no need to reconfigure or ready for reconfiguration
    if (!restart || isReadyForReconfigure()) {
        processRequest = fetchNextRequest(request);
    }

    if (processRequest) {
        // process request
        if (request.mParams.get() != nullptr) {
            processParameters(*(request.mParams.get()));
        }

        // re-check if need to reconfigure
        if (restart && isReconfigurationNeeded()) {
            LOG1("%s, ConfigMode change from %x to %x", __func__, mStreamConfig.operation_mode,
                  mRequestConfigMode);
            mStreamConfig.operation_mode = mRequestConfigMode;

            EventConfigData configData;
            configData.streamList = &mStreamConfig;
            EventData eventData;
            eventData.type = EVENT_DEVICE_RECONFIGURE;
            eventData.data.config = configData;
            notifyListeners(eventData);

            mNeedReconfigPipe = false;
            mReconfigPipeScore = 0;
        }

        EventRequestData requestData;
        requestData.bufferNum = request.mBufferNum;
        requestData.buffer = request.mBuffer;
        requestData.param = request.mParams.get();
        EventData eventData;
        eventData.type = EVENT_PROCESS_REQUEST;
        eventData.data.request = requestData;
        notifyListeners(eventData);

        // Recycle params ptr for re-using
        if (request.mParams) {
            AutoMutex l(mPendingReqLock);
            mReqParamsPool.push(request.mParams);
        }

        {
            AutoMutex l(mFirstRequestLock);
            if (mFirstRequest) {
                LOG1("%s: first request done", __func__);
                mFirstRequest = false;
                mFirstRequestSignal.signal();
            }
        }
    }

    return true;
}

} //namespace icamera
