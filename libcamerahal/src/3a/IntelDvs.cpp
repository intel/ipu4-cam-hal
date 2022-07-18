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

#define LOG_TAG "IntelDvs"

#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "iutils/Utils.h"

#include "PlatformData.h"
#include "ia_cmc_parser.h"
#include "AiqUtils.h"
#include "AiqResultStorage.h"
#include "IGraphConfigManager.h"
#include "GraphConfig.h"
#include "ia_pal_types_isp_ids_autogen.h"
#include "IntelDvs.h"

namespace icamera {

const float MAX_DVS2_YUVDS_RATIO = 1.3f;
const int DVS_OXDIM_Y = 128;
const int DVS_OYDIM_Y = 32;
const int DVS_OXDIM_UV  = 64;
const int DVS_OXDIM_UV_LDC  = 32;
const int DVS_OYDIM_UV  = 16;

const int ENVELOPE_BQ_WIDTH = 192;
const int ENVELOPE_BQ_HEIGHT = 96;

DvsResult::DvsResult() :
    mSequence(-1)
{
    LOG3A("@%s", __func__);

    CLEAR(mTransformation);
    CLEAR(mMorphTable);
    CLEAR(mDvsXcoordsY);
    CLEAR(mDvsYcoordsY);
    CLEAR(mDvsXcoordsUV);
    CLEAR(mDvsYcoordsUV);
    CLEAR(mDvsXcoordsUVFloat);
    CLEAR(mDvsYcoordsUVFloat);
    mMorphTable.xcoords_y = mDvsXcoordsY;
    mMorphTable.ycoords_y = mDvsYcoordsY;
    mMorphTable.xcoords_uv = mDvsXcoordsUV;
    mMorphTable.ycoords_uv = mDvsYcoordsUV;
    mMorphTable.xcoords_uv_float = mDvsXcoordsUVFloat;
    mMorphTable.ycoords_uv_float = mDvsYcoordsUVFloat;
}

DvsResult::~DvsResult()
{
    LOG3A("@%s", __func__);
}

DvsResult &DvsResult::operator=(const DvsResult &other)
{
    AiqUtils::deepCopyDvsResults(other.mMorphTable, &this->mMorphTable);
    AiqUtils::deepCopyDvsResults(other.mTransformation, &this->mTransformation);
    mSequence = other.mSequence;

    return *this;
}

IntelDvs::IntelDvs(int cameraId, AiqSetting *setting) :
    mDvsHandle(nullptr),
    mDvsEnabled(false),
    mLdcEnabled(false),
    mRscEnabled(false),
    mDigitalZoomRatio(1.0f),
    mCameraId(cameraId),
    mFps(30),
    mConfigMode(CAMERA_STREAM_CONFIGURATION_MODE_NORMAL),
    mTuningMode(TUNING_MODE_VIDEO_HDR),
    mAiqSetting(setting),
    mKernelId(0),
    mMorphTable(nullptr),
    mStatistics(nullptr)
{
    LOG1("@%s", __func__);

    CLEAR(mSrcResolution);
    CLEAR(mDstResolution);
    CLEAR(mImage_transformation);
}

IntelDvs::~IntelDvs()
{
    LOG1("@%s", __func__);
}

int IntelDvs::initDvsHandle(TuningMode tuningMode)
{
    int status = OK;

    CpfStore* cpf = PlatformData::getCpfStore(mCameraId);
    Check((cpf == nullptr), NO_INIT, "@%s, No CPF for cameraId:%d", __func__, mCameraId);

    ia_binary_data aiqData;
    ia_cmc_t * cmc;
    int ret = cpf->getDataAndCmc(nullptr, &aiqData, nullptr, &cmc, tuningMode);
    Check(ret != OK, BAD_VALUE, "@%s, Get cpf data failed", __func__);

    ia_err err = ia_dvs_init(&mDvsHandle, &aiqData, cmc);
    Check(err != ia_err_none, NO_INIT, "@%s, Failed to initilize the DVS library", __func__);

    return status;
}

int IntelDvs::deinitDvsHandle()
{
    int status = deInitDVSTable();
    if (mDvsHandle) {
        ia_dvs_deinit(mDvsHandle);
        mDvsHandle = nullptr;
    }

    return status;
}

int IntelDvs::init()
{
    LOG1("@%s", __func__);
    AutoMutex l(mLock);

    return OK;
}

int IntelDvs::deinit()
{
    LOG1("@%s", __func__);
    AutoMutex l(mLock);

    return deinitDvsHandle();
}

int IntelDvs::configure(const vector<ConfigMode>& configModes, uint32_t kernelId, int srcWidth, int srcHeight,
                        int dstWidth, int dstHeight)
{
    LOG1("@%s", __func__);
    AutoMutex l(mLock);

    if (configModes.empty()) {
        return UNKNOWN_ERROR;
    }
    mConfigMode = configModes[0];

    TuningMode tuningMode;
    if (PlatformData::getTuningModeByConfigMode(mCameraId, mConfigMode, tuningMode) != OK) {
        return UNKNOWN_ERROR;
    }
    mTuningMode = tuningMode;

    mKernelId = kernelId;
    mSrcResolution.width = srcWidth;
    mSrcResolution.height = srcHeight;
    mDstResolution.width = dstWidth;
    mDstResolution.height = dstHeight;

    return reconfigure();
}

int IntelDvs::configure(TuningMode tuningMode, uint32_t kernelId, int srcWidth, int srcHeight,
                        int dstWidth, int dstHeight)
{
    LOG1("@%s", __func__);
    AutoMutex l(mLock);

    mTuningMode = tuningMode;
    mKernelId = kernelId;
    mSrcResolution.width = srcWidth;
    mSrcResolution.height = srcHeight;
    mDstResolution.width = dstWidth;
    mDstResolution.height = dstHeight;

    return reconfigure();
}

int IntelDvs::setDVSConfiguration(uint32_t kernelId, ia_dvs_configuration &config)
{

    if (mDvsEnabled) {
        config.num_axis = ia_dvs_algorithm_6_axis;
    } else {
        config.num_axis = ia_dvs_algorithm_0_axis;
    }
    /* General setting for dvs */
    config.source_bq.width_bq = mSrcResolution.width / 2;
    config.source_bq.height_bq = mSrcResolution.height / 2;
    config.output_bq.width_bq = mSrcResolution.width / 2;
    config.output_bq.height_bq = mSrcResolution.height / 2;
    // if the DstResolution is valid, the output_bq from dstResolution.
    if (mDstResolution.width != 0 && mDstResolution.height != 0) {
        config.output_bq.width_bq = mDstResolution.width / 2;
        config.output_bq.height_bq = mDstResolution.height / 2;
    }
    config.ispfilter_bq.width_bq = 0;
    config.ispfilter_bq.height_bq = 0;

    //config.num_axis = ia_dvs_algorithm_0_axis;
    config.gdc_shift_x = 0;
    config.gdc_shift_y = 0;

    if (kernelId == ia_pal_uuid_isp_gdc3_1) {
        config.oxdim_y = DVS_OXDIM_Y;
        config.oydim_y = DVS_OYDIM_Y;
        config.oxdim_uv = DVS_OXDIM_UV;
        config.oydim_uv = DVS_OYDIM_UV;
    } else {
        config.oxdim_y = DVS_OXDIM_Y / 2;
        config.oydim_y = DVS_OYDIM_Y;
        config.oxdim_uv = DVS_OXDIM_UV;
        config.oydim_uv = DVS_OYDIM_UV;
    }

    config.hw_config.scan_mode = ia_dvs_gdc_scan_mode_stb;
    config.hw_config.interpolation = ia_dvs_gdc_interpolation_bci;
    config.hw_config.performance_point = ia_dvs_gdc_performance_point_1x1;

    config.gdc_buffer_config.x_offset = 0;
    config.gdc_buffer_config.y_offset = 0;
    config.gdc_buffer_config.width = config.source_bq.width_bq;
    config.gdc_buffer_config.height = config.source_bq.height_bq;
    config.frame_rate = mFps;
    config.validate_morph_table = false;
    config.zoom_enabled = false;
    /*
     * cropping from the active pixel array, needs to be coming from history
     */
    config.crop_params.horizontal_crop_offset = 0;
    config.crop_params.vertical_crop_offset = 0;
    config.crop_params.cropped_width = 0;
    config.crop_params.cropped_height = 0;

    config.envelope_bq.width_bq = ENVELOPE_BQ_WIDTH;
    config.envelope_bq.height_bq = ENVELOPE_BQ_HEIGHT;

    int bq_max_width = int(MAX_DVS2_YUVDS_RATIO * float(config.output_bq.width_bq));
    int bq_max_height = int(MAX_DVS2_YUVDS_RATIO * float(config.output_bq.height_bq));

    if (config.source_bq.width_bq - config.envelope_bq.width_bq - config.ispfilter_bq.width_bq > bq_max_width)
        config.envelope_bq.width_bq = config.source_bq.width_bq - config.ispfilter_bq.width_bq - bq_max_width;

    if (config.source_bq.height_bq - config.envelope_bq.height_bq - config.ispfilter_bq.height_bq > bq_max_height)
        config.envelope_bq.height_bq = config.source_bq.height_bq - config.ispfilter_bq.height_bq - bq_max_height;

    if (mLdcEnabled) {
        //The crop must be set in LDC function, or there is config dvs fail
        config.crop_params.cropped_width = mDstResolution.width / 2;
        config.crop_params.cropped_height = mDstResolution.height / 2;
        // envelope bq is only for stabilization and it has to be set as 0 when ldc enabled.
        // TODO: clear define the envelope_bq when ldc & video stabilization enabled together
        config.envelope_bq.width_bq = 0;
        config.envelope_bq.height_bq = 0;
        config.use_lens_distortion_correction = true;
        config.zoom_enabled = false;
        config.oxdim_uv = DVS_OXDIM_UV_LDC;
    }

    if (mRscEnabled) {
        //TODO: set config.nonblanking_ratio to inputReadoutTime/framePeriod.
    }
    return 0;
}

int IntelDvs::reconfigure()
{
    LOG1("@%s", __func__);

    int status = OK;
    uint32_t gdcKernelId = mKernelId;

    // If parameters are not valid, try to query them in GC.
    if (gdcKernelId == 0 || mSrcResolution.width == 0 || mSrcResolution.height == 0) {
        //update GC
        shared_ptr<IGraphConfig> gc = nullptr;

#if !defined(USE_STATIC_GRAPH)
        if (PlatformData::getGraphConfigNodes(mCameraId)) {
#endif
            IGraphConfigManager *GCM = IGraphConfigManager::getInstance(mCameraId);
            if (GCM) {
                gc = GCM->getGraphConfig(mConfigMode);
            }
#if !defined(USE_STATIC_GRAPH)
        }
#endif
        CheckWarning(gc == nullptr, OK, "Failed to get GC in DVS");

        //update resolution infomation
        status = gc->getGdcKernelSetting(gdcKernelId, mSrcResolution);
        CheckWarning(status != OK, OK, "Failed to get GDC kernel setting, DVS disabled");
    }
    LOG1("%s, GDC kernel setting: id: %u, src resolution: %dx%d, dst resolution: %dx%d",
        __func__, gdcKernelId, mSrcResolution.width, mSrcResolution.height,
        mDstResolution.width, mDstResolution.height);

    if (mDvsHandle) {
        deinitDvsHandle();
    }
    status = initDvsHandle(mTuningMode);

    if (!mDvsHandle)
        return status;

    ia_dvs_configuration config;
    CLEAR(config);

    setDVSConfiguration(gdcKernelId, config);
    dumpConfiguration(config);

    float zoomHRatio = mSrcResolution.width / (mSrcResolution.width - config.envelope_bq.width_bq * 2);
    float zoomVRatio = mSrcResolution.height / (mSrcResolution.height - config.envelope_bq.height_bq * 2);
    ia_err err = ia_dvs_config(mDvsHandle, &config, (zoomHRatio > zoomVRatio) ? zoomHRatio : zoomVRatio);
    if (err != ia_err_none) {
        LOGW("Configure DVS failed %d", err);
        return UNKNOWN_ERROR;
    }

    LOG2("Configure DVS success");
    ia_dvs_set_non_blank_ratio(mDvsHandle, config.nonblanking_ratio);
    status = initDVSTable();
    if (status != OK) {
        LOGW("Allocate dvs table failed");
        return UNKNOWN_ERROR;
    }

    return status;
}

void IntelDvs::handleEvent(EventData eventData)
{
    if (eventData.type != EVENT_PSYS_STATS_BUF_READY) return;

    LOG3A("%s: handle EVENT_PSYS_STATS_BUF_READY", __func__);
    long sequence = eventData.data.statsReady.sequence;

    AiqResultStorage* aiqResultStorage = AiqResultStorage::getInstance(mCameraId);
    DvsStatistics *dvsStatistics = aiqResultStorage->getDvsStatistics();
    if (dvsStatistics->sequence != sequence || dvsStatistics->dvsStats == nullptr) return;

    // Set dvs statistics
    setStats(dvsStatistics->dvsStats);

    // Run dvs
    if (mAiqSetting) {
        aiq_parameter_t aiqParam;
        mAiqSetting->getAiqParameter(aiqParam);
        updateParameter(aiqParam);
    }

    DvsResult *dvsResult = aiqResultStorage->acquireDvsResult();

    const AiqResult *feedback = aiqResultStorage->getAiqResult(sequence);
    if (feedback == nullptr) {
        LOGW("%s: no aiq result for sequence %ld! use the latest instead", __func__, sequence);
        feedback = aiqResultStorage->getAiqResult();
    }

    int ret = run(feedback->mAeResults, dvsResult, sequence);
    Check(ret != OK, VOID_VALUE, "Run DVS fail");

    aiqResultStorage->updateDvsResult(sequence);
}

int IntelDvs::setStats(ia_dvs_statistics* statistics)
{
    LOG2("@%s", __func__);
    AutoMutex l(mLock);

    mStatistics = statistics;
    return OK;
}

int IntelDvs::run( const ia_aiq_ae_results &ae_results,
                        DvsResult *result,
                        long sequence,
                        uint16_t focus_position)
{
    LOG2("@%s", __func__);
    PERF_CAMERA_ATRACE_IMAGING();
    AutoMutex l(mLock);

    runImpl(ae_results, focus_position);

    int dvsType = PlatformData::getDVSType(mCameraId);
    switch (dvsType) {
        case MORPH_TABLE:
            return getMorphTable(sequence, result);
        case IMG_TRANS:
            return getImageTrans(sequence, result);
        default:
            LOGE("not supportted dvs type");
            return UNKNOWN_ERROR;
    }
}

int IntelDvs::configureDigitalZoom(ia_dvs_zoom_mode zoom_mode, ia_rectangle &zoom_region,
                                        ia_coordinate &zoom_coordinate)
{
    LOG2("@%s zoom mode:%d", __func__, zoom_mode);
    AutoMutex l(mLock);

    int status = BAD_VALUE;
    ia_err err = ia_dvs_set_digital_zoom_mode(mDvsHandle, zoom_mode);
    if (err != ia_err_none) {
        LOGW("set zoom mode error: %d", err);
        return status;
    }

    if (zoom_mode == ia_dvs_zoom_mode_region) {
        err = ia_dvs_set_digital_zoom_region(mDvsHandle, &zoom_region);
    } else if (zoom_mode == ia_dvs_zoom_mode_coordinate) {
        err = ia_dvs_set_digital_zoom_coordinate(mDvsHandle, &zoom_coordinate);
    }

    int ret = AiqUtils::convertError(err);
    Check(ret != OK, ret, "Error config zoom: %d", ret);

    return OK;
}

int IntelDvs::setZoomRatio(float zoomRatio)
{
    LOG2("@%s zoom:%4.2f", __func__, zoomRatio);
    AutoMutex l(mLock);

    ia_err err = ia_dvs_set_digital_zoom_magnitude(mDvsHandle, zoomRatio);
    if (err != ia_err_none)
        return UNKNOWN_ERROR;

    return OK;
}

/**
 * Private function implementations. mLock is assumed to be held.
 */

int IntelDvs::initDVSTable()
{
    LOG1("@%s", __func__);

    int dvsType = PlatformData::getDVSType(mCameraId);
    switch (dvsType) {
        case MORPH_TABLE:
            if (mMorphTable) {
                ia_dvs_free_morph_table(mMorphTable);
                mMorphTable = nullptr;
            }
            if (mDvsHandle) {
                ia_err err = ia_dvs_allocate_morph_table(mDvsHandle, &mMorphTable);
                if (!mMorphTable) {
                    LOGW("mMorphTable allocate failed");
                    return UNKNOWN_ERROR;
                }
                int ret= AiqUtils::convertError(err);
                Check(ret != OK, ret, "DVS allcoate morph table failed: %d", ret);
            }
            break;
        case IMG_TRANS:
            LOG1("not need allocate MorphTable for image_transformation");
            break;
        default:
            LOGE("not supportted dvs type");
            return UNKNOWN_ERROR;
    }
    return OK;
}

int IntelDvs::deInitDVSTable()
{
    int status = OK;
    if (mMorphTable) {
        ia_dvs_free_morph_table(mMorphTable);
        mMorphTable = nullptr;
    }

    return status;
}

int IntelDvs::runImpl(const ia_aiq_ae_results &ae_results,
                           uint16_t focus_position)
{
    LOG2("@%s", __func__);
    ia_err err = ia_err_none;
    int ret = OK;

    if (!mDvsHandle)
        return UNKNOWN_ERROR;

    if ((mDvsEnabled) && mStatistics && mStatistics->vector_count > 0) {
        err = ia_dvs_set_statistics(mDvsHandle, mStatistics, &ae_results,
                                    /*af results */ nullptr,
                                    /*sensor events*/nullptr,
                                    0, 0);
        ret= AiqUtils::convertError(err);
        Check(ret != OK, ret, "DVS set statistics failed: %d", ret);
    } else if ((mDvsEnabled) && !mStatistics) {
        return UNKNOWN_ERROR;
    }

    err = ia_dvs_execute(mDvsHandle, focus_position);
    ret = AiqUtils::convertError(err);
    Check(ret != OK, ret, "DVS execution failed: %d", ret);

    return OK;
}

int IntelDvs::getMorphTable(long sequence, DvsResult *result)
{
    LOG2("@%s", __func__);

    ia_err err = ia_dvs_get_morph_table(mDvsHandle, mMorphTable);
    int ret = AiqUtils::convertError(err);
    Check(ret != OK, ret, "Error geting DVS result: %d", ret);
    dumpDVSTable(mMorphTable, sequence);
    return AiqUtils::deepCopyDvsResults(*mMorphTable, &result->mMorphTable);
}

int IntelDvs::getImageTrans(long sequence, DvsResult *result)
{
    LOG2("@%s", __func__);

    ia_err err = ia_dvs_get_image_transformation(mDvsHandle, &mImage_transformation);
    int ret = AiqUtils::convertError(err);
    Check(ret != OK, ret, "Error geting DVS result: %d", ret);
    dumpDVSTable(&mImage_transformation, sequence);
    return AiqUtils::deepCopyDvsResults(mImage_transformation, &result->mTransformation);
}

int IntelDvs::updateParameter(const aiq_parameter_t &param)
{
    LOG2("@%s", __func__);

    bool dvsEnabled = (param.videoStabilizationMode == VIDEO_STABILIZATION_MODE_ON);
    bool ldcEnabled = (param.ldcMode == LDC_MODE_ON);
    bool rscEnabled = (param.rscMode == RSC_MODE_ON);
    int digitalZoomRatio = param.digitalZoomRatio;

    if ((param.fps > 0.01 && param.fps != mFps)
        || param.tuningMode != mTuningMode
        || dvsEnabled != mDvsEnabled || ldcEnabled != mLdcEnabled
        || rscEnabled != mRscEnabled) {

        mFps = param.fps > 0.01 ? param.fps : mFps;
        mTuningMode = param.tuningMode;
        mDvsEnabled = dvsEnabled;
        mLdcEnabled = ldcEnabled;
        mRscEnabled = rscEnabled;

        LOG3A("%s: DVS fps = %f ", __func__, mFps);
        LOG3A("%s: DVS tuning Mode = %d ", __func__, mTuningMode);
        LOG3A("%s: DVS enabled = %d ", __func__, mDvsEnabled);
        LOG3A("%s: LDC enabled = %d ", __func__, mLdcEnabled);
        LOG3A("%s: RSC enabled = %d ", __func__, mRscEnabled);

        return reconfigure();
    }

    if (param.digitalZoomRatio > 0 && param.digitalZoomRatio!= mDigitalZoomRatio) {

        mDigitalZoomRatio = digitalZoomRatio;
        setZoomRatio(mDigitalZoomRatio);
        LOG3A("%s: digital zoom ratio = %f ", __func__, mDigitalZoomRatio);
    }

    return OK;
}

int IntelDvs::dumpDVSTable(ia_dvs_morph_table *table, long sequence)
{
    if (!CameraDump::isDumpTypeEnable(DUMP_AIQ_DVS_RESULT)) return OK;

    LOG3A("%s", __func__);

    if (!table){
        LOGW("%s: morph table is nullptr, and nothing to dump.", __func__);
        return BAD_VALUE;
    }

    BinParam_t bParam;
    bParam.bType = BIN_TYPE_GENERAL;
    bParam.mType = M_PSYS;
    bParam.sequence = sequence;
    bParam.gParam.appendix = "dvs_morph_table_x_y";
    CameraDump::dumpBinary(0, table->xcoords_y, table->width_y * table->height_y * sizeof(uint32_t), &bParam);
    bParam.gParam.appendix = "dvs_morph_table_y_y";
    CameraDump::dumpBinary(0, table->ycoords_y, table->width_y * table->height_y * sizeof(uint32_t), &bParam);
    bParam.gParam.appendix = "dvs_morph_table_x_uv";
    CameraDump::dumpBinary(0, table->xcoords_uv, table->width_uv * table->height_uv * sizeof(uint32_t), &bParam);
    bParam.gParam.appendix = "dvs_morph_table_y_uv";
    CameraDump::dumpBinary(0, table->ycoords_uv, table->width_uv * table->height_uv * sizeof(uint32_t), &bParam);

    LOG3A("%s: DVS morph table y=[%d x %d], uv=[%d x %d] changed=%s", __func__,
               table->width_y, table->height_y,
               table->width_uv, table->height_uv,
               table->morph_table_changed == true ? "TRUE" : "FALSE");
    return OK;
}

int IntelDvs::dumpDVSTable(ia_dvs_image_transformation *trans, long sequence)
{
    if (!CameraDump::isDumpTypeEnable(DUMP_AIQ_DVS_RESULT)) return OK;

    LOG3A("%s", __func__);

    if (!trans){
        LOGW("%s: trans table is nullptr, and nothing to dump.", __func__);
        return BAD_VALUE;
    }

    LOG3A("%s: DVS trans table num_homography_matrices=%d", __func__,
            trans->num_homography_matrices);

    BinParam_t bParam;
    bParam.bType = BIN_TYPE_GENERAL;
    bParam.mType = M_PSYS;
    bParam.sequence = sequence;
    for (int i = 0; i < DVS_HOMOGRAPHY_MATRIX_MAX_COUNT; i++) {
        LOG3A("%s: DVS trans table %d start_row=%d", __func__,
            i, trans->matrices[i].start_row);
        bParam.gParam.appendix = "matrices";
        CameraDump::dumpBinary(0, &trans->matrices[i].matrix, 3 * 3 * sizeof(float), &bParam);
    }

    return OK;
}

int IntelDvs::dumpConfiguration(ia_dvs_configuration config)
{
    LOG3A("%s", __func__);

    LOG3A("config.num_axis %d", config.num_axis);
    LOG3A("config.nonblanking_ratio %f", config.nonblanking_ratio);
    LOG3A("config.source_bq.width_bq %d", config.source_bq.width_bq);
    LOG3A("config.source_bq.height_bq %d", config.source_bq.height_bq);
    LOG3A("config.output_bq.width_bq %d", config.output_bq.width_bq);
    LOG3A("config.output_bq.height_bq %d", config.output_bq.height_bq);
    LOG3A("config.envelope_bq.width_bq %d", config.envelope_bq.width_bq);
    LOG3A("config.envelope_bq.height_bq %d", config.envelope_bq.height_bq);
    LOG3A("config.ispfilter_bq.width_bq %d", config.ispfilter_bq.width_bq);
    LOG3A("config.ispfilter_bq.height_bq %d", config.ispfilter_bq.height_bq);
    LOG3A("config.gdc_shift_x %d", config.gdc_shift_x);
    LOG3A("config.gdc_shift_y %d", config.gdc_shift_y);
    LOG3A("config.oxdim_y %d", config.oxdim_y);
    LOG3A("config.oydim_y %d", config.oydim_y);
    LOG3A("config.oxdim_uv %d", config.oxdim_uv);
    LOG3A("config.oydim_uv %d", config.oydim_uv);
    LOG3A("config.hw_config.scan_mode %d", config.hw_config.scan_mode);
    LOG3A("config.hw_config.interpolation %d", config.hw_config.interpolation);
    LOG3A("config.hw_config.performance_point %d", config.hw_config.performance_point);
    LOG3A("config.validate_morph_table = %s", (config.validate_morph_table == true) ? "true" : "false");
    LOG3A("config.use_lens_distortion_correction = %s", (config.use_lens_distortion_correction == true) ? "true" : "false");

    return OK;
}

} // namespace icamera

