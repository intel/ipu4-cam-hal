/*
 * Copyright (C) 2015-2018 Intel Corporation
 * Copyright 2008-2017, The Android Open Source Project
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
#define LOG_TAG "CameraProfiles"

#include <string.h>
#include <expat.h>
// CRL_MODULE_S
#include <linux/crlmodule.h>
// CRL_MODULE_E

#include "isp_control/IspControlUtils.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"
#include "metadata/ParameterHelper.h"

#include "PlatformData.h"
#include "CameraProfiles.h"

#if !defined(BYPASS_MODE) && !defined(USE_STATIC_GRAPH)
#include "gc/GraphConfigManager.h"
#endif

namespace icamera {

CameraProfiles::CameraProfiles(MediaControl *mc, PlatformData::StaticCfg *cfg)
{
    LOG1("@%s", __func__);
    mCurrentSensor = 0;
    mCurrentDataField = FIELD_INVALID;
    mSensorNum = 0;
    pCurrentCam = nullptr;
    mInMediaCtlCfg = false;
    mInStaticMetadata = false;

    mMetadataCache = new long[mMetadataCacheSize];

    mMC = mc;
    mStaticCfg = cfg;
    Check(mc == nullptr || cfg == nullptr, VOID_VALUE, "@%s, passed parameters wrong, mc:%p, data:%p", __func__, mc, cfg);

    getDataFromXmlFile();
    getGraphConfigFromXmlFile();

    if(gLogLevel & CAMERA_DEBUG_LOG_LEVEL2)
        dumpSensorInfo();
}

CameraProfiles::~CameraProfiles()
{
    delete []mMetadataCache;
}

/**
 * This function will check which field that the parser parses to.
 *
 * The field is set to 3 types.
 * FIELD_INVALID FIELD_SENSOR and FIELD_COMMON
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::checkField(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);
    if (strcmp(name, "CameraSettings") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        return;
    } else if (strcmp(name, "Sensor") == 0) {
        profiles->mSensorNum++;
        profiles->mCurrentSensor = profiles->mSensorNum - 1;
        if (profiles->mCurrentSensor >= 0 && profiles->mCurrentSensor < MAX_CAMERA_NUMBER) {
            profiles->pCurrentCam = new PlatformData::StaticCfg::CameraInfo;

            int idx = 0;
            while (atts[idx]) {
                const char* key = atts[idx];
                const char* val = atts[idx + 1];
                LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
                if (strcmp(key, "name") == 0) {
                    profiles->pCurrentCam->sensorName = val;
                } else if (strcmp(key, "description") == 0) {
                    profiles->pCurrentCam->sensorDescription = val;
                } else if (strcmp(key, "virtualChannel") == 0) {
                    profiles->pCurrentCam->mVirtualChannel = strcmp(val, "true") == 0 ? true : false;
                } else if (strcmp(key, "vcNum") == 0) {
                    profiles->pCurrentCam->mVCNum = strtoul(val, nullptr, 10);
                } else if (strcmp(key, "vcSeq") == 0) {
                    profiles->pCurrentCam->mVCSeq = strtoul(val, nullptr, 10);
                } else if (strcmp(key, "vcGroupId") == 0) {
                    profiles->pCurrentCam->mVCGroupId = strtoul(val, nullptr, 10);
                }
                idx += 2;
            }

            profiles->mMetadata.clear();
            profiles->mCurrentDataField = FIELD_SENSOR;

            return;
        }
    } else if (strcmp(name, "Common") == 0) {
        profiles->mCurrentDataField = FIELD_COMMON;
        return;
    }

    LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
    return;
}

int CameraProfiles::parseSensorName(const char *str, vector<string> &sensorNames)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        sensorNames.push_back(tablePtr);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

/**
 * This function will handle all the common related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleCommon(CameraProfiles *profiles, const char *name, const char **atts)
{
    Check(strcmp(atts[0], "value") != 0 || (atts[1] == nullptr), VOID_VALUE
         ,"@%s, name:%s, atts[0]:%s or atts[1] is nullptr, xml format wrong", __func__, name, atts[0]);

    LOGXML("@%s, name:%s, atts[0]:%s, atts[1]: %s", __func__, name, atts[0], atts[1]);
    if (strcmp(name, "version") == 0) {
        (profiles->mStaticCfg->mCommonConfig).xmlVersion = atof(atts[1]);
    } else if (strcmp(name, "platform") == 0) {
        (profiles->mStaticCfg->mCommonConfig).ipuName = atts[1];
    } else if (strcmp(name, "availableSensors") == 0) {
        parseSensorName(atts[1], (profiles->mStaticCfg->mCommonConfig).availableSensors);
    }
}

/**
 * This function will handle all the sensor related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleSensor(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s, profiles->mCurrentSensor:%d", __func__, name, profiles->mCurrentSensor);
    Check(strcmp(atts[0], "value") != 0 || (atts[1] == nullptr), VOID_VALUE
        ,"@%s, name:%s, atts[0]:%s or atts[1] is nullptr, xml format wrong", __func__, name, atts[0]);

    LOGXML("@%s, name:%s, atts[0]:%s, atts[1]:%s", __func__, name, atts[0], atts[1]);
    if (strcmp(name, "supportedISysSizes") == 0) {
        parseSizesList(atts[1], pCurrentCam->mSupportedISysSizes);
        for (const auto &s : pCurrentCam->mSupportedISysSizes)
            LOGXML("@%s, mSupportedISysSizes: width:%d, height:%d", __func__,
                s.width, s.height);
    } else if (strcmp(name, "supportedISysFormat") == 0) {
        getSupportedFormat(atts[1], pCurrentCam->mSupportedISysFormat);
    } else if (strcmp(name, "iSysRawFormat") == 0) {
        pCurrentCam->mISysRawFormat = CameraUtils::string2PixelCode(atts[1]);
    } else if (strcmp(name, "isaScaleRawConfig") == 0) {
        pCurrentCam->mIsaScaleRawConfig = parseIsaScaleRawConfig(atts[1]);
    } else if (strcmp(name, "configModeToStreamId") == 0) {
        char* srcDup = strdup(atts[1]);
        Check(!srcDup, VOID_VALUE, "Create a copy of source string failed.");

        char* endPtr = (char*)strchr(srcDup, ',');
        if (endPtr) {
            *endPtr = 0;
            ConfigMode configMode = CameraUtils::getConfigModeByName(srcDup);
            int streamId = atoi(endPtr + 1);
            pCurrentCam->mConfigModeToStreamId[configMode] = streamId;
        }
        free(srcDup);
    } else if (strcmp(name, "pSysFormat") == 0) {
        getSupportedFormat(atts[1], pCurrentCam->mPSysFormat);
    } else if (strcmp(name, "enableAIQ") == 0) {
        pCurrentCam->mEnableAIQ = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "statsTrigger") == 0) {
        pCurrentCam->mStatsTrigger = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "useCrlModule") == 0) {
        pCurrentCam->mUseCrlModule = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "dolVbpOffset") == 0) {
        parseSupportedIntRange(atts[1], pCurrentCam->mDolVbpOffset);
    } else if (strcmp(name, "sensorOBSettings") == 0) {
        parseSensorOBSettings(atts[1], pCurrentCam->mSensorOBSettings);
    } else if (strcmp(name, "skipFrameV4L2Error") == 0) {
        pCurrentCam->mSkipFrameV4L2Error = strcmp(atts[1], "true") == 0;
    // LITE_PROCESSING_S
    } else if (strcmp(name, "deinterlaceKeepFps") == 0) {
        pCurrentCam->mKeepFpsDuringDeinterlace = strcmp(atts[1], "true") == 0;
    // LITE_PROCESSING_E
    } else if (strcmp(name, "useSensorDigitalGain") == 0) {
        pCurrentCam->mUseSensorDigitalGain = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "useIspDigitalGain") == 0) {
        pCurrentCam->mUseIspDigitalGain = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "preRegisterBuffer") == 0) {
        pCurrentCam->mNeedPreRegisterBuffers = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "enableFrameSyncCheck") == 0) {
        pCurrentCam->mFrameSyncCheckEnabled = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "lensName") == 0) {
        pCurrentCam->mLensName = atts[1];
    } else if (strcmp(name, "lensHwType") == 0) {
        if (strcmp(atts[1], "LENS_VCM_HW") == 0) {
            pCurrentCam->mLensHwType = LENS_VCM_HW;
        } else if (strcmp(atts[1], "LENS_PWM_HW") == 0) {
            pCurrentCam->mLensHwType = LENS_PWM_HW;
        } else {
            LOGE("unknown Lens HW type %s, set to LENS_NONE_HW", atts[1]);
            pCurrentCam->mLensHwType = LENS_NONE_HW;
        }
    } else if (strcmp(name, "autoSwitchType") == 0) {
        if (strcmp(atts[1], "full") == 0) {
            pCurrentCam->mAutoSwitchType = AUTO_SWITCH_FULL;
        } else {
            pCurrentCam->mAutoSwitchType = AUTO_SWITCH_PSYS;
        }
    } else if (strcmp(name, "hdrExposureType") == 0) {
        if (strcmp(atts[1], "fix-exposure-ratio") == 0) {
            pCurrentCam->mHdrExposureType = HDR_FIX_EXPOSURE_RATIO;
        } else if (strcmp(atts[1], "relative-multi-exposures") == 0) {
            pCurrentCam->mHdrExposureType = HDR_RELATIVE_MULTI_EXPOSURES;
        } else if (strcmp(atts[1], "multi-exposures") == 0) {
            pCurrentCam->mHdrExposureType = HDR_MULTI_EXPOSURES;
        } else if (strcmp(atts[1], "dual-exposures-dcg-and-vs") == 0) {
            pCurrentCam->mHdrExposureType = HDR_DUAL_EXPOSURES_DCG_AND_VS;
        } else {
            LOGE("unknown HDR exposure type %s, set to HDR_EXPOSURE_NONE", atts[1]);
            pCurrentCam->mHdrExposureType = HDR_EXPOSURE_NONE;
        }
    } else if (strcmp(name, "lensCloseCode") == 0) {
        pCurrentCam->mLensCloseCode = atoi(atts[1]);
    } else if (strcmp(name, "cITMaxMargin") == 0) {
        pCurrentCam->mCITMaxMargin = atoi(atts[1]);
    } else if (strcmp(name, "ltmGainLag") == 0) {
        pCurrentCam->mLtmGainLag = atoi(atts[1]);
    } else if (strcmp(name, "enableLtmThread") == 0) {
        pCurrentCam->mEnableLtmThread = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "enableLtmDefog") == 0) {
        pCurrentCam->mEnableLtmDefog = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "maxSensorDg") == 0) {
        pCurrentCam->mMaxSensorDigitalGain = atoi(atts[1]);
    } else if (strcmp(name, "sensorDgType") == 0) {
        if (strcmp(atts[1], "type_2_x") == 0) {
            pCurrentCam->mSensorDgType = SENSOR_DG_TYPE_2_X;
        } else if (strcmp(atts[1], "type_x") == 0) {
            pCurrentCam->mSensorDgType = SENSOR_DG_TYPE_X;
        } else {
            LOGE("unknown sensor digital gain type:%s, set to SENSOR_DG_TYPE_NONE", atts[1]);
            pCurrentCam->mSensorDgType = SENSOR_DG_TYPE_NONE;
        }
    } else if (strcmp(name, "exposureLag") == 0) {
        pCurrentCam->mExposureLag = atoi(atts[1]);
    } else if (strcmp(name, "hdrExposureNum") == 0) {
        pCurrentCam->mHdrExposureNum = atoi(atts[1]);
    } else if (strcmp(name, "hdrStatsInputBitDepth") == 0) {
        pCurrentCam->mHdrStatsInputBitDepth = atoi(atts[1]);
    } else if (strcmp(name, "hdrStatsOutputBitDepth") == 0) {
        pCurrentCam->mHdrStatsOutputBitDepth = atoi(atts[1]);
    } else if (strcmp(name, "useFixedHdrExposureInfo") == 0) {
        pCurrentCam->mUseFixedHdrExposureInfo = strcmp(atts[1], "true") == 0;
    } else if (strcmp(name, "hdrGainType") == 0) {
        if (strcmp(atts[1], "multi-dg-and-convertion-ag") == 0) {
            pCurrentCam->mHdrGainType = HDR_MULTI_DG_AND_CONVERTION_AG;
        } else if (strcmp(atts[1], "isp-dg-and-sensor-direct-ag") == 0) {
            pCurrentCam->mHdrGainType = HDR_ISP_DG_AND_SENSOR_DIRECT_AG;
        } else if (strcmp(atts[1], "multi-dg-and-direct-ag") == 0) {
            pCurrentCam->mHdrGainType = HDR_MULTI_DG_AND_DIRECT_AG;
        } else {
            LOGE("unknown HDR gain type %s, set to HDR_GAIN_NONE", atts[1]);
            pCurrentCam->mHdrGainType = HDR_GAIN_NONE;
        }
    } else if (strcmp(name, "graphSettingsFile") == 0) {
        pCurrentCam->mGraphSettingsFile = atts[1];
    } else if (strcmp(name, "gainLag") == 0) {
        pCurrentCam->mGainLag = atoi(atts[1]);
    } else if (strcmp(name, "customAicLibraryName") == 0) {
        pCurrentCam->mCustomAicLibraryName = atts[1];
    } else if (strcmp(name, "custom3ALibraryName") == 0){
        pCurrentCam->mCustom3ALibraryName = atts[1];
    } else if (strcmp(name, "yuvColorRangeMode") == 0) {
        if (strcmp(atts[1],"full") == 0) {
            pCurrentCam->mYuvColorRangeMode = CAMERA_FULL_MODE_YUV_COLOR_RANGE;
        } else if (strcmp(atts[1],"reduced") == 0) {
            pCurrentCam->mYuvColorRangeMode = CAMERA_REDUCED_MODE_YUV_COLOR_RANGE;
        }
    } else if (strcmp(name, "initialSkipFrame") == 0) {
        pCurrentCam->mInitialSkipFrame = atoi(atts[1]);
    }  else if (strcmp(name, "topBottomReverse") == 0) {
        pCurrentCam->mTopBottomReverse = atoi(atts[1]);
    } else if (strcmp(name, "preferredBufQSize") == 0) {
        pCurrentCam->mPreferredBufQSize = atoi(atts[1]);
    } else if (strcmp(name, "pipeSwitchDelayFrame") == 0) {
        pCurrentCam->mPipeSwitchDelayFrame = atoi(atts[1]);
    } else if (strcmp(name, "supportedTuningConfig") == 0) {
        parseSupportedTuningConfig(atts[1], pCurrentCam->mSupportedTuningConfig);
    } else if (strcmp(name, "lardTags") == 0) {
        parseLardTags(atts[1], pCurrentCam->mLardTagsConfig);
    } else if (strcmp(name, "availableConfigModeForAuto") == 0) {
        parseConfigModeForAuto(atts[1], pCurrentCam->mConfigModesForAuto);
    } else if (strcmp(name, "supportedAeMultiExpRange") == 0) {
        parseMultiExpRange(atts[1]);
    } else if (strcmp(name, "dvsType") == 0) {
        if (strcmp(atts[1], "MORPH_TABLE") == 0) {
            pCurrentCam->mDVSType = MORPH_TABLE;
        } else if (strcmp(atts[1], "IMG_TRANS") == 0) {
            pCurrentCam->mDVSType = IMG_TRANS;
        }
    } else if (strcmp(name, "metadataSize") == 0) {
        pCurrentCam->mMetadataSize = atoi(atts[1]);
    }
}

TuningMode CameraProfiles::getTuningModeByStr(const char *str)
{
    if (strcmp(str, "VIDEO") == 0) {
        return TUNING_MODE_VIDEO;
    } else if (strcmp(str, "VIDEO-ULL") == 0) {
        return TUNING_MODE_VIDEO_ULL;
    } else if (strcmp(str, "VIDEO-HDR") == 0) {
        return TUNING_MODE_VIDEO_HDR;
    } else if (strcmp(str, "VIDEO-HDR2") == 0) {
        return TUNING_MODE_VIDEO_HDR2;
    } else if (strcmp(str, "VIDEO-HLC") == 0) {
        return TUNING_MODE_VIDEO_HLC;
    } else if (strcmp(str, "VIDEO-CUSTOM_AIC") == 0) {
        return TUNING_MODE_VIDEO_CUSTOM_AIC;
    } else if (strcmp(str, "VIDEO-LL") == 0) {
        return TUNING_MODE_VIDEO_LL;
    } else if (strcmp(str, "VIDEO-REAR-VIEW") == 0) {
        return TUNING_MODE_VIDEO_REAR_VIEW;
    } else if (strcmp(str, "VIDEO-HITCH-VIEW") == 0) {
        return TUNING_MODE_VIDEO_HITCH_VIEW;
    } else if (strcmp(str, "STILL_CAPTURE") == 0) {
        return TUNING_MODE_STILL_CAPTURE;
    } else {
        LOGE("unknown TuningMode %s", str);
    }
    return TUNING_MODE_VIDEO;
}

int CameraProfiles::parseSensorOBSettings(const char *str, vector<OBSetting> &obSettings)
{
    Check(str == NULL, -1, "@%s, str is NULL", __func__);
    LOGXML("@%s, str = %s", __func__, str);

    obSettings.clear();

    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    char *savePtr, *configMode;
    configMode = strtok_r(src, ",", &savePtr);
    OBSetting obSetting;

    while (configMode) {
        char* top = strtok_r(NULL, ",", &savePtr);
        char* left = strtok_r(NULL, ",", &savePtr);
        char* sectionHeight = strtok_r(NULL, ",", &savePtr);
        char* interleaveStep = strtok_r(NULL, ",", &savePtr);
        Check(configMode == NULL || top == NULL || left == NULL
              || sectionHeight == NULL || interleaveStep == NULL,
              -1, "@%s, wrong str %s", __func__, str);

        LOGXML("@%s, configMode %s, top %s, left %s, sectionHeight %s, step %s",
                __func__, configMode, top, left, sectionHeight, interleaveStep);

        obSetting.configMode =  CameraUtils::getConfigModeByName(configMode);
        obSetting.top = atoi(top);
        obSetting.left = atoi(left);
        obSetting.sectionHeight = atoi(sectionHeight);
        obSetting.interleaveStep = atoi(interleaveStep);
        obSettings.push_back(obSetting);

        if (savePtr != NULL)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        configMode = strtok_r(NULL, ",", &savePtr);
    }
    return 0;
}

int CameraProfiles::parseConfigModeForAuto(const char *str, vector <ConfigMode> &modes)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);
    LOGXML("@%s, str = %s", __func__, str);

    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    char *savePtr;
    char *modeStr = strtok_r(src, ",", &savePtr);
    while (modeStr) {
        LOGXML("@%s, configMode %s for auto", __func__, modeStr);
        ConfigMode mode = CameraUtils::getConfigModeByName(modeStr);
        modes.push_back(mode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        modeStr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

int CameraProfiles::parseSupportedTuningConfig(const char *str, vector <TuningConfig> &config)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);
    LOGXML("@%s, str = %s", __func__, str);

    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    char *savePtr;
    char *configMode = strtok_r(src, ",", &savePtr);
    TuningConfig cfg;
    while (configMode) {
        char* tuningMode = strtok_r(nullptr, ",", &savePtr);
        char* aiqb = strtok_r(nullptr, ",", &savePtr);
        Check(configMode == nullptr || tuningMode == nullptr
              || aiqb == nullptr, -1, "@%s, wrong str %s", __func__, str);

        LOGXML("@%s, configMode %s, tuningMode %s, aiqb name %s",
                __func__, configMode, tuningMode, aiqb);
        cfg.configMode = CameraUtils::getConfigModeByName(configMode);
        cfg.tuningMode = getTuningModeByStr(tuningMode);
        cfg.aiqbName = aiqb;
        config.push_back(cfg);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        configMode = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

int CameraProfiles::parseLardTags(const char *str, vector <LardTagConfig> &lardTags)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);
    LOGXML("@%s, str = %s", __func__, str);

    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    char *savePtr;
    char *tuningMode = strtok_r(src, ",", &savePtr);
    LardTagConfig cfg;
    while (tuningMode) {
        char* cmcTag = strtok_r(nullptr, ",", &savePtr);
        char* aiqTag = strtok_r(nullptr, ",", &savePtr);
        char* ispTag = strtok_r(nullptr, ",", &savePtr);
        char* othersTag = strtok_r(nullptr, ",", &savePtr);

        cfg.tuningMode = getTuningModeByStr(tuningMode);
        cfg.cmcTag = CameraUtils::fourcc2UL(cmcTag);
        cfg.aiqTag = CameraUtils::fourcc2UL(aiqTag);
        cfg.ispTag = CameraUtils::fourcc2UL(ispTag);
        cfg.othersTag = CameraUtils::fourcc2UL(othersTag);
        Check(cfg.cmcTag == 0 || cfg.aiqTag == 0 || cfg.ispTag == 0
              || cfg.othersTag == 0, -1, "@%s, wrong str %s", __func__, str);

        lardTags.push_back(cfg);
        LOGXML("@%s, tuningMode %s, cmc %s, aiq %s, isp %s, others %s",
                __func__, tuningMode, cmcTag, aiqTag, ispTag, othersTag);

        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tuningMode = strtok_r(nullptr, ",", &savePtr);
    }

    return 0;
}

int CameraProfiles::parseConfigMode(const char *str, vector <ConfigMode> &cfgMode)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);
    LOGXML("@%s, str = %s", __func__, str);

    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    ConfigMode mode;
    char *cfgName = nullptr, *savePtr = nullptr;
    cfgName = strtok_r(src, ",", &savePtr);

    while(cfgName) {
        mode = CameraUtils::getConfigModeByName(cfgName);
        cfgMode.push_back(mode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        cfgName = strtok_r(nullptr, ",", &savePtr);
    }

    return 0;
}

void CameraProfiles::parseMediaCtlConfigElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    MediaCtlConf mc;
    int idx = 0;

    while (atts[idx]) {
        const char *key = atts[idx];
        LOGXML("%s: name: %s, value: %s", __func__, atts[idx], atts[idx + 1]);
        if (strcmp(key, "id") == 0) {
            mc.mcId = strtol(atts[idx + 1], nullptr, 10);
        } else if (strcmp(key, "ConfigMode") == 0) {
            parseConfigMode(atts[idx + 1], mc.configMode);
        } else if (strcmp(key, "outputWidth") == 0) {
            mc.outputWidth = strtoul(atts[idx + 1], nullptr, 10);
        } else if (strcmp(key, "outputHeight") == 0) {
            mc.outputHeight = strtoul(atts[idx + 1], nullptr, 10);
        } else if (strcmp(key, "format") == 0) {
            mc.format = CameraUtils::string2PixelCode(atts[idx + 1]);
        } else if (strcmp(key, "vbp") == 0) {
            mc.vbp = strtoul(atts[idx + 1], nullptr, 10);
        }
        idx += 2;
    }

    LOGXML("@%s, name:%s, atts[0]:%s, id: %d", __func__, name, atts[0], mc.mcId);
    //Add a new empty MediaControl Configuration
    profiles->pCurrentCam->mMediaCtlConfs.push_back(mc);
}

#define V4L2_CID_WATERMARK  0x00982901
#define V4L2_CID_WATERMARK2 0x00982902
void CameraProfiles::parseControlElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    McCtl ctl;
    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    LOGXML("@%s, name:%s", __func__, name);

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx + 1, val);
        if (strcmp(key, "name") == 0) {
            ctl.entityName = val;
            ctl.entity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "ctrlId") == 0) {
            if (!strcmp(val, "V4L2_CID_LINK_FREQ")) {
                ctl.ctlCmd = V4L2_CID_LINK_FREQ;
            } else if (!strcmp(val, "V4L2_CID_VBLANK")) {
                ctl.ctlCmd = V4L2_CID_VBLANK;
            } else if (!strcmp(val, "V4L2_CID_HBLANK")) {
                ctl.ctlCmd = V4L2_CID_HBLANK;
            } else if (!strcmp(val, "V4L2_CID_EXPOSURE")) {
                ctl.ctlCmd = V4L2_CID_EXPOSURE;
            } else if (!strcmp(val, "V4L2_CID_ANALOGUE_GAIN")) {
                ctl.ctlCmd = V4L2_CID_ANALOGUE_GAIN;
            } else if (!strcmp(val, "V4L2_CID_HFLIP")) {
                ctl.ctlCmd = V4L2_CID_HFLIP;
            } else if (!strcmp(val, "V4L2_CID_VFLIP")) {
                ctl.ctlCmd = V4L2_CID_VFLIP;
            } else if (!strcmp(val, "V4L2_CID_WATERMARK")) {
                ctl.ctlCmd = V4L2_CID_WATERMARK;
            } else if (!strcmp(val, "V4L2_CID_WATERMARK2")) {
                ctl.ctlCmd = V4L2_CID_WATERMARK2;
            } else if (!strcmp(val, "V4L2_CID_TEST_PATTERN")) {
                ctl.ctlCmd = V4L2_CID_TEST_PATTERN;
            } else if (!strcmp(val, "V4L2_CID_WDR_MODE")) {
                ctl.ctlCmd = V4L2_CID_WDR_MODE;
// CRL_MODULE_S
            } else if (!strcmp(val, "V4L2_CID_LINE_LENGTH_PIXELS")) {
                ctl.ctlCmd = V4L2_CID_LINE_LENGTH_PIXELS;
            } else if (!strcmp(val, "V4L2_CID_FRAME_LENGTH_LINES")) {
                ctl.ctlCmd = V4L2_CID_FRAME_LENGTH_LINES;
            } else if (!strcmp(val, "CRL_CID_SENSOR_MODE")) {
                ctl.ctlCmd = CRL_CID_SENSOR_MODE;
            } else if (!strcmp(val, "CRL_CID_EXPOSURE_MODE")) {
                ctl.ctlCmd = CRL_CID_EXPOSURE_MODE;
            } else if (!strcmp(val, "CRL_CID_EXPOSURE_HDR_RATIO")) {
                ctl.ctlCmd = CRL_CID_EXPOSURE_HDR_RATIO;
// CRL_MODULE_E
            } else {
                LOGE("Unknow ioctl command %s", val);
                ctl.ctlCmd = -1;
            }
        } else if (strcmp(key, "value") == 0) {
            ctl.ctlValue = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "ctrlName") == 0) {
            ctl.ctlName = val;
        }
        idx += 2;
    }

    mc.ctls.push_back(ctl);
}

void CameraProfiles::parseSelectionElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    McFormat sel;
    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    LOGXML("@%s, name:%s", __func__, name);

    sel.top = -1; //top is not specified, need to be calc later.
    sel.left = -1; //left is not specified, need to be calc later.
    sel.width = 0; //width is not specified, need to be calc later.
    sel.height = 0; //height is not specified, need to be calc later.
    sel.formatType = FC_SELECTION;

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
        if (strcmp(key, "name") == 0) {
            sel.entityName = val;
            sel.entity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "pad") == 0) {
            sel.pad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "target") == 0) {
            if (!strcmp(val, "V4L2_SEL_TGT_COMPOSE")) {
                sel.selCmd = V4L2_SEL_TGT_COMPOSE;
            } else if (!strcmp(val, "V4L2_SEL_TGT_CROP")) {
                sel.selCmd = V4L2_SEL_TGT_CROP;
            }
        } else if (strcmp(key, "top") == 0) {
            sel.top = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "left") == 0) {
            sel.left = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "width") == 0) {
            sel.width = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "height") == 0) {
            sel.height = strtoul(val, nullptr, 10);
        }
        idx += 2;
    }

    mc.formats.push_back(sel);
}

/**
 * Store the MediaCtlConf mapping table for supportedStreamConfig by id.
 * Then we can select the MediaCtlConf through this table and configured stream.
 */
