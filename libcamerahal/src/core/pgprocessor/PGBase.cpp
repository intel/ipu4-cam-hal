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

#define LOG_TAG "PGBase"

#include "PGBase.h"

#include <stdint.h>

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"

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
    { V4L2_PIX_FMT_SGRBG12, IA_CSS_DATA_FORMAT_RAW },
};

ia_css_frame_format_type PGBase::getCssFmt(int v4l2Fmt) {
    int size = ARRAY_SIZE(sFormatMapping);
    for (int i = 0; i < size; i++) {
        if (sFormatMapping[i].v4l2Fmt == v4l2Fmt) {
            return sFormatMapping[i].cssFmt;
        }
    }

    LOGW("%s: unsupported v4l2 pixel format: %s", __func__, CameraUtils::format2string(v4l2Fmt));
    return IA_CSS_N_FRAME_FORMAT_TYPES;
}

PGBase::PGBase(int pgId):
    mMemoryDevice(nullptr),
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
    mFragmentCount(0),
    mFrameFormatType(nullptr),
    mPGParamAdapt(nullptr)
{
    CLEAR(mCmdCfg);
    CLEAR(mCtx);
    CLEAR(mCmd);
}

PGBase::~PGBase()
{

}

int PGBase::init()
{
    mDisableDataTermials.clear();
    mPGParamAdapt = new PGParamAdapt(mPGId);

    mCtx = ia_cipr_psys_create_context(nullptr);
    mMemoryDevice = ia_cipr_psys_get_memory_device(mCtx);
    int ret = getCapability();
    if (ret != OK) return ret;

    // create mManifestBuffer
    ret = getManifest(mPGId);
    if (ret != OK) return ret;

    mTerminalBuffers = (ia_cipr_buffer_t**)IA_CIPR_CALLOC(mTerminalCount, sizeof(ia_cipr_buffer_t*));
    Check(!mTerminalBuffers, NO_MEMORY, "Allocate terminal buffers fail");
    memset(mTerminalBuffers, 0, (mTerminalCount * sizeof(ia_cipr_buffer_t*)));
    CLEAR(mCmdCfg);
    mCmdCfg.bufcount = mTerminalCount;
    mCmd = ia_cipr_psys_create_command(&mCmdCfg);
    ia_cipr_psys_get_command_config(mCmd, &mCmdCfg);

    return ret;
}

void PGBase::deInit()
{
    mDisableDataTermials.clear();
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

    mPGParamAdapt->deinit();
    delete mPGParamAdapt;
}

void PGBase::setInputInfo(FrameInfoPortMap inputInfos)
{
    for (const auto& item : inputInfos) {
        Port port = item.first;
        FrameInfo frameInfo;
        frameInfo.mWidth = item.second.mWidth;
        frameInfo.mHeight = item.second.mHeight;
        frameInfo.mFormat = item.second.mFormat;
        frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
        frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
        mSrcFrame[port] = frameInfo;
        LOG1("%s, mSrcFrame port %d width:%d, height:%d, fmt:%s, bpp:%d, stride:%d", __func__, port,
                frameInfo.mWidth, frameInfo.mHeight, CameraUtils::format2string(frameInfo.mFormat),
                frameInfo.mBpp, frameInfo.mStride);
    }
}

void PGBase::setOutputInfo(FrameInfoPortMap outputInfos)
{
    for (const auto& item : outputInfos) {
        Port port = item.first;
        FrameInfo frameInfo;
        frameInfo.mWidth = item.second.mWidth;
        frameInfo.mHeight = item.second.mHeight;
        frameInfo.mFormat = item.second.mFormat;
        frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
        frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
        mDstFrame[port] = frameInfo;
        LOG1("%s, mDstFrame port %d width:%d, height:%d, fmt:%s, bpp:%d, stride:%d", __func__, port,
                frameInfo.mWidth, frameInfo.mHeight, CameraUtils::format2string(frameInfo.mFormat),
                frameInfo.mBpp, frameInfo.mStride);
    }
}


