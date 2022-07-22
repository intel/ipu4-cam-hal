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

#define LOG_TAG "PSysPipe"

#include <ia_pal_types_isp_ids_autogen.h>
#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "PSysPipe.h"

extern "C" {
#include <ia_tools/ia_macros.h>
}


namespace icamera {

//TODO: This file based FW concurrency control need to be replaced by official FW solution.
static const char SYS_FS_CONCURRENCY_CTRL[] = "/sys/module/intel_ipu4_psys/parameters/enable_concurrency";
FILE*  PSysPipe::mFwConcurFile = nullptr;
int PSysPipe::mFwConcurDisableCnt = 0;
Mutex PSysPipe::mPipeMutex;

PSysPipe::PSysPipe(int cameraId):
    mPipe(nullptr)
    ,mPipeIterator(nullptr)
    ,mParamBufferSize(0)
    ,mBuilder(nullptr)
    ,mPipeCtrl(nullptr)
    ,mParamBuffer(nullptr)
    ,mIsExclusive(false)
    ,mDecodeStagesReady(false)
    ,mCameraId(cameraId)
    ,mStreamId(-1)
{
    LOG1("@%s", __func__);
}

PSysPipe::~PSysPipe()
{
    LOG1("@%s", __func__);
    mTermConfigMap.clear();

    imaging_pipe_ctrl_destroy(mPipeCtrl);

    for (auto it = mTermBufferMap.begin(); it != mTermBufferMap.end(); ++it) {
        ia_cipf_buffer_t* ia_buffer = it->second;

        ia_cipf_pipe_unregister_buffer(mPipe, ia_buffer);
        ia_cipf_frame_t *frame = ia_cipf_buffer_get_frame_ref(ia_buffer);
        if (frame == nullptr) {
            LOGE("Error getting frame for buffer");
        } else {
            IA_CIPR_FREE(frame->payload[0].data.cpu_ptr);
        }
        ia_cipf_buffer_destroy(ia_buffer);
    }
    mTermBufferMap.clear();

    for (auto cipfBuf : mShadowedTermBuffer) {
        ia_cipf_pipe_unregister_buffer(mPipe, cipfBuf);
        ia_cipf_frame_t *frame = ia_cipf_buffer_get_frame_ref(cipfBuf);
        if (frame == nullptr) {
            LOGE("Error getting frame for buffer");
        } else {
            IA_CIPR_FREE(frame->payload[0].data.cpu_ptr);
        }
        ia_cipf_buffer_destroy(cipfBuf);
    }
    mShadowedTermBuffer.clear();

    for (auto paramBuf = mParamBuffs.begin(); paramBuf != mParamBuffs.end(); ++paramBuf) {
        ia_cipf_buffer_t* ia_buffer = paramBuf->second;

        ia_cipf_pipe_unregister_buffer(mPipe, ia_buffer);
        IA_CIPR_FREE(ia_buffer->payload.data.cpu_ptr);
        ia_cipf_buffer_destroy(ia_buffer);
    }
    mParamBuffs.clear();

    clearRegistedBuffers();
    destroyPipeline();
    if (mBuilder) {
        ia_cipb_destroy(mBuilder);
    }

    mPsysBuffers.clear();
}

void PSysPipe::clearRegistedBuffers()
{
    for (auto registeredHalBuf = mRegisteredHalBufs.begin(); registeredHalBuf != mRegisteredHalBufs.end(); ++registeredHalBuf) {
        vector<RegHalBuf> regBufs = registeredHalBuf->second;
        while (!regBufs.empty()) {
            ia_cipf_buffer_t*  cipfBuf = regBufs[0].cipfBuf;
            if (cipfBuf) {
                ia_cipf_pipe_unregister_buffer(mPipe, cipfBuf);
                // Memory belongs to HAL so no need to free
                ia_cipf_buffer_destroy(cipfBuf);
                regBufs.erase(regBufs.begin());
            }
        }
    }
    mRegisteredHalBufs.clear();
}

int PSysPipe::start()
{
    mPsysBuffers.clear();

    return OK;
}

void PSysPipe::stop()
{
    clearRegistedBuffers();
    mPsysBuffers.clear();
    mCyclicFeedbackRoutinePairs.clear();
    mCyclicFeedbackDelayPairs.clear();
}

/**
 * build
 * Create the PSYS pipeline based on a connectionConfig
 * gotten from graph config
 */
int PSysPipe::build()
{
    status_t ret = OK;
    css_err_t err = css_err_none;
    bool localSource = false, localSink = false;
    int32_t localSourceId = 0, localSinkId = 0;
    ia_cipf_stage_t *sourceStage = nullptr, *sinkStage = nullptr;

    if (!mBuilder)
        mBuilder = ia_cipb_create();
    Check(mBuilder == nullptr, NO_MEMORY, "%s, Failed to create builder", __func__);

    mPipe = ia_cipf_pipe_create();
    Check(mPipe == nullptr, NO_MEMORY, "%s, Failed to create pipe", __func__);

    vector <GraphConfig::ConnectionConfig>::iterator it;
    for (it = mConnectionConfig.begin(); it != mConnectionConfig.end(); ++it) {
        /*
         * Handle external source connections
         */
        if (it->mSourceStage == 0) {
            it->mSourceStage = ia_cipf_external_source_uid(localSourceId);
            it->mSourceTerminal = ia_cipf_external_source_terminal_uid(localSourceId++);
        }
        sourceStage = ia_cipf_pipe_get_stage_by_uid(mPipe, it->mSourceStage);
        if (!sourceStage) {
            localSource = true;
            sourceStage = ia_cipb_create_stage(mBuilder, mPipe, it->mSourceStage);
            if (!sourceStage) {
                LOGE("Unable to create the CIPF source stage for connection");
                ret = BAD_VALUE;
                break;
            }
            err = ia_cipf_stage_set_iteration_index(sourceStage, it->mSourceIteration);
            if (err != css_err_none) {
                LOGE("Unable to set the CIPF source stage iteration");
                ret = BAD_VALUE;
                break;
            }
        }
        /*
         * Handle external sink connections
         */
        if (it->mSinkStage == 0) {
            it->mSinkStage = ia_cipf_external_sink_uid(localSinkId);
            it->mSinkTerminal = ia_cipf_external_sink_terminal_uid(localSinkId++);
        }
        sinkStage = ia_cipf_pipe_get_stage_by_uid(mPipe, it->mSinkStage);
        if (!sinkStage) {
            localSink = true;
            sinkStage = ia_cipb_create_stage(mBuilder, mPipe, it->mSinkStage);
            if (!sinkStage) {
                LOGE("Unable to create the CIPF sink stage for connection");
                ret = BAD_VALUE;
                break;
            }
            err = ia_cipf_stage_set_iteration_index(sinkStage, it->mSinkIteration);
            if (err != css_err_none) {
                LOGE("Unable to set the CIPF sink stage iteration");
                ret = BAD_VALUE;
                break;
            }
        }

        ia_cipf_terminal_t* sourceTerminal = ia_cipf_stage_get_terminal_by_uid(sourceStage,
                                                                               it->mSourceTerminal);
        if (!sourceTerminal) {
            LOGE("No CIPF source terminal in given stage");
            ret = BAD_VALUE;
            break;
        }

        ia_cipf_terminal_t* sinkTerminal = ia_cipf_stage_get_terminal_by_uid(sinkStage,
                                                                             it->mSinkTerminal);
        if (!sinkTerminal) {
            LOGE("No CIPF sink terminal in given stage");
            ret = BAD_VALUE;
            break;
        }

        err = ia_cipf_pipe_connect(mPipe,
                                   sourceStage,
                                   sourceTerminal,
                                   sinkStage,
                                   sinkTerminal,
                                   (ia_cipf_connection_type_t)(it->mConnectionType));
        if (err != css_err_none) {
            LOGE("Unable to create CIPF connection");
            ret = UNKNOWN_ERROR;
            break;
        }
        localSource = localSink = false;
        sourceStage = sinkStage = nullptr;
    }
    // Apply disable terminal property
    for (auto tml : mDisableTerminal) {
        ret = setDisableProperty(tml);
        if (ret != css_err_none) {
            LOGE("Unable to disable CIPF terminal 0x%x", tml);
            ret = BAD_VALUE;
            break;
        }
    }

    if (ret != OK) {
        LOGE("CIPF pipe build process failed! err=%d", ret);
    } else {
        mPipeCtrl = imaging_pipe_ctrl_init(mPipe);
        if (!mPipeCtrl) {
            LOGE("Failed to initialize imaging pipe controller");
            ret = UNKNOWN_ERROR;
        }
    }

    if (ret != OK) {
        if (localSource && sourceStage)
            ia_cipf_stage_destroy(sourceStage);
        if (localSink && sinkStage)
            ia_cipf_stage_destroy(sinkStage);
    }

    return ret;
}

/**
 * Set the mapping between stage id and cyclic feedback routine
 */
int PSysPipe::setCyclicFeedbackRoutineMaps(const vector<int>& cyclicFeedbackRoutine)
{
    if (cyclicFeedbackRoutine.empty()) {
        return OK;
    }

    Check(mPGIds.size() != cyclicFeedbackRoutine.size(), BAD_VALUE,
          "Number of cyclic feedback routine configuration doesn't match PG number");

    mCyclicFeedbackRoutinePairs.clear();

    for (unsigned int i = 0; i < mPGIds.size(); i++) {
        ia_uid stageId = psys_2600_pg_uid(mPGIds[i]);
        mCyclicFeedbackRoutinePairs.push_back(make_pair(stageId, cyclicFeedbackRoutine[i]));
    }

    return OK;
}

/**
 * Set the mapping between stage id and cyclic feedback delay
 */
int PSysPipe::setCyclicFeedbackDelayMaps(const vector<int>& cyclicFeedbackDelay)
{
    if (cyclicFeedbackDelay.empty()) {
        return OK;
    }

    Check(mPGIds.size() != cyclicFeedbackDelay.size(), BAD_VALUE,
          "Number of cyclic feedback delay configuration doesn't match PG number");

    mCyclicFeedbackDelayPairs.clear();

    for (unsigned int i = 0; i < mPGIds.size(); i++) {
        ia_uid stageId = psys_2600_pg_uid(mPGIds[i]);
        mCyclicFeedbackDelayPairs.push_back(make_pair(stageId, cyclicFeedbackDelay[i]));
    }

    return OK;
}

/**
 * Disable the property by terminal uid.
 */
int PSysPipe::setDisableProperty(uint32_t terminalId)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    css_err_t ret = css_err_none;

