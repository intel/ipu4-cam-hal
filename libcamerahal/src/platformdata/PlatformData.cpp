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

#define LOG_TAG "PlatformData"

#include <sys/sysinfo.h>
#include <math.h>

#include "iutils/CameraLog.h"

#include "PlatformData.h"
#include "CameraProfiles.h"
// CUSTOM_WEIGHT_GRID_S
#include "TunningProfiles.h"
// CUSTOM_WEIGHT_GRID_E
#include "PolicyProfiles.h"

#if !defined(BYPASS_MODE) && !defined(USE_STATIC_GRAPH)
#include "gc/GraphConfigManager.h"
#endif

namespace icamera {
PlatformData *PlatformData::sInstance = nullptr;
Mutex  PlatformData::sLock;

PlatformData* PlatformData::getInstance()
{
    AutoMutex lock(sLock);
    if (sInstance == nullptr) {
        sInstance = new PlatformData();
    }

    return sInstance;
}

void PlatformData::releaseInstance()
{
    AutoMutex lock(sLock);
    LOG1("@%s", __func__);

    if (sInstance) {
        delete sInstance;
        sInstance = nullptr;
    }
}

PlatformData::PlatformData()
{
    LOG1("@%s", __func__);
    MediaControl *mc = MediaControl::getInstance();
    mc->initEntities();

    CameraProfiles cameraProfilesParser(mc, &mStaticCfg);
    // CUSTOM_WEIGHT_GRID_S
    TunningProfiles tunningProfilesParser(&mStaticCfg);
    // CUSTOM_WEIGHT_GRID_E
    PolicyProfiles policyProfilesParser(&mStaticCfg);
    CLEAR(mKnownCPFConfigurations);
}

PlatformData::~PlatformData() {
    deinitCpfStore();
    // CUSTOM_WEIGHT_GRID_S
    deinitWeightGridTable();
    // CUSTOM_WEIGHT_GRID_E
    releaseGraphConfigNodes();
    MediaControl::releaseInstance();
}

void PlatformData::deinitCpfStore()
{
    int cameraNumber = mStaticCfg.mCameras.size();
    for (int i=0; i<cameraNumber; i++) {
        delete mKnownCPFConfigurations[i];
        mKnownCPFConfigurations[i] = nullptr;
    }
}

// CUSTOM_WEIGHT_GRID_S
void PlatformData::deinitWeightGridTable()
{
    int cameraNum = mStaticCfg.mCameras.size();
    for (int i = 0; i < cameraNum; i++) {
        PlatformData::StaticCfg::CameraInfo *pCam = &(mStaticCfg.mCameras[i]);
        for (auto& wg : pCam->mWGTable) {
            if (wg.table) {
                delete [] wg.table;
                wg.table = nullptr;
            }
        }
    }
}
// CUSTOM_WEIGHT_GRID_E

void PlatformData::releaseGraphConfigNodes()
{
#if !defined(BYPASS_MODE) && !defined(USE_STATIC_GRAPH)
    for (PlatformData::StaticCfg::CameraInfo& info : mStaticCfg.mCameras) {
        delete info.mGCMNodes;
        info.mGCMNodes = nullptr;
    }
    for (uint8_t cameraId = 0; cameraId < mStaticCfg.mCameras.size(); cameraId++) {
        IGraphConfigManager::releaseInstance(cameraId);
    }
#endif
}

const char* PlatformData::getSensorName(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].sensorName.c_str();
}

const char* PlatformData::getSensorDescription(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].sensorDescription.c_str();
}

const char* PlatformData::getLensName(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mLensName.c_str();
}

int PlatformData::getLensHwType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mLensHwType;
}

int PlatformData::getDVSType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mDVSType;
}

int PlatformData::getCITMaxMargin(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mCITMaxMargin;
}

bool PlatformData::isEnableAIQ(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mEnableAIQ;
}

bool PlatformData::isStatsTriggered(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mStatsTrigger;
}

bool PlatformData::isEnableLtmThread(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mEnableLtmThread;
}

void PlatformData::getDolVbpOffset(int cameraId, vector<int>& dolVbpOffset)
{
    dolVbpOffset = getInstance()->mStaticCfg.mCameras[cameraId].mDolVbpOffset;
}

bool PlatformData::getSensorOBSetting(int cameraId, ConfigMode configMode,
                                      OBSetting &obSetting)
{
    if (getInstance()->mStaticCfg.mCameras[cameraId].mSensorOBSettings.empty())
        return false;

    for (auto& curSetting : getInstance()->mStaticCfg.mCameras[cameraId].mSensorOBSettings) {
        if (curSetting.configMode == configMode) {
            obSetting = curSetting;
            return true;
        }
    }
    return false;
}

bool PlatformData::isUsingSensorDigitalGain(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mUseSensorDigitalGain;
}

bool PlatformData::isUsingIspDigitalGain(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mUseIspDigitalGain;
}

