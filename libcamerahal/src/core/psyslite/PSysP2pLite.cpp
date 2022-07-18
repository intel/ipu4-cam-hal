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

#define LOG_TAG "PSysP2pLite"

#include "PSysP2pLite.h"

#include "iutils/CameraLog.h"
#include "iutils/Errors.h"

namespace icamera {

PSysP2pLite::PSysP2pLite(int pgId) :
        mPgId(pgId),
        mTerminalCount(0),
        mIspHandle(nullptr),
        mP2pHandle(nullptr),
        mProcessGroup(nullptr),
        mPgManifest(nullptr)
{
    CLEAR(mCurrentIpuParam);
    CLEAR(mKernelGroup);
    CLEAR(mP2pCacheBuffer);
    CLEAR(mPgReqs);
    CLEAR(mFragmentDesc);
}

PSysP2pLite::~PSysP2pLite()
{
    ia_p2p_deinit(mP2pHandle);
    ia_isp_bxt_deinit(mIspHandle);
    if (mP2pCacheBuffer.data) {
        IA_CIPR_FREE(mP2pCacheBuffer.data);
    }

    for (int i = 0; i < mTerminalCount; i++) {
        if(mPgReqs.terminals[i].kernelOrder)
            IA_CIPR_FREE(mPgReqs.terminals[i].kernelOrder);
    }
    delete[] mKernelGroup.run_kernels;
}

int PSysP2pLite::getKernelIdByBitmap(ia_css_kernel_bitmap_t bitmap)
{
    int n = 0;
    if (ia_css_is_kernel_bitmap_empty(bitmap))
        return -1;
    while (!ia_css_is_kernel_bitmap_set(bitmap, (unsigned int)n))
        n++;
    return n;
}

int PSysP2pLite::setKernelConfig(int count, ia_isp_bxt_run_kernels_t* kernels)
{
    if (mKernelGroup.run_kernels == nullptr || mKernelGroup.kernel_count != (uint32_t)count) {
        mKernelGroup.kernel_count = count;
        delete[] mKernelGroup.run_kernels;
        mKernelGroup.run_kernels = new ia_isp_bxt_run_kernels_t[count];
    }

    int kernelsBufferSize = count * sizeof(ia_isp_bxt_run_kernels_t);
    MEMCPY_S(mKernelGroup.run_kernels, kernelsBufferSize, kernels, kernelsBufferSize);
    LOG1("%s: kernel group %d kernels", __func__, mKernelGroup.kernel_count);

    return OK;
}

int PSysP2pLite::prepareP2p(ia_p2p_platform_t platform, const ia_p2p_fragment_desc &fragmentDesc, ia_dvs_morph_table *dvsMorphTable)
{
    mFragmentDesc = fragmentDesc;

    mIspHandle = ia_isp_bxt_init(nullptr, nullptr,
                                 MAX_STATISTICS_WIDTH, MAX_STATISTICS_HEIGHT,
                                 MAX_NUM_OF_STATS_PER_FRAME,
                                 nullptr);
    Check(!mIspHandle, NO_INIT, "ISP adaptor failed to initialize");

    ia_isp_bxt_input_params_v2 inputParams;
    CLEAR(inputParams);
    inputParams.program_group = &mKernelGroup;
    inputParams.dvs_morph_table = dvsMorphTable;

    ia_err err;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_isp_bxt_run", 1);
        err = ia_isp_bxt_run_v2(mIspHandle, &inputParams, &mCurrentIpuParam);
    }
    Check(err != ia_err_none, UNKNOWN_ERROR, "ISP parameter adaptation has failed %d", err);

    mP2pHandle = ia_p2p_init(platform);
    Check(!mP2pHandle, UNKNOWN_ERROR, "ia_p2p_init has failed");

    mP2pCacheBuffer.size = ia_p2p_get_cache_buffer_size(mP2pHandle);
    mP2pCacheBuffer.data = IA_CIPR_CALLOC(1, mP2pCacheBuffer.size);
    LOG1("mP2pCacheBuffer.size=%d", mP2pCacheBuffer.size);
    Check(!mP2pCacheBuffer.data, UNKNOWN_ERROR, "Failed to allocate P2P cache buffer.");

    err = ia_p2p_parse(mP2pHandle, &mCurrentIpuParam, mP2pCacheBuffer.data);
    Check(err != ia_err_none, UNKNOWN_ERROR, "Failed to parse PAL data.");