int PGBase::prepare(IspParamAdaptor* adaptor)
{
    // set the data terminal frame format and add disable data terminal to mDisableDataTermials
    int ret = configTerminal();
    Check((ret != OK), ret, "%s, call configTerminal fail", __func__);

    // TODO: add fragment support
    ia_p2p_fragment_desc fragmentDesc;
    fragmentDesc.fragment_width = mSrcFrame[MAIN_PORT].mWidth;
    fragmentDesc.fragment_height = mSrcFrame[MAIN_PORT].mHeight;
    fragmentDesc.fragment_start_x = 0;
    fragmentDesc.fragment_start_y = 0;
    mFragmentCount = 1;

    ia_css_program_group_manifest_t* manifestBuf =
        (ia_css_program_group_manifest_t*)getCiprBufferPtr(mManifestBuffer);

    PgConfiguration config;
    config.pgManifest = manifestBuf;
    config.disableDataTermials = mDisableDataTermials;
    config.fragmentDesc = fragmentDesc;
    config.fragmentCount = mFragmentCount;

    // init and config p2p handle
    ret = mPGParamAdapt->init(mPlatform, config);
    Check((ret != OK), ret, "%s, init p2p fail", __func__);

    // query and save the requirement for each terminal, get the final kernel bitmap
    ret = mPGParamAdapt->prepare(adaptor->getIpuParameter(), &mKernelBitmap);
    Check((ret != OK), ret, "%s, prepare p2p fail", __func__);

    ret = handlePGParams(mFrameFormatType);
    Check((ret != OK), ret, "%s, call handlePGParams fail", __func__);

    ret = setKernelBitMap();
    Check((ret != OK), ret, "%s, call setKernelBitMap fail", __func__);

    ia_css_program_group_param_t* pgParamsBuf =
        (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);

    size_t pgSize = ia_css_sizeof_process_group(manifestBuf, pgParamsBuf);
    LOG1("%s process group size is %zu", __func__, pgSize);

    mPGBuffer = createUserPtrCiprBuffer(pgSize);
    Check(!mPGBuffer, NO_MEMORY, "%s, call createUserPtrCiprBuffer fail", __func__);

    return ret;
}

int PGBase::getCapability()
{
    ia_cipr_psys_capability_t cap;
    int ret = OK;
    css_err_t err = ia_cipr_psys_get_capabilities(mCtx, &cap);
    Check((err != css_err_none), UNKNOWN_ERROR, "Call ia_cipr_psys_get_capabilities fail, ret:%d", ret);

    LOG1("%s: capability.version:%d", __func__, cap.version);
    LOG1("%s: capability.driver:%s", __func__, cap.driver);
    LOG1("%s: capability.dev_model:%s", __func__, cap.dev_model);
    LOG1("%s: capability.program_group_count:%d", __func__, cap.program_group_count);
    mPGCount = cap.program_group_count;

    if (strncmp((char *)cap.dev_model, "ipu4p", 5) == 0) {
        mPlatform = IA_P2P_PLATFORM_CNL_B0;
        LOG1("%s: cnl/icl/ksl shared the same p2p platform id", __func__);
    } else if (strncmp((char *)cap.dev_model, "ipu4", 4) == 0) {
        switch (cap.dev_model[13]) {
            case 'B':
                 mPlatform = IA_P2P_PLATFORM_BXT_B0;
                 break;
            default:
                 LOGE("%s: unsupported psys device model :%s", __func__, cap.dev_model);
                 ret = BAD_VALUE;
                 break;
        }
    } else {
        LOGE("%s: unsupported psys device model : %s", __func__, cap.dev_model);
        ret = BAD_VALUE;
    }

    return ret;
}