bool PlatformData::isNeedToPreRegisterBuffer(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mNeedPreRegisterBuffers;
}

int PlatformData::getAutoSwitchType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mAutoSwitchType;
}

bool PlatformData::isEnableFrameSyncCheck(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mFrameSyncCheckEnabled;
}

bool PlatformData::isEnableDefog(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mEnableLtmDefog;
}

bool PlatformData::isEnableHDR(int cameraId)
{
    return (getInstance()->mStaticCfg.mCameras[cameraId].mHdrExposureType != HDR_EXPOSURE_NONE);
}

int PlatformData::getExposureNum(int cameraId, bool hdrEnabled)
{
    if (hdrEnabled) {
        return getInstance()->mStaticCfg.mCameras[cameraId].mHdrExposureNum;
    }

    int exposureNum = 1;
    if (PlatformData::isDolShortEnabled(cameraId))
        exposureNum++;

    if (PlatformData::isDolMediumEnabled(cameraId))
        exposureNum++;

    return exposureNum;
}

int PlatformData::getHDRExposureType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mHdrExposureType;
}

int PlatformData::getHDRStatsInputBitDepth(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mHdrStatsInputBitDepth;
}

int PlatformData::getHDRStatsOutputBitDepth(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mHdrStatsOutputBitDepth;
}

int PlatformData::isUseFixedHDRExposureInfo(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mUseFixedHdrExposureInfo;
}

int PlatformData::getHDRGainType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mHdrGainType;
}

bool PlatformData::isSkipFrameOnSTR2MMIOErr(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mSkipFrameV4L2Error;
}

unsigned int PlatformData::getInitialSkipFrame(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mInitialSkipFrame;
}

unsigned int PlatformData::getTopBottomReverse(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mTopBottomReverse;
}

unsigned int PlatformData::getPreferredBufQSize(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mPreferredBufQSize;
}

unsigned int PlatformData::getPipeSwitchDelayFrame(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mPipeSwitchDelayFrame;
}

int PlatformData::getLtmGainLag(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mLtmGainLag;
}

int PlatformData::getMaxSensorDigitalGain(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mMaxSensorDigitalGain;
}

SensorDgType PlatformData::sensorDigitalGainType(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mSensorDgType;
}

int PlatformData::getExposureLag(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mExposureLag;
}

int PlatformData::getGainLag(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mGainLag;
}

// CUSTOM_WEIGHT_GRID_S
/*
 * According the cameraid, width and height to get the weight grid table.
 * Use index to get the corresponding one in the matching list.
 */
WeightGridTable* PlatformData::getWeightGrild(int cameraId, unsigned short width, unsigned short height, int index)
{
    int matchingCount = 0;
    PlatformData::StaticCfg::CameraInfo *pCam = &getInstance()->mStaticCfg.mCameras[cameraId];

    for (size_t i = 0; i < pCam->mWGTable.size(); i++) {
        if (pCam->mWGTable[i].width == width &&
            pCam->mWGTable[i].height == height) {
            matchingCount++;
            if (matchingCount == index) {
                return &(pCam->mWGTable[i]);
            }
        }
    }

    LOGW("Required index(%d) exceeds the count of matching tables (%d). Size %dx%d, camera %d",
         index, matchingCount, width, height, cameraId);
    return nullptr;
}
// CUSTOM_WEIGHT_GRID_E

PolicyConfig* PlatformData::getExecutorPolicyConfig(int graphId)
{
    size_t i = 0;
    PlatformData::StaticCfg *cfg = &getInstance()->mStaticCfg;

    for (i = 0; i < cfg->mPolicyConfig.size(); i++) {
        if (graphId == cfg->mPolicyConfig[i].graphId) {
            return &(cfg->mPolicyConfig[i]);
        }
    }

    LOGW("Couldn't find the executor policy for graphId(%d), please check xml file", graphId);
    return nullptr;
}

// CUSTOMIZED_3A_S
const char* PlatformData::getCustomizedAicLibraryName(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mCustomAicLibraryName.c_str();
}

const char* PlatformData::getCustomized3ALibraryName(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mCustom3ALibraryName.c_str();
}
// CUSTOMIZED_3A_E

int PlatformData::numberOfCameras()
{
    return getInstance()->mStaticCfg.mCameras.size();
}

MediaCtlConf *PlatformData::getMediaCtlConf(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mCurrentMcConf;
}