    LOG1("Disabling terminal UID %x", terminalId);
    ia_cipf_property_t *prop =
        ia_cipf_property_create_with_native_payload(ia_cipf_payload_uid_uint64);
    if (!prop) {
        LOGE("Failed to create property");
        return UNKNOWN_ERROR;
    }

    ia_cipf_terminal_t *term = ia_cipf_pipe_get_terminal_by_uid(mPipe, terminalId);
    if (!term) {
        LOGE("Failed to get terminal from pipe");
        ia_cipf_property_destroy(prop);
        return UNKNOWN_ERROR;
    }

    ret = ia_cipf_terminal_set_property_by_uid(term, css_kernel_disable_uid, prop);
    if (ret != css_err_none) {
        LOGE("Failed to set property");
        ia_cipf_property_destroy(prop);
        return UNKNOWN_ERROR;
    }

    ia_cipf_property_destroy(prop);

    return ret;
}

/**
 * Function to set frame format for each terminal of the PSYS pipeline.
 * The Format is stored in a map and applied in configureTerminals().
 *
 * \param[in] format Format settings to store
 */
void PSysPipe::setTerminalConfig(const GraphConfig::PortFormatSettings &format)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    ia_cipf_frame_format_t ff;
    CLEAR(ff);

    if (format.enabled == 0)
        return;

    ff.width = format.width;
    ff.height = format.height;
    ff.fourcc = format.fourcc;
    ff.bpl = format.bpl;
    ff.bpp = format.bpp;

    LOG1("%s: terminal id %d, resolution %dx%d, format %s", __func__,
        format.terminalId, format.width, format.height,
        CameraUtils::fourcc2String(format.fourcc).c_str());

    mTermConfigMap[format.terminalId] = ff;
}

/**
 * Nullify the connection structure of the side that is external to the pipe
 * for the connections that are on the edges of the stream.
 *
 *\param [IN] ci: Reference to the connection structure
 */
void PSysPipe::amendEdgeConnectionInfo(GraphConfig::ConnectionConfig &ci)
{
    if (ci.mConnectionType == connection_type_push) {
        /*
         * input port nullify the src
         */
        ci.mSourceStage = 0;
        ci.mSourceTerminal = 0;
    } else if (ci.mConnectionType == connection_type_pull) {
        /*
         * output port nullify the sink
         */
        ci.mSinkStage = 0;
        ci.mSinkTerminal = 0;
    }
}

/**
 * Function to set integer property af a stage.
 *
 * \param[in] stageUid identifier of CIPF stage
 * \param[in] propertyUid identifier of the CIPF property
 * \param[in] value value of the property
 */
int PSysPipe::setStageProperty(ia_uid stageUid, ia_uid propertyUid, uint32_t value)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    ia_cipf_stage_t *stage;
    css_err_t ret;

    if (mPipe == nullptr)
        return NO_INIT;

    stage = ia_cipf_pipe_get_stage_by_uid(mPipe, stageUid);
    Check(stage == nullptr, BAD_VALUE, "No such stage in ia_cipf pipe");

    ia_cipf_property_t *prop = ia_cipf_property_create_with_native_payload(ia_cipf_payload_uid_uint32);
    Check(prop == nullptr, NO_MEMORY, "Error creating ia_cipf property");

    ret = ia_cipf_property_set_uint32_value(prop, value);
    if (ret != css_err_none) {
        ia_cipf_property_destroy(prop);
        LOGE("Error setting ia_cipf property value");
        return BAD_VALUE;
    }

    ret = ia_cipf_stage_set_property_by_uid(stage, propertyUid, prop);
    if (ret != css_err_none) {
        ia_cipf_property_destroy(prop);
        LOGE("Error setting ia_cipf property to stage");
        return BAD_VALUE;
    }

    ia_cipf_property_destroy(prop);
    return OK;
}


