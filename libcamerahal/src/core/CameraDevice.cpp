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

#define LOG_TAG "CameraDevice"

#include <vector>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

#ifndef BYPASS_MODE
#include "GraphConfig.h"
#endif
#include "ICamera.h"
#include "PlatformData.h"
#include "CameraDevice.h"
#include "V4l2DeviceFactory.h"
#include "I3AControlFactory.h"
// FILE_SOURCE_S
#include "FileSource.h"
// FILE_SOURCE_E

namespace icamera {

CameraDevice::CameraDevice(int cameraId) :
    mState(DEVICE_UNINIT),
    mCameraId(cameraId),
    mStreamNum(0)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, cameraId:%d", __func__, mCameraId);

    CLEAR(mStreams);

    V4l2DeviceFactory::createDeviceFactory(mCameraId);
    CLEAR(mInputConfig);
    mInputConfig.format = -1;

    mCsiMetaDevice = new CsiMetaDevice(mCameraId);

    mProducer = createBufferProducer();

    mSofSource = new SofSource(mCameraId);

    mPerframeControlSupport = PlatformData::isFeatureSupported(mCameraId, PER_FRAME_CONTROL);

    mParamGenerator = new ParameterGenerator(mCameraId);

    mLensCtrl = new LensHw(mCameraId);
    mSensorCtrl = SensorHwCtrl::createSensorCtrl(mCameraId);

    m3AControl = I3AControlFactory::createI3AControl(mCameraId, mSensorCtrl, mLensCtrl);
    mRequestThread = new RequestThread(mCameraId);
    mRequestThread->registerListener(EVENT_PROCESS_REQUEST, this);
    mRequestThread->registerListener(EVENT_DEVICE_RECONFIGURE, this);

    mProcessorManager = new ProcessorManager(mCameraId);

#if defined(USE_STATIC_GRAPH)
    mGCM = IGraphConfigManager::getInstance(mCameraId);
#else
    if (PlatformData::getGraphConfigNodes(mCameraId)) {
        mGCM = IGraphConfigManager::getInstance(mCameraId);
    } else {
        mGCM = nullptr;
    }
#endif

    const char *CAMERA_HAL_DQBUF_TIMEDOUT = "cameraDqbufTimedout";
    char *dqbufTimedoutStr = getenv(CAMERA_HAL_DQBUF_TIMEDOUT);
    if (dqbufTimedoutStr == nullptr)
        mDqbufTimedout = false;
    else if (strncmp(dqbufTimedoutStr, "true", 4) || strncmp(dqbufTimedoutStr, "TRUE", 4))
        mDqbufTimedout = true;
    else
        mDqbufTimedout = false;
}

CameraDevice::~CameraDevice()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);
    AutoMutex   m(mDeviceLock);

    // Clear the media control when close the device.
    MediaCtlConf *mc = PlatformData::getMediaCtlConf(mCameraId);
    if (mc) {
        MediaControl::getInstance()->mediaCtlClear(mCameraId, mc);
    }

    mRequestThread->removeListener(EVENT_PROCESS_REQUEST, this);
    mRequestThread->removeListener(EVENT_DEVICE_RECONFIGURE, this);

    delete mProcessorManager;

    for (int i = 0; i < MAX_STREAM_NUMBER; i++)
        delete mStreams[i];

    delete mLensCtrl;
    delete m3AControl;
    delete mSensorCtrl;
    delete mParamGenerator;
    delete mSofSource;
    delete mProducer;
    delete mCsiMetaDevice;
    delete mRequestThread;

    V4l2DeviceFactory::releaseDeviceFactory(mCameraId);
    IGraphConfigManager::releaseInstance(mCameraId);
}

int CameraDevice::init()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d, mState:%d", __func__, mCameraId, mState);
    AutoMutex   m(mDeviceLock);

    int ret = mProducer->init();
    Check(ret < 0, ret, "%s: Init capture unit failed", __func__);

    ret = mCsiMetaDevice->init();
    Check(ret != OK, ret, "@%s: init csi meta device failed", __func__);

    ret = mSofSource->init();
    Check(ret != OK, ret, "@%s: init sync manager failed", __func__);

    initDefaultParameters();

    ret = m3AControl->init();
    Check((ret != OK), ret, "%s: Init 3A Unit falied", __func__);

    ret = mLensCtrl->init();
    Check((ret != OK), ret, "%s: Init Lens falied", __func__);

    mRequestThread->run("RequestThread", PRIORITY_NORMAL);

    mState = DEVICE_INIT;
    return ret;
}

void CameraDevice::deinit()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d, mState:%d", __func__, mCameraId, mState);
    AutoMutex   m(mDeviceLock);

    //deinit should not be call in UNINIT or START STATE
    if (mState == DEVICE_UNINIT) {
        //Do nothing
        return;
    }

    m3AControl->stop();

    if (mState == DEVICE_START) {
        //stop first
        stopLocked();
    }

    deleteStreams();

    mProcessorManager->deleteProcessors();

    m3AControl->deinit();

    mSofSource->deinit();

    mCsiMetaDevice->deinit();

    mProducer->deinit();

    mRequestThread->requestExit();
    mRequestThread->join();

    mState = DEVICE_UNINIT;
}

