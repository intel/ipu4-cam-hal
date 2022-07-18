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

#define LOG_TAG "IspParamAdaptor"

#include <stdio.h>

#include "IspParamAdaptor.h"

#include "3a/AiqResult.h"
#include "3a/AiqResultStorage.h"
#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Errors.h"
#include "PlatformData.h"
#include "IGraphConfig.h"

#include "IspControl.h"
#include "isp_control/IspControlUtils.h"

#include "ia_tools/ia_macros.h"
#include "ia_pal_types_isp_ids_autogen.h"

namespace icamera {

IspParamAdaptor::IspParamAdaptor(int cameraId, PgParamType type) :
        mCameraId(cameraId),
        mPgParamType(type),
        mTuningMode(TUNING_MODE_VIDEO),
        mIspAdaptHandle(nullptr),
        mBCompHandle(nullptr),
        mBCompResults(nullptr),
        mIspAdaptorState(ISP_ADAPTOR_NOT_INIT),
        mP2PWrapper(nullptr),
        mCurIspParamIndex(-1),
        mProcessGroupSize(0),
        mInputTerminalsSize(0),
        mOutputTerminalsSize(0),
        mGCM(nullptr)
{
    LOG1("IspParamAdaptor was created for id:%d type:%d", mCameraId, mPgParamType);
    CLEAR(mFrameParam);
    CLEAR(mCurrentIpuParam);
    CLEAR(mDvsresolution);
    if (PlatformData::getGraphConfigNodes(cameraId)) {
        mGCM = IGraphConfigManager::getInstance(cameraId);
    }
}

IspParamAdaptor::~IspParamAdaptor()
{
    LOG1("IspParamAdaptor was created for id:%d type:%d", mCameraId, mPgParamType);
}

int IspParamAdaptor::init()
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    AutoMutex l(mIspAdaptorLock);

    mIspAdaptorState = ISP_ADAPTOR_INIT;
    return OK;
}

int IspParamAdaptor::deinit()
{
    LOG1("ISP HW param adaptor de-initialized for camera id:%d type:%d", mCameraId, mPgParamType);
    AutoMutex l(mIspAdaptorLock);

    if (mP2PWrapper) {
        ipu_pg_die_destroy(mP2PWrapper);
        mP2PWrapper = nullptr;
    }

    deinitIspAdaptHandle();

    //Release the memory and clear the mapping
    for (auto& pgMap: mStreamIdToProgramGroupMap) {
        delete[] pgMap.second.run_kernels;
    }
    mStreamIdToProgramGroupMap.clear();
    mStreamIdToPGOutSizeMap.clear();
    releaseIspParamBuffers();

    CLEAR(mFrameParam);
    CLEAR(mCurrentIpuParam);

    mIspAdaptorState = ISP_ADAPTOR_NOT_INIT;
    return OK;
}

int IspParamAdaptor::initIspAdaptHandle(ConfigMode configMode, TuningMode tuningMode)
{
    int ret = OK;

    if (!PlatformData::isEnableAIQ(mCameraId)) {
        return ret;
    }

    ia_binary_data ispData;
    ia_cmc_t *cmcData = nullptr;

    CpfStore* cpf = PlatformData::getCpfStore(mCameraId);
    Check((cpf == nullptr), NO_INIT, "@%s, No CPF for cameraId:%d", __func__, mCameraId);
    ret = cpf->getDataAndCmc(&ispData, nullptr, nullptr, &cmcData, tuningMode);
    Check(ret != OK, NO_INIT, "get cpf and cmc data failed");

    int statsNum = PlatformData::getExposureNum(mCameraId,
                                                CameraUtils::isHdrPsysPipe(tuningMode));
    mIspAdaptHandle = ia_isp_bxt_init(&ispData, cmcData,
                                      MAX_STATISTICS_WIDTH, MAX_STATISTICS_HEIGHT,
                                      statsNum,
                                      nullptr);
    Check(!mIspAdaptHandle, NO_INIT, "ISP adaptor failed to initialize");

    if (PlatformData::isDolShortEnabled(mCameraId) ||
        PlatformData::isDolMediumEnabled(mCameraId)) {
        ia_bcomp_dol_mode_t dol_mode = ia_bcomp_non_dol;
        float conversion_gain_ratio = 1.0;

        // Parse the DOL mode and CG ratio from sensor mode config
        shared_ptr<IGraphConfig> graphConfig = mGCM->getGraphConfig(configMode);
        if (graphConfig != nullptr) {
            // TODO: libiacss should return mode as enum instead of string.
            //       Now use dol_mode_name and convert it to enum here.
            //       Remove it after libiacss updates the API.
            string dol_mode_name;
            graphConfig->getDolInfo(conversion_gain_ratio, dol_mode_name);
            map<string, ia_bcomp_dol_mode_t> dolModeNameMap;
            dolModeNameMap["DOL_MODE_2_3_FRAME"] = ia_bcomp_dol_two_or_three_frame;
            dolModeNameMap["DOL_MODE_DCG"] = ia_bcomp_dol_dcg;
            dolModeNameMap["DOL_MODE_COMBINED_VERY_SHORT"] = ia_bcomp_dol_combined_very_short;
            dolModeNameMap["DOL_MODE_DCG_VERY_SHORT"] = ia_bcomp_dol_dcg_very_short;
            if (dolModeNameMap.count(dol_mode_name)) {
                dol_mode = dolModeNameMap[dol_mode_name];
            }
        }
        LOG1("conversion_gain_ratio %f, dol_mode %d", conversion_gain_ratio, dol_mode);

        mBCompHandle = ia_bcomp_init(cmcData, dol_mode, conversion_gain_ratio);
        Check(!mBCompHandle, NO_INIT, "Bcomp failed to initialize");
    }

    /*
     * The number of streamId is identified in configure stream,
     * fill the mStreamIdToProgramGroupMap and allocate the IspParameter memory
     */
    if (mGCM != nullptr && mGCM->isGcConfigured()) {
        ret = initProgramGroupForAllStreams(configMode);
        Check(ret != OK, ret, "%s, Failed to init programGroup for all streams", __func__);
        ret = allocateIspParamBuffers();
        Check(ret != OK, ret, "%s, Failed to allocate isp parameter buffers", __func__);
    }

    LOG1("ISP HW param adaptor initialized successfully camera id:%d", mCameraId);

    return ret;
}

void IspParamAdaptor::deinitIspAdaptHandle()
{
    if (mIspAdaptHandle) {
        ia_isp_bxt_deinit(mIspAdaptHandle);
        mIspAdaptHandle = nullptr;
    }

    if (mBCompHandle) {
        ia_bcomp_deinit(mBCompHandle);
        mBCompHandle = nullptr;
    }
}

int IspParamAdaptor::initProgramGroupForAllStreams(ConfigMode configMode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    int kernelsBufferSize = 0;
    std::vector<int32_t> streamIds;

    //Release the memory and clear the mapping
    for (auto& pgMap: mStreamIdToProgramGroupMap) {
        delete[] pgMap.second.run_kernels;
    }
    mStreamIdToProgramGroupMap.clear();
    mStreamIdToPGOutSizeMap.clear();

    shared_ptr<IGraphConfig> graphConfig = mGCM->getGraphConfig(configMode);
    if(graphConfig == nullptr) {
        LOGW("There isn't GraphConfig for camera configMode: %d", configMode);
        return UNKNOWN_ERROR;
    }

    if (mPgParamType == PG_PARAM_ISYS) {
        int streamId = 0; // 0 is for PG_PARAM_ISYS
        streamIds.push_back(streamId);
    } else {
        status_t ret = graphConfig->graphGetStreamIds(streamIds);
        Check(ret != OK, UNKNOWN_ERROR, "Failed to get the PG streamIds");
    }

    for (auto id : streamIds) {
        ia_isp_bxt_program_group *pgPtr = graphConfig->getProgramGroup(id);
        if (pgPtr != nullptr) {
            ia_isp_bxt_program_group programGroup;
            CLEAR(programGroup);
            programGroup.kernel_count = pgPtr->kernel_count;
            kernelsBufferSize = pgPtr->kernel_count * sizeof(ia_isp_bxt_run_kernels_t);
            programGroup.run_kernels = new ia_isp_bxt_run_kernels_t[pgPtr->kernel_count];
            MEMCPY_S(programGroup.run_kernels, kernelsBufferSize,
                     pgPtr->run_kernels, kernelsBufferSize);

            // Override the stream id in kernel list with the one in sensor's config file.
            // Remove this after the sensor's tuning file uses correct stream id.
            int streamId = PlatformData::getStreamIdByConfigMode(mCameraId, configMode);
            if (streamId != -1) {
                programGroup.run_kernels->stream_id = streamId;
            }

            mStreamIdToProgramGroupMap[id] = programGroup;
            mStreamIdToPGOutSizeMap[id] = ia_isp_bxt_get_output_size(&programGroup);
        }
    }

    return OK;
}

