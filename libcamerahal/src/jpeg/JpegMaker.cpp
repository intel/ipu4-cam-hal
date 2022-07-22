/*
 * Copyright (C) 2016-2018 Intel Corporation
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

#define LOG_TAG "JpegMaker"

#include "JpegMaker.h"
#include "iutils/CameraLog.h"
#include "iutils/Utils.h"

namespace icamera {

static const unsigned char JPEG_MARKER_SOI[2] = {0xFF, 0xD8};  // JPEG StartOfImage marker

JpegMaker::JpegMaker() :
    mExifMaker(nullptr)
{
    ALOGV("@%s", __FUNCTION__);
}

JpegMaker::~JpegMaker()
{
    ALOGV("@%s", __FUNCTION__);

    if (mExifMaker != nullptr) {
        delete mExifMaker;
        mExifMaker = nullptr;
    }
}

status_t JpegMaker::init(void)
{
    ALOGV("@%s", __FUNCTION__);

    if (mExifMaker == nullptr) {
        mExifMaker = new EXIFMaker();
        if (mExifMaker == nullptr) {
            LOGE("ERROR: Can't create EXIF maker!");
            return NO_INIT;
        }
    }

    return OK;
}

status_t JpegMaker::setupExifWithMetaData(EncodePackage & package,
                                          ExifMetaData *metaData)
{
    ALOGV("@%s", __FUNCTION__);

    status_t status = OK;

    status = processJpegSettings(package, metaData);
    if (status != OK) {
        LOGE("@%s: Process settngs for JPEG failed! %d", __FUNCTION__, status);
        return status;
    }

    mExifMaker->initialize(package.mainWidth, package.mainHeight);
    mExifMaker->pictureTaken(metaData);

    mExifMaker->enableFlash(metaData->flashFired, metaData->v3AeMode, metaData->flashMode);
    mExifMaker->updateSensorInfo(package.params);
    mExifMaker->saveMakernote(package.params);

    status = processExifSettings(package.params, metaData);
    if (status != OK) {
        LOGE("@%s: Process settngs for Exif! %d", __FUNCTION__, status);
        return status;
    }

    mExifMaker->initializeLocation(metaData);
    mExifMaker->setSensorAeConfig();

    if (metaData->software)
        mExifMaker->setSoftware(metaData->software);

    return status;
}

/**
 * makeJpeg
 *
 * Create and prefix the exif header to the encoded jpeg data.
 * Skip the SOI marker got from the JPEG encoding. Append the camera3_jpeg_blob
 * at the end of the buffer.
 *
 * \param package [IN] EncodePackage from the caller with encoded main and thumb
 *  buffers , Jpeg settings, and encoded sizes
 * \param dest [IN] The final output buffer to client
 *
 */
status_t JpegMaker::makeJpeg(EncodePackage &package, int &finalSize)
{
    ALOGV("@%s", __FUNCTION__);
    unsigned int exifSize = 0;
    unsigned char* currentPtr = (unsigned char*)(package.jpegOut->addr);

    // Copy the SOI marker
    unsigned int jSOISize = sizeof(JPEG_MARKER_SOI);
    MEMCPY_S(currentPtr, package.jpegSize, JPEG_MARKER_SOI, jSOISize);
    currentPtr += jSOISize;

    if (package.thumb != nullptr) {
        mExifMaker->setThumbnail((unsigned char *)package.thumb->addr, package.thumbSize,
                                 package.thumbWidth, package.thumbHeight);
        exifSize = mExifMaker->makeExif(&currentPtr);
    } else {
        // No thumb is not critical, we can continue with main picture image
        exifSize = mExifMaker->makeExif(&currentPtr);
        LOGW("Exif created without thumbnail stream!");
    }
    currentPtr += exifSize;

    // Since the jpeg got from libmix JPEG encoder start with SOI marker
    // and EXIF also have the SOI marker so need to remove SOI marker fom
    // the start of the jpeg data
    MEMCPY_S(currentPtr,
           package.encodedDataSize-jSOISize,
           reinterpret_cast<char *>(package.main->addr)+jSOISize,
           package.encodedDataSize-jSOISize);

    finalSize = exifSize + package.encodedDataSize;

    return OK;
}

