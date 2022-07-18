/*
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

#ifndef __IA_CSS_S3A_HDR_DEFS_H
#define __IA_CSS_S3A_HDR_DEFS_H

#include "type_support.h"

/** @file
 * This file contains S3A_HDR runtime parameters
 **/

/*
 * WARNINGS
 * - "IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE" is designated to
 *   have a fixed value as "32" (equal to ISP_NWAY).
 *   Please do not change it to a different value.
 */
#define	IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE				(32)

/*
 * Internal kernel defines
 *
 * TODO: Check if this the right place.
 */
#ifndef _ISP_BQS
#define _ISP_BQS(num)							((num) / 2)
#endif
#define S3A_HDR_NUM_VECTORS						(64) /* TODO: Change to correct calculation */
#define S3A_HDR_NUM_ELEMENTS						(S3A_HDR_NUM_VECTORS * ISP_VEC_NELEMS)

#define S3A_HDR_MAX_BQ_GRID_WIDTH					(240)
#define S3A_HDR_MAX_BQ_GRID_HEIGHT					(135)

/* Multiply by 3 for RGBS, RGBY, and YDRC. Make it 4 for easy division. */
#define S3A_HDR_NUM_STATS						(4)
#define S3A_HDR_HI_LO_STRIDE						(S3A_HDR_NUM_ELEMENTS * S3A_HDR_NUM_STATS)

#define S3A_HDR_BPP							(15)
#define S3A_HDR_BPP_OUT							(20)
#define S3A_HDR_MAX_VAL							((1 << S3A_HDR_BPP) - 1)

/*
 * TODO: Add program terminals
 */

/*
 * S3A_HDR parameter terminal section
 */
#define IA_CSS_PSYS_PARAMETER_TERMINAL_S3A_HDR_SECTION_COUNT		(1)
#define IA_CSS_PSYS_PARAMETER_TERMINAL_S3A_HDR_MAX_SIZE_SECTION_0	(sizeof(struct ia_css_s3a_hdr_param))