void CameraProfiles::storeMcMappForConfig(int mcId, supported_stream_config_t streamCfg)
{
    //We need to insert new one if mcId isn't in mStreamToMcMap.
    if (pCurrentCam->mStreamToMcMap.find(mcId) == pCurrentCam->mStreamToMcMap.end()) {
        pCurrentCam->mStreamToMcMap.insert(pair<int, supported_stream_config_array_t>(mcId, supported_stream_config_array_t()));
    }

    supported_stream_config_array_t &streamVector = pCurrentCam->mStreamToMcMap[mcId];
    streamVector.push_back(streamCfg);
}

/**
 * \brief Parses the string with the supported stream configurations
 * a stream configuration is made of 4 necessary elements
 * - Format
 * - Resolution
 * - Field (Interlaced field)
 * - Media config ID
 * we parse the string in 4 steps
 * example of valid stream configuration is: V4L2_PIX_FMT_NV12,1920x1080,0,0

 * the following elements are optional:
 * - Max fps, for continuous streaming and high quality capture. (optional)
 * example: V4L2_PIX_FMT_NV12,1920x1080,0,0,(30/15)
 *
 * \param src: string to be parsed
 * \param configs: Stream config array needs to be filled in
 *
 * \return number of int entries to be stored (i.e. 6 per configuration found)
 */
