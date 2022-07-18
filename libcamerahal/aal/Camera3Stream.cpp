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

#define LOG_TAG "Camera3Stream"

#include "stdlib.h"

#include "Errors.h"
#include "Utils.h"
#include "ICamera.h"

#include "HALv3Utils.h"

#include "MetadataConvert.h"
#include "Camera3Stream.h"
#include "CameraDump.h"

namespace camera3 {

StreamBufferPool::StreamBufferPool() :
    mBlobs(nullptr),
    mNumBlob(0)
{
    LOG1("@%s", __func__);
}

StreamBufferPool::~StreamBufferPool()
{
    LOG1("@%s", __func__);
    destroy();
}

int StreamBufferPool::destroy()
{
    LOG1("@%s mNumBlob:%d", __func__, mNumBlob);
    std::lock_guard<std::mutex> l(mLock);

    while(mNumBlob) {
        if (mBlobs[mNumBlob-1].addr)
            free(mBlobs[mNumBlob-1].addr);
        mNumBlob--;
    }

    if (mBlobs) {
        free(mBlobs);
        mBlobs = nullptr;
    }
    return 0;
}

int StreamBufferPool::allocate(uint32_t blobSize, uint32_t numBlob)
{
    LOG1("@%s numBlob:%d blobSize:%d", __func__, numBlob, blobSize);
    std::lock_guard<std::mutex> l(mLock);

    mBlobs = static_cast<MemoryBlob *> (malloc(sizeof(struct MemoryBlob) * numBlob));
    Check(mBlobs == nullptr, -1, "failed to alloc %d MemoryBlob", numBlob);

    mNumBlob = numBlob;
    for (uint32_t i = 0; i < mNumBlob; i++) {
        int ret = posix_memalign(&mBlobs[i].addr, getpagesize(), blobSize);
        Check(ret != 0, -1, "%s, posix_memalign %d fails, ret:%d", __func__, blobSize, ret);
        mBlobs[i].busy = 0;
    }

    return 0;
}

MemoryBlob *StreamBufferPool::getBlob()
{
    std::lock_guard<std::mutex> l(mLock);
    for (uint32_t i = 0; i < mNumBlob; i++) {
        if (!mBlobs[i].busy) {
            mBlobs[i].busy = 1;
            LOG2("%s idx:%d addr:%p", __func__, i, mBlobs[i].addr);
            return &mBlobs[i];
        }
    }
    return nullptr;
}

void StreamBufferPool::returnBlob(void *memAddr)
{
    std::lock_guard<std::mutex> l(mLock);
    for (uint32_t i = 0; i < mNumBlob; i++) {
        if (mBlobs[i].addr == memAddr) {
            LOG2("%s idx:%d addr:%p", __func__, i, memAddr);
            mBlobs[i].busy = 0;
            return;
        }
    }

    LOGE("returned memory blob addr:%p not found", memAddr);
}

Camera3Stream::Camera3Stream(int cameraId, CallbackEventInterface *callback,
                             uint32_t maxNumReqInProc, const icamera::stream_t &halStream,
                             const camera3_stream_t &stream, const icamera::stream_t *shdStream) :
    mCameraId(cameraId),
    mEventCallback(callback),
    mPostProcessor(nullptr),
    mStreamState(false),
    mHALStream(halStream),
    mThbStream(shdStream),
    mMaxNumReqInProc(maxNumReqInProc),
    mStream(stream)
{
    LOG1("[%d]@%s, stream type %d, mMaxNumReqInProc %d", mHALStream.id, __func__,
            mStream.stream_type, mMaxNumReqInProc);

    int type = getPostProcessType(mStream);
    if (type != PROCESS_NONE) {
        mPostProcessor = new PostProcessor(mCameraId, stream, type);
    }
}

Camera3Stream::~Camera3Stream()
{
    LOG1("[%d]@%s", mHALStream.id, __func__);

    setActive(false);

    for (auto& buf : mBuffers) {
        buf.second->unlock();
        delete buf.second;
    }

    std::lock_guard<std::mutex> l(mLock);
    for (auto item : mCaptureResultVector) {
        delete item;
    }
    mCaptureResultVector.clear();

    delete mPostProcessor;
}

bool Camera3Stream::threadLoop()
{
    LOG1("[%d]@%s", mHALStream.id, __func__);

    std::unique_lock<std::mutex> lock(mLock);
    // check if there is buffer queued
    if (mCaptureResultVector.empty()) {
        std::cv_status ret = mBufferDoneCondition.wait_for(lock, std::chrono::nanoseconds(kMaxDuration));
        if (ret == std::cv_status::timeout) {
            LOGW("[%d]%s, wait request time out", mHALStream.id, __func__);
        }

        return true;
    }

    // dequeue buffer from HAL
    icamera::camera_buffer_t *buffer = nullptr, *thbBuffer = nullptr; // for thumbnail
    icamera::Parameters parameter;

    int ret = icamera::camera_stream_dqbuf(mCameraId, mHALStream.id, &buffer, &parameter);
    Check(ret != icamera::OK, true, "[%d]failed to dequeue buffer, ret %d", mHALStream.id, ret);

    // dequeue thumbnail stream buffer if exist
    if (mThbStream) {
        ret = icamera::camera_stream_dqbuf(mCameraId, mThbStream->id, &thbBuffer, nullptr);
        Check(ret != icamera::OK, true, "failed to dequeue thbBuffer, ret %d", ret);
    }

    CaptureResult *result = mCaptureResultVector.at(0);
    Check(result->halBuffer.addr != buffer->addr, true, "[%d]buffer mismatching, please check!",
          mHALStream.id);

    buffer_handle_t handle = result->handle;
    Camera3Buffer* ccBuf = mBuffers[handle];

    // handle postprocess
    if (mHALStream.usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
        icamera::camera_buffer_t jpegBuffer;
        CLEAR(jpegBuffer);
        jpegBuffer.addr = ccBuf->data();
        jpegBuffer.s.size = ccBuf->size();
        if (mPostProcessor) {
            LOG2("[%d]%s Post processing buffer.", mHALStream.id, __func__);
            icamera::status_t status = mPostProcessor->encodeJpegFrame(*buffer, *thbBuffer,
                                           parameter, jpegBuffer);
            Check(status != icamera::OK, true, "@%s, jpeg process failed.", __func__);
            ccBuf->dumpImage(result->frameNumber, icamera::DUMP_JPEG_BUFFER, V4L2_PIX_FMT_JPEG);
        }

        // return MemoryBlob after post processing
        LOG2("return buffer:%p add:%p after post processing", buffer, buffer->addr);
        mStillBufPool.returnBlob(buffer->addr);
        if (mThbStream) {
            LOG2("return shdBuffer:%p add:%p", thbBuffer, thbBuffer->addr);
            mThumbBufPool.returnBlob(thbBuffer->addr);
        }
    }

    ccBuf->unlock();
    ccBuf->deinit();
    ccBuf->getFence(&result->outputBuffer);

    // notify shutter done
    ShutterEvent shutterEvent;
    shutterEvent.frameNumber = result->frameNumber;
    shutterEvent.timestamp = buffer->timestamp;

    mEventCallback->shutterDone(shutterEvent);

    // notify frame done
    BufferEvent bufferEvent;
    bufferEvent.frameNumber = result->frameNumber;
    bufferEvent.timestamp = (int64_t)buffer->timestamp;
    bufferEvent.parameter = &parameter;
    bufferEvent.outputBuffer = &result->outputBuffer;

    mEventCallback->bufferDone(bufferEvent);

    delete result;
    mCaptureResultVector.erase(mCaptureResultVector.begin());

    return true;
}

void Camera3Stream::requestExit()
{
    LOG1("[%d]@%s", mHALStream.id, __func__);

    icamera::Thread::requestExit();
    std::lock_guard<std::mutex> l(mLock);
    mBufferDoneCondition.notify_one();
}

int Camera3Stream::processRequest(const camera3_stream_buffer_t &outputBuffer,
        icamera::camera_buffer_t *buffer, icamera::camera_buffer_t *thbBuffer)
{
    LOG1("[%d]@%s", mHALStream.id, __func__);

    // convert camera3_stream_buffer_t to icamera::camera_buffer_t
    buffer->s = mHALStream;

    Camera3Buffer* ccBuf = nullptr;
    buffer_handle_t handle = *outputBuffer.buffer;
    if (mBuffers.find(handle) == mBuffers.end()) {
        ccBuf = new Camera3Buffer;
        mBuffers[handle] = ccBuf;
    }

    // wait and lock buffer
    ccBuf = mBuffers[handle];
    ccBuf->init(&outputBuffer, mCameraId);
    ccBuf->waitOnAcquireFence();
    ccBuf->lock();

    if (mHALStream.usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
        MemoryBlob *blob = mStillBufPool.getBlob();
        Check(blob == nullptr, -1, "no available MemoryBlob");
        buffer->addr = blob->addr;
        if (mThbStream) {
            Check(thbBuffer == nullptr, -1, "thbBuffer is nullptr");
            blob = mThumbBufPool.getBlob();
            Check(blob == nullptr, -1, "no available MemoryBlob for thumb");
            thbBuffer->addr  = blob->addr;
            thbBuffer->flags = icamera::camera_buffer_flags_t::BUFFER_FLAG_SW_WRITE;
            thbBuffer->s     = *mThbStream;
            LOG2("[%d]@%s buffer:%p thbBuffer:%p shdStream id:%d req buf:%p buf addr:%p",
                    mHALStream.id, __func__, buffer, thbBuffer, mThbStream->id,
                    &outputBuffer, outputBuffer.buffer);
        }
    } else {
        buffer->addr = ccBuf->data();
        buffer->s.size = ccBuf->size();
        LOG2("[%d]@%s buffer:%p addr:%p",mHALStream.id, __func__, buffer, buffer->addr);
    }

    buffer->flags = icamera::camera_buffer_flags_t::BUFFER_FLAG_SW_WRITE;

    return icamera::OK;
}

void Camera3Stream::queueBufferDone(uint32_t frameNumber,
                                    const camera3_stream_buffer_t &outputBuffer,
                                    const icamera::camera_buffer_t &halBuffer)
{
    LOG1("[%d]@%s", mHALStream.id, __func__);

    std::lock_guard<std::mutex> l(mLock);

    CaptureResult *result = new CaptureResult;

    result->frameNumber = frameNumber;
    result->outputBuffer = outputBuffer;
    result->handle = *outputBuffer.buffer;
    result->outputBuffer.buffer = &result->handle;
    result->halBuffer = halBuffer;

    mCaptureResultVector.push_back(result);

    mBufferDoneCondition.notify_one();
}

void Camera3Stream::setActive(bool state)
{
    LOG1("[%d]@%s state %d", mHALStream.id, __func__, state);

    if (!mStreamState && state) {
        std::string threadName = "Cam3Stream-";
        threadName += std::to_string(mHALStream.id);

        // Run Camera3Stream thread
        run(threadName);

        // Allocate local buffer pool for still capture stream
        if (mHALStream.usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
            mStillBufPool.allocate(mHALStream.size, mMaxNumReqInProc);
            if (mThbStream)
                mThumbBufPool.allocate(mThbStream->size, mMaxNumReqInProc);
        }
    } else if (mStreamState && !state) {
        if (mHALStream.usage == icamera::CAMERA_STREAM_STILL_CAPTURE) {
            mStillBufPool.destroy();
            if (mThbStream)
                mThumbBufPool.destroy();
        }
        // Exit Camera3Stream thread
        requestExit();
    }

    mStreamState = state;
}

int Camera3Stream::getPostProcessType(const camera3_stream_t &stream)
{
    int type = PROCESS_NONE;
    if (mStream.format == HAL_PIXEL_FORMAT_BLOB) {
        type |= PROCESS_JPEG_ENCODING;
    }
    if (mStream.stream_type == camera3_stream_type::CAMERA3_STREAM_OUTPUT
        && mStream.rotation != CAMERA3_STREAM_ROTATION_0
        && mStream.rotation != CAMERA3_STREAM_ROTATION_180) {
        type |= PROCESS_ROTATE;
    }

    return type;
}
} // namespace camera3
