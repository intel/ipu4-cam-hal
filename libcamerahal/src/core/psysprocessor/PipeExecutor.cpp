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

#define LOG_TAG "PipeExecutor"

#include "PipeExecutor.h"
#include "PSysDAG.h"

#include "FormatUtils.h"
#include "iutils/CameraDump.h"
#include "../SyncManager.h"

#include <algorithm>


namespace icamera {

PipeExecutor::PipeExecutor(int cameraId, const ExecutorPolicy &policy,
                           vector<string> exclusivePGs, PSysDAG *psysDag, shared_ptr<GraphConfig> gc)
      : mCameraId(cameraId),
        mStreamId(-1),
        mName(policy.exeName),
        mPGs(policy.pgList),
        mOpModes(policy.opModeList),
        mCyclicFeedbackRoutine(policy.cyclicFeedbackRoutineList),
        mCyclicFeedbackDelay(policy.cyclicFeedbackDelayList),
        mGraphConfig(gc),
        mIsInputEdge(false),
        mIsOutputEdge(false),
        mNotifyPolicy(POLICY_FRAME_FIRST),
        mAdaptor(nullptr),
        mPolicyManager(nullptr),
        mExclusivePGs(exclusivePGs),
        mPSysDag(psysDag)
{
    mPSysPipe = new PSysPipe(cameraId);
    mProcessThread = new ProcessThread(this);
}

PipeExecutor::~PipeExecutor()
{
    releaseBuffers();
    delete mProcessThread;
    delete mPSysPipe;
}

int PipeExecutor::initPipe()
{
    GraphConfig::NodesPtrVector programGroups;
    vector<IGraphConfig::PipelineConnection> connVector;
    Check(mGraphConfig == nullptr, BAD_VALUE, "%s, the graph config is NULL, BUG!", __func__);

    int ret = mGraphConfig->pipelineGetInternalConnections(mPGs, connVector);
    Check(ret != OK || connVector.empty(), ret,
            "Failed to get connections for executor:%s", mName.c_str());

    mConnectionConfigs = connVector;
    ret = analyzeConnections();
    Check(ret != OK, ret, "Failed to analyze connections with: %d for executor: %s", ret, mName.c_str());

    assignInputPortsForTerminals();
    assignOutputPortsForTerminals();

    mPSysPipe->setPGIds(mPgIds);
    mPSysPipe->setStreamId(mStreamId);
    mPSysPipe->setCyclicFeedbackRoutineMaps(mCyclicFeedbackRoutine);
    mPSysPipe->setCyclicFeedbackDelayMaps(mCyclicFeedbackDelay);

    ia_uid inputStage = psys_2600_pg_uid(mPgIds.front());
    ia_uid outputStage = psys_2600_pg_uid(mPgIds.back());
    LOG1("%s, the inputStage: %d,  outputStage: %d", __func__, inputStage, outputStage);

    /*
     * Note: we must be ensure the order in connection vector which gotten
     * from graph config xml when there are multi output/input ports in
     * one executor.
     */
    for (auto connection : connVector) {
        if (connection.portFormatSettings.enabled == 0) {
            // Port is disabled, there is no need to setup the connection.
            mPSysPipe->disableTerminal(connection.portFormatSettings.terminalId);
            continue;
        }

        LOG1("%s: executor:%s edge:%d %d-%d -> %d-%d", __func__, mName.c_str(), connection.hasEdgePort,
            connection.connectionConfig.mSourceTerminal, connection.connectionConfig.mSourceStage,
            connection.connectionConfig.mSinkTerminal, connection.connectionConfig.mSinkStage);

        LOG1("%s: executor:%s terminalId:%d (%dx%d) bpp:%d bpl:%d", __func__, mName.c_str(),
            connection.portFormatSettings.terminalId,
            connection.portFormatSettings.width, connection.portFormatSettings.height,
            connection.portFormatSettings.bpp, connection.portFormatSettings.bpl);

        // Re-set mSourceStage for the input port of first pg.
        if (connection.connectionConfig.mSinkStage == inputStage)
            connection.connectionConfig.mSourceStage = 0;

        // Re-set mSinkStage for the output port of last pg.
        if (connection.connectionConfig.mSourceStage == outputStage)
            connection.connectionConfig.mSinkStage = 0;

        mPSysPipe->setTerminalConfig(connection.portFormatSettings);

        if (connection.hasEdgePort)
            mPSysPipe->amendEdgeConnectionInfo(connection.connectionConfig);

        mPSysPipe->setConnectionConfig(connection.connectionConfig);
    }

    /*
     * Ready to build and prepare the pipeline
     */
    ret = mPSysPipe->build();
    Check(ret != OK, ret, "Failed to build the pipe for stream for executor: %s", mName.c_str());

    ret = mGraphConfig->getProgramGroupsByName(mPGs, programGroups);
    Check((ret != OK || programGroups.empty()), BAD_VALUE,
            "No Program groups associated for executor: %s", mName.c_str());

    /*
     * For each PG, check related operation mode, if a valid operation mode has
     * been set in policy profile, then set it to pipe. One PG may have or have
     * no related operation mode in policy profile, check operation mode list for
     * each PG according to op_modes attribute's sequence in policy profile.
     */
    for (unsigned int policyIdx = 0; policyIdx < mPgIds.size(); policyIdx++){
        if (policyIdx >= mOpModes.size()) {
            break;
        }
        if (mOpModes[policyIdx] <= 0) continue; // means operation mode not set for this PG
        LOG1("%s: set operation mode %d for PG %d", __func__, mOpModes[policyIdx], mPgIds[policyIdx]);
        ret = mPSysPipe->setStageProperty(psys_2600_pg_uid(mPgIds[policyIdx]),
                                          psys_stage_operation_mode_uid,
                                          mOpModes[policyIdx]);
        if (ret != OK) {
            LOGW("Failed to set operation mode for executor %s", mName.c_str());
        }
    }

    /* Handle CIPF attributes from GraphConfig into the pipeline */
    for (auto pg : programGroups) {
        int pgId = -1, attValue = 0;

        Check(pg == nullptr, UNKNOWN_ERROR, "%s, The pg node is NULL - BUG", __func__);
        const GCSS::IGraphConfig* gc = mGraphConfig->getInterface(pg);
        Check(gc == nullptr, UNKNOWN_ERROR, "%s, Failed to get graph config interface - BUG", __func__);
        css_err_t val = gc->getValue(GCSS_KEY_PG_ID, pgId);
        if (val != css_err_none) {
            /* No PG ID */
            continue;
        }

        gc = gc->getDescendant(GCSS_KEY_CIPF);
        if (gc == nullptr) {
            /* No attributes to CIPF */
            continue;
        }

        GraphConfig::StageAttr stageAttr;
        if (mGraphConfig->getPgRbmValue(gc, &stageAttr) == OK) {
            ret = mPSysPipe->setStageRbm(psys_2600_pg_uid(pgId), stageAttr);
            Check(ret != OK, ret, "%s, Failed to set the stage rbm", __func__);
        }

        /* Set CIPF specific attributes via pipeline properties */
        val = gc->getValue(GCSS_KEY_PSYS_FREQ, attValue);
        if (val == css_err_none) {
            ret = mPSysPipe->setStageProperty(psys_2600_pg_uid(pgId),
                                              psys_command_psys_frequency_uid,
                                              (uint32_t) attValue);
            Check(ret != OK, ret, "%s, Failed to set the frequency count for PSYS stage", __func__);
        }

        val = gc->getValue(GCSS_KEY_FRAGMENT_COUNT, attValue);
        if (val == css_err_none) {
            ret = mPSysPipe->setStageProperty(psys_2600_pg_uid(pgId),
                                              css_fragment_count_uid,
                                              (uint32_t) attValue);
            Check(ret != OK, ret, "%s, Failed to set the fragment count for PSYS stage", __func__);
        }
    }

    ret = mPSysPipe->prepare(mGraphConfig, mAdaptor);
    Check(ret != OK, ret, "%s, Failed to prepare the pipe for executor: %s", __func__, mName.c_str());

    // Set exclusive or not for PSysPipe.
    bool isExclusivePipe = false;
    for (auto const& pipePG : mPGs) {
        for (auto const& exclusivePG : mExclusivePGs) {
            if (pipePG.compare(exclusivePG) == 0) {
                isExclusivePipe = true;
                break;
            }
        }
        if (isExclusivePipe) break;
    }
    LOG1("%s: executor: %s exclusive flag: %d", __func__, mName.c_str(), isExclusivePipe);
    mPSysPipe->setExclusive(isExclusivePipe);

    /*
     * TODO: configure DVS zoom.
     */

    return OK;
}

void PipeExecutor::getOutputTerminalPorts(std::map<ia_uid, Port>& terminals) const
{
    terminals = mOutputTerminalPortMaps;
}

void PipeExecutor::getInputTerminalPorts(std::map<ia_uid, Port>& terminals) const
{
    terminals = mInputTerminalPortMaps;
}

int PipeExecutor::setInputTerminals(const std::map<ia_uid, Port>& inputTerminals)
{
    if (!isInputEdge()) {
        // Overwrite input ports with producer's output ports
        mInputTerminalPortMaps.clear();
        for (auto const& external : inputTerminals) {
            for (auto const& internal : mInputTerminalPairs) {
                // Consumer.source = producer.source
                if (internal.second == external.first) {
                    mInputTerminalPortMaps[internal.first] = external.second;
                    break;
                }
            }
        }
    }

    Check(mInputTerminalPortMaps.size() != mInputTerminalPairs.size(), BAD_VALUE,
                "Ports of producer & own (%s) terminals mismatch!", getName());

    // Get stream configuration for all terminals
    std::map<Port, stream_t> outputInfo;
    std::map<Port, stream_t> inputInfo;

    for (auto terminalPort : mOutputTerminalPortMaps) {
        for (auto connection : mConnectionConfigs) {
            if (connection.connectionConfig.mSourceTerminal == terminalPort.first) {
                stream_t outputConfig;
                CLEAR(outputConfig);

                outputConfig.width = connection.portFormatSettings.width;
                outputConfig.height = connection.portFormatSettings.height;
                outputConfig.format = connection.portFormatSettings.fourcc;
                outputInfo[terminalPort.second] = outputConfig;
                break;
            }
        }
    }
    Check(outputInfo.size() != mOutputTerminalPortMaps.size(),
                BAD_VALUE, "Output ports & streams mismatch!");

    for (auto terminalPort : mInputTerminalPortMaps) {
        for (auto connection : mConnectionConfigs) {
            if (connection.connectionConfig.mSinkTerminal == terminalPort.first) {
                stream_t inputConfig;
                CLEAR(inputConfig);

                inputConfig.width = connection.portFormatSettings.width;
                inputConfig.height = connection.portFormatSettings.height;
                inputConfig.format = connection.portFormatSettings.fourcc;
                inputInfo[terminalPort.second] = inputConfig;
                break;
            }
        }
    }
    Check(inputInfo.size() != mInputTerminalPortMaps.size(),
                BAD_VALUE, "Input ports & streams mismatch!");

    BufferQueue::setFrameInfo(inputInfo, outputInfo);
    return OK;
}

int PipeExecutor::start()
{
    LOG1("%s executor:%s", __func__, mName.c_str());
    AutoMutex   l(mBufferQueueLock);

    allocBuffers();
    mPSysPipe->start();

    mThreadRunning = true;
    mProcessThread->run(mName.c_str(), PRIORITY_NORMAL);

    return OK;
}

void PipeExecutor::stop()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    mProcessThread->requestExitAndWait();