/**
 * configureTerminals
 * Function that sets the frame format to each terminal of the PSYS pipeline.
 */
int PSysPipe::configureTerminals()
{
    LOG1("@%s, mTermConfigMap.size():%zu", __func__, mTermConfigMap.size());

    for (const auto &configMap : mTermConfigMap) {
        ia_uid uid = configMap.first;
        LOG2("@%s: UId %x", __func__, uid);
        ia_cipf_terminal_t* terminal = ia_cipf_pipe_get_terminal_by_uid(mPipe, uid);
        Check(terminal == nullptr, UNKNOWN_ERROR, "Terminal UID %x not found for pipe", uid);

        if (!ia_cipf_terminal_get_format_ref(terminal)) {
            LOGW("not frame format continuing");
            continue;
        }

        ia_cipf_frame_format_t fformat = configMap.second;
        LOG2("@%s: width %d, height %d, fourcc %x bpl %d, bpp %d",
                __func__, fformat.width, fformat.height, fformat.fourcc, fformat.bpl, fformat.bpp);
        css_err_t ret = ia_cipf_terminal_set_format(terminal, &fformat);
        Check(ret != css_err_none, UNKNOWN_ERROR, "%s, Failed to set format for pipe", __func__);
    }

    return OK;
}

int PSysPipe::setStageRbm(ia_uid stageUid, GraphConfig::StageAttr stageAttr)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);
    ia_cipf_stage_t *stage;
    css_err_t ret;

    if (mPipe == nullptr)
        return NO_INIT;

    stage = ia_cipf_pipe_get_stage_by_uid(mPipe, stageUid);
    Check(stage == nullptr, BAD_VALUE, "No such stage in ia_cipf pipe");

    ia_cipf_property_t *prop_rbm = ia_cipf_property_create();
    Check(prop_rbm == nullptr, UNKNOWN_ERROR, "%s, Failed to create rbm", __func__);

    ret = ia_cipf_property_allocate_payload(prop_rbm, stageAttr.rbm_bytes);
    if (ret != css_err_none) {
        ia_cipf_property_destroy(prop_rbm);
        LOGE("Error, fail to allocate rbm storage");
        return BAD_VALUE;
    }

    ret = ia_cipf_property_write_payload(prop_rbm, stageAttr.rbm, stageAttr.rbm_bytes);
    if (ret != ia_err_none)
    {
        ia_cipf_property_destroy(prop_rbm);
        LOGE("ConfigureStages: Error, fail to write rbm value!");
        return BAD_VALUE;
    }
    if (stageAttr.rbm)
        IA_CIPR_FREE(stageAttr.rbm);

    stageAttr.rbm = nullptr;
    ret = ia_cipf_stage_set_property_by_uid(stage, psys_stage_routing_bitmap_uid, prop_rbm);
    if (ret != ia_err_none)
    {
        ia_cipf_property_destroy(prop_rbm);
        LOGE("ConfigureStages: Error, fail to add rbm property!");
        return BAD_VALUE;
    }

    return OK;
}

/**
 * identifyProperties
 * Identify all properties of a PSYS pipe and set the payload size accordingly
 */
int PSysPipe::identifyProperties(IspParamAdaptor* adaptor)
{
    LOG1("@%s", __func__);
    Check(((!adaptor) || (!mPipeCtrl)), UNKNOWN_ERROR,
                "%s, the adaptor or mPipeCtrl is nullptr, BUG", __func__);

    ia_cipf_association_t association;
    ia_cipf_property_t *property;
    imaging_stage_status_t ss;
    ia_uid uid = 0;
    css_err_t ret;
    /* Identify properties */
    ret = ia_cipf_pipe_next_unidentified_property(mPipe, &property);
    while ((ret == css_err_none) && property) {
        uid = ia_cipf_property_get_uid(property);
        LOG2("%s: property to identify %x (%s)\n", __func__, uid, CameraUtils::fourcc2String(uid).c_str());

        ret = ia_cipf_property_get_association(property, &association);
        CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipf_property_destroy(property), "failed to get association from property %s",
                CameraUtils::fourcc2String(uid).c_str());

        ret = imaging_pipe_ctrl_identify_stage(mPipeCtrl,
                                               association.stage,
                                               &ss);
        CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipf_property_destroy(property), "failed to identify stage from property %s",
                CameraUtils::fourcc2String(uid).c_str());

        /* Imaging controller identified a new IPU stage that needs to be
         * prepared using information from imaging stack */
        if (ss == IMAGING_STAGE_NEW_IPU) {
            LOG2("%s: new stage uid: %x", __func__, association.stage);
            ret = prepareStage(mPipeCtrl, association.stage, adaptor->getIpuParameter(-1, mStreamId));
            CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipf_property_destroy(property), "failed to prepare imaging stage %s",
                    CameraUtils::fourcc2String(association.stage).c_str());
        } else if (ss == IMAGING_STAGE_UNKNOWN) {
            LOGE("Property %s from stage unknown to imaging controller",
                    CameraUtils::fourcc2String(association.stage).c_str());
            ia_cipf_property_destroy(property);
            return UNKNOWN_ERROR;
        }

        ret = imaging_pipe_ctrl_identify_property(mPipeCtrl, property);
        CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipf_property_destroy(property), "failed to identify property %x (%s), ret:%d",
                uid, CameraUtils::fourcc2String(uid).c_str(), ret);

        ia_cipf_property_destroy(property);
        ret = ia_cipf_pipe_next_unidentified_property(mPipe, &property);
    }

    return OK;
}

/**
 * prepareStage
 * Prepare new stage during identifyProperties
 */
int PSysPipe::prepareStage(imaging_pipe_ctrl_t *ctrl, ia_uid stageUid,
                           const ia_binary_data *ipuParameters)
{
    if (!ctrl
        || !ipuParameters
        || !ipuParameters->data
        || !ipuParameters->size) {
        return css_err_argument;
    }

    // Default control attributes
    imaging_ctrl_attributes_t attr = { IMAGING_TNR_DATAFLOW_SWAP, false,
                                       CYCLIC_FEEDBACK_DATAFLOW_OFF, 1 };
    // For the PG which need more frame delay with in its buffer feedback loop.
    for (const auto &item : mCyclicFeedbackRoutinePairs) {
        if (item.first == stageUid) {
            attr.cyclic_feedback_routine = static_cast<cyclic_feedback_dfm_routine>(item.second);
            LOG2("@%s: stageUid %d, cyclic_feedback_routine %d", __func__, stageUid, attr.cyclic_feedback_routine);
            break;
        }
    }

    for (const auto &item : mCyclicFeedbackDelayPairs) {
        if (item.first == stageUid) {
            attr.cyclic_feedback_delay = item.second;
            LOG2("@%s: stageUid %d, cyclic_feedback_delay %d", __func__, stageUid, attr.cyclic_feedback_delay);
            break;
        }
    }

    ia_cipf_buffer_t *tmp = ia_cipf_buffer_create();
    if (!tmp)
        return css_err_nomemory;
    tmp->payload.data.cpu_ptr = ipuParameters->data;
    tmp->payload.size = ipuParameters->size;
    tmp->payload.uid = imaging_ctrl_payload_pal_data;

    css_err_t ret = ia_cipf_buffer_add_reference(tmp,
                                                 imaging_ctrl_payload_attributes,
                                                 &attr);
    CheckWarningNoReturn((ret != css_err_none), "Failed to add reference %d", ret);

    if (ret == css_err_none) {
        ret = imaging_pipe_ctrl_prepare_stage_v2(ctrl, stageUid, tmp);
    }
    ia_cipf_buffer_destroy(tmp);

    Check((ret != css_err_none), ret, "Failed to prepare stage:%d", ret);

    return OK;
}