    return OK;
}

int PSysP2pLite::prepareRequirements()
{
    css_err_t ret = css_err_none;
    ia_css_terminal_type_t terminalType;
    int8_t termIndex;
    int kernelId = 0;

    CLEAR(mPgReqs);
    for (termIndex = 0; termIndex < mTerminalCount; termIndex++) {
        Check((termIndex < 0) || (termIndex >= IPU_MAX_TERMINAL_COUNT), BAD_INDEX,
            "Terminal index is out of range [0, %d]", IPU_MAX_TERMINAL_COUNT - 1);
        ia_css_terminal_manifest_t *terminalManifest = ia_css_program_group_manifest_get_term_mnfst(mPgManifest, (unsigned int) termIndex);
        Check(!terminalManifest, css_err_internal, "No terminal manifest for terminal %d", termIndex);

        /* TODO: init time kernel enable bitmap from GraphConfig/P2P
         *       in order to skip terminals associated with disabled kernels
         *       See ia_css_data_terminal_manifest_get_kernel_bitmap()
         *       This would need to happen when we create the CIPF pipe.
         *
         *       Or optionally initialize with worst case resource shape and
         *       handle disabled terminals in stage
         */
        terminalType = ia_css_terminal_manifest_get_type(terminalManifest);
        mPgReqs.terminals[termIndex].type = terminalType;
        mPgReqs.terminals[termIndex].kernelOrder = nullptr;
        size_t kernelInfoSize = IPU_MAX_KERNELS_PER_PG * sizeof(IpuPgTerminalKernelInfo);
        switch (terminalType) {
            case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
                mPgReqs.terminals[termIndex].kernelOrder = (IpuPgTerminalKernelInfo*) IA_CIPR_MALLOC(kernelInfoSize);
                memset(mPgReqs.terminals[termIndex].kernelOrder, UINT8_MAX, kernelInfoSize);
                ret = getKernelOrderForParamCachedInTerm((ia_css_param_terminal_manifest_t*) terminalManifest,
                                                         mPgReqs.terminals[termIndex].kernelOrder);
                Check(ret != css_err_none, ret, "getKernelOrderForParamCachedInTerm failed");
                break;

            case IA_CSS_TERMINAL_TYPE_PROGRAM:
                mPgReqs.terminals[termIndex].kernelOrder =
                        (IpuPgTerminalKernelInfo*)IA_CIPR_MALLOC(kernelInfoSize);
                memset(mPgReqs.terminals[termIndex].kernelOrder, UINT8_MAX, kernelInfoSize);
                ret = getKernelOrderForProgramTerm((ia_css_program_terminal_manifest_t*) terminalManifest,
                                                    mPgReqs.terminals[termIndex].kernelOrder);
                Check(ret != css_err_none, ret, "getKernelOrderForProgramTerm failed");
                break;
            case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
                kernelId = (int32_t)((ia_css_spatial_param_terminal_manifest_t *)terminalManifest)->kernel_id;
                mPgReqs.terminals[termIndex].kernelBitmap = ia_css_kernel_bit_mask((uint32_t)kernelId);
                break;
            default:
                break;
        }
    }

    mPgReqs.terminalCount = mTerminalCount;

    ia_css_kernel_bitmap_t kernelBitmap = ia_p2p_get_kernel_bitmap(mP2pHandle,mPgId);
    LOG1("%s: kernel bitmap (p2p) : %#018lx", __func__, kernelBitmap);
    kernelBitmap = ia_css_kernel_bitmap_intersection(kernelBitmap,
        ia_css_program_group_manifest_get_kernel_bitmap(mPgManifest));
    LOG1("%s: kernel bitmap (masked by manifest) : %#018lx", __func__, kernelBitmap);

    while (!ia_css_is_kernel_bitmap_empty(kernelBitmap)) {
        ia_err iaRet;
        kernelId = getKernelIdByBitmap(kernelBitmap);
        Check((kernelId < 0 || kernelId >= IPU_MAX_KERNELS_PER_PG), ia_err_internal, "kernelId is out of index!");

        /* Get terminal requirements */
        CLEAR(mKernel.mSections[kernelId]);
        ret = ia_p2p_get_kernel_terminal_requirements(mP2pHandle, mPgId,
                                                      (uint32_t) kernelId, &mKernel.mSections[kernelId]);
        Check(ret != ia_err_none, ret, "%s: failed to get terminal requirements for PG %d kernel %d", __func__, mPgId, kernelId);
        int fragmentCount = 1;

        /* Get payload descriptor */
        CLEAR(mKernel.mPayloads[kernelId]);
        iaRet = ia_p2p_get_kernel_payload_desc(mP2pHandle, mPgId, (uint32_t) kernelId,
                                            fragmentCount, &mFragmentDesc, &mKernel.mPayloads[kernelId]);

        Check(iaRet != ia_err_none, iaRet, "ia_p2p_get_kernel_payload_desc failed (kernel %d)", kernelId);

        uint8_t kernelOrder = 0;
        if (mKernel.mSections[kernelId].param_in_section_count) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN;

            /* P2P assumes single CACHED IN, cumulate to first */
            termIndex = terminalEnumerateByType(&mPgReqs, terminalType, 0);
            Check(termIndex < 0, ia_err_internal, "No PARAM_CACHED_IN according to manifest!");
            if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, &kernelOrder)) {
                processTerminalKernelRequirements(&mPgReqs, termIndex, terminalType, kernelId);
            }
        }

        if (mKernel.mSections[kernelId].program_section_count_per_fragment) {
            terminalType = IA_CSS_TERMINAL_TYPE_PROGRAM;
            termIndex = terminalEnumerateByType(&mPgReqs, terminalType, 0);
            Check(termIndex < 0, ia_err_internal, "No PROGRAM-terminal according to manifest!");

            if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, &kernelOrder)) {
                processTerminalKernelRequirements(&mPgReqs, termIndex, terminalType, kernelId);
            }
        }

        /* Video ISA PG exposes some of the kernels with two different IDs. Only
         * the latter ones are to be used with spatial terminals. Until this is
         * properly fixed, a workaround is needed. Now just skipping any spatial
         * terminals that cannot be found in the manifest.
         */

        /* P2P assumes each spatial kernel parameter has its own terminal */
        if (mKernel.mSections[kernelId].spatial_param_in_section_count) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN;
            termIndex = terminalEnumerateByBitmap(&mPgReqs, terminalType, ia_css_kernel_bit_mask((uint32_t)kernelId));
            if (termIndex < 0 || termIndex >= IPU_MAX_TERMINAL_COUNT) {
                LOGW("%s: No valid spatial in terminal according to manifest! kernel id %d", __func__, kernelId);
            } else if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, nullptr)) {
                 mPgReqs.terminals[termIndex].sectionCount +=
                     mKernel.mSections[kernelId].spatial_param_in_section_count;
                 mPgReqs.terminals[termIndex].payloadSize +=
                     mKernel.mPayloads[kernelId].spatial_param_in_payload_size;
                mPgReqs.terminals[termIndex].kernelBitmap = ia_css_kernel_bit_mask((uint32_t)kernelId);
             }
        }

        kernelBitmap = ia_css_kernel_bitmap_unset(kernelBitmap,
                                           (uint32_t)kernelId);
    }

    return OK;
}