    // Thread is not running. It is safe to clear the Queue
    clearBufferQueues();
    mPSysPipe->stop();
}

void PipeExecutor::notifyStop()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    mProcessThread->requestExit();
    {
        AutoMutex l(mBufferQueueLock);
        mThreadRunning = false;
        // Wakeup the thread to exit
        mFrameAvailableSignal.signal();
        mOutputAvailableSignal.signal();
    }
}

int PipeExecutor::releaseStatsBuffer(const shared_ptr<CameraBuffer> &statsBuf)
{
    LOG3A("%s executor:%s", __func__, mName.c_str());
    AutoMutex lock(mStatsBuffersLock);

    mStatsBuffers.push(statsBuf);

    return OK;
}

bool PipeExecutor::hasOutputTerminal(ia_uid terminalId)
{
    for (auto& item : mOutputTerminalPairs) {
        if (item.second == terminalId) {
            return true;
        }
    }
    return false;
}

int PipeExecutor::getStreamByUsage(int usage) const
{
    int streamId = VIDEO_STREAM_ID;
    switch (usage) {
        case CAMERA_STREAM_STILL_CAPTURE:
            streamId = STILL_STREAM_ID;
            break;
        case CAMERA_STREAM_PREVIEW:
        case CAMERA_STREAM_VIDEO_CAPTURE:
        default:
            streamId = VIDEO_STREAM_ID;
            break;
    }

    return streamId;
}

