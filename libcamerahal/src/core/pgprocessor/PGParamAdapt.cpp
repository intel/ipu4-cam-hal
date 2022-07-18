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

#define LOG_TAG "PGParamAdapt"

#include "PGParamAdapt.h"

#include "iutils/CameraLog.h"

namespace icamera {

PGParamAdapt::PGParamAdapt(int pgId) :
        mPgId(pgId),
        mTerminalCount(0),
        mFragmentCount(0),
        mP2pHandle(nullptr),
        mPgManifest(nullptr)
{
    CLEAR(mFragmentDesc);
    CLEAR(mP2pCacheBuffer);
}

PGParamAdapt::~PGParamAdapt()
{
    for (int i = 0; i < mTerminalCount; i++) {
        delete[] mPgReqs.terminals[i].kernelOrder;
    }
}

int PGParamAdapt::init(ia_p2p_platform_t platform, PgConfiguration PgConfiguration)
{
    mP2pHandle = ia_p2p_init(platform);
    Check(!mP2pHandle, UNKNOWN_ERROR, "ia_p2p_init has failed");

    mP2pCacheBuffer.size = ia_p2p_get_cache_buffer_size(mP2pHandle);
    mP2pCacheBuffer.data = IA_CIPR_CALLOC(1, mP2pCacheBuffer.size);
    LOG1("%s: mP2pCacheBuffer.size=%d", __func__, mP2pCacheBuffer.size);
    Check(!mP2pCacheBuffer.data, UNKNOWN_ERROR, "Failed to allocate P2P cache buffer.");

    mPgManifest = PgConfiguration.pgManifest;
    mDisableDataTermials = PgConfiguration.disableDataTermials;
    mFragmentDesc = PgConfiguration.fragmentDesc;
    mFragmentCount = PgConfiguration.fragmentCount;
    mTerminalCount = ia_css_program_group_manifest_get_terminal_count(mPgManifest);

    return OK;
}

int PGParamAdapt::prepare(const ia_binary_data *ipuParameters, ia_css_kernel_bitmap_t *bitmap)
{
    Check(ipuParameters == nullptr || bitmap == nullptr, BAD_VALUE, "The input paramter is nullptr.");

    ia_css_terminal_type_t terminalType;
    int8_t termIndex;
    int kernelId = 0;

    int ret = updatePAL(ipuParameters);
    Check(ret != OK, ret, "Failed to update PAL data.");

    for (termIndex = 0; termIndex < mTerminalCount; termIndex++) {
        ia_css_terminal_manifest_t *terminalManifest =
            ia_css_program_group_manifest_get_term_mnfst(mPgManifest, (unsigned int)termIndex);
        Check(!terminalManifest, css_err_internal, "No terminal manifest for terminal %d", termIndex);

        terminalType = ia_css_terminal_manifest_get_type(terminalManifest);
        mPgReqs.terminals[termIndex].type = terminalType;
        mPgReqs.terminals[termIndex].kernelOrder = nullptr;
        size_t kernelInfoSize = IPU_MAX_KERNELS_PER_PG * sizeof(IpuPgTerminalKernelInfo);
        switch (terminalType) {
            case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
                mPgReqs.terminals[termIndex].kernelOrder = new IpuPgTerminalKernelInfo[IPU_MAX_KERNELS_PER_PG];
                memset(mPgReqs.terminals[termIndex].kernelOrder, UINT8_MAX, kernelInfoSize);
                ret = getKernelOrderForParamCachedInTerm((ia_css_param_terminal_manifest_t *)terminalManifest,
                                                         mPgReqs.terminals[termIndex].kernelOrder);
                Check(ret != css_err_none, ret, "getKernelOrderForParamCachedInTerm failed");
                break;

            case IA_CSS_TERMINAL_TYPE_PROGRAM:
                mPgReqs.terminals[termIndex].kernelOrder = new IpuPgTerminalKernelInfo[IPU_MAX_KERNELS_PER_PG];
                memset(mPgReqs.terminals[termIndex].kernelOrder, UINT8_MAX, kernelInfoSize);
                ret = getKernelOrderForProgramTerm((ia_css_program_terminal_manifest_t *)terminalManifest,
                                                    mPgReqs.terminals[termIndex].kernelOrder);
                Check(ret != css_err_none, ret, "getKernelOrderForProgramTerm failed");
                break;
            case IA_CSS_TERMINAL_TYPE_DATA_IN:
            case IA_CSS_TERMINAL_TYPE_DATA_OUT:
                /* Save the kernel bitmaps so that it can later be determined whether the terminals are disabled or not. */
                mPgReqs.terminals[termIndex].kernelBitmap =
                    ia_css_data_terminal_manifest_get_kernel_bitmap((ia_css_data_terminal_manifest_t *)terminalManifest);
                mPgReqs.terminals[termIndex].fragment_descs = &mFragmentDesc;
                break;
            case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
            case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT:
                kernelId = (int32_t)((ia_css_spatial_param_terminal_manifest_t *)terminalManifest)->kernel_id;
                mPgReqs.terminals[termIndex].kernelBitmap = ia_css_kernel_bit_mask((uint32_t)kernelId);
                break;
            default:
                break;
        }
    }

    mPgReqs.terminalCount = mTerminalCount;

    ia_css_kernel_bitmap_t kernelBitmap = ia_p2p_get_kernel_bitmap(mP2pHandle,mPgId);
    LOG1("%s: kernel bitmap (p2p) : %#018lx", __func__, ia_css_kernel_bitmap_to_uint64(kernelBitmap));
    kernelBitmap = ia_css_kernel_bitmap_intersection(kernelBitmap,
        ia_css_program_group_manifest_get_kernel_bitmap(mPgManifest));
    LOG1("%s: kernel bitmap (masked by manifest) : %#018lx", __func__, ia_css_kernel_bitmap_to_uint64(kernelBitmap));

    while (!ia_css_is_kernel_bitmap_empty(kernelBitmap)) {
        kernelId = getKernelIdByBitmap(kernelBitmap);
        Check((kernelId < 0 || kernelId >= IPU_MAX_KERNELS_PER_PG), ia_err_internal, "kernelId is out of range!");

        /* Get terminal requirements */
        ret = ia_p2p_get_kernel_terminal_requirements(mP2pHandle, mPgId,
                                                      (uint32_t) kernelId, &mKernel.mSections[kernelId]);
        Check(ret != ia_err_none, ret, "%s: failed to get terminal requirements for pg %d kernel %d",
            __func__, mPgId, kernelId);

        /* Get payload descriptor */
        ret = ia_p2p_get_kernel_payload_desc(mP2pHandle, mPgId, (uint32_t) kernelId,
                                            mFragmentCount, &mFragmentDesc, &mKernel.mPayloads[kernelId]);
        Check(ret != ia_err_none, ret, "%s: failed to get kernel paylaod for pg %d kernel %d",
            __func__, mPgId, kernelId);

        uint8_t kernelOrder = 0;
        if (mKernel.mSections[kernelId].param_in_section_count) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN;

            /* P2P assumes single CACHED IN, cumulate to first */
            termIndex = terminalEnumerateByType(&mPgReqs, terminalType, 0);
            Check(termIndex < 0, ia_err_internal, "No PARAM_CACHED_IN according to manifest!");
            if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, &kernelOrder)) {
                if (mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections
                    != mKernel.mSections[kernelId].param_in_section_count) {
                    LOGW("%s: p2p cached in section count differs from manifest (kernel_id:%i p2p:%d vs pg:%d)",
                            __func__, kernelId,
                            mKernel.mSections[kernelId].param_in_section_count,
                            mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections);
                    /* Overwrite P2P requirements with manifest */
                    mKernel.mSections[kernelId].param_in_section_count =
                        mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections;
                    mKernel.mPayloads[kernelId].param_in_payload_size =
                        std::max(mKernel.mPayloads[kernelId].param_in_payload_size,
                            mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].size);
                    mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].initialize = true;
                }
                processTerminalKernelRequirements(&mPgReqs, termIndex, terminalType, kernelId);
            }
        }

        if (mKernel.mSections[kernelId].param_out_section_count_per_fragment) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT;

            /* P2P assumes single CACHED OUT, cumulate to first */
            termIndex = terminalEnumerateByType(&mPgReqs, terminalType, 0);
            Check(termIndex < 0, ia_err_internal, "No PARAM_CACHED_OUT according to manifest!");
            if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, nullptr)) {
                processTerminalKernelRequirements(&mPgReqs, termIndex, terminalType, kernelId);
            }
        }

        if (mKernel.mSections[kernelId].program_section_count_per_fragment) {
            terminalType = IA_CSS_TERMINAL_TYPE_PROGRAM;
            termIndex = terminalEnumerateByType(&mPgReqs, terminalType, 0);
            Check(termIndex < 0, ia_err_internal, "No PROGRAM according to manifest!");

            if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, &kernelOrder)) {
                if (mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections !=
                    mKernel.mSections[kernelId].program_section_count_per_fragment) {
                    LOGW("%s: p2p program section count differs from manifest (kernel_id:%i p2p:%d vs pg:%d)",
                            __func__, kernelId,
                            mKernel.mSections[kernelId].program_section_count_per_fragment,
                            mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections);
                    /* Overwrite P2P requirements with manifest */
                    mKernel.mSections[kernelId].program_section_count_per_fragment =
                        mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].sections;
                    mKernel.mPayloads[kernelId].program_payload_size =
                        std::max(mKernel.mPayloads[kernelId].program_payload_size,
                            mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].size);
                    mPgReqs.terminals[termIndex].kernelOrder[kernelOrder].initialize = true;
                }
                processTerminalKernelRequirements(&mPgReqs, termIndex, terminalType, kernelId);
            }
        }

        /* P2P assumes each spatial kernel parameter has its own terminal */
        if (mKernel.mSections[kernelId].spatial_param_in_section_count) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN;
            termIndex = terminalEnumerateByBitmap(&mPgReqs, terminalType, ia_css_kernel_bit_mask((uint32_t)kernelId));
            if (termIndex < 0) {
                LOGW("%s: No PARAM_SPATIAL_IN for kernel id %d according to manifest!", __func__, kernelId);
            } else if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, nullptr)) {
                 mPgReqs.terminals[termIndex].sectionCount +=
                     mKernel.mSections[kernelId].spatial_param_in_section_count;
                 mPgReqs.terminals[termIndex].payloadSize +=
                     mKernel.mPayloads[kernelId].spatial_param_in_payload_size;
                mPgReqs.terminals[termIndex].kernelBitmap = ia_css_kernel_bit_mask((uint32_t)kernelId);
             }
        }

        if (mKernel.mSections[kernelId].spatial_param_out_section_count) {
            terminalType = IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT;
            termIndex = terminalEnumerateByBitmap(&mPgReqs, terminalType, ia_css_kernel_bit_mask((uint32_t)kernelId));
            if (termIndex < 0) {
                LOGW("%s: No PARAM_SPATIAL_OUT for kernel id %d according to manifest!", __func__, kernelId);
            } else if (isKernelIdInKernelOrder(&mPgReqs, termIndex, kernelId, nullptr)) {
                 mPgReqs.terminals[termIndex].sectionCount +=
                     mKernel.mSections[kernelId].spatial_param_out_section_count;
                 mPgReqs.terminals[termIndex].payloadSize +=
                     mKernel.mPayloads[kernelId].spatial_param_out_payload_size;
                mPgReqs.terminals[termIndex].kernelBitmap = ia_css_kernel_bit_mask((uint32_t)kernelId);
             }
        }

        kernelBitmap = ia_css_kernel_bitmap_unset(kernelBitmap, (uint32_t)kernelId);
    }

    /* get all kernel bits back */
    kernelBitmap = ia_css_program_group_manifest_get_kernel_bitmap(mPgManifest);

    /* get disabled kernels from p2p and remove them */
    kernelBitmap = ia_css_kernel_bitmap_intersection(kernelBitmap,
            ia_css_kernel_bitmap_complement(ia_p2p_get_kernel_disable_bitmap(mP2pHandle, mPgId)));
    LOG1("%s: kernel bitmap from p2p: %#018lx", __func__, ia_css_kernel_bitmap_to_uint64(kernelBitmap));

    /* get disabled data terminal kernels and remove them */
    for (auto& item : mDisableDataTermials) {
        ia_css_terminal_manifest_t *terminalManifest =
            ia_css_program_group_manifest_get_term_mnfst(mPgManifest, (unsigned int)item);
        ia_css_kernel_bitmap_t dataTerminalKernelBitmap =
            ia_css_data_terminal_manifest_get_kernel_bitmap((ia_css_data_terminal_manifest_t *)terminalManifest);
        LOG1("%s: item = %d, kernel bitmap: %#018lx, disabled data termial kernel bitmap: %#018lx",
            __func__, item, ia_css_kernel_bitmap_to_uint64(kernelBitmap), dataTerminalKernelBitmap);
        kernelBitmap = ia_css_kernel_bitmap_intersection(kernelBitmap,
            ia_css_kernel_bitmap_complement(dataTerminalKernelBitmap));
    }

    /* disable params terminals which payload size are zero */
    ret = disableZeroSizedTerminals(&kernelBitmap);
    Check(ret != OK, ret, "%s: failed to disable zero size terminals", __func__);

    *bitmap = kernelBitmap;
    LOG1("%s: final kernel bitmap: %#018lx", __func__, ia_css_kernel_bitmap_to_uint64(*bitmap));

    return ret;
}