void IspParamAdaptor::initInputParams(ia_isp_bxt_input_params_v2 *params, PgParamType type)
{
    Check(params == nullptr, VOID_VALUE, "NULL input parameter");

    if (type == PG_PARAM_PSYS_ISA) {
        params->ee_setting.feature_level = ia_isp_feature_level_low;
        params->ee_setting.strength = 0;
        LOG2("%s: set initial default edge enhancement setting: level: %d, strengh: %d",
            __func__, params->ee_setting.feature_level, params->ee_setting.strength);

        params->nr_setting.feature_level = ia_isp_feature_level_high;
        params->nr_setting.strength = 0;
        LOG2("%s: set initial default noise setting: level: %d, strengh: %d",
            __func__, params->nr_setting.feature_level, params->nr_setting.strength);
    }
}

int IspParamAdaptor::postConfigure(int width, int height)
{
    // The PG wrapper init is done by the imaging controller.
    if(mPgParamType == PG_PARAM_PSYS_ISA) {
        mIspAdaptorState = ISP_ADAPTOR_CONFIGURED;
        return OK; //No need to do anything for P2P. It id done by libiacss
    }

    //Init P2P wrapper for ISYS ISA
    uint8_t fragmentCount = 1;
    ipu_pg_die_fragment_desc_t fragmentDesc;
    CLEAR(fragmentDesc);
    fragmentDesc.fragment_width = width;
    fragmentDesc.fragment_height = height;
    LOG1("@%s, fragment_width:%d, fragment_height:%d", __func__,
         fragmentDesc.fragment_width, fragmentDesc.fragment_height);

    if (mP2PWrapper) {
        ipu_pg_die_destroy(mP2PWrapper);
    }

    // Calculate the memory requirements for the PG descriptor and payloads
    mP2PWrapper = ipu_pg_die_init(&mCurrentIpuParam, 0 /* ISYS PG specification */,
                                  fragmentCount, &fragmentDesc);
    Check(!mP2PWrapper, NO_INIT, "P2P wrapper failed to initialize");

    // Retrieve PG information
    int status = queryMemoryReqs();
    Check(status != OK, NO_INIT, "Failed to query the memory requirements for the Process Group ret=%d", status);

    mIspAdaptorState = ISP_ADAPTOR_CONFIGURED;
    return OK;
}


/**
 * configure
 *
 * (graph config version)
 * This is the method used when the spatial parameters change, usually during
 * stream configuration.
 *
 * We initialize the ISP adaptor to produce worst case scenario for memory
 * allocation.
 *
 * At this state we initialize the wrapper code that helps encoding the PG
 * descriptor and terminal payloads (i.e. the parameters for the PG).
 *
 * \param configMode[IN]: The real configure mode.
 * \param tuningMode[IN]:  The tuning mode.
 * \param stream[IN]: frame info.
 * \return OK: everything went ok.
 * \return UNKNOWN_ERROR: First run of ISP adaptation failed.
 * \return NO_INIT: Initialization of P2P or PG_DIE wrapper failed.
 */
int IspParamAdaptor::configure(const stream_t &stream,
        ConfigMode configMode, TuningMode tuningMode)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    AutoMutex l(mIspAdaptorLock);

    mTuningMode = tuningMode;

    ia_isp_bxt_input_params_v2 inputParams;
    CLEAR(inputParams);
    ia_aiq_sa_results_v1 fakeSaResults;
    CLEAR(fakeSaResults);

    deinitIspAdaptHandle();
    int ret = initIspAdaptHandle(configMode, tuningMode);
    Check(ret != OK, ret, "%s, init Isp Adapt Handle failed %d", __func__, ret);

    SensorFrameParams param;
    int status = PlatformData::calculateFrameParams(mCameraId, param);
    Check(status != OK, status, "%s: Failed to calculate frame params", __func__);
    AiqUtils::convertToAiqFrameParam(param, mFrameParam);

    LOG1("horizontal_crop_offset:%d", mFrameParam.horizontal_crop_offset);
    LOG1("vertical_crop_offset:%d", mFrameParam.vertical_crop_offset);
    LOG1("cropped_image_width:%d", mFrameParam.cropped_image_width);
    LOG1("cropped_image_height:%d", mFrameParam.cropped_image_height);
    LOG1("horizontal_scaling_numerator:%d", mFrameParam.horizontal_scaling_numerator);
    LOG1("horizontal_scaling_denominator:%d", mFrameParam.horizontal_scaling_denominator);
    LOG1("vertical_scaling_numerator:%d", mFrameParam.vertical_scaling_numerator);
    LOG1("vertical_scaling_denominator:%d", mFrameParam.vertical_scaling_denominator);

    /*
     * Construct the dummy Shading Adaptor  results to force the creation of
     * the LSC table.
     * Assign them to the AIC input parameter structure.
     */
    unsigned short fakeLscTable[4] = {1,1,1,1};
    for (int i = 0; i < MAX_BAYER_ORDER_NUM; i++) {
        for (int j = 0; j < MAX_BAYER_ORDER_NUM; j++) {
            fakeSaResults.lsc_grid[i][j] = fakeLscTable;
        }
    }
    fakeSaResults.fraction_bits = 0;
    fakeSaResults.color_order = cmc_bayer_order_grbg;
    fakeSaResults.lsc_update = true;
    fakeSaResults.width = 2;
    fakeSaResults.height = 2;
    inputParams.sa_results = &fakeSaResults;

    initInputParams(&inputParams, mPgParamType);

    /*
     *  IA_ISP_BXT can run without 3A results to produce the defaults for a
     *  given sensor configuration.
     *  TODO: change the mCurrentIpuParam in the future.
     */
    mCurIspParamIndex = 0;
    IspParameter& ipuParam = mIspParameters[mCurIspParamIndex];

    ipuParam.sequence = -1;
    for (auto& binaryMap : ipuParam.streamIdToDataMap) {
        inputParams.program_group = &(mStreamIdToProgramGroupMap[binaryMap.first]);
        inputParams.sensor_frame_params = &mFrameParam;
        mCurrentIpuParam = binaryMap.second;
        mCurrentIpuParam.size = mStreamIdToPGOutSizeMap[binaryMap.first];

        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_isp_bxt_run", 1);
#ifndef ENABLE_PAC
        ia_err err = ia_isp_bxt_run_v2(mIspAdaptHandle, &inputParams, &mCurrentIpuParam);
        Check(err != ia_err_none, UNKNOWN_ERROR, "ISP parameter adaptation has failed %d", err);
#endif
        binaryMap.second.size = mCurrentIpuParam.size;
    }

    dumpIspParameter(0);

    return postConfigure(stream.width, stream.height);
}

int IspParamAdaptor::getParameters(Parameters& param)
{
    AutoMutex l(mIspAdaptorLock);

    // Fill the ISP control related data.
    vector<uint32_t> controls = PlatformData::getSupportedIspControlFeatures(mCameraId);
    for (auto ctrlId : controls) {
        size_t size = 0;
        char* data = nullptr;
        ia_err err = ia_isp_bxt_get_interpolated_parameters(mIspAdaptHandle, ctrlId, &data, &size);
        if (err != ia_err_none) continue;

        LOG1("Fill ISP control data for: %s", IspControlUtils::getNameById(ctrlId));
        param.setIspControl(ctrlId, data);
    }

    camera_control_isp_ctrl_id ccmCtrlId = camera_control_isp_ctrl_id_color_correction_matrix;
    int ret = param.getIspControl(ccmCtrlId, nullptr);
    if (ret != OK && PlatformData::isIspControlFeatureSupported(mCameraId, ccmCtrlId)) {
        // CCM data should be filled if it's supported.
        // Here we use ACM data to fill it.
        camera_control_isp_advanced_color_correction_matrix_t acm;
        ret = param.getIspControl(camera_control_isp_ctrl_id_advanced_color_correction_matrix, &acm);
        if (ret == OK) {
            param.setIspControl(ccmCtrlId, acm.ccm_matrices);
        }
    }

    return OK;
}

int IspParamAdaptor::getProcessGroupSize()
{
    LOG1("%s process group size is: %d", __func__, mProcessGroupSize);
    AutoMutex l(mIspAdaptorLock);
    return mProcessGroupSize;
}

int IspParamAdaptor::getInputPayloadSize()
{
    LOG1("%s input payload size is: %d", __func__, mInputTerminalsSize);
    AutoMutex l(mIspAdaptorLock);
    return mInputTerminalsSize;
}

int IspParamAdaptor::getOutputPayloadSize()
{
    LOG1("%s output payload size is: %d", __func__, mOutputTerminalsSize);
    AutoMutex l(mIspAdaptorLock);
    return mOutputTerminalsSize;
}

