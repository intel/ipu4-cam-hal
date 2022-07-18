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

#define LOG_TAG "FileSource"

#include <fstream>
#include <expat.h>

#include "PlatformData.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

#include "FileSource.h"

namespace icamera {

FileSource::FileSource(int cameraId) :
        CaptureUnit(cameraId, V4L2_MEMORY_USERPTR),
        mCameraId(cameraId),
        mExitPending(false),
        mFps(30),
        mSequence(-1),
        mOutputPort(INVALID_PORT)
{
    LOGD("%s: FileSource is created for debugging.", __func__);

    const char * injectedFile = PlatformData::getInjectedFile();

    // Check if a xml config file is used or use frame file directly.
    if (injectedFile) {
        mInjectedFile = string(injectedFile);
    }
    string suffix = ".xml";
    size_t fullSize = mInjectedFile.size();
    size_t suffixSize = suffix.size();

    // If mInjectedFile is ended with ".xml", it means we're using config file mode.
    mUsingConfigFile = (fullSize > suffixSize) &&
                       (mInjectedFile.compare(fullSize - suffixSize, suffixSize, suffix) == 0);

    mProduceThread = new ProduceThread(this);
}

FileSource::~FileSource()
{
    LOG1("%s: FileSource is destroyed.", __func__);
    delete mProduceThread;
}

int FileSource::init()
{
    LOG1("%s", __func__);

    return OK;
}

void FileSource::deinit()
{
    LOG1("%s", __func__);
}

int FileSource::configure(const map<Port, stream_t>& outputFrames, const vector<ConfigMode>& configModes)
{
    Check(outputFrames.size() != 1, BAD_VALUE, "Support one port of input only.");

    mOutputPort = outputFrames.begin()->first;
    mStreamConfig = outputFrames.begin()->second;
    LOG1("%s, mCameraId:%d, w:%d, h:%d, f:%s", __func__, mCameraId,
          mStreamConfig.width, mStreamConfig.height,
          CameraUtils::format2string(mStreamConfig.format));
    return OK;
}

int FileSource::allocateSourceBuffer()
{
    LOG1("%s", __func__);
    int fmt = mStreamConfig.format;
    int width = mStreamConfig.width;
    int height = mStreamConfig.height;
    // Get frame size with aligned height taking in count for internal buffers.
    uint32_t size = CameraUtils::getFrameSize(fmt, width, height, true);
    map<int, string> frameFileName;
    if (mUsingConfigFile) {
        FileSourceProfile profile(mInjectedFile);
        int ret = profile.getFrameFiles(mCameraId, frameFileName);
        Check(ret != OK, BAD_VALUE, "Cannot find the frame files");
        for (const auto& item : frameFileName)
            frameFileName[item.first] = profile.getFrameFile(mCameraId, item.first);
    } else {
       frameFileName[0] = mInjectedFile;
    }

    for (const auto& item : frameFileName) {
        string fileName = item.second;
        // Prepare the buffers to read the files for frame
        LOG1("%s: alloc buf for Frame File: %s", __func__, fileName.c_str());
        shared_ptr<CameraBuffer> buf = CameraBuffer::create(mCameraId,
                BUFFER_USAGE_GENERAL, V4L2_MEMORY_USERPTR, size, 0, fmt, width, height);
        Check(!buf, BAD_VALUE, "@%s: Allocate producer buffer failed", __func__);
        fillFrameBuffer(fileName, buf);
        mFrameFileBuffers[fileName] = buf;
    }
    return OK;
}

int FileSource::start()
{
    LOG1("%s", __func__);

    AutoMutex l(mLock);
    allocateSourceBuffer();
    mSequence = -1;
    mExitPending = false;
    mProduceThread->run("FileSource", PRIORITY_URGENT_AUDIO);

    return OK;
}

int FileSource::stop()
{
    LOG1("%s", __func__);

    {
        AutoMutex l(mLock);
        mExitPending = true;
        mProduceThread->requestExit();
        mBufferSignal.signal();
    }

    mProduceThread->requestExitAndWait();
    mFrameFileBuffers.clear();

    return OK;
}

int FileSource::qbuf(Port port, const shared_ptr<CameraBuffer> &camBuffer)
{
    LOG2("%s", __func__);

    Check(!camBuffer, BAD_VALUE, "Camera buffer is null");

    AutoMutex l(mLock);

    bool needSignal = mBufferQueue.empty();
    mBufferQueue.push(camBuffer);
    if (needSignal) {
        mBufferSignal.signal();
    }

    return OK;
}

/**
 * The thread loop function that's used to produce frame buffers regularly.
 */
bool FileSource::produce()
{
    LOG1("%s", __func__);

    mSequence++;

    static const nsecs_t kWaitDuration = 40000000000; // 40s
    shared_ptr<CameraBuffer> qBuffer;
    timeval start, end;
    gettimeofday(&start, nullptr);

    {
        // Find a buffer which needs to be filled.
        ConditionLock lock(mLock);
        while(mBufferQueue.empty()) {
            if (mExitPending) {
                return false;
            }
            int ret = mBufferSignal.waitRelative(lock, kWaitDuration);
            if (mExitPending || ret == TIMED_OUT) {
                return false;
            }
        }

        qBuffer = mBufferQueue.front();
        mBufferQueue.pop();
    }

    notifySofEvent();

    fillFrameBuffer(qBuffer);

    gettimeofday(&end, nullptr);

    long seconds  = end.tv_sec  - start.tv_sec;
    long uSeconds = end.tv_usec - start.tv_usec;
    // Check how long to sleep to meet the specified FPS requirement.
    long sleepTime = (1000000 / mFps) -  (seconds * 1000000 + uSeconds);
    LOG2("Need to sleep: %ld us", sleepTime);
    // No need sleep if it takes too long to prepare the buffer.
    if (sleepTime > 0) {
        usleep(sleepTime);
    }

    qBuffer->getV4l2Buffer().sequence = mSequence;
    qBuffer->getV4l2Buffer().timestamp = end;

    notifyFrame(qBuffer);

    return !mExitPending;
}

void FileSource::fillFrameBuffer(string fileName, shared_ptr<CameraBuffer>& buffer)
{
    Check(fileName.empty(), VOID_VALUE, "Invalid frame file.");
    LOG2("Read frame from frame file:%s", fileName.c_str());

    ifstream file(fileName.c_str(), ios::in | ios::binary | ios::ate);
    Check(!file.is_open(), VOID_VALUE, "Cannot open frame file:%s", fileName.c_str());

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < buffer->getBufferSize()) {
        LOGW("The size of file:%s is less than buffer's requirement.", fileName.c_str());
    }
    file.read((char*)buffer->getBufferAddr(), std::min(fileSize, (size_t)buffer->getBufferSize()));
}