int PGParamAdapt::updatePAL(const ia_binary_data *ipuParameters)
{
    ia_err err = ia_p2p_parse(mP2pHandle, ipuParameters, mP2pCacheBuffer.data);
    Check(err != ia_err_none, UNKNOWN_ERROR, "Failed to parse PAL data.");

    return OK;
}

int PGParamAdapt::getPayloadSize(int terminalIndex, unsigned int *payloadSize)
{
    int boundary = mTerminalCount <= IPU_MAX_TERMINAL_COUNT? mTerminalCount: IPU_MAX_TERMINAL_COUNT;
    Check((terminalIndex < 0) || (terminalIndex >= boundary), UNKNOWN_ERROR,
        "Terminal index: %d is out of range [0, %d]", terminalIndex, boundary - 1);

    LOG2("%s: terminalIndex = %d, payloadSize = %d", __func__,
        terminalIndex, mPgReqs.terminals[terminalIndex].payloadSize);
    *payloadSize = mPgReqs.terminals[terminalIndex].payloadSize;
    return OK;
}

int PGParamAdapt::encode(int terminalIndex, ia_binary_data payload, ia_css_process_group_t *processGroup)
{
    int ret = OK;
    ia_css_terminal_t *terminal = nullptr;
    int index = 0;

    int boundary = mTerminalCount <= IPU_MAX_TERMINAL_COUNT? mTerminalCount: IPU_MAX_TERMINAL_COUNT;
    Check((terminalIndex < 0) || (terminalIndex >= boundary), UNKNOWN_ERROR,
        "Terminal index: %d is out of range [0, %d]", terminalIndex, boundary - 1);

    int termianlCount = ia_css_process_group_get_terminal_count(processGroup);
    for (index = 0; index < termianlCount; index++) {
        terminal = ia_css_process_group_get_terminal(processGroup, index);
        Check(!terminal, UNKNOWN_ERROR, "ia_css_process_group_get_terminal return nullptr");
        if (terminalIndex == terminal->tm_index) {
            LOG1("%s: terminal_count=%d, index=%d, terminal->tm_index=%d", __func__, termianlCount, index, terminal->tm_index);
            break;
        }
    }
    Check(index == termianlCount, UNKNOWN_ERROR, "Can't get terminal from process group for terminal index: %d", terminalIndex);

    ia_css_kernel_bitmap_t kernelBitmap = mPgReqs.terminals[terminalIndex].kernelBitmap;
    uint16_t kernelId = 0;
    uint8_t kernelIndex = 0;
    unsigned int curSection = 0;
    unsigned int curOffset = 0;
    ia_p2p_payload_desc tmpPayloadDesc;

    if (mPgReqs.terminals[terminalIndex].type == IA_CSS_TERMINAL_TYPE_PROGRAM) {
        ret = ia_p2p_program_terminal_init(
                mP2pHandle,
                mPgId,
                mFragmentCount,
                &mFragmentDesc,
                (ia_css_program_terminal_t*)terminal);
        Check(ret != ia_err_none, ret, "Failed to init program terminal.");
    }

    while (!ia_css_is_kernel_bitmap_empty(kernelBitmap)) {
        /* Use specific ordering of kernels when available */
        if (mPgReqs.terminals[terminalIndex].kernelOrder) {
            kernelId = mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex++].id;
            if (kernelId >= IPU_MAX_KERNELS_PER_PG) {
                /* All the kernels have now been encoded. */
                break;
            }
            /* Initialize parameter payload for current kernel with zeros in
             * case P2P has reported less sections for the kernel */
            if (mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex - 1].initialize) {
                LOG2("%s: initializing kernel %d payload in terminal %d (offset:%d, size:%d)",
                          __func__, kernelId, terminalIndex, curOffset,
                          mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex-1].size);
                memset((uint8_t* )payload.data + curOffset, 0,
                    mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex-1].size);
            }
        } else {
            kernelId = getKernelIdByBitmap(kernelBitmap);
        }
        LOG2("%s: encode kernelId: %d for terminalIndex: %d", __func__, kernelId, terminalIndex);

        /* Sanity check sections sizes and return the size to be used */
        css_err_t result = payloadSectionSizeSanityTest(&tmpPayloadDesc,
                                                kernelId,
                                                terminalIndex,
                                                curOffset,
                                                payload.size);
        Check((result != css_err_none), UNKNOWN_ERROR, "Failed sanity check of terminal payload sizes");

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
                Check(ret != ia_err_none, ret, "Failed to encode param in terminal.");

                curSection += mKernel.mSections[kernelId].param_in_section_count;
                curOffset += tmpPayloadDesc.param_in_payload_size;
                break;

            case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT:
                ret = ia_p2p_param_out_terminal_prepare(mP2pHandle,
                                                        mPgId,
                                                        kernelId,
                                                        mFragmentCount,
                                                        (ia_css_param_terminal_t *)terminal,
                                                        curSection,
                                                        mPgReqs.terminals[terminalIndex].sectionCount,
                                                        payload.size,
                                                        curOffset);
                Check(ret != ia_err_none, ret, "Failed to prepare param out terminal.");

                curSection += mKernel.mSections[kernelId].param_out_section_count_per_fragment;
                curOffset += tmpPayloadDesc.param_out_payload_size;
                break;

            case IA_CSS_TERMINAL_TYPE_PROGRAM:
                ret = ia_p2p_program_terminal_encode(mP2pHandle,
                                                     mPgId,
                                                     kernelId,
                                                     mFragmentCount,
                                                     &mFragmentDesc,
                                                     (ia_css_program_terminal_t*)terminal,
                                                     curSection,
                                                     mPgReqs.terminals[terminalIndex].sectionCount,
                                                     (uint8_t*)payload.data,
                                                     payload.size,
                                                     curOffset);
                Check(ret != ia_err_none, ret, "Failed to encode program terminal.");

                curSection += mKernel.mSections[kernelId].program_section_count_per_fragment;
                curOffset += tmpPayloadDesc.program_payload_size;
                break;

           case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
                 /* TODO: ensure program terminal gets encoded first */
                 ret = ia_p2p_spatial_param_in_terminal_encode(mP2pHandle,
                                                               mPgId,
                                                               kernelId,
                                                               mFragmentCount,
                                                               &mFragmentDesc,
                                                               (ia_css_spatial_param_terminal_t*)terminal,
                                                               curSection,
                                                               (uint8_t*)payload.data,
                                                               payload.size,
                                                               curOffset);
                 Check(ret != ia_err_none, ret, "Failed to encode spatial in terminal.");

                 curOffset += tmpPayloadDesc.spatial_param_in_payload_size;
                 curSection += mKernel.mSections[kernelId].spatial_param_in_section_count;
                 break;

            case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT:
                 ret = ia_p2p_spatial_param_out_terminal_prepare(mP2pHandle,
                                                               mPgId,
                                                               kernelId,
                                                               mFragmentCount,
                                                               &mFragmentDesc,
                                                               (ia_css_spatial_param_terminal_t*)terminal,
                                                               curSection,
                                                               payload.size,
                                                               curOffset);
                 Check(ret != ia_err_none, ret, "Failed to prepare spatial out terminal.");

                 curOffset += tmpPayloadDesc.spatial_param_out_payload_size;
                 curSection += mKernel.mSections[kernelId].spatial_param_out_section_count;
                 break;

            case IA_CSS_TERMINAL_TYPE_PROGRAM_CONTROL_INIT:
            case IA_CSS_TERMINAL_TYPE_DATA_IN:
            case IA_CSS_TERMINAL_TYPE_DATA_OUT:
                 /* No encode done for frame terminals */
                 break;

            default:
                LOGE("%s: terminal type %d encode not implemented", __func__, mPgReqs.terminals[terminalIndex].type);
                return UNKNOWN_ERROR;
        }

        if (!mPgReqs.terminals[terminalIndex].kernelOrder) {
            kernelBitmap = ia_css_kernel_bitmap_unset(kernelBitmap, kernelId);
        }
    }

    return ret;
}