/**
 * bufferRequirements
 * Identify buffer requirements of each terminal and allocate internal buffers accordingly
 */
int PSysPipe::bufferRequirements(bool realTerminals)
{
    LOG1("@%s", __func__);
    ia_cipf_buffer_t *requiredBuffer = nullptr;

    /* Allocate & register buffers */
    css_err_t ret = ia_cipf_pipe_next_buffer_requirement(mPipe, &requiredBuffer);
    while ((ret == css_err_none) && requiredBuffer) {
        LOG1("@%s, line:%d, ret:%d, requiredBuffer:%p", __func__, __LINE__, ret, requiredBuffer);
        ret = handleBufferRequirement(requiredBuffer, realTerminals);
        Check((ret != OK), UNKNOWN_ERROR, "%s, Failed to allocate buffer size = %d" , __func__, requiredBuffer->payload.size);
        /* we made copies out from request */
        ia_cipf_buffer_destroy(requiredBuffer);
        ret = ia_cipf_pipe_next_buffer_requirement(mPipe, &requiredBuffer);
    }

    Check((ret != css_err_none), ret, "Failed to allocate buffers to Xos:%d", ret);

    return OK;
}

/**
 * prepare
 * Public function to complete PSYS pipe creation
 * (graph config version)
 *
 * Initialize the PG param adaptor for this pipeline and calculate the buffer
 * requirements for each terminal.
 *
 * \param[in] graphConfig Reference to the the graph config object.
 * \param[in] adaptor The isp param adaptor.
 * \return OK
 * \return UNKNOWN_ERROR if we fail to load the cipf pipeline or create the
 *                        iterator. Or if the buffer requirements do not match
 *                        the terminal format
 */
int PSysPipe::prepare(shared_ptr<GraphConfig> graphConfig, IspParamAdaptor* adaptor)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    css_err_t ret = ia_cipf_pipe_load(mPipe);
    Check(ret != css_err_none, UNKNOWN_ERROR, "Failed to load cipf pipe");

    int status = configureTerminals();
    Check(status != OK, status, "%s, Failed to configure terminals", __func__);

    status = identifyProperties(adaptor);
    Check(status != OK, status, "%s, Failed to identify properties", __func__);

    status = bufferRequirements(true);
    Check(status != OK, status, "%s, Failed to request internal buffers", __func__);

    mPipeIterator = ia_cipf_iterator_create(mPipe);
    Check(mPipeIterator == nullptr, UNKNOWN_ERROR, "%s, Failed to create pipeline iterator", __func__);

    addDecodeStage(graphConfig, ia_pal_uuid_isp_bxt_awbstatistics);
    addDecodeStage(graphConfig, ia_pal_uuid_isp_awbstatistics_2_0);
    addDecodeStage(graphConfig, ia_pal_uuid_isp_bxt_dvsstatistics);

    /* NOTICE! Must add sis kernel at last.
     * Or the statistics kernels after it will not be added to decode stage. */
    addDecodeStage(graphConfig, ia_pal_uuid_isp_sis_1_0_a);

    mDecodeStagesReady = true;

    return OK;
}

/**
 * addDecodeStage
 * add pg uid which contain statistics kernel
 *
 * \param[in] graphConfig Reference to the the graph config object.
 * \param[in] kernelId the statistics kernel id
 */
void PSysPipe::addDecodeStage(shared_ptr<GraphConfig> &graphConfig, int32_t kernelId)
{
    int32_t pgId = 0;

    int status = graphConfig->getPgIdForKernel(mStreamId, kernelId, pgId);
    if (status != OK) {
        LOG2("%s: kernel %d is not found", __func__, kernelId);
        return;
    }

    //Make sure found PG id is within current pipe
    if (mPGIds.empty()){
        LOGW("Empty PG id set in pipe");
        return;
    }

    bool pgIdInPipe = false;
    for (auto pipePGId : mPGIds) {
        if (pgId == pipePGId) {
            pgIdInPipe = true;
            break;
        }
    }

    if (!pgIdInPipe) {
        LOG2("PG id %d is not in current pipe", pgId);
        return;
    }

    LOG2("%s: kernel %d is found", __func__, kernelId);

    bool stageIdExists = false;
    for (auto stageUidsWithStats : mStageUidsWithStats) {
        ia_uid stageUid = stageUidsWithStats.second;
        if (stageUid == psys_2600_pg_uid(pgId)) {
            stageIdExists = true;
            break;
        }
    }

    if (!stageIdExists || (kernelId == ia_pal_uuid_isp_sis_1_0_a)) {
        mStageUidsWithStats[kernelId] = psys_2600_pg_uid(pgId);
    }
}

int PSysPipe::getStatsBufferCount()
{
    if (!mDecodeStagesReady) {
        LOGE("Decode stages are not ready.");
        return -1;
    }

    return mStageUidsWithStats.size();
}

/**
 * getPayloadSize
 * Function to get the size of buffer according to the frame format fourcc
 */
uint32_t PSysPipe::getPayloadSize(ia_cipf_buffer_t *buffer, ia_cipf_frame_format_t *format)
{
    if (buffer->payload.uid == ia_cipf_frame_uid) {
        float subsamplingRatio = 0;
        bool vectorized = false;
        css_err_t ret = ia_cipf_buffer_get_frame_format(buffer, format);
        Check((ret != css_err_none), 0, "call ia_cipf_buffer_get_frame_format fail");
        LOG1("@%s, format->fourcc:%d, %s", __func__, format->fourcc,
                CameraUtils::fourcc2String(format->fourcc).c_str());

        switch (format->fourcc) {
        case css_fourcc_raw:
        case ia_cipf_frame_fourcc_ba10:
        case ia_cipf_frame_fourcc_gr10:
        case ia_cipf_frame_fourcc_grbg:
        case ia_cipf_frame_fourcc_rggb:
        case ia_cipf_frame_fourcc_bggr:
        case ia_cipf_frame_fourcc_gbrg:
        case css_fourcc_raw_interleaved:
        case ia_cipf_frame_fourcc_ba12:
            return format->height * format->bpl;
        /* YUV formats */
        case css_fourcc_yyuv420_v32:
            /* Chroma samples packed with Y samples. Already included in bpl,
             * no need to calculate additional buffer size for those. */
            subsamplingRatio = 0;
            vectorized = true;
            break;
        default:
            subsamplingRatio = 0.5;
            break;
        }

        /* Vectorized formats have two lines interleaved together */
        uint32_t size = (format->height / (vectorized ? 2 : 1))
            * format->bpl * (1 + subsamplingRatio);

        return size;
    }

    // Parameter buffers should already have the size
    return buffer->payload.size;
}

