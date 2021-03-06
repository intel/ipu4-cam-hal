/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2012-2018 Intel Corporation
 * All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its
 * suppliers or licensors. Title to the Material remains with Intel
 * Corporation or its suppliers and licensors. The Material may contain trade
 * secrets and proprietary and confidential information of Intel Corporation
 * and its suppliers and licensors, and is protected by worldwide copyright
 * and trade secret laws and treaty provisions. No part of the Material may be
 * used, copied, reproduced, modified, published, uploaded, posted,
 * transmitted, distributed, or disclosed in any way without Intel's prior
 * express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or
 * delivery of the Materials, either expressly, by implication, inducement,
 * estoppel or otherwise. Any license under such intellectual property rights
 * must be express and approved by Intel in writing.
 *
 * Unless otherwise agreed by Intel in writing, you may not remove or alter
 * this notice or any other notice embedded in Materials by Intel or Intels
 * suppliers or licensors in any way.
 */

/*!
 * \file ia_ccat.h
 * \brief Definitions of common analysis functions used by Intel 3A modules.
*/

#ifndef IA_CCAT_H_
#define IA_CCAT_H_

#include "ia_configuration.h"
#include "ia_ccat_types.h"
#include "ia_aiq_types.h"

#ifdef __cplusplus
extern "C" {
#endif
#define IA_CCAT_CCM_MAX_NUM 15
#define IA_CCAT_ACM_SECTORS_MAX_NUM 36

typedef struct ia_ccat_t ia_ccat;
typedef struct frame_info_t ia_ccat_frame_info;

LIBEXPORT ia_ccat*
ia_ccat_init();

/*!
 * \brief (Re)allocates memory for CMC/tunings used inside CCAT.
 * Given CMC structure must be valid throughout lifetime of CCAT.
 */
LIBEXPORT ia_err
ia_ccat_set_tunings(
    ia_ccat *ccat,        /*!< \param[in] Analysis toolbox's internal structure. */
    const ia_cmc_t *cmc); /*!< \param[in] CMC structure that will be stored and copied/modified for CCAT internal use. */

/*!
 * \brief De-initializes and frees memory allocated in ia_ccat_init() function.
 */
LIBEXPORT void
ia_ccat_deinit(ia_ccat *ccat); /*!< \param[in] Analysis toolbox's internal structure. */

/*!
 * \brief Statistics setting and initialization functions.
 *
 * Note: frame_parameters_available and frame_type:
 * CCAT keeps shallow copy of ia_ccat_frame_parameters structure for all frame_types (eg. flash and nonflash frame parameters) and accesses
 * parameters behind given pointers directly during its lifetime. When ia_ccat_set_frame_parameters is called with new set of parameters
 * (and frame_parameters_available is set to true), previously given frame parameters (for the given frame_type) will no longer be used and
 * can be freed/reused by CCAT client.
 * If CCAT client wants to invalidate given frame parameters (for particular frame_type) without new set of parameters, frame_parameters_available
 * flag should be set to false. This needs to be done for all frame_types that CCAT client wants to invalidate.
 *
 * Note: statistics_crop_area:
 * This information is needed to restrict use parameters from Camera Module Characterization (for example LSC),
 * which was done relative to the full sensor resolution (FOV).
 * For example, if sensor captures image size 1600x1200 (4:3 ratio) pixels and only 1600x900 (16:9) area is used from the center.
 * So, image area at top and bottom must not be used (needs to be cropped 150 pixels from top and bottom).
 * statistics_crop_area crop rectangle needs to be given relative to IA_COORDINATE_WIDTH, IA_COORDINATE_HEIGHT found in ia_coordinate.h.
 * Thus given structure in this example case should be:
 *  statistics_crop_area.left = (0*IA_COORDINATE_WIDTH/1600);
 *  statistics_crop_area.top = (150*IA_COORDINATE_HEIGHT/1200);
 *  statistics_crop_area.right = (0*IA_COORDINATE_WIDTH/1600);
 *  statistics_crop_area.bottom = (150*IA_COORDINATE_HEIGHT/1200);
 *
 */
LIBEXPORT ia_err
ia_ccat_set_frame_parameters(
    ia_ccat *ccat,                                                           /*!< \param[in] Analysis toolbox internal data structure. */
    const ia_ccat_frame_statistics *frame_statistics,                        /*!< \param[in] Input statistics */
    const ia_ccat_frame_parameters *frame_parameters);                       /*!< \param[in] Input parameters */

/*!
 * \brief Registers percentile that will be calculated from histograms covering whole frame.
 */
LIBEXPORT ia_err
ia_ccat_register_percentile_frame(
    ia_ccat *ccat,                    /*!< \param[in] Analysis toolbox internal data structure. */
    float percentile);

/*!
 * \brief Reserves frame into use.
 * \return Error code.
 */
LIBEXPORT ia_err
ia_ccat_hold_frame(
    ia_ccat *ccat,                    /*!< \param[in]  Analysis toolbox internal data structure. */
    ia_ccat_frame_type frame_type,    /*!< \param[in]  Frame type to acquire. */
    ia_ccat_frame_info **frame_info); /*!< \param[out] Pointer to acquired frame info. */

LIBEXPORT ia_err
ia_ccat_release_frame(
    ia_ccat *ccat,                    /*!< \param[in] Analysis toolbox internal data structure. */
    ia_ccat_frame_info **frame_info); /*!< \param[in] Frame info pointer that is no longer used. */

LIBEXPORT ia_err
ia_ccat_get_frame_percentile(
    ia_ccat_frame_info* frame_info,
    float percentile,
    unsigned int exposure_index,
    ia_ccat_histogram_type histogram_type,
    float *percentile_bin);

LIBEXPORT ia_err
ia_ccat_get_frame_normalized_percentile(
    ia_ccat_frame_info* frame_info,
    float percentile,
    unsigned int exposure_index,
    ia_ccat_histogram_type histogram_type,
    float *normalized_percentile);

LIBEXPORT ia_err
ia_ccat_get_frame_total_total_gain(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    float *total_gain);

LIBEXPORT ia_err
ia_ccat_get_frame_total_exposure_time(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    unsigned int *total_exposure_time);

LIBEXPORT ia_err
ia_ccat_get_frame_lux_level_estimate(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    float *lux_level_estimate);

LIBEXPORT ia_err
ia_ccat_get_frame_filtered_lux_level_estimate(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    float *filtered_lux_level_estimate);

LIBEXPORT ia_err
ia_ccat_hold_frame_histogram(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_ccat_histogram_type histogram_type,
    const ia_histogram **histogram);

LIBEXPORT ia_err
ia_ccat_release_frame_histogram(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_ccat_histogram_type histogram_type,
    const ia_histogram **histogram);

LIBEXPORT ia_err
ia_ccat_get_frame_histogram_info(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_ccat_histogram_type histogram_type,
    float *mean,
    float *saturation_percent,
    float *max);

LIBEXPORT ia_err
ia_ccat_get_frame_histogram_segment_average(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_ccat_histogram_type a_histogram_type,
    float low_limit,
    float high_limit,
    float *average);

LIBEXPORT ia_err
ia_ccat_get_frame_ae_results(
    ia_ccat_frame_info *frame_info,
    const ia_aec_results **ae_results);

LIBEXPORT ia_err
ia_ccat_get_frame_awb_results(
    ia_ccat_frame_info *frame_info,
    const ia_aiq_awb_results **awb_results);

LIBEXPORT ia_err
ia_ccat_get_frame_af_results(
    ia_ccat_frame_info *frame_info,
    const ia_aiq_af_results **af_results);

LIBEXPORT ia_err
ia_ccat_get_frame_exposure_result(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_aec_exposure_result *exposure_result);

LIBEXPORT ia_err
ia_ccat_get_frame_exposure_parameters(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_aec_exposure_parameters *exposure_parameters);

#ifdef IA_AEC_FEATURE_WEIGHT_GRID
LIBEXPORT ia_err
ia_ccat_get_frame_histogram_weight_map(
    ia_ccat_frame_info *frame_info,
    const ia_aec_weight_grid **weight_grid);
#endif

LIBEXPORT ia_err
ia_ccat_get_frame_sensor_exposure_parameters(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    ia_aiq_exposure_sensor_parameters *exposure_sensor_parameters);

LIBEXPORT ia_err
ia_ccat_get_frame_color_gains(
    ia_ccat_frame_info *frame_info,
    ia_aiq_color_channels *color_gains);

LIBEXPORT ia_err
ia_ccat_get_frame_color_correction_matrix(
    ia_ccat_frame_info *frame_info,
    float(*matrix)[3][3]);

LIBEXPORT ia_err
ia_ccat_get_frame_timestamp(
    ia_ccat_frame_info *frame_info,
    unsigned long long *frame_timestamp);

/*!
 * \brief Get information about number of exposures and which of them is the center (in case of multi exposure frame) exposure index.
 */
LIBEXPORT ia_err
ia_ccat_get_frame_exposure_index_info(
    ia_ccat_frame_info *frame_info,                                    /*!< [in] Frame handle. */
    unsigned int *center_exposure_index,                               /*!< [out] Center (in case of multiple exposures) / default exposure index . */
    unsigned int *num_exposures);                                      /*!< [out] Number of exposures/statistics in the frame. */

/*!
 * \brief Get closest ACMs for a white point.
 * Note: Only CCMs are updated in out_acm structure.
 */
LIBEXPORT ia_err
ia_ccat_calculate_weighted_acm(
    ia_ccat_frame_info *frame_info,                                    /*!< [in] Frame handle. */
    const cmc_parsed_color_matrices_t *parsed_color_matrices,          /*!< [in] CCM characterization data with R/G and B/G chromaticities. */
    const cmc_parsed_advanced_color_matrices_ls_t *parsed_acm_ls,      /*!< [in] ACMs from CMC or AIQ tunings. */
    unsigned int sector_count,                                         /*!< [in] Number of sectors in ACMs. */
    cmc_chromaticity point,                                            /*!< [in] Chromaticity to calculate distance (x and y axis) against input list of chromaticities in R/G and B/G plane. */
    float (*out_acm)[3][3],                                            /*!< [out] Resulting ACMs. Array length is defined by sector_count. */
    float (*out_ccm)[3][3]);                                           /*!< [out] Resulting CCM. */

/*!
 * \brief Get closest CCMs for a white point.
 */
LIBEXPORT ia_err
ia_ccat_calculate_weighted_ccm(
    ia_ccat_frame_info *frame_info,                                    /*!< [in] Frame handle. */
    const cmc_parsed_color_matrices_t *parsed_color_matrices,          /*!< [in] CCM characterization data with R/G and B/G chromaticities. */
    bool output_ccm_type_preferred,                                    /*!< [in] Flag to control output CCM to be preferred (true) or accurate (false). */
    cmc_chromaticity point,                                            /*!< [in] Chromaticity to calculate distance (x and y axis) against input list of chromaticities in R/G and B/G plane. */
    float (*out_ccm)[3][3]);                                           /*!< [out] Resulting CCM. */

LIBEXPORT ia_err
ia_ccat_calculate_chromaticity_based_weights(
    ia_ccat_frame_info *frame_info,                                    /*!< [in] Frame handle. */
    const cmc_parsed_color_matrices_t *parsed_color_matrices,          /*!< [in] CCM characterization data with R/G and B/G chromaticities. */
    unsigned int num_chromaticities,                                   /*!< [in] Number of input chromaticities. */
    cmc_chromaticity point,                                            /*!< [in] Chromaticity to calculate distance (x and y axis) against input list of chromaticities in R/G and B/G plane. */
    const float *ir_proportion,                                        /*!< [in] Ir effect on chromaticity point distance (z axis). */
    float(*weights)[IA_CCAT_CCM_MAX_NUM]);                             /*!< [out] Normalized chromaticity distances translated into weights. */

#ifdef IA_AEC_FEATURE_FLASH
/*!
 * \brief Calculate preferred flash ratio for multi-flash using non-flash white point information. flash_ratio scale between 0-100
 */
LIBEXPORT ia_err
ia_ccat_calculate_flash_ratios(
    const cmc_multi_led_flash_t *flash_tunings,
    ia_ccat_frame_info *nonflash_frame_info,
    float (*flash_ratios)[IA_AEC_FLASHES_NUM]);
#endif

#ifdef IA_CCAT_IR_GRID_ENABLED
LIBEXPORT ia_err
ia_ccat_get_frame_ir_grid(
    ia_ccat_frame_info *frame_info,
    const ia_ccat_grid_char **ir_grid);

LIBEXPORT ia_err
ia_ccat_get_frame_ir_histogram_info(
    ia_ccat_frame_info *frame_info,
    float *mean,
    float *saturation_percent,
    float *max);
#endif

#ifdef IA_CCAT_RGBS_GRID_ENABLED
LIBEXPORT ia_err
ia_ccat_get_frame_rgbs_grid(
    ia_ccat_frame_info *frame_info,
    bool shading_corrected,
    unsigned int exposure_index,
    const ia_rgbs_grid **rgbs_grid);

LIBEXPORT ia_err
ia_ccat_get_frame_af_grid(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_filter_response_grid **af_grid);

/*!
* \brief Retrieve LSC tables that are scaled to match RGBS grid size.
* \param[in] ccat Analysis toolbox's internal structure
* \param[in] cmc     CMC structure that will be stored and copied/modified for CCAT internal use.
*/
LIBEXPORT ia_err
ia_ccat_get_frame_scaled_lsc(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    cmc_lens_shading_correction **scaled_lsc);

LIBEXPORT ia_err
ia_ccat_get_frame_scaled_sa_results(
    ia_ccat_frame_info *frame_infor,
    unsigned int exposure_index,
    const ia_aiq_sa_results_v1 **scaled_sa_results);

#ifdef IA_CCAT_HSV_GRID_ENABLED
LIBEXPORT ia_err
ia_ccat_get_frame_hsv_grid(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_ccat_hsv_grid **hsv_grid);
#endif

#ifdef IA_CCAT_LUMINANCE_GRID_ENABLED
LIBEXPORT ia_err
ia_ccat_get_frame_luminance_grid(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_ccat_grid_char **luminance_grid);

#ifdef IA_CCAT_LUMINANCE_MOTION_ESTIMATE_ENABLED
LIBEXPORT ia_err
ia_ccat_get_frame_motion_level_estimate(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    float *motion_estimate);
#endif
#endif

#ifdef IA_CCAT_ROI_ANALYSIS_ENABLED
LIBEXPORT ia_err
ia_ccat_register_percentile_roi(
    ia_ccat *ccat,                    /*!< \param[in] Analysis toolbox internal data structure. */
    float percentile);

LIBEXPORT ia_err
ia_ccat_hold_frame_histogram_roi(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_rectangle *roi_area,
    ia_ccat_histogram_type histogram_type,
    const ia_histogram **histogram);

LIBEXPORT ia_err
ia_ccat_release_frame_histogram_roi(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_rectangle *roi_area,
    ia_ccat_histogram_type histogram_type,
    const ia_histogram **histogram);

LIBEXPORT ia_err
ia_ccat_get_frame_histogram_info_roi(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_rectangle *roi_area,
    ia_ccat_histogram_type histogram_type,
    float *mean,
    float *saturation_percent,
    float *max);

LIBEXPORT ia_err
ia_ccat_get_frame_percentile_roi(
    ia_ccat_frame_info *frame_info,
    float percentile,
    unsigned int exposure_index,
    const ia_rectangle *roi_area,
    ia_ccat_histogram_type histogram_type,
    float *percentile_bin);
#endif /* IA_CCAT_ROI_ANALYSIS_ENABLED */

#ifdef IA_CCAT_FACE_ANALYSIS_ENABLED
LIBEXPORT ia_err
ia_ccat_register_percentile_face(
    ia_ccat *ccat,                    /*!< \param[in] Analysis toolbox internal data structure. */
    float percentile);

LIBEXPORT ia_err
ia_ccat_get_face_stencil(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_rectangle *face_area,
    const ia_ccat_grid_char **stencil_mask);

LIBEXPORT ia_err
ia_ccat_get_frame_faces(
    ia_ccat_frame_info *frame_info,
    unsigned int *num_faces,
    const ia_face_roi **faces);

LIBEXPORT ia_err
ia_ccat_get_face_coverage(
    const ia_face_roi *face,
    float *coverage);

LIBEXPORT ia_err
ia_ccat_get_frame_face_y_mean(
    ia_ccat_frame_info *frame_info,
    unsigned int exposure_index,
    const ia_face_roi *face,
    float *face_y_mean);

LIBEXPORT ia_err
ia_ccat_get_frame_percentile_face(
    ia_ccat_frame_info *frame_info,
    float percentile,
    unsigned int exposure_index,
    const ia_face_roi *face,
    ia_ccat_histogram_type histogram_type,
    float *percentile_bin);
#endif /* IA_CCAT_FACE_ANALYSIS_ENABLED */
#endif /* IA_CCAT_RGBS_GRID_ENABLED */

#ifdef IA_CCAT_EXTERNAL_SENSORS_ENABLED
/*
* Sensor event functions.
*/
/*!
* \brief Set the accelerometer sensor events to CCAT internal circular buffer.
* Initialize sensor_events structure. Const fields in the structure are assumed to be initialized before calling this function.
*/
LIBEXPORT ia_err
ia_ccat_set_sensor_events_accelerometer(
    ia_ccat *ccat,                                     /*!< [in, out] Analysistoolbox internal data structures. */
    unsigned int num_events,                           /*!< [in] Number of accelerometer sensor events. */
    const ia_ccat_motion_sensor_event *sensor_events); /*!< [in] Structure containing accelerometer events for given time range. */

/*!
* \brief Get a copy of accelerometer events.
* Outputs all events within given timestamps.
*/
LIBEXPORT ia_err
ia_ccat_get_sensor_events_accelerometer(
    const ia_ccat *ccat,                               /*!< [in] Analysistoolbox internal data structures. */
    unsigned long long start_timestamp,                /*!< [in]  Start time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned long long end_timestamp,                  /*!< [in]  End time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned int *num_events,                          /*!< [in]  Number of events allowed to be copied (maximum amount of available memory in sensor_events).
                                                       [out] How many events were copied to the sensor_events structure. */
    ia_ccat_motion_sensor_event* sensor_events);       /*!< [out] Copied accelerometer events. */

/*!
* \brief Set the gyroscope sensor events to CCAT internal circular buffer.
*/
LIBEXPORT ia_err
ia_ccat_set_sensor_events_gyroscope(
    ia_ccat *ccat,                                     /*!< [in, out] Analysistoolbox internal data structures. */
    unsigned int num_events,                           /*!< [in] Number of gyroscope sensor events. */
    const ia_ccat_motion_sensor_event *sensor_events); /*!< [in] Structure containing gyroscope events for given time range. */

/*!
* \brief Get a copy of gyroscope events.
* Outputs all events within given timestamps.
*/
LIBEXPORT ia_err
ia_ccat_get_sensor_events_gyroscope(
    const ia_ccat *ccat,                               /*!< [in]  Analysistoolbox internal data structures. */
    unsigned long long start_timestamp,                /*!< [in]  Start time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned long long end_timestamp,                  /*!< [in]  End time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned int *num_events,                          /*!< [in]  Number of events allowed to be copied (maximum amount of available memory in sensor_events).
                                                       [out] How many events were copied to the sensor_events structure. */
    ia_ccat_motion_sensor_event* sensor_events);       /*!< [out] Copied gyroscope events. */

/*!
* \brief Set the gravity sensor events to CCAT internal circular buffer.
*/
LIBEXPORT ia_err
ia_ccat_set_sensor_events_gravity(
    ia_ccat *ccat,                                     /*!< [in, out] Analysistoolbox internal data structures. */
    unsigned int num_events,                           /*!< [in] Number of gravity sensor events. */
    const ia_ccat_motion_sensor_event *sensor_events); /*!< [in] Structure containing gravity events for given time range. */

/*!
* \brief Get a copy of gravity events.
* Outputs all events within given timestamps.
*/
LIBEXPORT ia_err
ia_ccat_get_sensor_events_gravity(
    const ia_ccat *ccat,                               /*!< [in]  Analysistoolbox internal data structures. */
    unsigned long long start_timestamp,                /*!< [in]  Start time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned long long end_timestamp,                  /*!< [in]  End time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned int *num_events,                          /*!< [in]  Number of events allowed to be copied (maximum amount of available memory in sensor_events).
                                                       [out] How many events were copied to the sensor_events structure. */
    ia_ccat_motion_sensor_event* sensor_events);       /*!< [out] Copied gravity events. */

/*!
* \brief Set the ambient light sensor events to CCAT internal circular buffer.
*/
LIBEXPORT ia_err
ia_ccat_set_sensor_events_ambient_light(
    ia_ccat *ccat,                                     /*!< [in, out] Analysistoolbox internal data structures. */
    unsigned int num_events,                           /*!< [in] Number of ambient light sensor events. */
    const ia_ccat_ambient_light_event *sensor_events); /*!< [in] Structure containing ambient light events for given time range. */

/*!
* \brief Get a copy of ambient light events.
* Outputs all events within given timestamps.
*/
LIBEXPORT ia_err
ia_ccat_get_sensor_events_ambient_light(
    const ia_ccat *ccat,                               /*!< [in]  Analysistoolbox internal data structures. */
    unsigned long long start_timestamp,                /*!< [in]  Start time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned long long end_timestamp,                  /*!< [in]  End time for events to be copied. 0 if all data (max_num_events) must be copied. */
    unsigned int *num_events,                          /*!< [in]  Number of events allowed to be copied (maximum amount of available memory in sensor_events).
                                                            [out] How many events were copied to the sensor_events structure. */
    ia_ccat_ambient_light_event* sensor_events);       /*!< [out] Copied ambient light events. */

                                                       /* Following functions require frame_info structure as input. Client should call ia_ccat_hold_frame() to get the frame handle.
                                                       * Once frame handle is no longer used, ia_ccat_release_frame() must be called. */
#endif /* IA_CCAT_EXTERNAL_SENSORS_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* IA_CCAT_H_ */
