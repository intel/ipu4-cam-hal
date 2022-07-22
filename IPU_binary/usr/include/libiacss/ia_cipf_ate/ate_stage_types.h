/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2018 Intel Corporation. All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to
 * the source code ("Material") are owned by Intel Corporation or its suppliers
 * or licensors. Title to the Material remains with Intel Corporation or its
 * suppliers and licensors. The Material contains trade secrets and proprietary
 * and confidential information of Intel or its suppliers and licensors. The
 * Material is protected by worldwide copyright and trade secret laws and
 * treaty provisions. No part of the Material may be used, copied, reproduced,
 * modified, published, uploaded, posted, transmitted, distributed, or
 * disclosed in any way without Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery
 * of the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be
 * express and approved by Intel in writing.
 */

#ifndef IA_CAMERA_CIPF_ATE_ATE_STAGE_TYPES_H_
#define IA_CAMERA_CIPF_ATE_ATE_STAGE_TYPES_H_

/* Avoid new includes in this header, especially ATE-related ones. For now this
 * header can be safely exported and included. */
#include <stdint.h>
#include <map>
extern "C" {
#include "ia_cipf/ia_cipf_types.h"
}

// map of terminal uid and last kernel connected to terminal
typedef std::map<unsigned int, std::string> uidLinkPairMap;

struct FrameOutputPayload {
    ia_cipf_payload_t outputPayload;
    std::string kernelOutputName;
};

/*
 * simulation related parsing container
 */
struct SimulationControls {
    int mode;             /*< mode of simulation. Still or Video. */
    bool provideAAAstats; /*< whether AAA stats are expected from the simulation run */
    bool provideDVSstats; /*< whether DVS stats are expected from the simulation run */
    bool provideImage;    /*< whether output image is expected from the simulation run */
    const uint8_t *dumpFilters; /*< filter names which should dump outputs */
    const uint8_t *dumpPath; /*< path which should be used for filter dumps*/
    int  dumpFileNameLength; /*< filter dump file name length */
    int  dumpFileNameCount; /*< filter dump file name count */
    const uint8_t *inputFileNames; /*< input file names in the filename@pin@filter format */
    int  inputFileNameLength; /*< length of single inputFileName in the filename@pin@filter format */
    int  inputFileNameCount; /*< input file name count */
    bool relativeInputFilePath; /*< true == relative, false == absolute */

    void reset() {
        mode = 0;
        provideAAAstats = false;
        provideDVSstats = false;
        provideImage = false;
        dumpFilters = nullptr;
        dumpPath = nullptr;
        dumpFileNameLength = 0;
        dumpFileNameCount = 0;
        inputFileNames = nullptr;
        inputFileNameLength = 0;
        inputFileNameCount = 0;
        relativeInputFilePath = false;
    }
    SimulationControls() {
        reset();
    }
};

#endif /* IA_CAMERA_CIPF_ATE_ATE_STAGE_TYPES_H_ */
