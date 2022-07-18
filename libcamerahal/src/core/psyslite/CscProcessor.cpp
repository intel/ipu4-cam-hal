/*
 * Copyright (C) 2016~2018 Intel Corporation.
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

#define LOG_TAG "CscProcessor"

#include "CscProcessor.h"

#include "iutils/SwImageConverter.h"
#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "iutils/CameraDump.h"
#include "PlatformData.h"

namespace icamera {

bool CscProcessor::isFormatSupported(int /*inputFmt*/, int outputFmt)
{
    // Currently don't check input format as we'll convert it to YUV420 which is the only
    // format supported by CSC PG.
    // And CSC PG support RGB88, RGB565, ARGB as output format.
    switch (outputFmt) {
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB24:
        case V4L2_PIX_FMT_RGB32:
        case V4L2_PIX_FMT_BGR24:
        case V4L2_PIX_FMT_BGR32:
        case V4L2_PIX_FMT_XBGR32:
            return true;
    }
    return false;
}

CscProcessor::CscProcessor(int cameraId) : mCameraId(cameraId)
{
    mProcessThread = new ProcessThread(this);
    mPipeline[MAIN_PORT] = new CscPipeline();
    mPipeline[SECOND_PORT] = new CscPipeline();

    LOG1("@%s camera id:%d", __func__, mCameraId);
}

CscProcessor::~CscProcessor()
{
    LOG1("@%s ", __func__);

    mProcessThread->join();
    delete mProcessThread;
    delete mPipeline[MAIN_PORT];
    delete mPipeline[SECOND_PORT];
}

int CscProcessor::configure(const vector<ConfigMode>& configModes)
{
    PERF_CAMERA_ATRACE();
    LOG1("@%s ", __func__);

    int srcFmt = V4L2_PIX_FMT_YUV420;
    FrameInfoPortMap srcFrame;
    FrameInfoPortMap dstFrame;

    for (const auto inputFrameInfo : mInputFrameInfo) {
        Port port = inputFrameInfo.first;
        FrameInfo frameInfo;
        frameInfo.mWidth = inputFrameInfo.second.width;
        frameInfo.mHeight = inputFrameInfo.second.height;
        frameInfo.mFormat = srcFmt;
        frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
        frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
        srcFrame[MAIN_PORT] = frameInfo;

        //for rgb 24bit: FW output is BGR24
        //for rgb 32bit: FW output is BGR32
        const stream_t& outputStream = mOutputFrameInfo[port];
        int dstFmt = outputStream.format;
        if (outputStream.format == V4L2_PIX_FMT_BGR24) {
            dstFmt = V4L2_PIX_FMT_RGB24;
        } else if (outputStream.format == V4L2_PIX_FMT_BGR32 ||
                outputStream.format == V4L2_PIX_FMT_XBGR32) {
            dstFmt = V4L2_PIX_FMT_RGB32;
        }

        frameInfo.mWidth = outputStream.width;
        frameInfo.mHeight = outputStream.height;
        frameInfo.mFormat = dstFmt;
        frameInfo.mBpp = CameraUtils::getBpp(frameInfo.mFormat);
        frameInfo.mStride = CameraUtils::getStride(frameInfo.mFormat, frameInfo.mWidth);
        dstFrame[MAIN_PORT] = frameInfo;

        LOG1("port[%d] %s:(%dx%d) -> %s:(%dx%d)",
                port,
                CameraUtils::format2string(srcFrame[MAIN_PORT].mFormat),
                srcFrame[MAIN_PORT].mWidth, srcFrame[MAIN_PORT].mHeight,
                CameraUtils::format2string(dstFrame[MAIN_PORT].mFormat),
                dstFrame[MAIN_PORT].mWidth, dstFrame[MAIN_PORT].mHeight);

        Check(((port != MAIN_PORT) && (port != SECOND_PORT)), BAD_VALUE, "%s: invalid port number: %d.", __func__, port);

        mPipeline[port]->setInputInfo(srcFrame);
        mPipeline[port]->setOutputInfo(dstFrame);
    }

    if (mInputFrameInfo.size() == 1) {
        return mPipeline[MAIN_PORT]->prepare();
    }
    else {
        mPipeline[MAIN_PORT]->prepare();
        return mPipeline[SECOND_PORT]->prepare();
    }
}

int CscProcessor::start()
{
    PERF_CAMERA_ATRACE();
    LOG1("%s", __func__);

    AutoMutex   l(mBufferQueueLock);
    mThreadRunning = true;

    mProcessThread->run("CscProcessor", PRIORITY_NORMAL);

    mInBuffer.reset();
    int ret = allocProducerBuffers(mCameraId, MAX_BUFFER_COUNT);
    Check((ret < 0), ret, "%s: failed to allocate internal buffers.", __func__);

    return OK;
}

void CscProcessor::stop()
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

int CscProcessor::execute(shared_ptr<CameraBuffer> inBuf, Port inputPort, map<Port, shared_ptr<CameraBuffer>> &outBuf)
{
    shared_ptr<CameraBuffer> tmpInput = inBuf;
    // CscPipeline only supports YUV420 as input.
    if (inBuf->getFormat() != V4L2_PIX_FMT_YUV420) {
        if (!mInBuffer) {
            int format = V4L2_PIX_FMT_YUV420;
            int width = mInputFrameInfo[inputPort].width;
            int height = mInputFrameInfo[inputPort].height;
            uint32_t size = CameraUtils::getFrameSize(format, width, height);
            mInBuffer = CameraBuffer::create(mCameraId,
                         BUFFER_USAGE_GENERAL, V4L2_MEMORY_USERPTR, size, 0, format, width, height);
            Check(!mInBuffer, NO_MEMORY, "@%s: Allocate intermediate buffer failed", __func__);
        }

        int ret = SwImageConverter::convertFormat(inBuf->getWidth(), inBuf->getHeight(),
                (unsigned char *)inBuf->getBufferAddr(), inBuf->getBufferSize(), inBuf->getFormat(),
                (unsigned char *)mInBuffer->getBufferAddr(), mInBuffer->getBufferSize(), mInBuffer->getFormat());
        Check((ret < 0), ret, "format convertion failed with %d", ret);

        tmpInput = mInBuffer;
    }

    shared_ptr<CameraBuffer> out = outBuf[inputPort];
    Check(!out, -1, "%s: outBuf is nullptr", __func__);

    vector<shared_ptr<CameraBuffer>> srcBuf(1, tmpInput);
    vector<shared_ptr<CameraBuffer>> dstBuf(1, out);

    Check(((inputPort != MAIN_PORT) && (inputPort != SECOND_PORT)), BAD_VALUE, "%s: invalid port number: %d.", __func__, inputPort);

    return mPipeline[inputPort]->iterate(srcBuf, dstBuf);
}

// CscProcessor ThreadLoop
int CscProcessor::processNewFrame()
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

    for (auto& input: mInputQueue) {
        input.second.pop();
    }

    for (auto& output: mOutputQueue) {
        output.second.pop();
    }
    } // End of auto lock mBufferQueueLock scope

    for (auto& item : srcBuffers) {
        ret = execute(item.second, item.first, dstBuffers);
        Check((ret != OK), -1, "Execute pipe failed with:%d", ret);
    }

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

    for (auto& item : srcBuffers) {
        mBufferProducer->qbuf(item.first, item.second);
    }

    return OK;
}

} //namespace icamera
