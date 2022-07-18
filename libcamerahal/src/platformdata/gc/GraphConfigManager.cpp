/*
 * Copyright (C) 2015-2018 Intel Corporation
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

#define LOG_TAG "GraphConfigManager"

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "GraphConfigManager.h"
#include "GraphConfig.h"
#include "PlatformData.h"
#include <ia_camera/GCSSParser.h>

using namespace GCSS;
using std::vector;
using std::map;


namespace icamera {

#ifdef __ANDROID__
static const char* DEFAULT_DESCRIPTOR_FILE = "/vendor/etc/graph_descriptor.xml";
static const char* GRAPH_SETTINGS_FILE_PATH = "/vendor/etc/";
#else
static const char* DEFAULT_DESCRIPTOR_FILE = "/usr/share/defaults/etc/camera/gcss/graph_descriptor.xml";
static const char* GRAPH_SETTINGS_FILE_PATH = "/usr/share/defaults/etc/camera/gcss/";
#endif

GraphConfigManager::GraphConfigManager(int32_t cameraId, GraphConfigNodes *testNodes) :
    mGcConfigured(false),
    mCameraId(cameraId),
    mGcManagerCore(nullptr),
    mMcId(-1)
{

    GraphConfigNodes *nodes = testNodes;
    if (nodes == nullptr) {
        if (!PlatformData::getGraphConfigNodes(cameraId)) {
            return;
        }
        nodes = const_cast<GraphConfigNodes*>(PlatformData::getGraphConfigNodes(cameraId));
    }
    mGcManagerCore = new GcManagerCore(cameraId, nodes);
    mGraphConfigMap.clear();
}

GraphConfigManager::~GraphConfigManager()
{
    mGraphConfigMap.clear();
    mGcConfigured = false;
    releaseHalStream();

    if (mGcManagerCore) {
        delete mGcManagerCore;
        mGcManagerCore = nullptr;
    }
}

void GraphConfigManager::releaseHalStream()
{
    for(auto &halStream : mHalStreamVec) {
        delete halStream;
    }
    mHalStreamVec.clear();
}

void GraphConfigManager::addCustomKeyMap()
{
    GcManagerCore::addKeyMap();
}

GraphConfigNodes* GraphConfigManager::parse(const char *settingsXmlFile)
{
    string settingsFile = string(GRAPH_SETTINGS_FILE_PATH) + settingsXmlFile;
    return GcManagerCore::parse(DEFAULT_DESCRIPTOR_FILE, settingsFile.c_str());
}

/*
 * Get the useCase from the stream and operationMode.
 */
StreamUseCase GraphConfigManager::getUseCaseFromStream(ConfigMode configMode, const stream_t *stream)
{
    if (configMode == CAMERA_STREAM_CONFIGURATION_MODE_STILL_CAPTURE ||
            stream->usage == CAMERA_STREAM_STILL_CAPTURE)
        return USE_CASE_STILL_CAPTURE;

    return USE_CASE_PREVIEW;
}

/**
 * Initialize the state of the GraphConfigManager after parsing the stream
 * configuration.
 * Perform the first level query to find a subset of settings that fulfill the
 * constrains from the stream configuration.
 *
 * \param[in] streamList: all the streams info.
 */
status_t GraphConfigManager::configStreams(const stream_config_t *streamList)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    Check(!streamList, BAD_VALUE, "%s: Null streamList configured", __func__);

    vector <ConfigMode> configModes;
    int ret = PlatformData::getConfigModesByOperationMode(mCameraId, streamList->operation_mode, configModes);
    Check(ret != OK, ret, "%s, get ConfigMode failed %d", __func__, ret);

    // Use the stream list with descending order to find graph settings.
    vector<stream_t*> streams;
    for (int i = 0; i < streamList->num_streams; i++) {
        bool stored = false;
        const stream_t &s  = streamList->streams[i];
        for (size_t j = 0; j < streams.size(); j++) {
            if (s.width * s.height > streams[j]->width * streams[j]->height) {
                stored = true;
                streams.insert((streams.begin() + j), &(streamList->streams[i]));
                break;
            }
        }
        if (!stored)
            streams.push_back(&(streamList->streams[i]));
    }

    //debug
    dumpStreamConfig(streams);
    mGraphConfigMap.clear();
    releaseHalStream();

    for (auto &s : streams) {
        StreamUseCase useCase = getUseCaseFromStream(configModes[0], s);
        streamProps props = {(uint32_t)s->width, (uint32_t)s->height,
                             s->format, useCase};

        HalStream *halStream = new HalStream(props, (void*)s);
        if (halStream) {
            mHalStreamVec.push_back(halStream);
            mCameraStreamToHalStreamMap[s] = halStream;
        }
    }


    mMcId = -1;
    for (auto mode : configModes) {
        LOG1("Mapping the operationMode %d to ConfigMode %d", streamList->operation_mode, mode);
        ret = mGcManagerCore->configStreams(mHalStreamVec, mode);
        if (ret != OK) {
            LOGW("%s, Failed to configure graph: real ConfigMode %x", __func__, mode);
            return ret;
        }

        int tmpId = mGcManagerCore->getSelectedMcId();
        if (tmpId != -1 && mMcId != -1 && mMcId != tmpId) {
            LOGW("Not support two different MC ID at same time:(%d/%d)", mMcId, tmpId);
        }
        mMcId = tmpId;

        LOGG("%s: Add graph setting for op_mode %d", __func__, mode);
        mGraphConfigMap[mode] = make_shared<GraphConfig>();
        ret = mGcManagerCore->prepareGraphConfig(mGraphConfigMap[mode]);
        if (ret != OK) {
            LOGW("%s, Failed to prepare graph config: real ConfigMode: %x", __func__, mode);
            return ret;
        }
    }

    mGcConfigured = true;
    return OK;
}

shared_ptr<IGraphConfig> GraphConfigManager::getGraphConfig(ConfigMode configMode)
{
    for (auto& gc : mGraphConfigMap) {
        if (gc.first == configMode) {
            LOGG("%s: found graph config for mode %d", __func__, configMode);
            return gc.second;
        }
    }

    return nullptr;
}

void GraphConfigManager::dumpStreamConfig(const vector<stream_t*> &streams)
{
    for (size_t i = 0; i < streams.size(); i++) {
        LOG1("stream[%zu] %dx%d, fmt %s", i,
                streams[i]->width, streams[i]->height,
                CameraUtils::pixelCode2String(streams[i]->format));
    }
}

map<int, IGraphConfigManager*> IGraphConfigManager::sInstances;
Mutex IGraphConfigManager::sLock;

IGraphConfigManager* IGraphConfigManager::getInstance(int cameraId)
{
    AutoMutex lock(sLock);
    if (sInstances.find(cameraId) != sInstances.end()) {
        return sInstances[cameraId];
    }

    sInstances[cameraId] = new GraphConfigManager(cameraId);
    return sInstances[cameraId];
}

void IGraphConfigManager::releaseInstance(int cameraId)
{
    AutoMutex lock(sLock);
    if (sInstances.find(cameraId) != sInstances.end()) {
        IGraphConfigManager* gcManager = sInstances[cameraId];
        sInstances.erase(cameraId);
        delete gcManager;
    }
}
}  // icamera
