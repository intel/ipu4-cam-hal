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

#define LOG_TAG "CaptureUnit"

#include <poll.h>

#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"

#include "PlatformData.h"
#include "MediaControl.h"
#include "CaptureUnit.h"

namespace icamera {

CaptureUnit::CaptureUnit(int cameraId, int memType) :
    BufferProducer(memType),
    mCameraId(cameraId),
    mState(CAPTURE_UNINIT),
    mExitPending(false)
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    mIsaAdaptor = new IspParamAdaptor(mCameraId, PG_PARAM_ISYS);
    mPollThread = new PollThread(this);

    mMaxBuffersInDevice = PlatformData::getExposureLag(mCameraId) + 1;
    if (mMaxBuffersInDevice < 2) {
        mMaxBuffersInDevice = 2;
    }
}

CaptureUnit::~CaptureUnit()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    delete mPollThread;
    delete mIsaAdaptor;
}

int CaptureUnit::init()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    mState = CAPTURE_INIT;

    return OK;
}

void CaptureUnit::deinit()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mState == CAPTURE_UNINIT) {
        LOG1("%s: deinit without init", __func__);
        return;
    }

    destroyDevices();
    mPollThread->join();

    mState = CAPTURE_UNINIT;
}

int CaptureUnit::createDevices()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    destroyDevices();

    // Default INVALID_PORT means the device isn't associated with any outside consumers.
    const Port kDefaultPort = INVALID_PORT;
    Port portOfMainDevice = findDefaultPort(mOutputFrameInfo);
    // Use the config for main port as the default one.
    const stream_t& kDefaultStream = mOutputFrameInfo.at(portOfMainDevice);
    // targetPorts specifies the desired port for the device. But the real port which will be used
    // is deciced whether the port is provided by the consumer.
    vector<Port> targetPorts;

    // Use VIDEO_GENERIC by default, update it VIDEO_ISA_SCALE is ISA scale enabled.
    VideoNodeType nodeType = PlatformData::isISysScaleEnabled(mCameraId) ? VIDEO_ISA_SCALE :
                                                                           VIDEO_GENERIC;
    mDevices.push_back(new MainDevice(mCameraId, nodeType, this));
    targetPorts.push_back(portOfMainDevice);

    if (PlatformData::isDolShortEnabled(mCameraId)) {
        mDevices.push_back(new DolCaptureDevice(mCameraId, VIDEO_GENERIC_SHORT_EXPO));
        targetPorts.push_back(SECOND_PORT);
    }

    if (PlatformData::isDolMediumEnabled(mCameraId)) {
        mDevices.push_back(new DolCaptureDevice(mCameraId, VIDEO_GENERIC_MEDIUM_EXPO));
        targetPorts.push_back(THIRD_PORT);
    }

    if (PlatformData::isISysScaleEnabled(mCameraId)) {
        mDevices.push_back(new IsaRawDevice(mCameraId, VIDEO_GENERIC));
        targetPorts.push_back(SECOND_PORT);
    }

    if (PlatformData::isIsaEnabled(mCameraId)) {
        int ret = mIsaAdaptor->init();
        Check((ret != OK), ret, "Init ISA adaptor failed with:%d", ret);

        ret = configureIsaAdaptor(kDefaultStream);
        Check(ret != OK, ret, "%s: Failed to configure ISA ISP adaptor.", __func__);

        mDevices.push_back(new IsaConfigDevice(mCameraId, VIDEO_ISA_CONFIG, mIsaAdaptor));
        targetPorts.push_back(kDefaultPort);

        mDevices.push_back(new IsaStatsDevice(mCameraId, VIDEO_AA_STATS, mIsaAdaptor));
        targetPorts.push_back(kDefaultPort);
    }

    // Open and configure the devices. The stream and port that are used by the device is
    // decided by whether consumer has provided such info, use the default one if not.
    for (uint8_t i = 0; i < mDevices.size(); i++) {
        DeviceBase* device = mDevices[i];

        int ret = device->openDevice();
        Check(ret != OK, ret, "Open device(%s) failed:%d", device->getName(), ret);

        const Port kTargetPort = targetPorts[i];
        bool hasPort = mOutputFrameInfo.find(kTargetPort) != mOutputFrameInfo.end();
        const stream_t& stream = hasPort ? mOutputFrameInfo.at(kTargetPort) : kDefaultStream;

        ret = device->configure(hasPort ? kTargetPort : kDefaultPort, stream);
        Check(ret != OK, ret, "Configure device(%s) failed:%d", device->getName(), ret);
    }

    return OK;
}

