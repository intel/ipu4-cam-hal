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

#define LOG_TAG "V4l2Dev"

#include "iutils/CameraLog.h"
#include "V4l2Buffer.h"
#include "PlatformData.h"

#include "V4l2Dev.h"

namespace icamera {

V4l2Dev::V4l2Dev(int cameraId, VideoNodeType nodeType, VideoNodeDirection nodeDirection) :
    mCameraId(cameraId),
    mFrameCounter(0),
    mType(nodeType),
    mBufType(V4L2_BUF_TYPE_VIDEO_CAPTURE),
    mMemoryType(V4L2_MEMORY_USERPTR),
    mNodeDirection(nodeDirection)
{
    if (PlatformData::getDevNameByType(cameraId, nodeType, mDevName) != OK) {
        LOGE("@%s: Failed to get video device name for cameraId: %d, node type: %d",
              __func__, cameraId, nodeType);
    }

    LOG1("@%s: cameraId:%d, node type:%d, device: %s",
          __func__, mCameraId, nodeType, mDevName.c_str());
}

V4l2Dev::~V4l2Dev()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());
}

int V4l2Dev::openDev()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());
    int ret = V4l2DevBase::openDev();
    Check(ret != 0, ret, "%s: Failed to open V4l2Dev device node %s %s", __func__, mDevName.c_str(), strerror(ret));

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));

    if (queryCap(&cap) != 0) {
        LOGE("%s: Failed to query capability", __func__);
    }

    return 0;
}

int V4l2Dev::closeDev()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    destroyBufferPool();

    if (mDevFd != -1) {
        mSC->close(mDevFd);
        mDevFd = -1;
    }

    return 0;
}

/**
 * queries the capabilities of the device and it does some basic sanity checks
 *
 * \param cap: [OUT] V4L2 capability structure
 *
 * \return 0  if everything went ok or IOCTL operation failed
 * \return DEAD_OBJECT if the basic checks for this object failed
 */
int V4l2Dev::queryCap(struct v4l2_capability *cap)
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    unsigned int capabilities = 0;

    int ret = mSC->ioctl(mDevFd, VIDIOC_QUERYCAP, cap);
    if (ret < 0) {
        LOGE("VIDIOC_QUERYCAP returned: %d (%s)", ret, strerror(errno));
        return 0;
    }

    LOG2( "driver:      '%s'", cap->driver);
    LOG2( "card:        '%s'", cap->card);
    LOG2( "bus_info:      '%s'", cap->bus_info);
    LOG2( "version:      %x", cap->version);
    LOG2( "capabilities:      %x", cap->capabilities);

    /* Do some basic sanity check */

    if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGW("No capture devices - But this is an input video node!");
        return -1;
    }

    if (!(cap->capabilities & V4L2_CAP_STREAMING)) {
        LOGW("Is not a video streaming device");
        return -1;
    }

    //Set buffer types
    capabilities = cap->capabilities & V4L2_CAP_DEVICE_CAPS ? cap->device_caps : cap->capabilities;

    if (mNodeDirection == INPUT_VIDEO_NODE) {
        if (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
            mBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else if (capabilities & V4L2_CAP_VIDEO_CAPTURE) {
            mBufType =  V4L2_BUF_TYPE_VIDEO_CAPTURE;
        }
    } else {
        if (capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
            mBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        } else if (capabilities & V4L2_CAP_VIDEO_OUTPUT) {
            mBufType =  V4L2_BUF_TYPE_VIDEO_OUTPUT;
        }

    }
    LOG2("Input buffer type is %d", mBufType);

    return 0;
}


/**
 * Stop the streaming of buffers of a video device
 * This method is basically a stream-off IOCTL, wake up the poll from driver
 *
 * \return   0 on success
 *          -1 on failure
 */
int V4l2Dev::streamOff()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    enum v4l2_buf_type type = mBufType;
    int ret = mSC->ioctl(mDevFd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("VIDIOC_STREAMOFF returned: %d (%s)", ret, strerror(errno));
        return ret;
    }

    return ret;
}

/**
 * Start the streaming of buffers in a video device
 *
 * This method just calls  call the stream on IOCTL
 */
int V4l2Dev::streamOn()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    enum v4l2_buf_type type = mBufType;

    LOG2("%s: buffer type is: %d", __func__, type);
    int ret = mSC->ioctl(mDevFd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("cameraId:%d, VIDIOC_STREAMON returned: %d (%s)", mCameraId, ret, strerror(errno));
        return ret;
    }

    mFrameCounter = 0;
    return ret;
}

/**
 * Update the current device node configuration (low-level)
 *
 * This methods allows more detailed control of the format than the previous one
 * It updates the internal configuration used to check for discrepancies between
 * configuration and buffer pool properties
 *
 * \param aFormat:[IN] reference to the new v4l2_format .
 *
 *  \return 0 if everything went well
 *          UNKNOWN_ERROR if we get an error from the v4l2 ioctl's
 */
