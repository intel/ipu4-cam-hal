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

/**
 *\file CameraProfiles.h
 *
 * parser for the camera xml configuration file
 *
 * This file calls the libexpat ditectly. The libexpat is one xml parser.
 * It will parse the camera configuration out firstly.
 * Then other module can call the methods of it to get the real configuration.
 *
 */

#pragma once

#include "PlatformData.h"
#include "CameraMetadata.h"

namespace icamera {

/**
 * \class CameraProfiles
 *
 * This class is used to parse the camera configuration file.
 * The configuration file is xml format.
 * This class will use the expat lib to do the xml parser.
 */
class CameraProfiles {
public:
    CameraProfiles(MediaControl *mc, PlatformData::StaticCfg *cfg);
    ~CameraProfiles();

    unsigned getSensorNum(void) {return mSensorNum;};

private:
    DISALLOW_COPY_AND_ASSIGN(CameraProfiles);

private:
    enum DataField {
        FIELD_INVALID = 0,
        FIELD_SENSOR,
        FIELD_COMMON
    } mCurrentDataField;
    int mSensorNum;
    int mCurrentSensor;
    PlatformData::StaticCfg::CameraInfo *pCurrentCam;
    bool mInMediaCtlCfg;
    bool mInStaticMetadata;
    MediaControl* mMC;
    PlatformData::StaticCfg* mStaticCfg;
    CameraMetadata mMetadata;

    long* mMetadataCache;
    static const int mMetadataCacheSize = 4096;

    static const int mBufSize = 4*1024;
    static void startElement(void *userData, const char *name, const char **atts);
    static void endElement(void *userData, const char *name);

    static void parseSizesList(const char *sizesStr, vector <camera_resolution_t> &sizes);
    static int getSupportedFormat(const char* str, vector <int>& supportedFormat);
    static int parsePair(const char *str, int *first, int *second, char delim, char **endptr = nullptr);

    void getAvaliableXmlFile(string fileName, string &xmlFile);
    void parseXmlFile(const string &xmlFile);
    void getDataFromXmlFile(void);
    void getSensorDataFromXmlFile(void);
    void getGraphConfigFromXmlFile(void);
    void checkField(CameraProfiles *profiles, const char *name, const char **atts);

    void handleSensor(CameraProfiles *profiles, const char *name, const char **atts);
    void handleCommon(CameraProfiles *profiles, const char *name, const char **atts);

    int parseSensorName(const char *str, vector<string> &sensorNames);
    int parseStreamConfig(const char* src, supported_stream_config_array_t& configs);
    void parseSupportedFeatures(const char* src, camera_features_list_t& features);
    void parseSupportedIspControls(const char* src, vector<uint32_t>& features);
    int parseSupportedIntRange(const char* str, vector<int>& rangeArray);
    int parseSupportedFloatRange(const char* str, vector<float>& rangeArray);
    int parseSupportedVideoStabilizationMode(const char* str, camera_video_stabilization_list_t &supportedModes);
    int parseSupportedAeMode(const char* str, vector <camera_ae_mode_t> &supportedModes);
    int parseSupportedAwbMode(const char* str, vector <camera_awb_mode_t> &supportedModes);
    int parseSupportedAfMode(const char* str, vector <camera_af_mode_t> &supportedModes);
    int parseSupportedSceneMode(const char* str, vector <camera_scene_mode_t> &supportedModes);
    int parseSupportedAntibandingMode(const char* str, vector <camera_antibanding_mode_t> &supportedModes);
    int parseRationalType(const char* str, int &numerator, int &denominator);
    int parseSupportedAeParamRange(const char* src, vector<int>& scenes,
                                   vector<float>& minValues, vector<float>& maxValues);
    stream_t parseIsaScaleRawConfig(const char* src);

// parse the media controller configuration in xml, the MediaControl MUST be run before the parser to run.
    int parseConfigMode(const char *str, vector <ConfigMode> &sceneMode);
    void handleMediaCtlCfg(CameraProfiles *profiles, const char *name, const char **atts);
    void handleStaticMetaData(CameraProfiles *profiles, const char *name, const char **atts);
    void handleGenericStaticMetaData(const char *name, const char *src);
    void parseMediaCtlConfigElement(CameraProfiles *profiles, const char *name, const char **atts);
    void storeMcMappForConfig(int mcId, supported_stream_config_t streamCfg);
    void parseLinkElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseRouteElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseControlElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseSelectionElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseFormatElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseVideoElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseOutputElement(CameraProfiles *profiles, const char *name, const char **atts);
    void parseMultiExpRange(const char* src);

    TuningMode getTuningModeByStr(const char *str);
    int parseSensorOBSettings(const char *str, vector<OBSetting> &obSettings);
    int parseSupportedTuningConfig(const char *str, vector <TuningConfig> &config);
    int parseLardTags(const char *str, vector <LardTagConfig> &lardTags);
    int parseConfigModeForAuto(const char *str, vector <ConfigMode> &modes);

    /**
     * Skip whitespace. (space, tab, newline, vertical tab, feed, carriage return)
     */
    const char* skipWhiteSpace(const char *src);

    void dumpSensorInfo(void);

    void parseViewProjection(const char* str, camera_view_projection_t &viewProjection);
    void parseViewRotation(const char* str, camera_view_rotation_t &viewRotation);
    void parseViewFineAdjustments(const char* str, camera_view_fine_adjustments_t &viewFineAdj);
    void parseCameraRotation(const char* str, camera_view_rotation_t &camRotation);
};

} // namespace icamera
