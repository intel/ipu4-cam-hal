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
#ifndef __CISU_TYPES_H__
#define __CISU_TYPES_H__

#include <stdint.h>
#include <string>
#include <ia_types.h>
#include <cisu_dll.h>

namespace cisu {
/**
 * CISU error codes. Used in the public interface
 * \ingroup cisu_public
 * \brief Error codes.
 */
enum  CisuError {
    cisu_ok              = 0,       /*!< No errors*/
    cisu_err_general     = (1 << 0), /*!< General error */
    cisu_err_no_mem      = (1 << 1), /*!< Out of memory */
    cisu_err_internal    = (1 << 2), /*!< Error in code */
    cisu_err_argument    = (1 << 3), /*!< Invalid argument for a function */
    cisu_err_disabled    = (1 << 4), /*!< Functionality is disabled */
    cisu_err_would_block = (1 << 5), /*!< call would block the caller */
    cisu_err_timeout     = (1 << 6), /*!< time out */
    cisu_err_not_found   = (1 << 7), /*!< item not found */
    cisu_err_bad_value   = (1 << 8), /*!< bad value */
    cisu_err_unknown     = (1 << 9), /*!< unknown error */
    cisu_err_not_init    = (1 << 10), /*!< Not initialized */
    /* remember to update error.cpp ErrorToString function! */
    cisu_err_max
};

/**
 * Generic data blob object to transfer data.
 * \ingroup cisu_private
 * \brief Object to transfer data.
 */

struct CisuBlob {
    size_t mSize;
    void *mData;

    void copyToIaBinary(ia_binary_data &ret) const {
        ret.data = mData;
        ret.size = (unsigned int)mSize;
    }
};

/**
 * returns a textual representation of the error
 */
CISU_EXPORT_TO_DLL std::string ErrorToString(int error);

}

#endif
