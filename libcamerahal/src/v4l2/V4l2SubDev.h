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

#include <unistd.h>
#include <stdlib.h>
#include <linux/v4l2-subdev.h>

#include <iutils/Utils.h>
#include "V4l2DevBase.h"


namespace icamera {

class V4l2SubDev: public V4l2DevBase {
friend class V4l2DeviceFactory;

private:
    DISALLOW_COPY_AND_ASSIGN(V4l2SubDev);

    explicit V4l2SubDev(const string& devName);
    virtual ~V4l2SubDev();

public:
    int openSubDev();
    void closeSubDev();
    int setFormat(struct v4l2_mbus_framefmt *format, unsigned int pad, enum v4l2_subdev_format_whence which, unsigned int stream);
    int getPadFormat(int padIndex, int &width, int &height, int &code);
    int setControl(int ctlCmd, int ctlValue);
    int getControl(int ctlCmd, int *value);
    int setSelection(int pad, int target, int top, int left, int width, int height);
    int queryMenu(v4l2_querymenu &menu);
    int queryControl(int ctlCmd, struct v4l2_queryctrl *query);
    int setSelection(struct v4l2_subdev_selection &selection);
    // VIRTUAL_CHANNEL_S
    int setRouting(struct v4l2_subdev_route *routes, uint32_t numRoutes);
    int getRouting(struct v4l2_subdev_route *routes, uint32_t *numRoutes);
    // VIRTUAL_CHANNEL_E
    int getFormat(struct v4l2_subdev_format &aFormat);
    int subscribeEvent(int event, int id = 0);
    int unsubscribeEvent(int event, int id = 0);
    int dequeueEvent(struct v4l2_event *event);
};

} //namespace icamera
