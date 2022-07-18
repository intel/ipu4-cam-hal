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

#define LOG_TAG "FisheyePipeline"

#include "FisheyePipeline.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "iutils/CameraDump.h"

namespace icamera {
#define PAGE_SIZE (getpagesize())

static const char SYS_FS_CONCURRENCY_CTRL[] = "/sys/module/intel_ipu4_psys/parameters/enable_concurrency";
FILE*  FisheyePipeline::mFwConcurFile = nullptr;
Mutex FisheyePipeline::mPipeMutex;

FisheyePipeline::FisheyePipeline(int cameraId) : PSysPipeBase(PG_ID),
    mCameraId(cameraId)
{
    LOG1("%s", __func__);

    for (int i = 0; i < (YUYV_LDC_TERMINAL_ID_PUT_MAIN + 1); i++) {
        mFisheyeFrameFmtTypeList[i] = IA_CSS_N_FRAME_FORMAT_TYPES;
    }
    mFrameFormatType = mFisheyeFrameFmtTypeList;
    // INTEL_DVS_S
    mIntelDvs = new IntelDvs(mCameraId);
    // INTEL_DVS_E
    CLEAR(mParamPayload);
    mDewarpingMode = FISHEYE_DEWARPING_OFF;
    enableConcurrency(false);
}

FisheyePipeline::~FisheyePipeline()
{
    LOG1("%s", __func__);
    // INTEL_DVS_S
    mIntelDvs->deinit();
    delete mIntelDvs;
    // INTEL_DVS_E
    for (int i = 0; i < mTerminalCount; i++) {
        if (mParamPayload[i].data) {
            IA_CIPR_FREE(mParamPayload[i].data);
        }
    }
    enableConcurrency(true);
}

void FisheyePipeline::setKernelConfig()
{
    ia_isp_bxt_resolution_info_t defaultResInfo =
    {
        1280, 720, { 0, 0, 0, 0 }, 1280, 720, { 0, 0, 0, 0 }
    };

    ia_isp_bxt_run_kernels_t defaultKernelSetting[numKernels] =
    {
        {
            60000,           /* stream_id */
            ia_pal_uuid_isp_gdc3, /* kernel_uuid = ia_pal_uuid_isp_gdc3 */
            1,               /* enable */
            nullptr,            /* resolution_info */
            nullptr,            /* resolution_history*/
            { 0, 0, 0, 0 }   /* metadata[4] */
        },
    };

    MEMCPY_S(mFisheyeKernels, numKernels * sizeof(ia_isp_bxt_run_kernels_t),
        defaultKernelSetting, numKernels * sizeof(ia_isp_bxt_run_kernels_t));

    for (int i = 0; i < numKernels; i++) {
        mFisheyeKernels[i].resolution_info = &mKernelResinfo[i];
        MEMCPY_S(mFisheyeKernels[i].resolution_info, sizeof(ia_isp_bxt_resolution_info_t),
            &defaultResInfo, sizeof(ia_isp_bxt_resolution_info_t));
        mFisheyeKernels[i].resolution_info->output_width = ALIGN_64(mDstFrame[MAIN_PORT].mWidth);
        mFisheyeKernels[i].resolution_info->output_height = ALIGN_64(mDstFrame[MAIN_PORT].mHeight);
    }
}

int FisheyePipeline::setTerminalParams(const ia_css_frame_format_type* frame_format_types)
{
    LOG1("%s", __func__);

    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);
    ia_css_program_group_manifest_t* pg_manifest =
        (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer);