status_t JpegMaker::processExifSettings(const Parameters *params,
                                        ExifMetaData *metaData)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    status = processGpsSettings(*params, metaData);
    status |= processColoreffectSettings(*params, metaData);
    status |= processScalerCropSettings(*params, metaData);
    status |= processEvCompensationSettings(*params, metaData);

    return status;
}

/**
 * processJpegSettings
 *
 * Store JPEG settings to the exif metadata
 *
 * \param settings [IN] settings metadata of the request
 *
 */
status_t JpegMaker::processJpegSettings(EncodePackage & package,
                                    ExifMetaData *metaData)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    if (metaData == nullptr) {
        LOGE("MetaData struct not intialized");
        return UNKNOWN_ERROR;
    }

    const Parameters *params = package.params;
    uint8_t new_jpeg_quality = 95; // use 95 by default
    int ret = params->getJpegQuality(&new_jpeg_quality);
    if (ret != icamera::OK) {
        LOGW("cannot find jpeg quality, use default");
    }
    metaData->mJpegSetting.jpegQuality = new_jpeg_quality;

    uint8_t new_jpeg_thumb_quality = 0;
    params->getJpegThumbnailQuality(&new_jpeg_thumb_quality);
    metaData->mJpegSetting.jpegThumbnailQuality = new_jpeg_thumb_quality;
    metaData->mJpegSetting.thumbWidth = package.thumbWidth;
    metaData->mJpegSetting.thumbHeight = package.thumbHeight;

    int new_rotation = 0;
    params->getJpegRotation(new_rotation);
    metaData->mJpegSetting.orientation = new_rotation;

    LOG1("jpegQuality=%d,thumbQuality=%d,thumbW=%d,thumbH=%d,orientation=%d",
         metaData->mJpegSetting.jpegQuality,
         metaData->mJpegSetting.jpegThumbnailQuality,
         metaData->mJpegSetting.thumbWidth,
         metaData->mJpegSetting.thumbHeight,
         metaData->mJpegSetting.orientation);

    return status;
}

/**
 * This function will get GPS metadata from request setting
 *
 * \param[in] settings The Anroid metadata to process GPS settings from
 * \param[out] metadata The EXIF data where the GPS setting are written to
 */
status_t JpegMaker::processGpsSettings(const Parameters &param,
                                       ExifMetaData *metadata)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    // gps latitude
    double new_gps_latitude = 0.0;
    param.getJpegGpsLatitude(new_gps_latitude);
    metadata->mGpsSetting.latitude = new_gps_latitude;

    double new_gps_longitude = 0.0;
    param.getJpegGpsLongitude(new_gps_longitude);
    metadata->mGpsSetting.longitude = new_gps_longitude;

    double new_gps_altitude = 0.0;
    param.getJpegGpsAltitude(new_gps_altitude);
    metadata->mGpsSetting.altitude = new_gps_altitude;

    // gps timestamp
    int64_t new_gps_timestamp = 0;
    param.getJpegGpsTimeStamp(new_gps_timestamp);
    metadata->mGpsSetting.gpsTimeStamp = new_gps_timestamp;

    // gps processing method
    char new_gps_processing_method[MAX_NUM_GPS_PROCESSING_METHOD + 1];
    CLEAR(new_gps_processing_method);
    param.getJpegGpsProcessingMethod(MAX_NUM_GPS_PROCESSING_METHOD, new_gps_processing_method);
    if (strlen(new_gps_processing_method) != 0) {
        snprintf(metadata->mGpsSetting.gpsProcessingMethod,
                 sizeof(metadata->mGpsSetting.gpsProcessingMethod),
                 "%s", new_gps_processing_method);
    }

    return status;
}

status_t JpegMaker::processColoreffectSettings(const Parameters &param, ExifMetaData *metaData)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    camera_effect_mode_t  new_image_effect = CAM_EFFECT_NONE;
    param.getImageEffect(new_image_effect);
    metaData->effectMode = new_image_effect;
    ALOGV("effect mode=%d", metaData->effectMode);

    return status;
}

status_t JpegMaker::processScalerCropSettings(const Parameters &param, ExifMetaData *metaData)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    return status;
}

status_t JpegMaker::processEvCompensationSettings(const Parameters &param, ExifMetaData *metaData)
{
    ALOGV("@%s:", __FUNCTION__);
    status_t status = OK;

    return status;
}

}  // namespace icamera
