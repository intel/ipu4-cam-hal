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

#define LOG_TAG "ResultProcessor"

#include <mutex>

#include "Errors.h"
#include "Utils.h"

#include "HALv3Utils.h"
#include "MetadataConvert.h"
#include "ResultProcessor.h"

namespace camera3 {

#define META_ENTRY_COUNT 256
#define META_DATA_COUNT  80000

MetadataMemory::MetadataMemory() :
    mMeta(META_ENTRY_COUNT, META_DATA_COUNT),
    mMemory(nullptr)
{
}

MetadataMemory::~MetadataMemory()
{
    // Return memory to metadata
    getMetadata();
}

android::CameraMetadata* MetadataMemory::getMetadata()
{
    if (mMemory) {
        mMeta.acquire(mMemory);
        mMemory = nullptr;
    }
    return &mMeta;
}

camera_metadata_t* MetadataMemory::getMemory()
{
    if (!mMemory) {
        mMemory = mMeta.release();
    }
    return mMemory;
}

void MetadataMemory::copyMetadata(const camera_metadata_t* src)
{
    getMemory();
    // Clear old metadata
    mMemory = place_camera_metadata(mMemory,
                                    get_camera_metadata_size(mMemory),
                                    get_camera_metadata_entry_capacity(mMemory),
                                    get_camera_metadata_data_capacity(mMemory));
    getMetadata();
    mMeta.append(src);
}

ResultProcessor::ResultProcessor(int cameraId, const camera3_callback_ops_t *callback,
                                 RequestManagerCallback *requestManagerCallback) :
    mCameraId(cameraId),
    mCallbackOps(callback),
    mLastSettings(nullptr),
    mRequestManagerCallback(requestManagerCallback)
{
    UNUSED(mCameraId);
    LOG1("@%s, mCameraId %d", __func__, mCameraId);

    mLastSettings = acquireMetadataMemory();

    mCamera3AMetadata = new Camera3AMetadata(mCameraId);
}

ResultProcessor::~ResultProcessor()
{
    LOG1("@%s", __func__);

    for (auto &item : mRequestStateVector) {
        releaseMetadataMemory(item.metaResult);
    }
    mRequestStateVector.clear();

    releaseMetadataMemory(mLastSettings);
    while (mMetadataVector.size() > 0) {
        LOG1("%s: release meta %p", __func__, mMetadataVector.back());
        delete mMetadataVector.back();
        mMetadataVector.pop_back();
    }

    delete mCamera3AMetadata;
}

int ResultProcessor::registerRequest(const camera3_capture_request_t *request)
{
    LOG1("@%s frame_number:%d", __func__, request->frame_number);

    RequestState req;

    req.frameNumber = request->frame_number;
    req.buffersToReturn = request->num_output_buffers;
    req.partialResultCount = 1;

    std::lock_guard<std::mutex> l(mLock);
    // Copy settings
    if (request->settings) {
        mLastSettings->copyMetadata(request->settings);
    }

    req.metaResult = acquireMetadataMemory();
    req.metaResult->copyMetadata(mLastSettings->getMemory());

    mRequestStateVector.push_back(req);
    return icamera::OK;
}

int ResultProcessor::shutterDone(const ShutterEvent &event)
{
    std::lock_guard<std::mutex> l(mLock);
    for (uint32_t i = 0; i < mRequestStateVector.size(); i++) {
        if (mRequestStateVector.at(i).frameNumber == event.frameNumber) {
            if (!mRequestStateVector.at(i).isShutterDone) {
                camera3_notify_msg_t notifyMsg;
                CLEAR(notifyMsg);

                notifyMsg.type = CAMERA3_MSG_SHUTTER;
                notifyMsg.message.shutter.frame_number = event.frameNumber;
                notifyMsg.message.shutter.timestamp = event.timestamp;

                mCallbackOps->notify(mCallbackOps, &notifyMsg);
                mRequestStateVector.at(i).isShutterDone = true;
                LOG2("%s, frame_number %d", __func__, event.frameNumber);
                if (checkRequestDone(mRequestStateVector.at(i))) {
                    returnRequestDone(event.frameNumber);
                    releaseMetadataMemory(mRequestStateVector.at(i).metaResult);
                    mRequestStateVector.erase(mRequestStateVector.begin() + i);
                }
            }
            return icamera::OK;
        }
    }

    LOGW("%s, event.frameNumber %u wasn't found!", __func__, event.frameNumber);
    return icamera::OK;
}

int ResultProcessor::bufferDone(const BufferEvent &event)
{
    LOG1("@%s frame_number:%d", __func__, event.frameNumber);
    camera3_capture_result_t result;
    CLEAR(result);

    MetadataMemory* metaMem = nullptr;
    {
        std::lock_guard<std::mutex> l(mLock);
        for (auto &reqStat : mRequestStateVector) {
            if (reqStat.frameNumber == event.frameNumber
                && reqStat.partialResultReturned < reqStat.partialResultCount) {
                reqStat.partialResultReturned = 1;
                metaMem = reqStat.metaResult;
            }
        }
    }

    result.frame_number = event.frameNumber;
    result.output_buffers = event.outputBuffer;
    result.num_output_buffers = 1;
    if (metaMem) {
        android::CameraMetadata* metaResult = metaMem->getMetadata();
        MetadataConvert::HALMetadataToRequestMetadata(*(event.parameter), metaResult);
        metaResult->update(ANDROID_SENSOR_TIMESTAMP, &event.timestamp, 1);

        mCamera3AMetadata->process3Astate(*(event.parameter), *metaResult);

        result.result = metaMem->getMemory();
        result.partial_result = 1;
    } else {
        result.result = nullptr;
        result.partial_result = 0;
    }
    mCallbackOps->process_capture_result(mCallbackOps, &result);

    bool found = false;
    std::lock_guard<std::mutex> l(mLock);
    for (uint32_t i = 0; i < mRequestStateVector.size(); i++) {
        if (mRequestStateVector.at(i).frameNumber == event.frameNumber) {
            mRequestStateVector.at(i).buffersReturned++;
            mRequestStateVector.at(i).partialResultReturned = 1;
            if (checkRequestDone(mRequestStateVector.at(i))) {
                returnRequestDone(event.frameNumber);
                releaseMetadataMemory(mRequestStateVector.at(i).metaResult);
                mRequestStateVector.erase(mRequestStateVector.begin() + i);
            }
            found = true;
        }
    }
    if (!found) {
        LOGW("%s, event.frameNumber %u wasn't found!", __func__, event.frameNumber);
    } else {
        LOG2("%s, event.frameNumber %u was returned", __func__, event.frameNumber);
    }

    return icamera::OK;
}

bool ResultProcessor::checkRequestDone(const RequestState &requestState)
{
    LOG1("@%s", __func__);

    return (requestState.isShutterDone
        && requestState.partialResultCount == requestState.partialResultReturned
        && requestState.buffersToReturn == requestState.buffersReturned);
}

void ResultProcessor::returnRequestDone(uint32_t frameNumber)
{
    LOG1("@%s frame_number:%d", __func__, frameNumber);

    mRequestManagerCallback->returnRequestDone(frameNumber);
}

MetadataMemory* ResultProcessor::acquireMetadataMemory()
{
    MetadataMemory* metaMem = nullptr;
    if (mMetadataVector.size() > 0) {
        metaMem = mMetadataVector.back();
        mMetadataVector.pop_back();
    } else {
        metaMem = new MetadataMemory();
        LOG1("%s: allocate new one: %p", __func__, metaMem);
    }

    return metaMem;
}

void ResultProcessor::releaseMetadataMemory(MetadataMemory* metaMem)
{
    Check(metaMem == nullptr, VOID_VALUE, "%s: null metaMem!", __func__);
    mMetadataVector.push_back(metaMem);
}

} // namespace camera3
