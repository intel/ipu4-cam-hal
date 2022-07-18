/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2017 Intel Corporation. All Rights Reserved.
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
#ifndef LIBCISU_OS_PLUGINS_INCLUDE_CISU_OS_BUFFER_DUMP_H_
#define LIBCISU_OS_PLUGINS_INCLUDE_CISU_OS_BUFFER_DUMP_H_

#include <string>
#include "cisu_types.h"
#include "cisu_buffer.h"

namespace cisu {
namespace os_buffer_dump
{
/** \addtogroup cisu-os-plugin
 *  @{
 */
/**
 * dumpBuffer
 *
 * Dumps the contents of a buffer to file
 *
 * \param [in] buffer Reference to the buffer to dump
 * \param [in] cameraId Identifier for the cisu device the dump originated
 * \param [in] frameNumber
 * \param [in] name String containing a textual identification of the dump
 */
CisuError dumpBuffer(ICisuBuffer &buffer,
                    int32_t cameraId,
                    int32_t frameNumber,
                    std::string &name);

/**
 * dumpImageBuffer
 *
 * Dumps the contents of an image buffer to file
 *
 * \param [in] buffer Reference to the buffer to dump
 * \param [in] cameraId Identifier for the cisu device the dump originated
 * \param [in] frameNumber
 * \param [in] name String containing a textual identification of the dump
 */
CisuError dumpImageBuffer(ICisuImageBuffer &buffer,
                          int32_t cameraId,
                          int32_t frameNumber,
                          std::string &name);
/**  @{ */
}
}


#endif /* LIBCISU_OS_PLUGINS_INCLUDE_CISU_OS_BUFFER_DUMP_H_ */
