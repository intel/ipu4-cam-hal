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

#pragma once

#include "EXIFMaker.h"
#include "EXIFMetaData.h"
#include "iutils/Errors.h"
#include "iutils/Utils.h"
#include "Parameters.h"
#include "IJpeg.h"

namespace icamera {

struct JpegSettings {
    int jpegQuality;
    int jpegThumbnailQuality;
    int thumbWidth;
    int thumbHeight;
    int orientation;
 };

/**
 * \class JpegMaker
 * Does the EXIF header creation and appending to the provided jpeg buffer
 *
 */
class JpegMaker {
public: /* Methods */
    explicit JpegMaker();
    virtual ~JpegMaker();
    status_t init();
    status_t setupExifWithMetaData(EncodePackage & package,
                                   ExifMetaData *metaData);
    status_t makeJpeg(EncodePackage &package, int &finalSize);
    status_t makeJpegInPlace(EncodePackage & package); // makes the jpeg inside the main buf writing exif in-place

private:  /* Methods */
    // prevent copy constructor and assignment operator
    DISALLOW_COPY_AND_ASSIGN(JpegMaker);

    status_t processExifSettings(const Parameters *params, ExifMetaData *metaData);
    status_t processJpegSettings(EncodePackage & package, ExifMetaData *metaData);
    status_t processGpsSettings(const Parameters &params, ExifMetaData *metadata);
    status_t processColoreffectSettings(const Parameters &params, ExifMetaData *metaData);
    status_t processScalerCropSettings(const Parameters &params, ExifMetaData *metaData);
    status_t processEvCompensationSettings(const Parameters &params, ExifMetaData *metaData);
    int32_t  getJpegBufferSize(void);

private:  /* Members */
    EXIFMaker * mExifMaker;
};
}  // namespace icamera