void FileSource::fillFrameBuffer(shared_ptr<CameraBuffer>& buffer)
{
    string fileName = mInjectedFile;

    if (mUsingConfigFile) {
        FileSourceProfile profile(mInjectedFile);
        fileName = profile.getFrameFile(mCameraId, mSequence);
        mFps = profile.getFps(mCameraId);
    }
    Check(fileName.empty(), VOID_VALUE, "Invalid frame file.");
    LOG2("Frame sequence:%ld uses frame file:%s", mSequence, fileName.c_str());

    for (auto& item : mFrameFileBuffers) {
        if (fileName.compare(item.first) == 0) {
            MEMCPY_S((buffer->getBufferAddr()), buffer->getBufferSize(),
                    (item.second->getBufferAddr()), item.second->getBufferSize());
            return;
        }
    }
    LOGE("Not find the framefile: %s", fileName.c_str());
}

void FileSource::notifyFrame(const shared_ptr<CameraBuffer>& buffer)
{
    EventData frameData;
    frameData.type = EVENT_ISYS_FRAME;
    frameData.buffer = nullptr;
    frameData.data.frame.sequence = mSequence;
    frameData.data.frame.timestamp = buffer->getV4l2Buffer().timestamp;
    notifyListeners(frameData);

    for (auto& consumer : mBufferConsumerList) {
        consumer->onFrameAvailable(mOutputPort, buffer);
    }
}