int CameraProfiles::parseStreamConfig(const char* src, supported_stream_config_array_t& configs)
{
    HAL_TRACE_CALL(1);

    int count = 0;  // entry count
    int mcId = -1;
    char* endPtr = nullptr;
    char* separatorPtr = nullptr;
    int parseStep = 0;
    supported_stream_config_t config;
    CLEAR(config);

#define NUM_ELEMENTS_NECESSARY 4
// Has optional element
#define NUM_ELEMENTS           (NUM_ELEMENTS_NECESSARY + 1)

    bool lastElement = false; // the last one?
    do {
        parseStep++;

        bool fetchElement = false;
        // Get the next segement for necessary element
        // Get the next segement for optional element if it exist
        if (parseStep <= NUM_ELEMENTS_NECESSARY
            || (!lastElement && (*src == '('))) {
            fetchElement = true;

            separatorPtr = (char *)strchr(src, ',');
            if (separatorPtr) {
                *separatorPtr = 0;
            } else {
                lastElement = true;
            }
        }

        switch (parseStep) {
            case 1: // Step 1: Parse format
                LOGXML("stream format is %s", src);
                config.format = CameraUtils::string2PixelCode(src);
                if (config.format == -1) {
                    LOGE("Malformed format in stream configuration");
                    goto parseError;
                }
                count++;
                break;
            case 2: // Step 2: Parse the resolution
                config.width = strtol(src, &endPtr, 10);
                if (endPtr == nullptr || *endPtr != 'x') {
                    LOGE("Malformed resolution in stream configuration");
                    goto parseError;
                }
                src = endPtr + 1;
                config.height = strtol(src, &endPtr, 10);
                count += 2;
                LOGXML("(%dx%d)", config.width, config.height);
                break;
            case 3: // Step 3: Parse field
                config.field = strtol(src, &endPtr, 10);
                LOGXML("stream field is %d", config.field);
                count++;
                break;
            case 4: // Step 4: Parse MediaCtlConf id.
                mcId = strtol(src, &endPtr, 10);
                if (mcId < 0) {
                    LOGE("Malformed, mcId in stream configuration");
                    goto parseError;
                }
                LOGXML("the mcId for supported stream config is %d", mcId);
                count++;
                break;
            case 5: // Step 5: Parse optional
                if (fetchElement) {
                    src++; // skip '('
                    config.maxVideoFps = strtol(src, &endPtr, 10);
                    if (endPtr == nullptr || *endPtr != '/') {
                        LOGE("Malformed, max fps in stream configuration");
                        goto parseError;
                    }
                    src = endPtr + 1;
                    config.maxCaptureFps = strtol(src, &endPtr, 10);
                    LOGXML("the max fps for supported stream config is (%d, %d",
                            config.maxVideoFps, config.maxCaptureFps);
                } else {
                    LOGXML("no max fps for supported stream config, use default");
                    config.maxVideoFps = 30;
                    config.maxCaptureFps = 30;
                }
                count += 2;
                break;
        }

        if (!lastElement) {
            // Move to the next element
            src = separatorPtr + 1;
            src = skipWhiteSpace(src);
        } else if (parseStep < NUM_ELEMENTS_NECESSARY ){
            LOGE("Malformed stream configuration, only finish step %d", parseStep);
            goto parseError;
        }

        // Finish all elements for one config
        if (parseStep >= NUM_ELEMENTS) {
            configs.push_back(config);
            storeMcMappForConfig(mcId, config);
            CLEAR(config);
            mcId = -1;
            parseStep = 0;
            LOGXML("Stream Configuration found");
            if (lastElement) {
                break;
            }
        }
    } while (true);

    return count;

parseError:
    LOGE("Error parsing stream configuration ");
    return 0;
}

