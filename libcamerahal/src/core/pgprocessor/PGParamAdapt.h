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

extern "C" {
#include "ia_isp_types.h"
#include "ia_isp_bxt.h"

#include "ia_pal_types_isp_ids_autogen.h"

#include <ia_p2p_types.h>
#include <ia_p2p.h>

#include <ia_tools/ia_macros.h>
#include <ia_cipr/ia_cipr_psys_android.h>
#include <ia_cipr/ia_cipr_psys.h>
#include <ia_cipr/ia_cipr_alloc.h>
#include <ia_cipr/ia_cipr_memory.h>
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
#include "iutils/Errors.h"

#include <map>

namespace icamera {

struct PgConfiguration {
    ia_css_program_group_manifest_t* pgManifest;
    std::vector<int> disableDataTermials;
    ia_p2p_fragment_desc fragmentDesc;
    uint8_t fragmentCount;
};

/**
 * \class PGParamAdapt
 *
 * \brief This is a version P2P implementation which is used to encode parameter terminal
 *        and decode statistic terminal for PSYS pipeline.
 *
 * The call sequence as follows:
 * 1. init();
 * 2. prepare();
 * 3. loop frame {
 *    3.1. updatePAL();
 *    3.2. loop terminal {
 *             encode();
 *         }
 *    3.3. execute
 *    3.4. loop terminal {
 *             decode();
 *         }
 *    3.5 serializeDecodeCache();
 *    }
 * 4. deinit();
 */
class PGParamAdapt {

public:
    PGParamAdapt(int pgId);
    ~PGParamAdapt();

    /**
     * Use to init and config P2P handle.
     */
    int init(ia_p2p_platform_t platform, PgConfiguration Pgconfiguration);

    /**
     * Query and save the requirement for each terminal, calculate the final kernel bitmap.
     */
    int prepare(const ia_binary_data *ipuParameters, ia_css_kernel_bitmap_t *bitmap);

    /**
     * Run P2P parser to provide new PAL data.
     */
    int updatePAL(const ia_binary_data *ipuParameters);

    /**
     * Get payload size for the given terminal index.
     */
    int getPayloadSize(int terminalIndex, unsigned int *payloadSize);

    /**
     * Encode payload data for the gaven terminal index.
     */
    int encode(int terminalIndex, ia_binary_data payload, ia_css_process_group_t *processGroup);

    /**
     * Decode payload data for the gaven terminal index.
     */
    int decode(int terminalIndex, ia_binary_data payload, ia_css_process_group_t *processGroup);

    /**
     * Serialize decode cache to get the decode result.
     */
    int serializeDecodeCache(ia_binary_data *result);

    /**
     * Use to deinit P2P handle.
     */
    void deinit();

private:
    int mPgId;
    int mTerminalCount;

    uint8_t mFragmentCount;
    ia_p2p_fragment_desc mFragmentDesc;
    ia_p2p_handle mP2pHandle;
    ia_binary_data mP2pCacheBuffer;

    ia_css_program_group_manifest_t* mPgManifest;
    std::vector<int> mDisableDataTermials;

    struct IpuPgTerminalKernelInfo {
        uint8_t  id;
        uint8_t  sections;
        uint32_t size;
        bool     initialize;
    };

    struct IpuPgTerminaRequirements {
        IpuPgTerminaRequirements() { kernelBitmap = ia_css_kernel_bitmap_clear(); }
        ia_css_terminal_type_t type = IA_CSS_N_TERMINAL_TYPES;
        uint32_t payloadSize = 0;
        ia_css_kernel_bitmap_t kernelBitmap;
        uint32_t sectionCount = 0;
        IpuPgTerminalKernelInfo *kernelOrder = nullptr;
        ia_p2p_fragment_desc *fragment_descs = nullptr;
    };

    struct IpuPgRequirements {
        IpuPgRequirements() {}
        uint32_t terminalCount = 0;
        IpuPgTerminaRequirements terminals[IPU_MAX_TERMINAL_COUNT];
    };

    struct KernelRequirement {
        KernelRequirement() { mKernelBitmap = ia_css_kernel_bitmap_clear(); }
        ia_p2p_terminal_requirements_t mSections[IPU_MAX_KERNELS_PER_PG];
        ia_p2p_payload_desc mPayloads[IPU_MAX_KERNELS_PER_PG];
        int mPayloadSize = 0;
        ia_css_kernel_bitmap_t mKernelBitmap;
    };

    KernelRequirement mKernel;
    IpuPgRequirements mPgReqs;

private:
    DISALLOW_COPY_AND_ASSIGN(PGParamAdapt);
    int getKernelIdByBitmap(ia_css_kernel_bitmap_t bitmap);
    ia_css_kernel_bitmap_t getCachedTerminalKernelBitmap(ia_css_param_terminal_manifest_t *manifest);
    ia_css_kernel_bitmap_t getProgramTerminalKernelBitmap(ia_css_program_terminal_manifest_t *manifest);
    int disableZeroSizedTerminals(ia_css_kernel_bitmap_t* kernelBitmap);
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
    css_err_t payloadSectionSizeSanityTest(ia_p2p_payload_desc *current,
                                           uint16_t kernelId, uint8_t terminalIndex,
                                           uint32_t currentOffset, size_t payloadSize);
};

} // namespace icamera
