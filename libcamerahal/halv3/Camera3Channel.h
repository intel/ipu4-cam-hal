/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2017 Intel Corporation.
 *
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

#ifndef __CAMERA3_CHANNEL_H__
#define __CAMERA3_CHANNEL_H__

// System dependencies
#include <utils/List.h>
#include <utils/Mutex.h>
#include <utils/Vector.h>
#include <queue>
#include <utils/threads.h>
#include <utils/RefBase.h>

#include "camera3.h"
#include <ICamera.h>
#include "StreamBuffer.h"

using namespace std;

#define MIN_STREAMING_BUFFER_NUM 7+11

namespace android {
namespace camera2 {

typedef void (*channel_cb_routine)(icamera::Parameters *metadata,
                                const camera3_stream_buffer_t *buffer,
                                uint32_t frame_number, uint64_t timestamp, void *userdata);
class Camera3Channel
{
public:
    Camera3Channel(int device_id,
                   icamera::stream_t *stream,
                   channel_cb_routine cb_routine,
                   void *userData);
    virtual ~Camera3Channel();

    virtual int32_t start();
    virtual void stop();
    virtual void    flush();
    virtual int32_t queueBuf(
        const camera3_stream_buffer_t *stream_buf,
        int stream_id,
        int frame_id);
    class DQThread: public Thread {
        Camera3Channel *mChannel;
        public:
            DQThread(Camera3Channel *c)
                :mChannel(c) { }
            virtual bool threadLoop() {
                return mChannel->processNewStream();
            }
    };
    icamera::stream_t* getStream() {return mStream;}
    int getStreamid() {return mStreamId;}

private:
    bool processNewStream();
    int     mDeviceId;
    int     mStreamId;
    void *mUserData;
    icamera::stream_t   *mStream;

    channel_cb_routine mChannelCB;

    //for the thread loop
    queue <sp <StreamBuffer>> mPendingStreams;
    vector<sp<StreamBuffer>> mStreamBufferPool;
    sp <DQThread> mDQThread;
    bool mThreadRunning; //state of DQThread. true after start and false after stop
};

} // namespace camera2
} // namespace android

#endif /* __CAMERA_CHANNEL_H__ */