int PlatformData::getCameraInfo(int cameraId, camera_info_t& info)
{
    // TODO correct the version info
    info.device_version = 1;
    info.facing = getInstance()->mStaticCfg.mCameras[cameraId].mFacing;
    info.orientation= getInstance()->mStaticCfg.mCameras[cameraId].mOrientation;
    info.name = getSensorName(cameraId);
    info.description = getSensorDescription(cameraId);
    info.capability = &getInstance()->mStaticCfg.mCameras[cameraId].mCapability;
    info.vc.total_num = 0;
    if (getInstance()->mStaticCfg.mCameras[cameraId].mVirtualChannel) {
        info.vc.total_num = getInstance()->mStaticCfg.mCameras[cameraId].mVCNum;
        info.vc.sequence = getInstance()->mStaticCfg.mCameras[cameraId].mVCSeq;
        info.vc.group = getInstance()->mStaticCfg.mCameras[cameraId].mVCGroupId;
    }

    info.metadataSize = getInstance()->mStaticCfg.mCameras[cameraId].mMetadataSize;
    return OK;
}

bool PlatformData::isFeatureSupported(int cameraId, camera_features feature)
{
    camera_features_list_t features;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedFeatures(features);

    if (features.empty()) {
        return false;
    }
    for (auto& item : features) {
        if (item == feature) {
            return true;
        }
    }
    return false;
}

bool PlatformData::isSupportedStream(int cameraId, const stream_t& conf)
{
    int width = conf.width;
    int height = conf.height;
    int format = conf.format;
    int field = conf.field;

    supported_stream_config_array_t availableConfigs;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedStreamConfig(availableConfigs);
    bool sameConfigFound = false;
    for (auto const& config : availableConfigs) {
        if (config.format == format && config.field == field
                && config.width == width && config.height == height) {
            sameConfigFound = true;
            break;
        }
    }

    return sameConfigFound;
}

void PlatformData::getSupportedISysSizes(int cameraId, vector <camera_resolution_t>& resolutions)
{
    resolutions = getInstance()->mStaticCfg.mCameras[cameraId].mSupportedISysSizes;
}

bool PlatformData::getSupportedISysFormats(int cameraId, vector <int>& formats)
{
    formats = getInstance()->mStaticCfg.mCameras[cameraId].mSupportedISysFormat;

    return true;
}

int PlatformData::getISysFormat(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mISysFourcc;
}

/**
 * The ISYS format is determined by the steps below:
 * 1. Try to use the specified format in media control config if it exists.
 * 2. If the given format is supported by ISYS, then use it.
 * 3. Use the first supported format if still could not find an appropriate one.
 */
void PlatformData::selectISysFormat(int cameraId, int format)
{
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    if (mc != nullptr && mc->format != -1) {
        getInstance()->mStaticCfg.mCameras[cameraId].mISysFourcc = mc->format;
    } else if (isISysSupportedFormat(cameraId, format)) {
        getInstance()->mStaticCfg.mCameras[cameraId].mISysFourcc = format;
    } else {
        // Set the first one in support list to default Isys output.
        vector <int> supportedFormat =
            getInstance()->mStaticCfg.mCameras[cameraId].mSupportedISysFormat;
        getInstance()->mStaticCfg.mCameras[cameraId].mISysFourcc = supportedFormat[0];
    }
}

/**
 * The media control config is determined by the steps below:
 * 1. Check if can get one from the given MC ID.
 * 2. And then, try to use ConfigMode to find matched one.
 * 3. Use stream config to get a corresponding mc id, and then get the config by id.
 * 4. Return nullptr if still could not find an appropriate one.
 */
void PlatformData::selectMcConf(int cameraId, stream_t stream, ConfigMode mode, int mcId)
{
    const StaticCfg::CameraInfo& pCam = getInstance()->mStaticCfg.mCameras[cameraId];

    MediaCtlConf* mcConfig = getMcConfByMcId(pCam, mcId);
    if (!mcConfig) {
        mcConfig = getMcConfByConfigMode(pCam, stream, mode);
    }

    if (!mcConfig) {
        mcConfig = getMcConfByStream(pCam, stream);
    }

    getInstance()->mStaticCfg.mCameras[cameraId].mCurrentMcConf = mcConfig;

    if (!mcConfig) {
        LOGE("No matching McConf: cameraId %d, configMode %d, mcId %d", cameraId, mode, mcId);
    }
}

/*
 * Find the MediaCtlConf based on the given MC id.
 */
MediaCtlConf* PlatformData::getMcConfByMcId(const StaticCfg::CameraInfo& cameraInfo, int mcId)
{
    if (mcId == -1) {
        return nullptr;
    }

    for (auto& mc : cameraInfo.mMediaCtlConfs) {
        if (mcId == mc.mcId) {
            return (MediaCtlConf*)&mc;
        }
    }

    return nullptr;
}

/*
 * Find the MediaCtlConf based on MC id in mStreamToMcMap.
 */
MediaCtlConf* PlatformData::getMcConfByStream(const StaticCfg::CameraInfo& cameraInfo,
                                              const stream_t& stream)
{
    int mcId = -1;
    for (auto& table : cameraInfo.mStreamToMcMap) {
        for(auto& config : table.second) {
            if (config.format == stream.format && config.field == stream.field
                    && config.width == stream.width && config.height == stream.height) {
                mcId = table.first;
                break;
            }
        }
        if (mcId != -1) {
            break;
        }
    }

    return getMcConfByMcId(cameraInfo, mcId);
}

