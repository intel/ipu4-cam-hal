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

#define LOG_TAG "ScalePipeline"

#include "ScalePipeline.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "iutils/Utils.h"
#include "iutils/CameraDump.h"

namespace icamera {

ia_isp_bxt_resolution_info_t osc_res_info =
{
    //the input and output resolution can been changed according to stream config
    640, 480, { 0, 0, 0, 0 }, 1920, 1080, { 0, 0, 0, 0 }
};

ia_isp_bxt_resolution_info_t osc_720p_res_info =
{
    1920, 1080, { 0, 0, 0, 0 }, 1280, 720, { 0, 0, 0, 0 }
};

ia_isp_bxt_run_kernels_t run_kernels_psys_scale[] =
{
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_bxt_ofa_mp, /* kernel_uuid */
        1,               /* enable */
        nullptr,         /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_bxt_ofa_dp, /* kernel_uuid */
        1,               /* enable */
        nullptr,         /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_bxt_ofa_ppp, /* kernel_uuid */
        1,               /* enable */
        nullptr,         /* resolution_info */
        nullptr,
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_dma_cropper_mp, /* kernel_uuid = ia_pal_uuid_isp_bxt_ofa_mp */
        1,               /* enable */
        &osc_720p_res_info,            /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_dma_cropper_dp, /* kernel_uuid = ia_pal_uuid_isp_bxt_ofa_dp */
        1,               /* enable */
        &osc_720p_res_info,            /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_dma_cropper_ppp, /* kernel_uuid = ia_pal_uuid_isp_bxt_ofa_ppp */
        1,               /* enable */
        &osc_720p_res_info,            /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_sc_outputscaler_dp, /* kernel_uuid */
        1,               /* enable */
        &osc_res_info,   /* resolution_info */
        nullptr,         /* resolution_history*/
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
    {
        60000,           /* stream_id */
        ia_pal_uuid_isp_sc_outputscaler_ppp, /* kernel_uuid */
        1,               /* enable */
        &osc_res_info,   /* resolution_info */
        nullptr,
        { 0, 0, 0, 0 }   /* metadata[4] */
    },
};

ScalePipeline::ScalePipeline() : PSysPipeBase(PG_ID)
{
    for (int i = 0; i < (YUYV_SCALE_TERMINAL_ID_PUT_PP + 1); i++) {
        mScaleFrameFmtTypeList[i] = IA_CSS_N_FRAME_FORMAT_TYPES;
    }
    mFrameFormatType = mScaleFrameFmtTypeList;
    CLEAR(mParamPayload);
    CLEAR(mCropRegion);
}

ScalePipeline::~ScalePipeline()
{
    for (int i = 0; i < mTerminalCount; i++) {
        if (mParamPayload[i].data) {
            IA_CIPR_FREE(mParamPayload[i].data);
        }
    }
}

int ScalePipeline::setTerminalParams(const ia_css_frame_format_type* frame_format_types)
{
    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    for (int i = 0; i < mTerminalCount; i++) {
        ia_css_terminal_param_t *terminalParam =
            ia_css_program_group_param_get_terminal_param(pgParamsBuf, i);
        Check(!terminalParam, -1, "@%s, call ia_css_program_group_param_get_terminal_param fail", __func__);

        FrameInfo config = mSrcFrame[MAIN_PORT];

        if (i >= YUYV_SCALE_TERMINAL_ID_PUT_DISPLAY )
            config = mDstFrame[mScaleMap[MAIN_PORT]];

        if ( mDstFrame.size() == 2 && i == YUYV_SCALE_TERMINAL_ID_PUT_MAIN)
            config = mDstFrame[mScaleMap[SECOND_PORT]];

        if ( mDstFrame.size() == 3 && i == YUYV_SCALE_TERMINAL_ID_PUT_PP)
            config = mDstFrame[mScaleMap[THIRD_PORT]];

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

int ScalePipeline::prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                          vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);

    Check((srcBufs.size() != 1 || dstBufs.size() == 0 || dstBufs.size() > 3), UNKNOWN_ERROR,
        "@%s, wrong, srcBufs size:%zu, dstBufs size:%zu",
        __func__, srcBufs.size(), dstBufs.size());

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
        Check(!ciprBuf, NO_MEMORY, "@%s, register param bin buffer fail", __func__);
        mTerminalBuffers[i] = ciprBuf;
    }

