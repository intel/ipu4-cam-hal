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
#ifndef LIBCISU_INCLUDE_CISU_I_RESULT_H_
#define LIBCISU_INCLUDE_CISU_I_RESULT_H_

#include <cisu_i_buffer.h>
#include <cisu_i_metadata.h>
#include <cisu_dll.h>
#include <memory>

namespace cisu {

/** \addtogroup cisu_request_result
 *  @{
 */

/**
 * \class CaptureResult
 *
 * Complete or partial results of a capture request.
 * The results of a capture request may be returned to the client in multiple
 * calls of ClientCallbacks::processCaptureResult().
 *
 * Objects of this class are reference counted using the std::shared_ptr.
 * Clients only get access to shared_ptr<CaptureResult>.
 * All the methods available to the clients are const.
 * Once a client is done using a reference to this objects it can simply destroy
 * the shared pointer.
 *
 * Clients cannot create objects of this class.
 *
 * The PartialId must be 0 if the result contains no metadata. For results which
 * contain metadata, the Id must be between 1 and the number of partials defined
 * in static metadata (TODO: define that metadata, meanwhile the number of
 * partials is to be considered always as 1, leaving this value as either 0 for
 * just buffers and 1 for metadata and possibly buffers).
 *
 * The maximum amount of CaptureResult callbacks for a single capture request is
 * the number of output buffers + the number of input buffers + the number of
 * partials. E.g. a request with 4 output buffers and 1 input buffer may produce
 * up to 6 callbacks (one for each buffer and one for metadata) or more if
 * partial metadata is used. Or on the other hand the results may all be sent in
 * a single callback carrying all buffers and the metadata.
 *
 */
class CISU_EXPORT_TO_DLL ICaptureResult {
public:
    virtual int32_t getFrameNumber() const = 0;
    virtual int32_t getPartialId() const = 0;
    virtual std::shared_ptr<const ICisuMetadata> getMetadata() const = 0;
    virtual std::vector<std::shared_ptr<ICisuImageBuffer>>& getOutputBuffers() = 0;
    virtual std::vector<std::shared_ptr<ICisuImageBuffer>>& getInputBuffers() = 0;
    virtual std::shared_ptr<ICisuImageBuffer>& getOuputStreamBuffer(std::string &portName) = 0;
    virtual std::shared_ptr<ICisuImageBuffer>& getInputStreamBuffer(std::string &portName) = 0;
    virtual void *getClientPrivPtr() const = 0;
    virtual CisuError getStatus() const = 0;
};
/**
 *  @}
 */
}
#endif /* LIBCISU_INCLUDE_CISU_I_RESULT_H_ */
