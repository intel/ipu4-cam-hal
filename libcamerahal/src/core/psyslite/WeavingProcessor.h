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
#include "WeavingPipeline.h"

namespace icamera {

class WeavingProcessor: public BufferQueue {

public:
    WeavingProcessor(int cameraId);
    virtual ~WeavingProcessor();

    virtual int start();
    virtual void stop();
    virtual int configure(const vector<ConfigMode>& configModes);

private:
    DISALLOW_COPY_AND_ASSIGN(WeavingProcessor);

    int processNewFrame();
    int execute(map<Port, shared_ptr<CameraBuffer>> &outBuffers);
    int prepareInputBuffersKeepFps(const shared_ptr<CameraBuffer> &curInBuffer);
    int prepareInputBuffersHalveFps(const shared_ptr<CameraBuffer> &curInBuffer);
    void qBackInBuffer(Port port);

private:
    int mCameraId;
    bool mNeedKeepFps; // If it's true, we need to reuse one of the input buffers for next iteration.
    shared_ptr<CameraBuffer> mPreviousBuffer;
    shared_ptr<CameraBuffer> mBufferTop;
    shared_ptr<CameraBuffer> mBufferBottom;
    WeavingPipeline* mPipeline;

}; // End of class WeavingProcessor

} //namespace icamera
