/*
 * Copyright (C) 2015-2016 Intel Corporation
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

#ifndef _CAMERA3_GFX_FORMAT_H_
#define _CAMERA3_GFX_FORMAT_H_

#include <system/window.h>
#include "iVP.h"
#include "Parameters.h"
#include <ui/GraphicBuffer.h>

namespace android {

/**
  * Package to hold the native window buffer and its corresponding
  * hal allocated buffer. The hal allocated buffer is used in case
  * de-interlacing is required. We will pass the hall allocated buffer
  * to the isys and use it as input to the graphics downscaler to scale
  * to the needed resolution by the native window.
  */
struct BufferPackage {
    icamera::camera_buffer_t nativeWinBuffer;
    buffer_handle_t *nativeWinBuffHandle;
    icamera::camera_buffer_t nativeHalBuffer;
    buffer_handle_t *nativeHalBuffHandle;
};


class CameraGfxBuffer: public RefBase {
public:
    CameraGfxBuffer(int w, int h, int s, int format, int v4L2Fmt, GraphicBuffer *gfxBuf, void * ptr, int usage = 0);
    void* data() { return mDataPtr; };
    int width() {return mWidth; }
    int height() {return mHeight; }
    int stride() {return mStride; }
    unsigned int size() {return mSize; }
    int format() {return mFormat; }
    int v4l2Fmt() {return mV4L2Fmt; }
    bool inUse() {return mInuse;}
    void setInUse(bool use) {mInuse = use;}
    buffer_handle_t *getBufferHandle() {return &mGfxBuffer->handle;}
    void setDataPtr(void *dataPtr) {mDataPtr = dataPtr;}

private:  /* methods */
    /**
     * no need to delete a buffer since it is RefBase'd. Buffer will be deleted
     * when no reference to it exist.
     */
    virtual ~CameraGfxBuffer();

private:
    int             mWidth;
    int             mHeight;
    unsigned int    mSize;           /*!< size in bytes, this is filled when we
                                           lock the buffer */
    int             mFormat;         /*!<  Gfx HAL PIXEL fmt */
    int             mV4L2Fmt;        /*!< V4L2 fourcc format code */
    int             mStride;
    GraphicBuffer * mGfxBuffer;
    void*           mDataPtr;
    bool            mInuse;
};

void *getPlatNativeHandle(buffer_handle_t *handle);
int getNativeHandleStride(buffer_handle_t *handle);
int getNativeHandleSize(buffer_handle_t *handle);
int getNativeHandleWidth(buffer_handle_t *handle);
int getNativeHandleIonFd(buffer_handle_t *handle);
int getNativeHandleDmaBufFd(buffer_handle_t *handle);

int setBufferColorRange(buffer_handle_t *handle, bool fullRange);

CameraGfxBuffer* allocateGraphicBuffer(int w, int h, int gfxFmt, int v4l2Fmt, uint32_t usage = 0,
                                       uint32_t lockUsage = (GRALLOC_USAGE_SW_READ_OFTEN   |
                                                             GRALLOC_USAGE_SW_WRITE_NEVER  |
                                                             GRALLOC_USAGE_HW_CAMERA_READ  |
                                                             GRALLOC_USAGE_HW_CAMERA_WRITE |
                                                             GRALLOC_USAGE_HW_COMPOSER),
                                        uint32_t createUsage = (GRALLOC_USAGE_HW_RENDER      |
                                                                GRALLOC_USAGE_SW_WRITE_OFTEN |
                                                                GRALLOC_USAGE_HW_TEXTURE     |
                                                                GRALLOC_USAGE_HW_CAMERA_WRITE));

class GenImageConvert {
public:
    GenImageConvert();
    ~GenImageConvert();
    // image down scaling and color conversion
    status_t downScalingAndColorConversion(BufferPackage &bp);
    bool isGraphicBufferNeeded(void) { return true; }
private:
    status_t cameraBuffer2iVPLayer(icamera::camera_buffer_t cameraBuffer,
                                   buffer_handle_t *buffHandle,
                                   iVP_layer_t *iVPLayer, int left = 0, int top = 0);
    status_t iVPColorConversion(BufferPackage &bp);
private:
    bool miVPCtxValid;
    iVPCtxID miVPCtx;

}; // GenImageConvert


} // namespace android

#endif // _CAMERA3_GFX_FORMAT_H_