bool PipeExecutor::isSameStreamConfig(const stream_t& internal, const stream_t& external,
                                      ConfigMode configMode, bool checkUsage) const
{
    /**
     * The internal format is ia_fourcc based format, so need to convert it to V4L2 format.
     */
    int internalFormat = graphconfig::utils::getV4L2Format(internal.format);

    LOG1("%s: executor:%s, stream id:%d, internal fmt:%s(%dx%d), external fmt:%s(%dx%d) usage:%d",
          __func__, mName.c_str(), mStreamId,
          CameraUtils::format2string(internalFormat), internal.width, internal.height,
          CameraUtils::format2string(external.format), external.width, external.height,
          external.usage);

    // Check if the stream usage matches with stream id. Now only check the output pipe executor
    if (checkUsage) {
        int streamId = getStreamByUsage(external.usage);
        if (streamId != mStreamId)
            return false;
    }

    /*
     * WA: PG accept GRBG format but actual input data is of RGGB format,
     *     PG use its kernel to crop to GRBG
     */
    if ((internalFormat == V4L2_PIX_FMT_SGRBG10 || internalFormat == V4L2_PIX_FMT_SGRBG12)
         && (external.format == V4L2_PIX_FMT_SRGGB10 || external.format == V4L2_PIX_FMT_SRGGB12)) {
         return true;
    }

    /*
     * WA: For some sensor setting, the output format is RAW10/VEC_RAW10,
     * but low latency PG only supports VEC_RAW12 input.
     * Now regard them as same format, and revert it after the format is
     * supported in low latency PG.
     */
    if ((configMode == CAMERA_STREAM_CONFIGURATION_MODE_VIDEO_LL ||
         configMode == CAMERA_STREAM_CONFIGURATION_MODE_ULL) &&
        (internalFormat == V4L2_PIX_FMT_SGRBG12V32 &&
         external.format == V4L2_PIX_FMT_SGRBG10V32)) {
        return true;
    }

    bool sameHeight = internal.height == external.height ||
                      internal.height == ALIGN_32(external.height);
    if (internalFormat == external.format && internal.width == external.width && sameHeight) {
        return true;
    }

    return false;
}