CaptureUnit* CameraDevice::createBufferProducer()
{
    // FILE_SOURCE_S
    if (PlatformData::isFileSourceEnabled()) {
        return new FileSource(mCameraId);
    }
    // FILE_SOURCE_E
    return new CaptureUnit(mCameraId);
}

void CameraDevice::bindListeners()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    vector<EventListener*> statsListenerList = m3AControl->getStatsEventListener();
    for (auto statsListener : statsListenerList) {
        mProducer->registerListener(EVENT_ISA_STATS_BUF_READY, statsListener);

        for (auto& item : mProcessors) {
            // Subscribe PSys statistics.
            item->registerListener(EVENT_PSYS_STATS_BUF_READY, statsListener);
            item->registerListener(EVENT_PSYS_STATS_SIS_BUF_READY, statsListener);
        }
    }

    for (auto& item : mProcessors) {
        // Subscribe PSys statistics.
        item->registerListener(EVENT_PSYS_STATS_BUF_READY, mRequestThread);
    }

    vector<EventListener*> sofListenerList = m3AControl->getSofEventListener();
    for (auto sofListener : sofListenerList) {
        mSofSource->registerListener(EVENT_ISYS_SOF, sofListener);
        // FILE_SOURCE_S
        if (PlatformData::isFileSourceEnabled()) {
            // File source needs to produce SOF event as well when it's enabled.
            mProducer->registerListener(EVENT_ISYS_SOF, sofListener);
        }
        // FILE_SOURCE_Es
    }

    // Listen to meta data when enabled
    if (mCsiMetaDevice->isEnabled()) {
        for (auto& item : mProcessors) {
            mCsiMetaDevice->registerListener(EVENT_META, item);
        }
    }

    if (mPerframeControlSupport) {
        mProcessors.back()->registerListener(EVENT_PSYS_FRAME, mRequestThread);
    } else {
        mProducer->registerListener(EVENT_ISYS_FRAME, mRequestThread);
    }
}

void CameraDevice::unbindListeners()
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    vector<EventListener*> statsListenerList = m3AControl->getStatsEventListener();
    for (auto statsListener : statsListenerList) {
        mProducer->removeListener(EVENT_ISA_STATS_BUF_READY, statsListener);

        for (auto& item : mProcessors) {
            item->removeListener(EVENT_PSYS_STATS_BUF_READY, statsListener);
            item->removeListener(EVENT_PSYS_STATS_SIS_BUF_READY, statsListener);
        }
    }

    for (auto& item : mProcessors) {
        // Subscribe PSys statistics.
        item->removeListener(EVENT_PSYS_STATS_BUF_READY, mRequestThread);
    }

    vector<EventListener*> sofListenerList = m3AControl->getSofEventListener();
    for (auto sofListener : sofListenerList) {
        mSofSource->removeListener(EVENT_ISYS_SOF, sofListener);
        // FILE_SOURCE_S
        if (PlatformData::isFileSourceEnabled()) {
            mProducer->removeListener(EVENT_ISYS_SOF, sofListener);
        }
        // FILE_SOURCE_E
    }

    if (mCsiMetaDevice->isEnabled()) {
        for (auto& item : mProcessors) {
            mCsiMetaDevice->removeListener(EVENT_META, item);
        }
    }

    if (mPerframeControlSupport) {
        mProcessors.back()->removeListener(EVENT_PSYS_FRAME, mRequestThread);
    } else {
        mProducer->removeListener(EVENT_ISYS_FRAME, mRequestThread);
    }
}

int CameraDevice::configureInput(const stream_t *inputConfig)
{
    PERF_CAMERA_ATRACE();

    AutoMutex lock(mDeviceLock);
    mInputConfig = *inputConfig;

    return OK;
}

int CameraDevice::configure(stream_config_t *streamList)
{
    PERF_CAMERA_ATRACE();

    int numOfStreams = streamList->num_streams;
    Check(!streamList->streams, BAD_VALUE, "%s: No valid stream config", __func__);
    Check(numOfStreams > MAX_STREAM_NUMBER || numOfStreams <= 0, BAD_VALUE,
          "%s: The requested stream number(%d) is invalid. Should be between [1-%d]",
          __func__, numOfStreams, MAX_STREAM_NUMBER);

    AutoMutex lock(mDeviceLock);

    Check((mState != DEVICE_STOP) && (mState != DEVICE_INIT), INVALID_OPERATION,
          "%s: Add streams in wrong state %d", __func__, mState);

    mRequestThread->configure(streamList);

    // Use concrete ISP mode from request thread for full and auto switch
    if (PlatformData::getAutoSwitchType(mCameraId) == AUTO_SWITCH_FULL &&
        (ConfigMode)(streamList->operation_mode) == CAMERA_STREAM_CONFIGURATION_MODE_AUTO) {
        stream_config_t requestStreamList = mRequestThread->getStreamConfig();
        LOG2("%s: for full and auto switch, use concrete config mode %u from request thread.",
             __func__, requestStreamList.operation_mode);
        return configureL(&requestStreamList);
    }

    return configureL(streamList);
}