/**
 * allocateFrameBuffer
 * Function to allocate memory for a cipf frame buffer
 */
ia_cipf_buffer_t * PSysPipe::allocateFrameBuffer(ia_cipf_buffer_t *reqBuffer)
{
    LOG1("@%s", __func__);
    ia_cipf_frame_format_t format;
    ia_cipf_frame_t *newFrame;
    uint32_t allocateSize = 0;
    ia_cipf_buffer_t *allocBuffer;

    Check(!reqBuffer, nullptr, "@%s, reqBuffer is NULL", __func__);

    allocateSize = getPayloadSize(reqBuffer, &format);
    LOG1("@%s, line:%d, allocateSize:%d", __func__, __LINE__, allocateSize);

    /* make a copy of request */
    allocBuffer = ia_cipf_buffer_create_copy(reqBuffer);
    Check((allocBuffer == nullptr), nullptr, "Error creating buffer copy");

    newFrame = ia_cipf_buffer_get_frame_ref(allocBuffer);
    Check((newFrame == nullptr), nullptr, "Error getting frame for buffer");

    newFrame->payload[0].data.cpu_ptr = IA_CIPR_ALLOC_ALIGNED(PAGE_ALIGN(allocateSize), IA_CIPR_PAGESIZE());
    Check((newFrame->payload[0].data.cpu_ptr == nullptr), nullptr, "Error allocating buffer");

    LOG2("@%s: Frame buffer allocateSize = %d", __func__, allocateSize);
    memset(newFrame->payload[0].data.cpu_ptr, 0, PAGE_ALIGN(allocateSize));

    newFrame->id = 0;
    newFrame->uid = format.fourcc;
    LOG2("@%s, line:%d, uid:%x\n", __func__, __LINE__, (int)newFrame->uid);
    newFrame->allocated = allocateSize;
    newFrame->planes = 1;
    newFrame->payload[0].size = allocateSize;
    newFrame->flags |= IA_CIPR_MEMORY_NO_FLUSH;

    /* Register buffer */
    css_err_t ret;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_pipe_register_buffer", 1);
        ret = ia_cipf_pipe_register_buffer(mPipe, allocBuffer);
    }
    Check((ret != css_err_none), nullptr, "Error registering buffer to pipe");

    LOG2("%s: allocated frame buffer %dx%d(%d)@%dbpp\n",
              __func__, format.width, format.height,
              format.fourcc, format.bpp);

    ia_cipf_terminal_t *terminal = ia_cipf_buffer_get_terminal(allocBuffer);
    ia_uid uid = ia_cipf_terminal_get_uid(terminal);

    LOG2("@%s, line:%d, uid:%x, %s\n", __func__, __LINE__, (int)uid,
            CameraUtils::fourcc2String(uid).c_str());

    return allocBuffer;
}

/**
 * allocateParamBuffer
 * Function to allocate memory for a cipf param buffer
 */
int PSysPipe::allocateParamBuffer(ia_cipf_buffer_t *reqBuffer)
{
    LOG1("@%s, mParamBuffs.size():%zu", __func__, mParamBuffs.size());
    Check(!reqBuffer, BAD_VALUE,"@%s, reqBuffer is nullptr", __func__);

    /* Right now we maintain one buffer for each param buffer requirement */
    auto it = mParamBuffs.find(reqBuffer->payload.uid);
    Check((it != mParamBuffs.end()), UNKNOWN_ERROR, "Buffer for uid: 0x%x already allocated!", reqBuffer->payload.uid);

    /* make a copy of request */
    ia_cipf_buffer_t* paramBuffer = ia_cipf_buffer_create();
    Check((!paramBuffer), UNKNOWN_ERROR, "@%s, line:%d, call ia_cipf_buffer_create fail", __func__, __LINE__);

    css_err_t ret = ia_cipf_buffer_replicate_association(paramBuffer, reqBuffer);
    Check((ret != css_err_none), UNKNOWN_ERROR, "@%s, line:%d, Error replicate association", __func__, __LINE__);

    paramBuffer->payload.data.cpu_ptr = IA_CIPR_ALLOC_ALIGNED(PAGE_ALIGN(reqBuffer->payload.size), IA_CIPR_PAGESIZE());
    if (paramBuffer->payload.data.cpu_ptr == nullptr) {
        ia_cipf_buffer_destroy(paramBuffer);
        LOGE("Error allocating buffer");
        return UNKNOWN_ERROR;
    }

    paramBuffer->payload.size = PAGE_ALIGN(reqBuffer->payload.size);
    LOG2("@%s: Param buffer allocateSize = %d", __func__, reqBuffer->payload.size);

    mParamBufferSize = paramBuffer->payload.size;
    LOG1("@%s: param buffer size = %d", __func__, mParamBufferSize);

    /* Letting Imaging controller to take care of the buffers */
    if (mPipeCtrl) {
        ret = imaging_pipe_ctrl_add_buffer(mPipeCtrl, paramBuffer);
        if (ret != css_err_none) {
            IA_CIPR_FREE(paramBuffer->payload.data.cpu_ptr);
            ia_cipf_buffer_destroy(paramBuffer);
            LOGE("Error adding parameter buffer to imaging pipe controller");
            return UNKNOWN_ERROR;
        }
    }

    /* Register buffer */
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_pipe_register_buffer", 1);
        ret = ia_cipf_pipe_register_buffer(mPipe, paramBuffer);
    }
    if (ret != css_err_none) {
        IA_CIPR_FREE(paramBuffer->payload.data.cpu_ptr);
        ia_cipf_buffer_destroy(paramBuffer);
        LOGE("Error registering buffer to pipe");
        return UNKNOWN_ERROR;
    }

    mParamBuffs[reqBuffer->payload.uid] = paramBuffer;
    LOG2("@%s, add uid to mParamBuffs:%x, %s\n", __func__,
            (int)reqBuffer->payload.uid, CameraUtils::fourcc2String(reqBuffer->payload.uid).c_str());

    return OK;
}

/**
 * handleBufferRequirement
 * Function to fulfill buffer requirements of each PSYS terminal
 */
