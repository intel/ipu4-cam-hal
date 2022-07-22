/*
* INTEL CONFIDENTIAL
*
* Copyright (C) 2014 - 2017 Intel Corporation.
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

#ifndef __IA_CSS_PSYS_PROCESS_HSYS_USER_H
#define __IA_CSS_PSYS_PROCESS_HSYS_USER_H

/*! \file */

/** @file ia_css_psys_process.hsys.user.h
 *
 * Define the methods on the process object: Hsys user interface
 */

#include <ia_css_program_group_param.h>		/* ia_css_program_param_t */

#include <ia_css_psys_process_types.h>
#include <ia_css_psys_manifest_types.h>

#include <type_support.h>					/* uint8_t */

/*
 * Creation
 */

/*! Compute the size of storage required for allocating the process object

 @param	manifest[in]			program manifest
 @param	param[in]				program parameters

 @return 0 on error
 */
extern size_t ia_css_sizeof_process(
	const ia_css_program_manifest_t			*manifest,
	const ia_css_program_param_t			*param);

/*! Create the process object

 @param	raw_mem[in]				pre allocated memory
 @param	manifest[in]			program manifest
 @param	param[in]				program parameters

 @return NULL on error
 */
extern ia_css_process_t *ia_css_process_create(
	void				*raw_mem,
	const ia_css_program_manifest_t	*manifest,
	const ia_css_program_param_t	*param,
	const uint32_t			program_idx);

/*! Destroy (the storage of) the process object

 @param	process[in]				process object

 @return NULL
 */
extern ia_css_process_t *ia_css_process_destroy(
	ia_css_process_t	*process);

/*
 * Access functions
 */

/*! Print the process object to file/stream

 @param	process[in]				process object
 @param	fid[out]				file/stream handle

 @return < 0 on error
 */
extern int ia_css_process_print(
	const ia_css_process_t	*process,
	void			*fid);

#endif /* __IA_CSS_PSYS_PROCESS_HSYS_USER_H */