int CameraDevice::configureL(stream_config_t *streamList, bool clean)
{
    LOG1("@%s, mCameraId:%d, operation_mode %x", __func__, mCameraId, (ConfigMode)streamList->operation_mode);

    int ret = analyzeStream(streamList);
    Check(ret != OK, ret, "@%s, analyzeStream failed", __func__);

    // If configured before, destroy current streams first.
    if (mStreamNum > 0 && clean) {
        deleteStreams();
    }
    mProcessorManager->deleteProcessors();

    // Clear all previous added listeners.
    mProducer->removeAllFrameAvailableListener();

    int mcId = -1;
    if (mGCM != nullptr) {
       /*
        * This is a WA for support user to dump SGRBG12 and NV12 data together.
        * In this case, should remove SGRBG12 stream from streamlist.
        */
        bool hasRaw = false;
        for (int i = 0; i < streamList->num_streams; i++) {
            if (streamList->streams[i].format == V4L2_PIX_FMT_SGRBG12) {
                hasRaw = true;
                break;
            }
        }

        if (hasRaw && (streamList->num_streams > 1)) {
            stream_config_t tmpStreamList;
            stream_t tmpStreams[MAX_STREAM_NUMBER];

            tmpStreamList.streams = tmpStreams;
            tmpStreamList.operation_mode = streamList->operation_mode;
            tmpStreamList.num_streams = streamList->num_streams - 1;
            for (int i = 0, j = 0; i < tmpStreamList.num_streams; i++, j++) {
                if (streamList->streams[i].format == V4L2_PIX_FMT_SGRBG12) {
                    j++;
                }

                tmpStreamList.streams[i] = streamList->streams[j];
            }

            ret = mGCM->configStreams(&tmpStreamList);
        } else {
            ret = mGCM->configStreams(streamList);
        }

        Check(ret != OK, INVALID_OPERATION, "No matching graph config found");
        mcId = mGCM->getSelectedMcId();
    }

    map<Port, stream_t> producerConfigs = selectProducerConfig(streamList, mcId);
    Check(producerConfigs.empty(), BAD_VALUE, "The config for producer is invalid.");

    bool needProcessor = isProcessorNeeded(streamList, producerConfigs[MAIN_PORT]);
    for (auto& item : producerConfigs) {
        LOG1("Producer config for port:%d, fmt:%s (%dx%d), needProcessor=%d", item.first,
             CameraUtils::format2string(item.second.format),
             item.second.width, item.second.height, needProcessor);
        // Only V4L2_MEMORY_MMAP is supported when using post processor
        if (needProcessor) {
            item.second.memType = V4L2_MEMORY_MMAP;
        }
    }

    vector<ConfigMode> configModes;
    PlatformData::getConfigModesByOperationMode(mCameraId, streamList->operation_mode, configModes);

    ret = mProducer->configure(producerConfigs, configModes);
    Check(ret < 0, BAD_VALUE, "@%s Device Configure failed", __func__);

    ret = mCsiMetaDevice->configure();
    Check(ret != OK, ret, "@%s failed to configure CSI meta device", __func__);

// CRL_MODULE_S
    ret = mSensorCtrl->configure();
    Check(ret != OK, ret, "@%s failed to configure sensor HW", __func__);
// CRL_MODULE_E

    ret = mSofSource->configure();
    Check(ret != OK, ret, "@%s failed to configure SOF source device", __func__);

    m3AControl->configure(streamList);

    if (needProcessor) {
        mProcessors = mProcessorManager->createProcessors(mInputConfig.format, producerConfigs,
                                                          mStreamIdToPortMap,
                                                          streamList, mParameter, mParamGenerator);
        ret = mProcessorManager->configureProcessors(configModes,
                                                     mProducer, mParameter);
        Check(ret != OK, ret, "@%s configure post processor failed with:%d", __func__, ret);
    }

    if (clean) {
        ret = createStreams(streamList);
        Check(ret < 0, ret, "@%s create stream failed with %d", __func__, ret);
    }

    ret = bindStreams(streamList);
    Check(ret < 0, ret, "@%s bind stream failed with %d", __func__, ret);

    mState = DEVICE_CONFIGURE;
    return OK;
}

/**
 * Select the producer's config from the supported list.
 *
 * How to decide the producer's config?
 * 1. The producer's config can only be one of the combination from ISYS supported format and
 *    resolution list.
 * 2. Try to use the same config as user's required.
 * 3. If the ISYS supported format and resolution cannot satisfy user's requirement, then use
 *    the closest one, and let post processor do the conversion.
 */