int PGParamAdapt::decode(int terminalIndex, ia_binary_data payload, ia_css_process_group_t *processGroup)
{
    int ret = OK;
    ia_css_terminal_t *terminal = nullptr;
    int index = 0;
    int termianlCount = ia_css_process_group_get_terminal_count(processGroup);
    for (index = 0; index < termianlCount; index++) {
        terminal = ia_css_process_group_get_terminal(processGroup, index);
        Check(!terminal, UNKNOWN_ERROR, "ia_css_process_group_get_terminal return nullptr");
        if (terminalIndex == terminal->tm_index) {
            LOG1("%s: terminal_count=%d, index=%d, terminal->tm_index=%d",
                __func__, termianlCount, index, terminal->tm_index);
            break;
        }
    }
    Check(index == termianlCount, UNKNOWN_ERROR,
        "Can't get terminal from process group for terminal index: %d", terminalIndex);

    unsigned int currentSection = 0;
    int kernelIndex = 0;
    uint16_t kernelId;
    ia_css_kernel_bitmap_t kernelBitmap = mPgReqs.terminals[terminalIndex].kernelBitmap;
    while (!ia_css_is_kernel_bitmap_empty(kernelBitmap)) {
        /* Use specific ordering of kernels when available */
        if (mPgReqs.terminals[terminalIndex].kernelOrder) {
            kernelId = mPgReqs.terminals[terminalIndex].kernelOrder[kernelIndex++].id;
            Check(kernelId >= IPU_MAX_KERNELS_PER_PG, css_err_internal,
                "%s: Kernel bitmap for terminal %d covers more kernels than in manifest",
                __func__, terminalIndex);
        } else {
            kernelId = getKernelIdByBitmap(kernelBitmap);
        }

        LOG2("%s: decode kernelId: %d for terminalId: %d", __func__, kernelId, terminalIndex);
        switch (mPgReqs.terminals[terminalIndex].type) {
            case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT:
                ret = ia_p2p_param_out_terminal_decode(
                        mP2pHandle,
                        mPgId,
                        kernelId,
                        mFragmentCount,
                        (ia_css_param_terminal_t* )terminal,
                        currentSection,
                        mPgReqs.terminals[terminalIndex].sectionCount,
                        (unsigned char* )payload.data,
                        payload.size);
                currentSection += mKernel.mSections[kernelId].param_out_section_count_per_fragment;
                break;
            case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT:
                ret = ia_p2p_spatial_param_out_terminal_decode_v2(
                        mP2pHandle,
                        mPgId,
                        kernelId,
                        mFragmentCount,
                        &mFragmentDesc,
                        (ia_css_spatial_param_terminal_t* )terminal,
                        currentSection,
                        (unsigned char* )payload.data,
                        payload.size,
                        mP2pCacheBuffer.data);
                currentSection += mKernel.mSections[kernelId].spatial_param_out_section_count;
                break;
            default:
                LOGE("%s: terminal type %d decode not implemented",
                    __func__, mPgReqs.terminals[terminalIndex].type);
                return UNKNOWN_ERROR;
        }

        Check(ret != ia_err_none, ret, "%s: failed to decode terminal %d", __func__, terminalIndex);
        kernelBitmap = ia_css_kernel_bitmap_unset(kernelBitmap, kernelId);
    }

    return ret;
}

