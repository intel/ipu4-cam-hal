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

#pragma once

#include "PGParamAdapt.h"
#include "IspParamAdaptor.h"
#include "BufferQueue.h"
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

struct FrameInfo {
    FrameInfo() {}
    int mWidth = 0;
    int mHeight = 0;
    int mFormat = 0;
    int mStride = 0;
    int mBpp = 0;
};

typedef std::map<Port, FrameInfo> FrameInfoPortMap;
typedef std::map<Port, shared_ptr<CameraBuffer>> CameraBufferPortMap;

/**
 * \class PGBase
 *
 * \brief This is a version PG implementation which is used to config and run PG.
 *
 * The call sequence as follows:
 * 1. init();
 * 2. setInputInfo();
 * 3. setOutputInfo();
 * 4. prepare();
 * 5. loop frame {
 *        iterate();
 *    }
 * 6. deInit();
 */
class PGBase{

public:
    static ia_css_frame_format_type getCssFmt(int v4l2Fmt);
    PGBase(int pgId);
    virtual ~PGBase();

    /**
     * allocate memory for some variables.
     */
    int init();

    /**
     * recycle memory.
     */
    void deInit();

    /**
     * set the input buffer info.
     */
    virtual void setInputInfo(FrameInfoPortMap inputInfos);

    /**
     * set the output buffers info.
     */
    virtual void setOutputInfo(FrameInfoPortMap outputInfos);

    /**
    * config the data terminals, init, config and prepare p2p, create process group.
    */
    virtual int prepare(IspParamAdaptor* adaptor);

    /**
    * run p2p to encode the params terminals, execute the PG and run p2p to decode the statistic terminals.
    */
    virtual int iterate(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf,
                       ia_binary_data *statistics, const ia_binary_data *ipuParameters) { return OK; }

private:
    DISALLOW_COPY_AND_ASSIGN(PGBase);

protected:
    virtual int configTerminal() { return OK; }
    virtual int prepareTerminalBuffers(CameraBufferPortMap &inBuf, CameraBufferPortMap &outBuf) { return OK; }
    virtual int setTerminalParams(const ia_css_frame_format_type* frameFormatTypes) { return OK; }
    virtual int decodeStats(ia_binary_data *statistics) { return OK; }

    ia_cipr_buffer_t* createDMACiprBuffer(int size, int fd);
    ia_cipr_buffer_t* createUserPtrCiprBuffer(int size, void* ptr = nullptr);
    void* getCiprBufferPtr(ia_cipr_buffer_t* buffer);
    int getCiprBufferSize(ia_cipr_buffer_t* buffer);
    ia_cipr_buffer_t* registerUserBuffer(int size, void* ptr);
    ia_cipr_buffer_t* registerUserBuffer(int size, int fd);
    int getCapability();
    int getManifest(int pgId);
    int handlePGParams(const ia_css_frame_format_type* frameFormatTypes);
    int setKernelBitMap();
    int handleCmd();
    int handleEvent();
    void dumpTerminalPyldAndDesc(int pgId, long sequence);

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
    int mFragmentCount;
    static const int timeout = 5000;

    FrameInfoPortMap mSrcFrame;
    FrameInfoPortMap mDstFrame;

    struct CiprBufferMapping {
        CiprBufferMapping() {}
        void* userPtr = nullptr;
        int userFd = -1;
        ia_cipr_buffer_t* baseCiprBuf = nullptr;
        ia_cipr_buffer_t* ciprBuf = nullptr;
    };
    std::vector<CiprBufferMapping> mBuffers;

    ia_cipr_psys_command_t mCmd;
    ia_cipr_psys_command_config_t mCmdCfg;

    ia_css_frame_format_type* mFrameFormatType;
    std::vector<int> mDisableDataTermials;
    PGParamAdapt* mPGParamAdapt;
};

} //namespace icamera