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

#define LOG_TAG "V4l2DevBase"

#include "iutils/CameraLog.h"

#include "iutils/Errors.h"
#include "iutils/Utils.h"
#include "V4l2DevBase.h"

namespace icamera {

struct VideoNodeInfo {
    VideoNodeType type;
    const char* fullName;
    const char* shortName;
};

static const VideoNodeInfo gVideoNodeInfos[] = {
    { VIDEO_GENERIC,             "VIDEO_GENERIC",              "Generic" },
    { VIDEO_GENERIC_MEDIUM_EXPO, "VIDEO_GENERIC_MEDIUM_EXPO",  "GenericMediumExpo" },
    { VIDEO_GENERIC_SHORT_EXPO,  "VIDEO_GENERIC_SHORT_EXPO",   "GenericShortExpo" },
    { VIDEO_AA_STATS,            "VIDEO_AA_STATS",             "IsaStats" },
    { VIDEO_ISA_CONFIG,          "VIDEO_ISA_CONFIG",           "IsaConfig" },
    { VIDEO_ISA_SCALE,           "VIDEO_ISA_SCALE",            "IsaScale" },
    { VIDEO_CSI_META,            "VIDEO_CSI_META",             "CsiMeta" },

    { VIDEO_PIXEL_ARRAY,         "VIDEO_PIXEL_ARRAY",          "PixelArray" },
    { VIDEO_PIXEL_BINNER,        "VIDEO_PIXEL_BINNER",         "PixelBinner" },
    { VIDEO_PIXEL_SCALER,        "VIDEO_PIXEL_SCALER",         "PixelScaler" },

    { VIDEO_ISA_DEVICE,          "VIDEO_ISA_DEVICE",           "IsaSubDevice" },
    { VIDEO_ISYS_RECEIVER,       "VIDEO_ISYS_RECEIVER",        "ISysReceiver" },
    { VIDEO_ISYS_RECEIVER_BACKEND,  "VIDEO_ISYS_RECEIVER_BACKEND",  "CsiBE"},
};

VideoNodeType V4l2DevBase::getNodeType(const char* nodeName)
{
    Check(nodeName == nullptr, VIDEO_GENERIC, "Invalid null video node name.");

    int size = ARRAY_SIZE(gVideoNodeInfos);
    for (int i = 0; i < size; i++) {
        if (strcmp(gVideoNodeInfos[i].fullName, nodeName) == 0) {
            return gVideoNodeInfos[i].type;
        }
    }

    LOGE("Invalid video node name: %s", nodeName);
    return VIDEO_GENERIC;
}

const char* V4l2DevBase::getNodeName(VideoNodeType nodeType)
{
    int size = ARRAY_SIZE(gVideoNodeInfos);
    for (int i = 0; i < size; i++) {
        if (gVideoNodeInfos[i].type == nodeType) {
            return gVideoNodeInfos[i].shortName;
        }
    }

    LOGE("Invalid video node type: %d", nodeType);
    return "InvalidNode";
}

V4l2DevBase::V4l2DevBase() : mDevFd(-1)
{
    mSC = SysCall::getInstance();
}

V4l2DevBase::V4l2DevBase(const string& devName) : mDevFd(-1),
        mDevName(devName)
{
    mSC = SysCall::getInstance();
}

V4l2DevBase::~V4l2DevBase()
{
    LOG1("@%s %s", __func__, mDevName.c_str());
    mDevFd = -1;
    mSC = nullptr;
}

int V4l2DevBase::openDev()
{
    if (mDevFd != -1)
        return 0;

    mDevFd = mSC->open(mDevName.c_str(), O_RDWR);
    if (mDevFd == -1) {
        LOGE("%s: Failed to open device node %s %s", __func__,mDevName.c_str(), strerror(errno));
        return -1;
    }

    return 0;
}

int V4l2DevBase::pollDevices(const vector<V4l2DevBase*> *devices,
                             vector<V4l2DevBase*> *activeDevices,
                             int timeOut, int flushFd, int events)
{

    int numFds = devices->size();
    int totalNumFds = (flushFd != -1) ? numFds + 1 : numFds; //adding one more fd if flushfd given.
    struct pollfd pollFds[totalNumFds];
    int ret = 0;

    for (int i = 0; i < numFds; i++) {
        LOG2("%s: poll device: %s, fd: %d", __func__, devices->at(i)->getDevName(), devices->at(i)->getDevFd());
        pollFds[i].fd = devices->at(i)->getDevFd();
        pollFds[i].events = events | POLLERR; // we always poll for errors, asked or not
        pollFds[i].revents = 0;
    }

    if (flushFd != -1) {
        pollFds[numFds].fd = flushFd;
        pollFds[numFds].events = POLLPRI | POLLIN;
        pollFds[numFds].revents = 0;
    }

    SysCall* SC = SysCall::getInstance();

    if (SC == nullptr) {
        LOGE("%s: Failed to find SysCall instance.", __func__);
        return -1;
    }

    ret = SC->poll(pollFds, totalNumFds, timeOut);

    if (ret <= 0) {
        for (uint32_t i = 0; i < devices->size(); i++) {
            LOG1("Device %s poll failed (%s)", devices->at(i)->getDevName(),
                                              (ret == 0) ? "timeout" : "error");
            if (pollFds[i].revents & POLLERR) {
                LOG1("%s: device %s received POLLERR", __func__, devices->at(i)->getDevName());
                return UNKNOWN_ERROR;
            }
        }
        return ret;
    }

    activeDevices->clear();

    //check first the flush
    if (flushFd != -1) {
        if ((pollFds[numFds].revents & POLLIN) || (pollFds[numFds].revents & POLLPRI)) {
            LOG1("%s: Poll returning from flush", __func__);
            return ret;
        }
    }

    // check other active devices.
    for (int i = 0; i < numFds; i++) {
        if (pollFds[i].revents & POLLERR) {
            LOG1("%s: received POLLERR", __func__);
            return UNKNOWN_ERROR;
        }
        // return nodes that have data available
        if (pollFds[i].revents & events) {
            activeDevices->push_back(devices->at(i));
            LOG2("%s: active device: %s, fd: %d, events 0x%X", __func__, devices->at(i)->getDevName(), devices->at(i)->getDevFd(), pollFds[i].revents);
        } else {
            LOG2("%s: inactive device: %s, fd: %d, events 0x%X", __func__, devices->at(i)->getDevName(), devices->at(i)->getDevFd(), pollFds[i].revents);
        }
    }

    return ret;
}

} //namespace icamera