map<Port, stream_t> CameraDevice::selectProducerConfig(const stream_config_t *streamList, int mcId)
{
    // Use the biggest stream to configure the producer.
    stream_t biggestStream = streamList->streams[mSortedStreamIds[0]];
    map<Port, stream_t> producerConfigs;

    /*
     * According to the stream info and operation mode to select MediaCtlConf.
     * and isys output format. If inputFmt is given and supported, we use it as isys output format.
     */
    int inputFmt = mInputConfig.format;
    int iSysFmt = biggestStream.format;
    if (inputFmt != -1) {
        if (!PlatformData::isISysSupportedFormat(mCameraId, inputFmt)) {
            LOGE("The given ISYS format %s is unsupported.", CameraUtils::pixelCode2String(inputFmt));
            return producerConfigs;
        }
        iSysFmt = inputFmt;
    }

    // Use CSI output to select MC config
    vector <ConfigMode> configModes;
    PlatformData::getConfigModesByOperationMode(mCameraId, streamList->operation_mode,
                                             configModes);
    stream_t matchedStream = biggestStream;
    if (!configModes.empty() && mGCM != nullptr) {
        shared_ptr<IGraphConfig> gc = mGCM->getGraphConfig(configModes[0]);
        if (gc) {
            camera_resolution_t csiOutput = {0, 0};
            gc->getCSIOutputResolution(csiOutput);
            if (csiOutput.width > 0 && csiOutput.height > 0) {
                matchedStream.width = csiOutput.width;
                matchedStream.height = csiOutput.height;
            }
        }
    }

    camera_crop_region_t cropRegion;
    int ret = mParameter.getCropRegion(cropRegion);
    if ((ret == OK) && (cropRegion.flag == 1)) {
        PlatformData::selectMcConf(mCameraId, mInputConfig,
                                  (ConfigMode)streamList->operation_mode, mcId);
    } else {
        PlatformData::selectMcConf(mCameraId, matchedStream,
                                  (ConfigMode)streamList->operation_mode, mcId);
    }

    PlatformData::selectISysFormat(mCameraId, iSysFmt);

    // Use the ISYS output if it's provided in media config section of config file.
    stream_t mainConfig = PlatformData::getISysOutputByPort(mCameraId, MAIN_PORT);
    mainConfig.memType = biggestStream.memType;
    mainConfig.field = biggestStream.field;

    if (mainConfig.width != 0 && mainConfig.height != 0) {
        if (isStillDuringVideo(streamList) && PlatformData::isISysScaleEnabled(mCameraId)) {
            stream_t secondConfig = PlatformData::getISysOutputByPort(mCameraId, SECOND_PORT);
            Check(secondConfig.width == 0 || secondConfig.height == 0, producerConfigs,
                  "No second port ISYS output config provided.");

            secondConfig.memType = mainConfig.memType;
            secondConfig.field = mainConfig.field;
            producerConfigs[SECOND_PORT] = secondConfig;
        }
        producerConfigs[MAIN_PORT] = mainConfig;

        return producerConfigs;
    }

    int inputWidth = mInputConfig.width;
    int inputHeight = mInputConfig.height;

    camera_resolution_t producerRes = {inputWidth, inputHeight};
    if (inputWidth == 0 && inputHeight == 0) {
        // Only get the ISYS resolution when input config is not specified.
        producerRes = PlatformData::getISysBestResolution(mCameraId, biggestStream.width,
                                                          biggestStream.height, biggestStream.field);
    } else if (!PlatformData::isISysSupportedResolution(mCameraId, producerRes)) {
        LOGE("The stream config: (%dx%d) is not supported.", inputWidth, inputHeight);
        return producerConfigs;
    }

    mainConfig.format = PlatformData::getISysFormat(mCameraId);
    mainConfig.width = producerRes.width;
    // Update the height according to the field.
    mainConfig.height = CameraUtils::getInterlaceHeight(mainConfig.field, producerRes.height);

    // Need configure second port when it's SDV case and ISA scale is enabled.
    if (isStillDuringVideo(streamList) && PlatformData::isISysScaleEnabled(mCameraId)) {
        stream_t secondConfig = PlatformData::getIsaScaleRawConfig(mCameraId);
        Check(secondConfig.width == 0 || secondConfig.height == 0, producerConfigs,
              "Invalid config for ISA scale raw device.");
        secondConfig.memType = mainConfig.memType;
        secondConfig.field = mainConfig.field;
        producerConfigs[SECOND_PORT] = secondConfig;
    }

    // In DOL case isys scale must be disabled, and 2 ports have same
    // configuration with main port
    if (PlatformData::isDolShortEnabled(mCameraId))
        producerConfigs[SECOND_PORT] = mainConfig;

    if (PlatformData::isDolMediumEnabled(mCameraId))
        producerConfigs[THIRD_PORT] = mainConfig;

    producerConfigs[MAIN_PORT] = mainConfig;

    return producerConfigs;
}

/**
 * Check if post processor is needed.
 * The processor is needed when:
 * 1. At least one of the given streams does not match with the producer's output.
 * 2. To support specific features such as HW weaving or dewarping.
 */
bool CameraDevice::isProcessorNeeded(const stream_config_t *streamList,
                                     const stream_t &producerConfig)
{
    int streamCounts = streamList->num_streams;
    for (int streamId = 0; streamId < streamCounts; streamId++) {
        if (producerConfig.field != V4L2_FIELD_ALTERNATE) {
            camera_crop_region_t cropRegion;
            int ret = mParameter.getCropRegion(cropRegion);
            if ((ret == OK) && (cropRegion.flag == 1)) return true;

            if (producerConfig.width != streamList->streams[streamId].width ||
                producerConfig.height != streamList->streams[streamId].height ||
                producerConfig.format != streamList->streams[streamId].format) {
                return true;
            }
        }

        if (streamList->streams[streamId].field != V4L2_FIELD_ANY) {
            camera_deinterlace_mode_t mode = DEINTERLACE_OFF;
            mParameter.getDeinterlaceMode(mode);
            if (mode == DEINTERLACE_WEAVING) {
                return true;
            }
        }
    }

    camera_fisheye_dewarping_mode_t dewarping_mode = FISHEYE_DEWARPING_OFF;
    mParameter.getFisheyeDewarpingMode(dewarping_mode);
    if (dewarping_mode > FISHEYE_DEWARPING_OFF) {
        return true;
    }

    return false;
}

