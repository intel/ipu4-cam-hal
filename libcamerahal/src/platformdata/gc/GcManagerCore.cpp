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

#define LOG_TAG "GcManagerCore"

#include "GcManagerCore.h"
#include "GraphConfig.h"
#include "PlatformData.h"
#include "FormatUtils.h"
#include <ia_camera/GCSSParser.h>

#include "iutils/CameraLog.h"

using namespace GCSS;
using std::vector;
using std::map;
using std::string;

// Settings to use in fallback cases
#define DEFAULT_SETTING_1_VIDEO_1_STILL "7002" // 7002, 1 video, 1 still stream
#define DEFAULT_SETTING_2_VIDEO_2_STILL "7004" // 7004, 2 video, 2 stills streams
#define DEFAULT_SETTING_2_STILL "7005" // 7005, 2 still streams
#define DEFAULT_SETTING_1_STILL "7006" // 7006, 1 still stream
// operation modes used in stream config
#define OP_MODE_NORMAL      0
#define OP_MODE_HIGH_SPEED  1

namespace icamera {

GraphConfigNodes::GraphConfigNodes() :
        mDesc(nullptr),
        mSettings(nullptr)
{
}

GraphConfigNodes::~GraphConfigNodes()
{
    delete mDesc;
    delete mSettings;
}

GcManagerCore::GcManagerCore(int32_t camId, GraphConfigNodes *nodes) :
    mCameraId(camId),
    mGraphQueryManager(new GraphQueryManager()),
    mQueryResult(nullptr),
    mFallback(false),
    mMcId(-1)
{
    if (nodes == nullptr) {
        LOGE("Failed to allocate Graph Query Manager -- FATAL");
        return;
    }

    mGraphQueryManager->setGraphDescriptor(nodes->mDesc);
    mGraphQueryManager->setGraphSettings(nodes->mSettings);
}

GcManagerCore::~GcManagerCore()
{
}

/**
 * Generate the helper vectors mVideoStreamResolutions and
 *  mStillStreamResolutions used during stream configuration.
 *
 * This is a helper member to store the ItemUID's for the width and height of
 * each stream. Each ItemUID points to items like :
 *  video0.width
 *  video0.height
 * This vector needs to be regenerated after each stream configuration.
 */
void GcManagerCore::initStreamResolutionIds()
{
    mVideoStreamResolutions.clear();
    mStillStreamResolutions.clear();
    mVideoStreamKeys.clear();
    mStillStreamKeys.clear();

    mVideoStreamKeys.push_back(GCSS_KEY_VIDEO0);
    mVideoStreamKeys.push_back(GCSS_KEY_VIDEO1);
    mVideoStreamKeys.push_back(GCSS_KEY_VIDEO2);
    mStillStreamKeys.push_back(GCSS_KEY_STILL0);
    mStillStreamKeys.push_back(GCSS_KEY_STILL1);
    mStillStreamKeys.push_back(GCSS_KEY_STILL2);

    for (auto &key : mVideoStreamKeys) {
        ItemUID w = {key, GCSS_KEY_WIDTH};
        ItemUID h = {key, GCSS_KEY_HEIGHT};
        mVideoStreamResolutions.push_back(std::make_pair(w,h));
    }
    for (auto &key : mStillStreamKeys) {
        ItemUID w = {key, GCSS_KEY_WIDTH};
        ItemUID h = {key, GCSS_KEY_HEIGHT};
        mStillStreamResolutions.push_back(std::make_pair(w,h));
    }
}

/**
 * Add predefined keys to the map used by the graph config parser.
 *
 * This method is static and should only be called once.
 *
 * We do this so that the keys we will use in the queries are already defined
 * and we can create the query objects in a more compact way, by using the
 * ItemUID initializers.
 */
void GcManagerCore::addKeyMap()
{
    /**
     * Initialize the map with custom specific tags found in the
     * Graph Config XML's
     */
    #define GCSS_KEY(key, str) std::make_pair(#str, GCSS_KEY_##key),
    map<std::string, ia_uid> CUSTOM_GRAPH_KEYS = {
        #include "custom_gcss_keys.h"
    };
    #undef GCSS_KEY

    LOG1("Adding %zu custom specific keys to graph config parser",
            CUSTOM_GRAPH_KEYS.size());

    /*
     * add custom specific tags so parser can use them
     */
    ItemUID::addCustomKeyMap(CUSTOM_GRAPH_KEYS);
}
/**
 *
 * Static method to parse the XML graph configurations and settings
 *
 * This method is currently called once per camera.
 *
 * \param[in] descriptorXmlFile: name of the file where the graphs are described
 * \param[in] settingsXmlFile: name of the file where the settings are listed
 *
 * \return nullptr if parsing failed.
 * \return pointer to a valid GraphConfigNode object. Ownership passes to
 *         caller.
 */
GraphConfigNodes* GcManagerCore::parse(const char *descriptorXmlFile,
                                            const char *settingsXmlFile)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    GCSSParser parser;

