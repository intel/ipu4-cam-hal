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

#define LOG_TAG "PSysPipeBase"

#include "PSysPipeBase.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "ia_log.h"

namespace icamera {

struct FormatMap {
    int v4l2Fmt;
    ia_css_frame_format_type cssFmt;
};

static const FormatMap sFormatMapping[] = {
    { V4L2_PIX_FMT_YUYV,   IA_CSS_DATA_FORMAT_YUYV },
    { V4L2_PIX_FMT_UYVY,   IA_CSS_DATA_FORMAT_UYVY },
    { V4L2_PIX_FMT_YUV420, IA_CSS_DATA_FORMAT_YUV420 },
    { V4L2_PIX_FMT_NV12,   IA_CSS_DATA_FORMAT_NV12 },
    { V4L2_PIX_FMT_NV16,   IA_CSS_DATA_FORMAT_NV16 },

    { V4L2_PIX_FMT_RGB565, IA_CSS_DATA_FORMAT_RGB565 },
    { V4L2_PIX_FMT_RGB24,  IA_CSS_DATA_FORMAT_RGB888 },
    { V4L2_PIX_FMT_RGB32,  IA_CSS_DATA_FORMAT_RGBA888 },
};

ia_css_frame_format_type PSysPipeBase::getCssFmt(int v4l2Fmt) {
    int size = ARRAY_SIZE(sFormatMapping);
    for (int i = 0; i < size; i++) {
        if (sFormatMapping[i].v4l2Fmt == v4l2Fmt) {
            return sFormatMapping[i].cssFmt;
        }
    }

    LOGW("Unsupported V4l2 Pixel Format: %s", CameraUtils::format2string(v4l2Fmt));
    return IA_CSS_N_FRAME_FORMAT_TYPES;
}

static void set_program_terminals(
                ia_css_process_group_t *process_group,
                ia_css_program_group_manifest_t *pg_manifest,
                int width, int height,
                unsigned int nof_fragments)
{
    PERF_CAMERA_ATRACE();

    for (int i = 0; i < ia_css_process_group_get_terminal_count(process_group); i++) {
        ia_css_terminal_t *terminal = ia_css_process_group_get_terminal(process_group, i);
        Check(!terminal, VOID_VALUE, "%s: ia_css_process_group_get_terminal return nullptr", __func__);

        ia_css_terminal_type_t terminal_type = ia_css_terminal_get_type(terminal);
        LOG1("%s: terminal_count=%d, i=%d, terminal_type=%d, terminal->tm_index:%d",
             __func__, ia_css_process_group_get_terminal_count(process_group), i, terminal_type, terminal->tm_index);

        if (terminal_type != IA_CSS_TERMINAL_TYPE_PROGRAM) continue;

        ia_css_program_terminal_t *prog_terminal = (ia_css_program_terminal_t *)terminal;
        uint16_t tm_index = ia_css_terminal_get_terminal_manifest_index(terminal);
        ia_css_terminal_manifest_t *t_manifest = ia_css_program_group_manifest_get_term_mnfst(pg_manifest, tm_index);
        Check(!t_manifest, VOID_VALUE, "No terminal manifest for terminal %d", tm_index);
        const ia_css_program_terminal_manifest_t *prog_terminal_man = (const ia_css_program_terminal_manifest_t *)t_manifest;
        uint16_t manifest_info_count = prog_terminal_man->kernel_fragment_sequencer_info_manifest_info_count;

        for (uint8_t j = 0; j < nof_fragments; j++) {
            LOG1("kernel_fragment_sequencer_info_manifest_info_count:%d", manifest_info_count);
            for (int k = 0; k < manifest_info_count; k++) {
                ia_css_kernel_fragment_sequencer_info_desc_t* sequencer_info_desc_base =
                        ia_css_program_terminal_get_kernel_frgmnt_seq_info_desc(prog_terminal, j, k, manifest_info_count);
                if (sequencer_info_desc_base != nullptr) {
                    sequencer_info_desc_base->fragment_grid_slice_dimension[IA_CSS_COL_DIMENSION] = width;
                    sequencer_info_desc_base->fragment_grid_slice_dimension[IA_CSS_ROW_DIMENSION] = height;
                    sequencer_info_desc_base->fragment_grid_slice_count[IA_CSS_COL_DIMENSION] = 1;
                    sequencer_info_desc_base->fragment_grid_slice_count[IA_CSS_ROW_DIMENSION] = 1;
                    sequencer_info_desc_base->fragment_grid_point_decimation_factor[IA_CSS_COL_DIMENSION] = 1;
                    sequencer_info_desc_base->fragment_grid_point_decimation_factor[IA_CSS_ROW_DIMENSION] = 1;
                    sequencer_info_desc_base->fragment_grid_overlay_pixel_topleft_index[IA_CSS_COL_DIMENSION] = 0;
                    sequencer_info_desc_base->fragment_grid_overlay_pixel_topleft_index[IA_CSS_ROW_DIMENSION] = 0;
                    sequencer_info_desc_base->fragment_grid_overlay_pixel_dimension[IA_CSS_COL_DIMENSION] = width;
                    sequencer_info_desc_base->fragment_grid_overlay_pixel_dimension[IA_CSS_ROW_DIMENSION] = height;
                    sequencer_info_desc_base->command_count = 0;
                    sequencer_info_desc_base->command_desc_offset = 0;
                }
            }
        }
    }
}

PSysPipeBase::PSysPipeBase(int pgId):
    mManifestBuffer(nullptr),
    mPGParamsBuffer(nullptr),
    mPGBuffer(nullptr),
    mTerminalBuffers(nullptr),
    mPGId(pgId),
    mPGCount(0),
    mPlatform(IA_P2P_PLATFORM_BXT_B0),
    mProgramCount(0),
    mTerminalCount(0),
    mManifestSize(0),
    mProcessGroup(nullptr),
    mKernelBitmap(ia_css_kernel_bitmap_clear()),
    mCmd(nullptr),
    mFrameFormatType(nullptr),
    mNeedP2p(false)
{
    CLEAR(mCmdCfg);
    CLEAR(mPsysParam);

    mP2p = new PSysP2pLite(mPGId);

    mCtx = ia_cipr_psys_create_context(nullptr);
    mMemoryDevice = ia_cipr_psys_get_memory_device(mCtx);
    int ret = getCapability();
    if (ret != OK) return;

    // create mManifestBuffer
    ret = getManifest(mPGId);
    if (ret != OK) return;

    mTerminalBuffers = (ia_cipr_buffer_t**)IA_CIPR_CALLOC(mTerminalCount, sizeof(ia_cipr_buffer_t*));
    Check(!mTerminalBuffers, VOID_VALUE, "@%s, call IA_CIPR_CALLOC fail", __func__);
    memset(mTerminalBuffers, 0, (mTerminalCount * sizeof(ia_cipr_buffer_t*)));
    CLEAR(mCmdCfg);
    mCmdCfg.bufcount = mTerminalCount;
    mCmd = ia_cipr_psys_create_command(&mCmdCfg);
    ia_cipr_psys_get_command_config(mCmd, &mCmdCfg);
    CLEAR(mPsysParam);

    ia_env env;
    CLEAR(env);
    env.vdebug = &Log::ccaPrintDebug;
    env.verror = &Log::ccaPrintError;
    env.vinfo = &Log::ccaPrintInfo;
    ia_log_init(&env);
}

PSysPipeBase::~PSysPipeBase()
{
    ia_log_deinit();

    if (mTerminalBuffers) {
        IA_CIPR_FREE(mTerminalBuffers);
    }
    ia_cipr_buffer_destroy(mManifestBuffer);
    ia_cipr_buffer_destroy(mPGBuffer);
    ia_cipr_buffer_destroy(mPGParamsBuffer);
    for (auto& item : mBuffers) {
        ia_cipr_buffer_destroy(item.ciprBuf);
    }

    ia_cipr_psys_destroy_command(mCmd);
    ia_cipr_psys_destroy_context(mCtx);

    delete mP2p;
}

int PSysPipeBase::getCapability()
{
    ia_cipr_psys_capability_t cap;
    css_err_t ret = ia_cipr_psys_get_capabilities(mCtx, &cap);
    Check((ret != css_err_none), UNKNOWN_ERROR, "Call ia_cipr_psys_get_capabilities fail, ret:%d", ret);

    LOG1("capability.version:%d", cap.version);
    LOG1("capability.driver:%s", cap.driver);
    LOG1("capability.dev_model:%s", cap.dev_model);
    LOG1("capability.program_group_count:%d", cap.program_group_count);

    mPGCount = cap.program_group_count;

    ret = OK;
    if (strncmp((char *)cap.dev_model, "ipu4p", 5) == 0) {
        mPlatform = IA_P2P_PLATFORM_CNL_B0;
        LOG1("CNL / ICL / KSL shared the same p2p platform id");
    } else if (strncmp((char *)cap.dev_model, "ipu4", 4) == 0) {
        switch (cap.dev_model[13]) {
            case 'B':
                 mPlatform = IA_P2P_PLATFORM_BXT_B0;
                 break;
            default:
                 LOG1("Unsupported PSYS device model :%s", cap.dev_model);
                 ret = BAD_VALUE;
                 break;
        }
    } else {
        LOG1("Unsupported PSYS device model : %s", cap.dev_model);
        ret = BAD_VALUE;
    }
    return ret;
}

int PSysPipeBase::getManifest(int pgId)
{
    LOG1("@%s, pgId:%d", __func__, pgId);

    int i = 0;
    for (; i < (int)mPGCount; i++) {
        ia_cipr_buffer_t* manifestBuffer = nullptr;
        int programCount = 0;
        int terminalCount = 0;
        int programGroupId = 0;
        int manifestSize = 0;
        ia_css_kernel_bitmap_t kernelBitmap = ia_css_kernel_bitmap_clear();
        uint32_t size = 0;

        css_err_t ret = ia_cipr_psys_get_manifest(mCtx, i, &size, nullptr);
        LOG1("ia_cipr_psys_get_manifest, manifest size:%u", size);
        if (ret != css_err_none) continue;

        Check((size == 0), UNKNOWN_ERROR, "@%s, the manifest size is 0", __func__);

        manifestBuffer = createUserPtrCiprBuffer(size);
        Check(!manifestBuffer, NO_MEMORY, "@%s, call createUserPtrCiprBuffer fail", __func__);

        void* manifest = getCiprBufferPtr(manifestBuffer);
        LOG1("@%s, manifest's cpuptr is:%p", __func__, manifest);

        ret = ia_cipr_psys_get_manifest(mCtx, i, &size, manifest);
        Check((ret != css_err_none), UNKNOWN_ERROR, "@%s, call ia_cipr_psys_get_manifest fail, ret:%d", __func__, ret);
        LOG1("@%s, i:%d, size:%d", __func__, i, size);

        const ia_css_program_group_manifest_t *mf = (const ia_css_program_group_manifest_t*)manifest;
        programCount = ia_css_program_group_manifest_get_program_count(mf);
        terminalCount = ia_css_program_group_manifest_get_terminal_count(mf);
        programGroupId = ia_css_program_group_manifest_get_program_group_ID(mf);
        manifestSize = ia_css_program_group_manifest_get_size(mf);
        kernelBitmap = ia_css_program_group_manifest_get_kernel_bitmap(mf);

        LOG1("i:%d, programGroupId:%d, manifestSize:%d, programCount:%d, terminalCount:%d, kernelBitmap:%#018lx",
                i, programGroupId, manifestSize, programCount, terminalCount, kernelBitmap);

        if (pgId == programGroupId) {
            LOG1("Manifest for PG id %d found at index: %d", pgId, i);
            mProgramCount = programCount;
            mTerminalCount = terminalCount;
            mManifestSize = manifestSize;
            mKernelBitmap = kernelBitmap;
            mManifestBuffer = manifestBuffer;
            break;
        }

        ia_cipr_buffer_destroy(manifestBuffer);
    }

    Check((i >= mPGCount), BAD_VALUE, "@%s, cannot found available pg!!!", __func__);

    return OK;
}

int PSysPipeBase::handlePGParams(const ia_css_frame_format_type* frame_format_types)
{
    int fragmentCount = 1;
    int pgParamsSize = ia_css_sizeof_program_group_param(mProgramCount, mTerminalCount, fragmentCount);
    LOG1("pgParamsSize:%d", pgParamsSize);

    mPGParamsBuffer = createUserPtrCiprBuffer(pgParamsSize);
    Check(!mPGParamsBuffer, NO_MEMORY, "@%s, call createUserPtrCiprBuffer fail", __func__);

    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    int ret = ia_css_program_group_param_init(pgParamsBuf, mProgramCount, mTerminalCount, fragmentCount, frame_format_types);
    Check((ret != 0), UNKNOWN_ERROR, "@%s, call ia_css_program_group_param_init fail, ret:%d", __func__, ret);

    return OK;
}

// currently let the the bitmap to be the default value
int PSysPipeBase::setKernelBitMap()
{
    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);
    LOG1("%s: %#018lx", __func__, mKernelBitmap);
    int ret = ia_css_program_group_param_set_kernel_enable_bitmap(pgParamsBuf, mKernelBitmap);
    Check((ret != 0), -1, "@%s, call ia_css_program_group_param_set_kernel_enable_bitmap fail, ret:%d", __func__, ret);