int PGParamAdapt::serializeDecodeCache(ia_binary_data *result)
{
    Check(!result, UNKNOWN_ERROR, "The statistics buffer is nullptr");

    ia_err ia_ret = ia_p2p_serialize_statistics(mP2pHandle, result, nullptr);
    Check(ia_ret != ia_err_none, UNKNOWN_ERROR, "Serializ statistics fail");

    return OK;
}

void PGParamAdapt::deinit()
{
    ia_p2p_deinit(mP2pHandle);
    if (mP2pCacheBuffer.data) {
        IA_CIPR_FREE(mP2pCacheBuffer.data);
    }
}

int PGParamAdapt::getKernelIdByBitmap(ia_css_kernel_bitmap_t bitmap)
{
    int kernelId = 0;
    Check(ia_css_is_kernel_bitmap_empty(bitmap), BAD_VALUE, "The bitmap is empty");
    while (!ia_css_is_kernel_bitmap_set(bitmap, (unsigned int)kernelId)) {
        kernelId++;
    }

    return kernelId;
}

ia_css_kernel_bitmap_t PGParamAdapt::getCachedTerminalKernelBitmap(ia_css_param_terminal_manifest_t *manifest)
{
    ia_css_kernel_bitmap_t kernelBitmap = ia_css_kernel_bitmap_clear();
    unsigned int section, sectionCount;

    /* Loop through all the sections in the manifest and put the kernel ids into the kernel bitmap. */
    sectionCount = manifest->param_manifest_section_desc_count;
    for (section = 0; section < sectionCount; section++) {
        ia_css_param_manifest_section_desc_t *desc = ia_css_param_terminal_manifest_get_prm_sct_desc(manifest, section);
        int index = desc->kernel_id;
        kernelBitmap = ia_css_kernel_bitmap_set(kernelBitmap, index);
    }

    return kernelBitmap;
}

