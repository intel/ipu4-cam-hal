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

#define LOG_TAG "PSysDAG"

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "PSysDAG.h"

namespace icamera {
PSysDAG::PSysDAG(int cameraId, PSysDagCallback* psysDagCB) :
    mCameraId(cameraId),
    mPSysDagCB(psysDagCB),
    mConfigMode(CAMERA_STREAM_CONFIGURATION_MODE_AUTO),
    mTuningMode(TUNING_MODE_MAX),
    mDefaultMainInputPort(MAIN_PORT)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    CLEAR(mOngoingSequence);
    mPolicyManager = new PolicyManager(mCameraId);
    mIspParamAdaptor = new IspParamAdaptor(mCameraId, PG_PARAM_PSYS_ISA);
}

PSysDAG::~PSysDAG()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    releasePipeExecutors();

    mIspParamAdaptor->deinit();
    delete mIspParamAdaptor;
    delete mPolicyManager;
}

void PSysDAG::setFrameInfo(const std::map<Port, stream_t>& inputInfo,
                           const std::map<Port, stream_t>& outputInfo) {
    mInputFrameInfo = inputInfo;
    mOutputFrameInfo = outputInfo;

    mDefaultMainInputPort = inputInfo.begin()->first;
    // Select default main input port in priority
    Port availablePorts[] = {MAIN_PORT, SECOND_PORT, THIRD_PORT, FORTH_PORT, INVALID_PORT};
    for (unsigned int i = 0; i < ARRAY_SIZE(availablePorts); i++) {
        if (mInputFrameInfo.find(availablePorts[i]) != mInputFrameInfo.end()) {
            mDefaultMainInputPort = availablePorts[i];
            break;
        }
    }
}

void PSysDAG::releasePipeExecutors()
{
    for (auto &executor : mExecutorsPool) {
        delete executor;
    }
    mExecutorsPool.clear();
}

/*
 * According to the policy config to create the executors,
 * and use the graph config data to configure the executors.
 */
int PSysDAG::createPipeExecutors()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    releasePipeExecutors();

    // initialize the sequence list to -1
    for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
        mOngoingSequence[i] = -1;
    }

    IGraphConfigManager *GCM = IGraphConfigManager::getInstance(mCameraId);
    Check(!GCM, UNKNOWN_ERROR, "Failed to get GC manager in PSysDAG!");

    shared_ptr<GraphConfig> gc = dynamic_pointer_cast<GraphConfig>(GCM->getGraphConfig(mConfigMode));
    Check(!gc, UNKNOWN_ERROR, "Failed to get GraphConfig in PSysDAG!");

    int graphId = gc->getGraphId();
    PolicyConfig* cfg = PlatformData::getExecutorPolicyConfig(graphId);
    Check(!cfg, UNKNOWN_ERROR, "Failed to get PolicyConfig in PSysDAG!");

    for (auto &item : cfg->pipeExecutorVec) {

        int streamId = -1;
        // Not support multiple streamId in one executor,
        // so need to the check the streamId of pgList.
        for (auto &pgName : item.pgList) {
            int tmpId = gc->getStreamIdByPgName(pgName);
            Check(tmpId == -1, BAD_VALUE, "Cannot get streamId for %s", pgName.c_str());
            Check(((streamId != -1) && (tmpId != streamId)), BAD_VALUE,
                    "the streamId: %d for pgName(%s) is different with previous: %d",
                    tmpId, pgName.c_str(), streamId);
            streamId = tmpId;
            LOG1("%s executor:%s pg name:%s streamId: %d",
                  __func__, item.exeName.c_str(), pgName.c_str(), streamId);
        }

        PipeExecutor *executor = new PipeExecutor(mCameraId, item, cfg->exclusivePgs, this, gc);
        executor->setIspParamAdaptor(mIspParamAdaptor);
        executor->setStreamId(streamId);
        executor->setPolicyManager(mPolicyManager);
        executor->setNotifyPolicy(item.notifyPolicy);

        int ret = executor->initPipe();
        if (ret != OK) {
            LOGE("Failed to create pipe for executor:%s", executor->getName());
            delete executor;
            return ret;
        }

        mExecutorsPool.push_back(executor);
    }

    for (auto &bundle : cfg->bundledExecutorDepths) {
        mPolicyManager->addExecutorBundle(bundle.bundledExecutors, bundle.depths);
    }

    return OK;
}