int PGBase::getManifest(int pgId)
{
    int i = 0;
    LOG1("%s: get manifest for pgId: %d", __func__, pgId);

    for (; i < mPGCount; i++) {
        ia_cipr_buffer_t* manifestBuffer = nullptr;
        int programCount = 0;
        int terminalCount = 0;
        int programGroupId = 0;
        int manifestSize = 0;
        ia_css_kernel_bitmap_t kernelBitmap = ia_css_kernel_bitmap_clear();
        uint32_t size = 0;

        css_err_t ret = ia_cipr_psys_get_manifest(mCtx, i, &size, nullptr);
        if (ret != css_err_none) continue;
        Check((size == 0), UNKNOWN_ERROR, "%s, the manifest size is 0", __func__);

        manifestBuffer = createUserPtrCiprBuffer(size);
        Check(!manifestBuffer, NO_MEMORY, "%s, call createUserPtrCiprBuffer fail", __func__);

        void* manifest = getCiprBufferPtr(manifestBuffer);
        LOG1("%s: manifest's cpuptr is:%p", __func__, manifest);

        ret = ia_cipr_psys_get_manifest(mCtx, i, &size, manifest);
        Check((ret != css_err_none), UNKNOWN_ERROR, "%s, call ia_cipr_psys_get_manifest fail", __func__);
        LOG1("%s: pg index: %d, manifest size: %u", __func__, i, size);

        const ia_css_program_group_manifest_t *mf = (const ia_css_program_group_manifest_t*)manifest;
        programCount = ia_css_program_group_manifest_get_program_count(mf);
        terminalCount = ia_css_program_group_manifest_get_terminal_count(mf);
        programGroupId = ia_css_program_group_manifest_get_program_group_ID(mf);
        manifestSize = ia_css_program_group_manifest_get_size(mf);
        kernelBitmap = ia_css_program_group_manifest_get_kernel_bitmap(mf);

        LOG1("%s: pgIndex: %d, programGroupId: %d, manifestSize: %d, programCount: %d, terminalCount: %d, kernelBitmap: %#018lx",
                __func__, i, programGroupId, manifestSize, programCount, terminalCount, ia_css_kernel_bitmap_to_uint64(kernelBitmap));

        if (pgId == programGroupId) {
            LOG1("%s: manifest for pg id %d found at index: %d", __func__, pgId, i);
            mProgramCount = programCount;
            mTerminalCount = terminalCount;
            mManifestSize = manifestSize;
            mKernelBitmap = kernelBitmap;
            mManifestBuffer = manifestBuffer;
            break;
        }

        ia_cipr_buffer_destroy(manifestBuffer);
    }

    Check((i == mPGCount), BAD_VALUE, "%s, Can't found available pg: %d", __func__, pgId);

    return OK;
}

int PGBase::handlePGParams(const ia_css_frame_format_type* frameFormatTypes)
{
    int pgParamsSize = ia_css_sizeof_program_group_param(mProgramCount, mTerminalCount, mFragmentCount);
    LOG1("%s: pgParamsSize: %d", __func__, pgParamsSize);

    mPGParamsBuffer = createUserPtrCiprBuffer(pgParamsSize);
    Check(!mPGParamsBuffer, NO_MEMORY, "%s, call createUserPtrCiprBuffer fail", __func__);

    ia_css_program_group_param_t* pgParamsBuf = (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);
    int ret = ia_css_program_group_param_init(pgParamsBuf, mProgramCount, mTerminalCount, mFragmentCount, frameFormatTypes);
    Check((ret != OK), ret, "%s, call ia_css_program_group_param_init fail", __func__);

    return ret;
}

int PGBase::setKernelBitMap()
{
    ia_css_program_group_param_t* pgParamsBuf = (ia_css_program_group_param_t*)getCiprBufferPtr(mPGParamsBuffer);
    LOG1("%s: mKernelBitmap: %#018lx", __func__, mKernelBitmap);
    int ret = ia_css_program_group_param_set_kernel_enable_bitmap(pgParamsBuf, mKernelBitmap);
    Check((ret != OK), ret, "%s, call ia_css_program_group_param_set_kernel_enable_bitmap fail", __func__);

    return ret;
}

