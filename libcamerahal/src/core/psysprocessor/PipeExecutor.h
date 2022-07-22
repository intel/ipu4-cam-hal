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

#include <map>
#include <vector>
#include <memory>
#include <string>
#include <utility> // For std::pair, std::make_pair

#include "Parameters.h"
#include "CameraBuffer.h"
#include "BufferQueue.h"
#include "PSysPipe.h"
#include "PolicyManager.h"
#include "IspParamAdaptor.h"

namespace icamera {

class PSysDAG;

typedef map<Port, shared_ptr<CameraBuffer>> CameraBufferPortMap;

class PipeExecutor : public BufferQueue {
public:
    PipeExecutor(int cameraId, const ExecutorPolicy &policy, vector<string> exclusivePGs,
                 PSysDAG *psysDag, shared_ptr<GraphConfig> gc);
    ~PipeExecutor();

    int start();
    void stop();
    int initPipe();
    void notifyStop();

    int releaseStatsBuffer(const shared_ptr<CameraBuffer> &statsBuf);

    void setStreamId(int streamId) { mStreamId = streamId; }
    void setIspParamAdaptor(IspParamAdaptor* adaptor) { mAdaptor = adaptor; }
    void setPolicyManager(PolicyManager* policyManager) { mPolicyManager = policyManager; }
    void setNotifyPolicy(ExecutorNotifyPolicy notifyPolicy) { mNotifyPolicy = notifyPolicy; }

    void getOutputTerminalPorts(std::map<ia_uid, Port>& outputTerminals) const;
    void getInputTerminalPorts(std::map<ia_uid, Port>& terminals) const;
    bool hasOutputTerminal(ia_uid terminalId);

    // Link output terminals of producer to its input terminals
    int setInputTerminals(const std::map<ia_uid, Port>& terminals);
    int registerOutBuffers(Port port, const shared_ptr<CameraBuffer> &camBuffer);
    int registerInBuffers(Port port, const shared_ptr<CameraBuffer> &inBuf);

    /**
     * Check if the two given stream configs are the same.
     */
    bool isSameStreamConfig(const stream_t& internal, const stream_t& external,
                            ConfigMode configMode, bool checkUsage) const;

    bool isInputEdge() { return mIsInputEdge; }
    bool isOutputEdge() { return mIsOutputEdge; }

    const char* getName() { return mName.c_str(); }

private:
    DISALLOW_COPY_AND_ASSIGN(PipeExecutor);

    int processNewFrame();
    int runPipe(map<Port, shared_ptr<CameraBuffer>> &inBuffers,
                map<Port, shared_ptr<CameraBuffer>> &outBuffers,
                vector<shared_ptr<CameraBuffer>> &outStatsBuffers,
                vector<EventType> &eventType);

    int notifyFrameDone(const v4l2_buffer_t& inV4l2Buf, const CameraBufferPortMap& outBuf);
    int notifyStatsDone(TuningMode tuningMode, const v4l2_buffer_t& inV4l2Buf,
                        const vector<shared_ptr<CameraBuffer>> &outStatsBuffers,
                        const vector<EventType> &eventType);

    int analyzeConnections();
    bool isInputPortUsed(Port port);
    bool isOutputPortUsed(Port port);
    int assignInputPortsForTerminals();
    int assignOutputPortsForTerminals();
    int getStreamByUsage(int usage) const;

    /**
     * Check if there is any valid buffer(not null) in the given port/buffer pairs.
     */
    bool hasValidBuffers(const CameraBufferPortMap& buffers);

    int allocBuffers();
    void releaseBuffers();

private:
    int mCameraId;
    int mStreamId;
    string mName;
    vector<string> mPGs;
    vector<int> mPgIds;
    vector<int> mOpModes;
    vector<int> mCyclicFeedbackRoutine;
    vector<int> mCyclicFeedbackDelay;
    shared_ptr<GraphConfig> mGraphConfig;
    bool mIsInputEdge;
    bool mIsOutputEdge;
    ExecutorNotifyPolicy mNotifyPolicy;

    PSysPipe* mPSysPipe;
    IspParamAdaptor* mAdaptor;
    PolicyManager* mPolicyManager;
    vector<IGraphConfig::PipelineConnection> mConnectionConfigs;

    map<ia_uid, Port> mInputTerminalPortMaps; // <internal uid, port>
                                              // port might be overwritten with
                                              // output port of producer
    map<ia_uid, Port> mOutputTerminalPortMaps;

    // The first uid belongs this object, and the second uid belongs its peer's.
    vector<pair<ia_uid, ia_uid>> mInputTerminalPairs;  // <sink, source>
    vector<pair<ia_uid, ia_uid>> mOutputTerminalPairs; // <source, sink>

    CameraBufQ mStatsBuffers;
    Mutex mStatsBuffersLock;
    vector<string> mExclusivePGs;
    PSysDAG *mPSysDag;
};
}