int PSysP2pLite::getPayloadSize(int terminalIndex, unsigned int *payloadSize)
{
    int ret = OK;
    Check((terminalIndex < 0) || (terminalIndex >= IPU_MAX_TERMINAL_COUNT), BAD_INDEX,
        "Terminal index: %d is out of range [0, %d]", terminalIndex, IPU_MAX_TERMINAL_COUNT - 1);

    LOG2("%s: terminalIndex = %d, payloadSize= %d", __func__,
        terminalIndex, mPgReqs.terminals[terminalIndex].payloadSize);
    *payloadSize = mPgReqs.terminals[terminalIndex].payloadSize;
    return ret;
}

int PSysP2pLite::updatePAL(ia_dvs_morph_table *dvsMorphTable)
{
    ia_isp_bxt_input_params_v2 inputParams;
    CLEAR(inputParams);
    CLEAR(mCurrentIpuParam);
    inputParams.program_group = &mKernelGroup;
    inputParams.dvs_morph_table = dvsMorphTable;

    ia_err err;
    {
        PERF_CAMERA_ATRACE_PARAM1_IMAGING("ia_isp_bxt_run", 1);
        err = ia_isp_bxt_run_v2(mIspHandle, &inputParams, &mCurrentIpuParam);
    }
    Check(err != ia_err_none, UNKNOWN_ERROR, "ISP parameter adaptation has failed %d", err);

    err = ia_p2p_parse(mP2pHandle, &mCurrentIpuParam, mP2pCacheBuffer.data);
    Check(err != ia_err_none, UNKNOWN_ERROR, "Failed to parse PAL data.");

    return OK;
}

