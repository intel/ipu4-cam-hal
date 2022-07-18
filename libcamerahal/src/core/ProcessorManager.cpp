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

#define LOG_TAG "ProcessorManager"

#include "ProcessorManager.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

#include "SwImageProcessor.h"

#ifndef BYPASS_MODE


#include "PSysProcessor.h"
// LITE_PROCESSING_S
#include "psyslite/WeavingProcessor.h"
#include "psyslite/CscProcessor.h"
#include "psyslite/ScaleProcessor.h"
#include "psyslite/FisheyeProcessor.h"
// LITE_PROCESSING_E

#endif

namespace icamera {

ProcessorManager::ProcessorManager(int cameraId) :
        mCameraId(cameraId),
        mPsysUsage(PSYS_NOT_USED)
{
    LOG1("@%s, cameraId:%d", __func__, mCameraId);
}

ProcessorManager::~ProcessorManager()
{
    LOG1("@%s, cameraId:%d", __func__, mCameraId);

    deleteProcessors();
}

vector<BufferQueue*> ProcessorManager::createProcessors(int inputFmt,
        const std::map<Port, stream_t>& producerConfigs,
        const std::map<int, Port>& streamIdToPortMap,
        stream_config_t *streamList, const Parameters& param, ParameterGenerator* paramGenerator)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    ProcessorConfig processorItem;
    processorItem.mInputConfigs = producerConfigs;
    for (const auto& item : streamIdToPortMap) {
        processorItem.mOutputConfigs[item.second] = streamList->streams[item.first];
    }

#ifndef BYPASS_MODE
    // Check if PSysProcessor can be used.
    mPsysUsage = PSYS_NORMAL;
    for (int i = 0; i < streamList->num_streams; i++) {
        if (!PlatformData::usePsys(mCameraId, streamList->streams[i].format)) {
            mPsysUsage = PSYS_NOT_USED;
            break;
        }
    }

    if (mPsysUsage == PSYS_NORMAL) {
        LOG1("Using normal Psys to do image processing.");
        processorItem.mProcessor = new PSysProcessor(mCameraId, paramGenerator);
        mProcessors.push_back(processorItem);
    }
    // LITE_PROCESSING_S
    else {
        createLiteProcessors(inputFmt, processorItem.mInputConfigs,
                             processorItem.mOutputConfigs, streamList, param);
    }
    // LITE_PROCESSING_E
#endif

    if (mPsysUsage == PSYS_NOT_USED) {
        LOG1("Using software to do color conversion.");
        processorItem.mProcessor = new SwImageProcessor(mCameraId);
        mProcessors.push_back(processorItem);
    }

    vector<BufferQueue*> processors;
    for (auto& p : mProcessors) {
        processors.push_back(p.mProcessor);
    }

    return processors;
}

