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

#define LOG_TAG "ScaleProcessor"

#include "ScaleProcessor.h"

#include "iutils/SwImageConverter.h"
#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "PlatformData.h"

namespace icamera {

bool ScaleProcessor::isFormatSupported(int /*inputFmt*/, int outputFmt)
{
    // Currently don't check input format as we'll convert it to YUYV/UYVY
    // which is the only formats supported by Scale PG.
    // And Scale PG support YUV420, NV12, NV21 as output format.
    switch (outputFmt) {
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
            return true;
    }
    return false;
}

bool ScaleProcessor::isScalePGNeeded(int inputFmt, camera_resolution_t srcRes, stream_config_t *streamList)
{
    // the case which need use scale PG:
    // 1. inputFmt is same with scale pg supported output format
    // 2. src resolution is not same with stream config size
    for (int i=0; i< streamList->num_streams; i++) {
        if ((!isFormatSupported(0, inputFmt)) ||
            (srcRes.width != streamList->streams[i].width) ||
            (srcRes.height != streamList->streams[i].height)) {
            return true;
        }
    }
    return false;
}

ScaleProcessor::ScaleProcessor(int cameraId) : mCameraId(cameraId)
{
    mProcessThread = new ProcessThread(this);
    mPipeline = new ScalePipeline();

    LOG1("@%s camera id:%d", __func__, mCameraId);
}

ScaleProcessor::~ScaleProcessor()
{
    LOG1("@%s ", __func__);

    mProcessThread->join();
    delete mProcessThread;
    delete mPipeline;
}

int ScaleProcessor::configure(const vector<ConfigMode>& configModes)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s ", __func__);
    int srcFmt = V4L2_PIX_FMT_YUYV;
    const stream_t& inputStream = mInputFrameInfo.begin()->second;
    if (inputStream.format == V4L2_PIX_FMT_YUYV ||
        inputStream.format == V4L2_PIX_FMT_UYVY) {
        srcFmt = inputStream.format;
    }

    FrameInfoPortMap mSrcFrame;
    FrameInfoPortMap mDstFrame;

    FrameInfo frameInfo;
    frameInfo.mWidth = inputStream.width;
    frameInfo.mHeight = inputStream.height;
    frameInfo.mFormat = srcFmt;
    frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
    frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
    mSrcFrame[MAIN_PORT] = frameInfo;

    int dstFmt = V4L2_PIX_FMT_YUV420;

    for (const auto& item : mOutputFrameInfo) {
        Port port = item.first;
        if (isFormatSupported(0, item.second.format)) {
            dstFmt = item.second.format;
        }

        frameInfo.mWidth = item.second.width;
        frameInfo.mHeight = item.second.height;
        frameInfo.mFormat = dstFmt;
        frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
        frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
        mDstFrame[port] = frameInfo;
    }

    mPipeline->setInputInfo(mSrcFrame);
    mPipeline->setOutputInfo(mDstFrame);

    return mPipeline->prepare();
}

int ScaleProcessor::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);

    AutoMutex   l(mBufferQueueLock);
    mThreadRunning = true;

    mProcessThread->run("ScaleProcessor", PRIORITY_NORMAL);

    mInBuffer.reset();
    int ret = allocProducerBuffers(mCameraId, MAX_BUFFER_COUNT);
    Check((ret < 0), ret, "%s: failed to allocate internal buffers.", __func__);

    return OK;
}

void ScaleProcessor::stop()
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

int ScaleProcessor::execute(shared_ptr<CameraBuffer> inBuf, map<Port, shared_ptr<CameraBuffer>> &outBuf)
{
    Check((outBuf.empty() || outBuf.size() > 3), UNKNOWN_ERROR,
        "@%s, wrong, outBuf size:%zu",__func__, outBuf.size());

    shared_ptr<CameraBuffer> tmpInput = inBuf;
    // ScalePipeline only supports YUYV/UYVY as input.
    if (inBuf->getFormat() != V4L2_PIX_FMT_YUYV && inBuf->getFormat() != V4L2_PIX_FMT_UYVY) {
        if (!mInBuffer) {
            int format = V4L2_PIX_FMT_YUYV;
            int width = mInputFrameInfo.begin()->second.width;
            int height = mInputFrameInfo.begin()->second.height;
            uint32_t size = CameraUtils::getFrameSize(format, width, height);
            mInBuffer = CameraBuffer::create(mCameraId,
                         BUFFER_USAGE_GENERAL, V4L2_MEMORY_USERPTR, size, 0, format, width, height);
            Check(!mInBuffer, NO_MEMORY, "@%s: Allocate intermediate buffer failed", __func__);
        }

        int ret = SwImageConverter::convertFormat(inBuf->getWidth(), inBuf->getHeight(),
                (unsigned char *)inBuf->getBufferAddr(), inBuf->getBufferSize(), inBuf->getFormat(),
                (unsigned char *)mInBuffer->getBufferAddr(), mInBuffer->getBufferSize(), mInBuffer->getFormat());

        LOG1("convertFormat %s:(%dx%d) -> %s:(%dx%d)",
                 CameraUtils::format2string(inBuf->getFormat()), inBuf->getWidth(), inBuf->getHeight(),
                 CameraUtils::format2string(mInBuffer->getFormat()), mInBuffer->getWidth(), mInBuffer->getHeight());

        Check((ret < 0), ret, "format convertion failed with %d", ret);

        tmpInput = mInBuffer;
    }

    vector<shared_ptr<CameraBuffer>> srcBuf(1, tmpInput);
    vector<shared_ptr<CameraBuffer>> dstBuf;

    for (const auto& buf : outBuf) {
        shared_ptr<CameraBuffer> out = buf.second;
        Check(!out, UNKNOWN_ERROR, "outBuf is nullptr");
        dstBuf.push_back(out);
    }

    return mPipeline->iterate(srcBuf, dstBuf);
}

// ScaleProcessor ThreadLoop
int ScaleProcessor::processNewFrame()
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
    Check(!cInBuffer, -1, "srcBuffers is nullptr");

    for (auto& input: mInputQueue) {
        input.second.pop();
    }

    for (auto& output: mOutputQueue) {
        output.second.pop();
    }
    } // End of auto lock mBufferQueueLock scope

    ret = execute(cInBuffer, dstBuffers);
    Check((ret != OK), -1, "Execute pipe failed with:%d", ret);

    for (auto& dst : dstBuffers) {
        Port port = dst.first;
        shared_ptr<CameraBuffer> cOutBuffer = dst.second;
        // If the output buffer is nullptr, that means user doesn't request that buffer,
        // so it doesn't need to be handled here.
        if (!cOutBuffer) continue;

        cOutBuffer->updateV4l2Buffer(cInBuffer->getV4l2Buffer());

        if (CameraDump::isDumpTypeEnable(DUMP_PSYS_OUTPUT_BUFFER)) {
            CameraDump::dumpImage(mCameraId, cOutBuffer, M_PSYS, port);
        }

        for (auto &it : mBufferConsumerList) {
            it->onFrameAvailable(port, cOutBuffer);
        }
    }

    {
        PERF_CAMERA_ATRACE_PARAM3("sof.sequence", cInBuffer->getSequence(),
                                  "csi2_port", cInBuffer->getCsi2Port(),
                                  "virtual_channel", cInBuffer->getVirtualChannel());
    }

    mBufferProducer->qbuf(inputPort, cInBuffer);

    return OK;
}

int ScaleProcessor::setParameters(const Parameters& param)
{
    return mPipeline->setParameters(param);
}

} //namespace icamera