/**
 * Check if there is any valid buffer(not null) in the given port/buffer pairs.
 *
 * return true if there is at least one not null buffer.
 */
bool PipeExecutor::hasValidBuffers(const CameraBufferPortMap& buffers)
{
    for (const auto& item : buffers) {
        if (item.second) return true;
    }

    return false;
}

int PipeExecutor::processNewFrame()
{
    PERF_CAMERA_ATRACE();

    LOG2("%s executor:%s", __func__, mName.c_str());

    int ret = OK;
    CameraBufferPortMap inBuffers, outBuffers;
    // Wait frame buffers.
    {
        ConditionLock lock(mBufferQueueLock);
        ret = waitFreeBuffersInQueue(lock, inBuffers, outBuffers);
        // Already stopped
        if (!mThreadRunning) return -1;

        if (ret != OK) return OK; // Wait frame buffer error should not involve thread exit.

        Check(inBuffers.empty() || outBuffers.empty(),
              UNKNOWN_ERROR, "Failed to get input or output buffers.");

        for (auto& output: mOutputQueue) {
            output.second.pop();
        }

        for (auto& input: mInputQueue) {
            input.second.pop();
        }
    }

    // Check if the executor needs to run the actual pipeline.
    // It only needs to run when there is at least one valid output buffer.
    if (!hasValidBuffers(outBuffers)) {
        // Return buffers if the executor is NOT an input edge.
        if (!mIsInputEdge) {
            for (const auto& item : inBuffers) {
                mBufferProducer->qbuf(item.first, item.second);
            }
        }
        return OK;
    }

    vector<shared_ptr<CameraBuffer>> outStatsBuffers;
    vector<EventType> eventType;
    // Get next available stats buffers.
    {
        AutoMutex l(mStatsBuffersLock);
        int statsBufferCount = mPSysPipe->getStatsBufferCount();
        Check((statsBufferCount < 0), UNKNOWN_ERROR, "Error in getting stats buffer count");

        for (int counter = 0; counter < statsBufferCount; counter++) {
            if (mStatsBuffers.empty()) {
                LOGW("No available stats buffer.");
                break;
            }
            outStatsBuffers.push_back(mStatsBuffers.front());
            mStatsBuffers.pop();
        }
    }

    // Should find first not none input buffer instead of always use the first one.
    shared_ptr<CameraBuffer> inBuf = inBuffers.begin()->second;
    Check(!inBuf, UNKNOWN_ERROR, "@%s: no valid input buffer", __func__);
    long inBufSequence = inBuf->getSequence();
    v4l2_buffer_t inV4l2Buf = inBuf->getV4l2Buffer();
    TuningMode tuningMode = mPSysDag->getTuningMode(inBufSequence);

    // Prepare the ipu parameters before run pipe
    // TODO remove it when 4k ull pipe run faster
    if ((tuningMode == TUNING_MODE_VIDEO_HDR) || (tuningMode == TUNING_MODE_VIDEO_HDR2)) {
        mPSysDag->prepareIpuParams(inBufSequence);
    }

    LOG2("%s:Id:%d run pipe start for buffer:%ld", mName.c_str(), mCameraId, inBufSequence);

    if (PlatformData::isEnableFrameSyncCheck(mCameraId)) {
        shared_ptr<CameraBuffer> cInBuffer = inBuffers[MAIN_PORT];
        int vc = cInBuffer->getVirtualChannel();

        while ((!SyncManager::getInstance()->vcSynced(vc)) && mThreadRunning)
            usleep(1);

        if (gLogLevel & CAMERA_DEBUG_LOG_VC_SYNC) {
            int seq = cInBuffer->getSequence();
            SyncManager::getInstance()->printVcSyncCount();
            LOGVCSYNC("[start runPipe], CPU-timestamp:%lu, sequence:%d, vc:%d, kernel-timestamp:%.3lfms, endl",
                    CameraUtils::systemTime(),
                    seq,
                    cInBuffer->getVirtualChannel(),
                    cInBuffer->getTimestamp().tv_sec*1000.0 + cInBuffer->getTimestamp().tv_usec/1000.0);
        }

        SyncManager::getInstance()->updateVcSyncCount(vc);

        // Run pipe with buffers
        ret = runPipe(inBuffers, outBuffers, outStatsBuffers, eventType);
        LOGVCSYNC("[done runPipe], CPU-timestamp:%lu, sequence:%ld, vc:%d, kernel-timestamp:%.3lfms, endl",
              CameraUtils::systemTime(),
              cInBuffer->getSequence(),
              cInBuffer->getVirtualChannel(),
              cInBuffer->getTimestamp().tv_sec*1000.0 + cInBuffer->getTimestamp().tv_usec/1000.0);
    } else
        // Run pipe with buffers
        ret = runPipe(inBuffers, outBuffers, outStatsBuffers, eventType);

    Check((ret != OK), UNKNOWN_ERROR, "@%s: failed to run pipe", __func__);
    LOG2("%s:Id:%d run pipe end for buffer:%ld", mName.c_str(), mCameraId, inBufSequence);

    if (mNotifyPolicy == POLICY_FRAME_FIRST) {
        // For general case, notify frame prior to stats to make sure its consumers can get
        // the frame buffers as early as possible.
        notifyFrameDone(inV4l2Buf, outBuffers);
        notifyStatsDone(tuningMode, inV4l2Buf, outStatsBuffers, eventType);
    } else if (mNotifyPolicy == POLICY_STATS_FIRST) {
        // Notify stats first and then handle frame buffers to make sure the next executor
        // can get this executor's IQ result.
        notifyStatsDone(tuningMode, inV4l2Buf, outStatsBuffers, eventType);

        // After the stats notified, we need to update the IPU parameters as well to get the
        // latest AIQ result.
        mPSysDag->prepareIpuParams(inBufSequence, true);

        notifyFrameDone(inV4l2Buf, outBuffers);
    } else {
        LOGW("Invalid notify policy:%d, should never happen.", mNotifyPolicy);
    }

    // Return buffers for the executor which is NOT an input edge
    if (!mIsInputEdge) {
        for (auto const& portBufferPair : inBuffers) {
            // Queue buffer to producer
            mBufferProducer->qbuf(portBufferPair.first, portBufferPair.second);
        }
    }

    return OK;
}

