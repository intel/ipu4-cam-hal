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

#define LOG_TAG "CsiMetaDevice"

#include <poll.h>

#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"
#include "linux/intel-ipu4-isys.h"

#include "CsiMetaDevice.h"
#include "PlatformData.h"

namespace icamera {

CsiMetaDevice::CsiMetaDevice(int cameraId) :
    mCameraId(cameraId),
    mCsiMetaDevice(nullptr),
    mIsCsiMetaEnabled(false),
    mCsiMetaBufferDQIndex(0),
    mBuffersInCsiMetaDevice(0),
    mState(CSI_META_DEVICE_UNINIT),
    mExitPending(false)
{
    LOG1("@%s", __func__);
    mPollThread = new PollThread(this);
    CLEAR(mEmbeddedMetaData);
}

CsiMetaDevice::~CsiMetaDevice()
{
    LOG1("@%s", __func__);
    delete mPollThread;
}

int CsiMetaDevice::init()
{
    LOG1("@%s", __func__);
    return OK;
}

void CsiMetaDevice::deinit()
{
    LOG1("@%s", __func__);
    AutoMutex l(mCsiMetaDeviceLock);
    if (!mIsCsiMetaEnabled) {
        return;
    }
    deinitLocked();
}

void CsiMetaDevice::deinitLocked()
{
    LOG1("@%s", __func__);

    mCsiMetaCameraBuffers.clear();
    deinitDev();
    mPollThread->join();
    mState = CSI_META_DEVICE_UNINIT;
}

int CsiMetaDevice::resetState()
{
    LOG1("@%s", __func__);

    mExitPending = false;
    mCsiMetaBufferDQIndex = 0;
    mBuffersInCsiMetaDevice = 0;
    mCsiMetaCameraBuffers.reserve(CSI_META_BUFFER_NUM);
    mState = CSI_META_DEVICE_INIT;

    return OK;
}

int CsiMetaDevice::initDev()
{
    // close the previous device if any
    deinitDev();

    mCsiMetaDevice = new V4l2Dev(mCameraId, VIDEO_CSI_META, INPUT_VIDEO_NODE);
    int ret = mCsiMetaDevice->openDev();
    if (ret != OK) {
        delete mCsiMetaDevice;
        mCsiMetaDevice = nullptr;

        LOGE("open csi meta dev failed. ret %d", ret);
        return BAD_VALUE;
    }

    mConfiguredDevices.push_back(mCsiMetaDevice);

    return OK;
}

void CsiMetaDevice::deinitDev()
{
    mConfiguredDevices.clear();
    if (mCsiMetaDevice) {
        mCsiMetaDevice->closeDev();
        delete mCsiMetaDevice;
        mCsiMetaDevice = nullptr;
    }
}

int CsiMetaDevice::initEmdMetaData()
{
    // initialize mEmbeddedMetaData
    string videoNodeName;
    int ret = PlatformData::getVideoNodeNameByType(mCameraId, VIDEO_ISYS_RECEIVER, videoNodeName);
    Check(ret != OK, ret, "failed to get device node name");

    McFormat format;

    ret = PlatformData::getFormatByDevName(mCameraId, videoNodeName, format);
    Check(ret != OK, ret, "failed to get format, sub Dev name = %s", videoNodeName.c_str());

    int bpp = CameraUtils::getBpp(format.pixelCode);
    Check(bpp < 0, BAD_VALUE, "failed to get bpp (%d)", bpp);

    int bpl = format.width * bpp / 8;

    mEmbeddedMetaData.csiMetaFormat = V4L2_FMT_INTEL_IPU4_ISYS_META;
    // for embedded meta data, width is equal with bytes per line
    mEmbeddedMetaData.width = bpl;
    //for compatible, set 1 as default, if the *.xml have configed it, use the config value repalce
    mEmbeddedMetaData.height = 1;
    ret = PlatformData::getVideoNodeNameByType(mCameraId, VIDEO_CSI_META, videoNodeName);
    if (ret == OK) {
        ret = PlatformData::getFormatByDevName(mCameraId, videoNodeName, format);
        if (ret == OK)
            mEmbeddedMetaData.height = format.height;
    }

    mEmbeddedMetaData.bpl = bpl;
    mEmbeddedMetaData.planesNum = CameraUtils::getNumOfPlanes(V4L2_FMT_INTEL_IPU4_ISYS_META);

    return OK;
}

int CsiMetaDevice::configure()
{
    LOG1("@%s", __func__);
    AutoMutex l(mCsiMetaDeviceLock);

    if (mState == CSI_META_DEVICE_START) {
        LOGE("@%s: Configure in wrong state %d", __func__, mState);
        return BAD_VALUE;
    }

    deinitLocked();
    if (PlatformData::isCsiMetaEnabled(mCameraId))
        mIsCsiMetaEnabled = true;

    int ret = resetState();
    Check(ret != OK, ret, "@%s: init csi meta device failed", __func__);

    if (!mIsCsiMetaEnabled) {
        return OK;
    }

    ret = initDev();
    Check(ret != 0, BAD_VALUE, "init CSI meta device failed. ret %d", ret);

    ret = initEmdMetaData();
    Check(ret != 0, BAD_VALUE, "init embedded metadata failed. ret %d", ret);

    ret = setFormat();
    Check(ret != 0, BAD_VALUE, "set format failed, ret = %d", ret);

    ret = allocCsiMetaBuffers();
    Check(ret != 0, BAD_VALUE, "failed to alloc CSI buffer, ret = %d", ret);

    mState = CSI_META_DEVICE_CONFIGURED;

    return OK;
}

int CsiMetaDevice::start()
{
    LOG1("@%s", __func__);
    AutoMutex l(mCsiMetaDeviceLock);
    if (!mIsCsiMetaEnabled) {
        return OK;
    }

    if (mState != CSI_META_DEVICE_CONFIGURED && mState != CSI_META_DEVICE_STOP) {
        LOGW("%s: start in wrong state: %d", __func__, mState);
        return OK;
    }

    int ret = mCsiMetaDevice->streamOn();
    Check(ret < 0, ret, "failed to stream on csi meta device, ret = %d", ret);

    mExitPending = false;
    mPollThread->run("CsiMetaDevice", PRIORITY_URGENT_AUDIO);
    mState = CSI_META_DEVICE_START;

    return OK;
}

int CsiMetaDevice::stop()
{
    LOG1("@%s", __func__);
    AutoMutex l(mCsiMetaDeviceLock);
    if (!mIsCsiMetaEnabled) {
        return OK;
    }

    if (mState != CSI_META_DEVICE_START) {
        LOGW("%s: device not started", __func__);
        return OK;
    }

    mExitPending = true;
    mPollThread->requestExit();

    int ret = mCsiMetaDevice->streamOff();
    Check(ret < 0, ret, "failed to stream off csi meta device, ret = %d", ret);

    mPollThread->requestExitAndWait();

    mState = CSI_META_DEVICE_STOP;
    return OK;
}

int CsiMetaDevice::poll()
{
    const int poll_timeout_count = 10;
    const int poll_timeout = 1000;

    int ret = 0;
    int timeOutCount = poll_timeout_count;
    LOG2("@%s before poll number buffer in devices: %d", __func__, mBuffersInCsiMetaDevice.load());

    if (!hasBufferIndevice()) {
        LOG2("@%s there is no buffers in device, skip this poll", __func__);
        return OK;
    }

    vector<V4l2Dev*> activeDevices;
    while (timeOutCount-- && ret == 0) {
        ret = V4l2Dev::pollDevices((vector<V4l2DevBase*>*) &mConfiguredDevices,
                                (vector<V4l2DevBase*>*) &activeDevices,
                                poll_timeout, -1, POLLPRI | POLLIN | POLLOUT | POLLERR);

        LOG2("@%s ing poll number buffer in devices: %d", __func__, mBuffersInCsiMetaDevice.load());
        if (mExitPending) {
            return NO_INIT;
        }
    }

    if (mExitPending) {
        //Exiting, no error
        return NO_INIT;
    }

    //handle the poll error
    Check(ret < 0, ret, "%s: Poll error", __func__);

    if (ret > 0) {
        handleCsiMetaBuffer();
    }

    LOG2("@%s after poll number buffer in devices: %d", __func__, mBuffersInCsiMetaDevice.load());
    return OK;
}

int CsiMetaDevice::hasBufferIndevice()
{
    return mBuffersInCsiMetaDevice.load();
}

int CsiMetaDevice::queueCsiMetaBuffer(const shared_ptr<CameraBuffer> &camBuffer)
{
    int ret = mCsiMetaDevice->qbuf(camBuffer->getV4l2Buffer());
    Check(ret < 0, BAD_VALUE, "%s: Failed to queue CSI meta buffer, ret=%d", __func__, ret);

    LOG2("%s:  queue CSI meta buffer, camBuffer index: %d", __func__, camBuffer->getIndex());

    mBuffersInCsiMetaDevice++;

    return OK;
}

void CsiMetaDevice::handleCsiMetaBuffer()
{
    AutoMutex l(mCsiMetaBuffersLock);

    shared_ptr<CameraBuffer> camBuffer = mCsiMetaCameraBuffers[mCsiMetaBufferDQIndex];
    Check(!camBuffer, VOID_VALUE, "Csi meta camera buffer %d is null", mCsiMetaBufferDQIndex);

    int index  = mCsiMetaDevice->grabFrame(camBuffer->getV4l2Buffer());
    if (mExitPending)   return;
    Check(index < 0, VOID_VALUE, "grab frame failed. index %d", index);

    LOG2("%s: grab meta data buffer sequence %ld timestamp %ld",
        __func__, camBuffer->getSequence(), TIMEVAL2USECS(camBuffer->getTimestamp()));
    PERF_CAMERA_ATRACE_PARAM3("grabFrame SeqID", camBuffer->getSequence(), "csi2_port", camBuffer->getCsi2Port(), \
                                "virtual_channel", camBuffer->getVirtualChannel());

    mBuffersInCsiMetaDevice--;
    mCsiMetaBufferDQIndex = (mCsiMetaBufferDQIndex+1) % mCsiMetaCameraBuffers.size();

    if (CameraDump::isDumpTypeEnable(DUMP_EMBEDDED_METADATA)) {
        BinParam_t bParam;
        bParam.bType    = BIN_TYPE_SENSOR_METADATA;
        bParam.mType    = M_ISYS;
        bParam.sequence = camBuffer->getSequence();
        bParam.mParam.width     = mEmbeddedMetaData.width;
        bParam.mParam.height    = mEmbeddedMetaData.height;
        bParam.mParam.metaFormat= mEmbeddedMetaData.csiMetaFormat;
        for (int i = 0; i < mEmbeddedMetaData.planesNum; i++) {
            bParam.mParam.planeIdx  = i;
            LOGD("@%s, dump sensor meta data, addr = %p, w:%d, h:%d, len:%d", __func__,
                    camBuffer->getBufferAddr(i), mEmbeddedMetaData.width, mEmbeddedMetaData.height,
                    camBuffer->getBufferSize(i));
            CameraDump::dumpBinary(mCameraId, camBuffer->getBufferAddr(i),
                                   camBuffer->getBufferSize(i), &bParam);
        }
    }
    if (Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)
        && strncmp(PlatformData::getSensorName(mCameraId), "ox03a10", strlen("ox03a10")) == 0) {
        char *buffer = static_cast<char*>(camBuffer->getBufferAddr());
        int index = 0x56;
        uint8_t framecount = *(buffer+index);
        index = 0x6e;
        uint8_t highExp = *(buffer+index);
        index = 0x86;
        uint8_t lowExp = *(buffer+index);
        index = 0xce;
        uint8_t dgH = *(buffer+index);
        index = 0xe6;
        uint8_t dgM = *(buffer+index);
        index = 0xfe;
        uint8_t dgL = *(buffer+index);
        index = 0x1a6;
        uint8_t agH = *(buffer+index);
        index = 0x1be;
        uint8_t agL = *(buffer+index);
        LOG3A("%s, count %d, sequence %ld, exp %d, dg %d, ag %d", __func__, framecount,
              camBuffer->getSequence(), highExp<<8 | lowExp, dgH<<10 | dgM<<2 | dgL>>6,
              agH<<6 | agL>>2);
    }


