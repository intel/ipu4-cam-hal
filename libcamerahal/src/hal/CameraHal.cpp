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

#define LOG_TAG "CameraHal"

#include <vector>

#include "iutils/CameraLog.h"

#include "ICamera.h"
#include "PlatformData.h"
#include "SyncManager.h"
#include "CameraHal.h"
#include "Parameters.h"

namespace icamera {

#define checkCameraDevice(device, err_code) \
    do { \
        if (mState == HAL_UNINIT) { \
            LOGE("HAL is not init."); \
            return err_code; \
        } \
        if (!(device)) { \
            LOGE("device is not open."); \
            return err_code; \
        } \
    } while (0)

CameraHal::CameraHal() :
    mInitTimes(0),
    mState(HAL_UNINIT)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s", __func__);

    CLEAR(mCameraDevices);
    CLEAR(mTotalVirtualChannelCamNum);
    CLEAR(mConfigTimes);
}

CameraHal::~CameraHal()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s", __func__);
    SyncManager::releaseInstance();
    PlatformData::releaseInstance();
}

int CameraHal::init()
{
    LOG1("@%s", __func__);
    AutoMutex lock(mLock);

    if (mInitTimes++ > 0) {
        LOGD("@%s, mInitTimes:%d, return without running", __func__, mInitTimes);
        return OK;
    }

    MediaControl::getInstance()->initEntities();

    for (int i = 0; i < MAX_VC_GROUP_NUMBER; i++) {
        mTotalVirtualChannelCamNum[i] = 0;
        mConfigTimes[i] = 0;
    }

    mState = HAL_INIT;

    return OK;
}

int CameraHal::deinit()
{
    LOG1("@%s", __func__);
    AutoMutex l(mLock);

    if (--mInitTimes > 0) {
        LOGD("@%s, mInitTimes:%d, return without set state", __func__, mInitTimes);
        return OK;
    }

    MediaControl::getInstance()->clearEntities();

    for (int i = 0; i < MAX_VC_GROUP_NUMBER; i++) {
        mTotalVirtualChannelCamNum[i] = 0;
        mConfigTimes[i] = 0;
    }

    mState = HAL_UNINIT;

    return OK;
}

int CameraHal::deviceOpen(int cameraId, int totalVirtualChannelCamNum)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, camera id:%d, totalVirtualChannelCamNum:%d", __func__, cameraId, totalVirtualChannelCamNum);
    LOG2("SENSORCTRLINFO: channel_id=%d", cameraId);

    AutoMutex l(mLock);
    Check(mState == HAL_UNINIT, NO_INIT,"HAL is not initialized");

    //Create the camera device that will be freed in close
    if (mCameraDevices[cameraId]) {
        LOGD("@%s: open multi times", __func__);
        return INVALID_OPERATION;
    }

    if (mCameraShm.CameraDeviceOpen(cameraId) != OK)
        return INVALID_OPERATION;

    mCameraDevices[cameraId] = new CameraDevice(cameraId);

    camera_info_t info;
    CLEAR(info);
    PlatformData::getCameraInfo(cameraId, info);
    int groupId = info.vc.group >= 0? info.vc.group: 0;
    mTotalVirtualChannelCamNum[groupId] = totalVirtualChannelCamNum;

    // The if check is to handle dual camera cases
    int cameraOpenNum = mCameraShm.cameraDeviceOpenNum();
    Check(cameraOpenNum == 0, INVALID_OPERATION, "camera open num couldn't be 0");
    if (cameraOpenNum == 1) {
        MediaControl::getInstance()->resetAllLinks();

        // VIRTUAL_CHANNEL_S
        if (info.vc.total_num) {
            // when the sensor belongs to virtual channel, reset the routes
            MediaControl::getInstance()->resetAllRoutes(cameraId);
        }
        // VIRTUAL_CHANNEL_E
    }

    return mCameraDevices[cameraId]->init();
}

void CameraHal::deviceClose(int cameraId)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, camera id:%d", __func__, cameraId);
    AutoMutex l(mLock);

    if (mCameraDevices[cameraId]) {
        mCameraDevices[cameraId]->deinit();
        delete mCameraDevices[cameraId];
        mCameraDevices[cameraId] = nullptr;

        mCameraShm.CameraDeviceClose(cameraId);
    }
}