/**
 * queryMemoryReqs
 *
 * Private method used during config stream phase where we query the program
 * group wrapper about the memory needs for the process group and the terminals.
 * This information is provided to the client so that it allocates enough memory
 * to encode the process group descriptor and terminal payloads.
 *
 * [IN] The program group wrapper is initialized in the member
 *      variable mP2PWrapper.
 * [OUT] The terminal payload sizes are stored in one vector that is a member of
 *       the class: mTerminalBuffers. The vector contains structures of type
 *       TerminalPayloadDescriptor.
 * [OUT] The accumulated input and output terminal sizes are stored in other
 *        2 member variables: mInputTerminalsSize and mOutputTerminalsSize
 * [OUT] The memory needs for the process group descriptor are stored in a
 *       member variable: mProcessGroupSize
 * [OUT] Finally the total number of terminals count is cached to a member
 *       variable: mTerminalCount
 */
int IspParamAdaptor::queryMemoryReqs()
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    mProcessGroupSize = ipu_pg_die_sizeof_process_group(mP2PWrapper);
    int terminalCount = ipu_pg_die_get_number_of_terminals(mP2PWrapper);
    LOG1("%s process group size:%d, terminal count:%d", __func__, mProcessGroupSize, terminalCount);
    Check(terminalCount == 0, NO_INIT, "Program group does not have any registered terminals");

    mTerminalBuffers.clear();
    mInputTerminalsSize = 0;
    mOutputTerminalsSize = 0;
    TerminalPayloadDescriptor terminalDesc;

    for (int termIdx = 0; termIdx < terminalCount; termIdx++) {
        terminalDesc.size = ipu_pg_die_sizeof_terminal_payload(mP2PWrapper, termIdx);
        // align terminal payload size to PAGE boundaries (4K) which is required by the driver.
        terminalDesc.paddedSize = PAGE_ALIGN(terminalDesc.size);
        bool isInputTerminal = ipu_pg_die_is_input_terminal(mP2PWrapper, termIdx);
        if (isInputTerminal) {
            terminalDesc.offset = mInputTerminalsSize;
            mInputTerminalsSize += terminalDesc.paddedSize;
        } else {
            terminalDesc.offset = mOutputTerminalsSize;
            mOutputTerminalsSize += terminalDesc.paddedSize;
        }
        LOG1("PG Terminal [%d] size %d padded Size %d offset %lx isInput=%d",
                termIdx, terminalDesc.size, terminalDesc.paddedSize, terminalDesc.offset, isInputTerminal);
        mTerminalBuffers.push_back(terminalDesc);
    }

    return OK;
}

void IspParamAdaptor::getHdrExposureInfo(const ia_aiq_ae_results &aeResults,
                                         ia_isp_hdr_exposure_info_t* hdrExposureInfo)
{
    Check(hdrExposureInfo == nullptr, VOID_VALUE, "NULL hdrExposureInfo pointer");

    ia_isp_stat_split_thresh statSplitThresh[] = {{0, 32767, 1}, {0, 2047, 1}};
    int numStatSplitThresh = sizeof(statSplitThresh) / sizeof(ia_isp_stat_split_thresh);

    hdrExposureInfo->num_exposures = numStatSplitThresh;
    hdrExposureInfo->hdr_gain = 1.0f;

    hdrExposureInfo->thresholds[numStatSplitThresh - 1]  = statSplitThresh[numStatSplitThresh - 1];
    for (int i = 0; i < numStatSplitThresh - 1; i++)
    {
        hdrExposureInfo->thresholds[i]  = statSplitThresh[i];
        hdrExposureInfo->exposure_ratios[i] = (float)hdrExposureInfo->thresholds[i].high /
                                             (float)hdrExposureInfo->thresholds[numStatSplitThresh - 1].high;
    }
}

int IspParamAdaptor::decodeStatsData(TuningMode tuningMode,
                                     shared_ptr<CameraBuffer> statsBuffer,
                                     shared_ptr<IGraphConfig> graphConfig)
{
    if (statsBuffer->getUsage() == BUFFER_USAGE_ISYS_STATS) {
        return decodeAndSaveAiqStats(tuningMode, statsBuffer);
    }

    ia_isp_bxt_statistics_query_results_t queryResults;
    CLEAR(queryResults);
    int ret = queryStats(statsBuffer, &queryResults);
    Check(ret != OK, ret, "Query statistics fail");

    AiqResultStorage *aiqResultStorage = AiqResultStorage::getInstance(mCameraId);

    // Decode DVS statistics
    if (queryResults.dvs_stats) {
        //update resolution infomation
        CheckWarning(graphConfig == nullptr, BAD_VALUE, "Null graph config");
        if((mDvsresolution.width == 0) && (mDvsresolution.height == 0)) {
            uint32_t gdcKernelId;
            ret = graphConfig->getGdcKernelSetting(gdcKernelId, mDvsresolution);
        }
        if (ret == OK) {
            ia_dvs_statistics *dvsStats = nullptr;
            convertDvsStatistics(statsBuffer, &dvsStats, mDvsresolution);

            DvsStatistics dvsStatsStorage(dvsStats, statsBuffer->getSequence());
            aiqResultStorage->updateDvsStatistics(dvsStatsStorage);
        } else {
            LOGW("Failed to get GDC kernel setting, DVS stats not decoded");
        }
    }

    // Decode LTM statistics
    if (queryResults.yv_grids_hdr) {
        ia_isp_bxt_hdr_yv_grid_t *hdrYvGrid;
        convertHdrYvStatistics(statsBuffer, &hdrYvGrid);

        LtmStatistics ltmStatsStorage(hdrYvGrid, statsBuffer->getSequence());
        aiqResultStorage->updateLtmStatistics(ltmStatsStorage);
    }

    // Decode and save RGBS and AF grids
    if (queryResults.rgbs_grid && queryResults.af_grid) {
        ret = decodeAndSaveAiqStats(tuningMode, statsBuffer);
        Check(ret != OK, ret, "Decode aiq statistics fail.");
    }

    return OK;
}

int IspParamAdaptor::decodeAndSaveAiqStats(TuningMode tuningMode,
                                           shared_ptr<CameraBuffer> statsBuffer)
{
    int exposureNum = PlatformData::getExposureNum(mCameraId, false);
    long sequence = statsBuffer->getSequence();

    ia_aiq_rgbs_grid *rgbs_grid[MAX_EXPOSURES_NUM] = {nullptr};
    ia_aiq_hdr_rgbs_grid *hdrRgbsGrid = nullptr;
    ia_aiq_af_grid *af_grid = nullptr;

    AiqResultStorage *aiqResultStorage = AiqResultStorage::getInstance(mCameraId);

    int ret = BAD_VALUE;
    const AiqResult *feedback = aiqResultStorage->getAiqResult(sequence);
    if (feedback == nullptr) {
        LOGW("No aiq result of sequence %ld! Use the latest instead", sequence);
        feedback = aiqResultStorage->getAiqResult();
    }

    if (CameraUtils::isHdrPsysPipe(tuningMode)
            && statsBuffer->getUsage() == BUFFER_USAGE_PSYS_STATS) {
        exposureNum = PlatformData::getExposureNum(mCameraId, true);

        ret = convertHdrRgbsStatistics(statsBuffer, feedback->mAeResults,
                                       feedback->mPaResults.color_gains, rgbs_grid, &hdrRgbsGrid);
        ret |= convertHdrAfStatistics(statsBuffer, &af_grid);
    } else if (statsBuffer->getUsage() == BUFFER_USAGE_PSYS_STATS) {
        ret = convertPsaRgbsStatistics(statsBuffer, feedback->mAeResults, rgbs_grid);
        ret |= convertPsaAfStatistics(statsBuffer, &af_grid);
    } else if (statsBuffer->getUsage() == BUFFER_USAGE_ISYS_STATS) {
        ret = convertIsaRgbsStatistics(statsBuffer, rgbs_grid);
        ret |= convertIsaAfStatistics(statsBuffer, &af_grid);
    }
    Check(ret != OK, ret, "Fail to convert rgbs and af statistics: %d", ret);

    AiqStatistics *aiqStatistics = aiqResultStorage->acquireAiqStatistics();

    if (hdrRgbsGrid) {
        aiqStatistics->saveHdrRgbsGridData(hdrRgbsGrid);
    }
    aiqStatistics->saveRgbsGridData(rgbs_grid, exposureNum);
    aiqStatistics->saveAfGridData(af_grid);

    aiqStatistics->mSequence = sequence;
    aiqStatistics->mTimestamp = TIMEVAL2USECS(statsBuffer->getTimestamp());
    aiqStatistics->mTuningMode = tuningMode;

    aiqResultStorage->updateAiqStatistics(sequence);
    return OK;
}

