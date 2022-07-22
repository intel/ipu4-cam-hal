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

#include "PSysP2pLite.h"

#include "iutils/Errors.h"
#include "CameraBuffer.h"
#include <vector>

extern "C" {

#include <ia_cipr/ia_cipr_psys_android.h>
#include <ia_cipr/ia_cipr_psys.h>
#include <ia_cipr/ia_cipr_alloc.h>
#include <ia_cipr/ia_cipr_memory.h>
#include <ia_tools/ia_macros.h>

#include <ia_css_psys_program_group_manifest.h>
#include <ia_css_psys_terminal_manifest.h>
#include <ia_css_program_group_data.h>
#include <ia_css_program_group_param.h>
#include <ia_css_psys_process_group.h>
#include <ia_css_psys_terminal.h>
#include <ia_css_terminal_types.h>
#include <ia_css_terminal_manifest_types.h>
}

namespace icamera {

struct PsysParams{
    ia_p2p_fragment_desc fragmentDesc;
    ia_dvs_morph_table *dvsMorphTable;
};

struct FrameInfo {
    int mWidth;
    int mHeight;
    int mFormat;
    int mStride;
    int mBpp;
};

typedef std::map<Port, FrameInfo> FrameInfoPortMap;

class PSysPipeBase {
public:
    static ia_css_frame_format_type getCssFmt(int v4l2Fmt);

    PSysPipeBase(int pgId);
    virtual ~PSysPipeBase();

    virtual void setInputInfo(const FrameInfoPortMap& inputInfos);
    virtual void setOutputInfo(const FrameInfoPortMap& outputInfos);
    virtual int prepare();
    virtual int iterate(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                vector<std::shared_ptr<CameraBuffer>>& dstBufs);
private:
    DISALLOW_COPY_AND_ASSIGN(PSysPipeBase);

protected:
    virtual int prepareTerminalBuffers(vector<std::shared_ptr<CameraBuffer>>& srcBufs,
                                       vector<std::shared_ptr<CameraBuffer>>& dstBufs) = 0;
    virtual int setTerminalParams(const ia_css_frame_format_type* frame_format_types) { return OK; }

    ia_cipr_buffer_t* createDMACiprBuffer(int size, int fd);
    ia_cipr_buffer_t* createUserPtrCiprBuffer(int size, void* ptr = nullptr);
    void* getCiprBufferPtr(ia_cipr_buffer_t* buffer);
    int getCiprBufferSize(ia_cipr_buffer_t* buffer);

    ia_cipr_buffer_t* registerUserBuffer(int size, void* ptr);
    ia_cipr_buffer_t* registerUserBuffer(int size, int fd);
    ia_cipr_buffer_t* registerUserBuffer(int size, int offset, ia_cipr_buffer_t* baseCiprBuf);
    ia_cipr_buffer_t* registerUserBuffer(std::shared_ptr<CameraBuffer>& buf, int size);

    int getCapability();
    int getManifest(int pgId);
    int handlePGParams(const ia_css_frame_format_type* frame_format_types);
    int setKernelBitMap();

    ia_css_process_group_t* preparePG();
    int handleCmd();
    int handleEvent();

    // debug purpose
    void printCommandConfig(int line);

protected:
    ia_cipr_psys_context_t mCtx;
    ia_cipr_memory_device_t* mMemoryDevice;

    ia_cipr_buffer_t* mManifestBuffer;
    ia_cipr_buffer_t* mPGParamsBuffer;
    ia_cipr_buffer_t* mPGBuffer;
    ia_cipr_buffer_t** mTerminalBuffers;

    int mPGId;
    int mPGCount;
    ia_p2p_platform_t mPlatform;
    int mProgramCount;
    int mTerminalCount;
    int mManifestSize;
    ia_css_process_group_t* mProcessGroup;
    ia_css_kernel_bitmap_t mKernelBitmap;

    FrameInfoPortMap mSrcFrame;
    FrameInfoPortMap mDstFrame;

    struct CiprBufferMapping {
        void* userPtr;
        int userFd;
        ia_cipr_buffer_t* baseCiprBuf;
        ia_cipr_buffer_t* ciprBuf;
        CiprBufferMapping() : userPtr(nullptr), userFd(-1), baseCiprBuf(nullptr), ciprBuf(nullptr) {}
    };
    std::vector<CiprBufferMapping> mBuffers;

    ia_cipr_psys_command_t mCmd;
    ia_cipr_psys_command_config_t mCmdCfg;

    ia_css_frame_format_type* mFrameFormatType;

    PsysParams mPsysParam;

    PSysP2pLite* mP2p;
    bool mNeedP2p;
};

} //namespace icamera