/**
 * Return true only if there are both still and video stream configured.
 */
bool CameraDevice::isStillDuringVideo(const stream_config_t *streamList)
{
    bool containStill = false;
    bool containVideo = false;
    for (int streamId = 0; streamId < streamList->num_streams; streamId++) {
        switch (streamList->streams[streamId].usage) {
        case CAMERA_STREAM_PREVIEW:
        case CAMERA_STREAM_VIDEO_CAPTURE:
            containVideo = true;
            break;
        case CAMERA_STREAM_STILL_CAPTURE:
            containStill = true;
            break;
        default:
            break;
        }
    }

    return (containStill && containVideo);
}

int CameraDevice::createStreams(stream_config_t *streamList)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    int streamCounts = streamList->num_streams;
    for (int streamId = 0; streamId < streamCounts; streamId++) {
        stream_t& streamConf = streamList->streams[streamId];
        LOG1("@%s, stream_number:%d, stream configure: format:%s (%dx%d)", __func__, streamCounts,
             CameraUtils::pixelCode2String(streamConf.format), streamConf.width, streamConf.height);

        streamConf.id = streamId;
        streamConf.max_buffers = MAX_BUFFER_COUNT;
        CameraStream *stream = new CameraStream(mCameraId, streamId, streamConf);
        stream->registerListener(EVENT_FRAME_AVAILABLE, mRequestThread);
        mStreams[streamId] = stream;
        mStreamNum++;

        LOG2("@%s: automation checkpoint: interlaced: %d", __func__, streamConf.field);
    }

    return OK;
}

/**
 * According resolution to store the streamId in descending order.
 * Use this order to bind stream to port, and set output Port mapping
 */
int CameraDevice::analyzeStream(stream_config_t *streamList)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    mSortedStreamIds.clear();
    mStreamIdToPortMap.clear();

    for (int i = 0; i < streamList->num_streams; i++) {
        const stream_t& streamConf = streamList->streams[i];

        camera_crop_region_t cropRegion;
        int ret = mParameter.getCropRegion(cropRegion);
        if (ret != OK || cropRegion.flag == 0) {
            if (!PlatformData::isSupportedStream(mCameraId, streamConf)) {
                LOGE("Stream config is not supported. format:%s (%dx%d)",
                    CameraUtils::pixelCode2String(streamConf.format),
                    streamConf.width, streamConf.height);
                    return BAD_VALUE;
            }
        }

        bool saved = false;
        // Store the streamId in descending order.
        for (size_t j = 0; j < mSortedStreamIds.size(); j++) {
            const stream_t& stream = streamList->streams[mSortedStreamIds[j]];
            if (streamConf.width * streamConf.height > stream.width * stream.height) {
                mSortedStreamIds.insert((mSortedStreamIds.begin() + j), i);
                saved = true;
                break;
            }
        }
        if (!saved)
            mSortedStreamIds.push_back(i);
    }

    const Port kPorts[] = {MAIN_PORT, SECOND_PORT, THIRD_PORT, FORTH_PORT};
    for (size_t i = 0; i < mSortedStreamIds.size(); i++) {
        mStreamIdToPortMap[mSortedStreamIds[i]] = kPorts[i];

        // Dump the stream info by descending order.
        const stream_t& stream = streamList->streams[mSortedStreamIds[i]];
        LOG1("%s  streamId: %d, %dx%d(%s)", __func__, mSortedStreamIds[i],
                stream.width, stream.height, CameraUtils::format2string(stream.format));
    }

    return OK;
}

/**
 * Bind all streams to their producers and to the correct port.
 *
 * Bind the streams to Port in resolution descending order:
 * Stream with max resolution            --> MAIN_PORT
 * Stream with intermediate resolution   --> SECOND_PORT
 * Stream with min resolution            --> THIRD_PORT
 */
int CameraDevice::bindStreams(stream_config_t *streamList)
{
    for (auto& iter : mStreamIdToPortMap) {
        mStreams[iter.first]->setPort(iter.second);

        // If no post processors, bind the stream to the producer.
        if (mProcessors.empty()) {
            mStreams[iter.first]->setBufferProducer(mProducer);
        } else {
            mStreams[iter.first]->setBufferProducer(mProcessors.back());
        }
    }

    return OK;
}

int CameraDevice::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d, mState:%d", __func__, mCameraId, mState);
    LOGDEVSTART("@%s, mCameraId:%d enter", __func__, mCameraId);

    // Not protected by mDeviceLock because it is required in qbufL()
    mRequestThread->wait1stRequestDone();

    AutoMutex   m(mDeviceLock);
    Check(mState != DEVICE_BUFFER_READY, BAD_VALUE, "start camera in wrong status %d", mState);
    Check(mStreamNum == 0, BAD_VALUE, "@%s: device doesn't add any stream yet.", __func__);

    int ret = startLocked();
    if (ret != OK) {
        LOGE("Camera device starts failed.");
        stopLocked();  // There is error happened, stop all related units.
        return INVALID_OPERATION;
    }

    mState = DEVICE_START;
    LOGDEVSTART("@%s, mCameraId:%d exit", __func__, mCameraId);
    return OK;
}