    return OK;
}

ia_cipr_buffer_t* PSysPipeBase::createDMACiprBuffer(int size, int fd)
{
    uint32_t deviceFlags = IA_CIPR_MEMORY_HANDLE | IA_CIPR_MEMORY_NO_FLUSH;

    ia_cipr_memory_t mem;
    CLEAR(mem);
    mem.size = size;
    mem.flags = IA_CIPR_MEMORY_HANDLE | IA_CIPR_MEMORY_HW_ONLY;
    mem.handle = fd;
    mem.cpu_ptr = nullptr;
    ia_cipr_buffer_t* buf = ia_cipr_buffer_create(size, mem.flags | deviceFlags, &mem);
    Check(!buf, nullptr, "@%s, call ia_cipr_buffer_create fail", __func__);

    css_err_t ret = ia_cipr_memory_device_migrate_buffer(mMemoryDevice, buf);
    if (ret != css_err_none) {
        LOGE("@%s, call ia_cipr_memory_device_migrate_buffer fail, ret:%d", __func__, ret);
        ia_cipr_buffer_destroy(buf);
        return nullptr;
    }

    return buf;
}

ia_cipr_buffer_t* PSysPipeBase::createUserPtrCiprBuffer(int size, void* ptr)
{
    ia_cipr_buffer_t* buf = nullptr;
    if (ptr == nullptr) {
        buf = ia_cipr_buffer_create(size, IA_CIPR_MEMORY_ALLOCATE_CPU_PTR, nullptr);
    } else {
        ia_cipr_memory_t mem;
        CLEAR(mem);
        mem.size = size;
        mem.flags = IA_CIPR_MEMORY_CPU_PTR;
        mem.handle = 0;
        mem.cpu_ptr = ptr;
        buf = ia_cipr_buffer_create(size, IA_CIPR_MEMORY_CPU_PTR, &mem);
    }

    Check(!buf, nullptr, "@%s, call ia_cipr_buffer_create fail", __func__);

    css_err_t ret = ia_cipr_memory_device_migrate_buffer(mMemoryDevice, buf);
    if (ret != css_err_none) {
        LOGE("@%s, call ia_cipr_memory_device_migrate_buffer fail, ret:%d", __func__, ret);
        ia_cipr_buffer_destroy(buf);
        return nullptr;
    }

    return buf;
}