    GraphConfigNodes *nodes = new GraphConfigNodes;

    parser.parseGCSSXmlFile(descriptorXmlFile, &nodes->mDesc);
    if (!nodes->mDesc) {
        LOGE("Failed to parse graph descriptor from %s", descriptorXmlFile);
        delete nodes;
        return nullptr;
    }

    parser.parseGCSSXmlFile(settingsXmlFile, &nodes->mSettings);
    if (!nodes->mSettings) {
        LOGE("Failed to parse graph settings from %s", settingsXmlFile);
        delete nodes;
        return nullptr;
    }

    return nodes;
}

/**
 * Perform a reverse lookup on the map that associates client streams to
 * virtual sinks.
 *
 * This method is used during pipeline configuration to find a stream associated
 * with the id (GCSS key) of the virtual sink
 *
 * \param[in] vPortId GCSS key representing one of the virtual sinks in the
 *                    graph, like GCSS_KEY_VIDEO1
 * \return nullptr if not found
 * \return pointer to the client stream associated with that virtual sink.
 */
HalStream* GcManagerCore::getHalStreamByVirtualId(uid_t vPortId)
{
    std::map<HalStream*, uid_t>::iterator it;
    it = mStreamToSinkIdMap.begin();

    for (; it != mStreamToSinkIdMap.end(); ++it) {
        if (it->second == vPortId) {
            return it->first;
        }
    }
    return nullptr;
}

/**
 * Initialize the state of the GcManagerCore after parsing the stream
 * configuration.
 * Perform the first level query to find a subset of settings that fulfill the
 * constrains from the stream configuration.
 *
 * \param[in] streams List of streams required by the client.
 * \param[in] operation mode The operation mode of streams in this configuration
 */
