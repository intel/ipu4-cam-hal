/*
 * Copyright (C) 2017-2018 Intel Corporation.
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

#include <queue>

#include "BufferQueue.h"
#include "iutils/RWLock.h"

#include "IspSettings.h"
#include "SensorOB.h"
#include "psysprocessor/PSysDAG.h"

namespace icamera {

class ParameterGenerator;
class PSysDAG;

typedef std::map<Port, shared_ptr<CameraBuffer>> CameraBufferPortMap;
typedef std::map<ConfigMode, shared_ptr<PSysDAG>> PSysDAGConfigModeMap;

/**
  * PSysProcessor runs the Image Process Alogirhtm in the PSYS.
  * It implements the BufferConsumer and BufferProducer Interface
  */
class PSysProcessor: public BufferQueue, public PSysDagCallback {

public:
    PSysProcessor(int cameraId, ParameterGenerator *pGenerator);
    virtual ~PSysProcessor();
    virtual int configure(const vector<ConfigMode>& configModes);
    virtual int setParameters(const Parameters& param);
    virtual int getParameters(Parameters& param);

    virtual int registerUserOutputBufs(Port port, const shared_ptr<CameraBuffer> &camBuffer);

    //Overwrite event source API to delegate related functions
    void registerListener(EventType eventType, EventListener* eventListener);
    void removeListener(EventType eventType, EventListener* eventListener);

    virtual int start();
    virtual void stop();

    // Overwrite PSysDagCallback API, used for returning back buffers from PSysDAG.
    void onFrameDone(const PSysTaskData& result);

private:
    DISALLOW_COPY_AND_ASSIGN(PSysProcessor);

private:
    int processNewFrame();
    shared_ptr<CameraBuffer> allocStatsBuffer(int index);
    int allocPalControlBuffers();

    void dispatchTask(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf);

    void handleEvent(EventData eventData);
    int setVbpToIspParam(long sequence, timeval timestamp);

    long getSettingSequence(const CameraBufferPortMap &outBuf);
    bool needSkipOutputFrame(long sequence);
    bool needExecutePipe(long settingSequence, long inputSequence);
    bool needHoldOnInputFrame(long settingSequence, long inputSequence);
    bool needSwitchPipe(long sequence);

    size_t getRequiredPalBufferSize();
    int fillPalOverrideData(const Parameters& param);
    int fillDefaultAcmData(uint8_t* overrideData);
    void outputRawImage(shared_ptr<CameraBuffer> &srcBuf, shared_ptr<CameraBuffer> &dstBuf);
    int handleMetadata(long sequence, shared_ptr<CameraBuffer> &srcBuf, shared_ptr<CameraBuffer> &dstBuf);

private:
    int mCameraId;
    static const nsecs_t kWaitDuration = 1000000000; //1000ms
    ParameterGenerator *mParameterGenerator;

    IspSettings mIspSettings;
    RWLock mIspSettingsLock;

    //Since the isp settings may be re-used in hdr mode, so the buffer size of
    //isp settings should be equal to frame buffer size.
    static const int IA_PAL_CONTROL_BUFFER_SIZE = 10;

    //Use mUpdatedIspIndex to select the buffer to store the updated param
    //and use mUsedIspIndex to select the buffer to set isp control.
    int mUpdatedIspIndex;
    int mUsedIspIndex;
    ia_binary_data mPalCtrlBuffers[IA_PAL_CONTROL_BUFFER_SIZE];

    Condition mFrameDoneSignal;
    queue<long> mSequenceInflight; // Save the sequences which are being processed.

    vector<ConfigMode> mConfigModes;
    PSysDAGConfigModeMap mPSysDAGs;
    // Active config mode and tuning mode
    ConfigMode mCurConfigMode;
    TuningMode mTuningMode;

    std::queue<EventDataMeta> mMetaQueue;
    //Guard for the metadata queue
    Mutex  mMetaQueueLock;
    Condition mMetaAvailableSignal;

    SensorOB* mSensorOB; //Sensor optical balck handler
    Port mRawPort;

    enum {
        PIPELINE_UNCREATED = 0,
        PIPELINE_CREATED
    } mStatus;
}; // End of class PSysProcessor

} //namespace icamera
