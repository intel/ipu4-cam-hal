/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2017-2018 Intel Corporation. All Rights Reserved.
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
#ifndef LIBCISU_INCLUDE_CISU_I_REQUEST_H_
#define LIBCISU_INCLUDE_CISU_I_REQUEST_H_


#include <cisu_i_metadata.h>
#include <cisu_types.h>
#include <cisu_i_buffer.h>
#include <vector>
#include <memory>
#include <mutex>

namespace cisu {

class ICaptureRequest;

/** \addtogroup cisu_request_result
 *  @{
 */

/**
 * \class RequestBuilder
 * Utility class to create or recycle capture requests in steps.
 * This helps us ensure that all request have the minimum correct structure.
 * \sa CaptureRequest
 */
class IRequestBuilder {
public:
    virtual ~IRequestBuilder() = 0;
    virtual CisuError init(int32_t frameNumber) = 0;
    virtual CisuError reInit(std::shared_ptr<ICaptureRequest> oldRequest,
        int32_t frameNumber) = 0;

    virtual CisuError addInputBuffer(std::shared_ptr<ICisuImageBuffer> &buf) = 0;
    virtual CisuError addOutputBuffer(std::shared_ptr<ICisuImageBuffer> &buf) = 0;
    virtual CisuError addSettings(std::shared_ptr<const ICisuMetadata> &settings) = 0;
    virtual CisuError build(std::shared_ptr<ICaptureRequest>& out) = 0;
    virtual void clear() = 0;
    virtual void destroy(IRequestBuilder *builder) = 0;
};

/**
 * \class CaptureRequest
 *
 * Represents a single iteration of the camera subsystem graph with detailed
 * settings for the 3A algorithms.
 * Each of the buffers are associated with an input/output stream.
 * If no input buffers are available it is assumed that the data will come
 * from an Imaging sensor, present in the graph.
 *
 * Capture request objects cannot be created, only built using the class
 * RequestBuilder
 *
 */
class ICaptureRequest {
public:
    virtual int32_t getFrameNumber() const = 0;
    virtual std::shared_ptr<const ICisuMetadata> getSettings() const = 0;
    virtual const std::vector<std::shared_ptr<ICisuImageBuffer>>& getOutputBuffers() const = 0;
    virtual const std::vector<std::shared_ptr<ICisuImageBuffer>>& getInputBuffers() const = 0;
    virtual const std::shared_ptr<ICisuImageBuffer>& getOutputStreamBuffer(const std::string &portName) const = 0;
    virtual const std::shared_ptr<ICisuImageBuffer>& getInputStreamBuffer(const std::string &portName) const = 0;
};

/**
 *  @}
 */
}

extern "C" CISU_EXPORT_TO_DLL cisu::IRequestBuilder* CISU_DLL_API GetRequestBuilder();

#endif /* LIBCISU_INCLUDE_CISU_I_REQUEST_H_ */
