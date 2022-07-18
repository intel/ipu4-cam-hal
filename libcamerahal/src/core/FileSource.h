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

#include "CaptureUnit.h"

#include "iutils/Thread.h"


namespace icamera {

/**
 * \class FileSource
 *
 * It's a buffer producer that's used to produce frame buffer via provided files
 * instead of from real sensor.
 *
 * There are two working mode:
 * 1. The simple mode which only provides one same frame for all sequences.
 *    How to enable: export cameraInjectFile="FrameFileName"
 * 2. The advanced mode which can configure which file is used for any sequence or FPS.
 *    How to enable: export cameraInjectFile="ConfigFileName.xml"
 *    The value of cameraInjectFile MUST be ended with ".xml".
 */
class FileSource : public CaptureUnit {
public:
    FileSource(int cameraId);
    ~FileSource();

    int init();
    void deinit();
    int configure(const map<Port, stream_t>& outputFrames, const vector<ConfigMode>& configModes);
    int start();
    int stop();

    virtual int qbuf(Port port, const shared_ptr<CameraBuffer> &camBuffer);

    virtual void addFrameAvailableListener(BufferConsumer *listener);
    virtual void removeFrameAvailableListener(BufferConsumer *listener);
    void removeAllFrameAvailableListener();

    // Overwrite EventSource APIs to avoid calling its parent's implementation.
    void registerListener(EventType eventType, EventListener* eventListener);
    void removeListener(EventType eventType, EventListener* eventListener);

private:
    bool produce();
    int  allocateSourceBuffer();
    void fillFrameBuffer(shared_ptr<CameraBuffer>& buffer);
    void fillFrameBuffer(string fileName, shared_ptr<CameraBuffer>& buffer);
    void notifyFrame(const shared_ptr<CameraBuffer>& buffer);
    void notifySofEvent();

private:
    class ProduceThread : public Thread {
    FileSource *mFileSrc;
    public:
        ProduceThread(FileSource *fileSource)  : mFileSrc(fileSource) { }

        virtual bool threadLoop() {
            return mFileSrc->produce();
        }
    };

    ProduceThread* mProduceThread;
    int mCameraId;
    bool mExitPending;

    int mFps;
    long mSequence;
    string mInjectedFile;      // The injected file can be a actual frame or a XML config file.
    bool mUsingConfigFile;     // If mInjectedFile ends with ".xml", it means we're using config file.

    stream_t mStreamConfig;
    Port mOutputPort;

    vector<BufferConsumer*> mBufferConsumerList;
    map<string, shared_ptr<CameraBuffer>> mFrameFileBuffers;
    CameraBufQ mBufferQueue;
    Condition mBufferSignal;
    //Guard for FileSource Public API
    Mutex mLock;
};

/**
 * \class FileSourceProfile
 *
 * It's used to parse file source config file, and provide such fps and frame file name etc
 * information for FileSource to use.
 */
class FileSourceProfile {
public:
    FileSourceProfile(string configFile);
    ~FileSourceProfile() {}

    int getFps(int cameraId);
    string getFrameFile(int cameraId, long sequence);
    int getFrameFiles(int cameraId, map<int, string> &framefiles);

private:
    DISALLOW_COPY_AND_ASSIGN(FileSourceProfile);

    static void startElement(void *userData, const char *name, const char **atts);
    static void endElement(void *userData, const char *name);

    void checkField(const char *name, const char **atts);
    void parseXmlFile(const string &xmlFile);
    void mergeCommonConfig();

private:
    struct CommonConfig {
        CommonConfig() : mFps(30), mFrameDir(".") {}
        int mFps;
        string mFrameDir;
    };

    struct FileSourceConfig {
        FileSourceConfig() : mFps(0) {}
        int mFps;
        string mFrameDir;
        map<int, string> mFrameFiles;
    };

    enum {
        FIELD_INVALID = 0,
        FIELD_SENSOR,
        FIELD_COMMON,
    } mCurrentDataField;

    string mCurrentSensor;
    CommonConfig mCommon;
    map<string, FileSourceConfig> mConfigs;
};

}