// Assume the inputConfig is already checked in upper layer
int CameraHal::deviceConfigInput(int cameraId, const stream_t *inputConfig)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, camera id:%d", __func__, cameraId);
    AutoMutex lock(mLock);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    device->configureInput(inputConfig);

    return OK;
}

// Assume the streamList is already checked in upper layer
int CameraHal::deviceConfigStreams(int cameraId, stream_config_t *streamList)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, camera id:%d", __func__, cameraId);
    AutoMutex lock(mLock);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    int ret = device->configure(streamList);
    if (ret != OK) {
        LOGE("failed to config streams.");
        return INVALID_OPERATION;
    }

    camera_info_t info;
    CLEAR(info);
    PlatformData::getCameraInfo(cameraId, info);
    int groupId = info.vc.group >= 0? info.vc.group: 0;
    if (mTotalVirtualChannelCamNum[groupId] > 0) {
        mConfigTimes[groupId]++;
        LOG1("@%s, camera id:%d, mConfigTimes:%d, before signal", __func__, cameraId, mConfigTimes[groupId]);
        mVirtualChannelSignal[groupId].signal();
    }

    return ret;
}

int CameraHal::deviceStart(int cameraId)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, cameraId is %d", __func__, cameraId);

    ConditionLock lock(mLock);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    camera_info_t info;
    CLEAR(info);
    PlatformData::getCameraInfo(cameraId, info);
    int groupId = info.vc.group >= 0? info.vc.group: 0;
    LOG1("@%s, cameraId is %d, mConfigTimes:%d, mTotalVirtualChannelCamNum:%d",
        __func__, cameraId, mConfigTimes[groupId], mTotalVirtualChannelCamNum[groupId]);

    if (mTotalVirtualChannelCamNum[groupId] > 0) {
        int timeoutCnt = 10;
        while (mConfigTimes[groupId] < mTotalVirtualChannelCamNum[groupId]) {
            mVirtualChannelSignal[groupId].waitRelative(lock, mWaitDuration);
            LOG1("@%s, cameraId is %d, mConfigTimes:%d, timeoutCnt:%d",
                  __func__, cameraId, mConfigTimes[groupId], timeoutCnt);
            --timeoutCnt;
            Check(!timeoutCnt, TIMED_OUT, "@%s, cameraId is %d, mConfigTimes:%d, wait time out",
                 __func__, cameraId, mConfigTimes[groupId]);
        }
    }

    return device->start();
}

int CameraHal::deviceStop(int cameraId)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, cameraId is %d", __func__, cameraId);

    AutoMutex lock(mLock);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    return device->stop();
}

int CameraHal::deviceAllocateMemory(int cameraId, camera_buffer_t *ubuffer)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, cameraId is %d", __func__, cameraId);

    CameraDevice *device = mCameraDevices[cameraId];

    checkCameraDevice(device, BAD_VALUE);

    return device->allocateMemory(ubuffer);
}

int CameraHal::streamQbuf(int cameraId, camera_buffer_t **ubuffer,
                          int bufferNum, const Parameters* settings)
{
    PERF_CAMERA_ATRACE();
    LOG2("@%s, cameraId is %d, fd:%d", __func__, cameraId, (*ubuffer)->dmafd);

    CameraDevice *device = mCameraDevices[cameraId];

    checkCameraDevice(device, BAD_VALUE);

    return device->qbuf(ubuffer, bufferNum, settings);
}

int CameraHal::streamDqbuf(int cameraId, int streamId, camera_buffer_t **ubuffer,
                           Parameters* settings)
{
    PERF_CAMERA_ATRACE();
    LOG2("@%s, cameraId is %d, streamId is %d", __func__, cameraId, streamId);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    return device->dqbuf(streamId, ubuffer, settings);
}

int CameraHal::getParameters(int cameraId, Parameters& param)
{
    LOG1("@%s, cameraId is %d", __func__, cameraId);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    return device->getParameters(param);
}

int CameraHal::setParameters(int cameraId, const Parameters& param)
{
    LOG1("@%s, cameraId is %d", __func__, cameraId);

    CameraDevice *device = mCameraDevices[cameraId];
    checkCameraDevice(device, BAD_VALUE);

    return device->setParameters(param);
}

} // namespace icamera
