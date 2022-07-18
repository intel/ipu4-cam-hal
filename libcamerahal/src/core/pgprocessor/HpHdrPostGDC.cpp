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

#define LOG_TAG "HpHdrPostGDC"

#include "HpHdrPostGDC.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

namespace icamera {
#define PAGE_SIZE (getpagesize())

HpHdrPostGDC::HpHdrPostGDC() : PGBase(PG_ID)
{
    for (int i = 0; i < (VPOSTGDC_HP_TERMINAL_ID_N); i++) {
        mHpHdrPostGDCFrameFmtTypeList[i] = IA_CSS_N_FRAME_FORMAT_TYPES;
    }
    mFrameFormatType = mHpHdrPostGDCFrameFmtTypeList;
    mAllocTNRBuffers = false;
    mTNRGetBuf = nullptr;
    mTNRPutBuf = nullptr;
    mTNRGetPsimBuf = nullptr;
    mTNRPutPsimBuf = nullptr;
    CLEAR(mParamPayload);
}

HpHdrPostGDC::~HpHdrPostGDC()
{
    for (int i = 0; i < mTerminalCount; i++) {
        if (mParamPayload[i].data) {
            IA_CIPR_FREE(mParamPayload[i].data);
        }
    }

    if(mTNRGetBuf) {
        free(mTNRGetBuf);
        mTNRGetBuf = nullptr;
    }

    if(mTNRPutBuf) {
        free(mTNRPutBuf);
        mTNRPutBuf = nullptr;
    }

    if(mTNRGetPsimBuf) {
        free(mTNRGetPsimBuf);
        mTNRGetPsimBuf = nullptr;
    }

    if(mTNRPutPsimBuf) {
        free(mTNRPutPsimBuf);
        mTNRPutPsimBuf = nullptr;
    }
}

int HpHdrPostGDC::configTerminal()
{
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_GET] = PGBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    if (mDstFrame.find(SECOND_PORT) != mDstFrame.end())
        mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_PUT_DISPLAY] = PGBase::getCssFmt(mDstFrame[SECOND_PORT].mFormat);
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_PUT_MAIN] = PGBase::getCssFmt(mDstFrame[MAIN_PORT].mFormat);
    if (mDstFrame.find(THIRD_PORT) != mDstFrame.end())
        mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_PUT_PP] = PGBase::getCssFmt(mDstFrame[THIRD_PORT].mFormat);
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_SPTNR_TERMINAL] = PGBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL] = IA_CSS_DATA_FORMAT_Y800;
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_SPTNR_TERMINAL] = PGBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mHpHdrPostGDCFrameFmtTypeList[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL] = IA_CSS_DATA_FORMAT_Y800;

    // Add disabled data terminal to mDisableDataTermials vector, which will be used to calculate the final kernel bitmap
    /* The second port is disable, add the display output terminal to mDisableDataTermials. */
    if (mDstFrame.find(SECOND_PORT) == mDstFrame.end())
        mDisableDataTermials.push_back(VPOSTGDC_HP_TERMINAL_ID_PUT_DISPLAY);

    /* The third port is disable, add the pp output terminal to mDisableDataTermials. */
    if (mDstFrame.find(THIRD_PORT) == mDstFrame.end())
        mDisableDataTermials.push_back(VPOSTGDC_HP_TERMINAL_ID_PUT_PP);

    return OK;
}

int HpHdrPostGDC::setTerminalParams(const ia_css_frame_format_type* frameFormatTypes)
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
        if (i == VPOSTGDC_HP_TERMINAL_ID_PUT_MAIN) {
            config = mDstFrame[MAIN_PORT];
        }

        if (mDstFrame.find(SECOND_PORT) != mDstFrame.end() && i == VPOSTGDC_HP_TERMINAL_ID_PUT_DISPLAY) {
            config = mDstFrame[SECOND_PORT];
        }

        if (mDstFrame.find(THIRD_PORT) != mDstFrame.end() && i == VPOSTGDC_HP_TERMINAL_ID_PUT_PP) {
            config = mDstFrame[THIRD_PORT];
        }

        terminalParam->frame_format_type = frameFormatTypes[i];
        terminalParam->dimensions[IA_CSS_COL_DIMENSION] = ((i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL ||
                                                            i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL) ?
                                                            config.mWidth / 8 : config.mWidth);
        terminalParam->dimensions[IA_CSS_ROW_DIMENSION] = ((i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL ||
                                                            i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL) ?
                                                            config.mHeight / 32 : config.mHeight);
        terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION] = terminalParam->dimensions[IA_CSS_COL_DIMENSION];
        terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION] = terminalParam->dimensions[IA_CSS_ROW_DIMENSION];
        /* Bits per pixel (bpp) is the total amount of bits used per pixel in the
         * whole image. We receive the bpp value with the image format from the
         * user. However, FW expects bits per pixel to be set as the bits per
         * Y-plane element, which differs from the definition of bpp we use. */
        // As YUV420 input is 12b per component, one channel(Y/U/V) is 16b, need double the stride
        terminalParam->bpp = (i == VPOSTGDC_HP_TERMINAL_ID_GET ? 16 : 8);
        terminalParam->bpe = terminalParam->bpp;
        terminalParam->stride = ((i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL ||
                                  i == VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL) ?
                                  terminalParam->dimensions[IA_CSS_COL_DIMENSION] * 2 :
                                  i == VPOSTGDC_HP_TERMINAL_ID_GET ? config.mStride * 2 : config.mStride);
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

