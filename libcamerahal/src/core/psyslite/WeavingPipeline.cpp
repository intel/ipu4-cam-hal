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

#define LOG_TAG "WeavingPipeline"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "WeavingPipeline.h"

namespace icamera {
static unsigned char weaving_proginit_bin[] __attribute__ ((aligned (4096))) = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

WeavingPipeline::WeavingPipeline() : PSysPipeBase(PG_ID)
{
    mWeavingFrameFmtTypeList[WEAVING_TERMINAL_ID_PROGRAM_INIT] = IA_CSS_DATA_CUSTOM_NO_DESCRIPTOR;
    mWeavingFrameFmtTypeList[WEAVING_TERMINAL_ID_GET_1_TERMINAL] = IA_CSS_DATA_FORMAT_BINARY_8;
    mWeavingFrameFmtTypeList[WEAVING_TERMINAL_ID_GET_2_TERMINAL] = IA_CSS_DATA_FORMAT_BINARY_8;
    mWeavingFrameFmtTypeList[WEAVING_TERMINAL_ID_PUT_TERMINAL] = IA_CSS_DATA_FORMAT_BINARY_8;
    mFrameFormatType = mWeavingFrameFmtTypeList;
}

WeavingPipeline::~WeavingPipeline()
{
}

int WeavingPipeline::setTerminalParams(const ia_css_frame_format_type* frame_format_types)
{
    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    for (int i = 0; i < mTerminalCount; i++) {
        ia_css_terminal_param_t *terminalParam =
            ia_css_program_group_param_get_terminal_param(pgParamsBuf, i);
        Check(!terminalParam, -1, "@%s, call ia_css_program_group_param_get_terminal_param fail", __func__);

        FrameInfo config = mSrcFrame[MAIN_PORT];
        if (i == WEAVING_TERMINAL_ID_PUT_TERMINAL) {
            config = mDstFrame[MAIN_PORT];
        }

        // The width required by weaving fw is half of aligned bpl.
        int width = config.mStride / 2;
        int height = config.mHeight;
        if (CameraUtils::isPlanarFormat(config.mFormat)) {
            // For planar format like NV16, extend the height to include second or third planar.
            height = height * config.mBpp / 8;
        }

        terminalParam->frame_format_type = frame_format_types[i];
        terminalParam->dimensions[IA_CSS_COL_DIMENSION] = width;
        terminalParam->dimensions[IA_CSS_ROW_DIMENSION] = height;
        terminalParam->bpp = 16; // Weaving pg required bpp is fixed to 16
        terminalParam->bpe = 16; // Weaving pg required bpe is fixed to 16
        terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION] = width;
        terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION] = height;
        terminalParam->stride = config.mStride;
        terminalParam->offset = 0;
        terminalParam->index[IA_CSS_COL_DIMENSION] = 0;
        terminalParam->index[IA_CSS_ROW_DIMENSION] = 0;
    }

    return OK;
}

int WeavingPipeline::prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                            vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);

    Check((srcBufs.size() != 2 || dstBufs.size() != 1), UNKNOWN_ERROR,
        "@%s, srcBufs size:%zu or dstBufs size:%zu is not 1",
        __func__, srcBufs.size(), dstBufs.size());

    // WEAVING_TERMINAL_ID_PROGRAM_INIT
    int size = sizeof(weaving_proginit_bin);
    ia_cipr_buffer_t* ciprBuf = registerUserBuffer(size, (void*)weaving_proginit_bin);
    Check(!ciprBuf, NO_MEMORY, "@%s, register proginit bin buffer fail", __func__);
    mTerminalBuffers[WEAVING_TERMINAL_ID_PROGRAM_INIT] = ciprBuf;

    // WEAVING_TERMINAL_ID_GET_1_TERMINAL
    size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
    ciprBuf = registerUserBuffer(srcBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register input terminal 1 buffer fail", __func__);
    mTerminalBuffers[WEAVING_TERMINAL_ID_GET_1_TERMINAL] = ciprBuf;

    // WEAVING_TERMINAL_ID_GET_2_TERMINAL
    ciprBuf = registerUserBuffer(srcBufs[1], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register input terminal 2 buffer fail", __func__);
    mTerminalBuffers[WEAVING_TERMINAL_ID_GET_2_TERMINAL] = ciprBuf;

    // WEAVING_TERMINAL_ID_PUT_TERMINAL
    size = CameraUtils::getFrameSize(mDstFrame[MAIN_PORT].mFormat, mDstFrame[MAIN_PORT].mWidth, mDstFrame[MAIN_PORT].mHeight);
    ciprBuf = registerUserBuffer(dstBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register output terminal buffer fail", __func__);
    mTerminalBuffers[WEAVING_TERMINAL_ID_PUT_TERMINAL] = ciprBuf;

    return OK;
}

int WeavingPipeline::prepare()
{
    return PSysPipeBase::prepare();
}

} //namespace icamera

