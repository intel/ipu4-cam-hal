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
#include "HalStream.h"
#include "iutils/Utils.h"
#include "iutils/Errors.h"

using namespace std;

namespace GCSS {
class GraphConfigNode;
class GraphQueryManager;
class ItemUID;
}

namespace icamera {

/**
 * \enum AndroidGraphConfigKey
 * List of keys that are Android Specific used in queries of settings by
 * the GcManagerCore.
 *
 * The enum should not overlap with the enum of tags already predefined by the
 * parser, hence the initial offset.
 */
#define GCSS_KEY(key, str) GCSS_KEY_##key,
enum AndroidGraphConfigKey {
    GCSS_ANDROID_KEY_START = GCSS_KEY_START_CUSTOM_KEYS,
    #include "custom_gcss_keys.h"
};
#undef GCSS_KEY

class GraphConfig;

/**
 * Static data for graph settings for given sensor. Used to initialize
 * \class GcManagerCore.
 */
class GraphConfigNodes
{
public:
    ~GraphConfigNodes();

private:
    // Private constructor, only initialized by GCM
    friend class GcManagerCore;
    GraphConfigNodes();

    // Disable copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(GraphConfigNodes);

private:
    GCSS::IGraphConfig *mDesc;
    GCSS::IGraphConfig *mSettings;
};

/**
 * \class GcManagerCore
 *
 * Class to wrap over parsing and executing queries on graph settings.
 * GcManagerCore owns the interface towards GCSS and provides convenience
 * for HAL to execute queries and it generates GraphConfig objects as results.
 *
 * GcManagerCore also provides static method for parsing graph descriptor
 * and graph settings from XML files and filtering that data based on sensor.
 * The \class GraphConfigmanager::Nodes object is stored in CameraCapInfo and
 * is used when instantiating GCM.
 *
 * At camera open, GcManagerCore object is created.
 * At stream config time the state of GraphConfig manager changes with the
 * result of the first query. This is the possible subset of graph settings that
 * can fulfill the requirements of requested streams.
 * At this point, there may be more than one options, but
 * GCM can always return some default settings.
 *
 * Per each request, GcManagerCore creates GraphConfig objects based
 * on request content. These objects are owned by GCM in a pool, and passed
 * around HAL via shared pointers.
 */

class GcManagerCore
{
public:
    GcManagerCore(int32_t camId, GraphConfigNodes *nodes);
    GcManagerCore();
    virtual ~GcManagerCore();
    /*
     * static methods for XML parsing
     */
    static void addKeyMap();
    static GraphConfigNodes* parse(const char *descriptorXmlFile,
                                   const char *settingsXmlFile);
    /*
     * First Query
     */
    status_t configStreams(const std::vector<HalStream*> &activeStreams,
                           uint32_t operation_mode);

    status_t prepareGraphConfig(shared_ptr<GraphConfig> gc);
    uid_t getSinkByStream(HalStream *stream) { return mStreamToSinkIdMap[stream]; }

    HalStream* getHalStreamByVirtualId(uid_t vPortId);

    int getSelectedMcId() { return mMcId; }

private: /* Types */
    /**
     * Pair of ItemUIDs to store the width and height of a stream
     * first item is for width, second for height
     */
    typedef std::pair<GCSS::ItemUID, GCSS::ItemUID> ResolutionItem;

private:
    // Disable copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(GcManagerCore);

    void initStreamResolutionIds();
    bool isVideoStream(HalStream *stream);
    status_t selectSetting(uint32_t operationMode);
    status_t selectDefaultSetting(int videoStreamCount,
                                  int stillStreamCount,
                                  string &settingsId);
    void dumpQuery(const std::map<GCSS::ItemUID, std::string> &query);
private:
    const int32_t mCameraId;
    std::unique_ptr<GCSS::GraphQueryManager> mGraphQueryManager;
    /*
     * The query interface uses types that are actually STL maps and vectors
     * to avoid the creation/deletion on the stack for every call we
     * have them as member variables.
     * - mQuery is reused between first and second level queries.
     * - mFirstQueryResults will be not be modified during request processing.
     *   only at stream config time.
     * - mSecondQueryResults is a temporary container, the settings that come
     *   here will be finally stored in a GraphConfig object.
     */
    std::map<GCSS::ItemUID, std::string> mQuery;
    std::vector<GCSS::IGraphConfig*> mFirstQueryResults;
    std::vector<GCSS::IGraphConfig*> mSecondQueryResults;
    GCSS::IGraphConfig *mQueryResult;

    std::vector<AndroidGraphConfigKey> mVideoStreamKeys;
    std::vector<AndroidGraphConfigKey> mStillStreamKeys;
    std::vector<ResolutionItem> mVideoStreamResolutions;
    std::vector<ResolutionItem> mStillStreamResolutions;

    /**
     * Map to get the virtual sink id from a client stream pointer.
     * The uid is one of the GCSS keys defined for the virtual sinks, like
     * GCSS_KEY_VIDEO0 or GCSS_KEY_STILL1
     * From that we can derive the name using the id to string methods from
     * ItemUID class
     */
    std::map<HalStream*, uid_t> mStreamToSinkIdMap;

    bool mFallback; /**< This is to tell if we need to use fallback settings */

    int mMcId;
};

} // namespace icamera
