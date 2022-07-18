/*
 * Copyright (C) 2018 Intel Corporation.
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

#define LOG_TAG "DeviceBase"

#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"
#include "linux/intel-ipu4-isys.h"

#include "SyncManager.h"
#include "PlatformData.h"
#include "DeviceBase.h"
#include "CameraEventType.h"
#include "V4l2DeviceFactory.h"

namespace icamera {

DeviceBase::DeviceBase(int cameraId, VideoNodeType nodeType, VideoNodeDirection nodeDirection,
                       DeviceCallback* deviceCB) :
        mCameraId(cameraId),
        mPort(INVALID_PORT),
        mNodeType(nodeType),
        mNodeDirection(nodeDirection),
        mName(V4l2DevBase::getNodeName(nodeType)),
        mLatestSequence(-1),
        mNeedSkipFrame(false),
        mDeviceCB(deviceCB)
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    mFrameSkipNum = PlatformData::getInitialSkipFrame(mCameraId);
    mDevice = new V4l2Dev(mCameraId, mNodeType, mNodeDirection);
}

DeviceBase::~DeviceBase()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    delete mDevice;
}

int DeviceBase::openDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    if (PlatformData::isEnableFrameSyncCheck(mCameraId))
        SyncManager::getInstance()->updateSyncCamNum();

    return mDevice->openDev();
}

void DeviceBase::closeDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
    {
        AutoMutex l(mBufferLock);

        mPendingBuffers.clear();
        mBuffersInDevice.clear();
        mAllocatedBuffers.clear();
    }

    mDevice->closeDev();
}

int DeviceBase::configure(Port port, const stream_t& config)
{
    LOG1("%s, camera id:%d device:%s, port:%d", __func__, mCameraId, mName, port);

    mPort = port;

    int ret = createBufferPool(config);
    Check(ret != OK, NO_MEMORY, "Failed to create buffer pool:%d", ret);

    resetBuffers();

    return OK;
}

int DeviceBase::streamOn()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    mFrameSkipNum = PlatformData::getInitialSkipFrame(mCameraId);

    return mDevice->streamOn();
}

int DeviceBase::streamOff()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    return mDevice->streamOff();
}

int DeviceBase::queueBuffer(long sequence)
{
    LOG2("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    shared_ptr<CameraBuffer> buffer;
    AutoMutex l(mBufferLock);
    if (mPendingBuffers.empty()) {
        LOG2("Device:%s has no pending buffer to be queued.", mName);
        return OK;
    }
    buffer = mPendingBuffers.front();

    int ret = onQueueBuffer(sequence, buffer);
    Check(ret != OK, ret, "Device:%s failed to preprocess the buffer with ret=%d", mName, ret);

    ret = mDevice->qbuf(buffer->getV4l2Buffer());
    if (ret >= 0) {
        mPendingBuffers.pop_front();
        mBuffersInDevice.push_back(buffer);
    }

    return ret;
}

int DeviceBase::dequeueBuffer()
{
    LOG2("%s, camera id:%d device:%s", __func__, mCameraId, mName);

    shared_ptr<CameraBuffer> camBuffer = getFirstDeviceBuffer();
    Check(!camBuffer, UNKNOWN_ERROR, "No buffer in device:%s.", mName);

    int ret = OK;
    struct v4l2_buffer &vbuf = camBuffer->getV4l2Buffer();
    int targetIndex = camBuffer->getIndex();
    int actualIndex = mDevice->grabFrame(vbuf);
    Check(actualIndex < 0, BAD_VALUE, "Device grabFrame failed:%d", actualIndex);
    if (actualIndex != targetIndex) {
        LOGE("%s, CamBuf index isn't same with index used by kernel", __func__);
        ret = BAD_VALUE;
    }

    mNeedSkipFrame = needQueueBack(camBuffer);
    popBufferFromDevice();

    // TODO: Will add device name info to distinguish different devices.
    PERF_CAMERA_ATRACE_PARAM3("grabFrame SeqID", camBuffer->getSequence(),
                              "csi2_port",       camBuffer->getCsi2Port(),
                              "virtual_channel", camBuffer->getVirtualChannel());

    ret |= onDequeueBuffer(camBuffer);

    // Skip initial frames if needed.
    if (mFrameSkipNum > 0) {
        mFrameSkipNum--;
    }
    return ret;
}

int DeviceBase::getBufferNumInDevice()
{
    AutoMutex l(mBufferLock);

    return mBuffersInDevice.size();
}

void DeviceBase::resetBuffers()
{
    AutoMutex l(mBufferLock);

    mBuffersInDevice.clear();
    mPendingBuffers.clear();

    for (const auto& buffer : mAllocatedBuffers) {
        mPendingBuffers.push_back(buffer);
    }
}

bool DeviceBase::hasPendingBuffer()
{
    AutoMutex l(mBufferLock);

    return !mPendingBuffers.empty();
}

void DeviceBase::addPendingBuffer(const shared_ptr<CameraBuffer>& buffer)
{
    AutoMutex l(mBufferLock);

    mPendingBuffers.push_back(buffer);
}

long DeviceBase::getPredictSequence()
{
    AutoMutex l(mBufferLock);

    return mLatestSequence + mFrameSkipNum + mBuffersInDevice.size();
}

shared_ptr<CameraBuffer> DeviceBase::getFirstDeviceBuffer()
{
    AutoMutex l(mBufferLock);

    return mBuffersInDevice.empty() ? nullptr : mBuffersInDevice.front();
}

bool DeviceBase::skipFrameAfterSyncCheck(long sequence)
{
    //For multi-camera sensor, to check whether the frame synced or not
    int count = 0;
    const int timeoutDuration = 1000; //1ms
    const int maxCheckTimes = 10;  //10 times
    while (!SyncManager::getInstance()->isSynced(mCameraId, sequence)) {
        usleep(timeoutDuration);
        count++;
        if (count > maxCheckTimes) {
            return true;
        }
    }
    return false;
}

void DeviceBase::popBufferFromDevice()
{
    AutoMutex l(mBufferLock);
    if (mBuffersInDevice.empty()) {
        return;
    }

    shared_ptr<CameraBuffer> camBuffer = mBuffersInDevice.front();
    mBuffersInDevice.pop_front();
    mLatestSequence = camBuffer->getSequence();

    if (mNeedSkipFrame) {
        mPendingBuffers.push_back(camBuffer);
    }
}

void DeviceBase::dumpFrame(const shared_ptr<CameraBuffer>& buffer)
{
    if (!CameraDump::isDumpTypeEnable(DUMP_ISYS_BUFFER)) return;

    LOGD("@%s, ISYS: fmt:%s(%dx%d), stride:%d, len:%d", __func__,
         CameraUtils::format2string(buffer->getFormat()),
         buffer->getWidth(), buffer->getHeight(), buffer->getStride(), buffer->getBufferSize());

    CameraDump::dumpImage(mCameraId, buffer, M_ISYS, mPort);
}

MainDevice::MainDevice(int cameraId, VideoNodeType nodeType, DeviceCallback* deviceCB) :
        DeviceBase(cameraId, nodeType, INPUT_VIDEO_NODE, deviceCB)
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

MainDevice::~MainDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

int MainDevice::createBufferPool(const stream_t& config)
{
    LOG1("%s, camera id:%d, fmt:%s(%dx%d) field:%d", __func__, mCameraId,
         CameraUtils::pixelCode2String(config.format), config.width, config.height, config.field);

    bool setWithHeaderCtl = true;
    string subDeviceNodeName;

    if (PlatformData::getDevNameByType(mCameraId, VIDEO_ISYS_RECEIVER, subDeviceNodeName) == OK) {
        LOG1("%s: found ISYS receiver subdevice %s", __func__, subDeviceNodeName.c_str());
        if (PlatformData::isTPGReceiver(mCameraId)) {
            LOG1("%s: no need to set csi header ctrl for tpg", __func__);
            setWithHeaderCtl = false;
        }
    } else {
        setWithHeaderCtl = false;
    }

    int withHeader = 1;
    struct v4l2_format v4l2fmt;
    v4l2fmt.fmt.pix_mp.field = config.field;

    if (PlatformData::isCSIFrontEndCapture(mCameraId)) {
        int planesNum = CameraUtils::getNumOfPlanes(config.format);
        LOG1("@%s Num of planes: %d, mCameraId:%d", __func__, planesNum, mCameraId);

        v4l2fmt.fmt.pix_mp.width = config.width;
        v4l2fmt.fmt.pix_mp.height = config.height;
        v4l2fmt.fmt.pix_mp.num_planes = planesNum;
        v4l2fmt.fmt.pix_mp.pixelformat = config.format;
        for (int i = 0; i < v4l2fmt.fmt.pix_mp.num_planes; i++) {
            v4l2fmt.fmt.pix_mp.plane_fmt[i].bytesperline = config.width;
            v4l2fmt.fmt.pix_mp.plane_fmt[i].sizeimage = 0;
        }
        // The frame data is without header(MIPI STORE MODE) when
        // format is YUV/RGB and frame output from CSI-Front-End entity.
        if (!CameraUtils::isRaw(config.format)) {
            LOG2("@%s, set frame without header for format: %s",
                    __func__, CameraUtils::pixelCode2String(config.format));
            withHeader = 0;
        }
    } else {
        v4l2fmt.fmt.pix.width = config.width;
        v4l2fmt.fmt.pix.height = config.height;
        v4l2fmt.fmt.pix.pixelformat = config.format;
        v4l2fmt.fmt.pix.bytesperline = config.width;
        v4l2fmt.fmt.pix.sizeimage = 0;
    }

    if (setWithHeaderCtl) {
        V4l2SubDev* receiverSubDev = V4l2DeviceFactory::getSubDev(mCameraId, subDeviceNodeName);
        int ret = receiverSubDev->setControl(V4L2_CID_INTEL_IPU4_STORE_CSI2_HEADER, withHeader);
        Check(ret != OK, ret, "set v4l2 store csi2 header failed, ret=%d", ret);
    }
    int ret = mDevice->setFormat(v4l2fmt);
    Check(ret != OK, ret, "set v4l2 format failed ret=%d", ret);

    int realBufferSize = v4l2fmt.fmt.pix.sizeimage;
    int calcBufferSize = CameraUtils::getFrameSize(config.format, config.width, config.height);
    Check(calcBufferSize < realBufferSize, BAD_VALUE,
          "realBufferSize %d is larger than calcBufferSize %d.", realBufferSize, calcBufferSize);

    int bufNum = mDevice->requestBuffers(MAX_BUFFER_COUNT, config.memType);
    Check(bufNum < 0, BAD_VALUE, "request buffers failed return=%d", bufNum);

    return OK;
}

int MainDevice::onDequeueBuffer(shared_ptr<CameraBuffer> buffer)
{
    mDeviceCB->onDequeueBuffer();

    if (mNeedSkipFrame) return OK;

    LOG2("@%s, sequence:%ld, field:%d, timestamp: sec=%ld, usec=%ld",
          __func__, buffer->getSequence(), buffer->getField(),
          buffer->getTimestamp().tv_sec, buffer->getTimestamp().tv_usec);

    for (auto& consumer : mConsumers) {
        consumer->onFrameAvailable(mPort, buffer);
    }

    EventData frameData;
    frameData.type = EVENT_ISYS_FRAME;
    frameData.buffer = nullptr;
    frameData.data.frame.sequence = buffer->getSequence();
    frameData.data.frame.timestamp.tv_sec = buffer->getTimestamp().tv_sec;
    frameData.data.frame.timestamp.tv_usec = buffer->getTimestamp().tv_usec;
    notifyListeners(frameData);

    dumpFrame(buffer);

    return OK;
}

bool MainDevice::needQueueBack(shared_ptr<CameraBuffer> buffer)
{
    bool needSkipFrame = (mFrameSkipNum > 0);

    struct v4l2_buffer &vbuf = buffer->getV4l2Buffer();
    // Check for STR2MMIO Error from kernel space
    if((vbuf.flags & V4L2_BUF_FLAG_ERROR) && PlatformData::isSkipFrameOnSTR2MMIOErr(mCameraId)) {
        // On STR2MMIO error, enqueue this buffer back to V4L2 before notifying the
        // listener/consumer and return
        needSkipFrame = true;
    }
    if (PlatformData::isEnableFrameSyncCheck(mCameraId)) {
        struct camera_buf_info sharedCamBufInfo;
        sharedCamBufInfo.sequence = buffer->getSequence();
        sharedCamBufInfo.sof_ts = buffer->getTimestamp();
        SyncManager::getInstance()->updateCameraBufInfo(mCameraId, &sharedCamBufInfo);
        if (skipFrameAfterSyncCheck(buffer->getSequence())) {
            LOG1("@%s: CameraID:%d sequence %ld been dropped due to frame not sync",
                  __func__, mCameraId, buffer->getSequence());
            needSkipFrame = true;
        }
    }
    return needSkipFrame;
}

DolCaptureDevice::DolCaptureDevice(int cameraId, VideoNodeType nodeType) :
        DeviceBase(cameraId, nodeType, INPUT_VIDEO_NODE)
{
    LOG1("%s, camera id:%d", __func__, mCameraId);
}

DolCaptureDevice::~DolCaptureDevice()
{
    LOG1("%s, camera id:%d", __func__, mCameraId);
}

int DolCaptureDevice::createBufferPool(const stream_t& config)
{
    LOG1("%s, camera id:%d, fmt:%s(%dx%d) field:%d", __func__, mCameraId,
         CameraUtils::pixelCode2String(config.format), config.width, config.height, config.field);

    Check(mPort == INVALID_PORT, NO_MEMORY, "@%s: consumer does not provide DOL buffers", __func__);

    struct v4l2_format v4l2fmt;
    v4l2fmt.fmt.pix.width = config.width;
    v4l2fmt.fmt.pix.height = config.height;
    v4l2fmt.fmt.pix.pixelformat = config.format;
    v4l2fmt.fmt.pix.bytesperline = config.width;
    v4l2fmt.fmt.pix.sizeimage = 0;
    v4l2fmt.fmt.pix_mp.field = 0;

    int ret = mDevice->setFormat(v4l2fmt);
    Check(ret != OK, ret, "set DOL v4l2 format failed ret=%d", ret);

    int bufNum = mDevice->requestBuffers(MAX_BUFFER_COUNT, config.memType);
    Check(bufNum < 0, bufNum, "request DOL buffers failed return=%d", bufNum);

    return OK;
}

int DolCaptureDevice::onDequeueBuffer(shared_ptr<CameraBuffer> buffer)
{
    if (mNeedSkipFrame) {
        return OK; // Do nothing if the buffer needs to be skipped.
    }

    for (auto& consumer : mConsumers) {
        consumer->onFrameAvailable(mPort, buffer);
    }

    dumpFrame(buffer);
    return OK;
}

bool DolCaptureDevice::needQueueBack(shared_ptr<CameraBuffer> buffer)
{
    /**
     * needNotifyBufferDone is used to check if the buffer needs to be returned to its consumer.
     * It is only true when: 1. ISA raw buffers are queued from its consumer;
     *                       2. there is no frame needs to be skipped;
     *                       3. there is no STR2MMIOErr happened.
     * needNotifyBufferDone is false means the buffer needs to be queued back immediately.
     */
    bool needNotifyBufferDone = (mPort != INVALID_PORT);
    if ((buffer->getV4l2Buffer().flags & V4L2_BUF_FLAG_ERROR) &&
            PlatformData::isSkipFrameOnSTR2MMIOErr(mCameraId)) {
        needNotifyBufferDone = false;
    } else if (mFrameSkipNum > 0) {
        needNotifyBufferDone = false;
    }

    return !needNotifyBufferDone;
}