status_t GcManagerCore::configStreams(const vector<HalStream*> &streams, uint32_t operationMode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    AndroidGraphConfigKey streamKey;
    mFirstQueryResults.clear();
    mQuery.clear();
    mQueryResult = nullptr;
    ResolutionItem res;
    mFallback = false;

    /*
     * Add to the query the number of active outputs
     */
    ItemUID streamCount = {GCSS_KEY_ACTIVE_OUTPUTS};
    mQuery[streamCount] = std::to_string(streams.size());
    /*
     * regenerate the stream resolutions vector if needed
     * We do this because we consume this vector for each stream configuration.
     * This allows us to have sequential stream numbers even when an input
     * stream is present.
     */
    initStreamResolutionIds();
    mStreamToSinkIdMap.clear();

    int32_t videoStreamCount = 0, stillStreamCount = 0;
    for (auto &s : streams) {
        if (s->useCase() == USE_CASE_INPUT) {
            // TODO re-processing not yet supported so return error.
            LOGE("Error: Re-processing not supported with graph config yet.");
            return UNKNOWN_ERROR;
        } else {
            /*
             * Decide what pipe will serve each stream
             * TODO: ensure stream list is coming sorted by size (descending)
             * TODO: move this logic to a separate method if it start to grow
             *       too much
             */
            if (isVideoStream(s)) {
                videoStreamCount++;
                res = mVideoStreamResolutions[0];
                mVideoStreamResolutions.erase(mVideoStreamResolutions.begin());
                streamKey = mVideoStreamKeys[0];
                mVideoStreamKeys.erase(mVideoStreamKeys.begin());
            } else {
                stillStreamCount++;
                if (mStillStreamResolutions.size() == 0) {
                    LOGE("Out of resolutions in GCM. Fix this please..");
                    break;
                }
                res = mStillStreamResolutions[0];
                mStillStreamResolutions.erase(mStillStreamResolutions.begin());
                streamKey = mStillStreamKeys[0];
                mStillStreamKeys.erase(mStillStreamKeys.begin());
            }

            /*
             * Map client stream to a virtual sink id
             * here the sink id is a GCSS_KEY, like GCSS_KEY_VIDEO2
             */
            LOG1("Adding stream %p to map %s", s,
                                               ItemUID::key2str(streamKey));
            mStreamToSinkIdMap[s] = streamKey;

            ItemUID w = res.first;
            ItemUID h = res.second;

            mQuery[w] = std::to_string(s->width());
            mQuery[h] = std::to_string(s->height());
        }
    }

    dumpQuery(mQuery);
    /**
     * Look for settings. If query results are empty, get default settings
     */
    int32_t id = 0;
    string settingsId = "0";
    mGraphQueryManager->queryGraphs(mQuery, mFirstQueryResults);
    if (mFirstQueryResults.empty()) {

        dumpQuery(mQuery);
        mFallback = true;
        mQuery.clear();

        status_t status = selectDefaultSetting(videoStreamCount, stillStreamCount, settingsId);
        if (status != OK) {
            return UNKNOWN_ERROR;
        }

        ItemUID content1({GCSS_KEY_KEY});
        mQuery.insert(std::make_pair(content1, settingsId));
        mGraphQueryManager->queryGraphs(mQuery, mFirstQueryResults);

        if (!mFirstQueryResults.empty()) {
            mFirstQueryResults[0]->getValue(GCSS_KEY_KEY, id);
            LOG1("CAM[%d]Default settings in use for this stream configuration. Settings id %d", mCameraId, id);
            mQueryResult = mFirstQueryResults[0];
        } else {
            LOGE("Failed to retrieve default settings(%s)", settingsId.c_str());
            return UNKNOWN_ERROR;
        }
    } else {
        // select setting from multiple results
        status_t ret = selectSetting(operationMode);
        if (ret != OK) {
            LOGW("Failed to select the settings for operation mode(0x%x)in results", operationMode);
            return BAD_VALUE;
        }
    }

    return OK;
}

/**
 * Prepare graph config object
 *
 * Use graph query results as a parameter to getGraph. The result will be given
 * to graph config object.
 *
 * \param[in/out] gc     Graph Config object.
 */
status_t GcManagerCore::prepareGraphConfig(shared_ptr<GraphConfig> gc)
{
    css_err_t ret;
    status_t status = OK;
    GCSS::IGraphConfig *result = nullptr;
    ret  = mGraphQueryManager->createGraph(mQueryResult, &result);
    if (ret != css_err_none) {
        //gc.reset();
        delete result;
        return UNKNOWN_ERROR;
    }

    status = gc->prepare(this, static_cast<GraphConfigNode*>(result), mStreamToSinkIdMap, mFallback);
    LOG1("Graph config object prepared");

    return status;
}

/*
 * Compare fps values of found settings.
 * If operation mode is 1 (high speed mode), we use settings with highest fps
 * rating. Otherwise we use the minimum fps. This is temporary solution to
 * select a setting when there is one high speed setting available.
 */