void FileSource::notifySofEvent()
{
    timeval timestamp;
    gettimeofday(&timestamp, nullptr);

    EventDataSync syncData;
    syncData.sequence = mSequence;
    syncData.timestamp = timestamp;
    EventData eventData;
    eventData.type = EVENT_ISYS_SOF;
    eventData.buffer = nullptr;
    eventData.data.sync = syncData;
    notifyListeners(eventData);
}

void FileSource::addFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s", __func__);

    AutoMutex l(mLock);
    mBufferConsumerList.push_back(listener);
}

void FileSource::removeFrameAvailableListener(BufferConsumer *listener)
{
    LOG1("%s", __func__);

    AutoMutex l(mLock);
    for (auto it = mBufferConsumerList.begin(); it != mBufferConsumerList.end(); ++it) {
        if ((*it) == listener) {
            mBufferConsumerList.erase(it);
            return ;
        }
    }
}

void FileSource::removeAllFrameAvailableListener()
{
    LOG1("%s", __func__);

    AutoMutex l(mLock);
    mBufferConsumerList.clear();
}

void FileSource::registerListener(EventType eventType, EventListener* eventListener)
{
    EventSource::registerListener(eventType, eventListener);
}

void FileSource::removeListener(EventType eventType, EventListener* eventListener)
{
    EventSource::removeListener(eventType, eventListener);
}

FileSourceProfile::FileSourceProfile(string configFile) : mCurrentDataField(FIELD_INVALID)
{
    LOGXML("@%s", __func__);

    parseXmlFile(configFile);
    mergeCommonConfig();

    for (auto& item : mConfigs) {
        // The first frame, that is the frame for sequence 0 MUST be provided.
        if (item.second.mFrameFiles.find(0) == item.second.mFrameFiles.end()) {
            LOGE("Sensor:%s, frame file for sequence 0 MUST be provided", item.first.c_str());
        }
    }
}

int FileSourceProfile::getFps(int cameraId)
{
    const char* sensorName = PlatformData::getSensorName(cameraId);
    if (mConfigs.find(sensorName) != mConfigs.end()) {
        return mConfigs[sensorName].mFps;
    }

    return mCommon.mFps;
}

int FileSourceProfile::getFrameFiles(int cameraId, map<int, string> &framefiles)
{
    const char* sensorName = PlatformData::getSensorName(cameraId);
    Check(mConfigs.find(sensorName) == mConfigs.end(), BAD_VALUE,
          "Failed to find the sensor: %s.", sensorName);

    FileSourceConfig& config = mConfigs[sensorName];
    framefiles = config.mFrameFiles;
    return OK;
}

string FileSourceProfile::getFrameFile(int cameraId, long sequence)
{
    const char* sensorName = PlatformData::getSensorName(cameraId);
    Check(mConfigs.find(sensorName) == mConfigs.end(), "",
          "Failed to find the sensor: %s.", sensorName);

    // Find the frame file which is the equal or most closest to the given sequence.
    FileSourceConfig& config = mConfigs[sensorName];
    long targetSequence = -1;
    for (const auto& item : config.mFrameFiles) {
        if (item.first == sequence) {
            targetSequence = sequence;
            break;
        }

        if (item.first < sequence && item.first > targetSequence) {
            targetSequence = item.first;
        }
    }
    Check(targetSequence == -1, "", "Cannot find the frame file for sequence:%ld", sequence);

    string fullName = config.mFrameDir + "/" + config.mFrameFiles[targetSequence];
    return fullName;
}

/**
 * Merge the common config into sensor specific configs.
 */
void FileSourceProfile::mergeCommonConfig()
{
    LOGXML("Common fps:%d, frame dir:%s", mCommon.mFps, mCommon.mFrameDir.c_str());

    for (auto& item : mConfigs) {
        // If one item isn't set in a particular sensor, then we just fill it with common settings.
        if (item.second.mFps == 0) {
            item.second.mFps = mCommon.mFps;
        }
        if (item.second.mFrameDir.empty()) {
            item.second.mFrameDir = mCommon.mFrameDir;
        }
        LOGXML("Sensor:%s, fps:%d frame dir:%s", item.first.c_str(),
               item.second.mFps, item.second.mFrameDir.c_str());

        for (const auto& sequenceFile : item.second.mFrameFiles) {
            LOGXML("Sequence:%d -> frame file:%s", sequenceFile.first, sequenceFile.second.c_str());
        }
    }
}