int PipeExecutor::registerInBuffers(Port port, const shared_ptr<CameraBuffer> &inBuf)
{
    for (auto const& inputUidPortPair : mInputTerminalPortMaps) {
        if (inputUidPortPair.second == port) {
            mPSysPipe->setPsysBuffer(inputUidPortPair.first, inBuf);
            mPSysPipe->registerBuffers();
            break;
        }
    }

    return OK;
}

int PipeExecutor::registerOutBuffers(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    for (auto const& outputUidPortPair : mOutputTerminalPortMaps) {
        if (outputUidPortPair.second == port) {
            mPSysPipe->setPsysBuffer(outputUidPortPair.first, camBuffer);
            mPSysPipe->registerBuffers();
            break;
        }
    }

    return OK;
}

int PipeExecutor::runPipe(map<Port, shared_ptr<CameraBuffer>> &inBuffers,
                          map<Port, shared_ptr<CameraBuffer>> &outBuffers,
                          vector<shared_ptr<CameraBuffer>> &outStatsBuffers,
                          vector<EventType> &eventType)
{
    PERF_CAMERA_ATRACE();

    LOG2("%s: Executor %s run with input: %zu, output: %zu",
         __func__, mName.c_str(), inBuffers.size(), outBuffers.size());

    Check((inBuffers.empty() || outBuffers.empty()), BAD_VALUE,
          "Error in pipe iteration input/output bufs");

    for (auto const& inputUidPortPair : mInputTerminalPortMaps) {
        if (inBuffers.find(inputUidPortPair.second) != inBuffers.end()
            && inBuffers[inputUidPortPair.second]) {
            mPSysPipe->setPsysBuffer(inputUidPortPair.first, inBuffers[inputUidPortPair.second]);
        }
    }

    for (auto const& outputUidPortPair : mOutputTerminalPortMaps) {
        if (outBuffers.find(outputUidPortPair.second) != outBuffers.end()
            && outBuffers[outputUidPortPair.second]) {
            mPSysPipe->setPsysBuffer(outputUidPortPair.first, outBuffers[outputUidPortPair.second]);
        }
    }

    int ret = mPSysPipe->registerBuffers();
    Check(ret != OK, ret, "%s: failed to register buffer to pipe with %d", mName.c_str(), ret);

    long sequence = inBuffers.begin()->second ? inBuffers.begin()->second->getSequence() : -1;

    if (mPolicyManager) {
        // Check if need to wait other executors.
        ret = mPolicyManager->wait(mName);
    }

    ret = mPSysPipe->iterate(outStatsBuffers, eventType, sequence, mAdaptor);
    Check((ret != OK), ret, "%s: error in pipe iteration with %d", mName.c_str(), ret);

    return OK;
}