void* PSysPipeBase::getCiprBufferPtr(ia_cipr_buffer_t* buffer)
{
    Check(!buffer, nullptr, "@%s, invalid cipr buffer", __func__);

    ia_cipr_memory_t memory;
    CLEAR(memory);

    css_err_t ret = ia_cipr_buffer_get_memory(buffer, &memory);
    Check((ret != css_err_none), nullptr, "@%s, call ia_cipr_buffer_get_memory fail", __func__);

    return memory.cpu_ptr;
}

int PSysPipeBase::getCiprBufferSize(ia_cipr_buffer_t* buffer)
{
    Check(!buffer, BAD_VALUE, "@%s, invalid cipr buffer", __func__);

    ia_cipr_memory_t memory;
    CLEAR(memory);

    css_err_t ret = ia_cipr_buffer_get_memory(buffer, &memory);
    Check((ret != css_err_none), NO_MEMORY, "@%s, call ia_cipr_buffer_get_memory fail", __func__);

    return memory.size;
}

ia_cipr_buffer_t* PSysPipeBase::registerUserBuffer(int size, void* ptr)
{
    Check((size <= 0 || ptr == nullptr), nullptr, "Invalid parameter: size=%d ptr=%p", size, ptr);

    for (auto& item : mBuffers) {
        if (ptr == item.userPtr) {
            return item.ciprBuf;
        }
    }

    ia_cipr_buffer_t* ciprBuf = createUserPtrCiprBuffer(size, ptr);
    Check(!ciprBuf, nullptr, "Create CIPR buffer for %p failed", ptr);

    CiprBufferMapping bufMap;
    bufMap.userPtr = ptr;
    bufMap.ciprBuf = ciprBuf;
    mBuffers.push_back(bufMap);

    return ciprBuf;
}

