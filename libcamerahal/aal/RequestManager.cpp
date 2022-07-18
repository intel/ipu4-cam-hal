/*
 * Copyright (C) 2017-2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "RequestManager"

#include <cstdlib>
#include <hardware/gralloc.h>

#include "videodev2.h"

#include "ICamera.h"
#include "Parameters.h"
#include "Utils.h"
#include "Errors.h"

#include "HALv3Utils.h"
#include "MetadataConvert.h"
#include "RequestManager.h"

namespace camera3 {

RequestManager::RequestManager(int cameraId) :
    mCameraId(cameraId),
    mCallbackOps(nullptr),
    mCameraDeviceStarted(false),
    mResultProcessor(nullptr),
    mRequestManagerState(IDLE),
    mRequestInProgress(0)
{
    LOG1("@%s", __func__);

    CLEAR(mCameraBufferInfo);
}

RequestManager::~RequestManager()
{
    LOG1("@%s", __func__);

    deteteStreams(false);

    delete mResultProcessor;
}

int RequestManager::init(const camera3_callback_ops_t *callback_ops)
{
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mLock);

    // Update the default settings from camera HAL
    icamera::Parameters parameter;
    int ret = icamera::camera_get_parameters(mCameraId, parameter);
    Check(ret != icamera::OK, ret, "failed to get parameters, ret %d", ret);

    android::CameraMetadata defaultRequestSettings;
    // Get static metadata
    MetadataConvert::HALCapabilityToStaticMetadata(parameter, &defaultRequestSettings);

    // Get defalut settings
    MetadataConvert::constructDefaultMetadata(&defaultRequestSettings);
    MetadataConvert::HALMetadataToRequestMetadata(parameter, &defaultRequestSettings);

    mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW] = defaultRequestSettings;
    MetadataConvert::updateDefaultRequestSettings(CAMERA3_TEMPLATE_PREVIEW,
            &mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW]);

    mResultProcessor = new ResultProcessor(mCameraId, callback_ops, this);
    mCallbackOps = callback_ops;

    mRequestManagerState = INIT;
    return icamera::OK;
}

int RequestManager::deinit()
{
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mLock);

    if (mCameraDeviceStarted) {
        int ret = icamera::camera_device_stop(mCameraId);
        Check(ret != icamera::OK, ret, "failed to stop camera device, ret %d", ret);
        mCameraDeviceStarted = false;
    }

    mRequestManagerState = IDLE;
    mRequestInProgress = 0;
    return icamera::OK;
}

int RequestManager::configureStreams(camera3_stream_configuration_t *stream_list)
{
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mLock);

    int ret = icamera::OK;

    // Validate if input list


    if (mCameraDeviceStarted) {
        ret = icamera::camera_device_stop(mCameraId);
        Check(ret != icamera::OK, ret, "failed to stop camera device, ret %d", ret);
        mCameraDeviceStarted = false;
    }
    uint32_t streamsNum = stream_list->num_streams;

    // Configure stream
    // Here we introduce a shadow stream concept:
    // When user configure a stream with BLOB format, it's usually for JPEG still capture
    // The JPEG image needs a small picture for creating thumbnail. Scaling from the big
    // picture takes more time than ISP does such thing. So we expand a new stream to fetch
    // the YUV image for thumbnail. We call it a shadow stream of the BLOB user stream.
    icamera::stream_config_t streamConfig;
    streamConfig.num_streams = streamsNum;
    streamConfig.streams = mHALStream;
    streamConfig.operation_mode = icamera::camera_stream_configuration_mode_t::CAMERA_STREAM_CONFIGURATION_MODE_NORMAL;
    for (uint32_t i = 0; i < streamsNum; i++) {
        ret = fillHWStreams(*stream_list->streams[i], mHALStream[i]);
        Check(ret != icamera::OK, ret, "failed to fill HW streas ret %d", ret);
        if (stream_list->streams[i]->format == HAL_PIXEL_FORMAT_BLOB) {
            fillShadowStream(mHALStream[i], mHALStream[streamsNum]);
            streamConfig.num_streams++;
        }
    }

    ret = icamera::camera_device_config_streams(mCameraId, &streamConfig);
    Check(ret != icamera::OK, ret, "failed to configure stream, ret %d", ret);

    // Mark all streams as NOT active
    for (auto &stream : mCamera3StreamVector) {
        stream->setActive(false);
    }

    // Create Stream for new streams
    for (uint32_t i = 0; i < streamsNum; i++) {
        camera3_stream_t  *stream    = stream_list->streams[i];
        icamera::stream_t *shdStream = stream->format == HAL_PIXEL_FORMAT_BLOB ?
                                       &mHALStream[streamsNum] : nullptr;
        if (stream->priv == nullptr) {
            Camera3Stream *s = new Camera3Stream(mCameraId, mResultProcessor, mHALStream[i].max_buffers,
                    mHALStream[i], *stream, shdStream);
            s->setActive(true);
            stream->priv = s;
            stream->max_buffers = mHALStream[i].max_buffers;
            stream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_NEVER;
            mCamera3StreamVector.push_back(s);
        } else {
            static_cast<Camera3Stream*> (stream->priv)->setActive(true);
        }
        LOGI("OUTPUT max buffer %d, usage %x, format %x", stream->max_buffers, stream->usage, stream->format);
    }

    // Remove useless Camera3Stream
    deteteStreams(true);
    mRequestManagerState = CONFIGURE_STREAMS;
    return icamera::OK;
}

int RequestManager::constructDefaultRequestSettings(int type, const camera_metadata_t **meta)
{
    LOG1("@%s, type %d", __func__, type);

    std::lock_guard<std::mutex> l(mLock);

    if (mDefaultRequestSettings.count(type) == 0) {
        mDefaultRequestSettings[type] = mDefaultRequestSettings[CAMERA3_TEMPLATE_PREVIEW];
        MetadataConvert::updateDefaultRequestSettings(type, &mDefaultRequestSettings[type]);
    }
    const camera_metadata_t *setting = mDefaultRequestSettings[type].getAndLock();
    *meta = setting;
    mDefaultRequestSettings[type].unlock(setting);

    return icamera::OK;
}

int RequestManager::processCaptureRequest(camera3_capture_request_t *request)
{
    LOG1("@%s", __func__);

    // Valid buffer and request
    Check(request->num_output_buffers >= kMaxStreamNum, icamera::BAD_VALUE, "Error buffer num %d",
          request->num_output_buffers);

    int ret = icamera::OK;

    waitProcessRequest();

    std::lock_guard<std::mutex> l(mLock);

    icamera::Parameters param;
    param.setMakernoteMode(icamera::MAKERNOTE_MODE_OFF);
    if (request->settings) {
        MetadataConvert::dumpMetadata(request->settings);

        mLastSettings = request->settings;
    } else if (mLastSettings.isEmpty()) {
        LOGE("nullptr settings for the first reqeust!");
        return icamera::BAD_VALUE;
    }

    int index = getAvailableCameraBufferInfoIndex();
    Check(index < 0, icamera::UNKNOWN_ERROR, "no empty CameraBufferInfo!");

    CLEAR(mCameraBufferInfo[index]);
    icamera::camera_buffer_t *buffer[kMaxStreamNum] = {nullptr};
    int numBuffers = request->num_output_buffers;
    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        camera3_stream_t *aStream = request->output_buffers[i].stream; // app stream
        Camera3Stream    *lStream = static_cast<Camera3Stream*> (aStream->priv); // local stream
        icamera::camera_buffer_t *shdBuffer = nullptr;
        if (aStream->format == HAL_PIXEL_FORMAT_BLOB) {
            shdBuffer = &mCameraBufferInfo[index].halBuffer[request->num_output_buffers];
            buffer[request->num_output_buffers] = shdBuffer;
            numBuffers++;
            param.setMakernoteMode(icamera::MAKERNOTE_MODE_JPEG);
        }
        lStream->processRequest(request->output_buffers[i], &mCameraBufferInfo[index].halBuffer[i], shdBuffer);
        buffer[i] = &mCameraBufferInfo[index].halBuffer[i];
    }

    // Convert metadata to Parameters
    MetadataConvert::requestMetadataToHALMetadata(mLastSettings, &param);

    ret = icamera::camera_stream_qbuf(mCameraId, buffer, numBuffers, &param);
    Check(ret != icamera::OK, ret, "failed to queue buffer to icamera, ret %d", ret);

    increaseRequestCount();

    if (!mCameraDeviceStarted) {
        ret = icamera::camera_device_start(mCameraId);
        Check(ret != icamera::OK, ret, "failed to start device, ret %d", ret);

        mCameraDeviceStarted = true;
    }

    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        Camera3Stream* s = static_cast<Camera3Stream*> (request->output_buffers[i].stream->priv);
        s->queueBufferDone(request->frame_number, request->output_buffers[i],
                           mCameraBufferInfo[index].halBuffer[i]);
    }

    mResultProcessor->registerRequest(request);

    mCameraBufferInfo[index].frameInProcessing = true;
    mCameraBufferInfo[index].frameNumber = request->frame_number;

    // Pending request if too much requests handled in HAL

    mRequestManagerState = PROCESS_CAPTURE_REQUEST;

    return ret;
}

void RequestManager::dump(int fd)
{
    LOG1("@%s", __func__);
}

int RequestManager::flush()
{
    LOG1("@%s", __func__);

    icamera::nsecs_t startTime = icamera::CameraUtils::systemTime();
    icamera::nsecs_t interval = 0;
    const icamera::nsecs_t ONE_SECOND = 1000000000;

    // wait 1000ms at most while there are requests in the HAL
    while (mRequestInProgress > 0 && interval <= ONE_SECOND) {
        usleep(10000); //wait 10ms
        interval = icamera::CameraUtils::systemTime() - startTime;
    }

    LOG2("@%s, line:%d, mRequestInProgress:%d, time spend:%ld us",
          __func__, __LINE__, mRequestInProgress, interval / 1000);

    // based on API, -ENODEV (NO_INIT) error should be returned.
    Check(interval > ONE_SECOND, icamera::NO_INIT,
          "flush() > 1s, timeout:%ld us", interval/1000);

    std::lock_guard<std::mutex> l(mLock);

    mRequestManagerState = FLUSH;
    return icamera::OK;
}

int RequestManager::HALFormatToV4l2Format(int halFormat)
{
    LOG1("@%s", __func__);

    int format = -1;
    switch (halFormat) {
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_BLOB:
            format = V4L2_PIX_FMT_NV12;
            break;
        default:
            LOGW("unsupport format %d", halFormat);
            break;
    }

    return format;
}

int RequestManager::fillHWStreams(const camera3_stream_t &camera3Stream,
                                  icamera::stream_t &stream)
{
    LOG1("@%s", __func__);

    stream.format = HALFormatToV4l2Format(camera3Stream.format);
    Check(stream.format == -1, icamera::BAD_VALUE, "unsupported format %x", camera3Stream.format);
    if (camera3Stream.rotation == CAMERA3_STREAM_ROTATION_90
        || camera3Stream.rotation == CAMERA3_STREAM_ROTATION_270) {
        stream.width = camera3Stream.height;
        stream.height = camera3Stream.width;
    } else {
        stream.width = camera3Stream.width;
        stream.height = camera3Stream.height;
    }
    stream.field = 0;
    stream.stride = icamera::CameraUtils::getStride(stream.format, stream.width);
    stream.size = icamera::CameraUtils::getFrameSize(stream.format, stream.width, stream.height);
    stream.memType = V4L2_MEMORY_USERPTR;
    stream.usage = (camera3Stream.format == HAL_PIXEL_FORMAT_BLOB ? icamera::CAMERA_STREAM_STILL_CAPTURE
                    : icamera::CAMERA_STREAM_PREVIEW);

    return icamera::OK;
}

int RequestManager::fillShadowStream(const icamera::stream_t &srcStream,
                                     icamera::stream_t &shdStream)
{
    // calculate the best thumbnail size for shadow stream
    uint32_t thumbW = ALIGN_32(srcStream.width / 16);
    while (4096 / thumbW > 12)
        thumbW += 32;
    while (thumbW % 128)
        thumbW += 32;

    uint32_t thumbH = (unsigned int)(thumbW * ((float)srcStream.height / (float)srcStream.width));

    // clone it first
    shdStream = srcStream;
    LOG1("@%s create shadow stream %dx%d", __func__, thumbW, thumbH);
    // fill the best thumbnail size to shadow stream
    shdStream.width  = thumbW;
    shdStream.height = thumbH;
    shdStream.stride = icamera::CameraUtils::getStride(shdStream.format, shdStream.width);
    shdStream.size   = icamera::CameraUtils::getFrameSize(shdStream.format,
            shdStream.width, shdStream.height);

    return icamera::OK;
}

void RequestManager::deteteStreams(bool inactiveOnly)
{
    LOG1("@%s", __func__);

    unsigned int i = 0;
    while (i < mCamera3StreamVector.size()) {
        Camera3Stream *s = mCamera3StreamVector.at(i);

        if (!inactiveOnly || !s->isActive()) {
            mCamera3StreamVector.erase(mCamera3StreamVector.begin() + i);
            delete s;
        } else {
            ++i;
        }
    }
}

int RequestManager::waitProcessRequest()
{
    LOG1("@%s", __func__);

    std::unique_lock<std::mutex> lock(mRequestLock);
    // check if it is ready to process next request
    while (mRequestInProgress >= mHALStream[0].max_buffers) {
        std::cv_status ret = mRequestCondition.wait_for(lock, std::chrono::nanoseconds(kMaxDuration));
        if (ret == std::cv_status::timeout) {
            LOGW("%s, wait to process request time out", __func__);
        }
    }

    return icamera::OK;
}

void RequestManager::increaseRequestCount()
{
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mRequestLock);
    ++mRequestInProgress;
}

void RequestManager::returnRequestDone(uint32_t frameNumber)
{
    LOG1("@%s", __func__);

    std::lock_guard<std::mutex> l(mRequestLock);

    mRequestInProgress--;

    // Update mCameraBufferInfo based on frameNumber
    for (int i = 0; i < kMaxProcessRequestNum; i++) {
        if (mCameraBufferInfo[i].frameNumber == frameNumber) {
            mCameraBufferInfo[i].frameInProcessing = false;
        }
    }

    mRequestCondition.notify_one();
}

int RequestManager::getAvailableCameraBufferInfoIndex()
{
    LOG1("@%s", __func__);

    std::unique_lock<std::mutex> lock(mRequestLock);

    for (int i = 0; i < kMaxProcessRequestNum; i++) {
        if (!mCameraBufferInfo[i].frameInProcessing) {
            return i;
        }
    }

    return -1;
}

} // namespace camera3
