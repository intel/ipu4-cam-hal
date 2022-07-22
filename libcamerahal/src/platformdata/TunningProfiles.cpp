/*
 * Copyright (C) 2016-2018 Intel Corporation.
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
#define LOG_TAG "TunningProfiles"

#include <string.h>
#include <expat.h>

#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

#include "TunningProfiles.h"

namespace icamera {

TunningProfiles::TunningProfiles(PlatformData::StaticCfg *cfg)
{
    LOG1("@%s", __func__);
    mStaticCfg = cfg;
    mCurrentCam = nullptr;
    mCurrentDataField = FIELD_INVALID;

    getTunningDataFromXmlFile();
}

//According sensorName to find the CameraInfo in mStaticCfg
void TunningProfiles::getCameraInfoByName(TunningProfiles *profiles, const char * name)
{
    LOGXML("@%s, name:%s", __func__, name);
    size_t i = 0;

    for (i = 0; i < profiles->mStaticCfg->mCameras.size(); i++) {
        if (strcmp(profiles->mStaticCfg->mCameras[i].sensorName.c_str(), name) == 0) {
            profiles->mCurrentCam = &profiles->mStaticCfg->mCameras[i];
            break;
        }
    }
    if (i == profiles->mStaticCfg->mCameras.size()) {
        LOGE("Couldn't find the CameraInfo, please check the sensor name in xml");
        return;
    }
    LOGXML("find the CameraInfo for sensor: %s", profiles->mCurrentCam->sensorName.c_str());
}

/**
 * This function will check which field that the parser parses to.
 *
 * The field is set to 3 types.
 * FIELD_INVALID FIELD_SENSOR and FIELD_COMMON
 *
 * \param profiles: the pointer of the TunningProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void TunningProfiles::checkField(TunningProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);
    if (strcmp(name, "TunningSettings") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        return;
    } else if (strcmp(name, "Sensor") == 0) {
        int idx = 0;
        while (atts[idx]) {
            const char* key = atts[idx];
            const char* val = atts[idx + 1];
            LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
            if (strcmp(key, "name") == 0) {
                profiles->getCameraInfoByName(profiles, val);
            }
            idx += 2;
        }
        profiles->mCurrentDataField = FIELD_SENSOR;
        return;
    } else if (strcmp(name, "Common") == 0) {
        profiles->mCurrentDataField = FIELD_COMMON;
        return;
    }

    LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
    return;
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
void TunningProfiles::handleCommon(TunningProfiles * /*profiles*/, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s, atts[0]:%s", __func__, name, atts[0]);

    if (strcmp(atts[0], "value") != 0) {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
        return;
    }
}

int TunningProfiles::parseWeightGridTable(const char *str, unsigned char *table)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    int index = 0;
    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        table[index] = atoi(tablePtr);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        index++;
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

void TunningProfiles::handleWeightGrid(TunningProfiles *profiles, const char *name, const char **atts)
{
    WeightGridTable wg;
    int idx = 0;

    CLEAR(wg);
    while (atts[idx]) {
        const char *key = atts[idx];
        LOGXML("%s: name: %s, value: %s", __func__, atts[idx], atts[idx + 1]);
        if (strcmp(key, "width") == 0) {
            wg.width = (unsigned short)strtoul(atts[idx + 1], nullptr, 10);
        } else if (strcmp(key, "height") == 0) {
            wg.height = (unsigned short)strtoul(atts[idx + 1], nullptr, 10);
        } else if (strcmp(key, "table") == 0) {
            if (wg.table) {
                delete [] wg.table;
                wg.table = nullptr;
            }
            if (0 < wg.width && wg.width < MAX_WEIGHT_GRID_SIDE_LEN &&
                0 < wg.height && wg.height < MAX_WEIGHT_GRID_SIDE_LEN) {
                wg.table = new unsigned char[wg.width * wg.height];
            }

            if (wg.table)
                parseWeightGridTable(atts[idx + 1], wg.table);
        }
        idx += 2;
    }

    LOGXML("@%s, name:%s, atts[0]:%s", __func__, name, atts[0]);
    profiles->mCurrentCam->mWGTable.push_back(wg);
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
void TunningProfiles::handleSensor(TunningProfiles *profiles, const char *name, const char **atts)
{
    if (profiles->mCurrentCam == nullptr) {
        LOGW("@%s, can't get the sensor name, will not process sensor weightgrid", __func__);
        return;
    }

    LOGXML("@%s, name:%s, atts[0]:%s, sensor:%s", __func__, name, atts[0], profiles->mCurrentCam->sensorName.c_str());
    if (strcmp(name, "WeightGrid") == 0) {
        handleWeightGrid(profiles, name, atts);
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
void TunningProfiles::startElement(void *userData, const char *name, const char **atts)
{
    TunningProfiles *profiles = reinterpret_cast<TunningProfiles*>(userData);

    if (profiles->mCurrentDataField == FIELD_INVALID) {
        profiles->checkField(profiles, name, atts);
        return;
    }

    switch (profiles->mCurrentDataField) {
        case FIELD_SENSOR:
            profiles->handleSensor(profiles, name, atts);
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
void TunningProfiles::endElement(void *userData, const char *name)
{
    LOGXML("@%s %s", __func__, name);

    TunningProfiles *profiles = reinterpret_cast<TunningProfiles*>(userData);

    if (strcmp(name, "Sensor") == 0 || strcmp(name, "Common") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
    }
}

/**
 * Get an avaliable xml file
 *
 * Find the first avaliable xml file.
 *
 * \param[in] const vector<char *>& allAvaliableXmlFiles: all avaliable xml files list.
 * \param[out] string& xmlFile: to store a avaliable xml file
 */
void TunningProfiles::getAvaliableXmlFile(const vector<const char *>& avaliableXmlFiles, string &xmlFile)
{
    struct stat st;
    for (auto xml : avaliableXmlFiles) {
        int ret = stat(xml, &st);
        if (ret == 0) {
            xmlFile = xml;
            return;
        }
    }
}

void TunningProfiles::parseXmlFile(const string &xmlFile)
{
    int done;
    FILE *fp = nullptr;

    if (xmlFile.empty()) {
        return;
    }

    LOGXML("@%s, parsing profile: %s", __func__, xmlFile.c_str());

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

const char* TunningProfiles::skipWhiteSpace(const char *src)
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
void TunningProfiles::getTunningDataFromXmlFile(void)
{
    LOGXML("@%s", __func__);
    const vector <const char *> profiles = {
        "./tunning_profiles.xml",
        "/usr/share/defaults/etc/camera/tunning_profiles.xml"
    };

    string chosenXmlFile;

    getAvaliableXmlFile(profiles, chosenXmlFile);
    Check(chosenXmlFile.empty(), VOID_VALUE, "tunning_profile is not found, please put it to current directory or /usr/share/defaults/etc/camera");
    parseXmlFile(chosenXmlFile);
}

} //namespace icamera