int PipeExecutor::notifyFrameDone(const v4l2_buffer_t& inV4l2Buf, const CameraBufferPortMap& outBuf)
{
    PERF_CAMERA_ATRACE();

    for (auto const& portBufferPair : outBuf) {
        shared_ptr<CameraBuffer> outBuf = portBufferPair.second;
        Port port = portBufferPair.first;
        // If the output buffer is nullptr, that means user doesn't request that buffer,
        // so it doesn't need to be handled here.
        if (!outBuf) continue;

        outBuf->updateV4l2Buffer(inV4l2Buf);

        // If it's output edge, the buffer should be returned to PSysDag,
        // otherwise they should be returned to its consumer.
        if (mIsOutputEdge) {
            mPSysDag->onFrameDone(port, outBuf);
        } else {
            if (CameraDump::isDumpTypeEnable(DUMP_EXECUTOR_OUTPUT)) {
                CameraDump::dumpImage(mCameraId, outBuf, M_PSYS);
            }

            for (auto &it : mBufferConsumerList) {
                it->onFrameAvailable(port, outBuf);
            }
        }
    }

    return OK;
}

int PipeExecutor::notifyStatsDone(TuningMode tuningMode,
                                  const v4l2_buffer_t& inV4l2Buf,
                                  const vector<shared_ptr<CameraBuffer>> &outStatsBuffers,
                                  const vector<EventType> &eventType)
{
    PERF_CAMERA_ATRACE();

    // The executor does not produce stats, so no need to notify.
    if (outStatsBuffers.empty()) return OK;

    int statsIndex = 0;
    // Notify PSYS statistics to listeners.
    for (auto statsBuf : outStatsBuffers) {
        if (!statsBuf) continue;

        ia_binary_data *hwStatsData = (ia_binary_data *)(statsBuf->getBufferAddr());
        if (hwStatsData->data == nullptr || hwStatsData->size == 0) {
            LOGW("%s: No statistics data in buffer", __func__);
            releaseStatsBuffer(statsBuf);
            continue;
        }

        statsBuf->updateV4l2Buffer(inV4l2Buf);

        // Decode the statistics data
        if (eventType[statsIndex] == EVENT_PSYS_STATS_BUF_READY) {
            mAdaptor->decodeStatsData(tuningMode, statsBuf, mGraphConfig);
        }

        EventDataStatsReady statsReadyData;
        statsReadyData.sequence = statsBuf->getSequence();
        statsReadyData.timestamp.tv_sec = statsBuf->getTimestamp().tv_sec;
        statsReadyData.timestamp.tv_usec = statsBuf->getTimestamp().tv_usec;

        EventData eventData;
        eventData.type = eventType[statsIndex];
        eventData.buffer = statsBuf;
        eventData.data.statsReady = statsReadyData;

        notifyListeners(eventData);

        releaseStatsBuffer(statsBuf);
        statsIndex++;
    }

    return OK;
}