IsaRawDevice::IsaRawDevice(int cameraId, VideoNodeType nodeType) :
        DeviceBase(cameraId, nodeType, INPUT_VIDEO_NODE)
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

IsaRawDevice::~IsaRawDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

int IsaRawDevice::createBufferPool(const stream_t& config)
{
    /**
     * Decide the ISA scale raw config by:
     * 1. Check if it's provided in media config section.
     * 2. Check if it's provided in sensor's common section.
     * 3. Use the main port config if still could find one.
     */
    stream_t isaRawConfig = PlatformData::getISysOutputByPort(mCameraId, mPort);
    if (isaRawConfig.width == 0 || isaRawConfig.height == 0) {
        isaRawConfig = PlatformData::getIsaScaleRawConfig(mCameraId);
        if (isaRawConfig.width == 0 || isaRawConfig.height == 0) {
            isaRawConfig = config;
            // Use specified ISA RAW format in this case.
            isaRawConfig.format = PlatformData::getISysRawFormat(mCameraId);
        }
    }
    Check(isaRawConfig.width == 0 || isaRawConfig.height == 0,
          BAD_VALUE, "No valid ISA raw config.");

    int width = isaRawConfig.width;
    int height = isaRawConfig.height;
    int format = isaRawConfig.format;

    struct v4l2_format v4l2fmt;
    v4l2fmt.fmt.pix.width = width;
    v4l2fmt.fmt.pix.height = height;
    v4l2fmt.fmt.pix.pixelformat = format;
    v4l2fmt.fmt.pix.bytesperline = width;
    v4l2fmt.fmt.pix.sizeimage = 0;
    v4l2fmt.fmt.pix_mp.field = 0;

    int ret = mDevice->setFormat(v4l2fmt);
    Check(ret != OK, ret, "set scaled v4l2 format failed ret=%d", ret);

    int bufNum = mDevice->requestBuffers(MAX_BUFFER_COUNT, V4L2_MEMORY_MMAP);
    Check(bufNum < 0, bufNum, "request scale buffers failed return=%d", bufNum);

    if (mPort != INVALID_PORT) {
        return OK;
    }

    AutoMutex l(mBufferLock);
    mAllocatedBuffers.clear();

    int length = CameraUtils::getFrameSize(v4l2fmt.fmt.pix.pixelformat, width, height);
    for (int i = 0; i < bufNum; i++ ) {
        shared_ptr<CameraBuffer> camIsaBuffer = make_shared<CameraBuffer>(mCameraId,
                BUFFER_USAGE_ISA_CAPTURE,  V4L2_MEMORY_MMAP, length, i, format);
        Check(!camIsaBuffer, NO_MEMORY, "@%s: fail to alloc CameraBuffer", __func__);
        camIsaBuffer->setUserBufferInfo(format, width, height);
        ret = mDevice->queryBuffer(i, true, &camIsaBuffer->getV4l2Buffer());
        camIsaBuffer->allocateMemory(mDevice);
        Check(ret < 0, ret, "set Isa Buffer Poll failed ret=%d", ret);

        mAllocatedBuffers.push_back(camIsaBuffer);
    }

    return OK;
}