/**
 * This function will check which field that the parser parses to.
 *
 * The field is set to 3 types.
 * FIELD_INVALID FIELD_COMMON FIELD_SENSOR
 *
 * \param name: The element's name.
 * \param atts: The element's attribute.
 */
void FileSourceProfile::checkField(const char *name, const char **atts)
{
    LOGXML("@%s, name:%s", __func__, name);

    if (strcmp(name, "FileSource") == 0) {
        mCurrentDataField = FIELD_INVALID;
    } else if (strcmp(name, "Sensor") == 0) {
        mCurrentSensor = atts[1];
        mConfigs[mCurrentSensor] = FileSourceConfig();
        mCurrentDataField = FIELD_SENSOR;
    } else if (strcmp(name, "Common") == 0) {
        mCurrentDataField = FIELD_COMMON;
    } else {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
    }
}

/**
 * the callback function of the libexpat for handling of one element start
 *
 * When it comes to the start of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void FileSourceProfile::startElement(void *userData, const char *name, const char **atts)
{
    LOGXML("@%s %s", __func__, name);

    FileSourceProfile *profile = reinterpret_cast<FileSourceProfile*>(userData);

    switch (profile->mCurrentDataField) {
        case FIELD_INVALID:
            profile->checkField(name, atts);
            break;
        case FIELD_COMMON:
            if (strcmp(name, "fps") == 0) {
                profile->mCommon.mFps = std::stoi(atts[1]);
            } else if (strcmp(name, "frameDir") == 0) {
                profile->mCommon.mFrameDir = atts[1];
            }
            break;
        case FIELD_SENSOR:
        {
            FileSourceConfig& config = profile->mConfigs[profile->mCurrentSensor];
            if (strcmp(name, "fps") == 0) {
                config.mFps = std::stoi(atts[1]);
            } else if (strcmp(name, "frameDir") == 0) {
                config.mFrameDir = atts[1];
            } else if (strcmp(name, "frameFile") == 0) {
                config.mFrameFiles[std::stoi(atts[1])] = atts[3];
            }
            break;
        }
        default:
            LOGE("Invalid field to handle");
            break;
    }
}

/**
 * the callback function of the libexpat for handling of one element end
 *
 * When it comes to the end of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void FileSourceProfile::endElement(void *userData, const char *name)
{
    LOGXML("@%s %s", __func__, name);

    FileSourceProfile *profile = reinterpret_cast<FileSourceProfile*>(userData);

    if (strcmp(name, "Sensor") == 0) {
        profile->mCurrentSensor = "";
        profile->mCurrentDataField = FIELD_INVALID;
    } else if (strcmp(name, "Common") == 0) {
        profile->mCurrentDataField = FIELD_INVALID;
    }
}

void FileSourceProfile::parseXmlFile(const string &xmlFile)
{
    LOGXML("@%s, parsing profile: %s", __func__, xmlFile.c_str());

    if (xmlFile.empty())  return;

    FILE* fp = fopen(xmlFile.c_str(), "r");
    Check(!fp, VOID_VALUE, "Can not open profile file %s", xmlFile.c_str());

    XML_Parser parser = XML_ParserCreate(nullptr);
    if (parser == nullptr) {
        LOGE("Create XML parser failed.");
        fclose(fp);
        return;
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);

    static const int kBufSize = 1024;
    char pTmpBuf[kBufSize];
    bool done = false;

    while (!done) {
        int len = fread(pTmpBuf, 1, kBufSize, fp);
        if (!len && ferror(fp)) {
            clearerr(fp);
            break;
        }
        done = len < kBufSize;
        if (XML_Parse(parser, pTmpBuf, len, done) == XML_STATUS_ERROR) {
            LOGE("@%s, XML_Parse error", __func__);
            break;
        }
    }

    XML_ParserFree(parser);
    fclose(fp);
}

} // end of namespace icamera