int IspParamAdaptor::convertHdrYvStatistics(const shared_ptr<CameraBuffer> &psysStats,
                                            ia_isp_bxt_hdr_yv_grid_t** hdrYvGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    ia_isp_bxt_statistics_query_results_t queryResults;
    CLEAR(queryResults);
    ia_binary_data *psysStatsData = (ia_binary_data *)(psysStats->getBufferAddr());
    ia_err status = ia_isp_bxt_statistics_query(mIspAdaptHandle, psysStatsData, &queryResults);
    CheckWarning(status != ia_err_none, UNKNOWN_ERROR, "Failed to query hdr yv stats: %d", status);

    LOG2("%s: af_grid: %d, histograms: %d, rgbs_grid: %d, rgbs_grids_hdr: %d, rgby_grids_hdr: %d, yv_grids_hdr: %d",
        __func__, queryResults.af_grid, queryResults.histograms, queryResults.rgbs_grid,
        queryResults.rgbs_grids_hdr, queryResults.rgby_grids_hdr, queryResults.yv_grids_hdr);

    CheckWarning(!queryResults.yv_grids_hdr, BAD_VALUE, "No hdr yv stats in psys stats: BAD_VALUE");

    status = ia_isp_bxt_statistics_get_hdr_yv_in_binary(psysStatsData, hdrYvGrid);

    ia_isp_bxt_hdr_yv_grid_t *grid = *hdrYvGrid;
    CheckWarning(status != ia_err_none || grid == nullptr, status, "Failed to get hdr yv stats: %d", status);

    LOG3A("%s: hdrYvGrid width %d, height %d, starting data: v_max: %d, y_avg: %d", __func__,
        grid->grid_width, grid->grid_height, grid->v_max[0], grid->y_avg[0]);

    if (CameraDump::isDumpTypeEnable(DUMP_PSYS_AIQ_STAT) && grid->grid_height != 0 && grid->grid_width != 0) {
        BinParam_t bParam;
        bParam.bType = BIN_TYPE_STATISTIC;
        bParam.mType = M_PSYS;
        bParam.sequence         = psysStats->getSequence();
        bParam.sParam.gridWidth = grid->grid_width;
        bParam.sParam.gridHeight= grid->grid_height;
        bParam.sParam.appendix  = "HdrYv-v_max";
        CameraDump::dumpBinary(mCameraId, grid->v_max, grid->grid_height * grid->grid_width * sizeof(grid->v_max[0]), &bParam);
        bParam.sParam.appendix  = "HdrYv-y_avg";
        CameraDump::dumpBinary(mCameraId, grid->y_avg, grid->grid_height * grid->grid_width * sizeof(grid->y_avg[0]), &bParam);
    }
    return OK;
}

int IspParamAdaptor::convertIsaRgbsStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                              ia_aiq_rgbs_grid** rgbsGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    // Variables for P2P wrapper
    ia_binary_data awbStats;
    ia_binary_data terminalPayload;
    ia_binary_data processGroup;
    CLEAR(awbStats);
    CLEAR(terminalPayload);
    CLEAR(processGroup);

    int planeIndex = 0;
    processGroup.data = hwStats->getBufferAddr(planeIndex);
    processGroup.size =  hwStats->getBufferSize(planeIndex);
    planeIndex = 1;
    char *payloadBase =  (char*)hwStats->getBufferAddr(planeIndex);

    // Decode AWB statistics using P2P, first get the terminal ID and then decode
    int terminal = 0;
    css_err_t err = ipu_pg_die_get_terminal_by_uid(mP2PWrapper, IA_CSS_ISYS_KERNEL_ID_3A_STAT_AWB, &terminal);
    CheckWarning(err != css_err_none, UNKNOWN_ERROR, "Failed to get AWB terminal: %d", err);

    terminalPayload.data = payloadBase + mTerminalBuffers[terminal].offset;
    terminalPayload.size = mTerminalBuffers[terminal].size;

    err = ipu_pg_die_decode_terminal_payload(mP2PWrapper, &processGroup, terminal,
                                             &terminalPayload, &awbStats);
    CheckWarning(err != css_err_none, UNKNOWN_ERROR, "Failed to decode AWB terminal payload %d", err);
    // Below API params list: ia_isp_bxt, statistics, ir_weight(for RGB-IR sensor),
    // ae_result(for 2DP-SVE), bcomp_results(for compressed stats), out_rgbs_grid, out_ir_grid
    ia_err iaErr = ia_isp_bxt_statistics_convert_awb_from_binary_v3(mIspAdaptHandle, &awbStats,
                                                                    nullptr, nullptr, nullptr,
                                                                    rgbsGrid, nullptr);
    CheckWarning(iaErr != ia_err_none, UNKNOWN_ERROR, "Failed to convert AWB statistics %d", iaErr);

    ia_aiq_rgbs_grid* rgbs = (*rgbsGrid);
    CheckWarning(!rgbs, UNKNOWN_ERROR, "Failed to convert AWB statistics");

    CheckWarning(rgbs->grid_width > MAX_STATISTICS_WIDTH || rgbs->grid_height > MAX_STATISTICS_HEIGHT,
            BAD_VALUE, " ISA rgbs buffer maybe too small %dx%d", rgbs->grid_width, rgbs->grid_height);

    dumpRgbsStats(rgbs, hwStats->getSequence());

    return OK;
}

int IspParamAdaptor::convertHdrRgbsStatistics(const shared_ptr<CameraBuffer> &hdrStats,
                                              const ia_aiq_ae_results &aeResults,
                                              const ia_aiq_color_channels &colorChannels,
                                              ia_aiq_rgbs_grid** rgbsGrid,
                                              ia_aiq_hdr_rgbs_grid** hdrRgbsGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    ia_binary_data *hdrStatsData = (ia_binary_data *)(hdrStats->getBufferAddr());
    if (CameraDump::isDumpTypeEnable(DUMP_PSYS_DECODED_STAT)) {
        BinParam_t bParam;
        bParam.bType = BIN_TYPE_GENERAL;
        bParam.mType = M_PSYS;
        bParam.sequence = hdrStats->getSequence();
        bParam.gParam.appendix = "hdr_p2p_decoded_stats";
        CameraDump::dumpBinary(mCameraId, hdrStatsData->data, hdrStatsData->size, &bParam);
    }

    ia_isp_bxt_statistics_query_results_t queryResults;
    CLEAR(queryResults);
    ia_err err = ia_isp_bxt_statistics_query(mIspAdaptHandle, hdrStatsData, &queryResults);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to query HDR RGBS stats: %d", err);
    CheckWarning(!queryResults.rgbs_grids_hdr, UNKNOWN_ERROR, "No HDR RGBS stats found: %d", err);

    ia_isp_hdr_exposure_info_t hdrExposureInfo;
    CLEAR(hdrExposureInfo);
    ia_aiq_rgbs_grid* tmpRgbsGridPtr[MAX_EXPOSURES_NUM] = { nullptr };

    ia_isp_bxt_hdr_compression_t* pHdrCompression = nullptr;
    ia_isp_bxt_hdr_compression_t hdrCompression;
    CLEAR(hdrCompression);

    if (PlatformData::isUseFixedHDRExposureInfo(mCameraId)) {
        // Below logic with CCA V1 API is for legacy sensor only.
        getHdrExposureInfo(aeResults, &hdrExposureInfo);

        for (unsigned int i = 0; i < hdrExposureInfo.num_exposures; i++) {
            LOG3A("hdr expo info: num exposures %d, hdr gain %f, thresholds%u[%f-%f], ratios%u[%f]",
                    hdrExposureInfo.num_exposures, hdrExposureInfo.hdr_gain,
                    i, hdrExposureInfo.thresholds[i].high, hdrExposureInfo.thresholds[i].low,
                    i,hdrExposureInfo.exposure_ratios[i]);
        }

        float digitalGain = 1.0f;
        if (aeResults.exposures[0].exposure->digital_gain > 1) {
           digitalGain = aeResults.exposures[0].exposure->digital_gain;
        }

        err = ia_isp_bxt_statistics_convert_awb_hdr_from_binary_v1(mIspAdaptHandle,
                                hdrStatsData,
                                &hdrExposureInfo,
                                pHdrCompression,
                                0,
                                0,
                                colorChannels.r / digitalGain,
                                (colorChannels.gr+colorChannels.gb) / (2 * digitalGain),
                                colorChannels.b / digitalGain,
                                (ia_aiq_rgbs_grid **)&tmpRgbsGridPtr,
                                hdrRgbsGrid);
    } else {
        pHdrCompression = &hdrCompression;
        hdrCompression.bpp_info.input_bpp =
            PlatformData::getHDRStatsInputBitDepth(mCameraId);
        hdrCompression.bpp_info.output_bpp =
            PlatformData::getHDRStatsOutputBitDepth(mCameraId);
        hdrCompression.y_compression_method = ia_isp_bxt_hdr_y_decompression_max_rgb;

        err = ia_isp_bxt_statistics_convert_awb_hdr_from_binary_v2(mIspAdaptHandle,
                                hdrStatsData,
                                &aeResults,
                                /* only decompress when input and output bpps are different */
                                (hdrCompression.bpp_info.input_bpp != hdrCompression.bpp_info.output_bpp) ?
                                    pHdrCompression : nullptr,
                                0,
                                0,
                                colorChannels.r,
                                (colorChannels.gr+colorChannels.gb) / 2,
                                colorChannels.b,
                                (ia_aiq_rgbs_grid **)&tmpRgbsGridPtr,
                                hdrRgbsGrid);
    }

    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to convert HDR AWB statistics %d", err);

    for (unsigned int i = 0; i < aeResults.num_exposures; i++) {
        rgbsGrid[i] = tmpRgbsGridPtr[i];
    }

    CheckWarning(!tmpRgbsGridPtr[0], UNKNOWN_ERROR, "Failed to convert HDR AWB statistics. NULL value");

    ia_aiq_rgbs_grid rgbs[MAX_EXPOSURES_NUM];
    for (unsigned int i = 0; i < aeResults.num_exposures; i++) {
        rgbs[i] = *rgbsGrid[i];
    }

    CheckWarning(rgbs[0].grid_width > MAX_STATISTICS_WIDTH || rgbs[0].grid_height > MAX_STATISTICS_HEIGHT,
            BAD_VALUE, " HDR rgbs buffer maybe too small %dx%d", rgbs[0].grid_width, rgbs[0].grid_height);

    dumpRgbsStats(rgbs, hdrStats->getSequence(), aeResults.num_exposures);

    return OK;
}