int IsaRawDevice::onDequeueBuffer(shared_ptr<CameraBuffer> buffer)
{
    if (mNeedSkipFrame) {
        return OK; // Do nothing if the buffer needs to be skipped.
    }

    for (auto& consumer : mConsumers) {
        consumer->onFrameAvailable(mPort, buffer);
    }

    dumpFrame(buffer);
    return OK;
}

bool IsaRawDevice::needQueueBack(shared_ptr<CameraBuffer> buffer)
{
    /**
     * needNotifyBufferDone is used to check if the buffer needs to be returned to its consumer.
     * It is only true when: 1. ISA raw buffers are queued from its consumer;
     *                       2. there is no frame needs to be skipped;
     *                       3. there is no STR2MMIOErr happened.
     * needNotifyBufferDone is false means the buffer needs to be queued back immediately.
     */
    bool needNotifyBufferDone = (mPort != INVALID_PORT);
    if ((buffer->getV4l2Buffer().flags & V4L2_BUF_FLAG_ERROR) &&
            PlatformData::isSkipFrameOnSTR2MMIOErr(mCameraId)) {
        needNotifyBufferDone = false;
    } else if (mFrameSkipNum > 0) {
        needNotifyBufferDone = false;
    }

    return !needNotifyBufferDone;
}