int HpHdrPostGDC::prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf)
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
    ia_cipr_buffer_t* tmpBuffer;
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

    // As YUV420 input is 12b per component, one channel(Y/U/V) is 16b, need double the size
    int size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight) * 2;
    ciprBuf = (inFd[MAIN_PORT] >= 0) ? registerUserBuffer(size, inFd[MAIN_PORT]) : registerUserBuffer(size, inPtr[MAIN_PORT]);
    Check(!ciprBuf, NO_MEMORY, "%s, register input buffer 1 fail", __func__);
    mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_GET] = ciprBuf;

    if (!mAllocTNRBuffers) {
        LOG1("%s: allocate the TNR input and output buffers", __func__);
        size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
        ret = posix_memalign((void**)&mTNRGetBuf, PAGE_SIZE, size + PAGE_SIZE);
        Check(ret, NO_MEMORY, "%s, allocate input buffer 2 fail", __func__);

        ciprBuf = registerUserBuffer(size, mTNRGetBuf);
        Check(!ciprBuf, NO_MEMORY, "%s, register input buffer 2 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_SPTNR_TERMINAL] = ciprBuf;

        size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth / 8, mSrcFrame[MAIN_PORT].mHeight / 32) / 1.5 * 2;
        ret = posix_memalign((void**)&mTNRGetPsimBuf, PAGE_SIZE, size + PAGE_SIZE);
        Check(ret, NO_MEMORY, "%s, allocate input buffer 3 fail", __func__);

        ciprBuf = registerUserBuffer(size, mTNRGetPsimBuf);
        Check(!ciprBuf, NO_MEMORY, "%s, register input buffer 3 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL] = ciprBuf;

        size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
        ret = posix_memalign((void**)&mTNRPutBuf, PAGE_SIZE, size + PAGE_SIZE);
        Check(ret, NO_MEMORY, "%s, allocate output buffer 4 fail", __func__);

        ciprBuf = registerUserBuffer(size, mTNRPutBuf);
        Check(!ciprBuf, NO_MEMORY, "%s, register output buffer 4 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_SPTNR_TERMINAL] = ciprBuf;

        size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth / 8, mSrcFrame[MAIN_PORT].mHeight / 32) / 1.5 * 2;
        ret = posix_memalign((void**)&mTNRPutPsimBuf, PAGE_SIZE, size + PAGE_SIZE);
        Check(ret, NO_MEMORY, "%s, allocate output buffer 5 fail", __func__);

        ciprBuf = registerUserBuffer(size, mTNRPutPsimBuf);
        Check(!ciprBuf, NO_MEMORY, "%s, register output buffer 5 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL] = ciprBuf;

        mAllocTNRBuffers = true;
    } else {
        LOG1("%s: swith the TNR input and output buffers", __func__);
        tmpBuffer = mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_SPTNR_TERMINAL];
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_SPTNR_TERMINAL] =
            mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_SPTNR_TERMINAL];
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_SPTNR_TERMINAL] = tmpBuffer;

        tmpBuffer = mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL];
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_GET_RSIM_TERMINAL] =
            mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL];
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_STNR5_2_1_PUT_RSIM_TERMINAL] = tmpBuffer;
    }

    size = CameraUtils::getFrameSize(mDstFrame[MAIN_PORT].mFormat, mDstFrame[MAIN_PORT].mWidth, mDstFrame[MAIN_PORT].mHeight);
    ciprBuf = (outFd[MAIN_PORT] >= 0) ? registerUserBuffer(size, outFd[MAIN_PORT]) : registerUserBuffer(size, outPtr[MAIN_PORT]);
    Check(!ciprBuf, NO_MEMORY, "%s, register output buffer 1 fail", __func__);
    mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_PUT_MAIN] = ciprBuf;

    if (outPtr[SECOND_PORT] || (outFd[SECOND_PORT] >= 0)) {
        size = CameraUtils::getFrameSize(mDstFrame[SECOND_PORT].mFormat, mDstFrame[SECOND_PORT].mWidth, mDstFrame[SECOND_PORT].mHeight);
        ciprBuf = (outFd[SECOND_PORT] >= 0) ? registerUserBuffer(size, outFd[SECOND_PORT]) : registerUserBuffer(size, outPtr[SECOND_PORT]);
        Check(!ciprBuf, NO_MEMORY, "%s, register output buffer 2 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_PUT_DISPLAY] = ciprBuf;
    }

    if (outPtr[THIRD_PORT] || (outFd[THIRD_PORT] >= 0)) {
        size = CameraUtils::getFrameSize(mDstFrame[THIRD_PORT].mFormat, mDstFrame[THIRD_PORT].mWidth, mDstFrame[THIRD_PORT].mHeight);
        ciprBuf = (outFd[THIRD_PORT] >= 0) ? registerUserBuffer(size, outFd[THIRD_PORT]) : registerUserBuffer(size, outPtr[THIRD_PORT]);
        Check(!ciprBuf, NO_MEMORY, "%s, register output buffer 3 fail", __func__);
        mTerminalBuffers[VPOSTGDC_HP_TERMINAL_ID_PUT_PP] = ciprBuf;
    }

    return ret;
}

int HpHdrPostGDC::iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
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

    dumpTerminalPyldAndDesc(PG_ID, inBuf[MAIN_PORT]->getSequence());

    return ret;
}

} //namespace icamera