int IspParamAdaptor::convertIsaAfStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                            ia_aiq_af_grid** afGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    // Variables for P2P wrapper
    ia_binary_data afStats;
    ia_binary_data terminalPayload;
    ia_binary_data processGroup;
    CLEAR(afStats);
    CLEAR(terminalPayload);
    CLEAR(processGroup);

    int planeIndex = 0;
    processGroup.data = hwStats->getBufferAddr(planeIndex);
    processGroup.size =  hwStats->getBufferSize(planeIndex);
    planeIndex = 1;
    char *payloadBase =  (char*)hwStats->getBufferAddr(planeIndex);

    // Decode AF statistics using P2P, first get the terminal ID and then decode
    int terminal = 0;
    css_err_t err = ipu_pg_die_get_terminal_by_uid(mP2PWrapper, IA_CSS_ISYS_KERNEL_ID_3A_STAT_AF, &terminal);
    CheckWarning(err != css_err_none, UNKNOWN_ERROR, "Failed to get AF terminal: %d", err);

    terminalPayload.data = payloadBase + mTerminalBuffers[terminal].offset;
    terminalPayload.size = mTerminalBuffers[terminal].size;

    err = ipu_pg_die_decode_terminal_payload(mP2PWrapper, &processGroup, terminal,
                                             &terminalPayload, &afStats);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to decode AWB terminal payload %d", err);

    ia_err iaErr = ia_isp_bxt_statistics_convert_af_from_binary(mIspAdaptHandle, &afStats, afGrid);
    CheckWarning(iaErr != ia_err_none, UNKNOWN_ERROR, "Failed to convert AF statistics %d", iaErr);

    ia_aiq_af_grid* af = (*afGrid);
    CheckWarning(af == nullptr, UNKNOWN_ERROR, "Failed to convert Isa AF statistics");

    LOG3A("AF  stat grid %dx%d", af->grid_width, af->grid_height);

    return OK;
}

int IspParamAdaptor::convertHdrAfStatistics(const shared_ptr<CameraBuffer> &hdrStats,
                                            ia_aiq_af_grid** afGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    ia_binary_data *hdrStatsData = (ia_binary_data *)(hdrStats->getBufferAddr());
    ia_err err = ia_isp_bxt_statistics_convert_af_from_binary(mIspAdaptHandle, hdrStatsData, afGrid);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to convert AF statistics %d", err);

    ia_aiq_af_grid* af = (*afGrid);
    CheckWarning(af == nullptr, UNKNOWN_ERROR, "Failed to convert Hdr AF statistics");

    LOG3A("AF  stat grid %dx%d", af->grid_width, af->grid_height);

    return OK;
}

int IspParamAdaptor::convertPsaAfStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                            ia_aiq_af_grid** afGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    ia_binary_data *hwStatsData = (ia_binary_data *)(hwStats->getBufferAddr());
    ia_err err = ia_isp_bxt_statistics_convert_af_from_binary(mIspAdaptHandle, hwStatsData, afGrid);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to convert AF statistics %d", err);

    ia_aiq_af_grid* af = (*afGrid);
    CheckWarning(af == nullptr, UNKNOWN_ERROR, "Failed to convert Psa AF statistics");

    LOG3A("AF stat grid %dx%d", af->grid_width, af->grid_height);

    return OK;
}

int IspParamAdaptor::convertPsaRgbsStatistics(const shared_ptr<CameraBuffer> &hwStats,
                                              const ia_aiq_ae_results &aeResults,
                                              ia_aiq_rgbs_grid** rgbsGrid)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    ia_binary_data *hwStatsData = (ia_binary_data *)(hwStats->getBufferAddr());
    // Below API params list: ia_isp_bxt, statistics, ir_weight(for RGB-IR sensor),
    // ae_result(for 2DP-SVE), bcomp_results(for compressed stats), out_rgbs_grid, out_ir_grid
    ia_err err = ia_isp_bxt_statistics_convert_awb_from_binary_v3(mIspAdaptHandle, hwStatsData,
                                                                  nullptr, &aeResults, mBCompResults,
                                                                  rgbsGrid, nullptr);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to convert RGBS statistics %d", err);

    ia_aiq_rgbs_grid* rgbs = (*rgbsGrid);
    CheckWarning(rgbs == nullptr, UNKNOWN_ERROR, "Failed to convert Psa RGBS statistics");

    LOG3A("RGBS stat grid %dx%d", rgbs->grid_width, rgbs->grid_height);

    CheckWarning(rgbs->grid_width > MAX_STATISTICS_WIDTH || rgbs->grid_height > MAX_STATISTICS_HEIGHT,
            BAD_VALUE, " PSA rgbs buffer maybe too small %dx%d", rgbs->grid_width, rgbs->grid_height);

    dumpRgbsStats(rgbs, hwStats->getSequence(), aeResults.num_exposures);

    return OK;
}

int IspParamAdaptor::convertDvsStatistics(const shared_ptr<CameraBuffer> &hwStats,ia_dvs_statistics **dvsStats,
                                              camera_resolution_t resolution)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    AutoMutex l(mIspAdaptorLock);
    ia_isp_bxt_statistics_query_results_t queryResults;
    CLEAR(queryResults);
    ia_binary_data *hwStatsData = (ia_binary_data *)(hwStats->getBufferAddr());
    ia_err err = ia_isp_bxt_statistics_query(mIspAdaptHandle, hwStatsData, &queryResults);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to query DVS statistics: %d", err);
    CheckWarning(!queryResults.dvs_stats, UNKNOWN_ERROR, "%s No DVS statistics", __func__);

    ia_binary_data *dvsStatsData = (ia_binary_data *)(hwStats->getBufferAddr());
    if (CameraDump::isDumpTypeEnable(DUMP_PSYS_DECODED_STAT)) {
        BinParam_t bParam;
        bParam.bType = BIN_TYPE_GENERAL;
        bParam.mType = M_PSYS;
        bParam.sequence = hwStats->getSequence();
        bParam.gParam.appendix = "dvs_p2p_decoded_stats";
        CameraDump::dumpBinary(mCameraId, dvsStatsData->data, dvsStatsData->size, &bParam);
    }

    err = ia_isp_bxt_statistics_convert_dvs_from_binary(mIspAdaptHandle, hwStatsData,
                                            resolution.width, resolution.height, dvsStats);
    CheckWarning(err != ia_err_none, UNKNOWN_ERROR, "Failed to convert DVS statistics %d", err);

    return OK;
}

int IspParamAdaptor::queryStats(const shared_ptr<CameraBuffer> &hwStats, ia_isp_bxt_statistics_query_results_t* queryResults)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    AutoMutex l(mIspAdaptorLock);
    ia_binary_data *hwStatsData = (ia_binary_data *)(hwStats->getBufferAddr());
    Check((hwStatsData == nullptr || hwStatsData->data == nullptr || hwStatsData->size <= 0),
           UNKNOWN_ERROR, " Wrong statistics buffer");
    ia_err ret = ia_isp_bxt_statistics_query(mIspAdaptHandle, hwStatsData, queryResults);
    Check(ret != ia_err_none, UNKNOWN_ERROR, " statistice query fail %d", ret);

    return OK;
}