ia_cipr_buffer_t* PSysPipeBase::registerUserBuffer(int size, int fd)
{
    Check((size <= 0 || fd < 0), nullptr, "Invalid parameter: size=%d fd=%d", size, fd);

    for (auto& item : mBuffers) {
        if (fd == item.userFd) {
            return item.ciprBuf;
        }
    }

    ia_cipr_buffer_t* ciprBuf = createDMACiprBuffer(size, fd);
    Check(!ciprBuf, nullptr, "Create CIPR buffer for fd %d failed", fd);

    CiprBufferMapping bufMap;
    bufMap.userFd = fd;
    bufMap.ciprBuf = ciprBuf;
    mBuffers.push_back(bufMap);

    return ciprBuf;
}

ia_cipr_buffer_t* PSysPipeBase::registerUserBuffer(int size, int offset, ia_cipr_buffer_t* baseCiprBuf)
{
    Check((size <= 0 || baseCiprBuf == nullptr || offset < 0), nullptr,
          "Invalid parameter: size=%d offset=%d baseCiprBuf=%p", size, offset, baseCiprBuf);

    for (auto& item : mBuffers) {
        if (baseCiprBuf == item.baseCiprBuf) {
            return item.ciprBuf;
        }
    }

    ia_cipr_buffer_t* ciprBuf = ia_cipr_buffer_create_region(baseCiprBuf, offset, size - offset);
    Check(!ciprBuf, nullptr, "Create CIPR buffer for baseCiprBuf %p", baseCiprBuf);

    CiprBufferMapping bufMap;
    bufMap.baseCiprBuf = baseCiprBuf;
    bufMap.ciprBuf = ciprBuf;
    mBuffers.push_back(bufMap);

    return ciprBuf;
}

