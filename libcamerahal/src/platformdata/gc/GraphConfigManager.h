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

#pragma once

#include <ia_camera/gcss.h>
#include <memory>
#include <utility>
#include <vector>
#include "iutils/Errors.h"
#include "iutils/Thread.h"
#include "GcManagerCore.h"
#include "IGraphConfigManager.h"

namespace icamera {

class GraphConfig;
class GraphConfigNodes;

/**
 * \interface IStreamConfigProvider
 *
 * This interface is used to expose the GraphConfig settings selected at stream
 * config time. At the moment only exposes the MediaController configuration.
 *
 * It is used by the 3 units (Ctrl, Capture and Processing).
 * At the moment it is implemented by the GraphConfigManager
 *
 *  TODO: Expose a full GraphConfig Object selected.
 */
class IStreamConfigProvider {
public:
    virtual ~IStreamConfigProvider() { };
    virtual shared_ptr<IGraphConfig> getGraphConfig(ConfigMode configMode) = 0;
};

/**
 * \class GraphConfigManager
 *
 * Class to wrap over parsing and executing queries on graph settings.
 * GraphConfigManager owns the interface towards GCSS and provides convenience
 * for HAL to execute queries and it generates GraphConfig objects as results.
 *
 * GraphConfigManager also provides static method for parsing graph descriptor
 * and graph settings from XML files and filtering that data based on sensor.
 * The \class GraphConfigmanager::Nodes object is stored in CameraCapInfo and
 * is used when instantiating GCM.
 *
 * At camera open, GraphConfigManager object is created.
 * At stream config time the state of GraphConfig manager changes with the
 * result of the first query. This is the possible subset of graph settings that
 * can fulfill the requirements of requested streams.
 * At this point, there may be more than one options, but
 * GCM can always return some default settings.
 *
 * Per each request, GraphConfigManager creates GraphConfig objects based
 * on request content. These objects are owned by GCM in a pool, and passed
 * around HAL via shared pointers.
 */
class GraphConfigManager: public IGraphConfigManager, public IStreamConfigProvider
{
public:
    GraphConfigManager(int32_t cameraId, GraphConfigNodes *nodes = nullptr);
    virtual ~GraphConfigManager();
    /*
     * static methods for XML parsing
     */
    static void addCustomKeyMap();
    static GraphConfigNodes* parse(const char *settingsXmlFile);
    /*
     * First Query
     */
    status_t configStreams(const stream_config_t *streamList);
    /*
     * Second query
     */
    shared_ptr<IGraphConfig> getGraphConfig(ConfigMode configMode);

    int getSelectedMcId() { LOGG("%s: %d", __func__, mMcId); return mMcId; }

    bool isGcConfigured(void) { LOGG("%s: %d", __func__, mGcConfigured); return mGcConfigured; }
public:
    static const int32_t MAX_REQ_IN_FLIGHT = 10;

private:
    // Disable copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(GraphConfigManager);

    StreamUseCase getUseCaseFromStream(ConfigMode configMode, const stream_t *stream);
    void releaseHalStream();

    /* according stream num to set active sinks dynamiclly
     * this isn't supported on linux currently.
     *
     * void detectActiveSinks(Camera3Request &request,
     *                     sp<GraphConfig> gc); */

    // Debuging helpers
    void dumpStreamConfig(const std::vector<stream_t*> &streams);
private:

    bool mGcConfigured;
    int32_t mCameraId;
    std::map<ConfigMode, shared_ptr<GraphConfig>> mGraphConfigMap;
    std::map<stream_t*, HalStream*> mCameraStreamToHalStreamMap;
    GcManagerCore *mGcManagerCore;
    std::vector<HalStream*> mHalStreamVec;
    int mMcId;
};

} // icamera