int PSysP2pLite::encode(int terminalIndex, ia_binary_data payload)
{
    ia_css_terminal_t *terminal = nullptr;
    int index = 0;
    int termianlCount = ia_css_process_group_get_terminal_count(mProcessGroup);
    for (index = 0; index < termianlCount; index++) {
        terminal = ia_css_process_group_get_terminal(mProcessGroup, index);
        Check(!terminal, UNKNOWN_ERROR, "ia_css_process_group_get_terminal return nullptr");
        if (terminalIndex == terminal->tm_index) {
            LOG1("%s: terminal_count=%d, index=%d, terminal->tm_index:%d", __func__, termianlCount, index, terminal->tm_index);
            break;
        }
    }
    Check(index == termianlCount, UNKNOWN_ERROR, "Can't get terminal from process group for terminal index: %d", terminalIndex);
    Check((terminalIndex < 0) || (terminalIndex >= IPU_MAX_TERMINAL_COUNT), BAD_INDEX,
        "Terminal index is out of range [0, %d]", IPU_MAX_TERMINAL_COUNT - 1);

    ia_css_terminal_type_t terminalType = ia_css_terminal_get_type(terminal);
    LOG2("%s: PgId:%d, terminalCount:%d, terminalType:%d, terminalIndex:%d", __func__, mPgId, mTerminalCount, terminalType, terminalIndex);

    ia_css_program_terminal_t* programTerminal = nullptr;
    ia_css_param_terminal_t *paramTerminal = nullptr;
    ia_css_spatial_param_terminal_t* spatial_terminal = nullptr;

    ia_css_kernel_bitmap_t kernelBitmap = mPgReqs.terminals[terminalIndex].kernelBitmap;
    int fragmentCount = 1;
    ia_err ret;
    uint16_t kernelId = 0;
    uint8_t kernelIndex = 0;
    unsigned int curSection = 0;
    unsigned int curOffset = 0;

    if (mPgReqs.terminals[terminalIndex].type == IA_CSS_TERMINAL_TYPE_PROGRAM) {
        ret = ia_p2p_program_terminal_init(
                mP2pHandle,
                mPgId,
                fragmentCount,
                &mFragmentDesc,
                (ia_css_program_terminal_t*)terminal);
        Check(ret != ia_err_none, ret, "%s: failed to init program terminal", __func__);
    }

    while (!ia_css_is_kernel_bitmap_empty(kernelBitmap)) {
        /* Use specific ordering of kernels when available */
        if (mPgReqs.terminals[terminalIndex].kernelOrder) {
            kernelId = mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex++].id;
            if (kernelId >= IPU_MAX_KERNELS_PER_PG) {
                /* All the kernels have now been encoded. */
                break;
            }
        } else {
            kernelId = getKernelIdByBitmap(kernelBitmap);
            kernelBitmap = ia_css_kernel_bitmap_unset(kernelBitmap,
                                                       kernelId);
        }
        LOG2("%s: kernel_id = %d",__func__, kernelId);
        switch (mPgReqs.terminals[terminalIndex].type) {
            case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
                ret = ia_p2p_param_in_terminal_encode(mP2pHandle,
                                                      mPgId,
                                                      kernelId,
                                                      (ia_css_param_terminal_t *)terminal,
                                                      curSection,
                                                      (uint8_t*)payload.data,
                                                      payload.size,
                                                      curOffset);

                curSection += mKernel.mSections[kernelId].param_in_section_count;
                curOffset += mKernel.mPayloads[kernelId].param_in_payload_size;
            break;

            case IA_CSS_TERMINAL_TYPE_PROGRAM:
                ret = ia_p2p_program_terminal_encode(mP2pHandle,
                                                     mPgId,
                                                     kernelId,
                                                     fragmentCount,
                                                     &mFragmentDesc,
                                                     (ia_css_program_terminal_t*)terminal,
                                                     curSection,
                                                     mPgReqs.terminals[terminalIndex].sectionCount,
                                                     (uint8_t*)payload.data,
                                                     payload.size,
                                                     curOffset);
                curSection += mKernel.mSections[kernelId].program_section_count_per_fragment;
                curOffset += mKernel.mPayloads[kernelId].program_payload_size;
            break;

           case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
                 /* TODO: ensure program terminal gets encoded first */
                 ret = ia_p2p_spatial_param_in_terminal_encode(mP2pHandle,
                                                               mPgId,
                                                               kernelId,
                                                               fragmentCount,
                                                               &mFragmentDesc,
                                                               (ia_css_spatial_param_terminal_t*)terminal,
                                                               curSection,
                                                               (uint8_t*)payload.data,
                                                               payload.size,
                                                               curOffset);
                 curOffset += mKernel.mPayloads[kernelId].spatial_param_in_payload_size;
                 curSection += mKernel.mSections[kernelId].spatial_param_in_section_count;
            break;
            default:
                LOG1("%s: terminal type %d encode not implemented", __func__, mPgReqs.terminals[terminalIndex].type);
                return ia_err_argument;
        }
        if (ret != 0) {
            LOG1("%s: failed to encode terminal %d", __func__, terminalIndex);
            return ia_err_general;
        }
    }
    /* Finally encode the offset in the payload descriptor */
    Check(!terminal, UNKNOWN_ERROR, "Failed to encode the terminal(%d), terminal is nullptr", terminalIndex);
    switch (mPgReqs.terminals[terminalIndex].type) {
        case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
            paramTerminal = (ia_css_param_terminal_t *)terminal;
            paramTerminal->param_payload.buffer = (vied_vaddress_t) 0;
            break;
        case IA_CSS_TERMINAL_TYPE_PROGRAM:
            programTerminal = (ia_css_program_terminal_t *)terminal;
            programTerminal->param_payload.buffer = (vied_vaddress_t) 0;
            break;
        case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
            spatial_terminal = (ia_css_spatial_param_terminal_t *)terminal;
            spatial_terminal->param_payload.buffer =
                        (vied_vaddress_t) 0;
            break;
        default:
            LOG1("%s: terminal type %d encode not implemented", __func__, mPgReqs.terminals[terminalIndex].type);
            return ia_err_argument;
    }
    return OK;
}