    // YUYV_SCALE_TERMINAL_ID_GET_YUYV
    int size = CameraUtils::getFrameSize(mSrcFrame[MAIN_PORT].mFormat, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight);
    ciprBuf = registerUserBuffer(srcBufs[0], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register input buffer fail", __func__);
    mTerminalBuffers[YUYV_SCALE_TERMINAL_ID_GET_YUYV] = ciprBuf;

    // YUYV_SCALE_TERMINAL_ID_PUT_DISPLAY
    size = CameraUtils::getFrameSize(mDstFrame[mScaleMap[MAIN_PORT]].mFormat, mDstFrame[mScaleMap[MAIN_PORT]].mWidth, mDstFrame[mScaleMap[MAIN_PORT]].mHeight);
    ciprBuf = registerUserBuffer(dstBufs[mScaleMap[MAIN_PORT]], size);
    Check(!ciprBuf, NO_MEMORY, "@%s, register output buffer 1 fail", __func__);
    mTerminalBuffers[YUYV_SCALE_TERMINAL_ID_PUT_DISPLAY] = ciprBuf;

    // YUYV_SCALE_TERMINAL_ID_PUT_MAIN
    if (dstBufs.size() > 1) {
        size = CameraUtils::getFrameSize(mDstFrame[mScaleMap[SECOND_PORT]].mFormat, mDstFrame[mScaleMap[SECOND_PORT]].mWidth, mDstFrame[mScaleMap[SECOND_PORT]].mHeight);
        ciprBuf = registerUserBuffer(dstBufs[mScaleMap[SECOND_PORT]], size);
        Check(!ciprBuf, NO_MEMORY, "@%s, register output buffer 2 fail", __func__);
        mTerminalBuffers[YUYV_SCALE_TERMINAL_ID_PUT_MAIN] = ciprBuf;
    }

    // YUYV_SCALE_TERMINAL_ID_PUT_PP
    if (dstBufs.size() > 2) {
        size = CameraUtils::getFrameSize(mDstFrame[mScaleMap[THIRD_PORT]].mFormat, mDstFrame[mScaleMap[THIRD_PORT]].mWidth, mDstFrame[mScaleMap[THIRD_PORT]].mHeight);
        ciprBuf = registerUserBuffer(dstBufs[mScaleMap[THIRD_PORT]], size);
        Check(!ciprBuf, NO_MEMORY, "@%s, register output buffer 3 fail", __func__);
        mTerminalBuffers[YUYV_SCALE_TERMINAL_ID_PUT_PP] = ciprBuf;
    }
    return OK;
}

int ScalePipeline::setParameters(const Parameters& param)
{
    param.getCropRegion(mCropRegion);
    return OK;
}

int ScalePipeline::setCropAndFormatInfo()
{
    ia_isp_bxt_resolution_info_t *resInfoOFS = run_kernels_psys_scale[ISP_SC_OUTPUTSCALER_DP].resolution_info;
    ia_isp_bxt_resolution_info_t *resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_DP].resolution_info;

