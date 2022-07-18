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

#pragma once

#include "PSysPipeBase.h"
// INTEL_DVS_S
#include "IntelDvs.h"
// INTEL_DVS_E

namespace icamera {

/**
 * \class FisheyePipeline
 *
 * \brief As known as Fisheye correction
 */
class FisheyePipeline : public PSysPipeBase {
public:
    static const int PG_ID = 1053;
    static const int numKernels = 1;
    static const int kParamNum = 4;
    FisheyePipeline(int cameraId);
    virtual ~FisheyePipeline();

    virtual int prepare();
    virtual int iterate(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                vector<std::shared_ptr<CameraBuffer>>& dstBufs);
    int setParameters(const Parameters& param);
private:
    DISALLOW_COPY_AND_ASSIGN(FisheyePipeline);

    virtual int prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                       vector<std::shared_ptr<CameraBuffer>>& dstBufs);

    virtual int setTerminalParams(const ia_css_frame_format_type* frame_format_types);
    void setKernelConfig();
    int runDVS();
    void enableConcurrency(bool enable);
    void releaseConcurrency();

private:
    int mCameraId;
    // INTEL_DVS_S
    IntelDvs *mIntelDvs;
    // INTEL_DVS_E
    camera_fisheye_dewarping_mode_t mDewarpingMode;
    ia_isp_bxt_run_kernels_t mFisheyeKernels[numKernels];
    ia_isp_bxt_resolution_info_t mKernelResinfo[numKernels];
    enum FISHEYE_TERMINAL_ID {
        YUYV_LDC_TERMINAL_ID_CACHED_PARAMETER_IN = 0,
        YUYV_LDC_TERMINAL_ID_GET,
        YUYV_LDC_TERMINAL_ID_DVS_COORDS,
        YUYV_LDC_TERMINAL_ID_PUT_MAIN
    };
    enum SCALE_KERNEL_ID {
        ISP_GDC3 = 0,
    };
    enum DVS_RESULT_INDEX {
        FISHEYE_DVS_RESULT_REARVIEW = 0,
        FISHEYE_DVS_RESULT_HITCHVIEW,
        FISHEYE_DVS_RESULT_MAX,
    };
    DvsResult mDvsResInfo[FISHEYE_DVS_RESULT_MAX];
    ia_css_frame_format_type_t mFisheyeFrameFmtTypeList[YUYV_LDC_TERMINAL_ID_PUT_MAIN + 1];
    ia_binary_data __attribute__ ((aligned (4096))) mParamPayload[kParamNum];
    static FILE* mFwConcurFile;
    static Mutex mPipeMutex;
};

} //namespace icamera
