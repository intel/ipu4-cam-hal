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

#define LOG_TAG "Camera3Channel"
#include "Camera3HWI.h"

// System dependencies
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

// Camera dependencies
#include "Camera3Channel.h"


namespace android {
namespace camera2 {
#undef ALOGD
#define ALOGD(...) ((void*)0)
#define IS_BUFFER_ERROR(x) (((x) & V4L2_BUF_FLAG_ERROR) == V4L2_BUF_FLAG_ERROR)

/*===========================================================================
 * FUNCTION   : Camera3Channel
 *
 * DESCRIPTION: constrcutor of Camera3Channel
 *
 * PARAMETERS :
 *   @cam_handle : camera handle
 *   @cam_ops    : ptr to camera ops table
 *
 * RETURN     : none
 *==========================================================================*/
Camera3Channel::Camera3Channel(int device_id, icamera::stream_t *stream,
                                 channel_cb_routine cb_routine, void *userData)
    : mDeviceId(device_id),
      mStreamId(stream->id),
      mUserData(userData),
      mStream(stream),
      mChannelCB(cb_routine),
      mThreadRunning(false)
{
    mDQThread = new DQThread(this);
}

/*===========================================================================
 * FUNCTION   : ~Camera3Channel
 *
 * DESCRIPTION: destructor of Camera3Channel
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
Camera3Channel::~Camera3Channel()
{
    mStreamBufferPool.clear();
}

/*===========================================================================
 * FUNCTION   : start
 *
 * DESCRIPTION: start channel, which will start DQ stream belong to this channel
 *
 * PARAMETERS :
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t Camera3Channel::start()
{
    ALOGD("%s Enter", __func__);
    mThreadRunning = true;
    mDQThread->run("DQThread", PRIORITY_URGENT_DISPLAY);

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : stop
 *
 * DESCRIPTION: stop a channel, which will stop the stream belong to this channel
 *
 * PARAMETERS : none
 *
 * RETURN     : N/A
 *==========================================================================*/
void Camera3Channel::stop()
{
    mThreadRunning = false;
    mDQThread->requestExitAndWait();

    //clear all the pending streams
    while (!mPendingStreams.empty()) {
        mPendingStreams.pop();
    }
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION: flush a channel
 *
 * PARAMETERS : none
 *
 * RETURN     : N/A
 *==========================================================================*/
void Camera3Channel::flush()
{
    mThreadRunning = false;
    mDQThread->requestExit();
}

/*===========================================================================
 * FUNCTION   : queueBuf
 *
 * DESCRIPTION: queue a buffer to the stream connected to this channel
 *              Save the stream buffer to the mPendingStreams.
 *
 * PARAMETERS : stream_buf: The stream buffer from frameworks
 *            : stream_id : The stream ID in the request
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success always
 *              none-zero failure code
 *==========================================================================*/
int32_t Camera3Channel::queueBuf(
    const camera3_stream_buffer_t *stream_buf,
    int stream_id,
    int frame_id)
{
    ALOGD("%s Enter", __func__);

    sp<StreamBuffer> streamBuf  = NULL;
    for (auto & buffer : mStreamBufferPool) {
        if (buffer->getBufferHandle() == *stream_buf->buffer) {
            streamBuf = buffer;
            streamBuf->updateBufferInfo(stream_buf, frame_id);
            break;
        }
    }

    if (streamBuf == NULL) {
        streamBuf = new StreamBuffer(stream_buf, stream_id, frame_id);
        mStreamBufferPool.push_back(streamBuf);
    }

    streamBuf->waitOnAcquireFence();

    streamBuf->connect();

    icamera::camera_buffer_t *buf = streamBuf->getHalBuf();
    buf->s = *mStream;

    mPendingStreams.push(streamBuf);
    return icamera::camera_stream_qbuf(mDeviceId, mStreamId, buf);
}

/*===========================================================================
 * FUNCTION   : processNewStream
 *
 * DESCRIPTION: Get a buffer from the ISP and return to framework.
 *              This function is inside a thread loop
 *
 * PARAMETERS :
 *
 * RETURN     : true: Continue next frame loop
 *              false: stop the channel frame loop
 *==========================================================================*/
bool Camera3Channel::processNewStream()
{
    ALOGD("%s: Enter", __func__);

    const camera3_stream_buffer_t *result;
    int resultFrameNumber;

    //Get a buffer from the camera, wait from driver to return
    icamera::camera_buffer_t    *buf;
    icamera::Parameters param;
    uint64_t timestamp = systemTime();

    int ret = icamera::camera_stream_dqbuf(mDeviceId, mStreamId, &buf, &param);

    if(mThreadRunning == false) {
        //request exit
        return false;
    }

    if (ret < 0) {
        ALOGE("%s: failed with %d", __func__, ret);
        return false;
    }

    //int resultFrameNumber = buf->sequence;
    //check the buffer whether match what we saved in the queue, it should be first in, first out
    sp<StreamBuffer> streamBuf = mPendingStreams.front();
    mPendingStreams.pop();
    if (streamBuf == NULL) {
        return false;
    }

    streamBuf->check(buf);
    result = streamBuf->getUserBuf();
    resultFrameNumber = streamBuf->frameid();
    ALOGI("%s: hw sequence id %d, hal result id %d",
        __func__, buf->sequence, resultFrameNumber);

    streamBuf->unlock();

    if (mChannelCB) {
        mChannelCB(&param, result, (uint32_t)resultFrameNumber, timestamp, mUserData);
    }

    ALOGD("%s: Exit", __func__);

    return true;
}

} // namespace camera2
} // namespace android
