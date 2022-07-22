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

#pragma once

#include <string>
#include <map>

#include "iutils/Errors.h"
#include "iutils/Utils.h"

#include "ia_cmc_parser.h"
#include "ia_lard.h"

using namespace std;
namespace icamera {

/**
  * The IA data stored
*/
class CpfConf
{
public:
    CpfConf();
    virtual ~CpfConf();

    /**
     * \brief get CMC handler
     *
     * return: the CMC handler
     */
    ia_cmc_t* getCMCHandler() const { return mCMC; }

    /**
     * \brief get ISP data from CPF file
     *
     * \param[out] ia_binary_data *IspData: ISP data
     */
    void getIspData(ia_binary_data *IspData);

    /**
     * \brief get AIQ data from CPF file
     *
     * \param[out] ia_binary_data *AiqData: AIQ data
     */
    void getAiqData(ia_binary_data *AiqData);

    /**
     * \brief get others data from CPF file, including LTM data
     *
     * \param[out] ia_binary_data *otherData: others data
     */
    void getOtherData(ia_binary_data *otherData);

    /**
     * \brief parse CMC/ISP/AIQ/Others from the CPF data
     *
     * Parse the CMC/ISP/AIQ/Others data according to the tuning mode, and init
     * the CMC handler.
     *
     * \param[in] int cameraId: camera id
     * \param[in] ia_binary_data cpfData: CPF data loaded from the AIQB file
     * \param[in] int mode: Tuning mode
     *
     * \return OK if init successfully; otherwise non-0 value is returned.
     */
    int init(int cameraId, ia_binary_data cpfData, TuningMode mode);

    /**
      * \brief deinit CMC handler.
    */
    void deinit();

private:
    DISALLOW_COPY_AND_ASSIGN(CpfConf);

    void initLardInputParam(int cameraId, ia_lard *iaLard, TuningMode mode, ia_lard_input_params *lardInputParam);
    bool isTagValid(unsigned int tag, unsigned int count, const unsigned int *tags);

private:
    ia_cmc_t *mCMC;
    ia_binary_data mAiq;
    ia_binary_data mIsp;
    ia_binary_data mOthers;
};//end CpfConf

/**
  * CPF file operation class
*/
class CpfStore
{
public:
    explicit CpfStore(int cameraId, string sensorName);
    virtual ~CpfStore();


    /**
     * get Isp and Aiq data info
     *
     * \param ispData: return isp data of struct ia_binary_data
     * \param aiqData: return aiq data of struct ia_binary_data
     * \param otherData: return other data of struct ia_binary_data, such as tuning data for LTM
     * \param cmcData: return cmc data from AIQ data
     * \param cameraId: 0 ~ MAX_CAMERA_NUMBER
     * \param mode: Camera Mode
     * \return NO_INIT if data not found, return OK if success.
     */
    int getDataAndCmc(ia_binary_data *ispData, ia_binary_data *aiqData, ia_binary_data *otherData,
                      ia_cmc_t **cmcData, TuningMode mode = TUNING_MODE_VIDEO);
private:
    DISALLOW_COPY_AND_ASSIGN(CpfStore);

    int findConfigFile(string& cpfPathName);
    int loadConf(string aiqbName);

public:
    CpfConf* mCpfConfig[TUNING_MODE_MAX];
    map<string, ia_binary_data> mCpfData;

};//end CpfStore

}