/*
 * Find the MediaCtlConf based on operation mode and stream info.
 */
MediaCtlConf* PlatformData::getMcConfByConfigMode(const StaticCfg::CameraInfo& cameraInfo,
                                                  const stream_t& stream, ConfigMode mode)
{
    for (auto& mc : cameraInfo.mMediaCtlConfs) {
        for (auto& cfgMode : mc.configMode) {
            if (mode != cfgMode) continue;

            int outputWidth = mc.outputWidth;
            int outputHeight = mc.outputHeight;
            /*
             * outputWidth and outputHeight is 0 means the ISYS output size
             * is dynamic, we don't need to check if it matches with stream config.
             */
            if ((outputWidth == 0 && outputHeight == 0 ) ||
                (stream.width == outputWidth && stream.height == outputHeight)) {
                return (MediaCtlConf*)&mc;
            }
        }
    }

    return nullptr;
}

/*
 * Check if video node is enabled via camera Id and video node type.
 */
bool PlatformData::isVideoNodeEnabled(int cameraId, VideoNodeType type) {
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    if (!mc) return false;

    for(auto const& nd : mc->videoNodes) {
        if (type == nd.videoNodeType) {
            return true;
        }
    }
    return false;
}

bool PlatformData::isISysSupportedFormat(int cameraId, int format)
{
    vector <int> supportedFormat;
    getSupportedISysFormats(cameraId, supportedFormat);

    for (auto const fmt : supportedFormat) {
        if (format == fmt)
            return true;
    }
    return false;
}

bool PlatformData::isISysSupportedResolution(int cameraId, camera_resolution_t resolution)
{
    vector <camera_resolution_t> res;
    getSupportedISysSizes(cameraId, res);

    for (auto const& size : res) {
        if (resolution.width == size.width && resolution.height== size.height)
            return true;
    }

    return false;
}

bool PlatformData::isISysScaleEnabled(int cameraId)
{
    return isVideoNodeEnabled(cameraId, VIDEO_ISA_SCALE);
}

int PlatformData::getISysRawFormat(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mISysRawFormat;
}

stream_t PlatformData::getIsaScaleRawConfig(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mIsaScaleRawConfig;
}

stream_t PlatformData::getISysOutputByPort(int cameraId, Port port)
{
    stream_t config;
    CLEAR(config);

    MediaCtlConf *mc = PlatformData::getMediaCtlConf(cameraId);
    Check(!mc, config, "Invalid media control config.");

    for (const auto& output : mc->outputs) {
        if (output.port == port) {
            config.format  = output.v4l2Format;
            config.width   = output.width;
            config.height  = output.height;
            break;
        }
    }

    return config;
}

bool PlatformData::isIsaEnabled(int cameraId)
{
    return isVideoNodeEnabled(cameraId, VIDEO_ISA_CONFIG);
}

bool PlatformData::isDolShortEnabled(int cameraId)
{
    return isVideoNodeEnabled(cameraId, VIDEO_GENERIC_SHORT_EXPO);
}

bool PlatformData::isDolMediumEnabled(int cameraId)
{
    return isVideoNodeEnabled(cameraId, VIDEO_GENERIC_MEDIUM_EXPO);
}

bool PlatformData::isCsiMetaEnabled(int cameraId)
{
    // FILE_SOURCE_S
    if (isFileSourceEnabled()) return false;
    // FILE_SOURCE_E
    return isVideoNodeEnabled(cameraId, VIDEO_CSI_META);
}

int PlatformData::getFixedVbp(int cameraId)
{
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    if (!mc) {
        LOGW("%s: Failed to get MC for fixed VBP, disable fixed VBP.", __func__);
        return -1;
    }
    return mc->vbp;
}

bool PlatformData::needHandleVbpInMetaData(int cameraId, ConfigMode configMode)
{
    vector<int> vbpOffset;
    int fixedVbp;

    if (configMode != CAMERA_STREAM_CONFIGURATION_MODE_HDR) {
        return false;
    }

    // Fixed VBP take higher priority when both fixed and dynamic VBP are configured
    fixedVbp = getFixedVbp(cameraId);
    if (fixedVbp >= 0) {
        LOG2("%s: fixed VBP configure detected, no need to handle VBP in meta", __func__);
        return false;
    }

    PlatformData::getDolVbpOffset(cameraId, vbpOffset);
    if (vbpOffset.size() > 0) {
        return true;
    }

    return false;
}