int PSysPipe::handleBufferRequirement(ia_cipf_buffer_t *reqBuffer, bool realTerminals)
{
    LOG1("@%s", __func__);
    ia_cipf_terminal_t *terminal;
    ia_cipf_buffer_t *allocBuffer;
    ia_uid uid = 0, remoteUid = 0;

    Check(!reqBuffer, BAD_VALUE, "@%s, reqBuffer is NULL", __func__);

    LOG2("@%s, line:%d, uid:%x, %s", __func__, __LINE__, reqBuffer->payload.uid,
            CameraUtils::fourcc2String(reqBuffer->payload.uid).c_str());

    switch(reqBuffer->payload.uid) {
    case ia_cipf_frame_uid:
        /* we want to know for what terminal we are allocating */
        terminal = ia_cipf_buffer_get_terminal(reqBuffer);
        /* received frame buffer requirement from non-terminal type
         *
         * this is possible if stage realization wants to use frame
         * buffer definition to request client to allocate e.g
         * intermediate frame buffers not associated with any terminal
         * that could be connected.
         * */
        Check((!terminal), UNKNOWN_ERROR, "Received frame buffer requirement from non-terminal type");

        allocBuffer = allocateFrameBuffer(reqBuffer);
        Check((allocBuffer == nullptr), UNKNOWN_ERROR, "error allocating input buffer");

        terminal = ia_cipf_buffer_get_terminal(allocBuffer);
        if (!terminal) {
            LOGE("Failed to get terminal of buffer");
            ia_cipf_buffer_destroy(allocBuffer);
            return UNKNOWN_ERROR;
        }

        uid = ia_cipf_terminal_get_uid(terminal);

        /* check if the buffer requirement comes from generic input or
         * output. This is to allow setting of input and output buffers
         * generally from the task */
        terminal = ia_cipf_terminal_get_remote(terminal);
        if (terminal && !realTerminals) {
            remoteUid = ia_cipf_terminal_get_uid(terminal);
            uid = (remoteUid == ia_cipf_external_source_uid
                      || remoteUid == ia_cipf_external_sink_uid
                      || remoteUid == ia_cipf_external_secondary_sink_uid)
                      ? remoteUid : uid;
        }

        // Save the previous buffer for same UID in to mShadowedTermBuffer
        if (mTermBufferMap.find(uid) != mTermBufferMap.end()) {
            mShadowedTermBuffer.push_back(mTermBufferMap[uid]);
        }

        mTermBufferMap[uid] = allocBuffer;
        LOG1("@%s: Adding payload buffer for uid: %d, mTermBufferMap:%zu",
                __func__, uid, mTermBufferMap.size());
        break;
    default:
        allocateParamBuffer(reqBuffer);
        break;
    }
    return OK;
}

ia_cipf_buffer_t *PSysPipe::createCipfBufCopy(ia_cipf_buffer_t *reqBuffer,
                                                  const shared_ptr<CameraBuffer> &halBuffer)
{
    LOG1("@%s, V4L2_MEMORY_DMABUF:%d, halBuffer mode:%d",
        __func__, V4L2_MEMORY_DMABUF, halBuffer->getMemory());
    ia_cipf_frame_format_t format;
    css_err_t ret;

    ia_cipf_buffer_t* newBuffer = ia_cipf_buffer_create_copy(reqBuffer);
    Check((!newBuffer), nullptr, "Terminal not found");

    ia_cipf_frame_t* newFrame = ia_cipf_buffer_get_frame_ref(newBuffer);
    if (newFrame == nullptr) {
        LOGE("Error getting frame for buffer");
        goto bail;
    }

    if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
        if (0 == halBuffer->getFd()) {
            LOGW("@%s, line:%d, the halBuffer fd is 0", __func__, __LINE__);
        }
        newFrame->flags = IA_CIPR_MEMORY_HANDLE;
        newFrame->payload[0].data.handle = halBuffer->getFd();
    } else {
        newFrame->flags = IA_CIPR_MEMORY_CPU_PTR;
        newFrame->payload[0].data.cpu_ptr = halBuffer->getBufferAddr();
    }

#if ENABLE_VIRTUAL_IPU_PIPE
    // For ATE, the buffer size would be enlarged as PAL and KUID was also there
    newFrame->payload[0].size = halBuffer->getBufferSize();
#else
    // Recalc payload size without extra size.
    newFrame->payload[0].size =
        CameraUtils::getFrameSize(halBuffer->getFormat(), halBuffer->getWidth(),
                                  halBuffer->getHeight(), false, false);
#endif
    LOG1("%s: payload size: %u", __func__, newFrame->payload[0].size);

    if (!(halBuffer->isFlagsSet(BUFFER_FLAG_SW_READ | BUFFER_FLAG_SW_WRITE)))
        newFrame->flags |= IA_CIPR_MEMORY_NO_FLUSH;

    ret = ia_cipf_buffer_get_frame_format(newBuffer, &format);
    if (ret != css_err_none) {
        LOGE("Error getting frame format");
        goto bail;
    }
    LOG1("@%s: pipeline allocated input buffer resolution = %d x %d, v4l2 format = %x",
          __func__, format.width, format.height, format.fourcc);

    /* Register buffer */
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_pipe_register_buffer", 1);
        ret = ia_cipf_pipe_register_buffer(mPipe, newBuffer);
    }
    if (ret != css_err_none) {
        LOGE("Error registering buffer to pipe");
        goto bail;
    }
    return newBuffer;
bail:
    ia_cipf_buffer_destroy(newBuffer);
    return nullptr;

}

int PSysPipe::setPsysBuffer(ia_uid uid, const shared_ptr<CameraBuffer> &camBuffer)
{
    LOG1("@%s uid:%x", __func__, uid);
    Check(!camBuffer, UNKNOWN_ERROR,"@%s, line:%d, buffer is nullptr", __func__, __LINE__);

    mPsysBuffers[uid] = camBuffer;
    return OK;
}

/**
 * registerBuffer
 * Function to register a vector of HAL buffers to a cipf buffer. The memory
 * pointed by the HAL buffer is used by the cipf buffer instead of the locally
 * alocated one
 */
