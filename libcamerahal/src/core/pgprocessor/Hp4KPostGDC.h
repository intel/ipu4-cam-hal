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
 * \class Hp4KPostGDC
 *
 * \brief As known as Post GDC
 */
class Hp4KPostGDC : public PGBase {
public:
    Hp4KPostGDC();
    virtual ~Hp4KPostGDC();

    virtual int iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
                           ia_binary_data *statistics, const ia_binary_data *ipuParameters);

private:
    DISALLOW_COPY_AND_ASSIGN(Hp4KPostGDC);

    virtual int configTerminal();
    virtual int prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf);
    virtual int setTerminalParams(const ia_css_frame_format_type* frameFormatTypes);

private:
    enum VPOSTGDC_HQ_4K_TERMINAL_ID {
        VPOSTGDC_HQ_4K_TERMINAL_ID_CACHED_PARAMETER_IN = 0,
        VPOSTGDC_HQ_4K_TERMINAL_ID_PROGRAM_INIT,
        VPOSTGDC_HQ_4K_TERMINAL_ID_GET,
        VPOSTGDC_HQ_4K_TERMINAL_ID_DVS_COORDS,
        VPOSTGDC_HQ_4K_TERMINAL_ID_PUT_DISPLAY,
        VPOSTGDC_HQ_4K_TERMINAL_ID_PUT_MAIN,
        VPOSTGDC_HQ_4K_TERMINAL_ID_PUT_PP,
        VPOSTGDC_HQ_4K_TERMINAL_ID_TNR5_2_2_GET_TERMINAL,
        VPOSTGDC_HQ_4K_TERMINAL_ID_TNR5_2_2_PUT_TERMINAL,
        VPOSTGDC_HQ_4K_TERMINAL_ID_TNR5_2_2_GET_RSIM_TERMINAL,
        VPOSTGDC_HQ_4K_TERMINAL_ID_TNR5_2_2_PUT_RSIM_TERMINAL,
        VPOSTGDC_HQ_4K_TERMINAL_ID_GET_NS_TERMINAL, /* Dummy Terminal */
        VPOSTGDC_HQ_4K_TERMINAL_ID_N
    };

    static const int PG_ID = 1011;
    static const int kParamNum = VPOSTGDC_HQ_4K_TERMINAL_ID_N;
    ia_css_frame_format_type_t mHp4KPostGDCFrameFmtTypeList[VPOSTGDC_HQ_4K_TERMINAL_ID_N];
    ia_binary_data __attribute__ ((aligned (4096))) mParamPayload[kParamNum];
    bool mAllocTNRBuffers;
    uint8_t* mTNRGetBuf;
    uint8_t* mTNRPutBuf;
    uint8_t* mTNRGetPsimBuf;
    uint8_t* mTNRPutPsimBuf;
};

} //namespace icamera