ia_css_kernel_bitmap_t PGParamAdapt::getProgramTerminalKernelBitmap(ia_css_program_terminal_manifest_t *manifest)
{
    ia_css_kernel_bitmap_t kernelBitmap = ia_css_kernel_bitmap_clear();
    unsigned int section, sectionCount;

    /* Loop through all the sections in the manifest and put the kernel ids into the kernel bitmap. */
    sectionCount = manifest->fragment_param_manifest_section_desc_count;
    for (section = 0; section < sectionCount; section++) {
        ia_css_fragment_param_manifest_section_desc_t *desc =
            ia_css_program_terminal_manifest_get_frgmnt_prm_sct_desc(manifest, section);
        int index = desc->kernel_id;
        kernelBitmap = ia_css_kernel_bitmap_set(kernelBitmap, index);
    }

    return kernelBitmap;
}

int PGParamAdapt::disableZeroSizedTerminals(ia_css_kernel_bitmap_t* kernelBitmap)
{
    int ret = OK;
    ia_css_terminal_type_t terminalType;
    ia_css_kernel_bitmap_t terminalKernelsBitmap = ia_css_kernel_bitmap_clear();
    ia_css_kernel_bitmap_t disabledTerminalKernelsBitmap = ia_css_kernel_bitmap_clear();
    for (int i = 0; i < mTerminalCount; i++) {
        terminalKernelsBitmap = ia_css_kernel_bitmap_clear();
        unsigned int payloadSize = 0;
        ret = getPayloadSize(i, &payloadSize);
        Check((ret != OK), ret, "%s, call get payload size fail", __func__);
        ia_css_terminal_manifest_t *manifest = ia_css_program_group_manifest_get_term_mnfst(mPgManifest, i);
        terminalType = ia_css_terminal_manifest_get_type(manifest);

        if (payloadSize == 0) {
            switch (terminalType) {
                case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
                case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT:
                    /* Spatial terminals are only associated to a single kernel. */
                    terminalKernelsBitmap = ia_css_kernel_bitmap_set(
                            terminalKernelsBitmap, ((ia_css_spatial_param_terminal_manifest_t* )manifest)->kernel_id);
                    break;
                case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
                case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT:
                    terminalKernelsBitmap = getCachedTerminalKernelBitmap((ia_css_param_terminal_manifest_t* )manifest);
                    break;
                case IA_CSS_TERMINAL_TYPE_PROGRAM:
                    terminalKernelsBitmap = getProgramTerminalKernelBitmap((ia_css_program_terminal_manifest_t* )manifest);
                    break;
                case IA_CSS_TERMINAL_TYPE_PROGRAM_CONTROL_INIT:
                    LOG1("%s: program control init terminal is always enabled.", __func__);
                    break;
                default:
                    break;
            }
            disabledTerminalKernelsBitmap = ia_css_kernel_bitmap_union(disabledTerminalKernelsBitmap, terminalKernelsBitmap);
        }
    }

    LOG1("%s: kernel bitmap: %#018lx, disabled zero sized params termial kernel bitmap: %#018lx",
        __func__, *kernelBitmap, ia_css_kernel_bitmap_to_uint64(disabledTerminalKernelsBitmap));

    *kernelBitmap = ia_css_kernel_bitmap_intersection(*kernelBitmap,
        ia_css_kernel_bitmap_complement(disabledTerminalKernelsBitmap));

    return ret;
}