    EventData eventData;
    EventDataMeta metaData;
    CLEAR(metaData);
    metaData.timestamp = camBuffer->getTimestamp();
    metaData.sequence = camBuffer->getSequence();

    int status = decodeMetaData(metaData, camBuffer);

    if (status == OK ||
            PlatformData::getMetadataSize(mCameraId) > 0) {
        eventData.type = EVENT_META;
        eventData.buffer = nullptr;
        eventData.data.meta = metaData;

        MEMCPY_S(eventData.data.meta.raw, RAW_SIZE,
                camBuffer->getBufferAddr(0), camBuffer->getBufferSize());

        notifyListeners(eventData);
    }

    queueCsiMetaBuffer(camBuffer);
}

int CsiMetaDevice::decodeMetaData(EventDataMeta& metaData, const shared_ptr<CameraBuffer> &camBuffer)
{
    vector<int> vbpOffset;
    PlatformData::getDolVbpOffset(mCameraId, vbpOffset);

    if (vbpOffset.size() == 0) {
        LOG2("%s: skip vbp meta data decode, no vbpOffset set.", __func__);
        return INVALID_OPERATION;
    } else if (vbpOffset.size() != 2) {
        LOGE("Invalid vbpOffset value size %lu, skip vpb meta data decode.", vbpOffset.size());
        return BAD_VALUE;
    }

    if (mEmbeddedMetaData.planesNum > 0) {
        char* metaPtr = (char*)(camBuffer->getBufferAddr(0));
        metaData.vbp = metaPtr[vbpOffset[0]] | (metaPtr[vbpOffset[1]] << 8);
        LOG2("%s: DOL sequence %ld, offset %d/%d, value is %d", __func__, metaData.sequence,
             vbpOffset[0], vbpOffset[1], metaData.vbp);
    }

    return OK;
}

