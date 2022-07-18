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

#pragma once

#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>

#include "SysCall.h"

using namespace std;

namespace icamera {

/**
 * Video node types
 */
enum VideoNodeType {
    // video node device
    VIDEO_GENERIC,
    VIDEO_GENERIC_MEDIUM_EXPO,
    VIDEO_GENERIC_SHORT_EXPO,
    VIDEO_AA_STATS,
    VIDEO_ISA_CONFIG,
    VIDEO_ISA_SCALE,
    VIDEO_CSI_META,

    // sensor subdevice
    VIDEO_PIXEL_ARRAY,
    VIDEO_PIXEL_BINNER,
    VIDEO_PIXEL_SCALER,

    // ISP subdevice
    VIDEO_ISA_DEVICE,
    VIDEO_ISYS_RECEIVER,
    VIDEO_ISYS_RECEIVER_BACKEND,
};

class V4l2DevBase {
public:
    V4l2DevBase();
    virtual ~V4l2DevBase();
    explicit V4l2DevBase(const string& devName);
    int openDev();
    int getDevFd()  {return mDevFd; }
    const char* getDevName() {return mDevName.c_str(); }
    static int pollDevices(const vector<V4l2DevBase*> *devices,
                           vector<V4l2DevBase*> *activeDevices,
                           int timeOut, int flushFd = -1,
                           int events = POLLPRI | POLLIN | POLLERR);

    static VideoNodeType getNodeType(const char* nodeName);
    static const char* getNodeName(VideoNodeType nodeType);

protected:
    int mDevFd;
    string mDevName;
    SysCall *mSC;
};

} //namespace icamera