bool PlatformData::needSetVbp(int cameraId, ConfigMode configMode)
{
    vector<int> vbpOffset;
    int fixedVbp;

    if (configMode != CAMERA_STREAM_CONFIGURATION_MODE_HDR) {
        return false;
    }

    fixedVbp = getFixedVbp(cameraId);
    if (fixedVbp >= 0) {
        LOG2("%s: Fixed VBP configure detected, value %d", __func__, fixedVbp);
        return true;
    }

    PlatformData::getDolVbpOffset(cameraId, vbpOffset);
    if (vbpOffset.size() > 0) {
        LOG2("%s: Dynamic VBP configure detected", __func__);
        return true;
    }

    return false;

}

int PlatformData::getFormatByDevName(int cameraId, const string& devName, McFormat& format)
{
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    Check(!mc, BAD_VALUE, "getMediaCtlConf returns nullptr, cameraId:%d", cameraId);

    for (auto &fmt : mc->formats) {
        if (fmt.formatType == FC_FORMAT && devName == fmt.entityName) {
            format = fmt;
            return OK;
        }
    }

    LOGE("Failed to find DevName for cameraId: %d, devname: %s", cameraId, devName.c_str());
    return BAD_VALUE;
}

int PlatformData::getVideoNodeNameByType(int cameraId, VideoNodeType videoNodeType, string& videoNodeName)
{
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    Check(!mc, BAD_VALUE, "getMediaCtlConf returns nullptr, cameraId:%d", cameraId);

    for(auto const& nd : mc->videoNodes) {
        if (videoNodeType == nd.videoNodeType) {
            videoNodeName = nd.name;
            return OK;
        }
    }

    LOGE("failed to find video note name for cameraId: %d", cameraId);
    return BAD_VALUE;
}

int PlatformData::getDevNameByType(int cameraId, VideoNodeType videoNodeType, string& devName)
{
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    bool isSubDev = false;

    switch (videoNodeType) {
        case VIDEO_PIXEL_ARRAY:
        case VIDEO_PIXEL_BINNER:
        case VIDEO_PIXEL_SCALER:
        {
            isSubDev = true;
            // For sensor subdevices are fixed and sensor HW may be initialized before configure,
            // the first MediaCtlConf is used to find sensor subdevice name.
            PlatformData::StaticCfg::CameraInfo *pCam = &getInstance()->mStaticCfg.mCameras[cameraId];
            mc = &pCam->mMediaCtlConfs[0];
            break;
        }
        case VIDEO_ISYS_RECEIVER_BACKEND:
        case VIDEO_ISYS_RECEIVER:
        case VIDEO_ISA_DEVICE:
        {
            isSubDev = true;
            break;
        }
        default:
            break;
    }

    Check(!mc, NAME_NOT_FOUND, "failed to get MediaCtlConf, videoNodeType %d", videoNodeType);

    for(auto& nd : mc->videoNodes) {
        if (videoNodeType == nd.videoNodeType) {
            string tmpDevName;
            CameraUtils::getDeviceName(nd.name.c_str(), tmpDevName, isSubDev);
            if (!tmpDevName.empty()) {
                devName = tmpDevName;
                LOG2("@%s, Found DevName. cameraId: %d, get video node: %s, devname: %s",
                      __func__, cameraId, nd.name.c_str(), devName.c_str());
                return OK;
            } else {
                // Use default device name if cannot find it
                if (isSubDev)
                    devName = "/dev/v4l-subdev1";
                else
                    devName = "/dev/video5";
                LOGE("Failed to find DevName for cameraId: %d, get video node: %s, devname: %s",
                      cameraId, nd.name.c_str(), devName.c_str());
                return NAME_NOT_FOUND;
            }
        }
    }

    LOGW("Failed to find devname for cameraId: %d, use default setting instead", cameraId);
    return NAME_NOT_FOUND;
}

/**
 * The ISYS best resolution is determined by the steps below:
 * 1. If the resolution is specified in MediaCtlConf, then use it.
 * 2. Try to find the exact matched one in ISYS supported resolutions.
 * 3. Try to find the same ratio resolution.
 * 4. If still couldn't get one, then use the biggest one.
 */
camera_resolution_t PlatformData::getISysBestResolution(int cameraId, int width,
                                                        int height, int field)
{
    LOG1("@%s, width:%d, height:%d", __func__, width, height);

    // Skip for interlace, we only support by-pass in interlaced mode
    if (field == V4L2_FIELD_ALTERNATE) {
        return {width, height};
    }

    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    // The isys output size is fixed if outputWidth/outputHeight != 0
    // So we use it to as the ISYS resolution.
    if (mc != nullptr && mc->outputWidth != 0 && mc->outputHeight != 0) {
        return {mc->outputWidth, mc->outputHeight};
    }

    const float RATIO_TOLERANCE = 0.05f; // Supported aspect ratios that are within RATIO_TOLERANCE
    const float kTargetRatio = (float)width / height;

    vector <camera_resolution_t> res;
    // The supported resolutions are saved in res with ascending order(small -> bigger)
    getSupportedISysSizes(cameraId, res);

    // Try to find out the same resolution in the supported isys resolution list
    // if it couldn't find out the same one, then use the bigger one which is the same ratio
    for (auto const& size : res) {
        if (width <= size.width && height <= size.height &&
            fabs((float)size.width/size.height - kTargetRatio) < RATIO_TOLERANCE) {
            LOG1("@%s: Found the best ISYS resoltoution (%d)x(%d)", __func__,
                 size.width, size.height);
            return {size.width, size.height};
        }
    }

    // If it still couldn't find one, then use the biggest one in the supported list.
    LOG1("@%s: ISYS resolution not found, used the biggest one: (%d)x(%d)",
         __func__, res.back().width, res.back().height);
    return {res.back().width, res.back().height};
}

