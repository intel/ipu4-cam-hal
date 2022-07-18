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

#pragma once

#include "BufferQueue.h"
#include "CscPipeline.h"

namespace icamera {

/**
 * \class CscProcessor
 *
 * \brief CSC as known as Color Space Conversion.
 *        which is used to convert buffer from one color space to another one.
 */
class CscProcessor: public BufferQueue {

public:
    static bool isFormatSupported(int inputFmt, int outputFmt);

    CscProcessor(int cameraId);
    virtual ~CscProcessor();

    virtual int start();
    virtual void stop();
    virtual int configure(const vector<ConfigMode>& configModes);

private:
    DISALLOW_COPY_AND_ASSIGN(CscProcessor);

    int processNewFrame();
    int execute(shared_ptr<CameraBuffer> inBuf, Port inputPort, map<Port, shared_ptr<CameraBuffer>> &outBuf);

private:
    int mCameraId;
    PSysPipeBase* mPipeline[2];
    shared_ptr<CameraBuffer> mInBuffer;
}; // End of class CscProcessor

} //namespace icamera