void CameraProfiles::parseSupportedFeatures(const char* src, camera_features_list_t& features)
{
    HAL_TRACE_CALL(1);

    char * endPtr = nullptr;
    camera_features feature = INVALID_FEATURE;
    do {
        endPtr = (char *)strchr(src, ',');
        if (endPtr) {
            *endPtr = 0;
        }
        if (strcmp(src, "MANUAL_EXPOSURE") == 0) {
            feature = MANUAL_EXPOSURE;
        } else if (strcmp(src, "MANUAL_WHITE_BALANCE") == 0) {
            feature = MANUAL_WHITE_BALANCE;
        } else if (strcmp(src, "IMAGE_ENHANCEMENT") == 0) {
            feature = IMAGE_ENHANCEMENT;
        } else if (strcmp(src, "NOISE_REDUCTION") == 0) {
            feature = NOISE_REDUCTION;
        } else if (strcmp(src, "SCENE_MODE") == 0) {
            feature = SCENE_MODE;
        } else if (strcmp(src, "WEIGHT_GRID_MODE") == 0) {
            feature = WEIGHT_GRID_MODE;
        } else if (strcmp(src, "PER_FRAME_CONTROL") == 0) {
            feature = PER_FRAME_CONTROL;
        } else if (strcmp(src, "ISP_CONTROL") == 0) {
            feature = ISP_CONTROL;
        } else {
            feature = INVALID_FEATURE;
        }

        if (feature != INVALID_FEATURE) {
            features.push_back(feature);
        }

        if (endPtr) {
            src = endPtr + 1;
            src = skipWhiteSpace(src);
        }
    } while (endPtr);
}

void CameraProfiles::parseSupportedIspControls(const char* src, vector<uint32_t>& ctrlIds)
{
    HAL_TRACE_CALL(1);

    char* srcDup = strdup(src);
    Check((srcDup == nullptr), VOID_VALUE, "Create a copy of source string failed.");

    char* srcTmp = srcDup;
    char * endPtr = nullptr;
    do {
        endPtr = (char *)strchr(srcTmp, ',');
        if (endPtr) {
            *endPtr = 0;
        }

        uint32_t ctrlId = IspControlUtils::getIdByName(srcTmp);
        if (ctrlId != 0) {
            ctrlIds.push_back(ctrlId);
        }

        if (endPtr) {
            srcTmp = endPtr + 1;
            srcTmp = (char*)skipWhiteSpace(srcTmp);
        }
    } while (endPtr);

    free(srcDup);
}

int CameraProfiles::parseSupportedIntRange(const char* str, vector<int>& rangeArray)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        rangeArray.push_back(atoi(tablePtr));
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedFloatRange(const char* str, vector<float>& rangeArray)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        rangeArray.push_back(atof(tablePtr));
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedVideoStabilizationMode(const char* str, camera_video_stabilization_list_t &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_video_stabilization_mode_t mode = VIDEO_STABILIZATION_MODE_OFF;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        if (strcmp(tablePtr, "ON") == 0) {
            mode = VIDEO_STABILIZATION_MODE_ON;
        } else if (strcmp(tablePtr, "OFF") == 0) {
            mode = VIDEO_STABILIZATION_MODE_OFF;
        }
        supportedModes.push_back(mode);

        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedAeMode(const char* str, vector <camera_ae_mode_t> &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_ae_mode_t aeMode = AE_MODE_AUTO;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        if (strcmp(tablePtr, "AUTO") == 0) {
            aeMode = AE_MODE_AUTO;
        } else if (strcmp(tablePtr, "MANUAL") == 0) {
            aeMode = AE_MODE_MANUAL;
        }
        supportedModes.push_back(aeMode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedAfMode(const char* str, vector <camera_af_mode_t> &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == NULL, -1, "@%s, str is NULL", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_af_mode_t afMode = AF_MODE_AUTO;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        if (strcmp(tablePtr, "AUTO") == 0) {
            afMode = AF_MODE_AUTO;
        } else if (strcmp(tablePtr, "MACRO") == 0) {
            afMode = AF_MODE_MACRO;
        } else if (strcmp(tablePtr, "CONTINUOUS_VIDEO") == 0) {
            afMode = AF_MODE_CONTINUOUS_VIDEO;
        } else if (strcmp(tablePtr, "CONTINUOUS_PICTURE") == 0) {
            afMode = AF_MODE_CONTINUOUS_PICTURE;
        } else if (strcmp(tablePtr, "OFF") == 0) {
            afMode = AF_MODE_OFF;
        }
        supportedModes.push_back(afMode);
        if (savePtr != NULL)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(NULL, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedAwbMode(const char* str, vector <camera_awb_mode_t> &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_awb_mode_t awbMode = AWB_MODE_MAX;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        awbMode = CameraUtils::getAwbModeByName(tablePtr);
        supportedModes.push_back(awbMode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedSceneMode(const char* str, vector <camera_scene_mode_t> &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_scene_mode_t sceneMode = SCENE_MODE_MAX;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        sceneMode = CameraUtils::getSceneModeByName(tablePtr);
        supportedModes.push_back(sceneMode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedAntibandingMode(const char* str, vector <camera_antibanding_mode_t> &supportedModes)
{
    HAL_TRACE_CALL(1);
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    camera_antibanding_mode_t antibandingMode = ANTIBANDING_MODE_OFF;

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        if (strcmp(tablePtr, "AUTO") == 0) {
            antibandingMode = ANTIBANDING_MODE_AUTO;
        } else if (strcmp(tablePtr, "50Hz") == 0) {
            antibandingMode = ANTIBANDING_MODE_50HZ;
        } else if (strcmp(tablePtr, "60Hz") == 0) {
            antibandingMode = ANTIBANDING_MODE_60HZ;
        } else if (strcmp(tablePtr, "OFF") == 0) {
            antibandingMode = ANTIBANDING_MODE_OFF;
        }
        supportedModes.push_back(antibandingMode);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }

    return OK;
}

int CameraProfiles::parseRationalType(const char* str, int &numerator, int &denominator)
{

    HAL_TRACE_CALL(1);
    Check(str == nullptr, UNKNOWN_ERROR, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    if (tablePtr) {
        numerator = atoi(tablePtr);
    }

    Check((savePtr == nullptr), UNKNOWN_ERROR, "Malformed tag for rational type");
    savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr) {
        denominator = atoi(tablePtr);
    }

    return OK;
}

int CameraProfiles::parseSupportedAeParamRange(const char* src, vector<int>& scenes,
        vector<float>& minValues, vector<float>& maxValues)
{
    HAL_TRACE_CALL(1);
    char* srcDup = strdup(src);
    Check((srcDup == nullptr), NO_MEMORY, "Create a copy of source string failed.");

    char* srcTmp = srcDup;
    char* endPtr = nullptr;
    while ((endPtr = (char *)strchr(srcTmp, ','))) {
        if (endPtr) *endPtr = 0;

        camera_scene_mode_t scene = CameraUtils::getSceneModeByName(srcTmp);
        scenes.push_back(scene);
        if (endPtr) {
            srcTmp = endPtr + 1;
            srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        }

        float min = strtof(srcTmp, &endPtr);
        minValues.push_back(min);
        if (endPtr == nullptr || *endPtr != ',') {
            LOGE("Malformed ET range in exposure time range configuration");
            free(srcDup);
            return UNKNOWN_ERROR;
        }
        srcTmp = endPtr + 1;
        float max = strtof(srcTmp, &endPtr);
        maxValues.push_back(max);

        if (endPtr == nullptr || *endPtr != ',') break;

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
    }
    free(srcDup);
    return OK;
}

void CameraProfiles::parseFormatElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);

    McFormat fmt;
    fmt.type = RESOLUTION_TARGET;

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
        if (strcmp(key, "name") == 0) {
            fmt.entityName = val;
            fmt.entity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "pad") == 0) {
            fmt.pad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "stream") == 0) {
            fmt.stream = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "type") == 0) {
            if (strcmp(val, "RESOLUTION_MAX") == 0) {
                fmt.type = RESOLUTION_MAX;
            } else if (strcmp(val, "RESOLUTION_COMPOSE") == 0) {
                fmt.type = RESOLUTION_COMPOSE;
            } else if (strcmp(val, "RESOLUTION_CROP") == 0) {
                fmt.type = RESOLUTION_CROP;
            } else if (strcmp(val, "RESOLUTION_TARGET") == 0) {
                fmt.type = RESOLUTION_TARGET;
            } else {
                LOGE("Parse format type failed. type = %s", val);
                return;
            }
        } else if (strcmp(key, "width") == 0) {
            fmt.width = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "height") == 0) {
            fmt.height = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "format") == 0) {
            fmt.pixelCode = CameraUtils::string2PixelCode(val);
        }
        idx += 2;
    }

    fmt.formatType = FC_FORMAT;
    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    mc.formats.push_back(fmt);
}

void CameraProfiles::parseLinkElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    McLink link;
    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    LOGXML("@%s, name:%s", __func__, name);

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
        if (strcmp(key, "srcName") == 0) {
            link.srcEntityName = val;
            link.srcEntity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "srcPad") == 0) {
            link.srcPad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "sinkName") == 0) {
            link.sinkEntityName = val;
            link.sinkEntity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "sinkPad") == 0) {
            link.sinkPad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "enable") == 0) {
            link.enable = strcmp(val, "true") == 0;
        }

        idx += 2;
    }

    mc.links.push_back(link);
}

void CameraProfiles::parseRouteElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    McRoute route;
    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    LOGXML("@%s, name:%s", __func__, name);
    route.flag = MEDIA_LNK_FL_ENABLED;

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
        if (strcmp(key, "name") == 0) {
            route.entityName = val;
            route.entity = profiles->mMC->getEntityIdByName(val);
        } else if (strcmp(key, "srcPad") == 0) {
            route.srcPad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "sinkPad") == 0) {
            route.sinkPad = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "srcStream") == 0) {
            route.srcStream = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "sinkStream") == 0) {
            route.sinkStream = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "flag") == 0) {
            route.flag = strtoul(val, nullptr, 10);
        }
        idx += 2;
    }

    mc.routes.push_back(route);
}