int PlatformData::calculateFrameParams(int cameraId, SensorFrameParams& sensorFrameParams)
{
    CLEAR(sensorFrameParams);

    uint32_t width = 0;
    uint32_t horizontalOffset = 0;
    uint32_t horizontalBinNum = 1;
    uint32_t horizontalBinDenom = 1;
    uint32_t horizontalBin = 1;

    uint32_t height = 0;
    uint32_t verticalOffset = 0;
    uint32_t verticalBinNum = 1;
    uint32_t verticalBinDenom = 1;
    uint32_t verticalBin = 1;

    /**
     * For this function, it may be called without configuring stream
     * in some UT cases, the mc is nullptr at this moment. So we need to
     * get one default mc to calculate frame params.
     */
    MediaCtlConf *mc = PlatformData::getMediaCtlConf(cameraId);
    if (mc == nullptr) {
        PlatformData::StaticCfg::CameraInfo *pCam = &getInstance()->mStaticCfg.mCameras[cameraId];
        mc = &pCam->mMediaCtlConfs[0];
    }

    bool pixArraySizeFound = false;
    for (auto const& current : mc->formats) {
        if (!pixArraySizeFound && current.width > 0 && current.height > 0) {
            width = current.width;
            height = current.height;
            pixArraySizeFound = true;
            LOG2("%s: active pixel array H=%d, W=%d", __func__, height, width);
            //Setup initial sensor frame params.
            sensorFrameParams.horizontal_crop_offset += horizontalOffset;
            sensorFrameParams.vertical_crop_offset += verticalOffset;
            sensorFrameParams.cropped_image_width = width;
            sensorFrameParams.cropped_image_height = height;
            sensorFrameParams.horizontal_scaling_numerator = horizontalBinNum;
            sensorFrameParams.horizontal_scaling_denominator = horizontalBinDenom;
            sensorFrameParams.vertical_scaling_numerator = verticalBinNum;
            sensorFrameParams.vertical_scaling_denominator = verticalBinDenom;
        }

        if (current.formatType != FC_SELECTION) {
            continue;
        }

        if (current.selCmd == V4L2_SEL_TGT_CROP) {

            width = current.width * horizontalBin;
            horizontalOffset = current.left * horizontalBin;
            height = current.height * verticalBin;
            verticalOffset = current.top * verticalBin;

            LOG2("%s: crop (binning factor: hor/vert:%d,%d)"
                  , __func__, horizontalBin, verticalBin);

            LOG2("%s: crop left = %d, top = %d, width = %d height = %d",
                  __func__, horizontalOffset, verticalOffset, width, height);

        } else if (current.selCmd == V4L2_SEL_TGT_COMPOSE) {
            if (width == 0 || height == 0) {
                LOGE("Invalid XML configuration, no pixel array width/height when handling compose, skip.");
                return BAD_VALUE;
            }
            if (current.width == 0 || current.height == 0) {
                LOGW("%s: Invalid XML configuration for TGT_COMPOSE,"
                     "0 value detected in width or height", __func__);
                return BAD_VALUE;
            } else {
                LOG2("%s: Compose width %d/%d, height %d/%d", __func__, width, current.width,
                    height, current.height);
                // the scale factor should be float, so multiple numerator and denominator
                // with coefficient to indicate float factor
                const int SCALE_FACTOR_COEF = 10;
                horizontalBin = width / current.width;
                horizontalBinNum = width * SCALE_FACTOR_COEF / current.width;
                horizontalBinDenom = SCALE_FACTOR_COEF;
                verticalBin = height / current.height;
                verticalBinNum = height * SCALE_FACTOR_COEF / current.height;
                verticalBinDenom = SCALE_FACTOR_COEF;
            }

            LOG2("%s: COMPOSE horizontal bin factor=%d, (%d/%d)",
                  __func__, horizontalBin, horizontalBinNum, horizontalBinDenom);
            LOG2("%s: COMPOSE vertical bin factor=%d, (%d/%d)",
                  __func__, verticalBin, verticalBinNum, verticalBinDenom);
        } else {
            LOGW("%s: Target for selection is not CROP neither COMPOSE!", __func__);
            continue;
        }

        sensorFrameParams.horizontal_crop_offset += horizontalOffset;
        sensorFrameParams.vertical_crop_offset += verticalOffset;
        sensorFrameParams.cropped_image_width = width;
        sensorFrameParams.cropped_image_height = height;
        sensorFrameParams.horizontal_scaling_numerator = horizontalBinNum;
        sensorFrameParams.horizontal_scaling_denominator = horizontalBinDenom;
        sensorFrameParams.vertical_scaling_numerator = verticalBinNum;
        sensorFrameParams.vertical_scaling_denominator = verticalBinDenom;
    }

    return OK;

}