void CaptureUnit::destroyDevices()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    for (auto device : mDevices) {
        device->closeDevice();
        delete device;
    }
    mDevices.clear();

    if (PlatformData::isIsaEnabled(mCameraId)) {
        mIsaAdaptor->deinit();
    }
}

/**
 * Find the device that can handle the given port.
 */
DeviceBase* CaptureUnit::findDeviceByPort(Port port)
{
    for (auto device : mDevices) {
        if (device->getPort() == port) {
            return device;
        }
    }

    return nullptr;
}

int CaptureUnit::streamOn()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    for (auto device : mDevices) {
        int ret = device->streamOn();
        Check(ret < 0, INVALID_OPERATION, "Device:%s stream on failed.", device->getName());
    }

    return OK;
}

int CaptureUnit::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    AutoMutex l(mLock);
    CheckWarning(mState == CAPTURE_START, OK, "@%s: device already started", __func__);

    int ret = streamOn();
    if (ret != OK) {
        streamOff();
        LOGE("Devices stream on failed:%d", ret);
        return ret;
    }

    mPollThread->run("CaptureUnit", PRIORITY_URGENT_AUDIO);
    mState = CAPTURE_START;
    mExitPending = false;
    LOG2("@%s: automation checkpoint: flag: poll_started", __func__);

    return OK;
}

void CaptureUnit::streamOff()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    for (auto device : mDevices) {
        device->streamOff();
    }
}

int CaptureUnit::stop()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s, mCameraId:%d", __func__, mCameraId);

    if (mState != CAPTURE_START) {
        LOGW("@%s: device not started", __func__);
        return OK;
    }

    mExitPending = true;
    mPollThread->requestExit();
    streamOff();
    mPollThread->requestExitAndWait();

    AutoMutex   l(mLock);
    mState = CAPTURE_STOP;

    for (auto device : mDevices) {
        device->resetBuffers();
    }

    LOG2("@%s: automation checkpoint: flag: poll_stopped", __func__);

    mExitPending = false; // It's already stopped.

    return OK;
}

/**
 * Check if the given outputFrames are different from the previous one.
 * Only return false when the config for each port is exactly same.
 */
bool CaptureUnit::isNewConfiguration(const map<Port, stream_t>& outputFrames)
{
    for (const auto& item : outputFrames) {
        if (mOutputFrameInfo.find(item.first) == mOutputFrameInfo.end()) {
            return true;
        }

        const stream_t& oldStream = mOutputFrameInfo[item.first];
        const stream_t& newStream = item.second;

        bool isNewConfig = (oldStream.width != newStream.width || oldStream.height != newStream.height
               || oldStream.format != newStream.format || oldStream.field != newStream.field
               || oldStream.memType != newStream.memType);
        if (isNewConfig) {
            return true;
        }
    }

    return false;
}