void CameraProfiles::parseVideoElement(CameraProfiles *profiles, const char * /*name*/, const char **atts)
{
   McVideoNode videoNode;
   MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();

   videoNode.name = atts[1];
   videoNode.videoNodeType = V4l2DevBase::getNodeType(atts[3]);
   LOGXML("@%s, name:%s, videoNodeType:%d", __func__, videoNode.name.c_str(), videoNode.videoNodeType);

   mc.videoNodes.push_back(videoNode);
}

// MediaCtl output tag xml parsing code for the field like:
// <output port="main" width="1920" height="1088" format="V4L2_PIX_FMT_YUYV420_V32"/>
// <output port="second" width="3264" height="2448" format="V4L2_PIX_FMT_SGRBG12V32"/>
void CameraProfiles::parseOutputElement(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);

    McOutput output;

    int idx = 0;
    while (atts[idx]) {
        const char* key = atts[idx];
        const char* val = atts[idx + 1];
        LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
        if (strcmp(key, "port") == 0) {
            if (strcmp(val, "main") ==  0)
                output.port = MAIN_PORT;
            else if (strcmp(val, "second") ==  0)
                output.port = SECOND_PORT;
            else if (strcmp(val, "third") ==  0)
                output.port = THIRD_PORT;
            else if (strcmp(val, "forth") ==  0)
                output.port = FORTH_PORT;
            else
                output.port = INVALID_PORT;
        } else if (strcmp(key, "width") == 0) {
            output.width = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "height") == 0) {
            output.height = strtoul(val, nullptr, 10);
        } else if (strcmp(key, "format") == 0) {
            output.v4l2Format = CameraUtils::string2PixelCode(val);
        }
        idx += 2;
    }

    LOGXML("@%s, port:%d, output size:%dx%d, v4l2Format:%x", __func__, output.port,
            output.width, output.height, output.v4l2Format);

    MediaCtlConf &mc = profiles->pCurrentCam->mMediaCtlConfs.back();
    mc.outputs.push_back(output);
}

void CameraProfiles::parseMultiExpRange(const char* src)
{
    ExpRange* range = nullptr;
    MultiExpRange multiRange;
    MultiExpRange* pCurrRange = nullptr;
    pCurrentCam->mMultiExpRanges.clear();
    static const int MULTI_EXPOSURE_TAG_SHS1 = 0;
    static const int MULTI_EXPOSURE_TAG_RHS1 = 1;
    static const int MULTI_EXPOSURE_TAG_SHS2 = 2;
    static const int MULTI_EXPOSURE_TAG_RHS2 = 3;
    static const int MULTI_EXPOSURE_TAG_SHS3 = 4;

    string srcDup = src;
    Check((srcDup.c_str() == nullptr), VOID_VALUE, "Create a copy of source string failed.");

    const char* srcTmp = srcDup.c_str();
    char* endPtr = nullptr;
    int tag = -1;
    while ((endPtr = (char *)strchr(srcTmp, ','))) {
        *endPtr = 0;
        if (strcmp(srcTmp, "SHS1") == 0) {
            tag = MULTI_EXPOSURE_TAG_SHS1;
        } else if (strcmp(srcTmp, "RHS1") == 0) {
            tag = MULTI_EXPOSURE_TAG_RHS1;
        } else if (strcmp(srcTmp, "SHS2") == 0) {
            tag = MULTI_EXPOSURE_TAG_SHS2;
        } else if (strcmp(srcTmp, "RHS2") == 0) {
            tag = MULTI_EXPOSURE_TAG_RHS2;
        } else if (strcmp(srcTmp, "SHS3") == 0) {
            tag = MULTI_EXPOSURE_TAG_SHS3;
        } else {
            LOGE("Malformed tag for multi-exposure range configuration");
            return;
        }

        if (endPtr) {
            srcTmp = endPtr + 1;
            srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        }

        CLEAR(multiRange);
        multiRange.Resolution.width = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed resolution for multi-exposure range configuration");

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        multiRange.Resolution.height = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed resolution for multi-exposure range configuration");

        pCurrRange = nullptr;
        for (unsigned int i = 0; i < pCurrentCam->mMultiExpRanges.size(); i++) {
            if (pCurrentCam->mMultiExpRanges[i].Resolution.width == multiRange.Resolution.width &&
                pCurrentCam->mMultiExpRanges[i].Resolution.height == multiRange.Resolution.height) {
                pCurrRange = &(pCurrentCam->mMultiExpRanges[i]);
                break;
            }
        }
        if (pCurrRange) {
            switch (tag) {
                case MULTI_EXPOSURE_TAG_SHS1:
                    range = &pCurrRange->SHS1;
                    break;
                case MULTI_EXPOSURE_TAG_RHS1:
                    range = &pCurrRange->RHS1;
                    break;
                case MULTI_EXPOSURE_TAG_SHS2:
                    range = &pCurrRange->SHS2;
                    break;
                case MULTI_EXPOSURE_TAG_RHS2:
                    range = &pCurrRange->RHS2;
                    break;
                case MULTI_EXPOSURE_TAG_SHS3:
                    range = &pCurrRange->SHS3;
                    break;
                default:
                    LOGE("Wrong tag for multi-exposure range configuration");
                    return;
            }
        } else {
            switch (tag) {
                case MULTI_EXPOSURE_TAG_SHS1:
                    range = &multiRange.SHS1;
                    break;
                case MULTI_EXPOSURE_TAG_RHS1:
                    range = &multiRange.RHS1;
                    break;
                case MULTI_EXPOSURE_TAG_SHS2:
                    range = &multiRange.SHS2;
                    break;
                case MULTI_EXPOSURE_TAG_RHS2:
                    range = &multiRange.RHS2;
                    break;
                case MULTI_EXPOSURE_TAG_SHS3:
                    range = &multiRange.SHS3;
                    break;
                default:
                    LOGE("Wrong tag for multi-exposure range configuration");
                    return;
            }
        }

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        range->min = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed range for multi-exposure range configuration");

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        range->max = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed range for multi-exposure range configuration");

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        range->step = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed range for multi-exposure range configuration");

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        range->lowerBound = strtol(srcTmp, &endPtr, 10);
        Check((endPtr == nullptr || *endPtr != ','), VOID_VALUE, "Malformed range for multi-exposure range configuration");

        srcTmp = endPtr + 1;
        srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        range->upperBound = strtol(srcTmp, &endPtr, 10);

        if (endPtr) {
            srcTmp = endPtr + 1;
            srcTmp = const_cast<char*>(skipWhiteSpace(srcTmp));
        }

        if (!pCurrRange) {
            pCurrentCam->mMultiExpRanges.push_back(multiRange);
        }
    }
}

int CameraProfiles::parsePair(const char *str, int *first, int *second, char delim, char **endptr)
{
    // Find the first integer.
    char *end;
    int w = (int)strtol(str, &end, 10);
    // If a delimeter does not immediately follow, give up.
    if (*end != delim) {
        LOGE("Cannot find delimeter (%c) in str=%s", delim, str);
        return -1;
    }

    // Find the second integer, immediately after the delimeter.
    int h = (int)strtol(end+1, &end, 10);

    *first = w;
    *second = h;

    if (endptr) {
        *endptr = end;
    }

    return 0;
}

stream_t CameraProfiles::parseIsaScaleRawConfig(const char* src)
{
    stream_t config;
    CLEAR(config);

    char* srcDup = strdup(src);
    Check(!srcDup, config, "Create a copy of source string failed.");

    char* endPtr = (char*)strchr(srcDup, ',');
    if (endPtr) {
        *endPtr = 0;
        config.format = CameraUtils::string2PixelCode(srcDup);
        parsePair(endPtr + 1, &config.width, &config.height, 'x');
    }

    free(srcDup);
    return config;
}

void CameraProfiles::parseSizesList(const char *sizesStr, vector <camera_resolution_t> &sizes)
{
    if (sizesStr == 0) {
        return;
    }

    char *sizeStartPtr = (char *)sizesStr;

    while (true) {
        camera_resolution_t r;
        int success = parsePair(sizeStartPtr, &r.width, &r.height, 'x',
                                 &sizeStartPtr);
        if (success == -1 || (*sizeStartPtr != ',' && *sizeStartPtr != '\0')) {
            LOGE("Picture sizes string \"%s\" contains invalid character.", sizesStr);
            return;
        }
        if (r.width > 0 && r.height > 0)
            sizes.push_back(r);

        if (*sizeStartPtr == '\0') {
            return;
        }
        sizeStartPtr++;
    }
}

void CameraProfiles::parseViewProjection(const char* str, camera_view_projection_t &viewProjection)
{
    HAL_TRACE_CALL(1);
    if (!str)
        return;

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    if (tablePtr) {
        if (strcmp(tablePtr, "RECTILINEAR") == 0)
            viewProjection.type = PROJECTION_RECTILINEAR;
        else if (strcmp(tablePtr, "CONICAL") == 0)
            viewProjection.type = PROJECTION_CONICAL;
        else if (strcmp(tablePtr, "EQUIRECTANGULAR") == 0)
            viewProjection.type = PROJECTION_EQUIRECTANGULAR;
        else if (strcmp(tablePtr, "CYLINDRICAL") == 0)
            viewProjection.type = PROJECTION_CYLINDRICAL;
    }

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewProjection.cone_angle = strtof(tablePtr, nullptr);

    LOGXML("@%s, projection type: %d, cone angle: %f", __func__, viewProjection.type, viewProjection.cone_angle);
}

void CameraProfiles::parseViewRotation(const char* str, camera_view_rotation_t &viewRotation)
{
    HAL_TRACE_CALL(1);
    if (!str)
        return;

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    if (tablePtr)
        viewRotation.pitch = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewRotation.yaw = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewRotation.roll = strtof(tablePtr, nullptr);

    LOGXML("@%s, view rotation: %f, %f, %f", __func__, viewRotation.pitch, viewRotation.yaw, viewRotation.roll);
}

void CameraProfiles::parseCameraRotation(const char* str, camera_view_rotation_t &camRotation)
{
    HAL_TRACE_CALL(1);
    if (!str)
        return;

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    if (tablePtr)
        camRotation.pitch = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        camRotation.yaw = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        camRotation.roll = strtof(tablePtr, nullptr);

    LOGXML("@%s, camera rotation: %f, %f, %f", __func__, camRotation.pitch, camRotation.yaw, camRotation.roll);
}


void CameraProfiles::parseViewFineAdjustments(const char* str, camera_view_fine_adjustments_t &viewFineAdj)
{
    HAL_TRACE_CALL(1);
    if (!str)
        return;

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    if (tablePtr)
        viewFineAdj.horizontal_shift = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewFineAdj.vertical_shift = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewFineAdj.window_rotation = strtof(tablePtr, nullptr);

    if (savePtr != nullptr)
        savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
    tablePtr = strtok_r(nullptr, ",", &savePtr);
    if (tablePtr)
        viewFineAdj.vertical_stretch = strtof(tablePtr, nullptr);

    LOGXML("@%s, view fine adjustments: %f, %f, %f, %f", __func__,
           viewFineAdj.horizontal_shift,
           viewFineAdj.vertical_shift,
           viewFineAdj.window_rotation,
           viewFineAdj.vertical_stretch);
}

