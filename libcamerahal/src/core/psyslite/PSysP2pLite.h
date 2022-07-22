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

extern "C" {
#include "ia_isp_types.h"
#include "ia_isp_bxt.h"

#include "ia_pal_types_isp_ids_autogen.h"

#include <ia_p2p_types.h>
#include <ia_p2p.h>

#include <ia_cipr/ia_cipr_psys_android.h>
#include <ia_cipr/ia_cipr_psys.h>
#include <ia_cipr/ia_cipr_alloc.h>
#include <ia_cipr/ia_cipr_memory.h>
#include <ia_tools/ia_macros.h>
#include <ia_tools/css_types.h>

#include <ia_css_psys_program_group_manifest.h>
#include <ia_css_psys_terminal_manifest.h>
#include <ia_css_program_group_data.h>
#include <ia_css_program_group_param.h>
#include <ia_css_psys_process_group.h>
#include <ia_css_psys_terminal.h>
#include <ia_css_terminal_types.h>
#include <ia_css_terminal_manifest_types.h>

#include "ia_camera/ipu_process_group_wrapper.h"
}

#include "iutils/Utils.h"

#include <map>

namespace icamera {

/**
 * \class PSysP2pLite
 *
 * \brief This is a lite version P2P implementation which is used to encode parameter terminal
 *        for lite PSYS pipeline, such as CscPipeline, ScalingPipeline etc.
 */
class PSysP2pLite {

public:
    PSysP2pLite(int pgId);
    ~PSysP2pLite();

    /**
     * Use to init ISP and P2P handle.
     */
    int prepareP2p(ia_p2p_platform_t platform, const ia_p2p_fragment_desc &fragmentDesc, ia_dvs_morph_table *dvsMorphTable);

    /**
     * Query and save the requirement for each terminal.
     */
    int prepareRequirements();

    /**
     * Get payload size for the given terminal index.
     */
    int getPayloadSize(int terminalIndex, unsigned int *payloadSize);

    /**
     * Update PAL to provide new P2P data.
     */
    int updatePAL(ia_dvs_morph_table *dvsMorphTable);

    /**
     * Encode payload data for the gaven terminal index.
     */
    int encode(int terminalIndex, ia_binary_data payload);

    int setKernelConfig(int count, ia_isp_bxt_run_kernels_t* kernels);
    void setProcessGroup(ia_css_process_group_t* processGroup) { mProcessGroup = processGroup; }
    void setPgManifest(ia_css_program_group_manifest_t* pgManifest) { mPgManifest = pgManifest; }
    void setTerminalCount(int terminalCount) { mTerminalCount = terminalCount; }

private:
    struct IpuPgTerminalKernelInfo {
        uint8_t  id;
    };

    struct IpuPgTerminaRequirements {
        ia_css_terminal_type_t type;
        uint32_t payloadSize;
        ia_css_kernel_bitmap_t kernelBitmap;
        uint32_t sectionCount;
        IpuPgTerminalKernelInfo *kernelOrder;
    };

    struct IpuPgRequirements {
        uint32_t terminalCount;
        IpuPgTerminaRequirements terminals[IPU_MAX_TERMINAL_COUNT];
    };

private:
    DISALLOW_COPY_AND_ASSIGN(PSysP2pLite);

    int getKernelIdByBitmap(ia_css_kernel_bitmap_t bitmap);

    css_err_t getKernelOrderForProgramTerm(ia_css_program_terminal_manifest_t *terminalManifest,
                                          IpuPgTerminalKernelInfo *kernelOrder);
    css_err_t getKernelOrderForParamCachedInTerm(ia_css_param_terminal_manifest_t *terminalManifest,
                                                IpuPgTerminalKernelInfo *kernelOrder);
    int8_t terminalEnumerateByType(IpuPgRequirements* reqs,
                                   ia_css_terminal_type_t terminalType,
                                   uint8_t num);
    int8_t terminalEnumerateByBitmap(IpuPgRequirements* reqs,
                               ia_css_terminal_type_t terminal_type,
                               ia_css_kernel_bitmap_t bitmap);
    bool isKernelIdInKernelOrder(IpuPgRequirements* reqs,int8_t termIndex,
                                 int kernelId,uint8_t *orderedIndex);
    uint32_t getKernelCountFromKernelOrder(IpuPgRequirements* reqs, int8_t termIndex, int kernelId);
    void processTerminalKernelRequirements(IpuPgRequirements* reqs, int8_t termIndex,
                                           ia_css_terminal_type_t terminalType, int kernelId);

private:
    static const int MAX_STATISTICS_WIDTH = 80;
    static const int MAX_STATISTICS_HEIGHT = 60;
    static const int MAX_NUM_OF_STATS_PER_FRAME = 1;

    int mPgId;
    int mTerminalCount;

    ia_p2p_fragment_desc mFragmentDesc;

    ia_isp_bxt_t* mIspHandle;
    ia_binary_data mCurrentIpuParam;
    ia_isp_bxt_program_group mKernelGroup;

    ia_p2p_handle mP2pHandle;
    ia_binary_data mP2pCacheBuffer;

    ia_css_process_group_t* mProcessGroup;
    ia_css_program_group_manifest_t* mPgManifest;

    struct KernelRequirement {
        KernelRequirement() { CLEAR(*this); }
        ia_p2p_terminal_requirements_t mSections[IPU_MAX_KERNELS_PER_PG];
        ia_p2p_payload_desc mPayloads[IPU_MAX_KERNELS_PER_PG];
        int mPayloadSize;
        ia_css_kernel_bitmap_t mKernelBitmap;
    };

    KernelRequirement mKernel;
    IpuPgRequirements mPgReqs;
};

} // namespace icamera