int PSysDAG::linkAndConfigExecutors()
{
    for (auto& consumer : mExecutorsPool) {
        std::map<ia_uid, Port> input;

        if (consumer->isInputEdge()) {
            // Use its own input info due to no executor as producer
            consumer->getInputTerminalPorts(input);
        } else {
            PipeExecutor* producer = findExecutorProducer(consumer);
            Check(producer == nullptr, BAD_VALUE, "no producer for executor %s!", consumer->getName());
            producer->getOutputTerminalPorts(input);

            consumer->setBufferProducer(producer);
            LOG1("%s: link consumer %s to %s", __func__, consumer->getName(), producer->getName());
        }

        // Link producer (output) to consumer (input) by terminal
        consumer->setInputTerminals(input);

        vector<ConfigMode> configModes;
        configModes.push_back(mConfigMode);
        consumer->configure(configModes);
    }

    return OK;
}

PipeExecutor* PSysDAG::findExecutorProducer(PipeExecutor* consumer)
{
    map<ia_uid, Port> inputTerminals;
    consumer->getInputTerminalPorts(inputTerminals);

    for (auto& executor : mExecutorsPool) {
        if (executor == consumer) {
            continue;
        }

        for (auto& inputTerminal : inputTerminals) {
            // Return if one is matched, because only one producer is supported now.
            if (executor->hasOutputTerminal(inputTerminal.first)) {
                return executor;
            }
        }
    }

    return nullptr;
}

/**
 * Bind the port between DAG and its edge executors.
 * After the binding we'll know where the task buffer should be queued to.
 */
int PSysDAG::bindExternalPortsToExecutor()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    mInputMaps.clear();
    mOutputMaps.clear();

   std::map<Port, stream_t> outputInfo;
   std::map<Port, stream_t> inputInfo;

    // Bind the input ports first.
    LOG2("%s, start to bind the input port", __func__);
    for (auto& executor : mExecutorsPool) {
        if (!executor->isInputEdge()) {
            continue;
        }
        executor->getFrameInfo(inputInfo, outputInfo);

        for (auto& frameInfo : mInputFrameInfo) {
            for (auto& portInfo : inputInfo) {
                if (executor->isSameStreamConfig(portInfo.second, frameInfo.second, mConfigMode, false)) {
                    PortMapping portMap;
                    portMap.mExecutor = executor;
                    portMap.mDagPort = frameInfo.first;
                    portMap.mExecutorPort = portInfo.first;
                    mInputMaps.push_back(portMap);
                    // Clear the stream of executor to avoid binding it again.
                    CLEAR(portInfo.second);
                    break;
                }
            }
        }
    }

    // Then bind the output ports.
    LOG2("%s, start to bind the output port", __func__);
    for (auto& executor : mExecutorsPool) {
        if (!executor->isOutputEdge()) {
            continue;
        }

        executor->getFrameInfo(inputInfo, outputInfo);
        for (auto& frameInfo : mOutputFrameInfo) {
            for (auto& portInfo : outputInfo) {
                if (executor->isSameStreamConfig(portInfo.second, frameInfo.second, mConfigMode, true)) {
                    PortMapping portMap;
                    portMap.mExecutor = executor;
                    portMap.mDagPort = frameInfo.first;
                    portMap.mExecutorPort = portInfo.first;
                    mOutputMaps.push_back(portMap);
                    // Clear the stream of executor to avoid binding it again.
                    CLEAR(portInfo.second);
                    break;
                }
            }
        }
    }

    // Each required port must be mapped to one of (edge) executor's port.
    Check(mInputMaps.size() != mInputFrameInfo.size(), BAD_VALUE, "Failed to bind input ports");
    Check(mOutputMaps.size() != mOutputFrameInfo.size(), BAD_VALUE, "Failed to bind output ports");

    return OK;
}

int PSysDAG::registerUserOutputBufs(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    for (auto& outputMap : mOutputMaps) {
        if (port == outputMap.mDagPort) {
            outputMap.mExecutor->registerOutBuffers(outputMap.mExecutorPort, camBuffer);
            break;
        }
    }

    return OK;
}

int PSysDAG::registerInternalBufs(map<Port, CameraBufVector> &internalBufs)
{
    for (auto& portToBuffers : internalBufs) {
        for (auto& inputMap : mInputMaps) {
            if (inputMap.mDagPort == portToBuffers.first) {
                for (auto& inputBuf : portToBuffers.second) {
                    inputMap.mExecutor->registerInBuffers(inputMap.mExecutorPort, inputBuf);
                }
                break;
            }
        }
    }

    return OK;
}

/**
 * Queue the buffers in PSysTaskData to the cooresponding executors.
 */