int CaptureUnit::configure(const map<Port, stream_t>& outputFrames, const vector<ConfigMode>& configModes)
{
    PERF_CAMERA_ATRACE();

    Check(outputFrames.empty(), BAD_VALUE, "No frame info configured.");
    Check(mState != CAPTURE_CONFIGURE && mState != CAPTURE_INIT && mState != CAPTURE_STOP,
          INVALID_OPERATION, "@%s: Configure in wrong state %d", __func__, mState);

    Port port = findDefaultPort(outputFrames);
    const stream_t& mainStream = outputFrames.at(port);

    if (!isNewConfiguration(outputFrames)) {
        LOGD("@%s: Configuration is not changed.", __func__);
        configureIsaAdaptor(mainStream); // ISA adaptor needs to be re-configured.
        return OK;
    }

    for (const auto& item : outputFrames) {
        LOG1("%s, mCameraId:%d, port:%d, w:%d, h:%d, f:%s", __func__, mCameraId, item.first,
              item.second.width, item.second.height,
              CameraUtils::format2string(item.second.format));
    }

    mConfigModes = configModes;
    mOutputFrameInfo = outputFrames;

    /* media ctl setup */
    MediaCtlConf *mc = PlatformData::getMediaCtlConf(mCameraId);
    Check(!mc, BAD_VALUE, "get format configuration failed for %s (%dx%d)",
            CameraUtils::format2string(mainStream.format), mainStream.width, mainStream.height);

    int status = MediaControl::getInstance()->mediaCtlSetup(mCameraId, mc,
            mainStream.width, mainStream.height, mainStream.field);
    Check(status != OK, status, "set up mediaCtl failed");

    // Create, open, and configure all of needed devices.
    status = createDevices();
    Check(status != OK, status, "Create devices failed:%d", status);

    mState = CAPTURE_CONFIGURE;

    // mExitPending should also be set false in configure to make buffers queued before start
    mExitPending = false;

    return OK;
}

Port CaptureUnit::findDefaultPort(const map<Port, stream_t>& frames) const
{
    Port availablePorts[] = {MAIN_PORT, SECOND_PORT, THIRD_PORT, FORTH_PORT};
    for (unsigned int i = 0; i < ARRAY_SIZE(availablePorts); i++) {
        if (frames.find(availablePorts[i]) != frames.end()) {
            return availablePorts[i];
        }
    }
    return INVALID_PORT;
}

int CaptureUnit::allocateMemory(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    struct v4l2_buffer &v = camBuffer->getV4l2Buffer();
    Check(v.index >= MAX_BUFFER_COUNT, -1
        ,"index %d is larger than max count %d", v.index, MAX_BUFFER_COUNT);
    Check(v.memory != V4L2_MEMORY_MMAP, -1
        ,"Allocating Memory Capture device only supports MMAP mode.");

    DeviceBase* device = findDeviceByPort(port);
    Check(!device, BAD_VALUE, "No device available for port:%d", port);

    int ret = device->getV4l2Device()->queryBuffer(v.index, true, &camBuffer->getV4l2Buffer());
    Check(ret < 0, ret, "query buffer failed ret(%d) for port:%d", ret, port);
    ret = camBuffer->allocateMemory(device->getV4l2Device());
    Check(ret < 0, ret, "Failed to allocate memory ret(%d) for port:%d", ret, port);

    return OK;
}

int CaptureUnit::configureIsaAdaptor(const stream_t &stream)
{
    if (!PlatformData::isIsaEnabled(mCameraId)) {
        return OK;
    }

    Check(mConfigModes.empty(), INVALID_OPERATION, "empty config modes");

    TuningMode tuningMode;
    int status = PlatformData::getTuningModeByConfigMode(mCameraId, mConfigModes[0], tuningMode);
    Check(status != OK, status, "%s, get tuningModes failed %d", __func__, status);

    status = mIsaAdaptor->configure(stream, mConfigModes[0], tuningMode);
    Check(status != OK, -1, "Failed to configure isa adaptor");

    return OK;
}

int CaptureUnit::qbuf(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    Check(camBuffer == nullptr, BAD_VALUE, "Camera buffer is null");
    Check((mState == CAPTURE_INIT || mState == CAPTURE_UNINIT), INVALID_OPERATION,
          "@%s: qbuf in wrong state %d", __func__, mState);

    DeviceBase* device = findDeviceByPort(port);
    Check(!device, BAD_VALUE, "No device available for port:%d", port);

    LOG2("@%s, mCameraId:%d, queue CameraBuffer: %p to port:%d",
         __func__, mCameraId, camBuffer.get(), port);

    device->addPendingBuffer(camBuffer);

    return processPendingBuffers();
}