ia_cipr_buffer_t* PGBase::createDMACiprBuffer(int size, int fd)
{
    uint32_t deviceFlags = IA_CIPR_MEMORY_HANDLE | IA_CIPR_MEMORY_NO_FLUSH;

    ia_cipr_memory_t mem;
    mem.size = size;
    mem.flags = IA_CIPR_MEMORY_HANDLE | IA_CIPR_MEMORY_HW_ONLY;
    mem.handle = fd;
    mem.cpu_ptr = nullptr;
    mem.anchor = nullptr;
    ia_cipr_buffer_t* buf = ia_cipr_buffer_create(size, mem.flags | deviceFlags, &mem);
    Check(!buf, nullptr, "%s, call ia_cipr_buffer_create fail", __func__);

    css_err_t ret = ia_cipr_memory_device_migrate_buffer(mMemoryDevice, buf);
    if (ret != css_err_none) {
        LOGE("%s, call ia_cipr_memory_device_migrate_buffer fail", __func__);
        ia_cipr_buffer_destroy(buf);
        return nullptr;
    }

    return buf;
}

ia_cipr_buffer_t* PGBase::createUserPtrCiprBuffer(int size, void* ptr)
{
    ia_cipr_buffer_t* buf = nullptr;
    if (ptr == nullptr) {
        buf = ia_cipr_buffer_create(size, IA_CIPR_MEMORY_ALLOCATE_CPU_PTR, nullptr);
    } else {
        ia_cipr_memory_t mem;
        mem.size = size;
        mem.flags = IA_CIPR_MEMORY_CPU_PTR;
        mem.handle = 0;
        mem.cpu_ptr = ptr;
        mem.anchor = nullptr;
        buf = ia_cipr_buffer_create(size, IA_CIPR_MEMORY_CPU_PTR, &mem);
    }

    Check(!buf, nullptr, "%s, call ia_cipr_buffer_create fail", __func__);

    css_err_t ret = ia_cipr_memory_device_migrate_buffer(mMemoryDevice, buf);
    if (ret != css_err_none) {
        LOGE("%s, call ia_cipr_memory_device_migrate_buffer fail", __func__);
        ia_cipr_buffer_destroy(buf);
        return nullptr;
    }

    return buf;
}

void* PGBase::getCiprBufferPtr(ia_cipr_buffer_t* buffer)
{
    Check(!buffer, nullptr, "%s, invalid cipr buffer", __func__);

    ia_cipr_memory_t memory;

    css_err_t ret = ia_cipr_buffer_get_memory(buffer, &memory);
    Check((ret != css_err_none), nullptr, "%s, call ia_cipr_buffer_get_memory fail", __func__);

    return memory.cpu_ptr;
}

int PGBase::getCiprBufferSize(ia_cipr_buffer_t* buffer)
{
    Check(!buffer, BAD_VALUE, "%s, invalid cipr buffer", __func__);

    ia_cipr_memory_t memory;

    css_err_t ret = ia_cipr_buffer_get_memory(buffer, &memory);
    Check((ret != css_err_none), NO_MEMORY, "%s, call ia_cipr_buffer_get_memory fail", __func__);

    return memory.size;
}

ia_cipr_buffer_t* PGBase::registerUserBuffer(int size, void* ptr)
{
    Check((size <= 0 || ptr == nullptr), nullptr, "Invalid parameter: size=%d, ptr=%p", size, ptr);

    for (auto& item : mBuffers) {
        if (ptr == item.userPtr) {
            return item.ciprBuf;
        }
    }

    ia_cipr_buffer_t* ciprBuf = createUserPtrCiprBuffer(size, ptr);
    Check(!ciprBuf, nullptr, "Create cipr buffer for %p failed", ptr);

    CiprBufferMapping bufMap;
    bufMap.userPtr = ptr;
    bufMap.ciprBuf = ciprBuf;
    mBuffers.push_back(bufMap);

    return ciprBuf;
}