css_err_t PSysP2pLite::getKernelOrderForParamCachedInTerm(ia_css_param_terminal_manifest_t *terminalManifest,
                                                         IpuPgTerminalKernelInfo *kernelOrder)
{
    Check((!terminalManifest || !kernelOrder), ia_err_argument, "%s: no manifest or order info", __func__);

    uint16_t sectionCount = terminalManifest->param_manifest_section_desc_count;
    Check(sectionCount == 0, css_err_argument, "%s: no static sections in manifest", __func__);
    uint8_t kernelCount = 0;

    for (uint16_t section = 0; section < sectionCount; section++) {
        ia_css_param_manifest_section_desc_t *param = ia_css_param_terminal_manifest_get_prm_sct_desc(terminalManifest, section);

        Check(!param, css_err_internal, "%s:Failed to get param from terminal manifest!", __func__);

        /* Note: there is agreement that sections of the same kernel are
         *       encoded in a row. Here, skipping sections of the same kernel
         *       based on this assumption.
         */
        int index = param->kernel_id;
        if (kernelCount > 0 && kernelOrder[kernelCount-1].id == index) {
            continue;
        }
        kernelOrder[kernelCount].id = (uint8_t) index;
        kernelCount++;
    }

    return css_err_none;
}

css_err_t PSysP2pLite::getKernelOrderForProgramTerm(ia_css_program_terminal_manifest_t *terminalManifest,
                                                   IpuPgTerminalKernelInfo *kernelOrder)
{
    Check((!terminalManifest || !kernelOrder), css_err_argument, "%s: no manifest or order info", __func__);
    uint16_t sectionCount = terminalManifest->fragment_param_manifest_section_desc_count;
    Check(sectionCount == 0, ia_err_internal, "%s: no static sections in manifest", __func__);
    uint8_t kernelCount = 0;

    for (uint16_t section = 0; section < sectionCount; section++) {
        ia_css_fragment_param_manifest_section_desc_t *param = ia_css_program_terminal_manifest_get_frgmnt_prm_sct_desc(terminalManifest, section);
        Check(!param, css_err_internal, "%s: no param info in manifest", __func__);

        /* Note: there is agreement that sections of the same kernel are
         *       encoded in a row. Here, skipping sections of the same kernel
         *       based on this assumption.
         */
        int index = param->kernel_id;
        if (kernelCount > 0 && kernelOrder[kernelCount-1].id == index) {
            continue;
        }
        kernelOrder[kernelCount].id = (uint8_t) index;
        kernelCount++;
    }
    return css_err_none;
}