int CameraProfiles::getSupportedFormat(const char* str, vector <int>& supportedFormat)
{
    if (str == nullptr) {
        LOGE("the str is nullptr");
        return -1;
    }

    LOGXML("@%s, str:%s", __func__, str);
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';
    char* savePtr;
    char* fmt = strtok_r(src, ",", &savePtr);
    while (fmt) {
        int actual = CameraUtils::string2PixelCode(fmt);
        if (actual != -1) {
            supportedFormat.push_back(actual);
            LOGXML("@%s, add format:%d", __func__, actual);
        }
        fmt = strtok_r(nullptr, ",", &savePtr);
    }

    return 0;
}

/**
 * This function will handle all the MediaCtlCfg related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleMediaCtlCfg(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s, atts[0]:%s, profiles->mCurrentSensor:%d", __func__, name, atts[0], profiles->mCurrentSensor);
    if (strcmp(name, "MediaCtlConfig") == 0) {
        parseMediaCtlConfigElement(profiles, name, atts);
    } else if (strcmp(name, "link") == 0) {
        parseLinkElement(profiles, name, atts);
    } else if (strcmp(name, "route") == 0) {
        parseRouteElement(profiles, name, atts);
    } else if (strcmp(name, "control") == 0) {
        parseControlElement(profiles, name, atts);
    } else if (strcmp(name, "selection") == 0) {
        parseSelectionElement(profiles, name, atts);
    } else if (strcmp(name, "format") == 0) {
        parseFormatElement(profiles, name, atts);
    } else if (strcmp(name, "videonode") == 0) {
        parseVideoElement(profiles, name, atts);
    } else if (strcmp(name, "output") == 0) {
        parseOutputElement(profiles, name, atts);
    }
}

/**
 * This function will handle all the StaticMetadata related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleStaticMetaData(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s, atts[0]:%s, profiles->mCurrentSensor:%d", __func__, name, atts[0], profiles->mCurrentSensor);
    if (strcmp(name, "supportedStreamConfig") == 0) {
        supported_stream_config_array_t configsArray;
        parseStreamConfig(atts[1], configsArray);
        const int STREAM_MEMBER_NUM = sizeof(supported_stream_config_t) / sizeof(int);
        int dataSize = configsArray.size() * STREAM_MEMBER_NUM;
        int configs[dataSize];
        CLEAR(configs);
        for (size_t i = 0; i < configsArray.size(); i++) {
            LOGXML("@%s, stream config info: format=%s (%dx%d) type=%d", __func__,
                    CameraUtils::format2string(configsArray[i].format),
                    configsArray[i].width, configsArray[i].height,
                    configsArray[i].field);
            MEMCPY_S(&configs[i * STREAM_MEMBER_NUM], sizeof(supported_stream_config_t),
                     &configsArray[i], sizeof(supported_stream_config_t));
        }
        mMetadata.update(CAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configs, dataSize);
    } else if (strcmp(name, "fpsRange") == 0) {
        vector<float> rangeArray;
        parseSupportedFloatRange(atts[1], rangeArray);
        float fpsRange[rangeArray.size()];
        CLEAR(fpsRange);
        for (size_t i = 0; i < rangeArray.size(); i++){
            fpsRange[i] = rangeArray[i];
        }
        LOGXML("@%s, supported fps range size: %zu", __func__, rangeArray.size());
        mMetadata.update(CAMERA_AE_AVAILABLE_TARGET_FPS_RANGES, fpsRange, ARRAY_SIZE(fpsRange));
    } else if (strcmp(name, "evRange") == 0) {
        vector<int> rangeArray;
        parseSupportedIntRange(atts[1], rangeArray);
        int evRange[rangeArray.size()];
        CLEAR(evRange);
        for (size_t i = 0; i < rangeArray.size(); i++) {
            evRange[i] = rangeArray[i];
        }
        LOGXML("@%s, supported ev range size: %zu", __func__, rangeArray.size());
        mMetadata.update(CAMERA_AE_COMPENSATION_RANGE, evRange, ARRAY_SIZE(evRange));
    } else if (strcmp(name, "evStep") == 0) {
        int numerator = -1;
        int denominator = -1;
        int ret = parseRationalType(atts[1], numerator, denominator);
        Check((ret != OK), VOID_VALUE, "Parse evStep failed");

        icamera_metadata_rational_t evStep = {numerator, denominator};
        LOGXML("@%s, the numerator: %d, denominator: %d", __func__, evStep.numerator, evStep.denominator);
        mMetadata.update(CAMERA_AE_COMPENSATION_STEP, &evStep, 1);
    } else if (strcmp(name, "supportedFeatures") == 0) {
        camera_features_list_t supportedFeatures;
        parseSupportedFeatures(atts[1], supportedFeatures);
        int numberOfFeatures = supportedFeatures.size();
        uint8_t features[numberOfFeatures];
        CLEAR(features);
        for (int i = 0; i < numberOfFeatures; i++) {
            features[i] = supportedFeatures[i];
        }
        mMetadata.update(INTEL_INFO_AVAILABLE_FEATURES, features, numberOfFeatures);
    } else if (strcmp(name, "supportedAeExposureTimeRange") == 0) {
        vector<int> scenes;
        vector<float> minValues, maxValues;
        int ret = parseSupportedAeParamRange(atts[1], scenes, minValues, maxValues);
        Check((ret != OK), VOID_VALUE, "Parse AE eExposure time range failed");

        const int MEMBER_COUNT = 3;
        const int dataSize = scenes.size() * MEMBER_COUNT;
        int rangeData[dataSize];
        CLEAR(rangeData);

        for (size_t i = 0; i < scenes.size(); i++) {
            LOGXML("@%s, scene mode:%d supported exposure time range (%f-%f)", __func__,
                    scenes[i], minValues[i], maxValues[i]);
            rangeData[i * MEMBER_COUNT] = scenes[i];
            rangeData[i * MEMBER_COUNT + 1] = (int)minValues[i];
            rangeData[i * MEMBER_COUNT + 2] = (int)maxValues[i];
        }
        mMetadata.update(INTEL_INFO_AE_EXPOSURE_TIME_RANGE, rangeData, dataSize);
    } else if (strcmp(name, "supportedAeGainRange") == 0) {
        vector<int> scenes;
        vector<float> minValues, maxValues;
        int ret = parseSupportedAeParamRange(atts[1], scenes, minValues, maxValues);
        Check((ret != OK), VOID_VALUE, "Parse AE gain range failed");

        const int MEMBER_COUNT = 3;
        const int dataSize = scenes.size() * MEMBER_COUNT;
        int rangeData[dataSize];
        CLEAR(rangeData);

        for (size_t i = 0; i < scenes.size(); i++) {
            LOGXML("@%s, scene mode:%d supported gain range (%f-%f)", __func__,
                    scenes[i], minValues[i], maxValues[i]);
            rangeData[i * MEMBER_COUNT] = scenes[i];
            // Since we use int to store float, before storing it we multiply min and max by 100.
            rangeData[i * MEMBER_COUNT + 1] = (int)(minValues[i] * 100);
            rangeData[i * MEMBER_COUNT + 2] = (int)(maxValues[i] * 100);
        }
        mMetadata.update(INTEL_INFO_AE_GAIN_RANGE, rangeData, dataSize);
    } else if (strcmp(name, "supportedVideoStabilizationModes") == 0) {
        camera_video_stabilization_list_t supportedMode;
        parseSupportedVideoStabilizationMode(atts[1], supportedMode);
        uint8_t modes[supportedMode.size()];
        CLEAR(modes);
        for(size_t i = 0; i < supportedMode.size(); i++) {
            modes[i] = supportedMode[i];
        }
        mMetadata.update(CAMERA_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, modes, supportedMode.size());
    } else if (strcmp(name, "supportedAeMode") == 0) {
        vector <camera_ae_mode_t> supportedAeMode;
        parseSupportedAeMode(atts[1], supportedAeMode);
        uint8_t aeModes[supportedAeMode.size()];
        CLEAR(aeModes);
        for (size_t i = 0; i < supportedAeMode.size(); i++) {
            aeModes[i] = supportedAeMode[i];
        }
        mMetadata.update(CAMERA_AE_AVAILABLE_MODES, aeModes, supportedAeMode.size());
    } else if (strcmp(name, "supportedAwbMode") == 0) {
        vector <camera_awb_mode_t> supportedAwbMode;
        parseSupportedAwbMode(atts[1], supportedAwbMode);
        uint8_t awbModes[supportedAwbMode.size()];
        CLEAR(awbModes);
        for (size_t i = 0; i < supportedAwbMode.size(); i++) {
            awbModes[i] = supportedAwbMode[i];
        }
        mMetadata.update(CAMERA_AWB_AVAILABLE_MODES, awbModes, supportedAwbMode.size());
    } else if (strcmp(name, "supportedSceneMode") == 0) {
        vector <camera_scene_mode_t> supportedSceneMode;
        parseSupportedSceneMode(atts[1], supportedSceneMode);
        uint8_t sceneModes[supportedSceneMode.size()];
        CLEAR(sceneModes);
        for (size_t i = 0; i < supportedSceneMode.size(); i++) {
            sceneModes[i] = supportedSceneMode[i];
        }
        mMetadata.update(CAMERA_CONTROL_AVAILABLE_SCENE_MODES, sceneModes, supportedSceneMode.size());
    } else if (strcmp(name, "supportedAfMode") == 0) {
        vector <camera_af_mode_t> supportedAfMode;
        parseSupportedAfMode(atts[1], supportedAfMode);
        uint8_t afModes[supportedAfMode.size()];
        CLEAR(afModes);
        for (size_t i = 0; i < supportedAfMode.size(); i++) {
            afModes[i] = supportedAfMode[i];
        }
        mMetadata.update(CAMERA_AF_AVAILABLE_MODES, afModes, supportedAfMode.size());
    } else if (strcmp(name, "supportedAntibandingMode") == 0) {
        vector <camera_antibanding_mode_t> supportedAntibandingMode;
        parseSupportedAntibandingMode(atts[1], supportedAntibandingMode);
        uint8_t antibandingModes[supportedAntibandingMode.size()];
        CLEAR(antibandingModes);
        for (size_t i = 0; i < supportedAntibandingMode.size(); i++) {
            antibandingModes[i] = supportedAntibandingMode[i];
        }
        mMetadata.update(CAMERA_AE_AVAILABLE_ANTIBANDING_MODES, antibandingModes, supportedAntibandingMode.size());
    } else if (strcmp(name, "supportedIspControls") == 0) {
        vector<uint32_t> ispCtrlIds;
        parseSupportedIspControls(atts[1], ispCtrlIds);
        size_t dataCount = ispCtrlIds.size();
        if (dataCount != 0) {
            int32_t data[dataCount];
            CLEAR(data);
            for (size_t i = 0; i < dataCount; i++) {
                data[i] = ispCtrlIds[i];
            }
            mMetadata.update(INTEL_CONTROL_ISP_SUPPORTED_CTRL_IDS, data, dataCount);
        }
    } else if (strcmp(name, "WFOV") == 0) {
        uint8_t wfov = 0;

        if (strcmp(atts[1], "ON") == 0)
            wfov = 1;
        mMetadata.update(INTEL_INFO_WFOV, &wfov, 1);
        LOGXML("@%s, WFOV mode: %d", __func__, wfov);
    } else if (strcmp(name, "sensorMountType") == 0) {
        uint8_t mountType = WALL_MOUNTED;

        if (strcmp(atts[1], "CEILING_MOUNTED") == 0)
            mountType = CEILING_MOUNTED;

        mMetadata.update(INTEL_INFO_SENSOR_MOUNT_TYPE, &mountType, 1);
        LOGXML("@%s, sensor mount type: %d", __func__, mountType);
    } else if (strcmp(name, "viewProjection") == 0) {
        camera_view_projection_t viewProjection = {PROJECTION_RECTILINEAR, 0};
        parseViewProjection(atts[1], viewProjection);
        mMetadata.update(INTEL_CONTROL_VIEW_PROJECTION, (uint8_t *)&viewProjection, sizeof(viewProjection));
    } else if (strcmp(name, "viewRotation") == 0) {
        camera_view_rotation_t viewRotation = {0.0f, 0.0f, 0.0f};
        parseViewRotation(atts[1], viewRotation);
        mMetadata.update(INTEL_CONTROL_VIEW_ROTATION, (uint8_t *)&viewRotation, sizeof(viewRotation));
    } else if (strcmp(name, "cameraRotation") == 0) {
        camera_view_rotation_t cameraRotation = {0.0f, 0.0f, 0.0f};
        parseViewRotation(atts[1], cameraRotation);
        mMetadata.update(INTEL_CONTROL_CAMERA_ROTATION, (uint8_t *)&cameraRotation, sizeof(cameraRotation));
    } else if (strcmp(name, "viewFineAdjustments") == 0) {
        camera_view_fine_adjustments_t viewFineAdj = {0.0f, 0.0f, 0.0f, 1.0f};
        parseViewFineAdjustments(atts[1], viewFineAdj);
        mMetadata.update(INTEL_CONTROL_VIEW_FINE_ADJUSTMENTS, (uint8_t *)&viewFineAdj, sizeof(viewFineAdj));
    } else if (strcmp(name, "StaticMetadata") != 0) { // Make sure it doesn't reach the end of StaticMetadata.
        handleGenericStaticMetaData(name, atts[1]);
    }
}

/**
 * \brief Parses string for generic static metadata and save them.
 *
 * \param name: the element's name.
 * \param src: the element's value, only include data and separator 'x' or ','.
 */