ia_cipr_buffer_t* PSysPipeBase::registerUserBuffer(std::shared_ptr<CameraBuffer>& buf, int size)
{
    ia_cipr_buffer_t* ciprBuf = nullptr;

    if (buf->getMemory() == V4L2_MEMORY_DMABUF) {
        int fd = buf->getFd();
        ciprBuf = (fd >= 0) ? registerUserBuffer(size, fd) : nullptr;
    } else {
        void* addr = buf->getBufferAddr();
        ciprBuf = (addr != nullptr) ? registerUserBuffer(size, addr) : nullptr;
    }

    return ciprBuf;
}

void PSysPipeBase::printCommandConfig(int line)
{
    LOG2("@%s, line:%d, mCmdCfg.id:%lu", __func__, line, mCmdCfg.id);
    LOG2("@%s, line:%d, mCmdCfg.issue_id:%ld", __func__, line, mCmdCfg.issue_id);
    LOG2("@%s, line:%d, mCmdCfg.priority:%d", __func__, line, mCmdCfg.priority);
    LOG2("@%s, line:%d, mCmdCfg.psys_frequency:%d", __func__, line, mCmdCfg.psys_frequency);
    LOG2("@%s, line:%d, mCmdCfg.ext_buf:%p", __func__, line, mCmdCfg.ext_buf);
    LOG2("@%s, line:%d, mCmdCfg.pg:%p", __func__, line, mCmdCfg.pg);
    LOG2("@%s, line:%d, mCmdCfg.pg_params_buf:%p", __func__, line, mCmdCfg.pg_params_buf);
    LOG2("@%s, line:%d, mCmdCfg.pg_manifest_buf:%p", __func__, line, mCmdCfg.pg_manifest_buf);
    LOG2("@%s, line:%d, mCmdCfg.bufcount:%d", __func__, line, mCmdCfg.bufcount);
    for (int i = 0; i < (int)mCmdCfg.bufcount; i++) {
        LOG2("@%s, line:%d, mCmdCfg.buffers[%d]:%p", __func__, line, i, mCmdCfg.buffers[i]);
    }
}

