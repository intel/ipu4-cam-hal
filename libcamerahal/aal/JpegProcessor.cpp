/*
 * Copyright (C) 2018 Intel Corporation
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

#define LOG_TAG "JpegProcessor"

#include "ICamera.h"
#include "Utils.h"
#include "Errors.h"
#include "JpegProcessor.h"
#include "IJpeg.h"
#include "Exif.h"
#include "EXIFMetaData.h"
#include "Parameters.h"
#include "HALv3Utils.h"
#ifdef CAL_BUILD
#include <cros-camera/jpeg_compressor.h>
#include "ColorConverter.h"
#endif

namespace camera3 {

JpegProcessor::JpegProcessor() : mInternalBufferSize(0)
{
    LOG1("@%s", __func__);
}

JpegProcessor::~JpegProcessor()
{
    LOG1("@%s", __func__);
}

void JpegProcessor::attachJpegBlob(int finalJpegSize, icamera::EncodePackage &package)
{
    LOG2("@%s", __func__);
    Check(!package.jpegSize, VOID_VALUE, "ERROR: JPEG_MAX_SIZE is 0 !");

    LOG2("actual jpeg size=%d, jpegbuffer size=%d", finalJpegSize, package.jpegSize);
    unsigned char *currentPtr = static_cast<unsigned char*>(package.jpegOut->addr)
                             + package.jpegSize
                             - sizeof(struct camera3_jpeg_blob);
    struct camera3_jpeg_blob *blob = reinterpret_cast<struct camera3_jpeg_blob *>(currentPtr);
    // save jpeg size at the end of file
    blob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    blob->jpeg_size = static_cast<uint32_t>(finalJpegSize);
}

#ifdef CAL_BUILD
int JpegProcessor::doJpegEncode(const icamera::InputBuffer &in, const icamera::OutputBuffer *out)
{
    LOG1("@%s", __func__);
    std::unique_ptr<cros::JpegCompressor> jpegCompressor(cros::JpegCompressor::GetInstance());

    int width = in.width;
    int height = in.height;
    int stride = in.stride;
    void *srcY = in.buf;
    void *srcUV = static_cast<unsigned char*>(in.buf) + stride * height;
    unsigned int currentBufferSize = width * height * 3/2;

    if (mInternalBufferSize != currentBufferSize) {
        mInternalBufferSize = currentBufferSize;
        mInternalBuffer.reset(new char[mInternalBufferSize]);
    }

    void* tempBuf = mInternalBuffer.get();

    switch (in.fourcc) {
    case V4L2_PIX_FMT_YUYV:
        icamera::YUY2ToP411(width, height, stride, srcY, tempBuf);
        break;
    case V4L2_PIX_FMT_NV12:
        icamera::NV12ToP411Separate(width, height, stride, srcY, srcUV, tempBuf);
        break;
    case V4L2_PIX_FMT_NV21:
        icamera::NV21ToP411Separate(width, height, stride, srcY, srcUV, tempBuf);
        break;
    default:
        LOGE("%s Unsupported format %d", __func__, in.fourcc);
        return 0;
    }

    uint32_t outSize = 0;
    void* pDst = out->buf;
    bool ret = jpegCompressor->CompressImage(tempBuf,
                                            width, height, out->quality,
                                            nullptr, 0,
                                            out->size, pDst,
                                            &outSize);
    LOG1("%s: encoding ret:%d, %dx%d, jpeg size %u, quality %d)",
         __func__, ret, out->width, out->height, outSize, out->quality);
    Check(!ret, 0, "@%s, jpegCompressor->CompressImage() fails", __func__);

    return outSize;
}
#endif

icamera::status_t JpegProcessor::doJpegProcess(icamera::camera_buffer_t &mainBuf,
                                               icamera::camera_buffer_t &thumbBuf,
                                               icamera::Parameters &parameter,
                                               icamera::camera_buffer_t &jpegBuf)
{
    LOG1("@%s", __func__);

    // make jpeg with thumbnail or not
    icamera::camera_resolution_t thumbSize = {0};
    parameter.getJpegThumbnailSize(thumbSize);
    LOG1("%s request thumbname size %dx%d", __func__, thumbSize.width, thumbSize.height);
    bool isThumbnailAvailable = (thumbSize.width != 0 && thumbSize.height != 0) ? true : false;

    icamera::status_t status = icamera::camera_jpeg_init();
    Check(status != icamera::OK, icamera::UNKNOWN_ERROR, "@%s, failed to init jpeg", __func__);

    // main jpeg picture
    icamera::InputBuffer inBuf;
    icamera::OutputBuffer outBuf;

    int finalJpegSize = 0, jpegEncodeSize = 0;
    icamera::camera_buffer_t jpegSource {};
    icamera::camera_buffer_t jpegThumbnailSource {};
    icamera::EncodePackage package {};

    // initialize package info
    package.mainWidth = mainBuf.s.width;
    package.mainHeight = mainBuf.s.height;
    package.mainSize = mainBuf.s.size;
    package.jpegOut = &jpegBuf;
    package.jpegSize = jpegBuf.s.size;
    package.params = &parameter;
    if (isThumbnailAvailable) {
        package.thumbWidth = thumbBuf.s.width;
        package.thumbHeight = thumbBuf.s.height;
    }

    icamera::ExifMetaData exifMetadata = {};
    status = icamera::camera_setupExifWithMetaData(package, &exifMetadata);
    Check(status != icamera::OK, icamera::UNKNOWN_ERROR, "@%s, Setup exif metadata failed.", __func__);
    LOG2("@%s: setting exif metadata done!", __func__);
    int quality = exifMetadata.mJpegSetting.jpegQuality;

    std::unique_ptr<unsigned char[]> mainJpegEncData(new unsigned char[mainBuf.s.size]);
    jpegSource.addr = mainJpegEncData.get();

    inBuf = {
        static_cast<unsigned char *>(mainBuf.addr),
        mainBuf.s.width,
        mainBuf.s.height,
        mainBuf.s.stride,
        mainBuf.s.format,
        mainBuf.s.size
    };

    outBuf = {
        mainJpegEncData.get(),
        mainBuf.s.width,
        mainBuf.s.height,
        mainBuf.s.size,
        quality
    };
    LOG2("main picture size: %d, width: %d, height: %d.",
        mainBuf.s.size, mainBuf.s.width, mainBuf.s.height);

    // do main jpeg encoding
#ifdef CAL_BUILD
    jpegEncodeSize = doJpegEncode(inBuf, &outBuf);
#else
    jpegEncodeSize = icamera::camera_jpeg_encode(inBuf, outBuf);
#endif
    Check(jpegEncodeSize == 0, icamera::BAD_VALUE, "jpeg encode size is 0!");
    LOG2("@%s: main picture jpeg encoding done! encoded size: %d", __func__, jpegEncodeSize);
    package.main = &jpegSource;
    package.encodedDataSize = jpegEncodeSize;

    // thumbnail jpeg picture
    icamera::InputBuffer inThumbBuf;
    icamera::OutputBuffer outThumbBuf;

    std::unique_ptr<unsigned char[]> thumbnailJpegEncData;
    // do thumbnail jpeg encoding
    if (isThumbnailAvailable) {
        thumbnailJpegEncData.reset(new unsigned char[thumbBuf.s.size]);
        jpegThumbnailSource.addr = thumbnailJpegEncData.get();

        inThumbBuf = {
            static_cast<unsigned char *>(thumbBuf.addr),
            thumbBuf.s.width,
            thumbBuf.s.height,
            thumbBuf.s.stride,
            thumbBuf.s.format,
            thumbBuf.s.size
        };

        outThumbBuf = {
            thumbnailJpegEncData.get(),
            thumbBuf.s.width,
            thumbBuf.s.height,
            thumbBuf.s.size,
            quality
        };
        LOG2("thumbnail picture size: %d, width: %d, height: %d.",
            thumbBuf.s.size, thumbBuf.s.width, thumbBuf.s.height);

        int thumbEncodeSize = 0;
#ifdef CAL_BUILD
        thumbEncodeSize = doJpegEncode(inThumbBuf, &outThumbBuf);
#else
        thumbEncodeSize = icamera::camera_jpeg_encode(inThumbBuf, outThumbBuf);
#endif
        Check(thumbEncodeSize == 0, icamera::BAD_VALUE, "thumbnail jpeg encode size is 0!");
        LOG2("@%s: thumbnail picture jpeg encoding done! encoded size: %d",
            __func__, thumbEncodeSize);
        package.thumb = &jpegThumbnailSource;
        package.thumbSize = thumbEncodeSize;
    }

    status = icamera::camera_jpeg_make(package, finalJpegSize);
    Check(status != icamera::OK, icamera::UNKNOWN_ERROR, "@%s, make jpeg failed.", __func__);
    Check(package.jpegSize < finalJpegSize, icamera::UNKNOWN_ERROR, "ERROR: alloc jpeg output size is not enough");

    attachJpegBlob(finalJpegSize, package);
    LOG2("@%s: jpeg making done! final jpeg size: %d", __func__, finalJpegSize);

    icamera::camera_jpeg_deinit();
    return icamera::OK;
}
} // namespace camera3