void CameraProfiles::handleGenericStaticMetaData(const char *name, const char *src)
{
    uint32_t tag =
            (strcmp(name, "ae.lockAvailable") == 0)              ? CAMERA_AE_LOCK_AVAILABLE
          : (strcmp(name, "awb.lockAvailable") == 0)             ? CAMERA_AWB_LOCK_AVAILABLE
          : (strcmp(name, "control.availableModes") == 0)        ? CAMERA_CONTROL_AVAILABLE_MODES
          : (strcmp(name, "sensor.info.activeArraySize") == 0)   ? CAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE
          : (strcmp(name, "sensor.info.pixelArraySize") == 0)    ? CAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE
          : (strcmp(name, "sensor.info.physicalSize") == 0)      ? CAMERA_SENSOR_INFO_PHYSICAL_SIZE
          : (strcmp(name, "sensor.info.sensitivityRange") == 0)  ? CAMERA_SENSOR_INFO_SENSITIVITY_RANGE
          : (strcmp(name, "sensor.info.exposureTimeRange") == 0) ? CAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE
          : (strcmp(name, "sensor.info.colorFilterArrangement") == 0) ? CAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT
          : (strcmp(name, "sensor.availableTestPatternModes") == 0) ? CAMERA_SENSOR_AVAILABLE_TEST_PATTERN_MODES
          : (strcmp(name, "sensor.orientation") == 0)            ? CAMERA_SENSOR_ORIENTATION
          : (strcmp(name, "lens.facing") == 0)                   ? CAMERA_LENS_FACING
          : (strcmp(name, "lens.info.availableApertures") == 0)  ? CAMERA_LENS_INFO_AVAILABLE_APERTURES
          : (strcmp(name, "lens.info.availableFilterDensities") == 0) ? CAMERA_LENS_INFO_AVAILABLE_FILTER_DENSITIES
          : (strcmp(name, "lens.info.availableFocalLengths") == 0) ? CAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS
          : (strcmp(name, "lens.info.hyperfocalDistance") == 0)  ? CAMERA_LENS_INFO_HYPERFOCAL_DISTANCE
          : (strcmp(name, "lens.info.minimumFocusDistance") == 0) ? CAMERA_LENS_INFO_MINIMUM_FOCUS_DISTANCE
          : (strcmp(name, "lens.info.shadingMapSize") == 0)      ? CAMERA_LENS_INFO_SHADING_MAP_SIZE
          : (strcmp(name, "lens.info.focusDistanceCalibration") == 0) ? CAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION
          : (strcmp(name, "request.maxNumOutputStreams") == 0)   ? CAMERA_REQUEST_MAX_NUM_OUTPUT_STREAMS
          : (strcmp(name, "request.pipelineMaxDepth") == 0)      ? CAMERA_REQUEST_PIPELINE_MAX_DEPTH
          : (strcmp(name, "request.availableCapabilities") == 0) ? CAMERA_REQUEST_AVAILABLE_CAPABILITIES
          : (strcmp(name, "jpeg.maxSize") == 0)                  ? CAMERA_JPEG_MAX_SIZE
          : (strcmp(name, "jpeg.availableThumbnailSizes") == 0)  ? CAMERA_JPEG_AVAILABLE_THUMBNAIL_SIZES
          : (strcmp(name, "edge.availableEdgeModes") == 0)       ? CAMERA_EDGE_AVAILABLE_EDGE_MODES
          : (strcmp(name, "hotPixel.availableHotPixelModes") == 0) ? CAMERA_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES
          : (strcmp(name, "noiseReduction.availableNoiseReductionModes") == 0) ? CAMERA_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES
          : (strcmp(name, "tonemap.maxCurvePoints") == 0)        ? CAMERA_TONEMAP_MAX_CURVE_POINTS
          : (strcmp(name, "tonemap.availableToneMapModes") == 0) ? CAMERA_TONEMAP_AVAILABLE_TONE_MAP_MODES
          : (strcmp(name, "info.supportedHardwareLevel") == 0)   ? CAMERA_INFO_SUPPORTED_HARDWARE_LEVEL
          : (strcmp(name, "sync.maxLatency") == 0)               ? CAMERA_SYNC_MAX_LATENCY
          : -1;
    int tagType = get_icamera_metadata_tag_type(tag);
    if (tagType == -1) {
        LOGW("Unsupported metadata %s", name);
        return;
    }

    union {
        uint8_t* u8;
        int32_t* i32;
        int64_t* i64;
        float*   f;
        double*  d;
        icamera_metadata_rational_t* r;
    } data;
    data.u8 = (unsigned char *)mMetadataCache;

    int index = 0;
    int maxIndex = mMetadataCacheSize / sizeof(double); // worst case
    char * endPtr = nullptr;
    do {
        switch (tagType) {
        case ICAMERA_TYPE_BYTE:
            data.u8[index]= (char)strtol(src, &endPtr, 10);
            LOGXML(" - %d -", data.u8[index]);
            break;
        case ICAMERA_TYPE_INT32:
        case ICAMERA_TYPE_RATIONAL:
            data.i32[index]= strtol(src, &endPtr, 10);
            LOGXML(" - %d -", data.i32[index]);
            break;
        case ICAMERA_TYPE_INT64:
            data.i64[index]= strtol(src, &endPtr, 10);
            LOGXML(" - %ld -", data.i64[index]);
            break;
        case ICAMERA_TYPE_FLOAT:
            data.f[index]= strtof(src, &endPtr);
            LOGXML(" - %8.3f -", data.f[index]);
            break;
        case ICAMERA_TYPE_DOUBLE:
            data.d[index]= strtof(src, &endPtr);
            LOGXML(" - %8.3f -", data.d[index]);
            break;
        }
        index++;

        if (endPtr != nullptr && (*endPtr == 'x' || *endPtr == ',')) {
            src = endPtr + 1;
        } else {
            break;
        }
    } while (index < maxIndex);

    switch (tagType) {
    case ICAMERA_TYPE_BYTE:
        mMetadata.update(tag, data.u8, index);
        break;
    case ICAMERA_TYPE_INT32:
        mMetadata.update(tag, data.i32, index);
        break;
    case ICAMERA_TYPE_INT64:
        mMetadata.update(tag, data.i64, index);
        break;
    case ICAMERA_TYPE_FLOAT:
        mMetadata.update(tag, data.f, index);
        break;
    case ICAMERA_TYPE_DOUBLE:
        mMetadata.update(tag, data.d, index);
        break;
    case ICAMERA_TYPE_RATIONAL:
        mMetadata.update(tag, data.r, index / 2);
        break;
    }
}

