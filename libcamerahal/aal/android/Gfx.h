/*
 * Copyright (C) 2015-2018 Intel Corporation
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

#include <system/window.h>
#include "iVP.h"
#include "Parameters.h"
#include <ui/GraphicBuffer.h>
#include "camera3.h"
#if defined(GRAPHIC_IS_GEN)
#include <ufo/gralloc.h>
#endif // GRAPHIC_IS_GEN

#ifndef ENABLE_IVP
#include <va/va.h>
#define ANDROID_DISPLAY_HANDLE 0x18C34078
#ifndef VA_FOURCC_R5G6B5
#define VA_FOURCC_R5G6B5        VA_FOURCC('R', 'G', '1', '6')
#endif
#endif

namespace android {

/**
  * Package to hold the native window buffer and its corresponding
  * hal allocated buffer. The hal allocated buffer is used in case
  * de-interlacing is required. We will pass the hall allocated buffer
  * to the isys and use it as input to the graphics downscaler to scale
  * to the needed resolution by the native window.
  */

typedef struct BufferPackage {
    camera3_stream_buffer_t *nativeWinBuf; // dst for GPU
    camera3_stream_buffer_t *nativeHalBuf; // src for GPU
    int flag;                              // using flag
    BufferPackage() {
        nativeWinBuf = nullptr;
        nativeHalBuf = nullptr;
        flag = 0;
    }
} BufferPackage;

#ifndef ENABLE_IVP
typedef struct VideoProcContext {
    VADisplay   vaDisplay;       // Display handle for VA context
    VAConfigID  vaConfig;        // Configuration for VA Context
    VAContextID vaContext;       // VA Context for video post processing
    VABufferID  srcBuffer;
    VASurfaceID srcSurface;
    VASurfaceID dstSurface;
    VARectangle srcRect;
    VARectangle dstRect;
} VideoProcContext;
#endif

class CameraGfxBuffer: public RefBase {
public:
    CameraGfxBuffer(int w, int h, int s, int format, GraphicBuffer *gfxBuf, void * ptr);
    void* data() { return mDataPtr; };
    int width() {return mWidth; }
    int height() {return mHeight; }
    int stride() {return mStride; }
    unsigned int size() {return mSize; }
    int format() {return mFormat; }
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
    int             mStride;
    GraphicBuffer * mGfxBuffer;
    void*           mDataPtr;
    bool            mInuse;
};


void *getPlatNativeHandle(buffer_handle_t *handle);
int getNativeHandleStride(buffer_handle_t *handle);
int getNativeHandleSize(buffer_handle_t *handle, int halFormat);
int getNativeHandleWidth(buffer_handle_t *handle);
int getNativeHandleIonFd(buffer_handle_t *handle);
int getNativeHandleDmaBufFd(buffer_handle_t *handle);

int setBufferColorRange(buffer_handle_t *handle, bool fullRange);

#if defined(USE_CROS_GRALLOC)
int lockBuffer(buffer_handle_t *handle, int format, uint64_t producerUsage, uint64_t consumerUsage,
               int width, int height, void **pAddr, int acquireFence);
int unlockBuffer(buffer_handle_t *handle, int *pOutReleaseFence = nullptr);

int getNativeHandleDimensions(buffer_handle_t *handle, uint32_t *pWidth,
                              uint32_t *pHeight, uint32_t *pStride);
#elif defined(GRAPHIC_IS_GEN)
bool getBufferInfo(buffer_handle_t *handle, intel_ufo_buffer_details_t *info);
#endif // GRAPHIC_IS_GEN

CameraGfxBuffer* allocateGraphicBuffer(int w, int h, int gfxFmt, uint32_t usage);

class GenImageConvert {
public:
    GenImageConvert();
    ~GenImageConvert();
    // image down scaling and color conversion
    status_t downScalingAndColorConversion(BufferPackage &bp);
    bool isGraphicBufferNeeded(void) { return true; }
private:
    status_t cameraBuffer2iVPLayer(const camera3_stream_buffer_t* cameraBuffer,
                                   buffer_handle_t *buffHandle,
                                   iVP_layer_t *iVPLayer, int left = 0, int top = 0);
    status_t iVPColorConversion(BufferPackage &bp);
private:
    bool miVPCtxValid;
    iVPCtxID miVPCtx;
#ifndef ENABLE_IVP
    VideoProcContext vaContext;
#endif

}; // GenImageConvert
} // namespace android