    FrameInfo dstframe = mDstFrame[mScaleMap[MAIN_PORT]];
    if (mCropRegion.flag == 0) {
        double ratioWidth = 1;
        double ratioHeight = 1;
        for (const auto& item : mDstFrame) {
            if(item.first == MAIN_PORT) {
                resInfoOFS = run_kernels_psys_scale[ISP_SC_OUTPUTSCALER_DP].resolution_info;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_DP].resolution_info;
                dstframe = mDstFrame[mScaleMap[MAIN_PORT]];
            } else if(item.first == SECOND_PORT) {
                resInfoOFS = nullptr;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_MP].resolution_info;
                dstframe = mDstFrame[mScaleMap[SECOND_PORT]];
            } else if(item.first == THIRD_PORT) {
                resInfoOFS = run_kernels_psys_scale[ISP_SC_OUTPUTSCALER_PPP].resolution_info;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_PPP].resolution_info;
                dstframe = mDstFrame[mScaleMap[THIRD_PORT]];
            }

            if(resInfoOFS != nullptr) {
                //OFS's input size is source image res.
                resInfoOFS->input_width = mSrcFrame[MAIN_PORT].mWidth;
                resInfoOFS->input_height= mSrcFrame[MAIN_PORT].mHeight;

                ratioWidth =  (double)dstframe.mWidth / (double)mSrcFrame[MAIN_PORT].mWidth;
                ratioHeight =  (double)dstframe.mHeight / (double)mSrcFrame[MAIN_PORT].mHeight;

                if (ratioWidth !=  ratioHeight) {
                    if (ratioWidth > ratioHeight) {
                        resInfoOFS->output_width = dstframe.mWidth;
                        resInfoOFS->output_height = ALIGN((int)(ratioWidth * mSrcFrame[MAIN_PORT].mHeight), 2);
                        ratioHeight = (double)resInfoOFS->output_height / (double)mSrcFrame[MAIN_PORT].mHeight;
                        //for some special cases, if the new ratioHeight is changed after alignment, the original ratiaWidth
                        //can not be smaller than the new ratiaHeight
                        if (ratioHeight > ratioWidth) {
                            resInfoOFS->output_height = ALIGN(((int)(ratioWidth * mSrcFrame[MAIN_PORT].mHeight) - 1), 2);
                        }
                    }else {
                        //OFS's output width needs 128 line align
                        int alignedWidth = ALIGN((int)(ratioHeight * mSrcFrame[MAIN_PORT].mWidth), 128);
                        ratioWidth = (double)alignedWidth / (double)mSrcFrame[MAIN_PORT].mWidth;
                        if (ratioWidth > ratioHeight) {
                            resInfoOFS->output_height = ALIGN((int)(ratioWidth * mSrcFrame[MAIN_PORT].mHeight), 2);
                        } else {
                            resInfoOFS->output_height = dstframe.mHeight;
                        }
                        resInfoOFS->output_width = alignedWidth;
                    }
                } else {
                    resInfoOFS->output_width = dstframe.mWidth;
                    resInfoOFS->output_height = dstframe.mHeight;
                }
            }

            //set DMA Cropper info.
            //OFS's output is DMA crop's input
            if( resInfoOFS != nullptr) {
                resInfoDMA->input_width = resInfoOFS->output_width;
                resInfoDMA->input_height= resInfoOFS->output_height;
            } else {
                resInfoDMA->input_width = mSrcFrame[MAIN_PORT].mWidth;
                resInfoDMA->input_height= mSrcFrame[MAIN_PORT].mHeight;
            }

            //DMA's output size is Dst res.
            resInfoDMA->output_width = dstframe.mWidth;
            resInfoDMA->output_height = dstframe.mHeight;

            //cal. crop values(center crop)
            resInfoDMA->input_crop.left = ALIGN((resInfoDMA->input_width - resInfoDMA->output_width) / 2, 2);
            resInfoDMA->input_crop.top = ALIGN((resInfoDMA->input_height - resInfoDMA->output_height) / 2, 2);

            resInfoDMA->input_crop.right = resInfoDMA->input_width - resInfoDMA->output_width - resInfoDMA->input_crop.left;
            resInfoDMA->input_crop.bottom = resInfoDMA->input_height - resInfoDMA->output_height - resInfoDMA->input_crop.top;

            if(resInfoOFS != nullptr) {
                LOG2("width_ratio:%lf, height_ratio:%lf", ratioWidth, ratioHeight);
                LOG2("OFS input width:%d, height: %d, output width: %d height: %d",
                    mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight,
                    resInfoOFS->output_width, resInfoOFS->output_height);
            } else {
                LOG2("No scale supported for MP\n");
            }

            LOG2("DMA input %d  %d output(dst) %d  %d",
                resInfoDMA->input_width,
                resInfoDMA->input_height,
                resInfoDMA->output_width,
                resInfoDMA->output_height);

            LOG2("DMA input crop : left %d top %d right %d bottom %d",
                resInfoDMA->input_crop.left,
                resInfoDMA->input_crop.top,
                resInfoDMA->input_crop.right,
                resInfoDMA->input_crop.bottom);
        }
    } else {
        for (const auto& item : mDstFrame) {
            if(item.first == MAIN_PORT) {
                resInfoOFS = run_kernels_psys_scale[ISP_SC_OUTPUTSCALER_DP].resolution_info;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_DP].resolution_info;
                dstframe = mDstFrame[mScaleMap[MAIN_PORT]];
            } else if(item.first == SECOND_PORT) {
                resInfoOFS = nullptr;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_MP].resolution_info;
                dstframe = mDstFrame[mScaleMap[SECOND_PORT]];
            } else if(item.first == THIRD_PORT) {
                resInfoOFS = run_kernels_psys_scale[ISP_SC_OUTPUTSCALER_PPP].resolution_info;
                resInfoDMA = run_kernels_psys_scale[ISP_DMA_CROPPER_PPP].resolution_info;
                dstframe = mDstFrame[mScaleMap[THIRD_PORT]];
            }

            if(resInfoOFS != nullptr) {
                //OFS's input size is source image res.
                resInfoOFS->input_width = mSrcFrame[MAIN_PORT].mWidth;
                resInfoOFS->input_height= mSrcFrame[MAIN_PORT].mHeight;
                resInfoOFS->output_width = resInfoOFS->input_width;
                resInfoOFS->output_height = resInfoOFS->input_height;
           }

           //set DMA Cropper info.
           //OFS's output is DMA crop's input
           if( resInfoOFS != nullptr) {
               resInfoDMA->input_width = resInfoOFS->output_width;
               resInfoDMA->input_height= resInfoOFS->output_height;
           } else {
               resInfoDMA->input_width = mSrcFrame[MAIN_PORT].mWidth;
               resInfoDMA->input_height= mSrcFrame[MAIN_PORT].mHeight;
           }

           //DMA's output size is Dst res.
           resInfoDMA->output_width = dstframe.mWidth;
           resInfoDMA->output_height = dstframe.mHeight;

           //cal. crop values(not center crop)
           resInfoDMA->input_crop.left = mCropRegion.x << 1;
           resInfoDMA->input_crop.top = mCropRegion.y << 1;
           resInfoDMA->input_crop.right = 0;
           resInfoDMA->input_crop.bottom = 0;

           if(resInfoOFS != nullptr) {
               LOG2("OFS input width:%d, height: %d, output width: %d height: %d",
                   mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight,
                   resInfoOFS->output_width, resInfoOFS->output_height);
           } else {
               LOG2("No scale supported for MP\n");
           }

           LOG2("DMA input %d  %d output(dst) %d  %d",
               resInfoDMA->input_width,
               resInfoDMA->input_height,
               resInfoDMA->output_width,
               resInfoDMA->output_height);

           LOG2("DMA input crop : left %d top %d right %d bottom %d",
               resInfoDMA->input_crop.left,
               resInfoDMA->input_crop.top,
               resInfoDMA->input_crop.right,
               resInfoDMA->input_crop.bottom);
        }
    }

    for(uint32_t i = 0; i < ARRAY_SIZE(run_kernels_psys_scale); i++) {
        switch(dstframe.mFormat) {
            case V4L2_PIX_FMT_YUV420:
                run_kernels_psys_scale[i].metadata[1] = OF_FORMAT_I420;
                break;
            case V4L2_PIX_FMT_NV12:
                run_kernels_psys_scale[i].metadata[1] = OF_FORMAT_NV12;
                break;
            case V4L2_PIX_FMT_NV21:
                run_kernels_psys_scale[i].metadata[1] = OF_FORMAT_NV21;
                break;
            default:
                LOG1("scale PG not support the format: %d", dstframe.mFormat);
                break;
        }
    }

    return OK;
}

