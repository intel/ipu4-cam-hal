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
#define LOG_TAG "SofSource"

#include <poll.h>

#include "iutils/CameraLog.h"
#include "iutils/Utils.h"
#include "V4l2DeviceFactory.h"
#include "PlatformData.h"

#include "SofSource.h"

namespace icamera {

SofSource::SofSource(int cameraId) :
    mPollThread(nullptr),
    mCameraId(cameraId),
    mIsysReceiverSubDev(nullptr),
    mExitPending(false)
{
    LOG1("%s: SofSource is constructed", __func__);

    mConfiguredDevices.clear();
    mSofDisabled = !PlatformData::isEnableAIQ(mCameraId);
    // FILE_SOURCE_S
    mSofDisabled = mSofDisabled || PlatformData::isFileSourceEnabled();
    // FILE_SOURCE_E
}

SofSource::~SofSource()
{
    LOG1("%s: SofSource is distructed.", __func__);
}

int SofSource::init()
{
    if (mSofDisabled) {
        return OK;
    }

    mPollThread = new PollThread(this);

    return OK;
}

int SofSource::deinit()
{
    if (mSofDisabled) {
        return OK;
    }

    int status = deinitDev();
    mPollThread->join();
    delete mPollThread;
    return status;
}

int SofSource::initDev()
{
    //Create and open receiver subdevice.
    string subDeviceNodeName;

    if (PlatformData::getDevNameByType(mCameraId, VIDEO_ISYS_RECEIVER, subDeviceNodeName) == OK) {
        LOG1("%s: found ISYS receiver subdevice %s", __func__, subDeviceNodeName.c_str());
    }

    V4l2SubDev *isysReceiverSubDev = V4l2DeviceFactory::getSubDev(mCameraId, subDeviceNodeName);
    // if the sub device does not change, do nothing.
    if (isysReceiverSubDev == mIsysReceiverSubDev) {
        return OK;
    }

    deinitDev();

    int id = 0;
    /* The value of virtual channel sequence is 1, 2, 3, ... if virtual channel supported.
       The value of SOF event id is 0, 1, 2, ... (sequence -1)  when virtual channel supported. */
    int sequence = PlatformData::getVirtualChannelSequence(mCameraId);
    if (sequence > 0) {
        id = sequence - 1;
    }

    mIsysReceiverSubDev = isysReceiverSubDev;
    mConfiguredDevices.push_back(mIsysReceiverSubDev);
    int status = mIsysReceiverSubDev->subscribeEvent(V4L2_EVENT_FRAME_SYNC, id);
    Check(status != OK, status, "%s: Failed to subscribe sync event %d", __func__, id);

    LOG1("%s: Using SOF event id %d for sync", __func__, id);
    return OK;
}

int SofSource::deinitDev()
{
    if (mIsysReceiverSubDev == nullptr) return OK;

    int id = 0;
    /* The value of virtual channel sequence is 1, 2, 3, ... if virtual channel supported.
       The value of SOF event id is 0, 1, 2, ... (sequence -1)  when virtual channel supported. */
    int sequence = PlatformData::getVirtualChannelSequence(mCameraId);
    if (sequence > 0) {
        id = sequence - 1;
    }

    int status = mIsysReceiverSubDev->unsubscribeEvent(V4L2_EVENT_FRAME_SYNC, id);

    if (status == OK) {
        LOG1("%s: Unsubscribe SOF event id %d done", __func__, id);
    } else {
        LOGE("%s: Failed to unsubscribe SOF event %d", __func__, id);
    }

    mConfiguredDevices.clear();
    return status;
}

int SofSource::configure()
{
    if (mSofDisabled) {
        return OK;
    }

    return initDev();
}

int SofSource::start()
{
    LOG1("%s", __func__);
    if (mSofDisabled) {
        return OK;
    }

    int status = mPollThread->run("SofSource", PRIORITY_URGENT_AUDIO);
    mExitPending = false;
    return status;

}

int SofSource::stop()
{
    LOG1("%s", __func__);
    if (mSofDisabled) {
        return OK;
    }

    mExitPending = true;
    int status = mPollThread->requestExitAndWait();
    return status;

}

int SofSource::poll()
{
    int ret = 0;
    const int pollTimeoutCount = 10;
    const int pollTimeout = 1000;
    vector<V4l2SubDev*> activeDevices;
    LOG2("@%s", __func__);

    int timeOutCount = pollTimeoutCount;

    while (timeOutCount-- && ret == 0) {

        ret = V4l2SubDev::pollDevices((vector<V4l2DevBase*>*) &mConfiguredDevices,
                                (vector<V4l2DevBase*>*) &activeDevices,
                                pollTimeout, -1, POLLPRI | POLLIN | POLLOUT | POLLERR);

        if (ret == 0 && mExitPending) {
            //timed out
            LOGD("@%s: Timedout or thread is not running, ret = %d", __func__, ret);
            return BAD_VALUE;
        }
    }

    //handle the poll error
    if (ret < 0) {
        if (mExitPending) {
            //Exiting, no error
            return 0;
        }

        LOGE("%s: Poll error", __func__);
        return ret;
    } else if (ret == 0) {
        LOGD("@%s, Sof poll timeout.", __func__);
        return 0;
    }

    struct v4l2_event event;
    CLEAR(event);
    mIsysReceiverSubDev->dequeueEvent(&event);

    EventDataSync syncData;
    syncData.sequence = event.u.frame_sync.frame_sequence;
    syncData.timestamp.tv_sec = event.timestamp.tv_sec;
    syncData.timestamp.tv_usec = (event.timestamp.tv_nsec / 1000);
    LOG2("%s:sof event sequence %ld, event.id %u", __func__, syncData.sequence, event.id);
    EventData eventData;
    eventData.type = EVENT_ISYS_SOF;
    eventData.buffer = nullptr;
    eventData.data.sync = syncData;
    notifyListeners(eventData);

    return 0;
}

}
