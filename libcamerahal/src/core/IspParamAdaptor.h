/*
 * Copyright (C) 2015-2018 Intel Corporation.
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

#include <vector>
#include <list>

#include "iutils/Errors.h"
#include "CameraBuffer.h"
#include "CameraTypes.h"
#include "V4l2Dev.h"

#ifndef BYPASS_MODE
extern "C" {
#include "ia_camera/ipu_process_group_wrapper.h"
#include "ia_css_isys_parameter_defs.h"
}

#include "ia_aiq_types.h"
#include "ia_isp_bxt_types.h"
#include "ia_isp_bxt_statistics_types.h"
#include "ia_isp_bxt.h"
#include "ia_bcomp.h"
#include "ia_bcomp_types.h"

#include "gc/IGraphConfigManager.h"
#include "IspSettings.h"
#endif  // BYPASS_MODE

namespace icamera {

enum PgParamType {
    PG_PARAM_VIDEO = 0,
    PG_PARAM_PSYS_ISA,
    PG_PARAM_ISYS,
    PG_PARAM_STILL_4k,
    PG_PARAM_STILL_8m
};

#ifdef BYPASS_MODE
// Dummy IspParamAdaptor for bypass mode.
class IspParamAdaptor {
public:
    IspParamAdaptor(int /*cameraId*/, PgParamType /*type*/) {}
    virtual ~IspParamAdaptor() {}

    int init() { return OK; }
    int deinit() { return OK; }
    int configure(const stream_t &stream, ConfigMode configMode, TuningMode tuningMode) { return OK; }
    int encodeIsaParams(const shared_ptr<CameraBuffer> &buf, EncodeBufferType type,
                        long settingSequence = -1) { return OK; }
    int getProcessGroupSize() { return 0; }
    int getInputPayloadSize() { return 0; }
    int getOutputPayloadSize() { return 0; }
    int decodeStatsData(TuningMode /*tuningMode*/,
                        shared_ptr<CameraBuffer> /*statsBuffer*/) { return OK; }
};

#else // BYPASS_MODE

/**
 * This class is for isp parameter converting including:
 * 1. Convert hw statistics to aiq statistics
 * 2. Convert aiq result to isa config
 * 3. Run isp config
 * 4. Provide p2p handle
 */
class IspParamAdaptor {
public:
    IspParamAdaptor(int cameraId, PgParamType type);
    virtual ~IspParamAdaptor();

    int init();
    int deinit();
    int configure(const stream_t &stream, ConfigMode configMode, TuningMode tuningMode);

    int getParameters(Parameters& param);

    int decodeStatsData(TuningMode tuningMode,
                        shared_ptr<CameraBuffer> statsBuffer,
                        shared_ptr<IGraphConfig> graphConfig = nullptr);

    int convertIsaRgbsStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                 ia_aiq_rgbs_grid** rgbsGrid);
    int convertIsaAfStatistics(const shared_ptr<CameraBuffer> &hwStats, ia_aiq_af_grid** afGrid);

    int convertHdrYvStatistics(const shared_ptr<CameraBuffer> &psysStats,
                               ia_isp_bxt_hdr_yv_grid_t** hdrYvGrid);
    int convertHdrRgbsStatistics(const shared_ptr<CameraBuffer> &hdrStats,
                                 const ia_aiq_ae_results &aeResults,
                                 const ia_aiq_color_channels &colorChannels,
                                 ia_aiq_rgbs_grid** rgbsGrid,
                                 ia_aiq_hdr_rgbs_grid** hdrRgbsGrid);
    int convertHdrAfStatistics(const shared_ptr<CameraBuffer> &hdrStats, ia_aiq_af_grid** afGrid);

    int convertPsaAfStatistics(const shared_ptr<CameraBuffer> &hwStats, ia_aiq_af_grid** afGrid);
    int convertPsaRgbsStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                 const ia_aiq_ae_results &aeResults,
                                 ia_aiq_rgbs_grid** rgbsGrid);
    int convertDvsStatistics(const shared_ptr<CameraBuffer> &hwStats, ia_dvs_statistics **dvsStats,
                             camera_resolution_t resolution);

    int queryStats(const shared_ptr<CameraBuffer> &hwStats, ia_isp_bxt_statistics_query_results_t* queryResults);

    int getProcessGroupSize();
    int getInputPayloadSize();
    int getOutputPayloadSize();
    std::vector<uint32_t> getEnabledKernelList() { return mEnabledKernelVec; }

    int encodeIsaParams(const shared_ptr<CameraBuffer> &buf,
                        EncodeBufferType type, long settingSequence = -1);
    int runIspAdapt(const IspSettings* ispSettings, long settingSequence = -1);
    //Get ISP param from mult-stream ISP param adaptation
    const ia_binary_data* getIpuParameter(long sequence = -1, int streamId = -1);