/**
 * encodeIsaParams
 *
 * Encode the ISA configuration input parameters
 * The ISA configuration buffers are multi-plane:
 * plane 0 contain the process group descriptor
 * plane 1 contain the parameter payload
 * This method is used to encode both and write them to the mmap buffers
 * created by the driver.
 *
 * Encoding of the ISA parameters involves running AIC + PAL + P2P
 *
 * \param [IN] type: Buffer type.
 * \param [IN] seqId: The sequence of the frame to be catched.
 * \param [IN/OUT] buf: The output of ISA configuration parameters, or stats data,
 *                      according to the process group descriptor of this buffer.
 *
 * \return OK everything went fine
 */
int IspParamAdaptor::encodeIsaParams(const shared_ptr<CameraBuffer> &buf,
                                     EncodeBufferType type, long settingSequence)
{
    PERF_CAMERA_ATRACE_IMAGING();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION,
          "wrong state %d to encode ISA params", mIspAdaptorState);
    Check(mPgParamType != PG_PARAM_ISYS, INVALID_OPERATION,
          "wrong pg param type %d to encode ISA params", mPgParamType);

    // stream id 0 for PG_PARAM_ISYS
    int status = runIspAdaptL(mStreamIdToProgramGroupMap[0], nullptr, settingSequence);
    Check(status != OK, UNKNOWN_ERROR, "runIspAdaptL failed in encodeIsaParams ret=%d", status);

    css_err_t err = ipu_pg_die_set_parameters(mP2PWrapper, &mCurrentIpuParam);
    Check(err != css_err_none, UNKNOWN_ERROR, "Could not set process group parameters %d", err);

    // Now we are ready to encode the process group descriptor and the terminal payloads
    ia_binary_data pg, terminalBuf;
    int planeIndex = 0;
    pg.data = buf->getBufferAddr(planeIndex);
    pg.size =  buf->getBufferSize(planeIndex);
    planeIndex = 1;
    char *payloadBase =  (char*)buf->getBufferAddr(planeIndex);

    err = ipu_pg_die_create_process_group(mP2PWrapper, &pg);
    Check(err != css_err_none, UNKNOWN_ERROR, "Could not create ISA process group %d", err);

    /* Iterate through all terminals but encode only input or outputs depending
     * on the input parameter, ENCODE_ISA_CONFIG needs to encode input terminals,
     * while ENCODE_STATS needs to encode output terminals.
     */
    for (uint32_t termIdx = 0; termIdx < mTerminalBuffers.size(); termIdx++) {
        if (mTerminalBuffers[termIdx].size == 0) {
            continue;
        }
        terminalBuf.size = mTerminalBuffers[termIdx].size;
        if (ipu_pg_die_is_input_terminal(mP2PWrapper, termIdx)) {
            if (type == ENCODE_STATS) {
                continue;
            }
            terminalBuf.data = payloadBase + mTerminalBuffers[termIdx].offset;
        } else {
            if (type == ENCODE_ISA_CONFIG) {
                continue;
            }
            terminalBuf.data = (void*) -1; // Irrelevant,  not used
        }

        {
            PERF_CAMERA_ATRACE_PARAM1_IMAGING("ipu_pg_die_encode_terminal_payload", 1);
            err = ipu_pg_die_encode_terminal_payload(mP2PWrapper, &pg, termIdx,
                                                 &terminalBuf, mTerminalBuffers[termIdx].offset);
        }
        if (err != css_err_none) {
            LOGE("@%s:Could not encode terminal %d error %d", __func__, termIdx, err);
            return UNKNOWN_ERROR;
        }
    }

    // dump PG and terminal content
    dumpP2PContent(buf, &pg, type);

    return OK;
}

void IspParamAdaptor::updateKernelToggles(ia_isp_bxt_program_group programGroup) {

    if (!Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_KERNEL_TOGGLE)) return;

    const char* ENABLED_KERNELS = "/tmp/enabledKernels";
    const char* DISABLED_KERNELS = "/tmp/disabledKernels";
    const int FLIE_CONT_MAX_LENGTH = 1024;
    ia_isp_bxt_run_kernels_t* curKernel = programGroup.run_kernels;
    char enabledKernels[FLIE_CONT_MAX_LENGTH] = { 0 };
    char disabledKernels[FLIE_CONT_MAX_LENGTH] = { 0 };

    int enLen = CameraUtils::getFileContent(ENABLED_KERNELS, enabledKernels, FLIE_CONT_MAX_LENGTH - 1);
    int disLen = CameraUtils::getFileContent(DISABLED_KERNELS, disabledKernels, FLIE_CONT_MAX_LENGTH - 1);

    if (enLen == 0 && disLen == 0) {
        LOG2("%s: no explicit kernel toggle.", __func__);
        return;
    }

    LOG2("%s: enabled kernels: %s, disabled kernels %s", __func__,
        enabledKernels, disabledKernels);

    for (unsigned int i = 0; i < programGroup.kernel_count; i++) {

        const char* curKernelUUID = std::to_string(curKernel->kernel_uuid).c_str();

        LOG2("%s: checking kernel %s", __func__, curKernelUUID);

        if (strstr(enabledKernels, curKernelUUID) != nullptr) {
            curKernel->enable = 1;
            LOG2("%s: kernel %d is explicitly enabled", __func__,
                curKernel->kernel_uuid);
        }

        if (strstr(disabledKernels, curKernelUUID) != nullptr) {
            curKernel->enable = 0;
            LOG2("%s: kernel %d is explicitly disabled", __func__,
                curKernel->kernel_uuid);
        }

        curKernel ++;
    }
}

/**
 * runIspAdapt
 * Convert the results of the 3A algorithms and parse with P2P.
 */
int IspParamAdaptor::runIspAdapt(const IspSettings* ispSettings, long settingSequence)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    AutoMutex l(mIspAdaptorLock);
    Check(mIspAdaptorState != ISP_ADAPTOR_CONFIGURED, INVALID_OPERATION, "%s, wrong state %d",
          __func__, mIspAdaptorState);

    int updateIndex = -1;
    // Check if the given sequence is already there, if so we need update it instead of
    // updating mCurIspParamIndex and using next buffer.
    for (int i = 0; i < ISP_PARAM_QUEUE_SIZE; i++) {
        if (mIspParameters[i].sequence == settingSequence) {
            updateIndex = i;
            break;
        }
    }

    bool forceUpdate = false;
    if (updateIndex == -1) {
        mCurIspParamIndex++;
        mCurIspParamIndex = mCurIspParamIndex % ISP_PARAM_QUEUE_SIZE;
        updateIndex = mCurIspParamIndex;
        forceUpdate = true;
        // Only Store the new sequence
        LOG2("%s, the sequence list size: %zu", __func__, mSequenceList.size());
        if (mSequenceList.size() >= MAX_BUFFER_COUNT) {
            mSequenceList.pop_front();
        }
        mSequenceList.push_back(settingSequence);
    }

    IspParameter& ipuParam = mIspParameters[updateIndex];
    LOG2("%s, current isp parameter index:%d, update index:%d, for sequence: %ld",
         __func__, mCurIspParamIndex, updateIndex, settingSequence);

    ipuParam.sequence = settingSequence;
    for (auto& binaryMap : ipuParam.streamIdToDataMap) {
        mCurrentIpuParam = binaryMap.second;
        mCurrentIpuParam.size = mStreamIdToPGOutSizeMap[binaryMap.first];
        int ret = runIspAdaptL(mStreamIdToProgramGroupMap[binaryMap.first],
                           ispSettings, settingSequence, forceUpdate);
        binaryMap.second.size = mCurrentIpuParam.size;

        Check(ret != OK, ret, "run isp adaptor error for streamId %d, sequence: %ld",
                               binaryMap.first, settingSequence);
    }

    return OK;
}

const ia_binary_data* IspParamAdaptor::getIpuParameter(long sequence, int streamId)
{
    AutoMutex l(mIspAdaptorLock);

    /* For old version.
     * TODO: We should get the ipu param according to streamId and
     * sequenceId when there are multi-streams in one pipe.
     */
    if (sequence == -1 && streamId == -1) {
        return &mCurrentIpuParam;
    }

    ia_binary_data* ipuParam = nullptr;
    for (int i = 0; i < ISP_PARAM_QUEUE_SIZE; i++) {
        IspParameter& param = mIspParameters[i];
        if (param.sequence == sequence &&
            param.streamIdToDataMap.find(streamId) != param.streamIdToDataMap.end()) {
            ipuParam = &(param.streamIdToDataMap[streamId]);
            break;
        }
    }

    if (ipuParam == nullptr) {
        LOGE("Failed to find ISP parameter for stream %d, sequence %ld", streamId, sequence);
        ipuParam = &mCurrentIpuParam;
    }

    return ipuParam;
}