int CaptureUnit::queueAllBuffers()
{
    PERF_CAMERA_ATRACE();

    if (mExitPending) return OK;

    long predictSequence = -1;

    for (auto device : mDevices) {
        int ret = device->queueBuffer(predictSequence);
        if (mExitPending) break;
        Check(ret != OK, ret, "Failed to queue buffer to device:%s, ret=%d",
             device->getName(), ret);
        if (predictSequence == -1) {
            predictSequence = device->getPredictSequence();
        }
    }

    return OK;
}

void CaptureUnit::onDequeueBuffer()
{
    processPendingBuffers();
}

int CaptureUnit::processPendingBuffers()
{
    LOG2("%s: buffers in device:%d", __func__, mDevices.front()->getBufferNumInDevice());

    while (mDevices.front()->getBufferNumInDevice() < mMaxBuffersInDevice) {
        bool hasPendingBuffer = true;
        for (auto device : mDevices) {
            if (!device->hasPendingBuffer()) {
                hasPendingBuffer = false;
                break;
            }
        }
        // Do not queue buffer when one of the devices has no pending buffers.
        if (!hasPendingBuffer) break;

        int ret = queueAllBuffers();

        if (mExitPending) break;

        Check(ret != OK, ret, "Failed to queue buffers, ret=%d", ret);
    }

    return OK;
}

int CaptureUnit::poll()
{
    PERF_CAMERA_ATRACE();
    int ret = 0;
    const int poll_timeout_count = 10;
    const int poll_timeout = 1000;
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    Check((mState != CAPTURE_CONFIGURE && mState != CAPTURE_START), INVALID_OPERATION,
          "@%s: poll buffer in wrong state %d", __func__, mState);

    int timeOutCount = poll_timeout_count;

    vector<V4l2DevBase*> allDevices, activeDevices;
    for (auto device : mDevices) {
        allDevices.push_back(device->getV4l2Device());
        LOG2("@%s: device:%s has %d buffers queued.",
              __func__, device->getName(), device->getBufferNumInDevice());
    }

    while (timeOutCount-- && ret == 0) {
        // If stream off, no poll needed.
        if (mExitPending) {
            LOG2("%s: mExitPending is true, exit", __func__);
            //Exiting, no error
            return -1;
        }

        ret = V4l2DevBase::pollDevices(&allDevices, &activeDevices,
                                       poll_timeout, -1, POLLPRI | POLLIN | POLLOUT | POLLERR);

        LOG2("@%s: automation checkpoint: flag: poll_buffer, ret:%d", __func__, ret);
    }

    //In case poll error after stream off
    if (mExitPending) {
        LOG2("%s: mExitPending is true, exit", __func__);
        //Exiting, no error
        return -1;
    }

    Check(ret < 0, UNKNOWN_ERROR, "%s: Poll error, ret:%d", __func__, ret);

    if (ret == 0) {
        LOG1("%s, cameraId: %d: timeout happens, wait recovery", __func__, mCameraId);
        return OK;
    }

    for (auto activeDevice : activeDevices) {
        for (auto device : mDevices) {
            if (device->getV4l2Device() == activeDevice) {
                int ret = device->dequeueBuffer();
                if (mExitPending) return -1;

                if (ret != OK) {
                    LOGE("Device:%s grab frame failed:%d", device->getName(), ret);
                }
                break;
            }
        }
    }

    return OK;
}

void CaptureUnit::addFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    for (auto device : mDevices) {
        device->addFrameListener(listener);
    }
}

void CaptureUnit::removeFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    for (auto device : mDevices) {
        device->removeFrameListener(listener);
    }
}

void CaptureUnit::removeAllFrameAvailableListener()
{
    LOG1("%s camera id:%d", __func__, mCameraId);

    AutoMutex   l(mLock);
    for (auto device : mDevices) {
        device->removeAllFrameListeners();
    }
}

void CaptureUnit::registerListener(EventType eventType, EventListener* eventListener)
{
    for (auto device : mDevices) {
        device->registerListener(eventType, eventListener);
    }
}

void CaptureUnit::removeListener(EventType eventType, EventListener* eventListener)
{
    for (auto device : mDevices) {
        device->removeListener(eventType, eventListener);
    }
}
} // namespace icamera