int PSysDAG::queueBuffers(const PSysTaskData& task)
{
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    // Provide the input buffers for the input edge executor.
    for (auto& inputFrame : task.mInputBuffers) {
        for (auto& inputMap : mInputMaps) {
            if (inputMap.mDagPort == inputFrame.first) {
                inputMap.mExecutor->onFrameAvailable(inputMap.mExecutorPort, inputFrame.second);
                break;
            }
        }
    }

    // Provide the output buffers for the output edge executor.
    for (auto& outputFrame : task.mOutputBuffers) {
        for (auto& outputMap : mOutputMaps) {
            if (outputMap.mDagPort == outputFrame.first) {
                outputMap.mExecutor->qbuf(outputMap.mExecutorPort, outputFrame.second);
                break;
            }
        }
    }

    return OK;
}

int PSysDAG::configure(ConfigMode configMode, TuningMode tuningMode)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    mConfigMode = configMode;
    mTuningMode = tuningMode;

    // Configure IspParamAdaptor
    int ret = mIspParamAdaptor->init();
    Check(ret != OK, ret, "Init isp Adaptor failed, tuningMode %d", mTuningMode);

    ret = mIspParamAdaptor->configure(mInputFrameInfo[mDefaultMainInputPort], mConfigMode, mTuningMode);
    Check(ret != OK, ret, "Configure isp Adaptor failed, tuningMode %d", mTuningMode);

    ret = createPipeExecutors();
    Check(ret != OK, ret, "@%s, create psys executors failed", __func__);

    ret = linkAndConfigExecutors();
    Check(ret != OK, ret, "Link executors failed");

    ret = bindExternalPortsToExecutor();
    Check(ret != OK, ret, "Bind ports failed");

    return OK;
}

int PSysDAG::start()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    mPolicyManager->setActive(true);

    for (auto& executors : mExecutorsPool) {
        executors->start();
    }
    return OK;
}

int PSysDAG::stop()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    mPolicyManager->setActive(false);

    for (auto& executors : mExecutorsPool) {
        executors->notifyStop();
    }

    for (auto& executors : mExecutorsPool) {
        executors->stop();
    }
    return OK;
}

int PSysDAG::resume()
{
    mPolicyManager->setActive(true);
    return OK;
}

int PSysDAG::pause()
{
    mPolicyManager->setActive(false);
    return OK;
}

void PSysDAG::addTask(PSysTaskData taskParam)
{
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    if (taskParam.mTuningMode != mTuningMode) {
        tuningReconfig(taskParam.mTuningMode);
    }

    // It's too early to runIspAdapt here, and the ipu parameters
    // may be incorrect when runPipe.
    // TODO: remove this condition when 4k ULL pipe run faster
    if ((mTuningMode != TUNING_MODE_VIDEO_HDR) && (mTuningMode != TUNING_MODE_VIDEO_HDR2)) {
        mIspParamAdaptor->runIspAdapt(&taskParam.mIspSettings,
                                      taskParam.mInputBuffers.at(mDefaultMainInputPort)->getSequence());
    }

    {
        // Save the task data into mOngoingTasks
        TaskInfo task;
        task.mTaskData = taskParam;
        // Count how many valid output buffers need to be returned.
        for (auto& outBuf : taskParam.mOutputBuffers) {
            if (outBuf.second) {
                task.mNumOfValidBuffers++;
            }
        }
        LOG2("%s:Id:%d push task with %d output buffers, sequence: %ld",
                __func__, mCameraId, task.mNumOfValidBuffers,
                taskParam.mInputBuffers.at(mDefaultMainInputPort)->getSequence());
        AutoMutex taskLock(mTaskLock);
        mOngoingTasks.push_back(task);
    }

    queueBuffers(taskParam);
}

int PSysDAG::getParameters(Parameters& param)
{
    return mIspParamAdaptor->getParameters(param);
}

TuningMode PSysDAG::getTuningMode(long sequence)
{
    AutoMutex taskLock(mTaskLock);

    TuningMode taskTuningMode = mTuningMode;
    bool taskTuningModeFound = false;

    for (auto const& task : mOngoingTasks) {
        if (sequence == task.mTaskData.mInputBuffers.at(mDefaultMainInputPort)->getSequence()) {
            taskTuningMode = task.mTaskData.mTuningMode;
            taskTuningModeFound = true;
            break;
        }
    }

    if (!taskTuningModeFound) {
        LOGW("No task tuning mode found for sequence:%ld, use current DAG tuning mode.", sequence);
    }

    return taskTuningMode;
}