int8_t PSysP2pLite::terminalEnumerateByType(IpuPgRequirements* reqs,
                                            ia_css_terminal_type_t terminalType,
                                            uint8_t num)
{
    Check(reqs->terminalCount == 0, ia_err_internal, "%s: no terminals!", __func__);

    for (uint8_t terminal = 0;terminal < reqs->terminalCount; terminal++) {
        if (reqs->terminals[terminal].type == terminalType) {
            if (num)
                num--;
            else
                return (int8_t) terminal;
        }
    }

    return -1;
}

int8_t PSysP2pLite::terminalEnumerateByBitmap(IpuPgRequirements* reqs,
                               ia_css_terminal_type_t terminal_type,
                               ia_css_kernel_bitmap_t bitmap)
 {
     Check(reqs->terminalCount == 0, ia_err_internal, "%s: no terminals!", __func__);

     for (uint8_t terminal = 0; terminal < reqs->terminalCount; terminal++) {
         if (reqs->terminals[terminal].type == terminal_type &&
             ia_css_is_kernel_bitmap_equal(reqs->terminals[terminal].kernelBitmap, bitmap)) {
             return (int8_t) terminal;
         }
     }

     return -1;
}

bool PSysP2pLite::isKernelIdInKernelOrder(IpuPgRequirements* reqs,
                                          int8_t termIndex,
                                          int kernelId,
                                          uint8_t *orderedIndex)
{
    /* No kernel order, return true always */
    if (!reqs->terminals[termIndex].kernelOrder)
        return true;

    /* Check if the kernel_id can be found from the kernelOrder */
    for (uint8_t i = 0; i < IPU_MAX_KERNELS_PER_PG; i++) {
        if (reqs->terminals[termIndex].kernelOrder[i].id == kernelId) {
            if (orderedIndex)
                *orderedIndex = i;
            return true;
        }
    }

    LOG1("Kernel %d not found from manifest, skipping!", kernelId);
    return false;
}

uint32_t PSysP2pLite::getKernelCountFromKernelOrder(IpuPgRequirements* reqs,
                                                    int8_t termIndex,
                                                    int kernelId)
{
    if (!reqs->terminals[termIndex].kernelOrder) {
        /* If no kernel order is present, assuming kernel appears once. */
        return 1;
    }

    uint32_t count = 0;
    for (int i = 0; i < IPU_MAX_KERNELS_PER_PG; i++) {
        if (reqs->terminals[termIndex].kernelOrder[i].id == kernelId) {
            ++count;
        }
    }

    return count;
}


void PSysP2pLite::processTerminalKernelRequirements(IpuPgRequirements* reqs,
                                                    int8_t termIndex,
                                                    ia_css_terminal_type_t terminalType,
                                                    int kernelId)
{
    uint32_t kernelCount = getKernelCountFromKernelOrder(reqs, termIndex, kernelId);
    uint32_t sectionCount = 0, payloadSize = 0;

    for (unsigned int i = 0; i < kernelCount; ++i) {
        switch (terminalType) {
        case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
            sectionCount = mKernel.mSections[kernelId].param_in_section_count;
            payloadSize = mKernel.mPayloads[kernelId].param_in_payload_size;
            break;
        case IA_CSS_TERMINAL_TYPE_PROGRAM:
            sectionCount = mKernel.mSections[kernelId].program_section_count_per_fragment;
            payloadSize = mKernel.mPayloads[kernelId].program_payload_size;
            break;
        default:
            LOG1("%s: terminal type %d encode not implemented", __func__, terminalType);
            break;
        }
        LOG1("%s: term_index: %d kernel_id: %d sectionCount:%d payloadSize:%d", __func__,
             termIndex, kernelId, sectionCount, payloadSize);
        reqs->terminals[termIndex].sectionCount += sectionCount;
        reqs->terminals[termIndex].payloadSize += payloadSize;

        mKernel.mPayloadSize = reqs->terminals[termIndex].payloadSize;
    }

    reqs->terminals[termIndex].kernelBitmap =
        ia_css_kernel_bitmap_set(reqs->terminals[termIndex].kernelBitmap, (unsigned int)kernelId);
}
} // namespace icamera
