/*
 * Copyright (C) 2017-2018 Intel Corporation
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

#include "Parameters.h"
#include "PlatformData.h"
#include "CameraBuffer.h"
#include "IspParamAdaptor.h"
#include "PipeExecutor.h"
#include "PolicyManager.h"

/*************************************************
 * TODO: currently only consider video stream,
 *       will also consider still stream later.
 *************************************************/
#define VIDEO_STREAM_ID 60001

namespace icamera {

/**
 * Encapsulation of all parameters needed by PSysExecutor to run PSYS pipeline.
 */
struct PSysTaskData {
    IspSettings mIspSettings;
    TuningMode mTuningMode;

    CameraBufferPortMap mInputBuffers;
    CameraBufferPortMap mOutputBuffers;
    PSysTaskData() { mTuningMode = TUNING_MODE_MAX; };
};

class PSysDagCallback {
public:
    PSysDagCallback() {};
    virtual ~PSysDagCallback() {};
    virtual void onFrameDone(const PSysTaskData& result) {};
};

class PSysDAG {

public:
    PSysDAG(int cameraId, PSysDagCallback* psysDagCB);
    virtual ~PSysDAG();
    void setFrameInfo(const std::map<Port, stream_t>& inputInfo,
                      const std::map<Port, stream_t>& outputInfo);
    int configure(ConfigMode configMode, TuningMode tuningMode);
    int start();
    int stop();

    int resume();
    int pause();

    int registerInternalBufs(map<Port, CameraBufVector> &internalBufs);
    int registerUserOutputBufs(Port port, const shared_ptr<CameraBuffer> &camBuffer);

    void addTask(PSysTaskData taskParam);
    int getParameters(Parameters& param);

    void registerListener(EventType eventType, EventListener* eventListener);
    void removeListener(EventType eventType, EventListener* eventListener);

    TuningMode getTuningMode(long sequence);
    int prepareIpuParams(long sequence, bool forceUpdate = false);

    /**
     * Use to handle the frame done event from the executors.
     */
    int onFrameDone(Port port, const shared_ptr<CameraBuffer>& buffer);

private:
    DISALLOW_COPY_AND_ASSIGN(PSysDAG);

    void tuningReconfig(TuningMode newTuningMode);

    int createPipeExecutors();
    int linkAndConfigExecutors();
    int bindExternalPortsToExecutor();
    void releasePipeExecutors();

    PipeExecutor* findExecutorProducer(PipeExecutor* consumer);

    int queueBuffers(const PSysTaskData& task);
    int returnBuffers(PSysTaskData& result);

private:
    int mCameraId;
    PSysDagCallback* mPSysDagCB; //Used to callback notify frame done handling
    PolicyManager* mPolicyManager;
    ConfigMode mConfigMode; //It is actually real config mode.
    TuningMode mTuningMode;
    IspParamAdaptor* mIspParamAdaptor;

    std::map<Port, stream_t> mInputFrameInfo;
    std::map<Port, stream_t> mOutputFrameInfo;
    Port mDefaultMainInputPort;

    vector<PipeExecutor*> mExecutorsPool;

    // A lock for protecting task data from being accessed by different threads.
    Mutex mTaskLock;
    // Used to save all on-processing tasks.
    struct TaskInfo {
        TaskInfo() : mNumOfValidBuffers(0), mNumOfReturnedBuffers(0) {}
        PSysTaskData mTaskData;
        int mNumOfValidBuffers;
        int mNumOfReturnedBuffers;
    };
    vector<TaskInfo> mOngoingTasks;

    long mOngoingSequence[MAX_BUFFER_COUNT];
    Mutex mSequenceLock;

    /**
     * The relationship mapping between DAG's port and executors port.
     */
    struct PortMapping {
        PortMapping() : mExecutor(nullptr), mDagPort(INVALID_PORT), mExecutorPort(INVALID_PORT) {}
        PipeExecutor* mExecutor;
        Port mDagPort;
        Port mExecutorPort;
    };

    vector<PortMapping> mInputMaps;
    vector<PortMapping> mOutputMaps;
};
}
