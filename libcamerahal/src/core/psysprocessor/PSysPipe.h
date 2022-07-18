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

extern "C" {
// Imaging Pipe Controller
#include <ia_camera/imaging_pipe_controller.h>

// Pipeline framework (CIPF)
#include <ia_cipf/ia_cipf_pipe.h>
#include <ia_cipf/ia_cipf_stage.h>
#include <ia_cipf/ia_cipf_buffer.h>
#include <ia_cipf/ia_cipf_property.h>
#include <ia_cipf/ia_cipf_terminal.h>
#include <ia_cipf/ia_cipf_iterator.h>

// CIPF backends
#include <ia_cipf_css/ia_cipf_css.h>
#include <ia_cipf_common/ia_cipf_common.h>

// CIPR alloc interface
#include <ia_cipf_css/ia_cipf_css_uids.h>
#include <ia_cipr/ia_cipr_alloc.h>
#include <ia_cipr/ia_cipr_psys_android.h>
#include <ia_cipr/ia_cipr_memory.h>

// Pipeline builder
#include <ia_camera/ia_cipb.h>
}

#include <memory>
#include <map>
#include "Parameters.h"
#include "GraphConfig.h"

#include "IspParamAdaptor.h"
#include "CameraEventType.h"

namespace icamera {

class PSysPipe {
public:
    PSysPipe(int cameraId);
    ~PSysPipe();

    int start();
    void stop();
    int build();
    void setPGIds(vector<int> pgIds) { mPGIds = pgIds; }
    void setStreamId(int streamId) { mStreamId = streamId; }
    int setCyclicFeedbackRoutineMaps(const vector<int>& cyclicFeedbackRoutine);
    int setCyclicFeedbackDelayMaps(const vector<int>& cyclicFeedbackDelay);
    void setConnectionConfig(const GraphConfig::ConnectionConfig &cc) { mConnectionConfig.push_back(cc); }
    void amendEdgeConnectionInfo(GraphConfig::ConnectionConfig &aConnInfo);
    int setStageRbm(ia_uid stageUid, GraphConfig::StageAttr stageAttr);
    int setStageProperty(ia_uid stageUid, ia_uid propertyUid, uint32_t value);
    void disableTerminal(uint32_t terminalId) { mDisableTerminal.push_back(terminalId); }
    int prepare(shared_ptr<GraphConfig> graphConfig, IspParamAdaptor* adaptor);
    int getStatsBufferCount();  // Call sequence is after "prepare" member function
    void setTerminalConfig(const GraphConfig::PortFormatSettings &format);
    int setPsysBuffer(ia_uid uid, const shared_ptr<CameraBuffer> &camBuffer);
    void setExclusive(bool isExclusive) { mIsExclusive = isExclusive; };
    int registerBuffers(void);
    int iterate(std::vector<shared_ptr<CameraBuffer>> & outStatsBuffers, std::vector<EventType> & eventType,
                long inputSequence = -1, IspParamAdaptor* adaptor = nullptr);
    int destroyPipeline(void);

private:
    DISALLOW_COPY_AND_ASSIGN(PSysPipe);

private:
    void clearRegistedBuffers();
    void addDecodeStage(shared_ptr<GraphConfig> &graphConfig, int32_t kernelId);
    int configureTerminals(void);
    int setDisableProperty(uint32_t terminalId);
    int identifyProperties(IspParamAdaptor* adaptor);
    int prepareStage(imaging_pipe_ctrl_t *ctrl, ia_uid stageUid, const ia_binary_data *ipuParameters);
    int bufferRequirements(bool realTerminals = false);
    int handleBufferRequirement(ia_cipf_buffer_t *reqBuffer, bool realTerminals = false);
    uint32_t getPayloadSize(ia_cipf_buffer_t *buffer, ia_cipf_frame_format_t *format);
    ia_cipf_buffer_t *allocateFrameBuffer(ia_cipf_buffer_t *reqBuffer);
    int allocateParamBuffer(ia_cipf_buffer_t *reqBuffer);
    ia_cipf_buffer_t *createCipfBufCopy(ia_cipf_buffer_t *reqBuffer,
                                        const shared_ptr<CameraBuffer> &halBuffer);
    void enableConcurrency(bool enable);
    void releaseConcurrency();
    void dumpIntermFrames(unsigned int sequence);

    int handleSisStats(ia_cipf_buffer_t* ia_buffer, const shared_ptr<CameraBuffer> &outStatsBuffers);
private:
    ia_cipf_pipe_t *mPipe;
    ia_cipf_iterator_t *mPipeIterator;
    int mParamBufferSize;
    ia_cipb_t mBuilder;
    vector<GraphConfig::ConnectionConfig> mConnectionConfig;
    vector<uint32_t> mDisableTerminal;
    imaging_pipe_ctrl_t* mPipeCtrl;

    map<ia_uid, ia_cipf_buffer_t*> mParamBuffs; // the allocated buffers for parameter
    map<ia_uid, ia_cipf_frame_format_t> mTermConfigMap; // set in setTerminalConfig(), uid is one of ia_cipf_external_source_uid and ia_cipf_external_sink_uid
    map<ia_uid, ia_cipf_buffer_t*> mTermBufferMap; // the uid is the terminal uid, buffer includes internal frame buffer, it's filled in handleBufferRequirement()

    // Used to save and release the terminal buffers which are shadowed.
    // This case happens when a PG is in coupled-relay mode, there are two buffers for the same
    // terminal uid. So the first buffer will be shadowed by the second one. And then the pointer
    // of the first is never released.
    vector<ia_cipf_buffer_t*> mShadowedTermBuffer;

    /**
     * regHalBuf
     * Struct to hold the cipf buffer copy created for a hal buffer. When
     * registering a hal buffer a cipf buffer copy is created and memory of
     * the hal buffer is used as the memory of the cipf buffer
     */
    typedef struct {
        ia_cipf_buffer_t* cipfBuf;
        union {
            void* halBuffer;
            int fd;
        } data;
    }  RegHalBuf;
    map<ia_uid, vector<RegHalBuf> > mRegisteredHalBufs; // hal buffers with corresponding cipf buffer type registered to a terminal, it's filled in registerBuffers()

    map<ia_uid, shared_ptr<CameraBuffer> > mPsysBuffers; // the input and output buffers which is set in setPsysBuffer(), uid is one of ia_cipf_external_source_uid and ia_cipf_external_sink_uid
    shared_ptr<CameraBuffer> mParamBuffer;
    std::map<int32_t, ia_uid> mStageUidsWithStats; /**< list of stages in the pipe that produce statistics.*/
    vector<int> mPGIds;
    vector<pair<ia_uid, int>> mCyclicFeedbackRoutinePairs; // stage id and cyclic_feedback_routine
    vector<pair<ia_uid, int>> mCyclicFeedbackDelayPairs; // stage id and cyclic_feedback_delay
    bool mIsExclusive;
    bool mDecodeStagesReady;
    int mCameraId;
    int mStreamId;

    // For IPU FW concurrency exclusive control
    static FILE* mFwConcurFile;
    static int mFwConcurDisableCnt;
    static Mutex mPipeMutex;
};

}  // namespace icamera
