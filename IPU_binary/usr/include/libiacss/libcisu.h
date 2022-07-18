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
#ifndef __LIBCISU_H__
#define __LIBCISU_H__

// TODO: Metadata definitions
#include <cisu_types.h>
#include <cisu_i_request.h>
#include <cisu_i_result.h>
#include <cisu_dll.h>
#include <memory>
#include <mutex>

namespace GCSS {
class IGraphConfig;
}
namespace cisu {
class ICisuDevice;
class Cisu3AEngine;
class RequestPU;
class PipelinePU;
class CameraControlPU;

/** \addtogroup cisu_public
 *  @{
 */

/**
 * \enum CisuRequestTemplate
 *
 * Available settings templates returned by the method
 * CisuDevice::createDefaultRequestSettings().
 *
 */
enum CisuRequestTemplate {
    CISU_TEMPLATE_PREVIEW,
    CISU_TEMPLATE_VIDEO_RECORD,
    CISU_TEMPLATE_STILL_CAPTURE,
    CISU_TEMPLATE_VIDEO_SNAPSHOT,
    CISU_TEMPLATE_STILL_COUNT
};
/**
 * \enum CisuNotificationTypes
 *
 * Types of notification produced by a cisu device.
 */
enum CisuNotificationTypes {
    /**
     * An error occurred with a previous capture request.
     */
    CISU_ERROR_MESSAGE,
    /**
     * Information about the time the capture was done
     */
    CISU_SHUTTER_MESSAGE
};

/**
 * \enum DeviceErrorCodes
 *
 * Types of error a device may report to its client
 */
enum DeviceErrorCodes {
    /**
     * Critical Error, the device should be closed.
     */
    CISU_ERROR_DEVICE,
    /**
     * An error occurred processing a request. No result will be produced for
     * the mentioned request (by frame number). This is a recoverable error.
     * Subsequent request should continue.
     */
    CISU_ERROR_REQUEST,
    /**
     * Information in the result metadata is not correct and should be discarded.
     * The buffers associated with the request should be valid.
     * This is a recoverable error. Subsequent capture request should succeed.
     */
    CISU_ERROR_RESULT,
    /**
     * An error occurred filling a buffer of a stream. Details of the request
     * and the stream will be available. Recoverable error. Clients should
     * continue as usual, just discarding the buffer.
     */
    CISU_ERROR_BUFFER,
    /**
     * Number of errors possible, not an error.
     */
    CISU_ERROR_MAX
};

/**
 * \struct CisuShutterMessage
 * This structure is passed as part inside a CisuNotificationMsg
 * when a shutter event has happen.
 */
struct CisuShutterMessage {
    uint32_t frameNumber;   /**< id for the request */
    uint64_t timestamp;     /**< time when the exposure of the frame started */
};

/**
 * \struct CisuErrorMessage
 */
struct CisuErrorMessage {
    uint32_t frameNumber;
    DeviceErrorCodes errorCode;
    uint32_t streamId;
};

/**
 * \struct CisuNotificationMsg
 */
struct CisuNotificationMsg {
    CisuNotificationTypes type;   /**< error or message*/
    union {
        CisuShutterMessage shutter;
        CisuErrorMessage error;
    } message;
    void *clientPrivPtr; /**< pointer provided by the client with callbacks
                              during device init call */
};

/**
 * \class ClientCallbacks
 * Interface implemented by the clients of CISU to receive the notifications
 * produced by a given device.
 */
class ClientCallbacks {
public:
    ClientCallbacks() {}
    virtual ~ClientCallbacks() {}
    /**
     * Asynchronous notification of events in the CISU device. Only for
     * errors or events that require special timing.
     *
     * \param[in] msg Information about the event being notified.
     */
    virtual void notify(const CisuNotificationMsg &msg) = 0;
    /**
     * Capture request completion
     */
    virtual void processCaptureResult(std::shared_ptr<ICaptureResult>) = 0;
};

/**
 * \class CisuModule
 * This class represents all the cameras and IPU's in the system.
 */

class CISU_EXPORT_TO_DLL CisuModule {

public:
    static CisuError init();
    static void setGcssKeys(std::map<std::string, ia_uid> &gcssKeys);
    static CisuError deInit();
    static CisuError getCameraInfo(int32_t id);
    static std::shared_ptr<ICisuDevice> openCamera(int32_t id);

private:
    CisuModule(const CisuModule&);
    CisuModule& operator=(const CisuModule&);
};

template<class T>
class CISU_EXPORT_TO_DLL Deleter {
public:
    void operator()(T* p) const {
        p->destroy();
    }
};

/**
 * \class ICisuStream
 * This class represents a stream. Streams carry a fully qualified GCSS port
 * name which can be used to fetch the port properties like width, height,
 * format etc. from GCSS.
 */
class CISU_EXPORT_TO_DLL ICisuStream {
public:
    virtual const std::string getGCSSPortName() const = 0;
    virtual void destroy() = 0;
    virtual ICisuStream* clone() const = 0;
};

/**
 * \class StreamConfigResult
 * StreamConfigResult contains the results of the configure streams call. For
 * convenience the input streams are separated from output streams.
 */
struct StreamConfigResult {
    std::vector<ICisuStream*> outputStreams;
    std::vector<ICisuStream*> inputStreams;

    virtual ~StreamConfigResult() { cleanUp(); }

    virtual void cleanUp()
    {
        for (auto it : inputStreams)
            it->destroy();
        inputStreams.clear();

        for (auto it : outputStreams)
            it->destroy();
        outputStreams.clear();
    }

    StreamConfigResult& operator=(const StreamConfigResult& other)
    {
        // check for self-assignment
        if (&other == this)
            return *this;

        cleanUp();

        for (auto it : other.inputStreams) {
            inputStreams.push_back(it->clone());
        }

        for (auto it : other.outputStreams) {
            outputStreams.push_back(it->clone());
        }

        return *this;
    }
};

/**
 * \class CisuDevice
 * This class represents a imaging device that can produce multiple streams.
 * The control of this device is done by the 3A+ algorithms, internal to CISU.
 *
 * Objects of this class are only created by calling the CisuModule::openCamera.
 *
 */
class CISU_EXPORT_TO_DLL ICisuDevice {
public:
    virtual CisuError init(ClientCallbacks *cb, const CisuBlob &blob, void *clientPrivPtr = NULL) = 0;
    virtual CisuError deinit() = 0;
    virtual CisuError resetAiqb(const CisuBlob &aiqbBlob) = 0;
    virtual CisuError configureStreams(std::shared_ptr<const GCSS::IGraphConfig> graphConfig,
        StreamConfigResult &result) = 0;
    virtual CisuError createDefaultRequestSettings(CisuRequestTemplate aTemplate,
        std::shared_ptr<ICisuMetadata> &settings) = 0;
    virtual CisuError processCaptureRequest(std::shared_ptr<const ICaptureRequest> aRequest) = 0;
    virtual void destroy() = 0;
};

/**
 * \function template for shared pointer deleter which destroys an interface
 */
template<class T>
void DeleteInterface(T *interface) {
    interface->destroy(interface);
}

}
/**
 *  @}
 */

#endif