int CameraDevice::stop()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d, mState:%d", __func__, mCameraId, mState);
    AutoMutex   m(mDeviceLock);

    mRequestThread->clearRequests();

    m3AControl->stop();

    if (mState == DEVICE_START)
        stopLocked();

    mState = DEVICE_STOP;

    return OK;
}

//No Lock for this fuction as it doesn't update any class member
int CameraDevice::allocateMemory(camera_buffer_t *ubuffer)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);
    Check(mState < DEVICE_CONFIGURE, BAD_VALUE, "@%s: Wrong state id %d", __func__, mState);
    Check(ubuffer->s.id < 0 || ubuffer->s.id >= mStreamNum, BAD_VALUE,
          "@%s: Wrong stream id %d", __func__, ubuffer->s.id);

    int ret = mStreams[ubuffer->s.id]->allocateMemory(ubuffer);
    Check(ret < 0, ret, "@%s: failed, index: %d", __func__, ubuffer->index);

    return ret;
}

/**
 * Delegate it to RequestThread, make RequestThread manage all buffer related actions.
 */
int CameraDevice::dqbuf(int streamId, camera_buffer_t **ubuffer, Parameters* settings)
{
    Check(streamId < 0 || streamId > mStreamNum, BAD_VALUE,
          "@%s: the given stream(%d) is invalid.", __func__, streamId);

    LOG2("@%s, camera id:%d, stream id:%d", __func__, mCameraId, streamId);

    int ret = mRequestThread->waitFrame(streamId, ubuffer);
    if (mDqbufTimedout) {
        if (ret == TIMED_OUT)
            return ret;
    } else {
        while (ret == TIMED_OUT)
            ret = mRequestThread->waitFrame(streamId, ubuffer);
    }

    Check(!*ubuffer || ret != OK, BAD_VALUE, "failed to get ubuffer from stream %d", streamId);

    // Update and keep latest parameters, copy to settings when needed.
    if (mPerframeControlSupport) {
        ret = mParamGenerator->getParameters((*ubuffer)->sequence, &mResultParameter);
    } else {
        // For non-perframe, update the result besed on the latest parameters.
        mResultParameter = mParameter;
        ret = mParamGenerator->getParameters((*ubuffer)->sequence, &mResultParameter, true);
    }

    if (settings) {
        bool mergeResultOnly = false;
        bool still = ((*ubuffer)->s.usage == CAMERA_STREAM_STILL_CAPTURE);
        if (!mPerframeControlSupport) {
            // For non-perframe, update the result besed on the latest parameters.
            *settings = mParameter;
            mergeResultOnly = true;
        }

        ret = mParamGenerator->getParameters((*ubuffer)->sequence, settings,
                                             mergeResultOnly, still);
    }

    return ret;
}

int CameraDevice::handleQueueBuffer(int bufferNum, camera_buffer_t **ubuffer, long sequence)
{
    LOG2("@%s, mCameraId:%d, sequence = %ld", __func__, mCameraId, sequence);
    Check(mState < DEVICE_CONFIGURE, BAD_VALUE,"@%s: Wrong state id %d", __func__, mState);

    // All streams need to be queued with either a real buffer from user or an empty buffer.
    for (int streamId = 0; streamId < mStreamNum; streamId++) {
        bool isBufferQueued = false;
        Check(mStreams[streamId] == nullptr, BAD_VALUE,
              "@%s: stream %d is nullptr", __func__, streamId);

        // Find if user has queued a buffer for mStreams[streamId].
        for (int bufferId = 0; bufferId < bufferNum; bufferId++) {
            camera_buffer_t* buffer = ubuffer[bufferId];
            int streamIdInBuf = buffer->s.id;
            Check(streamIdInBuf < 0 || streamIdInBuf > mStreamNum, BAD_VALUE,
                "@%s: Wrong stream id %d", __func__, streamIdInBuf);

            if (streamIdInBuf == streamId) {
                int ret = mStreams[streamId]->qbuf(buffer, sequence);
                Check(ret < 0, ret, "@%s: queue buffer:%p failed:%d", __func__, buffer, ret);
                isBufferQueued = true;
                break;
            }
        }

        // If streamId is not found in buffers queued by user, then we need to queue
        // an empty buffer to keep the BufferQueue run.
        if (!isBufferQueued) {
            int ret = mStreams[streamId]->qbuf(nullptr, sequence);
            Check(ret < 0, ret, "@%s: queue empty buffer failed:%d", __func__, ret);
        }
    }

    return OK;
}