int CsiMetaDevice::setFormat()
{
    LOG1("@%s", __func__);

    struct v4l2_format v4l2fmt;
    CLEAR(v4l2fmt);

    v4l2fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2fmt.fmt.pix_mp.width = mEmbeddedMetaData.width;
    v4l2fmt.fmt.pix_mp.height = mEmbeddedMetaData.height;
    v4l2fmt.fmt.pix_mp.num_planes = mEmbeddedMetaData.planesNum;
    v4l2fmt.fmt.pix_mp.pixelformat = mEmbeddedMetaData.csiMetaFormat;
    for (int i = 0; i < v4l2fmt.fmt.pix_mp.num_planes; i++) {
        v4l2fmt.fmt.pix_mp.plane_fmt[i].bytesperline = mEmbeddedMetaData.bpl;
        v4l2fmt.fmt.pix_mp.plane_fmt[i].sizeimage = 0;
    }
    v4l2fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    int ret = mCsiMetaDevice->setFormat(v4l2fmt);
    Check(ret != OK, ret, "set v4l2 format failed ret=%d", ret);

    for (int i = 0; i < v4l2fmt.fmt.pix_mp.num_planes; i++) {
        mEmbeddedMetaData.size[i] = v4l2fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
    }

    LOG2("VIDIOC_S_FMT type %d : width: %d, height: %d, bpl: %d, fourcc: %d, field: %d",
            v4l2fmt.type,
            v4l2fmt.fmt.pix.width,
            v4l2fmt.fmt.pix.height,
            v4l2fmt.fmt.pix.bytesperline,
            v4l2fmt.fmt.pix.pixelformat,
            v4l2fmt.fmt.pix.field);

    return OK;
}