void PlatformData::getSupportedTuningConfig(int cameraId, vector <TuningConfig> &configs)
{
    configs = getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig;
}

bool PlatformData::usePsys(int cameraId, int format)
{
    if (getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig.empty()) {
        LOG1("@%s, the tuning config in xml does not exist", __func__);
        return false;
    }

    if (getInstance()->mStaticCfg.mCameras[cameraId].mPSysFormat.empty()) {
        LOG1("@%s, the psys supported format does not exist", __func__);
        return false;
    }

    for (auto &psys_fmt : getInstance()->mStaticCfg.mCameras[cameraId].mPSysFormat) {
        if (format == psys_fmt)
            return true;
    }

    LOGW("%s, No matched format found, but expected format:%s", __func__,
        CameraUtils::pixelCode2String(format));

    return false;
}

int PlatformData::getConfigModesByOperationMode(int cameraId, uint32_t operationMode, vector <ConfigMode> &configModes)
{
    Check(getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig.empty(), INVALID_OPERATION,
          "@%s, the tuning config in xml does not exist", __func__);

    if (operationMode == CAMERA_STREAM_CONFIGURATION_MODE_AUTO) {
        if (getInstance()->mStaticCfg.mCameras[cameraId].mConfigModesForAuto.empty()) {
            // Use the first config mode as default for auto
            configModes.push_back(getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig[0].configMode);
            LOG2("%s: add config mode %d for operation mode %d", __func__, configModes[0], operationMode);
        } else {
            configModes = getInstance()->mStaticCfg.mCameras[cameraId].mConfigModesForAuto;
        }
    } else {
        for (auto &cfg : getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig) {
            if (operationMode == (uint32_t)cfg.configMode) {
                configModes.push_back(cfg.configMode);
                LOG2("%s: add config mode %d for operation mode %d", __func__, cfg.configMode, operationMode);
            }
        }
    }

    if (configModes.size() > 0) return OK;
    LOGW("%s, configure number %zu, operationMode %x, cameraId %d", __func__,
            configModes.size(), operationMode, cameraId);
    return INVALID_OPERATION;
}

int PlatformData::getTuningModeByConfigMode(int cameraId, ConfigMode configMode,
                                            TuningMode& tuningMode)
{
    Check(getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig.empty(),
          INVALID_OPERATION, "the tuning config in xml does not exist");

    for (auto &cfg : getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig) {
        LOG2("%s, tuningMode %d, configMode %x", __func__, cfg.tuningMode, cfg.configMode);
        if (cfg.configMode == configMode) {
            tuningMode = cfg.tuningMode;
            return OK;
        }
    }

    LOGW("%s, configMode %x, cameraId %d, no tuningModes", __func__, configMode, cameraId);
    return INVALID_OPERATION;
}

int PlatformData::getTuningConfigByConfigMode(int cameraId, ConfigMode mode, TuningConfig &config)
{
    Check(getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig.empty(), INVALID_OPERATION,
          "@%s, the tuning config in xml does not exist.", __func__);

    for (auto &cfg : getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig) {
        if (cfg.configMode == mode) {
            config = cfg;
            return OK;
        }
    }

    LOGW("%s, configMode %x, cameraId %d, no TuningConfig", __func__, mode, cameraId);
    return INVALID_OPERATION;
}

int PlatformData::getLardTagsByTuningMode(int cameraId, TuningMode mode, LardTagConfig &lardTags)
{
    if (getInstance()->mStaticCfg.mCameras[cameraId].mLardTagsConfig.empty()) {
        LOG1("@%s, the lardTags config does not exist", __func__);
        return NAME_NOT_FOUND;
    }

    for (auto &cfg : getInstance()->mStaticCfg.mCameras[cameraId].mLardTagsConfig) {
        if (cfg.tuningMode == mode) {
            lardTags = cfg;
            return OK;
        }
    }

    LOG1("@%s, the lard tag config does not exist for mode %d", __func__, mode);
    return NAME_NOT_FOUND;
}

int PlatformData::getStreamIdByConfigMode(int cameraId, ConfigMode configMode)
{
    map<int, int> modeMap = getInstance()->mStaticCfg.mCameras[cameraId].mConfigModeToStreamId;
    return modeMap.find(configMode) == modeMap.end() ? -1 : modeMap[configMode];
}

int PlatformData::getMaxRequestsInflight(int cameraId)
{
    return isEnableAIQ(cameraId) ? 4 : MAX_BUFFER_COUNT;
}

