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

#define LOG_TAG "PSysProcessor"

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/SwImageConverter.h"

#include "PlatformData.h"
#include "3a/AiqResultStorage.h"
#include "ParameterGenerator.h"

#include "PSysProcessor.h"

#include "IspControl.h"
#include "isp_control/IspControlUtils.h"


namespace icamera {
PSysProcessor::PSysProcessor(int cameraId, ParameterGenerator *pGenerator) :
        mCameraId(cameraId),
        mParameterGenerator(pGenerator),
        mUpdatedIspIndex(-1),
        mUsedIspIndex(-1),
        mCurConfigMode(CAMERA_STREAM_CONFIGURATION_MODE_NORMAL),
        mTuningMode(TUNING_MODE_MAX),
        mRawPort(INVALID_PORT),
        mStatus(PIPELINE_UNCREATED)
{
    LOG1("@%s camera id:%d", __func__, mCameraId);

    mProcessThread = new ProcessThread(this);
    allocPalControlBuffers();
    mSensorOB = new SensorOB(mCameraId);
}

PSysProcessor::~PSysProcessor()
{
    LOG1("@%s ", __func__);

    for (int i = 0; i < IA_PAL_CONTROL_BUFFER_SIZE; i++)
        free(mPalCtrlBuffers[i].data);

    mUpdatedIspIndex = -1;
    mUsedIspIndex = -1;
    mProcessThread->join();
    delete mProcessThread;
    delete mSensorOB;
}

int PSysProcessor::configure(const vector<ConfigMode>& configModes)
{
    //Create PSysDAGs actually
    LOG1("@%s ", __func__);
    Check(mStatus == PIPELINE_CREATED, -1, "@%s mStatus is in wrong status: PIPELINE_CREATED", __func__);
    mConfigModes = configModes;

    std::map<Port, stream_t> outputFrameInfo;
    // Check if it's required to output raw image from ISYS
    for (auto &outFrameInfo : mOutputFrameInfo) {
        if (outFrameInfo.second.format == V4L2_PIX_FMT_SGRBG12) {
            mRawPort = outFrameInfo.first;
        } else {
            outputFrameInfo[outFrameInfo.first] = outFrameInfo.second;
        }
    }

    int ret = OK;
    //Create PSysDAG according to real configure mode
    for (auto &cfg : mConfigModes) {
        if (mPSysDAGs.find(cfg) != mPSysDAGs.end()) {
            continue;
        }

        TuningConfig tuningConfig;
        ret = PlatformData::getTuningConfigByConfigMode(mCameraId, cfg, tuningConfig);
        Check(ret != OK, ret, "%s: can't get config for mode %d", __func__, cfg);

        LOG1("Create PSysDAG for ConfigMode %d", cfg);
        shared_ptr<PSysDAG> pSysDAG = shared_ptr<PSysDAG>(new PSysDAG(mCameraId, this));

        pSysDAG->setFrameInfo(mInputFrameInfo, outputFrameInfo);
        ret = pSysDAG->configure(tuningConfig.configMode, tuningConfig.tuningMode);
        Check(ret != OK, ret, "@%s configure psys dag failed:%d", __func__, ret);

        mPSysDAGs[tuningConfig.configMode] = pSysDAG;

        //Update default active config mode
        mCurConfigMode = tuningConfig.configMode;
        mTuningMode = tuningConfig.tuningMode;
    }

    if (ret == OK) mStatus = PIPELINE_CREATED;
    return ret;

}

int PSysProcessor::registerUserOutputBufs(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    for (auto psysDAGPair : mPSysDAGs) {
        shared_ptr<PSysDAG> psysDAG = psysDAGPair.second;
        if (!psysDAG) continue;
        int ret = psysDAG->registerUserOutputBufs(port, camBuffer);
        Check(ret != OK, BAD_VALUE, "%s, register user buffer failed, ret: %d", __func__, ret);
    }

    return OK;
}

int PSysProcessor::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);
    AutoMutex   l(mBufferQueueLock);

    /* Should use MIN_BUFFER_COUNT to optimize frame latency when PSYS processing
     * time is slower than ISYS
     */
    int ret = allocProducerBuffers(mCameraId, PlatformData::getPreferredBufQSize(mCameraId));
    Check(ret != OK, NO_MEMORY, "Allocating producer buffer failed:%d", ret);

    mThreadRunning = true;
    mProcessThread->run("PsysProcessor", PRIORITY_NORMAL);
    for (auto psysDAGPair : mPSysDAGs) {
        shared_ptr<PSysDAG> curPsysDAG = psysDAGPair.second;
        if (!curPsysDAG) continue;
        curPsysDAG->start();
        if (PlatformData::isNeedToPreRegisterBuffer(mCameraId)) {
            curPsysDAG->registerInternalBufs(mInternalBuffers);
        }
    }

    return OK;
}

