/*
 * Copyright (C) 2016-2018 Intel Corporation.
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

#define LOG_TAG "WeavingProcessor"

#include "WeavingProcessor.h"

#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "PlatformData.h"

namespace icamera {

WeavingProcessor::WeavingProcessor(int cameraId) : mCameraId(cameraId)
{
    mNeedKeepFps = PlatformData::needKeepFpsDuringDeinterlace(mCameraId);
    mProcessThread = new ProcessThread(this);
    mPipeline = new WeavingPipeline();

    LOG1("@%s camera id:%d keep FPS mode:%d", __func__, mCameraId, mNeedKeepFps);
}

WeavingProcessor::~WeavingProcessor()
{
    LOG1("@%s ", __func__);

    mProcessThread->join();
    delete mProcessThread;
    delete mPipeline;
}

int WeavingProcessor::configure(const vector<ConfigMode>& configModes)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s ", __func__);

    const stream_t& inputStream = mInputFrameInfo.begin()->second;
    FrameInfoPortMap mSrcFrame;
    FrameInfoPortMap mDstFrame;

    FrameInfo frameInfo;
    frameInfo.mWidth = inputStream.width;
    frameInfo.mHeight = inputStream.height;
    frameInfo.mFormat = inputStream.format;
    frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
    frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
    mSrcFrame[MAIN_PORT] = frameInfo;

    const stream_t& outputStream = mOutputFrameInfo.begin()->second;
    frameInfo.mWidth = outputStream.width;
    frameInfo.mHeight = outputStream.height;
    frameInfo.mFormat = outputStream.format;
    frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
    frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
    mDstFrame[MAIN_PORT] = frameInfo;

    mPipeline->setInputInfo(mSrcFrame);
    mPipeline->setOutputInfo(mDstFrame);

    return mPipeline->prepare();
}

int WeavingProcessor::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);

    AutoMutex   l(mBufferQueueLock);
    mThreadRunning = true;
    mPreviousBuffer.reset();

    mProcessThread->run("WeavingProcessor", PRIORITY_URGENT_AUDIO);

    int ret = allocProducerBuffers(mCameraId, MAX_BUFFER_COUNT);
    Check((ret < 0), ret, "%s: failed to allocate internal buffers.", __func__);

    return OK;
}

void WeavingProcessor::stop()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);

    mProcessThread->requestExit();
    {
        AutoMutex l(mBufferQueueLock);
        mThreadRunning = false;
        // Wakeup the thread to exit
        mFrameAvailableSignal.signal();
        mOutputAvailableSignal.signal();
    }

    mProcessThread->requestExitAndWait();

    // Thread is not running. It is safe to clear the Queue
    clearBufferQueues();
}

int WeavingProcessor::execute(map<Port, shared_ptr<CameraBuffer>> &outBuffers)
{
    LOG2("%s top/bottom (%ld/%ld)", __func__, mBufferTop->getSequence(), mBufferBottom->getSequence());

    shared_ptr<CameraBuffer> out = outBuffers.begin()->second;
    Check(!out, -1, "%s: outBuffers is nullptr", __func__);

    vector<shared_ptr<CameraBuffer>> srcBufs = {mBufferTop, mBufferBottom};
    vector<shared_ptr<CameraBuffer>> dstBuf = {out};
    if( PlatformData::getTopBottomReverse(mCameraId) == 1 ) {
        srcBufs = {mBufferBottom, mBufferTop};
    }

    return mPipeline->iterate(srcBufs, dstBuf);
}

int WeavingProcessor::prepareInputBuffersKeepFps(const shared_ptr<CameraBuffer> &curInBuffer)
{
    // If mPreviousBuffer is nullptr, that means this is the very first input buffer
    // so that we treat it as both top and bottom.
    if (!mPreviousBuffer) {
        mBufferTop = curInBuffer;
        mBufferBottom = curInBuffer;
        return OK;
    }

    long previousSequence = mPreviousBuffer->getSequence();
    long currentSequence = curInBuffer->getSequence();
    // Check if there is frame lost
    if (currentSequence - previousSequence == 1) {
        int field = mPreviousBuffer->getField();
        if (field == V4L2_FIELD_TOP) {
            mBufferTop = mPreviousBuffer;
            mBufferBottom = curInBuffer;
        } else {
            mBufferTop = curInBuffer;
            mBufferBottom = mPreviousBuffer;
        }
    } else {
        mBufferTop = curInBuffer;
        mBufferBottom = curInBuffer;
    }
    return OK;
}

int WeavingProcessor::prepareInputBuffersHalveFps(const shared_ptr<CameraBuffer> &curInBuffer)
{
    int field = curInBuffer->getField();
    if (field == V4L2_FIELD_TOP) {
        if (mBufferTop) {
            // This means two top buffers come in a row, we assign the latest one to bottom buffer
            // in order to continue to weave them, instead of waiting next real bottom buffer.
            mBufferBottom = curInBuffer;
        } else {
            mBufferTop = curInBuffer;
        }

        if (!mBufferBottom) {
            // We need to wait for bottom buffer to perform weaving.
            return WOULD_BLOCK;
        }
    } else if (field == V4L2_FIELD_BOTTOM) {
        if (mBufferBottom) {
            // Here comes the same case as it may happen in top buffer.
            mBufferTop = curInBuffer;
        } else {
            mBufferBottom = curInBuffer;
        }

        if (!mBufferTop) {
            // We need to wait for top buffer to perform weaving.
            return WOULD_BLOCK;
        }
    } else {
        LOGW("The buffer should be either top or bottom.");
    }
    return OK;
}

void WeavingProcessor::qBackInBuffer(Port port)
{
    Check(!mBufferProducer, VOID_VALUE, "Invalid producer");

    if (mNeedKeepFps) {
        if (mPreviousBuffer) {
            mBufferProducer->qbuf(port, mPreviousBuffer);
        }
        return;
    }

    if (mBufferTop) {
        mBufferProducer->qbuf(port, mBufferTop);
        mBufferTop.reset();
    }
    if (mBufferBottom) {
        mBufferProducer->qbuf(port, mBufferBottom);
        mBufferBottom.reset();
    }
}

// WeavingProcessor ThreadLoop
int WeavingProcessor::processNewFrame()
{
    PERF_CAMERA_ATRACE();
    LOG2("%s", __func__);

    int ret = OK;
    map<Port, shared_ptr<CameraBuffer> > srcBuffers, dstBuffers;
    shared_ptr<CameraBuffer> cInBuffer;
    Port inputPort = INVALID_PORT;

    { // Auto lock mBufferQueueLock scope
    ConditionLock lock(mBufferQueueLock);
    ret = waitFreeBuffersInQueue(lock, srcBuffers, dstBuffers);

    if (!mThreadRunning) return -1;

    Check((ret < 0), -1, "@%s: wake up from the wait abnomal such as stop", __func__);

    inputPort = srcBuffers.begin()->first;
    cInBuffer = srcBuffers[inputPort];
    Check(!cInBuffer, -1, "%s: srcBuffers is nullptr", __func__);
    LOG2("%s sequence:%ld field:%d", __func__, cInBuffer->getSequence(), cInBuffer->getField());

    for (auto& input: mInputQueue) {
        input.second.pop();
    }

    ret = mNeedKeepFps ? prepareInputBuffersKeepFps(cInBuffer) : prepareInputBuffersHalveFps(cInBuffer);
    if (ret == WOULD_BLOCK) {
        // We should wait for both two input buffers ready.
        // In this case, output buffer should not popup from the queue, since we won't handle them.
        return OK;
    }

    for (auto& output: mOutputQueue) {
        output.second.pop();
    }
    } // End of auto lock mBufferQueueLock scope

    ret = execute(dstBuffers);
    if (ret != OK) {
        LOGW("Execute weaving pipe failed with:%d", ret);
    }

    long topSequence = mBufferTop->getSequence();
    long bottomSequence = mBufferBottom->getSequence();
    shared_ptr<CameraBuffer> latestInput = (topSequence > bottomSequence) ? mBufferTop : mBufferBottom;

    for (auto& dst : dstBuffers) {
        Port port = dst.first;
        shared_ptr<CameraBuffer> cOutBuffer = dst.second;
        // If the output buffer is nullptr, that means user doesn't request that buffer,
        // so it doesn't need to be handled here.
        if (!cOutBuffer) continue;

        cOutBuffer->updateV4l2Buffer(latestInput->getV4l2Buffer());
        // Field value should be set to V4L2_FIELD_ANY after weaving.
        cOutBuffer->getV4l2Buffer().field = V4L2_FIELD_ANY;

        if (CameraDump::isDumpTypeEnable(DUMP_PSYS_OUTPUT_BUFFER)) {
            CameraDump::dumpImage(mCameraId, cOutBuffer, M_PSYS, port);
        }

        //Notify listener: No lock here: mBufferConsumerList will not updated in this state
        for (auto &it : mBufferConsumerList) {
            it->onFrameAvailable(port, cOutBuffer);
        }
    }

    {
        PERF_CAMERA_ATRACE_PARAM3("sof.sequence", cInBuffer->getSequence(), "csi2_port", cInBuffer->getCsi2Port(), \
                                  "virtual_channel", cInBuffer->getVirtualChannel());
    }

    qBackInBuffer(inputPort);
    mPreviousBuffer = cInBuffer;

    return OK;
}

} //namespace icamera