// LITE_PROCESSING_S
#ifndef BYPASS_MODE
void ProcessorManager::createLiteProcessors(int inputFmt,
                                            const std::map<Port, stream_t>& inputConfigs,
                                            const std::map<Port, stream_t>& outputConfigs,
                                            stream_config_t *streamList, const Parameters& param)
{
    // Only consider main port config for now.
    const stream_t& inputConfig = inputConfigs.at(MAIN_PORT);
    const stream_t& outputConfig = outputConfigs.at(MAIN_PORT);

    if ((inputFmt != -1) && (inputFmt != outputConfig.format)) {
        if (CscProcessor::isFormatSupported(inputFmt, outputConfig.format)) {
            mPsysUsage = PSYS_CSC;
        }
    }

    camera_resolution_t srcRes = {inputConfig.width, inputConfig.height};
    if ((inputFmt != -1) && ScaleProcessor::isScalePGNeeded(inputFmt, srcRes, streamList)) {
        if (mPsysUsage == PSYS_CSC ||
            ScaleProcessor::isFormatSupported(inputFmt, outputConfig.format)) {
            mPsysUsage = (mPsysUsage == PSYS_CSC) ? PSYS_SCALE_CSC : PSYS_SCALE;
        }
    }

    if (outputConfig.field != V4L2_FIELD_ANY) {
        camera_deinterlace_mode_t mode = DEINTERLACE_OFF;
        param.getDeinterlaceMode(mode);
        if (mode == DEINTERLACE_WEAVING) {
            mPsysUsage = (mPsysUsage == PSYS_SCALE) ? PSYS_WEAVING_SCALE :
                         (mPsysUsage == PSYS_SCALE_CSC) ? PSYS_WEAVING_SCALE_CSC :
                         PSYS_WEAVING;
        }
    }

    camera_fisheye_dewarping_mode_t dewarping_mode = FISHEYE_DEWARPING_OFF;
    param.getFisheyeDewarpingMode(dewarping_mode);
    if (dewarping_mode > FISHEYE_DEWARPING_OFF) {
        mPsysUsage = PSYS_FISHEYE;
    }

    ProcessorConfig processorItem;
    processorItem.mInputConfigs = inputConfigs;
    processorItem.mOutputConfigs = outputConfigs;
    stream_t tmpConfig; // Used as intermediate config if there are more than two processors.
    CLEAR(tmpConfig);

    switch (mPsysUsage) {
        case PSYS_FISHEYE:
            LOG1("Using single PG to do fisheye dewarping.");
            processorItem.mProcessor = new FisheyeProcessor(mCameraId);
            mProcessors.push_back(processorItem);
            break;

        case PSYS_WEAVING:
            processorItem.mProcessor = new WeavingProcessor(mCameraId);
            mProcessors.push_back(processorItem);
            break;

        case PSYS_CSC:
            processorItem.mProcessor = new CscProcessor(mCameraId);
            mProcessors.push_back(processorItem);
            break;

        case PSYS_SCALE:
            processorItem.mProcessor = new ScaleProcessor(mCameraId);
            mProcessors.push_back(processorItem);
            break;

        case PSYS_SCALE_CSC:
            LOG1("Using Scale and Csc to do scale and color conversion.");

            // Use ScaleProcessor first, and then do the color conversion.
            processorItem.mProcessor = new ScaleProcessor(mCameraId);
            processorItem.mInputConfigs = inputConfigs;
            processorItem.mOutputConfigs[MAIN_PORT].format = V4L2_PIX_FMT_YUV420;

            // Use CscProcessor to do color conversion for 2 ports with the same output format.
            if ((outputConfigs.size() == 2) && (outputConfig.format == outputConfigs.at(SECOND_PORT).format)) {
                processorItem.mOutputConfigs[SECOND_PORT].format = V4L2_PIX_FMT_YUV420;
            }

            mProcessors.push_back(processorItem);

            processorItem.mProcessor = new CscProcessor(mCameraId);
            processorItem.mInputConfigs = processorItem.mOutputConfigs;
            processorItem.mOutputConfigs = outputConfigs;
            mProcessors.push_back(processorItem);
            break;

        case PSYS_WEAVING_SCALE:
            LOG1("Using Weaving and Scale PG to do Weaving and scale.");

            tmpConfig.width = inputConfig.width;
            tmpConfig.height = inputConfig.height * 2;
            tmpConfig.format = inputConfig.format;

            // Do the HW weaving first and then do the scaling.
            processorItem.mProcessor = new WeavingProcessor(mCameraId);
            processorItem.mInputConfigs = inputConfigs;
            processorItem.mOutputConfigs[MAIN_PORT] = tmpConfig;
            mProcessors.push_back(processorItem);

            processorItem.mProcessor = new ScaleProcessor(mCameraId);
            processorItem.mInputConfigs = processorItem.mOutputConfigs;
            processorItem.mOutputConfigs = outputConfigs;
            mProcessors.push_back(processorItem);
            break;

        case PSYS_WEAVING_SCALE_CSC:
            LOGE("Currently not support Weaving/Scale/CSC 3 PGs.");
            break;

        case PSYS_NOT_USED:
            LOG1("Psys is not used, use software processor");
            break;

        default:
            LOGE("Not supported Psys usage: %d", mPsysUsage);
            break;
    }
}
#endif
// LITE_PROCESSING_E
int ProcessorManager::deleteProcessors()
{
    for (auto& item : mProcessors) {
        delete item.mProcessor;
    }
    mProcessors.clear();

    mPsysUsage = PSYS_NOT_USED;

    return OK;
}

/**
 * Configure processor with input and output streams
 */
int ProcessorManager::configureProcessors(const vector<ConfigMode>& configModes,
                                          BufferProducer* producer,
                                          const Parameters& param)
{
    LOG1("@%s, mCameraId:%d", __func__, mCameraId);

    BufferProducer* preProcess =  nullptr;
    for (auto& item : mProcessors) {
        BufferQueue* processor = item.mProcessor;
        processor->setFrameInfo(item.mInputConfigs, item.mOutputConfigs);
        processor->setParameters(param);
        int ret = processor->configure(configModes);
        Check(ret < 0, ret, "Configure processor failed with:%d", ret);

        processor->setBufferProducer(preProcess ? preProcess : producer);
        preProcess = processor;
    }

    return OK;
}

} // end of namespace icamera