void PSysProcessor::stop()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);

    for (auto psysDAGPair : mPSysDAGs) {
        shared_ptr<PSysDAG> curPsysDAG = psysDAGPair.second;
        if (!curPsysDAG) continue;
        curPsysDAG->stop();
    }

    mProcessThread->requestExit();
    {
        AutoMutex l(mBufferQueueLock);
        mThreadRunning = false;
        //Wakeup the thread to exit
        mFrameAvailableSignal.signal();
        mOutputAvailableSignal.signal();
        mFrameDoneSignal.signal();
        AutoMutex lMeta(mMetaQueueLock);
        mMetaAvailableSignal.signal();
    }

    mProcessThread->requestExitAndWait();

    // Thread is not running. It is safe to clear the Queue
    clearBufferQueues();
}

int PSysProcessor::setParameters(const Parameters& param)
{
    LOG1("%s camera id:%d", __func__, mCameraId);
    // Process image enhancement related settings.
    camera_image_enhancement_t enhancement;
    int ret = param.getImageEnhancement(enhancement);
    AutoWMutex wl(mIspSettingsLock);
    if (ret == OK) {
        mIspSettings.manualSettings.manualSharpness = (char)enhancement.sharpness;
        mIspSettings.manualSettings.manualBrightness = (char)enhancement.brightness;
        mIspSettings.manualSettings.manualContrast = (char)enhancement.contrast;
        mIspSettings.manualSettings.manualHue = (char)enhancement.hue;
        mIspSettings.manualSettings.manualSaturation = (char)enhancement.saturation;

        // TODO: need to consider how to add feature level from user setting.
        mIspSettings.eeSetting.feature_level = ia_isp_feature_level_low;
        mIspSettings.eeSetting.strength = enhancement.sharpness;
    } else {
        mIspSettings.eeSetting.feature_level = ia_isp_feature_level_low;
        mIspSettings.eeSetting.strength = 0;
    }

    camera_nr_mode_t manualNrMode;
    camera_nr_level_t manualNrLevel;

    int manualNrModeSet = param.getNrMode(manualNrMode);
    int manualNrLevelSet = param.getNrLevel(manualNrLevel);

    if (manualNrModeSet == OK) {
        LOG2("%s: manual NR mode set: %d", __func__, manualNrMode);
        switch (manualNrMode) {
            case NR_MODE_OFF:
                mIspSettings.nrSetting.feature_level = ia_isp_feature_level_off;
                break;
            case NR_MODE_AUTO:
                mIspSettings.nrSetting.feature_level = ia_isp_feature_level_low;
                break;
            case NR_MODE_MANUAL_NORMAL:
                mIspSettings.nrSetting.feature_level = ia_isp_feature_level_low;
                break;
            case NR_MODE_MANUAL_EXPERT:
                mIspSettings.nrSetting.feature_level = ia_isp_feature_level_high;
                break;
            default:
                mIspSettings.nrSetting.feature_level = ia_isp_feature_level_low;
        }

    } else {
        LOG2("%s: manual NR mode not set, default enabled", __func__);
        mIspSettings.nrSetting.feature_level = ia_isp_feature_level_high;
    }

    if (manualNrLevelSet == OK) {
        LOG2("%s: manual NR level set: %d", __func__, manualNrLevel.overall);
        mIspSettings.nrSetting.strength = (char)manualNrLevel.overall;
    } else {
        LOG2("%s: manual NR level not set, default used", __func__);
        mIspSettings.nrSetting.strength = (char)0;
    }

    LOG2("%s: ISP NR setting, level: %d, strength: %d",
            __func__, (int)mIspSettings.nrSetting.feature_level,
            (int)mIspSettings.nrSetting.strength);

    camera_video_stabilization_mode_t stabilizationMode;
    ret = param.getVideoStabilizationMode(stabilizationMode);
    if (ret == OK) {
         mIspSettings.videoStabilization = (stabilizationMode == VIDEO_STABILIZATION_MODE_ON);
    } else {
         mIspSettings.videoStabilization = false;
    }
    LOG2("%s: Video stablilization enabled:%d", __func__, mIspSettings.videoStabilization);

    uint8_t wfovMode;
    ret = param.getWFOV(wfovMode);
    if (ret == OK && wfovMode) {
         mIspSettings.wfovMode = true;
         param.getDigitalZoomRatio(mIspSettings.zoom);
         param.getSensorMountType(mIspSettings.sensorMountType);
         param.getViewProjection(mIspSettings.viewProjection);
         param.getViewRotation(mIspSettings.viewRotation);
         param.getCameraRotation(mIspSettings.cameraRotation);
         param.getViewFineAdjustments(mIspSettings.viewFineAdj);
    } else {
         mIspSettings.wfovMode = false;
    }
    LOG2("%s: WFOV mode enabled:%d", __func__, mIspSettings.wfovMode);

    fillPalOverrideData(param);

    return ret;
}