IsaConfigDevice::IsaConfigDevice(int cameraId, VideoNodeType nodeType, IspParamAdaptor* isaAdaptor) :
        DeviceBase(cameraId, nodeType, OUTPUT_VIDEO_NODE),
        mIsaAdaptor(isaAdaptor)
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

IsaConfigDevice::~IsaConfigDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

int IsaConfigDevice::createBufferPool(const stream_t& config)
{
    int enabledIsaBlocks = 0;
    // ISA param config blocks
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_BLC;
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_LSC;
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_DPC;
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_SCALER;
    // 3A statistics blocks
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_AWB;
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_AF;
    enabledIsaBlocks |= V4L2_INTEL_IPU4_ISA_EN_AE;

    // Create and open ISA subdevice
    string subDeviceNodeName;
    int ret = PlatformData::getDevNameByType(mCameraId, VIDEO_ISA_DEVICE, subDeviceNodeName);
    Check(ret != OK, ret, "%s: Failed to get ISA subdevice.", __func__);

    LOG1("%s: found ISA subdevice: %s", __func__, subDeviceNodeName.c_str());
    V4l2SubDev* isaSubDevice = V4l2DeviceFactory::getSubDev(mCameraId, subDeviceNodeName);
    ret = isaSubDevice->setControl(V4L2_CID_INTEL_IPU4_ISA_EN, enabledIsaBlocks);
    Check(ret != 0, -1, "Failed to configure ISA subdevice");

    // Set the ISA configuration structure to the driver.
    // The ISA configuration consists of 2 planes:
    // 1st plane: process_group_light structure
    // 2nd plane: parameter payload
    // SetFormat sets the plane sizes to the driver.
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    v4l2_fmt.fmt.pix_mp.pixelformat = V4L2_FMT_INTEL_IPU4_ISA_CFG;
    v4l2_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = mIsaAdaptor->getProcessGroupSize();
    v4l2_fmt.fmt.pix_mp.plane_fmt[1].sizeimage = mIsaAdaptor->getInputPayloadSize();
    v4l2_fmt.fmt.pix_mp.num_planes =
        CameraUtils::getNumOfPlanes(V4L2_FMT_INTEL_IPU4_ISA_CFG);

    LOG2("%s: type %d, fmt: %d, 0 size: %d, 1 size: %d, planes: %d", __func__,
        v4l2_fmt.type, v4l2_fmt.fmt.pix_mp.pixelformat, v4l2_fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
        v4l2_fmt.fmt.pix_mp.plane_fmt[1].sizeimage, v4l2_fmt.fmt.pix_mp.num_planes);
    ret = mDevice->setFormat(v4l2_fmt);
    Check(ret != 0, -1, "Failed to set format for device:%s", mName);

    int bufNum = mDevice->requestBuffers(MAX_BUFFER_COUNT, V4L2_MEMORY_MMAP);
    Check(bufNum < 0, bufNum, "request ISA stats buffers failed return=%d", bufNum);

    AutoMutex l(mBufferLock);
    mAllocatedBuffers.clear();

    for (int  i = 0; i < bufNum; i++ ) {
        shared_ptr<CameraBuffer> camBuffer = make_shared<CameraBuffer>(mCameraId,
                BUFFER_USAGE_ISA_PARAM, V4L2_MEMORY_MMAP, 0, i, V4L2_FMT_INTEL_IPU4_ISA_CFG);
        Check(!camBuffer, NO_MEMORY, "@%s: fail to alloc CameraBuffer", __func__);
        camBuffer->setBufferSize(mIsaAdaptor->getProcessGroupSize(), 0);
        camBuffer->setBufferSize(mIsaAdaptor->getInputPayloadSize(), 1);

        ret = mDevice->queryBuffer(i, false, &camBuffer->getV4l2Buffer());
        Check(ret < 0, ret, "set ISA config Buffer Poll failed ret=%d", ret);

        // MMap user space buffer
        camBuffer->allocateMemory(mDevice);
        mAllocatedBuffers.push_back(camBuffer);

        LOG2("%s:  store ISA param buffer vector index: %d, vbuff index: %d",
                   __func__, i, camBuffer->getIndex());
    }

    return OK;
}