int V4l2Dev::setFormat(struct v4l2_format &aFormat)
{

    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    aFormat.type = mBufType;

    LOG2("VIDIOC_S_FMT type %d : width: %d, height: %d, bpl: %d, fourcc: %d, field: %d",
            aFormat.type,
            aFormat.fmt.pix.width,
            aFormat.fmt.pix.height,
            aFormat.fmt.pix.bytesperline,
            aFormat.fmt.pix.pixelformat,
            aFormat.fmt.pix.field);

    int ret = mSC->ioctl(mDevFd, VIDIOC_S_FMT, &aFormat);
    if (ret < 0) {
        LOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
        return UNKNOWN_ERROR;
    }

    return 0;
}

int V4l2Dev::grabFrame(struct v4l2_buffer &vbuf)
{
    LOG2("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    int ret = dqbuf(vbuf);

    if (ret < 0)
        return ret;

    mFrameCounter++;

    return vbuf.index;
}

void V4l2Dev::destroyBufferPool()
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    requestBuffers(0, mMemoryType);
}

int V4l2Dev::requestBuffers(size_t numBuffers, int memType)
{
    LOG1("@%s: cameraId:%d, device: %s, numBuffers:%zu", __func__, mCameraId, mDevName.c_str(), numBuffers);
    struct v4l2_requestbuffers reqBuf;
    memset(&reqBuf, 0, sizeof(struct v4l2_requestbuffers));

    mMemoryType = memType;
    reqBuf.memory = (enum v4l2_memory)memType;
    reqBuf.count = numBuffers;
    reqBuf.type = mBufType;

    LOG2("VIDIOC_REQBUFS, count=%u, memory=%u, type=%u", reqBuf.count, reqBuf.memory, reqBuf.type);
    int ret = mSC->ioctl(mDevFd, VIDIOC_REQBUFS, &reqBuf);

    if (ret < 0) {
        LOGE("VIDIOC_REQBUFS(%zu) returned: %d (%s)", numBuffers, ret, strerror(errno));
        return ret;
    }

    if (reqBuf.count < numBuffers)
        LOG2("Got less buffers than requested! %u < %zu",reqBuf.count, numBuffers);

    return reqBuf.count;
}

int V4l2Dev::qbuf(struct v4l2_buffer &vbuf)
{
    LOG2("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());
    struct v4l2_buffer *v4l2_buf = &vbuf;

    V4l2Buffer::dump(vbuf, __func__, mDevName.c_str());

    int ret = mSC->ioctl(mDevFd, VIDIOC_QBUF, v4l2_buf);
    if (ret < 0) {
        LOGE("VIDIOC_QBUF on %s failed: %s", mDevName.c_str(), strerror(errno));
        return ret;
    }

    return ret;
}

int V4l2Dev::dqbuf(struct v4l2_buffer &vbuf)
{
    LOG2("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    vbuf.memory = mMemoryType;
    vbuf.type = mBufType;

    int ret = mSC->ioctl(mDevFd, VIDIOC_DQBUF, &vbuf);

    V4l2Buffer::dump(vbuf, __func__, mDevName.c_str());

    return ret;
}

int V4l2Dev::queryBuffer(int index, bool cached, struct v4l2_buffer *vbuf)
{
    LOG1("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    int cacheFlags = V4L2_BUF_FLAG_NO_CACHE_INVALIDATE |
        V4L2_BUF_FLAG_NO_CACHE_CLEAN;

    vbuf->flags = cached? 0: cacheFlags;
    vbuf->type = mBufType;
    vbuf->index = index;
    int ret = mSC->ioctl(mDevFd , VIDIOC_QUERYBUF, vbuf);

    if (ret < 0) {
        LOGE("VIDIOC_QUERYBUF failed: %s", strerror(errno));
        return ret;
    }

    V4l2Buffer::dump(*vbuf, __func__, mDevName.c_str());

    return ret;
}

int V4l2Dev::exportDmaBuf(struct v4l2_buffer &buf, int plane)
{
    struct v4l2_exportbuffer expbuf;
    CLEAR(expbuf);
    expbuf.type = buf.type;
    expbuf.index = buf.index;
    expbuf.plane = plane;

    int ret = mSC->ioctl(mDevFd, VIDIOC_EXPBUF, &expbuf);
    Check(ret < 0, -1, "export buffer error! type %d index %d %s\n",
            buf.type, buf.index, strerror(errno));

    return expbuf.fd;
}

int V4l2Dev::poll(int timeout)
{
    LOG2("@%s: cameraId:%d, device: %s", __func__, mCameraId, mDevName.c_str());

    if (mDevFd < 0) {
        LOG2("Device %s already closed. Do nothing.", mDevName.c_str());
        return -1;
    }

    struct pollfd pfd[1];
    pfd[0].fd = mDevFd;
    pfd[0].events = POLLPRI | POLLIN | POLLERR;
    pfd[0].revents = 0;

    // Handle the return value in the caller
    int ret = mSC->poll(pfd, 1, timeout);

    if (pfd[0].revents & POLLERR)
        return UNKNOWN_ERROR;

    return ret;
}

} //namespace icamera