css_err_t PGParamAdapt::getKernelOrderForParamCachedInTerm(ia_css_param_terminal_manifest_t *terminalManifest,
                                                         IpuPgTerminalKernelInfo *kernelOrder)
{
    Check((!terminalManifest || !kernelOrder), ia_err_argument, "No manifest or order info");

    uint16_t sectionCount = terminalManifest->param_manifest_section_desc_count;
    Check(sectionCount == 0, css_err_argument, "No static sections in manifest");
    uint8_t kernelCount = 0;

    for (uint16_t section = 0; section < sectionCount; section++) {
        ia_css_param_manifest_section_desc_t *param =
            ia_css_param_terminal_manifest_get_prm_sct_desc(terminalManifest, section);
        Check(!param, css_err_internal, "Failed to get param from terminal manifest!");

        /* there is agreement that sections of the same kernel are
         * encoded in a row. Here, skipping sections of the same kernel
         * based on this assumption.
         */
        int index = param->kernel_id;
        if (kernelCount > 0 && kernelOrder[kernelCount - 1].id == index) {
            ++kernelOrder[kernelCount - 1].sections;
            kernelOrder[kernelCount - 1].size += param->max_mem_size;
            continue;
        }
        kernelOrder[kernelCount].id = (uint8_t) index;
        kernelOrder[kernelCount].sections = 1;
        kernelOrder[kernelCount].size = param->max_mem_size;
        kernelOrder[kernelCount].initialize = false;
        kernelCount++;
    }

    return css_err_none;
}