int PSysProcessor::getParameters(Parameters& param)
{
    LOG1("@%s ", __func__);
    AutoRMutex rl(mIspSettingsLock);
    camera_image_enhancement_t enhancement = { mIspSettings.manualSettings.manualSharpness,
                                               mIspSettings.manualSettings.manualBrightness,
                                               mIspSettings.manualSettings.manualContrast,
                                               mIspSettings.manualSettings.manualHue,
                                               mIspSettings.manualSettings.manualSaturation };
    int ret = param.setImageEnhancement(enhancement);

    ret |= mPSysDAGs[mCurConfigMode]->getParameters(param);

    // Override the data with what user has enabled before, since the data get from
    // IspParamAdaptor might be old, and it causes inconsistent between what user sets and gets.
    if (mUpdatedIspIndex != -1) {
        ia_binary_data *palOverride = &mPalCtrlBuffers[mUpdatedIspIndex];

        set<uint32_t> enabledControls;
        param.getEnabledIspControls(enabledControls);
        for (auto ctrlId : enabledControls) {
            void* data = IspControlUtils::findDataById(ctrlId, palOverride->data, palOverride->size);
            if (data == nullptr) continue;

            param.setIspControl(ctrlId, data);
        }
    }

    return ret;
}

/**
 * Get required PAL override buffer size
 *
 * According to the supported ISP control feature list, calculate how much the buffer we need
 * to be able to store all data sent from application.
 */
size_t PSysProcessor::getRequiredPalBufferSize()
{
    vector<uint32_t> controls = PlatformData::getSupportedIspControlFeatures(mCameraId);
    const size_t kHeaderSize = sizeof(ia_record_header);
    size_t totalSize = 0;
    for (auto ctrlId : controls) {
        totalSize += ALIGN_8(kHeaderSize + IspControlUtils::getSizeById(ctrlId));
    }

    return totalSize;
}

/**
 * Fill the PAL override data by the given param
 */
int PSysProcessor::fillPalOverrideData(const Parameters& param)
{
    // Find one new pal control buffer to update the pal override data
    if (mUpdatedIspIndex == mUsedIspIndex) {
        mUpdatedIspIndex++;
        mUpdatedIspIndex = mUpdatedIspIndex % IA_PAL_CONTROL_BUFFER_SIZE;
    }

    // Use mPalCtrlBuffers[mUpdatedIspIndex] to store the override data
    ia_binary_data *palOverride = &mPalCtrlBuffers[mUpdatedIspIndex];
    palOverride->size = getRequiredPalBufferSize();

    const size_t kHeaderSize = sizeof(ia_record_header);
    uint32_t offset = 0;
    uint8_t* overrideData = (uint8_t*)palOverride->data;

    set<uint32_t> enabledControls;
    param.getEnabledIspControls(enabledControls);

    bool isCcmEnabled = false;
    bool isAcmEnabled = false;

    for (auto ctrlId : enabledControls) {
        if (!PlatformData::isIspControlFeatureSupported(mCameraId, ctrlId)) continue;

        LOG1("Enabled ISP control: %s", IspControlUtils::getNameById(ctrlId));

        ia_record_header* header = (ia_record_header*)(overrideData + offset);
        header->uuid = ctrlId;
        header->size = ALIGN_8(kHeaderSize + IspControlUtils::getSizeById(ctrlId));
        Check((offset + header->size) > palOverride->size, BAD_VALUE,
              "The given buffer is not big enough for the override data");

        int ret = param.getIspControl(ctrlId, overrideData + (offset + kHeaderSize));
        // If ctrlId is set by the app, then move to next memory block, otherwise the offest
        // remain unchanged in order to use the same memory block.
        if (ret != OK) continue;

        offset += header->size;

        if (ctrlId == camera_control_isp_ctrl_id_color_correction_matrix) {
            isCcmEnabled = true;
        } else if (ctrlId == camera_control_isp_ctrl_id_advanced_color_correction_matrix) {
            isAcmEnabled = true;
        }
    }

    // Use identity matrix to fill ACM's matrices since HDR/ULL may use ACM combined with CCM,
    // if ACM is not provided, then there will be no IQ effect for CCM as well.
    if (isCcmEnabled && !isAcmEnabled) {
        offset += fillDefaultAcmData(overrideData + offset);
    }

    // Reset the original size of palOverride to the size of its valid data.
    palOverride->size = offset;
    LOG1("%s, the data size for pal override: %u", __func__, palOverride->size);

    return OK;
}

