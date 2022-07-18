/*
 * Copyright (C) 2015-2018 Intel Corporation.
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

#include "Parameters.h"
#include "EXIFMetaData.h"
namespace icamera {

struct InputBuffer {
    unsigned char *buf;
    int width;
    int height;
    int stride;
    int fourcc;
    int size;

    void clear()
    {
        buf = nullptr;
        width = 0;
        height = 0;
        fourcc = 0;
        size = 0;
    }
};

struct OutputBuffer {
    unsigned char *buf;
    int width;
    int height;
    int size;
    int quality;
    int length;     /*>! amount of the data actually written to the buffer. Always smaller than size field*/

    void clear()
    {
        buf = nullptr;
        width = 0;
        height = 0;
        size = 0;
        quality = 0;
        length = 0;
    }
};


struct EncodePackage {
    EncodePackage() :
            main(nullptr),
            mainWidth(0),
            mainHeight(0),
            mainSize(0),
            encodedDataSize(0),
            thumb(nullptr),
            thumbWidth(0),
            thumbHeight(0),
            thumbSize(0),
            jpegOut(nullptr),
            jpegSize(0),
            jpegDQTAddr(nullptr),
            padExif(false),
            encodeAll(true),
            params(nullptr) {}

    camera_buffer_t *main;        // for input
    int              mainWidth;
    int              mainHeight;
    int              mainSize;
    int              encodedDataSize;
    camera_buffer_t *thumb;       // for input, can be NULL
    int              thumbWidth;
    int              thumbHeight;
    int              thumbSize;
    camera_buffer_t *jpegOut;     // for final JPEG output
    int              jpegSize;    // Jpeg output size
    unsigned char    *jpegDQTAddr; // pointer to DQT marker inside jpeg, for in-place exif creation
    bool             padExif;     // Boolean to control if padding is preferred over copying during in-place exif creation
    bool             encodeAll;   // Boolean to control if both thumbnail and main image shall be encoded. False means just thumbnail.
    const Parameters *params;
};


int camera_jpeg_init();

int camera_jpeg_deinit();

// SW_JPEG_ENCODE_S
int camera_jpeg_encode(const InputBuffer &in, const OutputBuffer &out);
// SW_JPEG_ENCODE_E

int camera_jpeg_make(EncodePackage &package, int &finalSize);

int camera_setupExifWithMetaData(EncodePackage & package, ExifMetaData *metaData);


} // namespace icamera