    for (int i = 0; i < mTerminalCount; i++) {
        ia_css_terminal_param_t *terminalParam =
            ia_css_program_group_param_get_terminal_param(pgParamsBuf, i);
        Check(!terminalParam, -1, "@%s, call ia_css_program_group_param_get_terminal_param fail", __func__);
        ia_css_terminal_manifest_t *terminal_manifest = ia_css_program_group_manifest_get_term_mnfst(pg_manifest, i);
        ia_css_terminal_type_t  terminal_type = ia_css_terminal_manifest_get_type(terminal_manifest);

        if (!((terminal_type == IA_CSS_TERMINAL_TYPE_DATA_OUT) || (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_IN))) {
            continue;
        }

        FrameInfo config = mSrcFrame[MAIN_PORT];
        if (terminal_type == IA_CSS_TERMINAL_TYPE_DATA_OUT) {
            config = mDstFrame[MAIN_PORT];
        }

        terminalParam->frame_format_type = frame_format_types[i];
        terminalParam->dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;
        /* hardcode bpp/bpe now, FW need figure out the value used */
        terminalParam->bpp = 8;
        terminalParam->bpe = 8;
        terminalParam->fragment_dimensions[IA_CSS_COL_DIMENSION] = config.mWidth;
        terminalParam->fragment_dimensions[IA_CSS_ROW_DIMENSION] = config.mHeight;
        terminalParam->stride = config.mStride;
        terminalParam->offset = 0;
        terminalParam->index[IA_CSS_COL_DIMENSION] = 0;
        terminalParam->index[IA_CSS_ROW_DIMENSION] = 0;
    }

    return OK;
}

int FisheyePipeline::prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                            vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);

    Check((srcBufs.size() != 1 || dstBufs.size() != 1), UNKNOWN_ERROR,
        "@%s, srcBufs size:%zu or dstBufs size:%zu is not 1",
        __func__, srcBufs.size(), dstBufs.size());

    Check((mDewarpingMode <= FISHEYE_DEWARPING_OFF), UNKNOWN_ERROR, "@%s, dewarping mode error", __func__);

    mP2p->updatePAL(&mDvsResInfo[mDewarpingMode - 1].mMorphTable);

    ia_cipr_buffer_t* ciprBuf;
    for (int i = 0; i < mTerminalCount; i++) {
        unsigned int payloadSize = 0;
        int ret = mP2p->getPayloadSize(i, &payloadSize);
        Check((ret != OK), ret, "@%s, call get payload size fail", __func__);
        if (payloadSize == 0)
            continue;

        Check((i >= kParamNum), BAD_INDEX, "mParamPayload index is out of range [0, %d]", kParamNum);
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
        mP2p->encode(i, mParamPayload[i]);
        ciprBuf = registerUserBuffer(mParamPayload[i].size, mParamPayload[i].data);
        Check(!ciprBuf, NO_MEMORY, "@%s, register param buffer fail", __func__);
        mTerminalBuffers[i] = ciprBuf;
    }

    // YUYV_LDC_TERMINAL_ID_GET
    int size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
    ciprBuf = registerUserBuffer(srcBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register input buffer fail", __func__);
    mTerminalBuffers[YUYV_LDC_TERMINAL_ID_GET] = ciprBuf;

    // YUYV_LDC_TERMINAL_ID_PUT_MAIN
    size = CameraUtils::getFrameSize(mDstFrame[MAIN_PORT].mFormat, mDstFrame[MAIN_PORT].mWidth, mDstFrame[MAIN_PORT].mHeight);
    ciprBuf = registerUserBuffer(dstBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register output buffer 1 fail", __func__);
    mTerminalBuffers[YUYV_LDC_TERMINAL_ID_PUT_MAIN] = ciprBuf;

    return OK;
}

int FisheyePipeline::prepare()
{
    LOG1("%s", __func__);

    // INTEL_DVS_S
    if (mIntelDvs->init() != OK) {
        return UNKNOWN_ERROR;
    }
    // INTEL_DVS_E
    mNeedP2p = true;
    setKernelConfig();
    mP2p->setKernelConfig(numKernels, mFisheyeKernels);

    mFisheyeFrameFmtTypeList[YUYV_LDC_TERMINAL_ID_GET] = PSysPipeBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mFisheyeFrameFmtTypeList[YUYV_LDC_TERMINAL_ID_PUT_MAIN] = PSysPipeBase::getCssFmt(mDstFrame[MAIN_PORT].mFormat);
    ia_css_kernel_bitmap_t bitmapMask = ia_css_kernel_bitmap_create_from_uint64(0x2fULL);
    mKernelBitmap = ia_css_kernel_bitmap_intersection(mKernelBitmap, bitmapMask);

    LOG1("dewarping mode = %d", mDewarpingMode);

    // INTEL_DVS_S
    runDVS();
    // INTEL_DVS_E

    PsysParams psysParam;
    CLEAR(psysParam);
    psysParam.fragmentDesc.fragment_width = mDstFrame[MAIN_PORT].mWidth;
    psysParam.fragmentDesc.fragment_height = mDstFrame[MAIN_PORT].mHeight;

    if (mDewarpingMode == FISHEYE_DEWARPING_REARVIEW) {
        psysParam.dvsMorphTable = &mDvsResInfo[FISHEYE_DVS_RESULT_REARVIEW].mMorphTable;
    } else if (mDewarpingMode == FISHEYE_DEWARPING_HITCHVIEW)
        psysParam.dvsMorphTable = &mDvsResInfo[FISHEYE_DVS_RESULT_HITCHVIEW].mMorphTable;
    else
        psysParam.dvsMorphTable = nullptr;
    mPsysParam = psysParam;

    return PSysPipeBase::prepare();
}

int FisheyePipeline::iterate(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                             vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);
    ia_css_process_group_t* processGroup = preparePG();
    Check(!processGroup, UNKNOWN_ERROR, "@%s, failed to prepare process group", __func__);

    int ret = prepareTerminalBuffers(srcBufs, dstBufs);
    Check((ret != OK), ret, "@%s, prepareTerminalBuffers fail with %d", __func__, ret);

    ret = handleCmd();
    Check((ret != OK), ret, "@%s, call handleCmd fail", __func__);

    ret = handleEvent();
    Check((ret != OK), ret, "@%s, call handleEvent fail", __func__);
    return OK;
}

