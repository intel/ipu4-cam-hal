/*
 * Copyright (C) 2018 Intel Corporation.
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

#include "PGBase.h"

namespace icamera {

/**
 * \class HpHdrPreGDC
 *
 * \brief As known as Pre GDC
 */
class HpHdrPreGDC : public PGBase {
public:
    HpHdrPreGDC();
    virtual ~HpHdrPreGDC();

    virtual int iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
                            ia_binary_data *statistics, const ia_binary_data *ipuParameters);

private:
    DISALLOW_COPY_AND_ASSIGN(HpHdrPreGDC);

    virtual int configTerminal();
    virtual int prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf);
    virtual int setTerminalParams(const ia_css_frame_format_type* frameFormatTypes);
    virtual int decodeStats(ia_binary_data *statistics);


private:
    enum VPREGDC_HP_TERMINAL_ID {
        VPREGDC_HP_TERMINAL_ID_CACHED_PARAMETER_IN,
        VPREGDC_HP_TERMINAL_ID_PROGRAM_INIT,
        VPREGDC_HP_TERMINAL_ID_GET,
        VPREGDC_HP_TERMINAL_ID_PUT,
        VPREGDC_HP_TERMINAL_ID_CACHED_PARAMETER_OUT,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_0,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_1,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_2,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_0,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_1,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_2,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_3A_STAT_AWB,
        VPREGDC_HP_TERMINAL_ID_ISA_ACC_CTRL_SPT_3A_STAT_AF,
        VPREGDC_HP_TERMINAL_ID_SPT_3A_HDR_STAT_RGBY_OUT,
        VPREGDC_HP_TERMINAL_ID_SPT_3A_HDR_STAT_RGBS_OUT,
        VPREGDC_HP_TERMINAL_ID_SPT_3A_HDR_STAT_YDRC_OUT,
        VPREGDC_HP_TERMINAL_ID_SPT_DRC_MAP_IN,
        VPREGDC_HP_TERMINAL_ID_N
    };

    static const int PG_ID = 1002;
    static const int kParamNum = VPREGDC_HP_TERMINAL_ID_N;
    ia_css_frame_format_type_t mHpHdrPreGDCFrameFmtTypeList[VPREGDC_HP_TERMINAL_ID_N];
    ia_binary_data __attribute__ ((aligned (4096))) mParamPayload[kParamNum];
};

} //namespace icamera