css_err_t PGParamAdapt::getKernelOrderForProgramTerm(ia_css_program_terminal_manifest_t *terminalManifest,
                                                   IpuPgTerminalKernelInfo *kernelOrder)
{
    Check((!terminalManifest || !kernelOrder), css_err_argument, "No manifest or order info");
    uint16_t sectionCount = terminalManifest->fragment_param_manifest_section_desc_count;
    Check(sectionCount == 0, ia_err_internal, "No static sections in manifest");
    uint8_t kernelCount = 0;

    for (uint16_t section = 0; section < sectionCount; section++) {
        ia_css_fragment_param_manifest_section_desc_t *param =
            ia_css_program_terminal_manifest_get_frgmnt_prm_sct_desc(terminalManifest, section);
        Check(!param, css_err_internal, "Failed to get param from terminal manifest!");

        /* there is agreement that sections of the same kernel are
         * encoded in a row. Here, skipping sections of the same kernel
         * based on this assumption.
         */
        int index = param->kernel_id;
        if (kernelCount > 0 && kernelOrder[kernelCount - 1].id == index) {
            ++kernelOrder[kernelCount - 1].sections;
            kernelOrder[kernelCount - 1].size += param->max_mem_size;
            continue;
        }
        kernelOrder[kernelCount].id = (uint8_t) index;
        kernelOrder[kernelCount].sections = 1;
        kernelOrder[kernelCount].size = param->max_mem_size;
        kernelOrder[kernelCount].initialize = false;
        kernelCount++;
    }

    return css_err_none;
}

int8_t PGParamAdapt::terminalEnumerateByType(IpuPgRequirements* reqs,
                                            ia_css_terminal_type_t terminalType, uint8_t num)
{
    Check(reqs->terminalCount == 0, -1, "%s: no terminals!", __func__);

    for (uint8_t terminal = 0; terminal < reqs->terminalCount; terminal++) {
        if (reqs->terminals[terminal].type == terminalType) {
            if (num)
                num--;
            else
                return (int8_t)terminal;
        }
    }

    return -1;
}

int8_t PGParamAdapt::terminalEnumerateByBitmap(IpuPgRequirements* reqs,
                               ia_css_terminal_type_t terminal_type, ia_css_kernel_bitmap_t bitmap)
 {
     Check(reqs->terminalCount == 0, -1, "%s: no terminals!", __func__);

     for (uint8_t terminal = 0; terminal < reqs->terminalCount; terminal++) {
         if (reqs->terminals[terminal].type == terminal_type &&
             ia_css_is_kernel_bitmap_equal(reqs->terminals[terminal].kernelBitmap, bitmap)) {
             return (int8_t)terminal;
         }
     }

     return -1;
}

bool PGParamAdapt::isKernelIdInKernelOrder(IpuPgRequirements* reqs,
                                          int8_t termIndex, int kernelId, uint8_t *orderedIndex)
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

    LOG1("%s: Kernel %d not found from manifest, skipping!", __func__, kernelId);
    return false;
}

uint32_t PGParamAdapt::getKernelCountFromKernelOrder(IpuPgRequirements* reqs,
                                                    int8_t termIndex, int kernelId)
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

void PGParamAdapt::processTerminalKernelRequirements(IpuPgRequirements* reqs, int8_t termIndex,
                                                             ia_css_terminal_type_t terminalType, int kernelId)
{
    uint32_t kernelCount = getKernelCountFromKernelOrder(reqs, termIndex, kernelId);
    uint32_t sectionCount = 0, payloadSize = 0;

    for (unsigned int i = 0; i < kernelCount; ++i) {
        switch (terminalType) {
        case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
            sectionCount = mKernel.mSections[kernelId].param_in_section_count;
            payloadSize = mKernel.mPayloads[kernelId].param_in_payload_size;
            break;
        case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT:
            sectionCount = mKernel.mSections[kernelId].param_out_section_count_per_fragment;
            payloadSize = mKernel.mPayloads[kernelId].param_out_payload_size;
            break;
        case IA_CSS_TERMINAL_TYPE_PROGRAM:
            sectionCount = mKernel.mSections[kernelId].program_section_count_per_fragment;
            payloadSize = mKernel.mPayloads[kernelId].program_payload_size;
            break;
        default:
            LOG1("%s: terminal type %d encode not implemented", __func__, terminalType);
            break;
        }
        LOG1("%s: term_index: %d, kernel_id: %d, sectionCount: %d, payloadSize: %d",
            __func__, termIndex, kernelId, sectionCount, payloadSize);
        reqs->terminals[termIndex].sectionCount += sectionCount;
        reqs->terminals[termIndex].payloadSize += payloadSize;

        mKernel.mPayloadSize = reqs->terminals[termIndex].payloadSize;
    }

    reqs->terminals[termIndex].kernelBitmap =
        ia_css_kernel_bitmap_set(reqs->terminals[termIndex].kernelBitmap, (unsigned int)kernelId);
}