int PSysProcessor::fillDefaultAcmData(uint8_t* overrideData)
{
    // Don't fill ACM if it's not supported.
    if (!PlatformData::isIspControlFeatureSupported(mCameraId,
            camera_control_isp_ctrl_id_advanced_color_correction_matrix)) {
        return 0;
    }

    const size_t kHeaderSize = sizeof(ia_record_header);
    ia_record_header* header = (ia_record_header*)(overrideData);
    header->uuid = camera_control_isp_ctrl_id_advanced_color_correction_matrix;
    header->size = ALIGN_8(kHeaderSize + IspControlUtils::getSizeById(header->uuid));

    camera_control_isp_advanced_color_correction_matrix_t* acm =
        (camera_control_isp_advanced_color_correction_matrix_t*)(overrideData + kHeaderSize);

    acm->bypass = 0;
    acm->number_of_sectors = 24;
    const float kIdentityMatrix[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < acm->number_of_sectors; i++) {
        MEMCPY_S(acm->ccm_matrices + i * 9, sizeof(kIdentityMatrix),
                 kIdentityMatrix, sizeof(kIdentityMatrix));
    }

    return header->size;
}

int PSysProcessor::allocPalControlBuffers()
{
    LOG1("%s", __func__);

    for (int i = 0; i < IA_PAL_CONTROL_BUFFER_SIZE; i++) {
        mPalCtrlBuffers[i].size = getRequiredPalBufferSize();
        mPalCtrlBuffers[i].data = calloc(1, mPalCtrlBuffers[i].size);
        Check(mPalCtrlBuffers[i].data == nullptr, NO_MEMORY, "Faile to calloc the memory for pal override");
    }

    return OK;
}

/**
 * Get available setting sequence from outBuf
 */
long PSysProcessor::getSettingSequence(const CameraBufferPortMap &outBuf)
{
    long settingSequence = -1;
    for (auto& output: outBuf) {
        if (output.second) {
            settingSequence = output.second->getSettingSequence();
            break;
        }
    }
    return settingSequence;
}

/**
 * Check if the input frame should be skipped
 *
 * If the corresponding mSkip of AiqResult gotten from sequence is true,
 * return true; otherwise return false.
 */
bool PSysProcessor::needSkipOutputFrame(long sequence)
{
    // Check if need to skip output frame
    const AiqResult* aiqResults = AiqResultStorage::getInstance(mCameraId)->getAiqResult(sequence);
    if (aiqResults != nullptr && aiqResults->mSkip) {
        LOG1("%s, sequence %ld", __func__, sequence);
        return true;
    }
    return false;
}

/**
 * Check if 'inBuffer' can be used for 'settingSequence' to run PSys pipe.
 *
 * If 'settingSequence' is -1, it means the output buffer doesn't require particular
 * input buffer, so it can run the pipe.
 * If 'inputSequence' larger than 'settingSequence', the pipeline needs to
 * run as well, otherwise the pipe doesn't need to run and this input buffer needs to
 * be skipped.
 */
bool PSysProcessor::needExecutePipe(long settingSequence, long inputSequence)
{
    if (settingSequence == -1 || inputSequence >= settingSequence) {
        return true;
    }

    return false;
}

