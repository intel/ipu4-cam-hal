/**
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2016 - 2018 Intel Corporation.
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

#ifndef __IA_CSS_STITCH_DEFS_H
#define __IA_CSS_STITCH_DEFS_H

#include "type_support.h"

#define IA_CSS_PSYS_PARAMETER_TERMINAL_STITCH_SECTION_COUNT      (1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_STITCH_MAX_SIZE_SECTION_0 (sizeof(struct ia_css_stitch_dmem_param))

/**
 * Stitch DMEM parameters.
 * This struct needs to be aligned to 4 bytes.
 */
struct ia_css_stitch_dmem_param {
	int16_t cgl_r;                /* Total channel gain for Red channel for LE*/
	int16_t cgl_gr;               /* Total channel gain for Gr channel for LE*/
	int16_t cgl_gb;               /* Total channel gain for Gb channel for LE*/
	int16_t cgl_b;                /* Total channel gain for B channel for LE*/

	int16_t cgs_r;                /* Total channel gain for Red channel for SE*/
	int16_t cgs_gr;               /* Total channel gain for Gr channel for SE*/
	int16_t cgs_gb;               /* Total channel gain for Gb channel for SE*/
	int16_t cgs_b;                /* Total channel gain for B channel for SE*/

	int16_t bll_cc00;             /* Black level for Gr channel for LE*/
	int16_t bll_cc01;             /* Black level for Red channel for LE*/
	int16_t bll_cc10;             /* Black level for B channel for LE*/
	int16_t bll_cc11;             /* Black level for Gb channel for LE*/

	int16_t bls_cc00;             /* Integer part of Black level for Gr channel for SE*/
	int16_t bls_cc01;             /* Integer part of Black level for Red channel for SE*/
	int16_t bls_cc10;             /* Integer part of Black level for B channel for SE*/
	int16_t bls_cc11;             /* Integer part of Black level for Gb channel for SE*/

	int16_t blr_cc00;             /* Residual part of Black level for Gr channel for SE*/
	int16_t blr_cc01;             /* Residual part of Black level for Red channel for SE*/
	int16_t blr_cc10;             /* Residual part of Black level for B channel for SE*/
	int16_t blr_cc11;             /* Residual part of Black level for Gb channel for SE*/

	int16_t  blend_upper_thr;     /* Upper threshold for image blending */
	uint16_t blend_maxblendval;   /* Log2 of blending transition width */
	uint16_t blend_maxblendval2;  /* Log2 of blending transition width */
	int16_t  blend_shift;         /* normalization shift for blending mask values */

	uint16_t dg_yshift;           /* Shift for luminance for computing ghost mask */
	uint16_t dg_a;                /* Parameter of ghost map transfer function */
	uint16_t dg_s;                /* Parameter of ghost map transfer function */
	uint16_t dg_yfactshift;       /* Shift for luminance factor in deghosting */

	uint16_t dg_yfactoffset;      /* Fact offset for luminance factor in deghosting */
	uint16_t dg_ylothr;           /* Threshold for deghosting luminance sensitivity */
	uint16_t dg_blur;             /* weight for blurring of short exposure in ghosting regions */
	uint16_t dpc_enable;          /* weight for outlier detection in defect pixel correction of short exposure*/

	uint16_t ngain_factor;        /* Normalization gain factor */
	uint16_t ngain_shift;         /* Normalization gain shift */
	int16_t lowlight_value;       /* threshold value for desaturation factor */
	int16_t lowlight_shift;       /* desaturation factor shift */

	int16_t lowlight_filterstrength; /* filter strength for low light desaturation */
	uint16_t padding;                /* 2 bytes padding to make size aligned to 4 bytes */
};

#endif /* __IA_CSS_STITCH_DEFS_H */
