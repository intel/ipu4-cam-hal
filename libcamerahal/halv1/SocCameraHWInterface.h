/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
** Copyright 2015-2017, Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H

#include <utils/threads.h>
#include <utils/RefBase.h>
#include <hardware/camera.h>
#include <hardware/gralloc.h>
#include <camera/CameraParameters.h>
#include <ICamera.h>
#include "Parameters.h"
#include "Gfx.h"

#define MAX_BUFFERS 16
#define MAX_CAMERAS 8
namespace android {
    class CameraHardwareSoc : public virtual RefBase {
public:
    virtual void        setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void *user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const void *opaque);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1, int32_t arg2);
    virtual status_t    setPreviewWindow(preview_stream_ops *w);
    virtual status_t    storeMetaDataInBuffers(bool enable);
    virtual void        release();

    inline  int         getCameraId() const;

    CameraHardwareSoc(int cameraId, camera_device_t *dev);
    virtual             ~CameraHardwareSoc();

private: /* private types */
    class PreviewThread : public Thread {
        CameraHardwareSoc *mHardware;
    public:
        PreviewThread(CameraHardwareSoc *hw):
        Thread(false),
        mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardwareSoc *mHardware;
    public:
        PictureThread(CameraHardwareSoc *hw):
        Thread(false),
        mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardwareSoc *mHardware;
    public:
        AutoFocusThread(CameraHardwareSoc *hw):
        Thread(false),
        mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAutoFocusThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return true;
        }
    };


    enum {
        BUFFER_NOT_OWNED,
        BUFFER_OWNED,
    };

struct JpegCaptureBuffs {
    sp<CameraGfxBuffer> scalerInBuf;
    sp<CameraGfxBuffer> scalerOutBuf;
    sp<CameraGfxBuffer> scalerOutBuf2;
};

private: /* Methods */
    int         findXmlId(const char *sensorCfgName);
    void        setDeviceId(int cameraId);
    status_t    configStreams();
    status_t    startPreviewInternal();
    void        stopPreviewInternal();
    status_t    setInternalParameters();
    int         pictureThread();
    int         autoFocusThread();
    status_t    waitCaptureCompletion();
    void        initDefaultParameters(int cameraId);
    int         previewThread();
    int         previewThreadWrapper();
    bool        isSupportedPreviewSize(const int width,
                                       const int height) const;
    bool        isSupportedPictureSize(const int width,
                                       const int height) const;
    //internal functions
    int displayBuffer(int index);
    int allocateGfxBuf(int count);
    int getExtraHeight(int w, int h, int gfxFmt, int v4l2Fmt);
    int allocateHalBuffers(int count);
    void deallocateGfxBuf();
    void deallocateHalBuffers();
    int allocateBuffJpeg();
    int getBitsPerPixel(int format);
    void getSupportedV4L2Formats();
    bool isSupportedStreamFormat(int halFormat);
    int previewFormat2HalEnum(const char *format);
    int imageFormat2HalEnum(const char *format);
    int V4L2Format2HalFormat(int V4L2Format);
    int HalFormat2V4L2Format(int HalFormat);
    int calculateBufferSize(int width, int height, int format);
    void copyBufForDataCallback(void *dst_buf, void *src_buf, int found);
    void deallocateBuffJpeg();
    long cal_diff(const struct timeval t1, const struct timeval t2);
    void writeData(const void* data, int size, const char* fileName);
    const char* getCurrentSensorName();
    void getInputConfig();
private:  /* Members */
    static  const int   kBufferCount = 8;
    static  const int   kBufferCountForRecord = 5;
    static gralloc_module_t const* mGrallocHal;
    icamera::stream_config_t mStream_list;
    icamera::stream_t mStreams[1];
    icamera::stream_t mInputConfig;

    sp<PreviewThread>   mPreviewThread;
    /* used by preview thread to block until it's told to run */
    mutable Mutex       mPreviewLock;
    mutable Condition   mPreviewCondition;
    mutable Condition   mPreviewStoppedCondition;
    bool mPreviewRunning;
    bool mPreviewStartDeferred;
    bool mExitPreviewThread;

    sp<PictureThread>   mPictureThread;
    bool        mCaptureInProgress;
    /* used to guard mCaptureInProgress */
    mutable Mutex       mCaptureLock;
    mutable Condition   mCaptureCondition;

    sp<AutoFocusThread> mAutoFocusThread;
    mutable Mutex       mFocusLock;
    mutable Condition   mFocusCondition;
    bool                mExitAutoFocusThread;
    bool                mNeedInternalBuf;
    bool                mDisplayDisabled;
    bool                mPerfEnabled;


    CameraParameters    mParameters;
    bool                mCanUsePsys;

    camera_notify_callback     mNotifyCb;
    camera_data_callback       mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    camera_request_memory      mGetMemoryCb;
    void        *mCallbackCookie;
    int32_t     mMsgEnabled;

    int mPreviewWidth;
    int mPreviewHeight;
    int mPictureWidth;
    int mPictureHeight;
    int mThumbnailWidth;
    int mThumbnailHeight;
    int mPreviewSize;
    int mJpegQuality;
    int mJpegThumbnailQuality;
    int mNativeWindowStride;
    int mUsage;
    int mFormat;
    int mV4l2Format;
    int mISysV4l2Format;
    int mMinUndequeuedBuffers;
    int mBufferCount;
    int mCameraId;  //Camera Id from framework
    int mDeviceId;  // Sensor Id in the xml
    int mField;
    int mDeinterlaceMode;
    BufferPackage mBufferPackage[MAX_BUFFERS]; // Native window and hal allocated buffers
    Vector<sp<CameraGfxBuffer> > mGfxPtrs; // To be used for deleting
    int mLocalFlag[MAX_BUFFERS];

    preview_stream_ops *mWindow;
    icamera::Parameters    mInternalParameters;
    Vector<Size> mSupportedPreviewSizes;
    Vector<Size> mSupportedPictureSizes;
    Vector<int> mSupportedV4L2Formats;

    camera_memory_t     *mRecordHeap[MAX_BUFFERS];
    bool                mRecordRunning;
    mutable Mutex       mRecordLock;

    GenImageConvert *mGenConvert;
    JpegCaptureBuffs mjcBuffers;

    struct timeval mBase;
    long mBufcount;
    float mFps;
};

}; // namespace android

#endif
