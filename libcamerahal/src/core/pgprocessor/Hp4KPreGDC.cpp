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

#define LOG_TAG "Hp4KPreGDC"

#include "Hp4KPreGDC.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

namespace icamera {

Hp4KPreGDC::Hp4KPreGDC() : PGBase(PG_ID)
{
    for (int i = 0; i < VPREGDC_ISL_HQ_4K_TERMINAL_ID_N; i++) {
        mHp4KPreGDCFrameFmtTypeList[i] = IA_CSS_N_FRAME_FORMAT_TYPES;
    }
    mFrameFormatType = mHp4KPreGDCFrameFmtTypeList;
    CLEAR(mParamPayload);
}

Hp4KPreGDC::~Hp4KPreGDC()
{
    for (int i = 0; i < mTerminalCount; i++) {
        if (mParamPayload[i].data) {
            IA_CIPR_FREE(mParamPayload[i].data);
        }
    }
}

int Hp4KPreGDC::configTerminal()
{
    mHp4KPreGDCFrameFmtTypeList[VPREGDC_ISL_HQ_4K_TERMINAL_ID_GET] = PGBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mHp4KPreGDCFrameFmtTypeList[VPREGDC_ISL_HQ_4K_TERMINAL_ID_PUT_OUT] = PGBase::getCssFmt(mDstFrame[MAIN_PORT].mFormat);

    return OK;
}

int Hp4KPreGDC::setTerminalParams(const ia_css_frame_format_type* frameFormatTypes)
{
    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);
    ia_css_program_group_manifest_t* pg_manifest =
        (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer);

    for (int i = 0; i < mTerminalCount; i++) {
        ia_css_terminal_param_t *terminalParam =
            ia_css_program_group_param_get_terminal_param(pgParamsBuf, i);
        Check(!terminalParam, UNKNOWN_ERROR, "%s, call ia_css_program_group_param_get_terminal_param fail", __func__);
        ia_css_terminal_manifest_t *terminal_manifest = ia_css_program_group_manifest_get_term_mnfst(pg_manifest, i);
        ia_css_terminal_type_t  terminal_type = ia_css_terminal_manifest_get_type(terminal_manifest);

        if (!((terminal_type == IA_CSS_TERMINAL_TYPE_DATA_OUT) || (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_IN))) {
            continue;
        }

        FrameInfo config = mSrcFrame[MAIN_PORT];
        if (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_OUT) {
            config = mDstFrame[MAIN_PORT];
        }


        terminalParam->frame_format_type = frameFormatTypes[i];
        terminalParam->dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;
        terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;

        /* Bits per pixel (bpp) is the total amount of bits used per pixel in the
         * whole image. We receive the bpp value with the image format from the
         * user. However, FW expects bits per pixel to be set as the bits per
         * Y-plane element, which differs from the definition of bpp we use. */
        // As Input is 12b raw, one pixel is 16b, the bpe is equal to bpp
        if (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_IN) {
            terminalParam->bpp = 16;
            terminalParam->bpe = terminalParam->bpp;
            terminalParam->stride = config.mStride;
        }

        // As YUV420 output is 12b per component, one channel(Y/U/V) is 16b, need double the stride
         if (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_OUT) {
            terminalParam->bpp = 16;
            terminalParam->bpe = terminalParam->bpp;
            terminalParam->stride = config.mStride * 2;
        }

        terminalParam->offset = 0;
        terminalParam->index[IA_CSS_COL_DIMENSION] = 0;
        terminalParam->index[IA_CSS_ROW_DIMENSION] = 0;

        LOG1("%s: index=%d, format=%d, w=%d, h=%d, fw=%d, fh=%d, bpp=%d, bpe=%d, stride=%d, offset=%d, col=%d, row=%d",
            __func__, i,
            terminalParam->frame_format_type,
            terminalParam->dimensions[IA_CSS_COL_DIMENSION],
            terminalParam->dimensions[IA_CSS_ROW_DIMENSION],
            terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION],
            terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION],
            terminalParam->bpp,
            terminalParam->bpe,
            terminalParam->stride,
            terminalParam->offset,
            terminalParam->index[IA_CSS_COL_DIMENSION],
            terminalParam->index[IA_CSS_ROW_DIMENSION]);
    }

    return OK;
}