/*
 * Allocate memory for mIspParameters
 * TODO: Let PAL to expose the max ia_binary_data buffer size which
 * come from mIspAdaptHandle->ia_pal.m_output_isp_parameters_size
 */
int IspParamAdaptor::allocateIspParamBuffers()
{
    releaseIspParamBuffers();

    for (int i = 0; i < ISP_PARAM_QUEUE_SIZE; i++) {
        for (auto & pgMap : mStreamIdToProgramGroupMap) {
            ia_binary_data ispParam;
            int size = mStreamIdToPGOutSizeMap[pgMap.first];
            CLEAR(ispParam);
            ispParam.size = size;
            ispParam.data = calloc(1, size);
            Check(ispParam.data == nullptr, NO_MEMORY, "Faile to calloc the memory for isp parameter");
            mIspParameters[i].streamIdToDataMap[pgMap.first] = ispParam;
        }
        mIspParameters[i].sequence = -1;
    }

    return OK;
}

void IspParamAdaptor::releaseIspParamBuffers()
{
    for (int i = 0; i < ISP_PARAM_QUEUE_SIZE; i++) {
        for (auto& binaryMap : mIspParameters[i].streamIdToDataMap)
            free(binaryMap.second.data);

        mIspParameters[i].sequence = -1;
        mIspParameters[i].streamIdToDataMap.clear();
    }
}

int IspParamAdaptor::runIspAdaptL(ia_isp_bxt_program_group programGroup,
                                  const IspSettings* ispSettings, long settingSequence,
                                  bool forceUpdate)
{
    PERF_CAMERA_ATRACE_IMAGING();
    AiqResult* aiqResults = const_cast<AiqResult*>(AiqResultStorage::getInstance(mCameraId)->getAiqResult(settingSequence));
    if (aiqResults == nullptr) {
        LOGW("%s: no result for sequence %ld! use the latest instead", __func__, settingSequence);
        aiqResults = const_cast<AiqResult*>(AiqResultStorage::getInstance(mCameraId)->getAiqResult());
        Check((aiqResults == nullptr), INVALID_OPERATION, "Cannot find available aiq result.");
    }
    Check((aiqResults->mSaResults.width * aiqResults->mSaResults.height == 0),
            INVALID_OPERATION, "No invalid aiq result needed to run Generic AIC");

    LOG2("%s: device type: %d", __func__, mPgParamType);

    ia_isp_bxt_input_params_v2 inputParams;
    ia_view_config_t viewConfig;
    CLEAR(inputParams);
    CLEAR(viewConfig);

    // LOCAL_TONEMAP_S
    if (PlatformData::isEnableHDR(mCameraId) && CameraUtils::isHdrPsysPipe(mTuningMode)) {
        size_t ltmLag = PlatformData::getLtmGainLag(mCameraId);
        long ltmSequence = settingSequence;

        // Consider there may be skipped frames, so according to the gain lag and current
        // sequence to find the actual ltm sequence in history list.
        if (mSequenceList.size() > ltmLag) {
            size_t index = 0;
            for(auto iter = mSequenceList.begin(); iter != mSequenceList.end(); iter++) {
                if (*iter == settingSequence && index >= ltmLag) {
                    ltmSequence = *(std::prev(iter, ltmLag));
                    break;
                }
                index++;
            }
        }
        ltm_result_t* ltmResult = const_cast<ltm_result_t*>(AiqResultStorage::getInstance(mCameraId)->getLtmResult(ltmSequence));
        if (ltmResult != nullptr) {
            LOG2("%s: frame sequence %ld, ltm sequence %ld, actual sequence: %ld",
                    __func__, settingSequence, ltmSequence, ltmResult->sequence);
            inputParams.ltm_results = &ltmResult->ltmResults;
            inputParams.ltm_drc_params = &ltmResult->ltmDrcParams;
        }
    }
    // LOCAL_TONEMAP_E

    // update metadata of runnning kernels
    if (mPgParamType == PG_PARAM_PSYS_ISA) {
        for (unsigned int i=0; i<programGroup.kernel_count; i++) {
            switch (programGroup.run_kernels[i].kernel_uuid) {
            case ia_pal_uuid_isp_tnr5_21:
            case ia_pal_uuid_isp_tnr5_22:
            case ia_pal_uuid_isp_tnr5_25:
                programGroup.run_kernels[i].metadata[0] = aiqResults->mSequence;
                LOG2("ia_pal_uuid_isp_tnr5_2x frame count = %d", programGroup.run_kernels[i].metadata[0]);
                break;
            case ia_pal_uuid_isp_bxt_ofa_dp:
            case ia_pal_uuid_isp_bxt_ofa_mp:
            case ia_pal_uuid_isp_bxt_ofa_ppp:
                programGroup.run_kernels[i].metadata[2] = aiqResults->mAiqParam.flipMode;
                LOG2("%s: flip mode set to %d", __func__, programGroup.run_kernels[i].metadata[2]);

                programGroup.run_kernels[i].metadata[3] = aiqResults->mAiqParam.yuvColorRangeMode;
                LOG2("ofa yuv color range mode %d", programGroup.run_kernels[i].metadata[3]);
                break;
            }
        }
    }

    // Enable or disable kernels according to environment variables for debug purpose.
    updateKernelToggles(programGroup);

    inputParams.program_group = &programGroup;
    inputParams.sensor_frame_params = &mFrameParam;

    inputParams.ae_results = &aiqResults->mAeResults;
    inputParams.gbce_results = &aiqResults->mGbceResults;
    inputParams.awb_results = &aiqResults->mAwbResults;
    inputParams.pa_results = &aiqResults->mPaResults;
    inputParams.sa_results = &aiqResults->mSaResults;
    inputParams.weight_grid = aiqResults->mAeResults.weight_grid;

    if (inputParams.ae_results != nullptr && mBCompHandle != nullptr) {
        ia_bcomp_input_params params = {inputParams.ae_results};
        ia_err err = ia_bcomp_run(mBCompHandle, &params, &mBCompResults);
        Check(err != ia_err_none, UNKNOWN_ERROR, "bit compression run failed %d", err);
        inputParams.bcomp_results = mBCompResults;
    }

    if (aiqResults->mCustomControls.count > 0) {
        inputParams.custom_controls = &aiqResults->mCustomControls;
    }

    if (ispSettings) {
        inputParams.nr_setting = ispSettings->nrSetting;
        inputParams.ee_setting = ispSettings->eeSetting;
        LOG2("%s: ISP NR setting, level: %d, strength: %d",
                __func__, (int)ispSettings->nrSetting.feature_level,
                (int)ispSettings->nrSetting.strength);
        inputParams.effects = ispSettings->effects;
        inputParams.manual_brightness = ispSettings->manualSettings.manualBrightness;
        inputParams.manual_contrast = ispSettings->manualSettings.manualContrast;
        inputParams.manual_hue = ispSettings->manualSettings.manualHue;
        inputParams.manual_saturation = ispSettings->manualSettings.manualSaturation;
        LOG2("%s: ISP EE setting, level: %d, strength: %d",
                __func__, ispSettings->eeSetting.feature_level,
                ispSettings->eeSetting.strength);
        // INTEL_DVS_S
        if (ispSettings->videoStabilization) {
            int dvsType = PlatformData::getDVSType(mCameraId);
            LOG2("%s: ISP Video Stabilization Mode Enable, dvs type %d", __func__, dvsType);
            DvsResult* dvsResult = const_cast<DvsResult*>(AiqResultStorage::getInstance(mCameraId)->getDvsResult());
            if (dvsType == MORPH_TABLE) {
                inputParams.dvs_morph_table = (dvsResult == nullptr) ? nullptr : &dvsResult->mMorphTable;
            } else if (dvsType == IMG_TRANS) {
                inputParams.gdc_transformation = (dvsResult == nullptr) ? nullptr : &dvsResult->mTransformation;
            }
        }
        // INTEL_DVS_E

        // Update sensor OB data if needed.
        if (ispSettings->useSensorOB) {

            inputParams.ob_black_level = ispSettings->obOutput;

            LOG3A("%s, ob_out(00:%.3f, 01:%.3f, 10:%.3f, 11:%.3f)", __func__,
                  inputParams.ob_black_level.cc00, inputParams.ob_black_level.cc01,
                  inputParams.ob_black_level.cc10, inputParams.ob_black_level.cc11);
        }

        if (ispSettings->wfovMode) {
            viewConfig.camera_mount_type = (ia_view_camera_mount_type_t)ispSettings->sensorMountType;
            viewConfig.zoom = ispSettings->zoom;
            viewConfig.type = (ia_view_projection_type_t)ispSettings->viewProjection.type;
            viewConfig.cone_angle = ispSettings->viewProjection.cone_angle;
            viewConfig.invalid_coordinate_mask[0] = 0;
            viewConfig.invalid_coordinate_mask[1] = 128;
            viewConfig.invalid_coordinate_mask[2] = 128;
            viewConfig.invalid_coordinate_mask[3] = 128;
            viewConfig.view_rotation.pitch = ispSettings->viewRotation.pitch;
            viewConfig.view_rotation.yaw = ispSettings->viewRotation.yaw;
            viewConfig.view_rotation.roll = ispSettings->viewRotation.roll;
            viewConfig.camera_rotation.pitch = ispSettings->cameraRotation.pitch;
            viewConfig.camera_rotation.yaw = ispSettings->cameraRotation.yaw;
            viewConfig.camera_rotation.roll = ispSettings->cameraRotation.roll;
            viewConfig.fine_adjustments.horizontal_shift = ispSettings->viewFineAdj.horizontal_shift;
            viewConfig.fine_adjustments.vertical_shift = ispSettings->viewFineAdj.vertical_shift;
            viewConfig.fine_adjustments.window_rotation = ispSettings->viewFineAdj.window_rotation;
            viewConfig.fine_adjustments.vertical_stretch = ispSettings->viewFineAdj.vertical_stretch;
            inputParams.view_params = (ia_isp_bxt_view_params_t const *)&viewConfig;
        }
        inputParams.pal_override = ispSettings->palOverride;
    }

    if (CameraUtils::isUllPsysPipe(mTuningMode)) {
        Check((aiqResults->mAeResults.exposures[0].exposure == nullptr), BAD_VALUE, "Aiq exposure is NULL.");
        // The situation that all DG passed to ISP, not sensor.
        if (!PlatformData::isUsingSensorDigitalGain(mCameraId)) {
            inputParams.manual_digital_gain = aiqResults->mAeResults.exposures[0].exposure->digital_gain;
        }
        // Fine-tune DG passed to ISP if partial ISP DG is needed.
        if (PlatformData::isUsingIspDigitalGain(mCameraId)) {
            inputParams.manual_digital_gain = AiqUtils::getIspDigitalGain(mCameraId,
                                aiqResults->mAeResults.exposures[0].exposure->digital_gain);
        }

        LOG3A("%s: set digital gain for ULL pipe: %f", __func__, inputParams.manual_digital_gain);
    } else if (CameraUtils::isHdrPsysPipe(mTuningMode) &&
               PlatformData::getHDRGainType(mCameraId) == HDR_ISP_DG_AND_SENSOR_DIRECT_AG) {
        Check((aiqResults->mAeResults.exposures[0].exposure == nullptr), BAD_VALUE, "Aiq exposure is NULL.");

        LOG3A("%s: all digital gain is passed to ISP, DG(%ld): %f",
              __func__, aiqResults->mSequence, aiqResults->mAeResults.exposures[0].exposure->digital_gain);
        inputParams.manual_digital_gain = aiqResults->mAeResults.exposures[0].exposure->digital_gain;
    }

    if (forceUpdate) {
        inputParams.sa_results->lsc_update = true;
    }

#ifndef ENABLE_PAC
    ia_err err;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_isp_bxt_run", 1);
        err = ia_isp_bxt_run_v2(mIspAdaptHandle, &inputParams, &mCurrentIpuParam);
    }
    Check(err != ia_err_none, UNKNOWN_ERROR, "ISP parameter adaptation has failed %d", err);