private:
    DISALLOW_COPY_AND_ASSIGN(IspParamAdaptor);

    int  queryMemoryReqs();
    int initProgramGroupForAllStreams(ConfigMode configMode);
    int postConfigure(int width, int height);
    void initInputParams(ia_isp_bxt_input_params_v2 *params, PgParamType type);

    int initIspAdaptHandle(ConfigMode configMode, TuningMode tuningMode);
    void deinitIspAdaptHandle();

    int runIspAdaptL(ia_isp_bxt_program_group programGroup,
                     const IspSettings* ispSettings, long settingSequence,
                     bool forceUpdate = false);

    //Allocate memory for mIspParameters
    int allocateIspParamBuffers();
    //Release memory for mIspParameters
    void releaseIspParamBuffers();

    int decodeAndSaveAiqStats(TuningMode tuningMode, shared_ptr<CameraBuffer> statsBuffer);

    void getHdrExposureInfo(const ia_aiq_ae_results &aeResults,
                            ia_isp_hdr_exposure_info_t* hdrExposureInfo);
    // Dumping methods for debugging purposes.
    void dumpRgbsStats(ia_aiq_rgbs_grid *rgbs_grid, long sequence, unsigned int num = 1);
    void dumpIspParameter(long sequence);
    void dumpP2PContent(const shared_ptr<CameraBuffer> &buf,
                        ia_binary_data* pg, EncodeBufferType type);

    // Enable or disable kernels according to environment variables for debug purpose.
    void updateKernelToggles(ia_isp_bxt_program_group programGroup);
private:
    int mCameraId;
    PgParamType mPgParamType;
    TuningMode mTuningMode;

    ia_isp_bxt   *mIspAdaptHandle;
    ia_bcomp *mBCompHandle;
    ia_bcomp_results *mBCompResults;

    camera_resolution_t mDvsresolution;

    enum IspAdaptorState {
        ISP_ADAPTOR_NOT_INIT,
        ISP_ADAPTOR_INIT,
        ISP_ADAPTOR_CONFIGURED
    } mIspAdaptorState;

    //Guard for IspParamAdaptor public API
    Mutex mIspAdaptorLock;

    ipu_pg_die_t  mP2PWrapper;
    map<int, ia_isp_bxt_program_group> mStreamIdToProgramGroupMap;
    map<int, int> mStreamIdToPGOutSizeMap;
    ia_aiq_frame_params mFrameParam;
    ia_binary_data mCurrentIpuParam;   // current output from AIC

    static const int ISP_PARAM_QUEUE_SIZE = 10;
    int mCurIspParamIndex;
    struct IspParameter {
        long sequence; // frame sequence id
        map<int, ia_binary_data> streamIdToDataMap; // map from stream id to ia_binary_data
    } mIspParameters[ISP_PARAM_QUEUE_SIZE];

    // Process group memory requirements
    int    mProcessGroupSize;    // Size in bytes required for the Process group descriptor
    int    mInputTerminalsSize;  // Bytes required to store all the input terminal payloads
    int    mOutputTerminalsSize; // Bytes required to store all the output terminal payloads
    IGraphConfigManager *mGCM;

    struct TerminalPayloadDescriptor {
        int size;         // Size of the terminal payload
        int paddedSize;   // Size of the terminal payload plus padding to meet memory alignment requirements
        uint64_t offset;     // Offset from the base of the payload buffer to the start of the terminal payload
    };
    std::vector<TerminalPayloadDescriptor> mTerminalBuffers;
    std::vector<uint32_t> mEnabledKernelVec;
    std::list<long> mSequenceList;  // Store the sequence history in IspParamAdaptor
};
#endif  // BYPASS_MODE

} // namespace icamera