ia_css_process_group_t* PSysPipeBase::preparePG()
{
    Check(!mPGBuffer, nullptr, "Invalid process group buffer");
    Check(!mManifestBuffer, nullptr, "Invalid pg manifest buffer");
    Check(!mPGParamsBuffer, nullptr, "Invalid pg parameter buffer");

    ia_css_process_group_t* processGroup = ia_css_process_group_create(
            getCiprBufferPtr(mPGBuffer),
            (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer),
            (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer));

    return processGroup;
}

int PSysPipeBase::handleCmd()
{
    Check((!mCmd), INVALID_OPERATION, "@%s, Command is invalid.", __func__);
    Check((!mProcessGroup), INVALID_OPERATION, "@%s, process group is invalid.", __func__);

    int bufferCount = ia_css_process_group_get_terminal_count(mProcessGroup);
    mCmdCfg.id = mPGId;
    mCmdCfg.priority = 1;
    mCmdCfg.pg_params_buf = mPGParamsBuffer;
    mCmdCfg.pg_manifest_buf = mManifestBuffer;
    mCmdCfg.pg = mPGBuffer;
    mCmdCfg.bufcount = bufferCount;

    for (int i = 0; i < bufferCount; i++) {
        ia_css_terminal_t *terminal = ia_css_process_group_get_terminal(mProcessGroup, i);
        mCmdCfg.buffers[i] = mTerminalBuffers[terminal->tm_index];
        LOG1("%s: terminal_count=%d, i=%d, terminal->tm_index=%d", __func__, bufferCount, i, terminal->tm_index);
    }

    css_err_t ret = ia_cipr_psys_set_command_config(mCmd, &mCmdCfg);
    Check((ret != css_err_none), UNKNOWN_ERROR, "@%s, call ia_cipr_psys_set_command_config fail", __func__);
    printCommandConfig(__LINE__);

    ret = ia_cipr_psys_get_command_config(mCmd, &mCmdCfg);
    Check((ret != css_err_none), UNKNOWN_ERROR, "@%s, call ia_cipr_psys_get_command_config fail", __func__);
    printCommandConfig(__LINE__);

    ret = ia_cipr_psys_queue_command(mCtx, mCmd);
    Check((ret != css_err_none), UNKNOWN_ERROR, "@%s, call ia_cipr_psys_queue_command fail", __func__);

    return OK;
}