/**
 * Check if the input buffer need to be reused
 *
 * If 'settingSequence' is -1, it means the output buffer doesn't require particular
 * input buffer, so the input buffer doesn't need to be reused.
 * If 'inputSequence' larger than 'settingSequence', means the input buffer
 * may be required by following output buffer, so it may be reused later.
 */
bool PSysProcessor::needHoldOnInputFrame(long settingSequence, long inputSequence)
{
    if (settingSequence == -1 || inputSequence <= settingSequence) {
        return false;
    }

    return true;
}

/**
 * Check if pipe needs to be switched according to AIQ result.
 */
bool PSysProcessor::needSwitchPipe(long sequence)
{
    const AiqResult* aiqResults = AiqResultStorage::getInstance(mCameraId)->getAiqResult(sequence);
    if (aiqResults == nullptr) {
        LOG2("%s: not found sequence %ld in AiqResultStorage, no update for active modes",
            __func__, sequence);
        return false;
    }

    TuningMode curTuningMode = aiqResults->mTuningMode;
    LOG2("%s: aiqResults->mTuningMode = %d", __func__, curTuningMode);

    if (mTuningMode == curTuningMode) {
        return false;
    }

    for (auto cfg : mConfigModes) {
        TuningMode tMode;
        int ret = PlatformData::getTuningModeByConfigMode(mCameraId, cfg, tMode);
        if (ret == OK && tMode == curTuningMode) {
            mCurConfigMode = cfg;
            mTuningMode = curTuningMode;
            return true;
        }
    }
    return false;
}

void PSysProcessor::handleEvent(EventData eventData)
{
    LOG2("%s: got event type %d", __func__, eventData.type);
    // Process registered events
    switch (eventData.type) {
        case EVENT_META:
            if (PlatformData::needHandleVbpInMetaData(mCameraId, mCurConfigMode) ||
                    PlatformData::getMetadataSize(mCameraId) > 0) {
                AutoMutex l(mMetaQueueLock);
                mMetaQueue.push(eventData.data.meta);
                LOG2("%s: received meta data, current queue size %lu",
                    __func__, mMetaQueue.size());
                mMetaAvailableSignal.signal();
            }
            break;
        default:
            LOGW("Unexpected event: %d", eventData.type);
            break;
    }
}

int PSysProcessor::setVbpToIspParam(long sequence, timeval timestamp)
{

    //Check fixed VBP firstly.
    int fixedVbp = PlatformData::getFixedVbp(mCameraId);

    if (fixedVbp >= 0) {
        LOG2("%s: set fixed vbp %d", __func__, fixedVbp);
        mIspSettings.vbp = fixedVbp;
        return OK;
    }

    //Check dynamic VBP.
    ConditionLock lock(mMetaQueueLock);

    //Remove all older meta data
    while (!mMetaQueue.empty() && mMetaQueue.front().sequence < sequence) {
        LOG2("%s: remove older meta data for sequence %ld", __func__,
            mMetaQueue.front().sequence);
        mMetaQueue.pop();
    }

    while (mMetaQueue.empty()) {
        int ret = mMetaAvailableSignal.waitRelative(lock, kWaitDuration);

        if (!mThreadRunning) {
            LOG1("@%s: Processor is not active while waiting for meta data.", __func__);
            return UNKNOWN_ERROR;
        }

        if (ret == TIMED_OUT) {
            LOGE("@%s: dqbuf MetaQueue timed out", __func__);
            return ret;
        }
    }

    if (mMetaQueue.front().sequence == sequence) {
        AutoWMutex l(mIspSettingsLock);
        mIspSettings.vbp = mMetaQueue.front().vbp;
        mMetaQueue.pop();
        LOG2("%s: found vbp %d for frame sequence %ld", __func__,
            mIspSettings.vbp, sequence);
        return OK;
    }

    LOGW("Missing meta data for seq %ld, timestamp %ld, Cur meta seq %ld, timestamp %ld",
          sequence, TIMEVAL2USECS(timestamp),
          mMetaQueue.front().sequence, TIMEVAL2USECS(mMetaQueue.front().timestamp));
    return UNKNOWN_ERROR;
}

