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

#define LOG_TAG "CameraConf"

#include <sys/stat.h>

#include "iutils/CameraLog.h"
#include "CameraConf.h"
#include "PlatformData.h"
#include "ia_types.h"
#include "ia_cmc_types.h"

namespace icamera {

CpfConf::CpfConf() :
      mCMC(nullptr)
{
    CLEAR(mAiq);
    CLEAR(mIsp);
    CLEAR(mOthers);
}

CpfConf::~CpfConf()
{
    LOG1("@%s", __func__);
}

int CpfConf::init(int cameraId, ia_binary_data cpfData, TuningMode mode)
{
    LOG1("@%s", __func__);

    CheckWarning(mCMC != nullptr, OK, "CMC has already been init before!");
    Check((cpfData.data == nullptr), BAD_VALUE, "Error Initializing CPF configure");

    ia_lard *iaLard = ia_lard_init(&cpfData);
    if (iaLard != nullptr) {
        LOG1("AIQB file supported by lard.");
        ia_lard_input_params lardInputParams;
        initLardInputParam(cameraId, iaLard, mode, &lardInputParams);

        ia_lard_results* lardResults;
        // Run ia_lard, result is nullptr if aiqb file is not supported
        ia_err iaErr = ia_lard_run(iaLard, &lardInputParams, &lardResults);
        if (lardResults != nullptr) {
            LOG1("ia_lard_run success, using lard to get cmc mode and tuning.");
            mCMC = ia_cmc_parser_init_v1((ia_binary_data*)&lardResults->aiqb_cmc_data, NULL);
            mAiq = lardResults->aiqb_aiq_data;
            mIsp = lardResults->aiqb_isp_data;
            mOthers = lardResults->aiqb_other_data;
        } else {
            LOGE("Fail to run ia_lard, iaErr = %d", iaErr);
        }
        ia_lard_deinit(iaLard);

    } else {
        LOG1("Lard not supported. The AIQB file may be in old CPF format");
        mCMC = ia_cmc_parser_init(&cpfData);
        mAiq = cpfData;
        mIsp = cpfData;
        mOthers = cpfData;
    }
    Check((mCMC == nullptr), FAILED_TRANSACTION, "Error cmc parser init!");

    return OK;
}

void CpfConf::getIspData(ia_binary_data *ispData)
{
    ispData->data = mIsp.data;
    ispData->size = mIsp.size;
}

void CpfConf::getAiqData(ia_binary_data *aiqData)
{
    aiqData->data = mAiq.data;
    aiqData->size = mAiq.size;
}

void CpfConf::getOtherData(ia_binary_data *otherData)
{
    otherData->data = mOthers.data;
    otherData->size = mOthers.size;
}

void CpfConf::deinit()
{
    if (mCMC) {
        ia_cmc_parser_deinit(mCMC);
        mCMC = nullptr;
    }
}

void CpfConf::initLardInputParam(int cameraId, ia_lard *iaLard, TuningMode mode, ia_lard_input_params *lardInputParam)
{
    LardTagConfig lardTags;
    int ret = PlatformData::getLardTagsByTuningMode(cameraId, mode, lardTags);
    if (ret != OK) {
        lardInputParam->cmc_mode_tag = FOURCC_TO_UL('D','F','L','T');
        lardInputParam->aiq_mode_tag = FOURCC_TO_UL('D','F','L','T');
        lardInputParam->isp_mode_index = FOURCC_TO_UL('D','F','L','T');
        lardInputParam->others_mode_tag = FOURCC_TO_UL('D','F','L','T');
        return;
    }

    unsigned int count = 0;
    const unsigned int *tags = nullptr;

    ia_lard_get_tag_list(iaLard, FOURCC_TO_UL('L','C','M','C'), &count, &tags);
    lardInputParam->cmc_mode_tag = isTagValid(lardTags.cmcTag, count, tags) ? \
                                   lardTags.cmcTag : FOURCC_TO_UL('D','F','L','T');

    ia_lard_get_tag_list(iaLard, FOURCC_TO_UL('L','A','I','Q'), &count, &tags);
    lardInputParam->aiq_mode_tag = isTagValid(lardTags.aiqTag, count, tags) ? \
                                   lardTags.aiqTag : FOURCC_TO_UL('D','F','L','T');

    ia_lard_get_tag_list(iaLard, FOURCC_TO_UL('L','I','S','P'), &count, &tags);
    lardInputParam->isp_mode_index = isTagValid(lardTags.ispTag, count, tags) ? \
                                     lardTags.ispTag : FOURCC_TO_UL('D','F','L','T');

    ia_lard_get_tag_list(iaLard, FOURCC_TO_UL('L','T','H','R'), &count, &tags);
    lardInputParam->others_mode_tag = isTagValid(lardTags.othersTag, count, tags) ? \
                                      lardTags.othersTag : FOURCC_TO_UL('D','F','L','T');

    LOG1("@%s: The lard tags are: aiq-0x%x, isp-0x%x, cmc-0x%x, others-0x%x", __func__,
        lardInputParam->aiq_mode_tag, lardInputParam->isp_mode_index,
        lardInputParam->cmc_mode_tag, lardInputParam->others_mode_tag);
}

bool CpfConf::isTagValid(unsigned int tag, unsigned int count, const unsigned int *tags)
{
    if (tags != nullptr) {
        for (unsigned int i = 0; i < count; i++) {
            if (tags[i] == tag) return true;
        }
    }
    LOG1("@%s: Tag 0x%x is not valid. Will use DFLT instead.", __func__, tag);
    return false;
}

CpfStore::CpfStore(int cameraId, string sensorName)
{
    LOG1("@%s:Sensor Name = %s", __func__, sensorName.c_str());

    CLEAR(mCpfConfig);
    vector <TuningConfig> configs;

    PlatformData::getSupportedTuningConfig(cameraId, configs);

    for (auto &cfg : configs) {
        if (mCpfConfig[cfg.tuningMode] != nullptr) {
            continue;
        }

        if (cfg.aiqbName.empty()) {
            LOGE("aiqb name is empty, sensor name %s", sensorName.c_str());
            continue;
        }

        if (mCpfData.find(cfg.aiqbName) == mCpfData.end()) {
            // Obtain the configurations
            if (loadConf(cfg.aiqbName) != OK) {
                LOGE("load file %s failed, sensor %s", cfg.aiqbName.c_str(), sensorName.c_str());
                continue;
            }
        }

        mCpfConfig[cfg.tuningMode] = new CpfConf();
        mCpfConfig[cfg.tuningMode]->init(cameraId, mCpfData[cfg.aiqbName], cfg.tuningMode);
    }
}

CpfStore::~CpfStore()
{
    LOG1("@%s", __func__);
    for (int mode=0; mode<TUNING_MODE_MAX; mode++) {
        if (mCpfConfig[mode]) {
            mCpfConfig[mode]->deinit();
            delete mCpfConfig[mode];
        }
    }
    for (auto &cpfData : mCpfData) {
        if (cpfData.second.data) {
            free(cpfData.second.data);
        }
    }
    mCpfData.clear();
}

/**
 * findConfigFile
 *
 * Search the path where CPF files are stored
*/
int CpfStore::findConfigFile(string& cpfPathName)
{
    LOG1("@%s", __func__);
    vector<string> configFilePath;
    configFilePath.push_back("./");
    configFilePath.push_back("/usr/share/defaults/etc/camera/");
    int configFileCount = configFilePath.size();

    string cpfFile;
    for (int i = 0; i < configFileCount; i++) {
        cpfFile.append(configFilePath.at(i));
        cpfFile.append(cpfPathName);
        struct stat st;
        if (!stat(cpfFile.c_str(), &st))
            break;
        cpfFile.clear();
    }

    if (cpfFile.empty()) {//CPF file not found
        LOG1("@%s:No CPF file found for %s", __func__,cpfPathName.c_str());
        return NAME_NOT_FOUND;
    }

    cpfPathName = cpfFile;
    LOG1("@%s:CPF file found %s", __func__,cpfPathName.c_str());
    return OK;
}

/**
 * loadConf
 *
 * load the CPF file
*/
int CpfStore::loadConf(string aiqbName)
{
    LOG1("@%s", __func__);
    int ret = OK;
    const char *suffix = ".aiqb";

    string cpfPathName = aiqbName;
    cpfPathName.append(suffix);
    LOG1("aiqb file name %s", cpfPathName.c_str());

    if (findConfigFile(cpfPathName) != OK) {
        LOGE("CpfStore no aiqb file:%s", aiqbName.c_str());
        return NAME_NOT_FOUND;
    }

    LOG1("Opening CPF file \"%s\"", cpfPathName.c_str());
    FILE *file = fopen(cpfPathName.c_str(), "rb");
    Check((file == nullptr), NAME_NOT_FOUND, "ERROR in opening CPF file \"%s\": %s!", cpfPathName.c_str(), strerror(errno));
    do {
        int fileSize;
        if ((fseek(file, 0, SEEK_END) < 0) || ((fileSize = ftell(file)) < 0) || (fseek(file, 0, SEEK_SET) < 0)) {
            LOGE("ERROR querying properties of CPF file \"%s\": %s!", cpfPathName.c_str(), strerror(errno));
            ret = BAD_VALUE;
            break;
        }

        mCpfData[aiqbName].data = malloc(fileSize);
        if (!mCpfData[aiqbName].data) {
            LOGE("ERROR no memory in %s!", __func__);
            ret = NO_MEMORY;
            break;
        }

        if (fread(mCpfData[aiqbName].data, fileSize, 1, file) < 1) {
            LOGE("ERROR reading CPF file \"%s\"!", cpfPathName.c_str());
            ret = INVALID_OPERATION;
            break;
        }
        mCpfData[aiqbName].size = fileSize;
    } while (0);

    if (fclose(file)) {
        LOGE("ERROR in closing CPF file \"%s\": %s!", cpfPathName.c_str(), strerror(errno));
    }

    return ret;
}

/**
 * convenience getter for Isp data, Aiq data, cmc data and other data.
 */
int CpfStore::getDataAndCmc(ia_binary_data *ispData, ia_binary_data *aiqData, ia_binary_data *otherData,
                            ia_cmc_t **cmcData, TuningMode mode)
{
    LOG1("@%s mode = %d", __func__, mode);
    Check((mCpfConfig[mode] == nullptr), NO_INIT, "@%s, No aiqb init, mode = %d", __func__, mode);
    if (ispData != nullptr)
        mCpfConfig[mode]->getIspData(ispData);
    if (aiqData != nullptr)
        mCpfConfig[mode]->getAiqData(aiqData);
    if (otherData != nullptr)
        mCpfConfig[mode]->getOtherData(otherData);
    *cmcData = mCpfConfig[mode]->getCMCHandler();
    Check((*cmcData == nullptr), NO_INIT, "@%s, Could not parse cmc data", __func__);

    if (mode == TUNING_MODE_VIDEO_ULL) {
        LOG2("@%s ULL mode, ULL cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_HDR) {
        LOG2("@%s HDR mode, HDR cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_HDR2) {
        LOG2("@%s HDR2 mode, HDR2 cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_HLC) {
        LOG2("@%s HLC mode, HLC cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_CUSTOM_AIC) {
        LOG2("@%s CUSTOM AIC mode, CUSTOM AIC cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_LL) {
        LOG2("@%s VIDEO LL mode, VIDEO LL cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_REAR_VIEW) {
        LOG2("@%s VIDEO Rear View mode, VIDEO REAR VIEW cpf file is used", __func__);
    } else if (mode == TUNING_MODE_VIDEO_HITCH_VIEW) {
        LOG2("@%s VIDEO Hitch View mode, VIDEO HITCH VIEW cpf file is used", __func__);
    } else {
        LOG2("@%s VIDEO mode, default cpf file is used", __func__);
    }

    return OK;
}

}