int PSysPipeBase::handleEvent()
{
    css_err_t ret;
    ia_cipr_psys_event_config_t eventCfg;
    CLEAR(eventCfg);
    eventCfg.timeout = 5000;

    ia_cipr_psys_event_t event = ia_cipr_psys_create_event(&eventCfg);
    Check(!event, UNKNOWN_ERROR, "@%s, call create_event fail", __func__);

    ret = ia_cipr_psys_wait_for_event(mCtx, event);
    CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipr_psys_destroy_event(event), "@%s, call wait_for_event fail, ret:%d", __func__, ret);

    ret = ia_cipr_psys_get_event_config(event, &eventCfg);
    CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipr_psys_destroy_event(event), "@%s, call get_event_config fail, ret:%d", __func__, ret);
    // Ignore the error in event config since it's not a fatal error.
    if (eventCfg.error) {
        LOG1("%s, event config error: %d", __func__, eventCfg.error);
    }

    ia_cipr_psys_destroy_event(event);

    return OK;
}

void PSysPipeBase::setInputInfo(const FrameInfoPortMap& inputInfos)
{
    mSrcFrame = inputInfos;
}

void PSysPipeBase::setOutputInfo(const FrameInfoPortMap& outputInfos)
{
    mDstFrame = outputInfos;
}

int PSysPipeBase::prepare()
{
    int ret = handlePGParams(mFrameFormatType);
    Check((ret != OK), ret, "@%s, call handlePGParams fail", __func__);

    ret = setKernelBitMap();
    Check((ret != OK), ret, "@%s, call setKernelBitMap fail", __func__);

    ret = setTerminalParams(mFrameFormatType);
    Check((ret != OK), ret, "@%s, call setTerminalParams fail", __func__);

    ia_css_program_group_manifest_t* manifestBuf =
        (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer);

    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    size_t pgSize = ia_css_sizeof_process_group(manifestBuf, pgParamsBuf);
    LOG1("%s pgSize=%zu", __func__, pgSize);
    mPGBuffer = createUserPtrCiprBuffer(pgSize);
    Check(!mPGBuffer, NO_MEMORY, "@%s, call createUserPtrCiprBuffer fail", __func__);

    ia_css_process_group_t* processGroup = preparePG();
    Check(!processGroup, UNKNOWN_ERROR, "Create process group failed.");
    mProcessGroup = processGroup;

    set_program_terminals(processGroup, manifestBuf, mSrcFrame[MAIN_PORT].mWidth, mSrcFrame[MAIN_PORT].mHeight, 1);

    if (mNeedP2p) {
        mP2p->setTerminalCount(mTerminalCount);
        mP2p->setPgManifest((ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer));
        mP2p->setProcessGroup(processGroup);
        ia_p2p_fragment_desc fragmentDesc;
        CLEAR(fragmentDesc);
        if (mPsysParam.fragmentDesc.fragment_width == 0 &&
            mPsysParam.fragmentDesc.fragment_height == 0) {
            fragmentDesc.fragment_width = mSrcFrame[MAIN_PORT].mWidth;
            fragmentDesc.fragment_height = mSrcFrame[MAIN_PORT].mHeight;
            fragmentDesc.fragment_start_x = 0;
            fragmentDesc.fragment_start_y = 0;
        } else {
            fragmentDesc.fragment_width = mPsysParam.fragmentDesc.fragment_width;
            fragmentDesc.fragment_height = mPsysParam.fragmentDesc.fragment_height;
            fragmentDesc.fragment_start_x = 0;
            fragmentDesc.fragment_start_y = 0;
        }

        mP2p->prepareP2p(mPlatform, fragmentDesc, mPsysParam.dvsMorphTable);
        mP2p->prepareRequirements();
    }

    return OK;
}

int PSysPipeBase::iterate(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                          vector<std::shared_ptr<CameraBuffer>>& dstBufs)
{
    LOG1("%s", __func__);

    int ret = prepareTerminalBuffers(srcBufs, dstBufs);
    Check((ret != OK), ret, "@%s, prepareTerminalBuffers fail with %d", __func__, ret);

    ia_css_process_group_t* processGroup = preparePG();
    Check(!processGroup, UNKNOWN_ERROR, "@%s, failed to prepare process group", __func__);

    ret = handleCmd();
    Check((ret != OK), ret, "@%s, call handleCmd fail", __func__);

    ret = handleEvent();
    Check((ret != OK), ret, "@%s, call handleEvent fail", __func__);

    return OK;
}

} // namespace icamera