int Hp4KPreGDC::prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf)
{
    int ret = OK;
    int inFd[INVALID_PORT] = { -1, -1, -1, -1};
    void *inPtr[INVALID_PORT] = { nullptr, nullptr, nullptr, nullptr };
    int outFd[INVALID_PORT] = { -1, -1, -1, -1};
    void *outPtr[INVALID_PORT] = { nullptr, nullptr, nullptr, nullptr };

    for (const auto& item : inBuf) {
        if(inBuf[item.first]->getMemory() == V4L2_MEMORY_DMABUF) {
            inFd[item.first] = inBuf[item.first]->getFd();
            inPtr[item.first] = nullptr;
        } else {
            inFd[item.first] = -1;
            inPtr[item.first] = inBuf[item.first]->getBufferAddr();
        }
    }

    for (const auto& item : outBuf) {
        if(outBuf[item.first]->getMemory() == V4L2_MEMORY_DMABUF) {
            outFd[item.first] = outBuf[item.first]->getFd();
            outPtr[item.first] = nullptr;
        } else {
            outFd[item.first] = -1;
            outPtr[item.first] = outBuf[item.first]->getBufferAddr();
        }
    }

    Check((!inPtr[MAIN_PORT] && inFd[MAIN_PORT] < 0), BAD_VALUE, "%s, wrong parameters, inPtr:%p, inFd1:%d", __func__, inPtr[MAIN_PORT], inFd[MAIN_PORT]);
    Check((!outPtr[MAIN_PORT] && outFd[MAIN_PORT] < 0), BAD_VALUE, "%s, wrong parameters, outPtr1:%p, outFd1:%d", __func__, outPtr[MAIN_PORT], outFd[MAIN_PORT]);

    ia_cipr_buffer_t* ciprBuf;
    for (int i = 0; i < mTerminalCount; i++) {
        unsigned int payloadSize = 0;
        ret = mPGParamAdapt->getPayloadSize(i, &payloadSize);
        Check((ret != OK), ret, "%s, call get payload size fail", __func__);
        if (payloadSize == 0)
            continue;

        if ((mParamPayload[i].data != nullptr) && (mParamPayload[i].size != payloadSize)) {
            IA_CIPR_FREE(mParamPayload[i].data);
            mParamPayload[i].data = nullptr;
        }

        mParamPayload[i].size = payloadSize;
        LOG2("%s: mParamPayload[%d].size= %d", __func__, i, mParamPayload[i].size);
        if (mParamPayload[i].data == nullptr) {
            mParamPayload[i].data = IA_CIPR_ALLOC_ALIGNED(PAGE_ALIGN(mParamPayload[i].size), IA_CIPR_PAGESIZE());
        }
        Check(mParamPayload[i].data == nullptr, NO_MEMORY, "Failed to allocate parameter payload buffer");
        ret = mPGParamAdapt->encode(i, mParamPayload[i], mProcessGroup);
        Check((ret != OK), ret, "%s, call p2p encode fail", __func__);
        ciprBuf = registerUserBuffer(mParamPayload[i].size, mParamPayload[i].data);
        Check(!ciprBuf, NO_MEMORY, "%s, register param bin buffer fail", __func__);
        mTerminalBuffers[i] = ciprBuf;
    }

    int size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
    ciprBuf = (inFd[MAIN_PORT] >= 0) ? registerUserBuffer(size, inFd[MAIN_PORT]) : registerUserBuffer(size, inPtr[MAIN_PORT]);
    Check(!ciprBuf, NO_MEMORY, "%s, register input buffer fail", __func__);
    mTerminalBuffers[VPREGDC_ISL_HQ_4K_TERMINAL_ID_GET] = ciprBuf;

    // As YUV420 input is 12b per component, one channel(Y/U/V) is 16b, need double the size
    size = CameraUtils::getFrameSize(mDstFrame[MAIN_PORT].mFormat, mDstFrame[MAIN_PORT].mWidth, mDstFrame[MAIN_PORT].mHeight) * 2;
    ciprBuf = (outFd[MAIN_PORT] >= 0) ? registerUserBuffer(size, outFd[MAIN_PORT]) : registerUserBuffer(size, outPtr[MAIN_PORT]);
    Check(!ciprBuf, NO_MEMORY, "%s, register output buffer fail", __func__);
    mTerminalBuffers[VPREGDC_ISL_HQ_4K_TERMINAL_ID_PUT_OUT] = ciprBuf;

    return ret;
}

int Hp4KPreGDC::iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
                            ia_binary_data *statistics, const ia_binary_data *ipuParameters)
{
    // config the data terminal
    int ret = setTerminalParams(mFrameFormatType);
    Check((ret != OK), ret, "%s, call setTerminalParams fail", __func__);

    // create process group
    mProcessGroup = ia_css_process_group_create(getCiprBufferPtr(mPGBuffer),
                   (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer),
                   (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer));
    Check(!mProcessGroup, UNKNOWN_ERROR, "Create process group failed.");

    mPGParamAdapt->updatePAL(ipuParameters);
    ret = prepareTerminalBuffers(inBuf, outBuf);
    Check((ret != OK), ret, "%s, prepareTerminalBuffers fail with %d", __func__, ret);

    ret = handleCmd();
    Check((ret != OK), ret, "%s, call handleCmd fail", __func__);

    ret = handleEvent();
    Check((ret != OK), ret, "%s, call handleEvent fail", __func__);

    if (statistics) {
        ret = decodeStats(statistics);
        Check((ret != OK), ret, "%s, call decodeStats fail", __func__);
    }

    dumpTerminalPyldAndDesc(PG_ID, inBuf[MAIN_PORT]->getSequence());

    return ret;
}

int Hp4KPreGDC::decodeStats(ia_binary_data *statistics)
{
    int ret = OK;
    int terminalCount = ia_css_process_group_get_terminal_count(mProcessGroup);
    for (int i = 0; i < terminalCount; i++) {
        ia_css_terminal_t *terminal = ia_css_process_group_get_terminal(mProcessGroup, i);
        if ((terminal->terminal_type != IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT) &&
            (terminal->terminal_type != IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT)) {
            continue;
        }
        ia_cipr_memory_t memory;
        ia_cipr_buffer_get_memory(mTerminalBuffers[terminal->tm_index], &memory);
        ia_binary_data payload;
        payload.data = (void *)memory.cpu_ptr;
        payload.size = memory.size;
        ret = mPGParamAdapt->decode(terminal->tm_index, payload, mProcessGroup);
        Check((ret != OK), ret, "%s, call p2p decode fail", __func__);
    }

    ret = mPGParamAdapt->serializeDecodeCache(statistics);
    Check((ret != OK), ret, "%s, call p2p serialize decode cache fail", __func__);
    return ret;
}

} //namespace icamera
