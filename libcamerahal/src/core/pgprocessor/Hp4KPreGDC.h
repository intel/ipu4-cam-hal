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
 * \class Hp4KPreGDC
 *
 * \brief As known as Pre GDC
 */
class Hp4KPreGDC : public PGBase {
public:
    Hp4KPreGDC();
    virtual ~Hp4KPreGDC();

    virtual int iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
                            ia_binary_data *statistics, const ia_binary_data *ipuParameters);
private:
    DISALLOW_COPY_AND_ASSIGN(Hp4KPreGDC);

    virtual int configTerminal();
    virtual int prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf);
    virtual int setTerminalParams(const ia_css_frame_format_type* frameFormatTypes);
    virtual int decodeStats(ia_binary_data *statistics);

private:
    enum VPREGDC_ISL_HQ_4K_TERMINAL_ID {
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_CACHED_PARAMETER_IN = 0,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_PROGRAM_INIT,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_GET,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_PUT_OUT,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_CACHED_PARAMETER_OUT,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_0,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_1,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_BLC_SENSOR_TYPE_2,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_0,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_1,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_LSC_SENSOR_TYPE_2,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_3A_STAT_AWB,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_ISA_ACC_CTRL_SPT_3A_STAT_AF,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_IN_L0,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_IN_L1,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_IN_L2,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_OUT_L0,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_OUT_L1,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_DVS_ACC_CTRL_SPT_DVS_OUT_L2,
        VPREGDC_ISL_HQ_4K_TERMINAL_ID_N
    };

    static const int PG_ID = 1012;
    static const int kParamNum = VPREGDC_ISL_HQ_4K_TERMINAL_ID_N;
    ia_css_frame_format_type_t mHp4KPreGDCFrameFmtTypeList[VPREGDC_ISL_HQ_4K_TERMINAL_ID_N];
    ia_binary_data __attribute__ ((aligned (4096))) mParamPayload[kParamNum];
};

} //namespace icamera