void ScalePipeline::setScaleMapInfo(const FrameInfoPortMap& outputInfos)
{
    for (const auto& item : outputInfos) {
        mScaleMap[item.first] = item.first;
    }

    if (outputInfos.size() >= 2)
    {
        FrameInfo frameInfo = outputInfos.at(MAIN_PORT);
        Port port = MAIN_PORT;
        for (const auto& item : outputInfos) {
            port = item.first;
            frameInfo = item.second;

            if( frameInfo.mWidth == mSrcFrame[MAIN_PORT].mWidth &&
                frameInfo.mHeight == mSrcFrame[MAIN_PORT].mHeight &&
                port != SECOND_PORT )
            {
                mScaleMap[port] = SECOND_PORT;
                mScaleMap[SECOND_PORT] = port;
                break;
            }
        }
    }
}

int ScalePipeline::prepare()
{
    mNeedP2p = true;

    setScaleMapInfo(mDstFrame);
    setCropAndFormatInfo();
    mP2p->setKernelConfig(ARRAY_SIZE(run_kernels_psys_scale), run_kernels_psys_scale);

    mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_GET_YUYV] = PSysPipeBase::getCssFmt(mSrcFrame[MAIN_PORT].mFormat);
    mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_DISPLAY] = PSysPipeBase::getCssFmt(mDstFrame[mScaleMap[MAIN_PORT]].mFormat);
    mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_MAIN] = PSysPipeBase::getCssFmt(mDstFrame[mScaleMap[MAIN_PORT]].mFormat);
    mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_PP] = PSysPipeBase::getCssFmt(mDstFrame[mScaleMap[MAIN_PORT]].mFormat);

    // YUYV_SCALE_KERNEL_ID_PUT_MAIN = 8
    // YUYV_SCALE_KERNEL_ID_PUT_PP = 9
    // Disble YUYV_SCALE_TERMINAL_ID_PUT_MAIN & YUYV_SCALE_TERMINAL_ID_PUT_PP
    ia_css_kernel_bitmap_t bitmapMask = ia_css_kernel_bitmap_create_from_uint64(0x0ffULL);

    if(mDstFrame.size() >=2 ) {
        bitmapMask = ia_css_kernel_bitmap_create_from_uint64(0x1ffULL);
        mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_MAIN] = PSysPipeBase::getCssFmt(mDstFrame[mScaleMap[SECOND_PORT]].mFormat);
    }

    if(mDstFrame.size() >=3 ) {
        bitmapMask = ia_css_kernel_bitmap_create_from_uint64(0x3ffULL);
        mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_PP] = PSysPipeBase::getCssFmt(mDstFrame[mScaleMap[THIRD_PORT]].mFormat);
    }

    mKernelBitmap = ia_css_kernel_bitmap_intersection(mKernelBitmap, bitmapMask);

    return PSysPipeBase::prepare();
}

} //namespace icamera

