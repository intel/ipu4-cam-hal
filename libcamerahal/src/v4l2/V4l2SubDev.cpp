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

#define LOG_TAG "V4l2SubDev"

#include "iutils/CameraLog.h"

#include "iutils/Utils.h"
#include "V4l2SubDev.h"

namespace icamera {

#define CheckDeviceState(ret) \
    do { \
        if (mDevFd == -1) { \
            LOGE("%s: device %s not opened", __func__, mDevName.c_str()); \
            return ret; \
        } \
    } while (0)

V4l2SubDev::V4l2SubDev(const string& devName):
        V4l2DevBase(devName)
{
    LOG1("@%s %s", __func__, devName.c_str());
}

V4l2SubDev::~V4l2SubDev()
{
    LOG1("@%s %s", __func__, mDevName.c_str());
}

/************* Public Member Functions ****************/
int V4l2SubDev::openSubDev()
{
    LOG1("@%s %s", __func__, mDevName.c_str());

    int ret = V4l2DevBase::openDev();
    Check(ret != 0, ret, "%s: Failed to open V4l2Dev device node %s %s", __func__, mDevName.c_str(), strerror(ret));

    return 0;
}

void V4l2SubDev::closeSubDev()
{
    LOG1("@%s %s", __func__, mDevName.c_str());

    if (mDevFd != -1) {
        mSC->close(mDevFd);
        mDevFd = -1;
    }
}

int V4l2SubDev::setFormat(struct v4l2_mbus_framefmt *format, unsigned int pad, enum v4l2_subdev_format_whence which, unsigned int stream)
{
    LOG1("@%s %s pad %d, stream %d", __func__, mDevName.c_str(), pad, stream);
    CheckDeviceState(-1);

    struct v4l2_subdev_format fmt;
    int ret;
    memset(&fmt, 0, sizeof(fmt));
    fmt.pad = pad;
    fmt.which = which;
    fmt.format = *format;
    // VIRTUAL_CHANNEL_S
    fmt.stream = stream;
    // VIRTUAL_CHANNEL_E

    ret = mSC->ioctl(mDevFd, VIDIOC_SUBDEV_S_FMT, &fmt);
    if (ret < 0) {
        return -errno;
    }

    *format = fmt.format;
    return 0;
}

int V4l2SubDev::setControl(int ctlCmd, int ctlValue)
{
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ctrl;
    struct v4l2_queryctrl query;
    int64_t oldVal = ctlValue;
    int is_64;
    int ret;

    LOG2("@%s %s ctlCmd 0x%x ctlValue %d", __func__, mDevName.c_str(), ctlCmd, ctlValue);
    CheckDeviceState(-1);

    ret = queryControl(ctlCmd, &query);
    if (ret < 0) {
        LOGE("queryControl failed.");
        return -1;
    }

    is_64 = (query.type == V4L2_CTRL_TYPE_INTEGER64);

    memset(&ctrls, 0, sizeof(ctrls));
    memset(&ctrl, 0, sizeof(ctrl));

    ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(ctlCmd);
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    ctrl.id = ctlCmd;
    if (is_64)
        ctrl.value64 = ctlValue;
    else
        ctrl.value = ctlValue;

    ret = mSC->ioctl(mDevFd, VIDIOC_S_EXT_CTRLS, &ctrls);
    if (ret != -1) {
        if (is_64)
            ctlValue = ctrl.value64;
        else
            ctlValue = ctrl.value;
    } else if (!is_64 && query.type != V4L2_CTRL_TYPE_STRING &&
            (errno == EINVAL || errno == ENOTTY)) {
        struct v4l2_control old;

        old.id = ctlCmd;
        old.value = ctlValue;
        ret = mSC->ioctl(mDevFd, VIDIOC_S_CTRL, &old);
        if (ret != -1)
            ctlValue = old.value;
    }
    if (ret == -1) {
        LOGE("unable to set control 0x%8.8x: %s (%d).\n", ctlCmd, strerror(errno), errno);
        return -1;
    }

    LOG2("Control 0x%08x set to %ld, is %d \n", ctlCmd, oldVal, ctlValue);
    return 0;
}

int V4l2SubDev::getControl(int ctlCmd, int *value)
{
    LOG2("@%s, ctlCmd:0x%x", __func__, ctlCmd);
    CheckDeviceState(-1);

    struct v4l2_control control;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control extControl;

    CLEAR(control);
    CLEAR(controls);
    CLEAR(extControl);

    control.id = ctlCmd;
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control.id);
    controls.count = 1;
    controls.controls = &extControl;
    extControl.id = ctlCmd;