int PSysPipe::registerBuffers(void)
{
    LOG1("@%s", __func__);
    ia_cipf_buffer_t *newBuffer;
    bool bufregd = false;

    PERF_CAMERA_ATRACE();
    Check(mPsysBuffers.empty(), UNKNOWN_ERROR, "No PSYS buffers set");

    for (auto& psysBuffer : mPsysBuffers) {
        ia_uid terminalUid = psysBuffer.first;
        shared_ptr<CameraBuffer> halBuffer = psysBuffer.second;
        if (!halBuffer) {
            continue;
        }

        // Check if UID of terminal has any buffer registered
        auto registeredHalBuf = mRegisteredHalBufs.find(terminalUid);
        if (registeredHalBuf == mRegisteredHalBufs.end()) {
            // First time registering buffer for terminal
            // Create the list
            auto mapIt = mTermBufferMap.find(terminalUid);
            Check((mapIt == mTermBufferMap.end()), UNKNOWN_ERROR, "Terminal not found");

            newBuffer = createCipfBufCopy(mapIt->second, halBuffer);
            Check((newBuffer == nullptr), UNKNOWN_ERROR, "Failed to create cipf buffer copy");

            vector<RegHalBuf> theVector;
            if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
                theVector.push_back((RegHalBuf){newBuffer, {.fd = halBuffer->getFd()}});
                LOG1("@%s: New list cipf buffer = %p, for hal buffer fd = %d temrminal uid %x",
                     __func__, newBuffer, halBuffer->getFd(), terminalUid);
            } else {
                theVector.push_back((RegHalBuf){newBuffer, {.halBuffer = halBuffer->getBufferAddr()}});
                LOG1("@%s: New list cipf buffer = %p, for hal buffer = %p temrminal uid %x",
                     __func__, newBuffer, halBuffer->getBufferAddr(), terminalUid);
            }
            mRegisteredHalBufs[terminalUid] = theVector;
            continue;
        }

        // terminal has a buffer registered already, add to the list if not found
        vector<RegHalBuf> &theVector = registeredHalBuf->second;
        for (auto it = theVector.begin(); it != theVector.end(); ++it) {
            if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
                if (it->data.fd == halBuffer->getFd()) {
                    // Hal buffer is registered already
                    bufregd = true;
                    break;
                }
            } else {
                if (it->data.halBuffer == halBuffer->getBufferAddr()) {
                    // Hal buffer is registered already
                    bufregd = true;
                    break;
                }
            }
        }
        if (bufregd) {
            // Reset the variable
            bufregd = false;
            // Continue to register other buffers
            continue;
        }

        // First find template buffer
        auto mapIt = mTermBufferMap.find(terminalUid);
        Check((mapIt == mTermBufferMap.end()), UNKNOWN_ERROR, "Terminal not found");

        newBuffer = createCipfBufCopy(mapIt->second, halBuffer);
        Check((newBuffer == nullptr), UNKNOWN_ERROR, "Failed to create cipf buffer copy");

        if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
            theVector.push_back((RegHalBuf){newBuffer, {.fd = halBuffer->getFd()}});
            LOG1("@%s: Add cipf buffer = %p, for hal buffer fd= %d temrminal uid %x",
                    __func__, newBuffer, halBuffer->getFd(), terminalUid);
            LOG1("@%s: pipeline allocated input buffer = %p, for hal buffer fd= %d",
                    __func__, newBuffer, halBuffer->getFd());
        } else {
            theVector.push_back((RegHalBuf){newBuffer, {.halBuffer = halBuffer->getBufferAddr()}});
            LOG1("@%s: Add cipf buffer = %p, for hal buffer = %p temrminal uid %x",
                    __func__, newBuffer, halBuffer->getBufferAddr(), terminalUid);
            LOG1("@%s: pipeline allocated input buffer = %p, for hal buffer = %p",
                    __func__, newBuffer, halBuffer->getBufferAddr());
        }

        LOG1("@%s: hal buffer resolution = %d x %d, v4l2 format = %x",
             __func__, halBuffer->getWidth(), halBuffer->getHeight(), halBuffer->getFormat());
    }

    return OK;
}

/**
 * dumpIntermFrames
 * Dump the internal frame buffer in psys pipeline
 *
 *\param[in] sequence: the frame sequence id which need to dump
 */
void PSysPipe::dumpIntermFrames(unsigned int sequence)
{
    int ret;
    LOG1("@%s", __func__);

    if (!CameraDump::isDumpTypeEnable(DUMP_PSYS_INTERM_BUFFER))
        return;

    for (auto& it : mTermBufferMap) {
        BinParam_t binParam;
        ia_cipf_frame_format_t format;
        ia_uid uid = it.first;
        ia_cipf_buffer_t* ia_buffer = it.second;

        /**
         * Skip the non-intermediate buffer by seeing if the uid
         * is source of sink terminal uid.
         */
        if (uid == ia_cipf_external_source_uid   ||
                uid == ia_cipf_external_sink_uid ||
                uid == ia_cipf_external_secondary_sink_uid) continue;

        ia_cipf_frame_t* frame = ia_cipf_buffer_get_frame_ref(ia_buffer);
        Check((frame == nullptr), VOID_VALUE, "Error getting frame for buffer");

        ret = ia_cipf_buffer_get_frame_format(ia_buffer, &format);
        Check((ret != 0), VOID_VALUE, "fail to get frame format");

        binParam.bType    = BIN_TYPE_BUFFER;
        binParam.mType    = M_PSYS;
        binParam.sequence = sequence;
        binParam.bParam.width  = format.width;
        binParam.bParam.height = format.height;
        binParam.bParam.format = format.fourcc;
        LOG1("%s dump intermediate frame %d %dx%d %d %s\n", __func__, sequence,
                format.width, format.height, frame->payload[0].size,
                CameraUtils::fourcc2String(format.fourcc).c_str());
        CameraDump::dumpBinary(mCameraId, frame->payload[0].data.cpu_ptr,
                frame->payload[0].size, &binParam);
    }
}

int PSysPipe::handleSisStats(ia_cipf_buffer_t* ia_buffer, const shared_ptr<CameraBuffer> &outStatsBuffers)
{
    int ret;
    LOG1("@%s", __func__);

    ia_cipf_frame_t* frame = ia_cipf_buffer_get_frame_ref(ia_buffer);
    Check((frame == nullptr), BAD_VALUE, "Error getting frame for sis buffer");

    ia_binary_data* statBuf = (ia_binary_data*)outStatsBuffers->getBufferAddr();
    Check((statBuf == nullptr), BAD_VALUE, "Error getting buffer for sis a stats");

    statBuf->data = frame->payload[0].data.cpu_ptr;
    statBuf->size = frame->payload[0].size;

    ia_cipf_frame_format_t format;
    ret = ia_cipf_buffer_get_frame_format(ia_buffer, &format);
    Check((ret != 0), BAD_VALUE, "fail to get sis a frame format");

    outStatsBuffers->setUserBufferInfo(-1, format.width, format.height);

    LOG2("@%s: Ltm sis width is %d, height is %d ", __func__, format.width, format.height);

    return OK;
}

/**
 * iterate
 * Public function to iterate the PSYS pipeline with given buffers
 */