/**
 * Use to handle the frame done event from the executors.
 *
 * This is for returning output buffers to PSysDAG. And it'll check if all the valid
 * output buffer returned, if so, then it'll return the whole corresponding task data to
 * PSysProcessor.
 */
int PSysDAG::onFrameDone(Port port, const shared_ptr<CameraBuffer>& buffer)
{
    LOG2("@%s, mCameraId:%d buffer=%p", __func__, mCameraId, buffer.get());

    if (!buffer) return OK; // No need to handle if the buffer is nullptr.

    long sequence = buffer->getSequence();
    bool needReturn = false;
    PSysTaskData result;

    {
        // Remove the sequence when finish to process it
        AutoMutex   l(mSequenceLock);
        for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
            if (mOngoingSequence[i] == sequence) {
                mOngoingSequence[i] = -1;
                break;
            }
        }
    }

    {
        AutoMutex taskLock(mTaskLock);
        for (auto it = mOngoingTasks.begin(); it != mOngoingTasks.end(); it++) {
            // Check if the returned buffer belong to the task.
            if (sequence != it->mTaskData.mInputBuffers.at(mDefaultMainInputPort)->getSequence()) {
                continue;
            }

            it->mNumOfReturnedBuffers++;
            if (it->mNumOfReturnedBuffers >= it->mNumOfValidBuffers) {
                result = it->mTaskData;
                needReturn = true;
                LOG2("%s:Id:%d finish task with %d returned output buffers, sequence: %ld", __func__,
                        mCameraId, it->mNumOfReturnedBuffers, sequence);
                // Remove the task data from mOngoingTasks since it's already processed.
                mOngoingTasks.erase(it);
            }
            // No need check other if other tasks are matched with the returned buffer since
            // we already found one.
            break;
        }
    }

    if (needReturn) {
        returnBuffers(result);
    }

    return OK;
}

int PSysDAG::prepareIpuParams(long sequence, bool forceUpdate)
{
    // Make sure the AIC is executed once.
    if (!forceUpdate) {
        AutoMutex   l(mSequenceLock);

        for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
            // This means aic for the sequence has been executed.
            if (mOngoingSequence[i] == sequence) {
                return OK;
            }
        }

        // Store the new sequence.
        for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
            if (mOngoingSequence[i] == -1) {
                mOngoingSequence[i] = sequence;
                break;
            }
        }
    }

    const IspSettings* ispSettings = nullptr;
    {
        AutoMutex taskLock(mTaskLock);
        for (auto const& task : mOngoingTasks) {
            if (sequence == task.mTaskData.mInputBuffers.at(mDefaultMainInputPort)->getSequence()) {
                ispSettings = &task.mTaskData.mIspSettings;
                break;
            }
        }
    }

    if (ispSettings == nullptr) {
        LOGW("Run ISP adaptor with ispSettings is nullptr. This should never happen.");
    }
    LOG2("%s, Run AIC for sequence: %ld", __func__, sequence);

    return mIspParamAdaptor->runIspAdapt(ispSettings, sequence);
}

int PSysDAG::returnBuffers(PSysTaskData& result)
{
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    Check(!mPSysDagCB, INVALID_OPERATION, "Invalid PSysProcessor");

    mPSysDagCB->onFrameDone(result);
    return OK;
}

void PSysDAG::registerListener(EventType eventType, EventListener* eventListener)
{
    //Pass through event registration to PipeExecutor
    for (auto const& executor : mExecutorsPool) {
        executor->registerListener(eventType, eventListener);
    }
}

void PSysDAG::removeListener(EventType eventType, EventListener* eventListener)
{
    //Pass through event unregistration to PipeExecutor
    for (auto const& executor : mExecutorsPool) {
        executor->removeListener(eventType, eventListener);
    }
}

void PSysDAG::tuningReconfig(TuningMode newTuningMode)
{
    LOG1("@%s ", __func__);

    if (mIspParamAdaptor) {
        mIspParamAdaptor->deinit();
    } else {
        mIspParamAdaptor = new IspParamAdaptor(mCameraId, PG_PARAM_PSYS_ISA);
    }

    int ret = mIspParamAdaptor->init();
    Check(ret != OK, VOID_VALUE, "Init isp Adaptor failed, tuningMode %d", newTuningMode);

    ret = mIspParamAdaptor->configure(mInputFrameInfo[mDefaultMainInputPort], mConfigMode, newTuningMode);
    Check(ret != OK, VOID_VALUE, "Failed to reconfig isp Adaptor.");

    mTuningMode = newTuningMode;
}

}
