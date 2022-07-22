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

#pragma once

#include "PSysPipeBase.h"

namespace icamera {

/**
 * \class CscPipeline
 *
 * \brief As known as Color Space Conversion
 *        which is used to convert from one color space to another one.
 */
class CscPipeline : public PSysPipeBase {
public:
    static const int PG_ID = 1052;

    CscPipeline();
    virtual ~CscPipeline();

    virtual int prepare();

private:
    DISALLOW_COPY_AND_ASSIGN(CscPipeline);

    virtual int prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                       vector<std::shared_ptr<CameraBuffer>>& dstBufs);
    virtual int setTerminalParams(const ia_css_frame_format_type* frame_format_types);

private:
    enum CSC_TERMINAL_ID {
        CSC_TERMINAL_ID_CACHED_PARAMETER_IN = 0,
        CSC_TERMINAL_ID_GET_TERMINAL,
        CSC_TERMINAL_ID_PUT_TERMINAL
    };
    ia_css_frame_format_type_t mCscFrameFmtTypeList[CSC_TERMINAL_ID_PUT_TERMINAL + 1];
    ia_binary_data mParamPayload;
};

} //namespace icamera