/**
 * Analyze the connection config and parse it to input and output terminal pairs.
 */
int PipeExecutor::analyzeConnections()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    Check(mPGs.empty(), INVALID_OPERATION, "No available PG names");

    for (auto const& pgName : mPGs) {
        int pgId = mGraphConfig->getPgIdByPgName(pgName);
        Check(pgId == -1, BAD_VALUE, "Cannot get PG ID for %s", pgName.c_str());
        LOG1("%s: executor:%s pg name:%s pg id:%d", __func__, mName.c_str(), pgName.c_str(), pgId);
        mPgIds.push_back(pgId);
    }

    int firstPgId = mPgIds.front();
    int lastPgId = mPgIds.back();

    ia_uid firstStageId = psys_2600_pg_uid(firstPgId);
    ia_uid lastStageId = psys_2600_pg_uid(lastPgId);

    mInputTerminalPairs.clear();
    mOutputTerminalPairs.clear();
    mOutputTerminalPortMaps.clear();
    mInputTerminalPortMaps.clear();

    for (auto const& connection : mConnectionConfigs) {
        if (connection.portFormatSettings.enabled == 0) {
            // No actions are needed for the disabled connections.
            continue;
        }

        pair<ia_uid, ia_uid> terminalPair;
        // First fill the input terminal pairs.
        // If the connection's sink stage is same as the first stage/pg id in this executor,
        // then it means the connection belongs to input terminal pairs.
        if (connection.connectionConfig.mSinkStage == firstStageId) {
            terminalPair.first = connection.connectionConfig.mSinkTerminal;
            terminalPair.second = connection.connectionConfig.mSourceTerminal;
            mInputTerminalPairs.push_back(terminalPair);

            if (connection.hasEdgePort) {
                mIsInputEdge = true;
            }
        }

        // Then fill the output terminal pairs.
        // If the connection's source stage is same as the last stage/pg id in this executor,
        // then it means the connection belongs to output terminal pairs.
        if (connection.connectionConfig.mSourceStage == lastStageId) {
            terminalPair.first = connection.connectionConfig.mSourceTerminal;
            terminalPair.second = connection.connectionConfig.mSinkTerminal;
            mOutputTerminalPairs.push_back(terminalPair);

            if (connection.hasEdgePort) {
                mIsOutputEdge = true;
            }
        }
    }

    LOG1("%s: executor:%s inputEdge:%d outputEdge:%d", __func__, mName.c_str(), mIsInputEdge, mIsOutputEdge);

    for (auto terminalPair : mInputTerminalPairs) {
        LOG1("%s: executor:%s input pairs (%d->%d)",
             __func__, mName.c_str(), terminalPair.first, terminalPair.second);
    }

    for (auto terminalPair : mOutputTerminalPairs) {
        LOG1("%s: executor:%s output pairs (%d->%d)",
             __func__, mName.c_str(), terminalPair.first, terminalPair.second);
    }

    Check(mInputTerminalPairs.empty() || mOutputTerminalPairs.empty(), BAD_VALUE,
          "Not valid input or output terminal pairs for executor:%s", mName.c_str());
    Check(mInputTerminalPairs.size() > INVALID_PORT, BAD_VALUE, "%s: too many input!", getName());
    Check(mOutputTerminalPairs.size() > INVALID_PORT, BAD_VALUE, "%s: too many output!", getName());

    return OK;
}

/**
 * Check if the port has already been assigned to a terminal.
 */
bool PipeExecutor::isInputPortUsed(Port port)
{
    for (auto const& terminalPortPair : mInputTerminalPortMaps) {
        if (terminalPortPair.second == port) {
            return true;
        }
    }
    return false;
}

/**
 * Check if the port has already been assigned to a terminal.
 */
bool PipeExecutor::isOutputPortUsed(Port port)
{
    for (auto const& terminalPortPair : mOutputTerminalPortMaps) {
        if (terminalPortPair.second == port) {
            return true;
        }
    }
    return false;
}

/**
 * Assign input ports for input terminals as internal default value
 * Ports may be overwritten with output ports of producer in setInputTerminals()
 */
