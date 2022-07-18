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

#define LOG_TAG "CscPipeline"

#include "CscPipeline.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

namespace icamera {

ia_isp_bxt_run_kernels_t run_kernels_psys_csc[] =
{
    {
        60000,                                      /* stream_id */
        ia_pal_uuid_isp_bxt_csc_yuv2rgb,            /* kernel_uuid = ia_pal_uuid_isp_bxt_csc_yuv2rgb */
        1,                                          /* enable */
        nullptr,                                    /* resolution_info */
        nullptr,                                    /* resolution_history*/
        { 0, 0, 0, 0 }                              /* metadata[4] */
    },
};

static uint8_t csc_param_bin[] __attribute__ ((aligned (4096))) = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x8c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3c, 0x06, 0x00, 0x00, 0x8c, 0x04, 0x00, 0x00,
    0x79, 0xFE, 0xFF, 0xFF, 0xD3, 0xFC, 0xFF, 0xFF,
    0x8C, 0x04, 0x00, 0x00, 0xE1, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

CscPipeline::CscPipeline() : PSysPipeBase(PG_ID)
{
    for (int i = 0; i < (CSC_TERMINAL_ID_PUT_TERMINAL + 1); i++) {
        mCscFrameFmtTypeList[i] = IA_CSS_N_FRAME_FORMAT_TYPES;
    }
    mFrameFormatType = mCscFrameFmtTypeList;
    CLEAR(mParamPayload);
}

CscPipeline::~CscPipeline()
{
    if (mParamPayload.data) {
        IA_CIPR_FREE(mParamPayload.data);
    }
}

int CscPipeline::setTerminalParams(const ia_css_frame_format_type* frame_format_types)
{
    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    for (int i = 0; i < mTerminalCount; i++) {
        ia_css_terminal_param_t *terminalParam =
            ia_css_program_group_param_get_terminal_param(pgParamsBuf, i);
        Check(!terminalParam, -1, "@%s, call ia_css_program_group_param_get_terminal_param fail", __func__);

        FrameInfo config = mSrcFrame[MAIN_PORT];
        if (i == CSC_TERMINAL_ID_PUT_TERMINAL) {
            config = mDstFrame[MAIN_PORT];
        }

        terminalParam->frame_format_type = frame_format_types[i];
        terminalParam->dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;
        terminalParam->bpp = config.mBpp;
        terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;
        terminalParam->stride = config.mStride;
        terminalParam->offset = 0;
        terminalParam->index[IA_CSS_COL_DIMENSION] = 0;
        terminalParam->index[IA_CSS_ROW_DIMENSION] = 0;
    }

    return OK;
}

int CscPipeline::prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                        vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);

    Check((srcBufs.size() != 1 || dstBufs.size() != 1), UNKNOWN_ERROR,
        "@%s, srcBufs size:%zu or dstBufs size:%zu is not 1",
        __func__, srcBufs.size(), dstBufs.size());

    if (mParamPayload.data == nullptr) {
        unsigned int payloadSize = 0;
        int ret = mP2p->getPayloadSize(CSC_TERMINAL_ID_CACHED_PARAMETER_IN, &payloadSize);
        Check((ret != OK), ret, "@%s, call get payload size fail", __func__);
        mParamPayload.size = payloadSize;
        LOG2("%s: mParamPayload.size=%d", __func__, mParamPayload.size);
        mParamPayload.data = IA_CIPR_ALLOC_ALIGNED(PAGE_ALIGN(mParamPayload.size), IA_CIPR_PAGESIZE());
        Check(mParamPayload.data == nullptr, NO_MEMORY, "Failed to allocate parameter payload buffer");

        // Only need encode once.
        mP2p->encode(CSC_TERMINAL_ID_CACHED_PARAMETER_IN, mParamPayload);
    }

    // CSC_TERMINAL_ID_CACHED_PARAMETER_IN
    ia_cipr_buffer_t* ciprBuf = registerUserBuffer(mParamPayload.size, mParamPayload.data);

    if (!ciprBuf) {
        LOGV("register param bin buffer fail, using the fixed param");
        ciprBuf = registerUserBuffer(sizeof(csc_param_bin), (void*)csc_param_bin);
    }
    Check(!ciprBuf, NO_MEMORY, "@%s, register param bin buffer fail", __func__);
    mTerminalBuffers[CSC_TERMINAL_ID_CACHED_PARAMETER_IN] = ciprBuf;

    // CSC_TERMINAL_ID_GET_TERMINAL
    int size = mSrcFrame[MAIN_PORT].mWidth * mSrcFrame[MAIN_PORT].mHeight * mSrcFrame[MAIN_PORT].mBpp / 8;
    ciprBuf = registerUserBuffer(srcBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register input terminal buffer fail", __func__);
    mTerminalBuffers[CSC_TERMINAL_ID_GET_TERMINAL] = ciprBuf;

    // CSC_TERMINAL_ID_PUT_TERMINAL
    size = mDstFrame[MAIN_PORT].mWidth * mDstFrame[MAIN_PORT].mHeight * mDstFrame[MAIN_PORT].mBpp / 8;
    ciprBuf = registerUserBuffer(dstBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register output terminal buffer fail", __func__);
    mTerminalBuffers[CSC_TERMINAL_ID_PUT_TERMINAL] = ciprBuf;

    return OK;
}

int CscPipeline::prepare()
{
    mNeedP2p = true;
    mP2p->setKernelConfig(ARRAY_SIZE(run_kernels_psys_csc), run_kernels_psys_csc);

    mCscFrameFmtTypeList[CSC_TERMINAL_ID_GET_TERMINAL] = PSysPipeBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mCscFrameFmtTypeList[CSC_TERMINAL_ID_PUT_TERMINAL] = PSysPipeBase::getCssFmt(mDstFrame[MAIN_PORT].mFormat);

    return PSysPipeBase::prepare();
}

} //namespace icamera