// PSysProcessor ThreadLoop
int PSysProcessor::processNewFrame()
{
    PERF_CAMERA_ATRACE();
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    Check(!mBufferProducer, INVALID_OPERATION, "No available producer");

    int ret = OK;
    bool needRunPipe = true;
    bool holdOnInput = false;
    CameraBufferPortMap srcBuffers, dstBuffers;

    {
    ConditionLock lock(mBufferQueueLock);
    ret = waitFreeBuffersInQueue(lock, srcBuffers, dstBuffers);
    // Already stopped
    if (!mThreadRunning) return -1;

    // Wait frame buffer time out should not involve thread exit.
    if (ret != OK) {
        LOG1("%s, cameraId: %d timeout happen, wait recovery", __func__, mCameraId);
        return OK;
    }

    Port defaultPort = srcBuffers.begin()->first;
    shared_ptr<CameraBuffer> mainBuf = srcBuffers[defaultPort];
    long inputSequence = mainBuf->getSequence();

    if (mSensorOB->runOB(mCurConfigMode, mainBuf, &mIspSettings) != OK) {
        LOGW("No OB data obtained from sensor.");
    }

    if (PlatformData::needSetVbp(mCameraId, mCurConfigMode)) {
        LOG2("%s: handle frame buffer sequence %ld timestamp %ld",
            __func__, inputSequence, TIMEVAL2USECS(mainBuf->getTimestamp()));

        int vbpStatus = setVbpToIspParam(inputSequence, mainBuf->getTimestamp());

        // Skip input frame and return buffer if no matching vbp set to ISP params
        if (vbpStatus != OK) {
            for (auto& input: mInputQueue) {
                input.second.pop();
            }

            for (const auto& item : srcBuffers) {
                mBufferProducer->qbuf(item.first, item.second);
            }
            return OK;
        }
    }

    // Output raw image
    if (mRawPort != INVALID_PORT) {
        shared_ptr<CameraBuffer> dstBuf = nullptr;

        // Get output buffer and remove it from dstBuffers
        for (auto &buffer : dstBuffers) {
            if (buffer.first == mRawPort) {
                dstBuf = buffer.second;
                dstBuf->updateV4l2Buffer(mainBuf->getV4l2Buffer());
                dstBuffers.erase(mRawPort);
                break;
            }
        }

        outputRawImage(mainBuf, dstBuf);
    }

    long settingSequence = getSettingSequence(dstBuffers);
    needRunPipe = needExecutePipe(settingSequence, inputSequence);
    holdOnInput = needHoldOnInputFrame(settingSequence, inputSequence);

    LOG2("%s: dst sequence = %ld, src sequence = %ld, needRunPipe = %d, needReuseInput = %d",
        __func__, settingSequence, inputSequence, needRunPipe, holdOnInput);

    if (needRunPipe && !needSkipOutputFrame(inputSequence)) {
        for (auto& output: mOutputQueue) {
            output.second.pop();
        }
    }

    // If input buffer will be used later, don't pop it from the queue.
    if (!holdOnInput) {
        for (auto& input: mInputQueue) {
            input.second.pop();
        }
    }
    } // End of lock mBufferQueueLock

    if (needRunPipe) {
        dispatchTask(srcBuffers, dstBuffers);
    } else if (!holdOnInput) {
        for (const auto& src : srcBuffers) {
            mBufferProducer->qbuf(src.first, src.second);
        }
    }

    return OK;
}