int PipeExecutor::assignInputPortsForTerminals()
{
    for (auto const& inputTerminalPair : mInputTerminalPairs) {
        Port availablePort = INVALID_PORT;
        // Find the first not used port and assign it to the terminal.
        if (!isInputPortUsed(MAIN_PORT)) {
            availablePort = MAIN_PORT;
        } else if (!isInputPortUsed(SECOND_PORT)) {
            availablePort = SECOND_PORT;
        } else if (!isInputPortUsed(THIRD_PORT)) {
            availablePort = THIRD_PORT;
        }
        Check(availablePort == INVALID_PORT, INVALID_OPERATION, "No output port available");

        mInputTerminalPortMaps[inputTerminalPair.first] = availablePort;
    }

    for (auto terminalPort : mInputTerminalPortMaps) {
        LOG1("%s: executor:%s input edge terminal->port:(%d->%d)",
              __func__, mName.c_str(), terminalPort.first, terminalPort.second);
    }

    return OK;
}

/**
 * Assign output ports for output terminals
 * Ports are used for outside of the executor, so we need map terminals to ports here.
 */
int PipeExecutor::assignOutputPortsForTerminals()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    for (auto const& outputTerminalPair : mOutputTerminalPairs) {
        Port availablePort = INVALID_PORT;
        // Find the first not used port and assign it to the terminal.
        if (!isOutputPortUsed(MAIN_PORT)) {
            availablePort = MAIN_PORT;
        } else if (!isOutputPortUsed(SECOND_PORT)) {
            availablePort = SECOND_PORT;
        } else if (!isOutputPortUsed(THIRD_PORT)) {
            availablePort = THIRD_PORT;
        }
        Check(availablePort == INVALID_PORT, INVALID_OPERATION, "No output port available");

        mOutputTerminalPortMaps[outputTerminalPair.first] = availablePort;
    }

    for (auto terminalPort : mOutputTerminalPortMaps) {
        LOG1("%s: executor:%s output terminal->port:(%d->%d)",
             __func__, mName.c_str(), terminalPort.first, terminalPort.second);
    }

    return OK;
}

int PipeExecutor::allocBuffers()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    releaseBuffers();

    // Allocate internal frame buffers for the executor which is not input edge
    if (!mIsInputEdge) {
        for (auto const& uidPortPair : mInputTerminalPortMaps) {
            Port inputPort = uidPortPair.second;
            int srcFmt = mInputFrameInfo[inputPort].format;
            int srcWidth = mInputFrameInfo[inputPort].width;
            int srcHeight = mInputFrameInfo[inputPort].height;
            // Get frame size with aligned height taking in count for internal buffers.
            // To garantee PSYS kernel like GDC always get enough buffer size to process.
            int size = CameraUtils::getFrameSize(srcFmt, srcWidth, srcHeight, true);

            LOG1("%s: PipeExecutor %s allocate input buffer for terminal %d, port %d", __func__,
                  mName.c_str(), uidPortPair.first, uidPortPair.second);
            for (int i = 0; i < MAX_BUFFER_COUNT; i++) {

                // Prepare internal frame buffer for its producer.
                shared_ptr<CameraBuffer> buf = CameraBuffer::create(mCameraId,
                             BUFFER_USAGE_PSYS_INPUT, V4L2_MEMORY_USERPTR, size, i, srcFmt, srcWidth, srcHeight);
                Check(!buf, NO_MEMORY, "@%s: Allocate producer buffer failed", __func__);
                mInternalBuffers[inputPort].push_back(buf);

                mBufferProducer->qbuf(inputPort, buf);
            }
        }
    }

    // Allocate stats buffers if needed.
    int statsBufferCount = mPSysPipe->getStatsBufferCount();
    Check((statsBufferCount < 0), UNKNOWN_ERROR, "Error in getting stats buffer count for allocation");

    for (int i = 0; i < MAX_BUFFER_COUNT * statsBufferCount; i++) {
        shared_ptr<CameraBuffer> statsBuf = CameraBuffer::create(mCameraId,
                     BUFFER_USAGE_PSYS_STATS, V4L2_MEMORY_USERPTR, sizeof(ia_binary_data), i);
        Check(!statsBuf, NO_MEMORY, "Executor %s: Allocate stats buffer failed", mName.c_str());

        AutoMutex lock(mStatsBuffersLock);
        mStatsBuffers.push(statsBuf);
    }

    return OK;
}

void PipeExecutor::releaseBuffers()
{
    LOG1("%s executor:%s", __func__, mName.c_str());

    // Release internel frame buffers
    mInternalBuffers.clear();

    // Release stats buffers
    {
        AutoMutex lock(mStatsBuffersLock);
        while (!mStatsBuffers.empty()) mStatsBuffers.pop();
    }
}

}