ia_cipr_buffer_t* PGBase::registerUserBuffer(int size, int fd)
{
    Check((size <= 0 || fd < 0), nullptr, "Invalid parameter: size: %d, fd: %d", size, fd);

    for (auto& item : mBuffers) {
        if (fd == item.userFd) {
            return item.ciprBuf;
        }
    }

    ia_cipr_buffer_t* ciprBuf = createDMACiprBuffer(size, fd);
    Check(!ciprBuf, nullptr, "Create cipr buffer for fd %d failed", fd);

    CiprBufferMapping bufMap;
    bufMap.userFd = fd;
    bufMap.ciprBuf = ciprBuf;
    mBuffers.push_back(bufMap);

    return ciprBuf;
}

int PGBase::handleCmd()
{
    Check((!mCmd), INVALID_OPERATION, "%s, Command is invalid.", __func__);
    Check((!mProcessGroup), INVALID_OPERATION, "%s, process group is invalid.", __func__);

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
        LOG1("%s: buffer count: %d, buffer index: %d, terminal index: %d", __func__, bufferCount, i, terminal->tm_index);
    }

    css_err_t ret = ia_cipr_psys_set_command_config(mCmd, &mCmdCfg);
    Check((ret != css_err_none), UNKNOWN_ERROR, "%s, call ia_cipr_psys_set_command_config fail", __func__);

    ret = ia_cipr_psys_get_command_config(mCmd, &mCmdCfg);
    Check((ret != css_err_none), UNKNOWN_ERROR, "%s, call ia_cipr_psys_get_command_config fail", __func__);

    ret = ia_cipr_psys_queue_command(mCtx, mCmd);
    Check((ret != css_err_none), UNKNOWN_ERROR, "%s, call ia_cipr_psys_queue_command fail", __func__);

    return OK;
}

int PGBase::handleEvent()
{
    css_err_t ret;
    ia_cipr_psys_event_config_t eventCfg;
    eventCfg.timeout = timeout;

    ia_cipr_psys_event_t event = ia_cipr_psys_create_event(&eventCfg);
    Check(!event, UNKNOWN_ERROR, "%s, call create_event fail", __func__);

    ret = ia_cipr_psys_wait_for_event(mCtx, event);
    CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipr_psys_destroy_event(event), "%s, call wait_for_event fail, ret: %d", __func__, ret);

    ret = ia_cipr_psys_get_event_config(event, &eventCfg);
    CheckAndClean((ret != css_err_none), UNKNOWN_ERROR, ia_cipr_psys_destroy_event(event), "%s, call get_event_config fail, ret: %d", __func__, ret);
    // Ignore the error in event config since it's not a fatal error.
    if (eventCfg.error) {
        LOGW("%s, event config error: %d", __func__, eventCfg.error);
    }

    ia_cipr_psys_destroy_event(event);

    return OK;
}

void PGBase::dumpTerminalPyldAndDesc(int pgId, long sequence)
{
    if (!CameraDump::isDumpTypeEnable(DUMP_PSYS_PG)) return;

    char fileName[MAX_NAME_LEN] = {'\0'};
    int terminalCount = ia_css_process_group_get_terminal_count(mProcessGroup);
    for (int i = 0; i < terminalCount; i++) {
        ia_css_terminal_t *terminal = ia_css_process_group_get_terminal(mProcessGroup, i);
        ia_css_param_terminal_t *paramterminal = (ia_css_param_terminal_t *)terminal;

        snprintf(fileName, (MAX_NAME_LEN - 1), "pg_%d_frame_%ld_%s_tidx#%d.bin", pgId, sequence, "desc", terminal->tm_index);
        CameraDump::writeData(paramterminal, paramterminal->base.size, fileName);

        snprintf(fileName, (MAX_NAME_LEN - 1), "pg_%d_frame_%ld_%s_tidx#%d.bin", pgId, sequence, "pyld", terminal->tm_index);
        void* ptr = getCiprBufferPtr(mTerminalBuffers[terminal->tm_index]);
        int size = getCiprBufferSize(mTerminalBuffers[terminal->tm_index]);
        CameraDump::writeData(ptr, PAGE_ALIGN(size), fileName);
    }
}

} // namespace icamera