int IsaConfigDevice::onQueueBuffer(long sequence, shared_ptr<CameraBuffer> buffer)
{
    // Add isa config payload
    int ret = mIsaAdaptor->encodeIsaParams(buffer, ENCODE_ISA_CONFIG, sequence);
    Check(ret != OK, ret, "Failed to encode ISA config parameters:%d", ret);

    // Update bytesused in buffer
    buffer->setBytesused(mIsaAdaptor->getProcessGroupSize(), 0);
    buffer->setBytesused(mIsaAdaptor->getInputPayloadSize(), 1);
    return OK;
}

IsaStatsDevice::IsaStatsDevice(int cameraId, VideoNodeType nodeType, IspParamAdaptor* isaAdaptor) :
        DeviceBase(cameraId, nodeType, INPUT_VIDEO_NODE),
        mIsaAdaptor(isaAdaptor)
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

IsaStatsDevice::~IsaStatsDevice()
{
    LOG1("%s, camera id:%d device:%s", __func__, mCameraId, mName);
}

int IsaStatsDevice::createBufferPool(const stream_t& config)
{
    // Set the 3A stats configuration structure to the driver.
    // The 3A stats node output has 2 planes:
    // 1st plane: process_group_light structure
    // 2nd plane: statistics payload
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_fmt.fmt.pix_mp.pixelformat = V4L2_FMT_INTEL_IPU4_ISA_CFG;
    v4l2_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = mIsaAdaptor->getProcessGroupSize();
    v4l2_fmt.fmt.pix_mp.plane_fmt[1].sizeimage = mIsaAdaptor->getOutputPayloadSize();
    v4l2_fmt.fmt.pix_mp.num_planes = CameraUtils::getNumOfPlanes(V4L2_FMT_INTEL_IPU4_ISA_CFG);

    LOG2("%s: type %d, fmt: %d, 0 size: %d, 1 size: %d, planes: %d", __func__,
        v4l2_fmt.type, v4l2_fmt.fmt.pix_mp.pixelformat, v4l2_fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
        v4l2_fmt.fmt.pix_mp.plane_fmt[1].sizeimage, v4l2_fmt.fmt.pix_mp.num_planes);
    int ret = mDevice->setFormat(v4l2_fmt);
    Check(ret != 0, -1, "Failed to set format for device:%s", mName);

    int bufNum = mDevice->requestBuffers(MAX_BUFFER_COUNT, V4L2_MEMORY_MMAP);
    Check(bufNum < 0, bufNum, "request ISA stats buffers failed return=%d", bufNum);

    AutoMutex l(mBufferLock);
    mAllocatedBuffers.clear();

    for (int  i= 0; i < bufNum; i++) {
        shared_ptr<CameraBuffer> camBuffer = make_shared<CameraBuffer>(mCameraId,
                BUFFER_USAGE_ISYS_STATS, V4L2_MEMORY_MMAP, 0, i, V4L2_FMT_INTEL_IPU4_ISA_CFG);
        Check(!camBuffer, NO_MEMORY, "@%s: fail to alloc CameraBuffer", __func__);
        camBuffer->setBufferSize(mIsaAdaptor->getProcessGroupSize(), 0);
        camBuffer->setBufferSize(mIsaAdaptor->getOutputPayloadSize(), 1);

        ret = mDevice->queryBuffer(i, false, &camBuffer->getV4l2Buffer());
        Check(ret < 0, ret, "set ISA stats Buffer Poll failed ret=%d", ret);

        // MMap user space buffer
        camBuffer->allocateMemory(mDevice);
        mAllocatedBuffers.push_back(camBuffer);

        LOG2("%s:  store stats buffer vector index: %d, vbuff index: %d",
                   __func__, i, camBuffer->getIndex());
    }

    return OK;
}