GraphConfigNodes* PlatformData::getGraphConfigNodes(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mGCMNodes;
}

camera_yuv_color_range_mode_t PlatformData::getYuvColorRangeMode(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mYuvColorRangeMode;
}

// load cpf when tuning file (.aiqb) is available
CpfStore* PlatformData::getCpfStore(int cameraId)
{
#ifndef BYPASS_MODE
    // Aiqb tuning file is configured in mSupportedTuningConfig
    if (!getInstance()->mStaticCfg.mCameras[cameraId].mSupportedTuningConfig.empty()) {
        if (getInstance()->mKnownCPFConfigurations[cameraId] == nullptr) {
            getInstance()->mKnownCPFConfigurations[cameraId]
                = new CpfStore(cameraId, getInstance()->mStaticCfg.mCameras[cameraId].sensorName);
        }
        return getInstance()->mKnownCPFConfigurations[cameraId];
    }
#endif
    return nullptr;
}

bool PlatformData::isCSIFrontEndCapture(int cameraId)
{
    bool isCsiFeCapture = false;
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    Check(!mc, false, "getMediaCtlConf returns nullptr, cameraId:%d", cameraId);

    for(const auto& node : mc->videoNodes) {
        if (node.videoNodeType == VIDEO_GENERIC &&
                (node.name.find("CSI-2") != string::npos ||
                 node.name.find("TPG") != string::npos)) {
            isCsiFeCapture = true;
            break;
        }
    }
    return isCsiFeCapture;
}

bool PlatformData::isTPGReceiver(int cameraId)
{
    bool isTPGCapture = false;
    MediaCtlConf *mc = getMediaCtlConf(cameraId);
    Check(!mc, false, "getMediaCtlConf returns nullptr, cameraId:%d", cameraId);

    for(const auto& node : mc->videoNodes) {
        if (node.videoNodeType == VIDEO_ISYS_RECEIVER &&
                (node.name.find("TPG") != string::npos)) {
            isTPGCapture = true;
            break;
        }
    }
    return isTPGCapture;
}

int PlatformData::getSupportAeExposureTimeRange(int cameraId, camera_scene_mode_t sceneMode,
                                                camera_range_t& etRange)
{
    vector<camera_ae_exposure_time_range_t> ranges;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedAeExposureTimeRange(ranges);

    if (ranges.empty())
        return NAME_NOT_FOUND;

    for (auto& item : ranges) {
        if (item.scene_mode == sceneMode) {
            etRange = item.et_range;
            return OK;
        }
    }
    return NAME_NOT_FOUND;
}

int PlatformData::getSupportAeGainRange(int cameraId, camera_scene_mode_t sceneMode,
                                        camera_range_t& gainRange)
{
    vector<camera_ae_gain_range_t> ranges;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedAeGainRange(ranges);

    if(ranges.empty()) {
        return NAME_NOT_FOUND;
    }

    for (auto& item : ranges) {
        if (item.scene_mode == sceneMode) {
            gainRange = item.gain_range;
            return OK;
        }
    }
    return NAME_NOT_FOUND;
}

// LITE_PROCESSING_S
bool PlatformData::needKeepFpsDuringDeinterlace(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mKeepFpsDuringDeinterlace;
}
// LITE_PROCESSING_E

bool PlatformData::isUsingCrlModule(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mUseCrlModule;
}

vector<MultiExpRange> PlatformData::getMultiExpRanges(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mMultiExpRanges;
}

vector<uint32_t> PlatformData::getSupportedIspControlFeatures(int cameraId)
{
    vector<uint32_t> features;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedIspControlFeatures(features);
    return features;
}

bool PlatformData::isIspControlFeatureSupported(int cameraId, uint32_t ctrlId)
{
    vector<uint32_t> features;
    getInstance()->mStaticCfg.mCameras[cameraId].mCapability.getSupportedIspControlFeatures(features);
    for (auto & id : features) {
        if (id == ctrlId) {
            return true;
        }
    }

    return false;
}

// FILE_SOURCE_S
const char* PlatformData::getInjectedFile()
{
    const char* PROP_CAMERA_FILE_INJECTION = "cameraInjectFile";
    return getenv(PROP_CAMERA_FILE_INJECTION);
}

bool PlatformData::isFileSourceEnabled()
{
    return getInjectedFile() != nullptr;
}
// FILE_SOURCE_E

int PlatformData::getVirtualChannelSequence(int cameraId)
{
    if (getInstance()->mStaticCfg.mCameras[cameraId].mVirtualChannel) {
        return getInstance()->mStaticCfg.mCameras[cameraId].mVCSeq;
    }

    return -1;
}

unsigned int PlatformData::getMetadataSize(int cameraId)
{
    return getInstance()->mStaticCfg.mCameras[cameraId].mMetadataSize;
}
} // namespace icamera
