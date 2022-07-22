/**
* INTEL CONFIDENTIAL
*
* Copyright (C) 2015 - 2017 Intel Corporation.
* All Rights Reserved.
*
* The source code contained or described herein and all documents
* related to the source code ("Material") are owned by Intel Corporation
* or licensors. Title to the Material remains with Intel
* Corporation or its licensors. The Material contains trade
* secrets and proprietary and confidential information of Intel or its
* licensors. The Material is protected by worldwide copyright
* and trade secret laws and treaty provisions. No part of the Material may
* be used, copied, reproduced, modified, published, uploaded, posted,
* transmitted, distributed, or disclosed in any way without Intel's prior
* express written permission.
*
* No License under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or
* delivery of the Materials, either expressly, by implication, inducement,
* estoppel or otherwise. Any license under such intellectual property rights
* must be express and approved by Intel in writing.
*/

#ifndef __IA_CSS_TERMINAL_MANIFEST_H
#define __IA_CSS_TERMINAL_MANIFEST_H

#include "type_support.h"
#include "ia_css_param_storage_class.h"
#include "ia_css_terminal_manifest_types.h"

IA_CSS_PARAMETERS_STORAGE_CLASS_H
unsigned int ia_css_param_terminal_manifest_get_size(
	const unsigned int nof_sections
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
int ia_css_param_terminal_manifest_init(
	ia_css_param_terminal_manifest_t *param_terminal,
	const uint16_t section_count
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
ia_css_param_manifest_section_desc_t *
ia_css_param_terminal_manifest_get_prm_sct_desc(
	const ia_css_param_terminal_manifest_t *param_terminal_manifest,
	const unsigned int section_index
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
unsigned int ia_css_spatial_param_terminal_manifest_get_size(
	const unsigned int nof_frame_param_sections
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
int ia_css_spatial_param_terminal_manifest_init(
	ia_css_spatial_param_terminal_manifest_t *spatial_param_terminal,
	const uint16_t section_count
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
ia_css_frame_grid_param_manifest_section_desc_t *
ia_css_spatial_param_terminal_manifest_get_frm_grid_prm_sct_desc(
	const ia_css_spatial_param_terminal_manifest_t *
		spatial_param_terminal_manifest,
	const unsigned int section_index
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
unsigned int ia_css_sliced_param_terminal_manifest_get_size(
	const unsigned int nof_slice_param_sections
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
int ia_css_sliced_param_terminal_manifest_init(
	ia_css_sliced_param_terminal_manifest_t *sliced_param_terminal,
	const uint16_t section_count
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
ia_css_sliced_param_manifest_section_desc_t *
ia_css_sliced_param_terminal_manifest_get_sliced_prm_sct_desc(
	const ia_css_sliced_param_terminal_manifest_t *
		sliced_param_terminal_manifest,
	const unsigned int section_index
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
unsigned int ia_css_program_terminal_manifest_get_size(
	const unsigned int nof_fragment_param_sections,
	const unsigned int nof_kernel_fragment_sequencer_infos
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
int ia_css_program_terminal_manifest_init(
	ia_css_program_terminal_manifest_t *program_terminal,
	const uint16_t fragment_param_section_count,
	const uint16_t kernel_fragment_seq_info_section_count
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
ia_css_fragment_param_manifest_section_desc_t *
ia_css_program_terminal_manifest_get_frgmnt_prm_sct_desc(
	const ia_css_program_terminal_manifest_t *program_terminal_manifest,
	const unsigned int section_index
);

IA_CSS_PARAMETERS_STORAGE_CLASS_H
ia_css_kernel_fragment_sequencer_info_manifest_desc_t *
ia_css_program_terminal_manifest_get_kernel_frgmnt_seq_info_desc(
	const ia_css_program_terminal_manifest_t *program_terminal_manifest,
	const unsigned int info_index
);

#ifdef __INLINE_PARAMETERS__
#include "ia_css_terminal_manifest_impl.h"
#endif /* __INLINE_PARAMETERS__ */

#endif /* __IA_CSS_TERMINAL_MANIFEST_H */