int CameraDevice::registerBuffer(camera_buffer_t **ubuffer, int bufferNum)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);
    Check(mState < DEVICE_CONFIGURE, BAD_VALUE,"@%s: Wrong state id %d", __func__, mState);
    if (mProcessors.empty()) return OK;

    for (int bufferId = 0; bufferId < bufferNum; bufferId++) {
        camera_buffer_t *buffer = ubuffer[bufferId];
        Check(buffer == nullptr, BAD_VALUE, "@%s, the queue ubuffer %d is NULL", __func__, bufferId);
        int streamIdInBuf = buffer->s.id;
        Check(streamIdInBuf < 0 || streamIdInBuf > mStreamNum, BAD_VALUE,
                "@%s: Wrong stream id %d", __func__, streamIdInBuf);
        shared_ptr<CameraBuffer> camBuffer =
            mStreams[streamIdInBuf]->userBufferToCameraBuffer(buffer);
        for (auto& iter : mStreamIdToPortMap) {
            // Register buffers to the last processor
            if (iter.first == streamIdInBuf) {
                BufferQueue *processor = mProcessors.back();
                processor->registerUserOutputBufs(iter.second, camBuffer);
                break;
            }
        }
    }

    return OK;
}

int CameraDevice::qbuf(camera_buffer_t **ubuffer,
                       int bufferNum, const Parameters *settings)
{
    LOG2("@%s, mCameraId:%d", __func__, mCameraId);

    {
        AutoMutex   m(mDeviceLock);
        if (mState == DEVICE_CONFIGURE || mState == DEVICE_STOP) {
            // Start 3A here then the HAL can run 3A for request
            int ret = m3AControl->start();
            Check((ret != OK), BAD_VALUE, "Start 3a unit failed with ret:%d.", ret);

            mState = DEVICE_BUFFER_READY;
        }
    }

    if (mState != DEVICE_START && PlatformData::isNeedToPreRegisterBuffer(mCameraId)) {
        registerBuffer(ubuffer, bufferNum);
    }

    // Make sure request's configure mode is updated by latest result param if no settings
    if (!settings) {
        mRequestThread->setConfigureModeByParam(mResultParameter);
    }

    return mRequestThread->processRequest(bufferNum, ubuffer, settings);
}

long CameraDevice::fetchCaptureSettings(const Parameters *params)
{
    long settingsSequence = -1;

    if (mPerframeControlSupport && params)
        m3AControl->run3A(&settingsSequence);
    else
        m3AControl->run3A(nullptr);

    if (mPerframeControlSupport) {
        mParamGenerator->saveParameters(settingsSequence, mParameter);
    }

    // Return valid settingSequence only when it requires per-frame control
    return settingsSequence;
}

int CameraDevice::getParameters(Parameters& param)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s mCameraId:%d", __func__, mCameraId);
    AutoMutex   m(mDeviceLock);

    param = mParameter;

    mParamGenerator->getParameters(-1, &param, true);

    for (auto& item : mProcessors) {
        item->getParameters(param);
    }

    return OK;
}

int CameraDevice::setParameters(const Parameters& param)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s mCameraId:%d", __func__, mCameraId);
    AutoMutex   m(mDeviceLock);

    int ret = mRequestThread->processParameters(param);
    ret |= setParametersL(param);

    return ret;
}

int CameraDevice::setParametersL(const Parameters& param)
{
    // Merge given param into internal unique mParameter
    mParameter.merge(param);

    int ret = m3AControl->setParameters(mParameter);
    for (auto& item : mProcessors) {
        item->setParameters(mParameter);
    }

    return ret;
}

//Private Functions, these functions are called with device lock hold

//Destroy all the streams
void CameraDevice::deleteStreams()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s mCameraId:%d, streams:%d", __func__, mCameraId, mStreamNum);

    for (int streamId = 0; streamId < mStreamNum; streamId++) {
        mStreams[streamId]->stop();
        delete mStreams[streamId];
        mStreams[streamId] = nullptr;
    }
    mStreamNum = 0;
}

// Internal start without lock hold
int CameraDevice::startLocked()
{
    int ret = OK;

    bindListeners();

    //Start all the streams
    for(int i = 0; i < mStreamNum; i++) {
        ret = mStreams[i]->start();
        Check(ret < 0, BAD_VALUE, "Start stream %d failed with ret:%d.", i, ret);
    }

    for (auto& item : mProcessors) {
        ret = item->start();
        Check((ret < 0), BAD_VALUE, "Start image processor failed with ret:%d.", ret);
    }

    //Start the CaptureUnit for streamon
    ret = mProducer->start();
    Check((ret < 0), BAD_VALUE, "Start capture unit failed with ret:%d.", ret);

    ret = mCsiMetaDevice->start();
    Check((ret != OK), BAD_VALUE, "Start CSI meta failed with ret:%d.", ret);

    ret = mSofSource->start();
    Check((ret != OK), BAD_VALUE, "Start SOF event source failed with ret:%d.", ret);

    return OK;
}

// Internal stop without lock hold
int CameraDevice::stopLocked()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    unbindListeners();

    mSofSource->stop();

    mCsiMetaDevice->stop();

    //Stop the CaptureUnit for streamon
    mProducer->stop();

    for (auto& item : mProcessors) {
        item->stop();
    }

    mParamGenerator->reset();

    return OK;
}