/**
 * the callback function of the libexpat for handling of one element start
 *
 * When it comes to the start of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::startElement(void *userData, const char *name, const char **atts)
{
    CameraProfiles *profiles = reinterpret_cast<CameraProfiles*>(userData);

    if (profiles->mCurrentDataField == FIELD_INVALID) {
        profiles->checkField(profiles, name, atts);
        return;
    }

    switch (profiles->mCurrentDataField) {
        case FIELD_SENSOR:
            if (strcmp(name, "MediaCtlConfig") == 0) {
                profiles->mInMediaCtlCfg = true;
                LOGXML("@%s %s, mInMediaCtlCfg is set to true", __func__, name);
            } else if (strcmp(name, "StaticMetadata") == 0) {
                profiles->mInStaticMetadata = true;
                LOGXML("@%s %s, mInStaticMetadata is set to true", __func__, name);
            }

            if (profiles->mInMediaCtlCfg) {
                // The MediaCtlCfg belongs to the sensor segments
                profiles->handleMediaCtlCfg(profiles, name, atts);
            } else if (profiles->mInStaticMetadata) {
                // The StaticMetadata belongs to the sensor segments
                profiles->handleStaticMetaData(profiles, name, atts);
            } else {
                profiles->handleSensor(profiles, name, atts);
            }
            break;
        case FIELD_COMMON:
            profiles->handleCommon(profiles, name, atts);
            break;
        default:
            LOGE("@%s, line:%d, go to default handling", __func__, __LINE__);
            break;
    }
}

/**
 * the callback function of the libexpat for handling of one element end
 *
 * When it comes to the end of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::endElement(void *userData, const char *name)
{
    LOGXML("@%s %s", __func__, name);

    CameraProfiles *profiles = reinterpret_cast<CameraProfiles*>(userData);

    if (strcmp(name, "Sensor") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        if (profiles->pCurrentCam) {
            LOGXML("@%s: Add camera id %d (%s)",
                __func__, profiles->mCurrentSensor,
                profiles->pCurrentCam->sensorName.c_str());
                // Merge the content of mMetadata into mCapability.
                ParameterHelper::merge(profiles->mMetadata, &profiles->pCurrentCam->mCapability);
                profiles->mMetadata.clear();

                // For non-extended camera, it should be in order by mCurrentSensor
                profiles->mStaticCfg->mCameras.insert(profiles->mStaticCfg->mCameras.begin() + profiles->mCurrentSensor, *(profiles->pCurrentCam));

            delete profiles->pCurrentCam;
            profiles->pCurrentCam = nullptr;
        }
    }

    if (strcmp(name, "MediaCtlConfig") == 0) {
        LOGXML("@%s %s, mInMediaCtlCfg is set to false", __func__, name);
        profiles->mInMediaCtlCfg = false;
    }

    if (strcmp(name, "StaticMetadata") == 0) {
        LOGXML("@%s %s, mInStaticMetadata is set to false", __func__, name);
        profiles->mInStaticMetadata = false;
    }

    if (strcmp(name, "Common") == 0)
        profiles->mCurrentDataField = FIELD_INVALID;
}

/**
 * Get an avaliable xml file
 *
 * Find the first avaliable xml file.
 *
 * \param[in] const vector<char *>& allAvaliableXmlFiles: all avaliable xml files list.
 * \param[out] string& xmlFile: to store a avaliable xml file
 */
void CameraProfiles::getAvaliableXmlFile(string profiles, string &xmlFile)
{
    struct stat st;
    profiles.append(".xml");

    string fileName = "./";
    fileName.append(profiles);
    if (stat(fileName.c_str(), &st) == 0) {
        xmlFile = fileName;
        return;
    }

    fileName.clear();
    fileName = "/usr/share/defaults/etc/camera/";
    fileName.append(profiles);
    if (stat(fileName.c_str(), &st) == 0) {
        xmlFile = fileName;
        return;
    }
}

void CameraProfiles::parseXmlFile(const string &xmlFile)
{
    int done;
    FILE *fp = nullptr;

    if (xmlFile.empty()) {
        return;
    }

    LOG2("@%s, parsing profile: %s", __func__, xmlFile.c_str());

    fp = ::fopen(xmlFile.c_str(), "r");
    if (nullptr == fp) {
        LOGE("@%s, line:%d, Can not open profile file %s in read mode, fp is nullptr", __func__, __LINE__, xmlFile.c_str());
        return;
    }

    XML_Parser parser = ::XML_ParserCreate(nullptr);
    if (nullptr == parser) {
        LOGE("@%s, line:%d, parser is nullptr", __func__, __LINE__);
        goto exit;
    }
    ::XML_SetUserData(parser, this);
    ::XML_SetElementHandler(parser, startElement, endElement);

    char pBuf[mBufSize];
    do {
        int len = (int)::fread(pBuf, 1, mBufSize, fp);
        if (!len) {
            if (ferror(fp)) {
                clearerr(fp);
                goto exit;
            }
        }
        done = len < mBufSize;
        if (XML_Parse(parser, (const char *)pBuf, len, done) == XML_STATUS_ERROR) {
            LOGE("@%s, line:%d, XML_Parse error", __func__, __LINE__);
            goto exit;
        }
    } while (!done);

exit:
    if (parser)
        ::XML_ParserFree(parser);
    if (fp)
    ::fclose(fp);
}

const char* CameraProfiles::skipWhiteSpace(const char *src)
{
    while( *src == '\n' || *src == '\t' || *src == ' ' || *src == '\v' || *src == '\r' || *src == '\f'  ) {
        src++;
    }
    return src;
}

/**
 * Get camera configuration from xml file
 *
 * The function will read the xml configuration file firstly.
 * Then it will parse out the camera settings.
 * The camera setting is stored inside this CameraProfiles class.
 *
 */
void CameraProfiles::getDataFromXmlFile(void)
{
    LOG2("@%s", __func__);

    // Get common data from libcamhal_profile.xml
    string commonXmlFile;
    string profiles = "libcamhal_profile";
    getAvaliableXmlFile(profiles, commonXmlFile);
    Check(commonXmlFile.empty(), VOID_VALUE, "%s is not found, please put it to current directory or /etc/camera", profiles.c_str());
    LOG2("@%s, the common profile name: %s", __func__, commonXmlFile.c_str());
    parseXmlFile(commonXmlFile);

    // According to sensor name to get sensor data
    LOG2("The kinds of sensor is %zu", mStaticCfg->mCommonConfig.availableSensors.size());
    vector<string> allSensors = mStaticCfg->mCommonConfig.availableSensors;

    if (allSensors.size() == 0) {
        LOGW("The style of libcamhal_profile is too old, please switch it as soon as possible !!!");
        return;
    }

    for (auto sensor : allSensors) {
        string sensorXmlFile;
        string sensorName = "sensors/";
        sensorName.append(sensor);
        getAvaliableXmlFile(sensorName, sensorXmlFile);
        if (sensorXmlFile.empty()) {
            LOGW("%s.xml, isn't found, please put it to ./sensors/ or /usr/share/defaults/etc/camera/sensors/", sensor.c_str());
            continue;
        }

        LOG2("@%s, the sensor profile name: %s", __func__, sensorXmlFile.c_str());
        parseXmlFile(sensorXmlFile);
    }
}

/**
 * Read graph descriptor and settings from configuration files.
 *
 * The resulting graphs represend all possible graphs for given sensor, and
 * they are stored in capinfo structure.
 */
void CameraProfiles::getGraphConfigFromXmlFile(void)
{
#if !defined(BYPASS_MODE) && !defined(USE_STATIC_GRAPH)
    // Assuming that PSL section from profiles is already parsed, and number
    // of cameras is known.
    GraphConfigManager::addCustomKeyMap();
    for (size_t i = 0; i < getSensorNum(); ++i) {

        if (mStaticCfg->mCameras[i].mGCMNodes) {
            LOGE("Camera %zu Graph Config already initialized - BUG", i);
            continue;
        }

        const string &fileName = mStaticCfg->mCameras[i].mGraphSettingsFile;
        if (fileName.empty()) {
            continue;
        }

        LOG1("Using graph setting file:%s for camera:%zu", fileName.c_str(), i);

        mStaticCfg->mCameras[i].mGCMNodes = GraphConfigManager::parse(fileName.c_str());
        if (!mStaticCfg->mCameras[i].mGCMNodes) {
            LOGE("Could not read graph descriptor from file for camera %zu", i);
        }
    }
#endif
}

void CameraProfiles::dumpSensorInfo(void)
{
    LOG2("@%s, line%d, for sensors settings==================", __func__, __LINE__);
    LOG2("@%s, line%d, sensor number:%d", __func__, __LINE__, getSensorNum());
    for (unsigned i = 0; i < getSensorNum(); i++) {
        LOG2("@%s, line%d, i:%d", __func__, __LINE__, i);
        LOG2("@%s, line%d, mCameras[%d].sensorName:%s", __func__, __LINE__, i, mStaticCfg->mCameras[i].sensorName.c_str());
        LOG2("@%s, line%d, mCameras[%d].mISysFourcc:%d", __func__, __LINE__, i, mStaticCfg->mCameras[i].mISysFourcc);

        supported_stream_config_array_t supportedConfigs;
        mStaticCfg->mCameras[i].mCapability.getSupportedStreamConfig(supportedConfigs);
        for (size_t j = 0; j < supportedConfigs.size(); j++) {
            LOG2("@%s, line%d, mCameras[%d]: format:%d size(%dx%d) field:%d", __func__, __LINE__,
                i, supportedConfigs[j].format, supportedConfigs[j].width,
                supportedConfigs[j].height, supportedConfigs[j].field);
        }

        for (unsigned j = 0; j < mStaticCfg->mCameras[i].mSupportedISysFormat.size(); j++) {
            LOG2("@%s, line%d, mCameras[%d].mSupportedISysFormat:%d", __func__, __LINE__, i, mStaticCfg->mCameras[i].mSupportedISysFormat[j]);
        }

        // dump the media controller mapping table for supportedStreamConfig
        LOG2("The media controller mapping table size: %zu", mStaticCfg->mCameras[i].mStreamToMcMap.size());
        for (auto& pool : mStaticCfg->mCameras[i].mStreamToMcMap) {
            int mcId = pool.first;
            supported_stream_config_array_t &mcMapVector = pool.second;
            LOG2("mcId: %d, the supportedStreamConfig vector size: %zu", mcId, mcMapVector.size());
        }

        // dump the media controller information
        LOG2("============Format Configuration==================");
        for (unsigned j = 0; j < mStaticCfg->mCameras[i].mMediaCtlConfs.size(); j++) {
            const MediaCtlConf* mc = &mStaticCfg->mCameras[i].mMediaCtlConfs[j];
            for (unsigned k = 0; k < mc->links.size(); k++) {
                const McLink* link = &mc->links[k];
                LOG2("       link src %s [%d:%d] ==> %s [%d:%d] enable %d", link->srcEntityName.c_str(), link->srcEntity, link->srcPad, link->sinkEntityName.c_str(), link->sinkEntity, link->sinkPad, link->enable);
            }
            for (unsigned k = 0; k < mc->ctls.size(); k++) {
                const McCtl* ctl = &mc->ctls[k];
                LOG2("       Ctl %s [%d] cmd %s [0x%08x] value %d", ctl->entityName.c_str(), ctl->entity, ctl->ctlName.c_str(), ctl->ctlCmd, ctl->ctlValue);
            }
            for (unsigned k = 0; k < mc->formats.size(); k++) {
                const McFormat* format = &mc->formats[k];
                if (format->formatType == FC_FORMAT)
                    LOG2("       format %s [%d:%d] [%dx%d] %s", format->entityName.c_str(), format->entity, format->pad, format->width, format->height, CameraUtils::pixelCode2String(format->pixelCode));
                else if (format->formatType == FC_SELECTION)
                    LOG2("       select %s [%d:%d] selCmd: %d [%d, %d] [%dx%d]", format->entityName.c_str(), format->entity, format->pad, format->selCmd, format->top, format->left, format->width, format->height);
            }
        }
        LOG2("============End of Format Configuration===========");
    }

    LOG2("@%s, line%d, for common settings==================", __func__, __LINE__);
}

} //namespace icamera