/*
 * S3A_HDR spatial parameter terminals and sections
 *
 * Each of the statistics maps has its own spatial terminal. The size of the maps is controlled by the PAL library.
 * The exact value is defined by the frame resolution and passed downscale parameters. There are three resulting
 * statistics: RGBY, RGBS, and YDRC. RGBS statistics have two sections since, due to the decompression, data may
 * be stored in double precision. Section 0 is RGBS.lo, and section 1 is RGBS.hi.
 *
 * Spatial terminal memory layout is depicted below. Depending on the downscale factor, a special case is illustrated
 * for each of the terminals. The actual size of the maps (i.e. spatial terminal) is defined by the image resolution
 * and the downscale factor parameter defined by the PAL library and/or tuning and passed to the S3A_HDR kernel via
 * parameter terminals. Element size for each of the sections is the same and is equal to the NWAY of the targeted
 * processor (32 elements in IPU4i). There is either 8, 4, 2, or 1 consecutive grid pixels within a basic section
 * element for the same channel.
 *
 * The users of the spatial parameter terminals needs to ensure that a proper stride is set during the
 * memory allocation. Offset for each of the sections is defined by the element size, width, and height.
 *
 * RGBY example for a 128x128 frame:
 *
 * <----------------------------------- N = 64 / NWAY = 2 ------------------------------------>
 *
 * --------------------------------------------------------------------------------------------   ^
 * | (8*G, 8*R, 8*B, 8*Y)[0][0]   |            ...            | (8*G, 8*R, 8*B, 8*Y)[0][N-1]   |  |
 * --------------------------------------------------------------------------------------------   |
 *                                              .                                                 |
 *                                              .                                               M = 16
 *                                              .                                                 |
 * --------------------------------------------------------------------------------------------   |
 * | (8*G, 8*R, 8*B, 8*Y)[M-1][0] |            ...            | (8*G, 8*R, 8*B, 8*Y)[M-1][N-1] |  |
 * --------------------------------------------------------------------------------------------   v
 *
 * RGBS example for a 128x128 frame and dcn_mode = 2 (32x32 downscale):
 *
 * <-------------------------------------------- N = 64 / NWAY = 2 --------------------------------------------->
 *
 * --------------------------------------------------------------------------------------------------------------   ^
 * | (2*G, 2*R, 2*B, 2*S, 24*zero)[0][0]   |            ...            | (2*G, 2*R, 2*B, 2*S, 24*zero)[0][N-1]   |  |
 * --------------------------------------------------------------------------------------------------------------   |
 *                                                       .                                                          |
 *                                                       .                                                        M = 4
 *                                                       .                                                          |
 * --------------------------------------------------------------------------------------------------------------   |
 * | (2*G, 2*R, 2*B, 2*S, 24*zero)[M-1][0] |            ...            | (2*G, 2*R, 2*B, 2*S, 24*zero)[M-1][N-1] |  |
 * --------------------------------------------------------------------------------------------------------------   v
 * | (2*G, 2*R, 2*B, 2*S, 24*zero)[0][0]   |            ...            | (2*G, 2*R, 2*B, 2*S, 24*zero)[0][N-1]   |
 * --------------------------------------------------------------------------------------------------------------
 *                                                       .
 *                                                       .
 *                                                       .
 * --------------------------------------------------------------------------------------------------------------
 * | (2*G, 2*R, 2*B, 2*S, 24*zero)[M-1][0] |            ...            | (2*G, 2*R, 2*B, 2*S, 24*zero)[M-1][N-1] |
 * --------------------------------------------------------------------------------------------------------------
 *
 * As illustrated above, packaging of valid data per section element depends on dcn_mode selected by the user.
 *  if (dcn_mode = 0) --> (8*G, 8*R, 8*B, 8*S)
 *  if (dcn_mode = 1) --> (4*G, 4*R, 4*B, 4*S, 16*zero)
 *  if (dcn_mode = 2) --> (2*G, 2*R, 2*B, 2*S, 24*zero)
 *  if (dcn_mode = 3) --> (1*G, 1*R, 1*B, 1*S, 28*zero)
 *
 *  RGBS has 2 sections. section0 is for the lower order bits and section1 is
 *  for the higher order bits. This will be used when the decomp_mode is
 *  enabled as the output of that is 20 bits. Currently decomp_mode is not
 *  supported and the output of section1 will be 0.
 *
 * YDRC example for a 128x128 frame and y_grid_ss2_bypass = 0:
 *
 * <---------------------------------- N = 64 / NWAY = 2 ----------------------------------->
 *
 * ------------------------------------------------------------------------------------------   ^
 * | (4*Y, 4*V, 24*zero)[0][0]   |            ...            | (4*Y, 4*V, 24*zero)[0][N-1]   |  |
 * ------------------------------------------------------------------------------------------   |
 *                                             .                                                |
 *                                             .                                              M = 8
 *                                             .                                                |
 * ------------------------------------------------------------------------------------------   |
 * | (4*Y, 4*V, 24*zero)[M-1][0] |            ...            | (4*Y, 4*V, 24*zero)[M-1][N-1] |  |
 * ------------------------------------------------------------------------------------------   v
 *
 * As illustrated above, packaging of valid data per section element depends on y_grid_ss2_bypass selected by the user.
 * Both Y (luma channel) and V (maximum of R, G, and B channels) components are provided.
 *  if (y_grid_ss2_bypass = 0) --> (4*Y, 4*V, 24*zero)
 *  if (y_grid_ss2_bypass = 1) --> (8*Y, 8*V, 16*zero)
 *
 */
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBY_SECTION_COUNT	(1)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBY_ID_SECTION_0	(0)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBY_ELEM_SIZE_SECTION_0	\
						(IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE * sizeof(uint16_t))
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBS_SECTION_COUNT	(2)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBS_ID_SECTION_0	(0)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBS_ELEM_SIZE_SECTION_0	\
						(IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE * sizeof(uint16_t))
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBS_ID_SECTION_1	(1)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_RGBS_ELEM_SIZE_SECTION_1	\
						(IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE * sizeof(uint16_t))
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_YDRC_SECTION_COUNT	(1)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_YDRC_ID_SECTION_0	(0)
#define IA_CSS_PSYS_SPATIAL_PARAMETER_TERMINAL_YDRC_ELEM_SIZE_SECTION_0	\
						(IA_CSS_S3A_HDR_ARRAY_ACCESS_SIZE * sizeof(uint16_t))

struct ia_css_s3a_hdr_param {
	uint16_t rgbs_grid_enable;		/**< Enable RGBS grid collection */
	uint16_t y_grid_enable;			/**< Enable Y grid collection */
	uint16_t rgby_grid_enable;		/**< Enable RGBY grid collection */
	uint16_t dcn_mode;			/**< Decimation factor mode (bypass, 2x2, 4x4, 8x8) */
	uint16_t sat_thr;			/**< Saturation threshold for each of the Bayer channels */
	uint16_t y_grid_mode;			/**< Y grid mode selection (Avg(R, G, B) or Max(R, G, B) */
	uint16_t y_grid_ss2_bypass;		/**< Bypass 2x2 decimation during Y grid collection */
	uint16_t decomp_bypass;			/**< Bypass decompression for the RGBS grid */
};

#endif /* __IA_CSS_S3A_HDR_DEFS_H */