status_t GcManagerCore::selectSetting(uint32_t operationMode)
{
    int key = 0;
    string opMode;
    mQueryResult = nullptr;

    vector<GCSS::IGraphConfig*> internalQueryResults;

    for (auto &result : mFirstQueryResults) {
        vector<ConfigMode> cfgModes;
        result->getValue(GCSS_KEY_OP_MODE, opMode);
        LOG1("The operation mode str in xml: %s", opMode.c_str());

        CameraUtils::getConfigModeFromString(opMode, cfgModes);
        LOG1("The query results supports configModes size: %zu", cfgModes.size());

        for (const auto mode : cfgModes) {
            if (operationMode == (uint32_t)mode) {
                internalQueryResults.push_back(result);
                break;
            }
        }
    }

    // May still have multiple graphs after config mode parsing
    // Those graphs have same resolution/configMode, but different output formats
    // Do second graph query with format/bpp as query condition
    if (internalQueryResults.size() > 1) {
        map<GCSS::ItemUID, std::string> queryItem;
        for (auto const& item : mStreamToSinkIdMap) {
            HalStream *s = item.first;
            ItemUID formatKey = {(ia_uid)item.second, GCSS_KEY_FORMAT};
            string fmt = graphconfig::utils::format2string(s->format());
            queryItem[formatKey] = fmt;
        }

        LOG1("dumpQuery with format condition");
        dumpQuery(queryItem);

        mGraphQueryManager->queryGraphs(queryItem, internalQueryResults, mSecondQueryResults);
        if(mSecondQueryResults.size() != 1) {
            LOGE("Failed to query one unique graph");
            mQueryResult = nullptr;
        } else {
            mQueryResult = mSecondQueryResults[0];
        }

    } else if (internalQueryResults.size() == 1) {
        mQueryResult = internalQueryResults[0];
    } else {
        mQueryResult = nullptr;
    }

    if (mQueryResult == nullptr) {
        return INVALID_OPERATION;
    }

    string mcId;
    mQueryResult->getValue(GCSS_KEY_MC_ID, mcId);
    mMcId = mcId.empty() ? -1 : stoi(mcId);

    mQueryResult->getValue(GCSS_KEY_KEY, key);
    LOG1("CAM[%d]Graph config in use for this stream configuration - SUCCESS,"
            " using settings id %d, operation mode: %s", mCameraId, key, opMode.c_str());
    return OK;
}

/**
 * Find suitable default setting based on stream config.
 *
 * \param[in] videoStreamCount
 * \param[in] stillStreamCount
 * \param[out] settingsId
 * \return OK when success, UNKNOWN_ERROR on failure
 */
status_t GcManagerCore::selectDefaultSetting(int videoStreamCount,
                                                  int stillStreamCount,
                                                  string &settingsId)
{
    // Determine which default setting to use
    switch (videoStreamCount) {
    case 0:
        if (stillStreamCount == 1) {
            settingsId = DEFAULT_SETTING_1_STILL; // 0 video, 1 still
        } else if (stillStreamCount == 2) {
            settingsId = DEFAULT_SETTING_2_STILL; // 0 video, 2 still
        } else {
            LOGE("Default settings cannot support 0 video, >2 still streams");
            return UNKNOWN_ERROR;
        }
        break;
    case 1:
        if ((stillStreamCount == 0) || (stillStreamCount == 1)) {
            settingsId = DEFAULT_SETTING_1_VIDEO_1_STILL; // 1 video, 1 still
        } else if (stillStreamCount == 2) {
            settingsId = DEFAULT_SETTING_2_VIDEO_2_STILL; // 2 video, 2 still
        } else {
            LOGE("Default settings cannot support 1 video, >2 still streams");
            return UNKNOWN_ERROR;
        }
        break;
    case 2:
        // Works for 2 video 2 still, and 2 video 1 still.
        settingsId = DEFAULT_SETTING_2_VIDEO_2_STILL; // 2 video, 2 still
        if (stillStreamCount > 2) {
            LOGE("Default settings cannot support 2 video, >2 still streams");
            return UNKNOWN_ERROR;
        }
        break;
    default:
        LOGE("Default settings cannot support > 2 video streams");
        return UNKNOWN_ERROR;
    }
    return OK;
}

/******************************************************************************
 *  HELPER METHODS
 ******************************************************************************/
/**
 * Check the gralloc hint flags and decide whether this stream should be served
 * by Video Pipe or Still Pipe
 */
bool GcManagerCore::isVideoStream(HalStream *stream)
{
    if (stream->useCase() == USE_CASE_PREVIEW || stream->useCase() == USE_CASE_VIDEO)
        return true;

    return false;
}

void GcManagerCore::dumpQuery(const map<GCSS::ItemUID, std::string> &query)
{
    map<GCSS::ItemUID, std::string>::const_iterator it;
    it = query.begin();
    LOG1("Query Dump ------- Start");
    for(; it != query.end(); ++it) {
        LOG1("item: %s value %s", it->first.toString().c_str(),
                                  it->second.c_str());
    }
    LOG1("Query Dump ------- End");
}
}  // icamera