int IsaStatsDevice::onQueueBuffer(long sequence, shared_ptr<CameraBuffer> buffer)
{
    // Add stats payload
    int ret = mIsaAdaptor->encodeIsaParams(buffer, ENCODE_STATS, sequence);
    Check(ret != OK, ret, "Failed to encode ISA stats parameters:%d", ret);

    return OK;
}

int IsaStatsDevice::onDequeueBuffer(shared_ptr<CameraBuffer> buffer)
{
    // Decode the statistics data
    mIsaAdaptor->decodeStatsData(TUNING_MODE_MAX, buffer);

    // Notify the listeners of EVENT_ISA_STATS_BUF_READY
    // TODO:  cleanup the stats event data after the cleanup of listeners are finished.
    EventDataStatsReady statsReadyData;
    statsReadyData.sequence = buffer->getSequence();
    statsReadyData.timestamp.tv_sec = buffer->getTimestamp().tv_sec;
    statsReadyData.timestamp.tv_usec = buffer->getTimestamp().tv_usec;

    EventData eventData;
    eventData.type = EVENT_ISA_STATS_BUF_READY;
    eventData.data.statsReady = statsReadyData;

    notifyListeners(eventData);

    AutoMutex l(mBufferLock);
    mPendingBuffers.push_back(buffer);

    return OK;
}

} // namespace icamera