#endif

    dumpIspParameter(aiqResults->mSequence);

    return OK;
}

void IspParamAdaptor::dumpRgbsStats(ia_aiq_rgbs_grid *rgbsGrid, long sequence, unsigned int num)
{
    if (rgbsGrid == nullptr) return;

    if (Log::isDebugLevelEnable(CAMERA_DEBUG_LOG_AIQ)) {
        for (unsigned int i = 0; i < num; i++ ) {
            rgbs_grid_block *rgbsPtr = rgbsGrid[i].blocks_ptr;
            int size = rgbsGrid[i].grid_width * rgbsGrid[i].grid_height;
            // Print out some value to check if it's reasonable
            for (int j = 100; j < 105 && j < size; j++) {
                LOG3A("RGBS: [%d]:%d, %d, %d, %d, %d", j, rgbsPtr[j].avg_b, rgbsPtr[j].avg_gb,
                            rgbsPtr[j].avg_gr, rgbsPtr[j].avg_r, rgbsPtr[j].sat);
            }

            // Only print last Rgbs Stats's y_mean for validation purpose
            if (i < num - 1) continue;

            int sumLuma = 0;
            for (int j = 0; j < size; j++) {
                sumLuma += (rgbsPtr[j].avg_b + rgbsPtr[j].avg_r + (rgbsPtr[j].avg_gb + rgbsPtr[j].avg_gr) / 2) / 3;
            }
            LOG3A("RGB stat grid[%d] %dx%d, sequence %lld, y_mean %d", i,
                  rgbsGrid[i].grid_width, rgbsGrid[i].grid_height, sequence, sumLuma/size);
        }
    }

    if ((mPgParamType == PG_PARAM_PSYS_ISA && CameraDump::isDumpTypeEnable(DUMP_PSYS_AIQ_STAT)) ||
        (mPgParamType == PG_PARAM_ISYS && CameraDump::isDumpTypeEnable(DUMP_ISYS_AIQ_STAT))) {
        char name[30];
        BinParam_t bParam;
        bParam.bType    = BIN_TYPE_STATISTIC;
        bParam.mType    = mPgParamType == PG_PARAM_PSYS_ISA ? M_PSYS : M_ISYS;
        bParam.sequence = sequence;
        for (unsigned int i = 0; i < num; i++ ) {
            CLEAR(name);
            snprintf(name, sizeof(name), "%s_stats_%u_%u",
                    mPgParamType == PG_PARAM_PSYS_ISA ? "hdr_rgbs" : "rgbs", num, i);
            bParam.sParam.gridWidth  = rgbsGrid[i].grid_width;
            bParam.sParam.gridHeight = rgbsGrid[i].grid_height;
            bParam.sParam.appendix   = name;
            if (rgbsGrid[i].grid_width != 0 && rgbsGrid[i].grid_height != 0) {
                CameraDump::dumpBinary(mCameraId, rgbsGrid[i].blocks_ptr,
                                       rgbsGrid[i].grid_width * rgbsGrid[i].grid_height * sizeof(rgbs_grid_block),
                                       &bParam);
            }
        }
    }
}

void IspParamAdaptor::dumpIspParameter(long sequence) {
    if (mPgParamType == PG_PARAM_PSYS_ISA && !CameraDump::isDumpTypeEnable(DUMP_PSYS_PAL)) return;
    if (mPgParamType == PG_PARAM_ISYS && !CameraDump::isDumpTypeEnable(DUMP_ISYS_PAL)) return;

    BinParam_t bParam;
    bParam.bType    = BIN_TYPE_GENERAL;
    bParam.mType    = mPgParamType == PG_PARAM_PSYS_ISA ? M_PSYS : M_ISYS;
    bParam.sequence = sequence;
    bParam.gParam.appendix = "pal";
    CameraDump::dumpBinary(mCameraId, mCurrentIpuParam.data, mCurrentIpuParam.size, &bParam);
}

void IspParamAdaptor::dumpP2PContent(const shared_ptr<CameraBuffer> &buf,
                                     ia_binary_data* pg, EncodeBufferType type)
{
    if (CameraDump::isDumpTypeEnable(DUMP_ISYS_PG) && type == ENCODE_ISA_CONFIG) {
        ia_binary_data terminalBuf;
        char file_name[MAX_NAME_LEN];
        snprintf(file_name, sizeof(file_name), "%s/cam%d_%s_isys_pg_%04ld_id_", CameraDump::getDumpPath(),
                mCameraId, PlatformData::getSensorName(mCameraId), buf->getSequence());
        terminalBuf.data = (char*)buf->getBufferAddr(1);
        terminalBuf.size = mInputTerminalsSize;
        ipu_pg_die_dump_hexfile(mP2PWrapper, pg, &terminalBuf, file_name);
    } else if (CameraDump::isDumpTypeEnable(DUMP_ISYS_ENCODED_STAT) && type == ENCODE_STATS) {
        BinParam_t bParam;
        bParam.bType    = BIN_TYPE_GENERAL;
        bParam.mType    = M_ISYS;
        bParam.sequence = buf->getSequence();
        bParam.gParam.appendix = "payload_stats";
        CameraDump::dumpBinary(mCameraId, buf->getBufferAddr(1), buf->getBufferSize(1), &bParam);
    }
}

} // namespace icamera