void PSysProcessor::dispatchTask(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf)
{
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    long currentSequence = inBuf.begin()->second->getSequence();

    {
        ConditionLock lock(mBufferQueueLock);

        ConfigMode previousMode = mCurConfigMode;
        bool needSwitch = needSwitchPipe(currentSequence);

        if (needSwitch) {
            LOG1("Switch pipe for sequence:%ld, unprocessed buffer number:%zu",
                  currentSequence, mSequenceInflight.size());

            // Deactive the PSysDag which is no longer used.
            mPSysDAGs[previousMode]->pause();

            // Before switching, need to wait all buffers in current pipe being processed.
            while (!mSequenceInflight.empty()) {
                int ret = mFrameDoneSignal.waitRelative(lock, kWaitDuration);
                if (!mThreadRunning) {
                    LOG1("@%s: Processor is not active while waiting for frame done.", __func__);
                    return;
                }

                if (ret == TIMED_OUT) {
                    LOGE("Waiting for frame done event timeout");
                    return;
                }
            }

            // Activate the current used PSysDag.
            mPSysDAGs[mCurConfigMode]->resume();
        }
        mSequenceInflight.push(currentSequence);
    } // End of lock mBufferQueueLock

    // Prepare the task input paramerters including input and output buffers, settings etc.
    PSysTaskData taskParam;
    taskParam.mTuningMode = mTuningMode;
    taskParam.mInputBuffers = inBuf;
    taskParam.mOutputBuffers = outBuf;

    long settingSequence = getSettingSequence(outBuf);
    // Handle per-frame settings if output buffer requires
    if (settingSequence > -1 && mParameterGenerator) {
        Parameters params;
        if (mParameterGenerator->getParameters(currentSequence, &params) == OK) {
            setParameters(params);
        }
    }
    {
        AutoRMutex rl(mIspSettingsLock);
        if (mUpdatedIspIndex > -1)
            mUsedIspIndex = mUpdatedIspIndex;
        if (mUsedIspIndex > -1 &&
            mPalCtrlBuffers[mUsedIspIndex].size > 0) {
            mIspSettings.palOverride = &mPalCtrlBuffers[mUsedIspIndex];
        } else {
            mIspSettings.palOverride = nullptr;
        }
        taskParam.mIspSettings = mIspSettings;
    }

    if (!mThreadRunning) return;

    mPSysDAGs[mCurConfigMode]->addTask(taskParam);
}

void PSysProcessor::registerListener(EventType eventType, EventListener* eventListener)
{
    // Only delegate stats event registration to deeper layer DAG and PipeExecutor
    if ((eventType != EVENT_PSYS_STATS_BUF_READY) && (eventType != EVENT_PSYS_STATS_SIS_BUF_READY)) {
        BufferQueue::registerListener(eventType, eventListener);
        return;
    }

    for (auto const& realModeDAGPair: mPSysDAGs) {
        realModeDAGPair.second->registerListener(eventType, eventListener);
    }
}

void PSysProcessor::removeListener(EventType eventType, EventListener* eventListener)
{
    // Only delegate stats event unregistration to deeper layer DAG and PipeExecutor
    if ((eventType != EVENT_PSYS_STATS_BUF_READY) && (eventType != EVENT_PSYS_STATS_SIS_BUF_READY)) {
        BufferQueue::removeListener(eventType, eventListener);
        return;
    }

    for (auto const& realModeDAGPair: mPSysDAGs) {
        realModeDAGPair.second->removeListener(eventType, eventListener);
    }
}
int PSysProcessor::handleMetadata(long sequence, shared_ptr<CameraBuffer> &srcBuf, shared_ptr<CameraBuffer> &dstBuf) {
    ConditionLock lock(mMetaQueueLock);

    if (!mMetaQueue.empty()) {
        EventDataMeta metadata;
        metadata = mMetaQueue.front();
        mMetaQueue.pop();

        while (metadata.sequence < sequence &&
                !mMetaQueue.empty()) {
            metadata = mMetaQueue.front();
            mMetaQueue.pop();
        }

        camera_buffer_t *ubuffer;
        ubuffer = dstBuf->getUserBuffer();
        camera_buffer_extended_info_t *extended_info;
        extended_info = ubuffer->extended_info;

        if (metadata.sequence == sequence &&
                dstBuf->isFlagsSet(BUFFER_FLAG_EXTENDED_INFO)) {
            if(extended_info &&
                    extended_info->flags & BUFFER_EXTENDED_INFO_FLAG_METADATA &&
                    extended_info->metadata)
                MEMCPY_S(extended_info->metadata, PlatformData::getMetadataSize(mCameraId),
                        metadata.raw, RAW_SIZE);
            LOG2("%s: copy raw sensor metadata to user buffer", __func__);
        } else if (metadata.sequence != sequence &&
                dstBuf->isFlagsSet(BUFFER_FLAG_EXTENDED_INFO)) {
            if(extended_info &&
                    extended_info->flags & BUFFER_EXTENDED_INFO_FLAG_METADATA &&
                    extended_info->metadata) {
                memset(extended_info->metadata, 0, PlatformData::getMetadataSize(mCameraId));
                LOGW("@%s: invalid sequence matching, metadata buffer will be cleared", __func__);
            }
        }
    }

    return OK;
}

