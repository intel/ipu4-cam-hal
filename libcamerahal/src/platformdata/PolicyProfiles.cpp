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
#define LOG_TAG "PolicyProfiles"

#include <string.h>
#include <expat.h>

#include "iutils/CameraLog.h"

#include "PolicyProfiles.h"

namespace icamera {

PolicyProfiles::PolicyProfiles(PlatformData::StaticCfg *cfg)
{
    LOGXML("@%s", __func__);
    mStaticCfg = cfg;
    pCurrentConf = nullptr;
    mStaticCfg->mPolicyConfig.clear();
    mCurrentDataField = FIELD_INVALID;

    getPolicyConfigFromXmlFile();
}

/**
 * This function will check which field that the parser parses to.
 *
 * The field is set to 2 types.
 * FIELD_INVALID FIELD_GRAPH
 *
 * \param profiles: the pointer of the PolicyProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void PolicyProfiles::checkField(PolicyProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);
    if (strcmp(name, "PsysPolicyConfig") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        return;
    } else if (strcmp(name, "graph") == 0) {
        profiles->pCurrentConf = new PolicyConfig;

        int idx = 0;
        while (atts[idx]) {
            const char* key = atts[idx];
            const char* val = atts[idx + 1];
            LOGXML("@%s, name:%s, atts[%d]:%s, atts[%d]:%s", __func__, name, idx, key, idx+1, val);
            if (strcmp(key, "id") == 0) {
                profiles->pCurrentConf->graphId = atoi(val);
            } else if (strcmp(key, "description") == 0) {
                profiles->pCurrentConf->policyDescription = val;
            }
            idx += 2;
        }
        profiles->mCurrentDataField = FIELD_GRAPH;
        return;
    }

    LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
    return;
}

int PolicyProfiles::parsePgList(const char *str, vector<string> &pgList)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        pgList.push_back(tablePtr);
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

int PolicyProfiles::parseOpModeList(const char *str, vector<int> &opModeList)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        opModeList.push_back(atoi(tablePtr));
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

int PolicyProfiles::parseCyclicFeedbackRoutineList(const char *str, vector<int> &cyclicFeedbackRoutineList)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        cyclicFeedbackRoutineList.push_back(atoi(tablePtr));
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

int PolicyProfiles::parseCyclicFeedbackDelayList(const char *str, vector<int> &cyclicFeedbackDelayList)
{
    Check(str == nullptr, -1, "@%s, str is nullptr", __func__);

    char *savePtr, *tablePtr;
    int sz = strlen(str);
    char src[sz + 1];
    MEMCPY_S(src, sz, str, sz);
    src[sz] = '\0';

    tablePtr = strtok_r(src, ",", &savePtr);
    while (tablePtr) {
        cyclicFeedbackDelayList.push_back(atoi(tablePtr));
        if (savePtr != nullptr)
            savePtr = const_cast<char*>(skipWhiteSpace(savePtr));
        tablePtr = strtok_r(nullptr, ",", &savePtr);
    }
    return 0;
}

void PolicyProfiles::handlePipeExecutor(PolicyProfiles *profiles, const char *name, const char **atts)
{
    int idx = 0;
    ExecutorPolicy policy;

    while (atts[idx]) {
        const char *key = atts[idx];
        LOGXML("%s: name: %s, value: %s", __func__, atts[idx], atts[idx + 1]);
        if (strcmp(key, "name") == 0) {
            policy.exeName = atts[idx + 1];
        } else if (strcmp(key, "pgs") == 0) {
            parsePgList(atts[idx + 1], policy.pgList);
        } else if (strcmp(key, "op_modes") == 0) {
            parseOpModeList(atts[idx + 1], policy.opModeList);
        } else if (strcmp(key, "notify_policy") == 0) {
            int notifyPolicy = std::stoi(atts[idx + 1]);
            if (notifyPolicy >= 0 && notifyPolicy < POLICY_INVALID) {
                policy.notifyPolicy = (ExecutorNotifyPolicy)notifyPolicy;
            } else {
                LOGW("Invalid notify policy value: %d", notifyPolicy);
            }
        } else if (strcmp(key, "cyclic_feedback_routine") == 0) {
            parseCyclicFeedbackRoutineList(atts[idx + 1], policy.cyclicFeedbackRoutineList);
        } else if (strcmp(key, "cyclic_feedback_delay") == 0) {
            parseCyclicFeedbackDelayList(atts[idx + 1], policy.cyclicFeedbackDelayList);
        } else {
            LOGW("Invalid policy attribute: %s", key);
        }
        idx += 2;
    }

    LOGXML("@%s, name:%s, atts[0]:%s", __func__, name, atts[0]);
    profiles->pCurrentConf->pipeExecutorVec.push_back(policy);
}

void PolicyProfiles::handleExclusivePGs(PolicyProfiles *profiles, const char *name, const char **atts)
{
    int idx = 0;
    LOGXML("%s: name: %s, value: %s", __func__, atts[idx], atts[idx + 1]);
    const char *key = atts[idx];
    if (strcmp(key, "pgs") == 0) {
        parsePgList(atts[idx + 1], profiles->pCurrentConf->exclusivePgs);
    } else {
        LOGE("Invalid policy attribute %s in exclusive label.", key);
    }
}

void PolicyProfiles::handleBundles(PolicyProfiles *profiles, const char *name, const char **atts)
{
    int idx = 0;
    LOGXML("%s: name: %s, value: %s", __func__, atts[idx], atts[idx + 1]);
    const char *key = atts[idx];

    Check(strcmp(key, "executors") != 0, VOID_VALUE, "Invalid policy attribute %s in bundle label.", key);

    // The structure of a bundle looks like: "hdr_proc:0,hdr_post:1" which uses ',' to split
    // different executors' names, and uses ':' to specify the executor's depth.
    vector<string> bundledExecutors;
    vector<int> depths;
    vector<string> executors = CameraUtils::splitString(atts[idx + 1], ',');

    for (const auto & item : executors) {
        vector<string> executorDepth = CameraUtils::splitString(item.c_str(), ':');
        Check(executorDepth.size() != 2, VOID_VALUE, "Invalid executor-depth mapping.");

        bundledExecutors.push_back(executorDepth[0]);
        depths.push_back(std::stoi(executorDepth[1]));
    }

    ExecutorDepth executorDepth = {bundledExecutors, depths};
    profiles->pCurrentConf->bundledExecutorDepths.push_back(executorDepth);
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
void PolicyProfiles::handlePolicyConfig(PolicyProfiles *profiles, const char *name, const char **atts)
{
    LOGXML("@%s, name:%s, atts[0]:%s", __func__, name, atts[0]);
    if (strcmp(name, "pipe_executor") == 0) {
        handlePipeExecutor(profiles, name, atts);
    } else if (strcmp(name, "exclusive") == 0) {
        handleExclusivePGs(profiles, name, atts);
    } else if (strcmp(name, "bundle") == 0) {
        handleBundles(profiles, name, atts);
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
void PolicyProfiles::startElement(void *userData, const char *name, const char **atts)
{
    PolicyProfiles *profiles = reinterpret_cast<PolicyProfiles*>(userData);

    if (profiles->mCurrentDataField == FIELD_INVALID) {
        profiles->checkField(profiles, name, atts);
        return;
    }

    switch (profiles->mCurrentDataField) {
        case FIELD_GRAPH:
            profiles->handlePolicyConfig(profiles, name, atts);
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
void PolicyProfiles::endElement(void *userData, const char *name)
{
    LOGXML("@%s %s", __func__, name);

    PolicyProfiles *profiles = reinterpret_cast<PolicyProfiles*>(userData);

    if (strcmp(name, "graph") == 0) {
        LOGXML("@%s, add policyConf, graphId: %d", __func__, profiles->pCurrentConf->graphId);
        profiles->mStaticCfg->mPolicyConfig.push_back(*(profiles->pCurrentConf));
        delete profiles->pCurrentConf;
        profiles->pCurrentConf = nullptr;
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
void PolicyProfiles::getAvaliableXmlFile(const vector<const char *>& avaliableXmlFiles, string &xmlFile)
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

void PolicyProfiles::parseXmlFile(const string &xmlFile)
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

const char* PolicyProfiles::skipWhiteSpace(const char *src)
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
void PolicyProfiles::getPolicyConfigFromXmlFile(void)
{
    LOGXML("@%s", __func__);
    const vector <const char *> profiles = {
        "./psys_policy_profiles.xml",
        "/usr/share/defaults/etc/camera/psys_policy_profiles.xml"
    };

    string chosenXmlFile;

    getAvaliableXmlFile(profiles, chosenXmlFile);
    Check(chosenXmlFile.empty(), VOID_VALUE, "psys_policy_profiles is not found in current directory and /usr/share/defaults/etc/camera");
    parseXmlFile(chosenXmlFile);
}

} //namespace icamera