    if (mSC->ioctl(mDevFd, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
       *value = extControl.value;
       return 0;
    }

    if (mSC->ioctl(mDevFd, VIDIOC_G_CTRL, &control) == 0) {
       *value = control.value;
       return 0;
    }

    LOGE("Failed to get value for control (%d) on device '%s', %s",
            ctlCmd, mDevName.c_str(), strerror(errno));
    return -1;
}

int V4l2SubDev::getPadFormat(int padIndex, int &width, int &height, int &code)
{
    LOG1("@%s pad: %d", __func__, padIndex);
    CheckDeviceState(-1);

    struct v4l2_subdev_format format;
    CLEAR(format);
    format.pad = padIndex;
    format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    int status = getFormat(format);
    if (status == 0) {
        width = format.format.width;
        height = format.format.height;
        code = format.format.code;
    }
    return status;
}

int V4l2SubDev::getFormat(struct v4l2_subdev_format &aFormat)
{
    LOG1("@%s device = %s", __func__, mDevName.c_str());
    CheckDeviceState(-1);

    int ret = mSC->ioctl(mDevFd, VIDIOC_SUBDEV_G_FMT, &aFormat);
    if (ret < 0) {
        LOGE("VIDIOC_SUBDEV_G_FMT failed: %s", strerror(errno));
        return -1;
    }

    LOG1("VIDIOC_SUBDEV_G_FMT: pad: %d, which: %d, width: %d, height: %d, format: 0x%x, field: %d, color space: %d",
             aFormat.pad,
             aFormat.which,
             aFormat.format.width,
             aFormat.format.height,
             aFormat.format.code,
             aFormat.format.field,
             aFormat.format.colorspace);

    return 0;
}

int V4l2SubDev::queryMenu(v4l2_querymenu &menu)
{
    LOG1("@%s", __func__);
    CheckDeviceState(-1);

    int ret = mSC->ioctl(mDevFd, VIDIOC_QUERYMENU, &menu);
    if (ret != 0) {
        LOGE("Failed to get values for query menu (%d) on device '%s', %s", menu.id, mDevName.c_str(), strerror(errno));
        return -1;
    }

    return 0;
}

int V4l2SubDev::queryControl(int ctlCmd, struct v4l2_queryctrl *query)
{
    LOG1("@%s %s ctlCmd %d", __func__, mDevName.c_str(), ctlCmd);
    CheckDeviceState(-1);

    memset(query, 0, sizeof(*query));
    query->id = ctlCmd;

    int ret = mSC->ioctl(mDevFd, VIDIOC_QUERYCTRL, query);
    if (ret < 0) {
        LOGW("unable to query control 0x%08x: %s.\n", ctlCmd, strerror(errno));
    }

    return ret;
}

int V4l2SubDev::setSelection(int pad, int target, int top, int left, int width, int height)
{
    LOG1("@%s %s pad %d target %d top %d left %d width %d height %d", __func__, mDevName.c_str(), pad, target, top, left, width, height);
    CheckDeviceState(-1);

    struct v4l2_subdev_selection selection;
    memset(&selection, 0, sizeof(struct v4l2_subdev_selection));
    selection.pad = pad;
    selection.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    selection.target = target;
    selection.flags = 0;
    selection.r.top = top;
    selection.r.left = left;
    selection.r.width = width;
    selection.r.height = height;

    return setSelection(selection);
}

int V4l2SubDev::setSelection(struct v4l2_subdev_selection &selection)
{
    LOG1("Call VIDIOC_SUBDEV_S_SELECTION on %s which: %d, pad: %d, target: 0x%x, "
         "flags: 0x%x, rect left: %d, rect top: %d, width: %d, height: %d", mDevName.c_str(),
        selection.which, selection.pad, selection.target, selection.flags,
        selection.r.left, selection.r.top, selection.r.width, selection.r.height);
    CheckDeviceState(-1);

    int ret = mSC->ioctl(mDevFd, VIDIOC_SUBDEV_S_SELECTION, &selection);
    if (ret < 0) {
        LOGE("ioctl VIDIOC_SUBDEV_S_SELECTION failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

// VIRTUAL_CHANNEL_S
int V4l2SubDev::setRouting(v4l2_subdev_route *routes, uint32_t numRoutes)
{
    LOG1("@%s", __func__);
    Check(!routes, -1, "@%s, routes:%p", __func__, routes);

    v4l2_subdev_routing r = {routes, numRoutes};

    for (unsigned int i = 0; i < numRoutes; i++) {
        LOG1("%s, numRoutes:%u, i:%u, sink_pad:%u, source_pad:%u, sink_stream:%u, source_stream:%u, flags:%x",
            __func__, numRoutes, i, routes[i].sink_pad, routes[i].source_pad,
            routes[i].sink_stream, routes[i].source_stream, routes[i].flags);
    }

    int ret = mSC->ioctl(mDevFd, VIDIOC_SUBDEV_S_ROUTING, &r);
    if (ret < 0) {
        LOG1("ioctl VIDIOC_SUBDEV_S_ROUTING failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int V4l2SubDev::getRouting(struct v4l2_subdev_route *routes, uint32_t *numRoutes)
{
    LOG1("@%s", __func__);
    Check((!routes || !numRoutes), -1, "@%s, routes:%p, numRoutes:%p", __func__, routes, numRoutes);

    v4l2_subdev_routing r = {routes, *numRoutes};

    int ret = mSC->ioctl(mDevFd, VIDIOC_SUBDEV_G_ROUTING, &r);
    if (ret < 0) {
        LOG1("ioctl VIDIOC_SUBDEV_G_ROUTING failed: %s", strerror(errno));
        return -1;
    }

    *numRoutes = r.num_routes;

    for (unsigned int i = 0; i < r.num_routes; i++) {
        LOG1("%s, numRoutes:%u, i:%u, sink_pad:%u, source_pad:%u, sink_stream:%u, source_stream:%u, flags:%x",
            __func__, r.num_routes, i, routes[i].sink_pad, routes[i].source_pad,
            routes[i].sink_stream, routes[i].source_stream, routes[i].flags);
    }

    return 0;
}
// VIRTUAL_CHANNEL_E

int V4l2SubDev::subscribeEvent(int event, int id)
{
    LOG1("@%s", __func__);
    int ret(0);
    struct v4l2_event_subscription sub;

    if (mDevFd == -1) {
        LOG1("Device %s already closed. cannot subscribe.", mDevName.c_str());
        return -1;
    }

    CLEAR(sub);
    sub.type = event;
    sub.id = id;

    ret = mSC->ioctl(mDevFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    if (ret < 0) {
        LOGE("error subscribing event %x: %s", event, strerror(errno));
    }

    return ret;
}

int V4l2SubDev::unsubscribeEvent(int event, int id)
{
    LOG1("@%s", __func__);
    int ret(0);
    struct v4l2_event_subscription sub;

    if (mDevFd == -1) {
        LOG1("Device %s closed. cannot unsubscribe.", mDevName.c_str());
        return -1;
    }

    CLEAR(sub);
    sub.type = event;
    sub.id = id;

    ret = mSC->ioctl(mDevFd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
    if (ret < 0) {
        LOGE("error unsubscribing event %x :%s",event,strerror(errno));
    }

    return ret;
}

int V4l2SubDev::dequeueEvent(struct v4l2_event *event)
{
    LOG2("@%s", __func__);
    int ret(0);

    if (mDevFd == -1) {
        LOG1("Device %s closed. cannot dequeue event.", mDevName.c_str());
        return -1;
    }

    ret = mSC->ioctl(mDevFd, VIDIOC_DQEVENT, event);
    if (ret < 0) {
        LOGE("error dequeuing event");
    }

    return ret;
}


} //namespace icamera