int CsiMetaDevice::allocCsiMetaBuffers()
{
    AutoMutex l(mCsiMetaBuffersLock);

    mCsiMetaCameraBuffers.clear();

    int bufNum = mCsiMetaDevice->requestBuffers(MAX_BUFFER_COUNT, V4L2_MEMORY_MMAP);
    Check(bufNum < 0, bufNum, "request CSI meta buffers failed return=%d", bufNum);

    for (int i=0; i<bufNum; i++) {
        shared_ptr<CameraBuffer> camBuffer = make_shared<CameraBuffer>(mCameraId, BUFFER_USAGE_METADATA,
                                               V4L2_MEMORY_MMAP, 0, i,V4L2_FMT_INTEL_IPU4_ISYS_META);
        Check(!camBuffer, NO_MEMORY, "@%s: fail to alloc CameraBuffer", __func__);

        int ret = mCsiMetaDevice->queryBuffer(i, false, &camBuffer->getV4l2Buffer());
        Check(ret < 0, ret, "Failed to query buffer, ret=%d", ret);

        for (int j=0; j<mEmbeddedMetaData.planesNum; j++) {
            LOG2("@%s, buffer size [%d] = %d", __func__, j, mEmbeddedMetaData.size[j]);
            camBuffer->setBufferSize(mEmbeddedMetaData.size[j], j);
        }

        // MMap user space buffer
        camBuffer->allocateMemory(mCsiMetaDevice);
        mCsiMetaCameraBuffers.push_back(camBuffer);

        // Enqueue CSI meta buffer to ISP
        queueCsiMetaBuffer(camBuffer);

        LOG2("%s:  store csi meta buffer vector index: %d, vbuff index: %d camBuffer->getBufferAddr() = %p",
                   __func__, i, camBuffer->getIndex(), camBuffer->getBufferAddr());
    }

    return OK;
}

} /* namespace icamera */