void PSysProcessor::onFrameDone(const PSysTaskData& result)
{
    PERF_CAMERA_ATRACE();
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    EventDataFrame eventDataFrame;
    CLEAR(eventDataFrame);
    eventDataFrame.sequence = -1;

    long sequence = result.mInputBuffers.begin()->second->getSequence();

    for (auto& dst : result.mOutputBuffers) {
        Port port = dst.first;
        shared_ptr<CameraBuffer> outBuf = dst.second;
        // If the output buffer is nullptr, that means user doesn't request that buffer,
        // so it doesn't need to be handled here.
        if (!outBuf) {
            continue;
        }

        if (CameraDump::isDumpTypeEnable(DUMP_PSYS_OUTPUT_BUFFER)) {
            CameraDump::dumpImage(mCameraId, outBuf, M_PSYS, port);
        }

        if (!needSkipOutputFrame(sequence)) {
            if (PlatformData::getMetadataSize(mCameraId) > 0) {
                    shared_ptr<CameraBuffer> srcBuf = result.mInputBuffers.begin()->second;
                    handleMetadata(sequence, srcBuf, outBuf);
            }

            for (auto &it : mBufferConsumerList) {
                it->onFrameAvailable(port, outBuf);
            }
            eventDataFrame.sequence = outBuf->getSequence();
            eventDataFrame.timestamp.tv_sec = outBuf->getTimestamp().tv_sec;
            eventDataFrame.timestamp.tv_usec = outBuf->getTimestamp().tv_usec;
        } else {
            LOG1("Frame %ld is being skipped.", sequence);
        }
    }

    if (eventDataFrame.sequence >= 0) {
        EventData frameData;
        frameData.type = EVENT_PSYS_FRAME;
        frameData.buffer = nullptr;
        frameData.data.frame.sequence = eventDataFrame.sequence;
        frameData.data.frame.timestamp.tv_sec = eventDataFrame.timestamp.tv_sec;
        frameData.data.frame.timestamp.tv_usec = eventDataFrame.timestamp.tv_usec;
        notifyListeners(frameData);
    }

    long settingSequence = getSettingSequence(result.mOutputBuffers);
    bool holdOnInput = needHoldOnInputFrame(settingSequence, sequence);
    // Return buffer only if the buffer is not used in the future.
    if (!holdOnInput && mBufferProducer) {
        for (const auto& src : result.mInputBuffers) {
            mBufferProducer->qbuf(src.first, src.second);
        }
    }

    AutoMutex l(mBufferQueueLock);
    long oldest = mSequenceInflight.front();
    if (sequence != oldest) {
        // The output buffer should always be FIFO.
        LOGW("The sequence should be %ld, but it's %ld", oldest, sequence);
    }

    mSequenceInflight.pop();
    if (mSequenceInflight.empty()) {
        mFrameDoneSignal.signal();
    }
}

void PSysProcessor::outputRawImage(shared_ptr<CameraBuffer> &srcBuf, shared_ptr<CameraBuffer> &dstBuf)
{
    if ((srcBuf == nullptr) || (dstBuf == nullptr)) {
        return;
    }

    // Copy from source buffer
    int srcBufferSize = srcBuf->getBufferSize();
    int srcMemoryType = srcBuf->getMemory();
    void* pSrcBuf = (srcMemoryType == V4L2_MEMORY_DMABUF)
                    ? CameraBuffer::mapDmaBufferAddr(srcBuf->getFd(), srcBufferSize)
                    : srcBuf->getBufferAddr();

    int dstBufferSize = dstBuf->getBufferSize();
    int dstMemoryType = dstBuf->getMemory();
    void* pDstBuf = (dstMemoryType == V4L2_MEMORY_DMABUF)
                    ? CameraBuffer::mapDmaBufferAddr(dstBuf->getFd(), dstBufferSize)
                    : dstBuf->getBufferAddr();

    MEMCPY_S(pDstBuf, dstBufferSize, pSrcBuf, srcBufferSize);

    if (srcMemoryType == V4L2_MEMORY_DMABUF) {
        CameraBuffer::unmapDmaBufferAddr(pSrcBuf, srcBufferSize);
    }

    if (dstMemoryType == V4L2_MEMORY_DMABUF) {
        CameraBuffer::unmapDmaBufferAddr(pDstBuf, dstBufferSize);
    }

    // Send output buffer to its consumer
    for (auto &it : mBufferConsumerList) {
        it->onFrameAvailable(mRawPort, dstBuf);
    }
}

} //namespace icamera