int CameraDevice::reconfigure(stream_config_t *streamList)
{
    AutoMutex   m(mDeviceLock);

    int ret = OK;

    LOG2("%s: switch type: %d, new mode:%d", __func__,
        PlatformData::getAutoSwitchType(mCameraId), streamList->operation_mode);

    if (PlatformData::getAutoSwitchType(mCameraId) == AUTO_SWITCH_FULL) {
        // Wait and return all user buffers in all streams firstly
        for (int streamId = 0; streamId < mStreamNum; streamId++) {
            mStreams[streamId]->waitToReturnAllUserBufffers();
        }

        LOG2("%s: all streams stopped", __func__);

        // Stop and clean what needed.
        m3AControl->stop();

        if (mState == DEVICE_START)
            stopLocked();

        mState = DEVICE_STOP;

        for (int streamId = 0; streamId < mStreamNum; streamId++) {
            mStreams[streamId]->stop();
        }

        mProcessorManager->deleteProcessors();

        m3AControl->deinit();

        mSofSource->deinit();

        mCsiMetaDevice->deinit();

        mProducer->deinit();

        /* TODO: Currently kernel have issue and need reopen subdevices
         * when stream off and on. Remove below delete and recreate code
         * when all kernel issues got fixed.
         */
        // Delete related components and v4l2 devices
        delete mLensCtrl;
        delete m3AControl;
        delete mSensorCtrl;
        delete mSofSource;
        delete mProducer;
        delete mCsiMetaDevice;

        V4l2DeviceFactory::releaseDeviceFactory(mCameraId);

        // Re-create related components and v4l2 devices
        mCsiMetaDevice = new CsiMetaDevice(mCameraId);
        mProducer = createBufferProducer();
        mSofSource = new SofSource(mCameraId);
        mLensCtrl = new LensHw(mCameraId);
        mSensorCtrl = SensorHwCtrl::createSensorCtrl(mCameraId);
        m3AControl = I3AControlFactory::createI3AControl(mCameraId, mSensorCtrl, mLensCtrl);

        // Init and config with new mode
        int ret = mProducer->init();
        Check(ret < 0, ret, "%s: Init capture unit failed", __func__);

        ret = mCsiMetaDevice->init();
        Check(ret != OK, ret, "@%s: init csi meta device failed", __func__);

        ret = mSofSource->init();
        Check(ret != OK, ret, "@%s: init sync manager failed", __func__);

        initDefaultParameters();

        ret = m3AControl->init();
        Check((ret != OK), ret, "%s: Init 3A Unit falied", __func__);

        ret = mLensCtrl->init();
        Check((ret != OK), ret, "%s: Init Lens falied", __func__);

        mState = DEVICE_INIT;

        // Auto switch do not recreate streams.
        configureL(streamList, false);

        ret = m3AControl->setParameters(mParameter);
        for (auto& item : mProcessors) {
            item->setParameters(mParameter);
        }
        Check((ret != OK), ret, "%s: set parameters falied", __func__);

        ret = m3AControl->start();
        Check((ret != OK), BAD_VALUE, "Start 3a unit failed with ret:%d.", ret);

        mState = DEVICE_BUFFER_READY;

        ret = startLocked();
        if (ret != OK) {
            LOGE("Camera device starts failed.");
            stopLocked();  // There is error happened, stop all related units.
            return INVALID_OPERATION;
        }

        mState = DEVICE_START;

        LOG2("%s: reconfigure CameraDevice done", __func__);
    } else {

        /* TODO: scene mode based psys-only auto switch here will be used to
         * replace current auto-switch mechanism in AiqSetting:updateTuningMode,
         * which is for non-DOL sensor auto-switch. The switch stabilization
         * counting in AiqSetting:updateTuningMode will also be replaced by the
         * same mechanism in RequestThread.
         */
        LOG2("%s: reconfigure CameraDevice to new mode %d with psys-only switch",
             __func__, streamList->operation_mode);
    }

    return ret;
}

void CameraDevice::handleEvent(EventData eventData)
{
    LOG2("%s, event type:%d", __func__, eventData.type);

    switch (eventData.type) {
    case EVENT_PROCESS_REQUEST: {
        const EventRequestData& request = eventData.data.request;
        if (request.param) {
            setParametersL(*request.param);
        }

        long sequence = fetchCaptureSettings(request.param);
        handleQueueBuffer(request.bufferNum, request.buffer, sequence);
        break;
    }

    case EVENT_DEVICE_RECONFIGURE: {
        const EventConfigData& config = eventData.data.config;
        reconfigure(config.streamList);
        break;
    }

    default:
        LOGE("Not supported event type:%d", eventData.type);
        break;
    }
}

int CameraDevice::initDefaultParameters()
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s mCameraId:%d", __func__, mCameraId);

    camera_info_t info;
    CLEAR(info);
    PlatformData::getCameraInfo(mCameraId, info);

    // Init mParameter to camera's capability first and then add some others default settings
    mParameter = *info.capability;

    // TODO: Figure out a better place to set default parameters since they may be platform specified.
    camera_range_t fps = {10, 60};
    mParameter.setFpsRange(fps);
    mParameter.setFrameRate(30);

    camera_image_enhancement_t enhancement;
    CLEAR(enhancement); // All use 0 as default
    mParameter.setImageEnhancement(enhancement);

    mParameter.setWeightGridMode(WEIGHT_GRID_AUTO);

    mParameter.setWdrLevel(100);

    mParameter.setFlipMode(FLIP_MODE_NONE);

    mParameter.setRun3ACadence(1);

    mParameter.setYuvColorRangeMode(PlatformData::getYuvColorRangeMode(mCameraId));

    return OK;
}

} //namespace icamera