css_err_t PGParamAdapt::payloadSectionSizeSanityTest(ia_p2p_payload_desc *current,
                                           uint16_t kernelId, uint8_t terminalIndex,
                                           uint32_t currentOffset, size_t payloadSize)
{
    size_t nextPayloadSize = 0;
    ia_p2p_payload_desc init = mKernel.mPayloads[kernelId];
    /* calculate again the memory requirements for each kernel
     * and compare it with what we stored at init time. */
    ia_err ia_ret = ia_p2p_get_kernel_payload_desc(
            mP2pHandle,
            mPgId,
            kernelId,
            mFragmentCount,
            &mFragmentDesc,
            current);
    Check(ia_ret != ia_err_none, css_err_internal,
        "Failed to get payload description during sanity check (kernel %d)", kernelId);

    switch (mPgReqs.terminals[terminalIndex].type) {
    case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN:
        if (current->param_in_payload_size > init.param_in_payload_size) {
            LOGW("%s: param-in section size mismatch in pg[%d] kernel[%d]"
                               " p2p size %d pg_die size %d",
                               __func__,
                               mPgId,
                               kernelId,
                               current->param_in_payload_size,
                               init.param_in_payload_size);
        } else {
            current->param_in_payload_size = init.param_in_payload_size;
        }
        nextPayloadSize = current->param_in_payload_size;
        break;
    case IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT:
        if (current->param_out_payload_size > init.param_out_payload_size) {
            LOGW("%s: param-out section size mismatch in pg[%d] kernel[%d]"
                               " p2p size %d pg_die size %d",
                               __func__,
                               mPgId,
                               kernelId,
                               current->param_out_payload_size,
                               init.param_out_payload_size);
        } else {
            current->param_out_payload_size = init.param_out_payload_size;
        }
        nextPayloadSize = current->param_out_payload_size;
        break;
    case IA_CSS_TERMINAL_TYPE_PROGRAM:
        if (current->program_payload_size > init.program_payload_size) {
            LOG1("%s: program section size mismatch in pg[%d] kernel[%d]"
                               " p2p size %d pg_die size %d",
                               __func__,
                               mPgId,
                               kernelId,
                               current->program_payload_size,
                               init.program_payload_size);
        } else {
            current->program_payload_size = init.program_payload_size;
        }
        nextPayloadSize = current->program_payload_size;
        break;
    case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN:
        if (current->spatial_param_in_payload_size > init.spatial_param_in_payload_size) {
            LOGW("%s: spatial-in section size mismatch in pg[%d] kernel[%d]"
                           " p2p size %d pg_die size %d",
                           __func__,
                           mPgId,
                           kernelId,
                           current->spatial_param_in_payload_size,
                           init.spatial_param_in_payload_size);
        } else {
            current->spatial_param_in_payload_size = init.spatial_param_in_payload_size;
        }
        nextPayloadSize = current->spatial_param_in_payload_size;
        break;
    case IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT:
        if (current->spatial_param_out_payload_size > init.spatial_param_out_payload_size) {
            LOGW("%s: spatial-out section size mismatch in pg[%d] kernel[%d]"
                           " p2p size %d pg_die size %d",
                           __func__,
                           mPgId,
                           kernelId,
                           current->spatial_param_out_payload_size,
                           init.spatial_param_out_payload_size);
        } else {
            current->spatial_param_out_payload_size = init.spatial_param_out_payload_size;
        }
        nextPayloadSize = current->spatial_param_out_payload_size;
        break;
    case IA_CSS_TERMINAL_TYPE_DATA_IN:
    case IA_CSS_TERMINAL_TYPE_DATA_OUT:
    case IA_CSS_TERMINAL_TYPE_PROGRAM_CONTROL_INIT:
        /* No check done for frame terminals */
        break;
    default:
        LOGE("%s: terminal type %d payload check not implemented",
            __func__, mPgReqs.terminals[terminalIndex].type);
        return css_err_argument;
    }

    Check((currentOffset + nextPayloadSize > payloadSize), css_err_nomemory,
        "pg %d terminal %d payload buffer size too small, encoding for kernel %d will exceed payload size by %d bytes",
        mPgId, terminalIndex, kernelId, (int)(currentOffset + nextPayloadSize - payloadSize));
    return css_err_none;
}

} // namespace icamera
