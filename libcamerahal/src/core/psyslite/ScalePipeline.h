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

#define OF_FORMAT_I420 0
#define OF_FORMAT_YV12 1
#define OF_FORMAT_NV12 2
#define OF_FORMAT_NV21 3
#define OF_FORMAT_M420 4
#define OF_FORMAT_YUY2 5


namespace icamera {

/**
 * First Port: Scale PG output Port
 * Second Port: Stream output Port
 */
typedef std::map<Port, Port> ScalePortMap;

/**
 * \class ScalePipeline
 *
 * \brief As known as Scale up & down Conversion
 */
class ScalePipeline : public PSysPipeBase {
public:
    static const int PG_ID = 1051;
    static const int kParamNum = 6;
    ScalePipeline();
    virtual ~ScalePipeline();

    virtual int prepare();

    virtual int setParameters(const Parameters& param);
private:
    DISALLOW_COPY_AND_ASSIGN(ScalePipeline);

    virtual int prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                       vector<std::shared_ptr<CameraBuffer>>& dstBufs);

    virtual int setTerminalParams(const ia_css_frame_format_type* frame_format_types);

    virtual int setCropAndFormatInfo();

    virtual void setScaleMapInfo(const FrameInfoPortMap& outputInfos);

private:
    enum SCALE_TERMINAL_ID {
        YUYV_SCALE_TERMINAL_ID_CACHED_PARAMETER_IN = 0,
        YUYV_SCALE_TERMINAL_ID_PROGRAM_INIT,
        YUYV_SCALE_TERMINAL_ID_GET_YUYV,
        YUYV_SCALE_TERMINAL_ID_PUT_DISPLAY,
        YUYV_SCALE_TERMINAL_ID_PUT_MAIN,
        YUYV_SCALE_TERMINAL_ID_PUT_PP
    };
    enum SCALE_KERNEL_ID {
        ISP_BXT_OFA_MP = 0,
        ISP_BXT_OFA_DP,
        ISP_BXT_OFA_PPP,
        ISP_DMA_CROPPER_MP,
        ISP_DMA_CROPPER_DP,
        ISP_DMA_CROPPER_PPP,
        ISP_SC_OUTPUTSCALER_DP,
        ISP_SC_OUTPUTSCALER_PPP

    };
    ScalePortMap mScaleMap;
    ia_css_frame_format_type_t mScaleFrameFmtTypeList[YUYV_SCALE_TERMINAL_ID_PUT_PP + 1];
    ia_binary_data __attribute__ ((aligned (4096))) mParamPayload[kParamNum];
    camera_crop_region_t mCropRegion;
};

} //namespace icamera