// INTEL_DVS_S
int FisheyePipeline::runDVS()
{
    LOG1("%s", __func__);

    aiq_parameter_t aiqParam;
    aiqParam.reset();
    if (mDewarpingMode != FISHEYE_DEWARPING_OFF)
        aiqParam.ldcMode = LDC_MODE_ON;
    aiqParam.fps = 30;

    for (int i = 0; i < FISHEYE_DVS_RESULT_MAX; i++) {
        TuningMode tuningMode = TUNING_MODE_VIDEO_REAR_VIEW;
        if (i == FISHEYE_DVS_RESULT_REARVIEW) {
            tuningMode = TUNING_MODE_VIDEO_REAR_VIEW;
        } else {
            tuningMode = TUNING_MODE_VIDEO_HITCH_VIEW;
        }
        aiqParam.tuningMode = tuningMode;

        int ret = mIntelDvs->configure(tuningMode, ia_pal_uuid_isp_gdc3,
                            mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight,
                            mDstFrame[MAIN_PORT].mWidth, mDstFrame[MAIN_PORT].mHeight);
        Check(ret != OK, ret, "%s: Could not configure DVS library", __func__);
        mIntelDvs->updateParameter(aiqParam);
        ia_aiq_ae_results aeResults;
        CLEAR(aeResults);

        ret = mIntelDvs->run(aeResults, &mDvsResInfo[i], 0, 0);
        Check(ret != OK, BAD_VALUE, "%s run DVS fail", __func__);
    }
    return OK;
}
// INTEL_DVS_E

int FisheyePipeline::setParameters(const Parameters& param)
{
    param.getFisheyeDewarpingMode(mDewarpingMode);
    return OK;
}

void FisheyePipeline::enableConcurrency(bool enable)
{
    AutoMutex l(mPipeMutex);

    const char data = enable ? 'Y' : 'N';
    LOG2("%s:enable:%d", __func__, enable);

    if (mFwConcurFile == nullptr) {
        LOG1("%s: open file for concurrency control", __func__);
        mFwConcurFile = fopen(SYS_FS_CONCURRENCY_CTRL, "w");
    }
    if (mFwConcurFile != nullptr) {
        rewind(mFwConcurFile);
        LOG2("%s: write FW concurrency file with enable flag: %d", __func__, enable);
        if ((fwrite((void*)&data, 1, 1, mFwConcurFile)) == 1) {
            fflush(mFwConcurFile);
        } else {
            LOGE("Error to write to sys fs enable_concurrency");
        }
        LOG1("%s: close file for concurrency control", __func__);
        fclose(mFwConcurFile);
        mFwConcurFile = nullptr;
    } else {
        LOGE("Failed to operate FW concurrency control file");
    }
}

} //namespace icamera