int PSysPipe::iterate(std::vector<shared_ptr<CameraBuffer>> & outStatsBuffers,
                      vector<EventType> & eventType,
                      long inputSequence, IspParamAdaptor* adaptor)
{
    css_err_t ret;
    shared_ptr<CameraBuffer> halBuffer;
    ia_cipf_buffer_t *cipfBuffer = nullptr;

    if (mPsysBuffers.empty()) {
        LOGE("mPsysBuffers is empty, return Error");
        return UNKNOWN_ERROR;
    }
    LOG1("@%s, line:%d, mPsysBuffers.size():%zu", __func__, __LINE__, mPsysBuffers.size());

    for (auto i = mPsysBuffers.begin(); i != mPsysBuffers.end(); ++i) {
        ia_uid terminalUid = i->first;
        halBuffer = i->second;
        Check(!halBuffer, UNKNOWN_ERROR, "mPsysBuffers has invlid param");

        // Check if UID of terminal has any buffer registered
        auto registeredHalBuf = mRegisteredHalBufs.find(terminalUid);
        Check((registeredHalBuf == mRegisteredHalBufs.end()), UNKNOWN_ERROR, "No psys buffer registered for Terminal %x ", terminalUid);

        // get the cipf buffer corresponding to the hal buffer
        vector<RegHalBuf> &theVector = registeredHalBuf->second;
        cipfBuffer = nullptr;  // Reset variable
        for (auto it = theVector.begin(); it != theVector.end(); ++it) {
            if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
                LOG2("@%s, line:%d, fd:%d, halBuffer fd:%d", __func__, __LINE__, it->data.fd, halBuffer->getFd());
                if (it->data.fd == halBuffer->getFd()) {
                    LOG2("@%s: iterate fd:%d, cipf %p", __func__, it->data.fd, it->cipfBuf);
                    cipfBuffer = it->cipfBuf;
                    break;
                }
            } else {
                LOG2("@%s, line:%d, buf:%p, halBuffer->buf:%p", __func__, __LINE__, it->data.halBuffer, halBuffer->getBufferAddr());
                if (it->data.halBuffer == halBuffer->getBufferAddr()) {
                    LOG2("@%s: iterate halBuffer:%p, cipf %p", __func__, it->data.halBuffer, it->cipfBuf);
                    cipfBuffer = it->cipfBuf;
                    break;
                }
            }
        }

        if (halBuffer->getMemory() == V4L2_MEMORY_DMABUF) {
            Check(!cipfBuffer, UNKNOWN_ERROR, "HAL buffer not registered for Terminal fd:%d ", halBuffer->getFd());
        } else {
            Check(!cipfBuffer, UNKNOWN_ERROR, "HAL buffer not registered for Terminal %p ", halBuffer->getBufferAddr());
        }

        // update the buffer sequence and timestamp
        ia_cipf_frame_t* thisFrame = ia_cipf_buffer_get_frame_ref(cipfBuffer);
        if (thisFrame) {
            thisFrame->sequence           = halBuffer->getSequence();
            thisFrame->timestamp.seconds  = halBuffer->getTimestamp().tv_sec;
            thisFrame->timestamp.useconds = halBuffer->getTimestamp().tv_usec;
        } else {
            LOGW("Fail to get frame for cipf buffer");
        }

        LOG1("@%s: setting buffer %p", __func__, cipfBuffer);
        ret = ia_cipf_iteration_set_buffer(mPipeIterator, cipfBuffer);
        Check((ret != css_err_none), UNKNOWN_ERROR, "Error setting buffer to iterator");

        cipfBuffer = nullptr;  // Reset variable
    }

    LOG2("@%s: buffer setting done", __func__);

    if (adaptor && mPipeCtrl) {
        const ia_binary_data* ipuParams = adaptor->getIpuParameter(inputSequence, mStreamId);
        Check((ipuParams == nullptr), UNKNOWN_ERROR, "Failed to get IPU parameters");

        PERF_CAMERA_ATRACE_PARAM1_IMAGING("imaging_pipe_ctrl_configure_stages", 1);
        ret = imaging_pipe_ctrl_configure_stages(mPipeCtrl, mPipeIterator, ipuParams);
        Check((ret != css_err_none), UNKNOWN_ERROR, "Imaging pipe controller failed to configure stages");
    }
    LOG2("@%s: configure stage done", __func__);

    if (mIsExclusive){
        enableConcurrency(false);
    }

    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_iteration_execute", 1);
        ret = ia_cipf_iteration_execute(mPipeIterator);
    }
    if (ret == css_err_again) {
        LOG2("@%s: execute again", __func__);
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_iteration_execute again", 1);
        ret = ia_cipf_iteration_execute(mPipeIterator);
    }
    Check((ret != css_err_none), UNKNOWN_ERROR, "Error iterating (ret = %d)", ret);
    LOG2("@%s: iteration execute done", __func__);

    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_cipf_iteration_wait", 1);
        ret = ia_cipf_iteration_wait(mPipeIterator);
    }
    Check((ret != css_err_none), UNKNOWN_ERROR, "Error waiting (ret = %d)", ret);
    LOG2("@%s: iteration wait done", __func__);

    if (mIsExclusive){
        enableConcurrency(true);
    }

    // dump psys intermediate buffer if required
    dumpIntermFrames(halBuffer ? halBuffer->getSequence() : 0);

    // Decode PSYS stats buffer
    Check((outStatsBuffers.size() < mStageUidsWithStats.size()), UNKNOWN_ERROR, "No enough stats buffers");
    int statsIndex = 0;

    for (auto stageUidsWithStats : mStageUidsWithStats) {
        int32_t kernelId = stageUidsWithStats.first;
        ia_uid decodeStageUid = stageUidsWithStats.second;
        LOG2("PG %d decode statistics, KernelId %d", decodeStageUid, kernelId);

        ia_binary_data* statBuf = (ia_binary_data*)outStatsBuffers[statsIndex]->getBufferAddr();
        ret = imaging_pipe_ctrl_decode_statistics(mPipeCtrl, mPipeIterator, decodeStageUid, statBuf);
        if (ret == ia_err_none) {
            LOG2("@%s: statsBuf after decoding, data: %p, size: %u",
                __func__, statBuf->data, statBuf->size);
        } else {
            LOGW("Error decoding PSYS statistics (ret = %d)", ret);
        }

        eventType.push_back(EVENT_PSYS_STATS_BUF_READY);

        statsIndex++;
    }

    LOG2("@%s: psys stats done", __func__);

    // clear the buffer vector
    mPsysBuffers.clear();

    return OK;
}

void PSysPipe::enableConcurrency(bool enable)
{
    AutoMutex l(mPipeMutex);

    // Protection for the situation of multiple exclusive pipes
    if (!enable) {
        mFwConcurDisableCnt ++;
    } else {
        mFwConcurDisableCnt --;
    }

    if (mFwConcurDisableCnt > 1) {
        return;
    }

    const char data = mFwConcurDisableCnt > 0 ? '0' : '1';
    LOG2("%s: %d", __func__, enable);

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
    } else {
        LOGE("Failed to operate FW concurrency control file");
    }
}

void PSysPipe::releaseConcurrency()
{
    AutoMutex l(mPipeMutex);

    if (mFwConcurFile != nullptr) {
        mFwConcurDisableCnt = 0;

        // Set enable concurrency
        const char data = '1';
        rewind(mFwConcurFile);
        LOG1("%s: write FW concurrency file with enable flag: 1", __func__);
        if ((fwrite((void*)&data, 1, 1, mFwConcurFile)) != 1) {
            LOGE("Error to write to sys fs enable_concurrency");
        }

        LOG1("%s: close file for concurrency control", __func__);
        fclose(mFwConcurFile);
        mFwConcurFile = nullptr;
    }
}

int PSysPipe::destroyPipeline(void)
{
    LOG1("@%s", __func__);

    if (mPipeIterator) {
        ia_cipf_iterator_destroy(mPipeIterator);
        mPipeIterator = nullptr;
    }

    if (mPipe) {
        ia_cipf_pipe_destroy(mPipe);
        mPipe = nullptr;
    }

    if (mIsExclusive){
        releaseConcurrency();
    }

    return OK;
}
}

