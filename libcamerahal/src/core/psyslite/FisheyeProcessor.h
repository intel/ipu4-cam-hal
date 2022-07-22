/*
 * Copyright (C) 2017-2018 Intel Corporation.
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
#include "FisheyePipeline.h"

namespace icamera {

/**
 * \class FisheyeProcessor
 *
 */
class FisheyeProcessor: public BufferQueue {

public:
    static bool isFormatSupported(int outputFmt);
    FisheyeProcessor(int cameraId);
    virtual ~FisheyeProcessor();
    virtual int configure(const vector<ConfigMode>& configModes);
    virtual int setParameters(const Parameters& param);
    virtual int start();
    virtual void stop();

private:
    DISALLOW_COPY_AND_ASSIGN(FisheyeProcessor);

    int processNewFrame();
    int execute(shared_ptr<CameraBuffer> inBuf, map<Port, shared_ptr<CameraBuffer>> &outBuf);

private:
    int mCameraId;
    FisheyePipeline* mPipeline;
    shared_ptr<CameraBuffer> mInBuffer;
}; // End of class FisheyeProcessor

} //namespace icamera
