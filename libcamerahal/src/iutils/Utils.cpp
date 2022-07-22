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

#define LOG_TAG "Utils"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <sstream>

#include "PlatformData.h"
#include "iutils/Utils.h"
#include "iutils/CameraLog.h"
#include "linux/media-bus-format.h"
#include "linux/intel-ipu4-isys.h"

namespace icamera {

int CameraUtils::getFileContent(const char* filename, char* buffer, int maxSize) {

    ifstream stream(filename);

    stream.seekg(0, std::ios::end);
    long copyLength = stream.tellg();
    stream.seekg(0, std::ios::beg);

    if (copyLength > maxSize) {
        copyLength = maxSize;
    }

    stream.read(buffer, copyLength);
    return copyLength;
}

#define GET_FOURCC_FMT(a, b, c, d) ((uint32_t)(d) | ((uint32_t)(c) << 8) \
                                   | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

enum FormatType {
    FORMAT_RAW,
    FORMAT_RAW_VEC,
    FORMAT_YUV,
    FORMAT_YUV_VEC,
    FORMAT_RGB,
    FORMAT_MBUS,
    FORMAT_JPEG,
    FORMAT_FOURCC,
};

struct FormatInfo {
    int pixelCode;
    const char* fullName;
    const char* shortName;
    int bpp;
    FormatType type;
};

static const FormatInfo gFormatMapping[] = {
    { V4L2_PIX_FMT_SBGGR8, "V4L2_PIX_FMT_SBGGR8", "BGGR8", 8, FORMAT_RAW },
    { V4L2_PIX_FMT_SGBRG8, "V4L2_PIX_FMT_SGBRG8", "GBRG8", 8, FORMAT_RAW },
    { V4L2_PIX_FMT_SGRBG8, "V4L2_PIX_FMT_SGRBG8", "GRBG8", 8, FORMAT_RAW },
    { V4L2_PIX_FMT_SRGGB8, "V4L2_PIX_FMT_SRGGB8", "RGGB8", 8, FORMAT_RAW },

    { V4L2_PIX_FMT_SBGGR10, "V4L2_PIX_FMT_SBGGR10", "BGGR10", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SGBRG10, "V4L2_PIX_FMT_SGBRG10", "GBRG10", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SGRBG10, "V4L2_PIX_FMT_SGRBG10", "GRBG10", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SRGGB10, "V4L2_PIX_FMT_SRGGB10", "RGGB10", 16, FORMAT_RAW },

    { V4L2_PIX_FMT_SBGGR12, "V4L2_PIX_FMT_SBGGR12", "BGGR12", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SGBRG12, "V4L2_PIX_FMT_SGBRG12", "GBRG12", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SGRBG12, "V4L2_PIX_FMT_SGRBG12", "GRBG12", 16, FORMAT_RAW },
    { V4L2_PIX_FMT_SRGGB12, "V4L2_PIX_FMT_SRGGB12", "RGGB12", 16, FORMAT_RAW },

    { V4L2_PIX_FMT_SBGGR10P, "V4L2_PIX_FMT_SBGGR10P", "BGGR10P", 10, FORMAT_RAW },
    { V4L2_PIX_FMT_SGBRG10P, "V4L2_PIX_FMT_SGBRG10P", "GBRG10P", 10, FORMAT_RAW },
    { V4L2_PIX_FMT_SGRBG10P, "V4L2_PIX_FMT_SGRBG10P", "GRBG10P", 10, FORMAT_RAW },
    { V4L2_PIX_FMT_SRGGB10P, "V4L2_PIX_FMT_SRGGB10P", "RGGB10P", 10, FORMAT_RAW },

    { V4L2_PIX_FMT_SBGGR8V32, "V4L2_PIX_FMT_SBGGR8V32", "BGGR8V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGBRG8V32, "V4L2_PIX_FMT_SGBRG8V32", "GBRG8V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGRBG8V32, "V4L2_PIX_FMT_SGRBG8V32", "GRBG8V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SRGGB8V32, "V4L2_PIX_FMT_SRGGB8V32", "RGGB8V32", 16, FORMAT_RAW_VEC },

    { V4L2_PIX_FMT_SBGGR10V32, "V4L2_PIX_FMT_SBGGR10V32", "BGGR10V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGBRG10V32, "V4L2_PIX_FMT_SGBRG10V32", "GBRG10V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGRBG10V32, "V4L2_PIX_FMT_SGRBG10V32", "GRBG10V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SRGGB10V32, "V4L2_PIX_FMT_SRGGB10V32", "RGGB10V32", 16, FORMAT_RAW_VEC },

    { V4L2_PIX_FMT_SBGGR12V32, "V4L2_PIX_FMT_SBGGR12V32", "BGGR12V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGBRG12V32, "V4L2_PIX_FMT_SGBRG12V32", "GBRG12V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SRGGB12V32, "V4L2_PIX_FMT_SRGGB12V32", "RGGB12V32", 16, FORMAT_RAW_VEC },
    { V4L2_PIX_FMT_SGRBG12V32, "V4L2_PIX_FMT_SGRBG12V32", "GRBG12V32", 16, FORMAT_RAW_VEC },

    { V4L2_PIX_FMT_NV12, "V4L2_PIX_FMT_NV12", "NV12", 12, FORMAT_YUV },
    { V4L2_PIX_FMT_NV21, "V4L2_PIX_FMT_NV21", "NV21", 12, FORMAT_YUV },
    { V4L2_PIX_FMT_NV16, "V4L2_PIX_FMT_NV16", "NV16", 16, FORMAT_YUV },
    { V4L2_PIX_FMT_YUYV, "V4L2_PIX_FMT_YUYV", "YUYV", 16, FORMAT_YUV },
    { V4L2_PIX_FMT_UYVY, "V4L2_PIX_FMT_UYVY", "UYVY", 16, FORMAT_YUV },

    { V4L2_PIX_FMT_YUV420, "V4L2_PIX_FMT_YUV420", "YUV420", 12, FORMAT_YUV },
    { V4L2_PIX_FMT_YVU420, "V4L2_PIX_FMT_YVU420", "YVU420", 12, FORMAT_YUV },
    { V4L2_PIX_FMT_YUV422P, "V4L2_PIX_FMT_YUV422P", "YUV422P", 16, FORMAT_YUV },

    { V4L2_PIX_FMT_YUYV420_V32, "V4L2_PIX_FMT_YUYV420_V32", "YUYV420V32", 24, FORMAT_YUV_VEC },

    { V4L2_PIX_FMT_P010_BE, "V4L2_PIX_FMT_P010_BE", "P010", 24, FORMAT_YUV },
    { V4L2_PIX_FMT_P010_LE, "V4L2_PIX_FMT_P010_LE", "P01L", 24, FORMAT_YUV },

    { V4L2_PIX_FMT_BGR24, "V4L2_PIX_FMT_BGR24", "BGR24", 24, FORMAT_RGB },
    { V4L2_PIX_FMT_BGR32, "V4L2_PIX_FMT_BGR32", "BGR32", 32, FORMAT_RGB },
    { V4L2_PIX_FMT_RGB24, "V4L2_PIX_FMT_RGB24", "RGB24", 24, FORMAT_RGB },
    { V4L2_PIX_FMT_RGB32, "V4L2_PIX_FMT_RGB32", "RGB32", 32, FORMAT_RGB },
    { V4L2_PIX_FMT_XBGR32, "V4L2_PIX_FMT_XBGR32", "XBGR32", 32, FORMAT_RGB },
    { V4L2_PIX_FMT_XRGB32, "V4L2_PIX_FMT_XRGB32", "XRGB32", 32, FORMAT_RGB },
    { V4L2_PIX_FMT_RGB565, "V4L2_PIX_FMT_RGB565", "RGB565", 16, FORMAT_RGB },

    { V4L2_PIX_FMT_JPEG, "V4L2_PIX_FMT_JPEG", "JPG", 0, FORMAT_JPEG },

    { V4L2_MBUS_FMT_SBGGR12_1X12, "V4L2_MBUS_FMT_SBGGR12_1X12", "SBGGR12_1X12", 12, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGBRG12_1X12, "V4L2_MBUS_FMT_SGBRG12_1X12", "SGBRG12_1X12", 12, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGRBG12_1X12, "V4L2_MBUS_FMT_SGRBG12_1X12", "SGRBG12_1X12", 12, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SRGGB12_1X12, "V4L2_MBUS_FMT_SRGGB12_1X12", "SRGGB12_1X12", 12, FORMAT_MBUS },

    { V4L2_MBUS_FMT_SBGGR10_1X10, "V4L2_MBUS_FMT_SBGGR10_1X10", "SBGGR10_1X10", 10, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGBRG10_1X10, "V4L2_MBUS_FMT_SGBRG10_1X10", "SGBRG10_1X10", 10, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGRBG10_1X10, "V4L2_MBUS_FMT_SGRBG10_1X10", "SGRBG10_1X10", 10, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SRGGB10_1X10, "V4L2_MBUS_FMT_SRGGB10_1X10", "SRGGB10_1X10", 10, FORMAT_MBUS },

    { V4L2_MBUS_FMT_SBGGR8_1X8, "V4L2_MBUS_FMT_SBGGR8_1X8", "SBGGR8_1X8", 8, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGBRG8_1X8, "V4L2_MBUS_FMT_SGBRG8_1X8", "SGBRG8_1X8", 8, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SGRBG8_1X8, "V4L2_MBUS_FMT_SGRBG8_1X8", "SGRBG8_1X8", 8, FORMAT_MBUS },
    { V4L2_MBUS_FMT_SRGGB8_1X8, "V4L2_MBUS_FMT_SRGGB8_1X8", "SRGGB8_1X8", 8, FORMAT_MBUS },

    { V4L2_MBUS_FMT_UYVY8_1X16, "V4L2_MBUS_FMT_UYVY8_1X16", "UYVY8_1X16", 16, FORMAT_MBUS },
    { V4L2_MBUS_FMT_YUYV8_1X16, "V4L2_MBUS_FMT_YUYV8_1X16", "YUYV8_1X16", 16, FORMAT_MBUS },
    { V4L2_MBUS_FMT_UYVY8_2X8, "V4L2_MBUS_FMT_UYVY8_2X8","UYVY8_2X8", 8, FORMAT_MBUS},

    { MEDIA_BUS_FMT_RGB888_1X24, "MEDIA_BUS_FMT_RGB888_1X24", "RGB888_1X24", 0, FORMAT_MBUS },
    { MEDIA_BUS_FMT_RGB565_1X16, "MEDIA_BUS_FMT_RGB565_1X16", "RGB565_1X16", 0, FORMAT_MBUS },
    { MEDIA_BUS_FMT_YUYV12_1X24, "MEDIA_BUS_FMT_YUYV12_1X24", "YUYV12_1X24", 0, FORMAT_MBUS },
    { MEDIA_BUS_FMT_SGRBG10_1X10, "MEDIA_BUS_FMT_SGRBG10_1X10", "SGRBG10_1X10", 0, FORMAT_MBUS },

    { MEDIA_BUS_FMT_RGB888_1X32_PADHI, "MEDIA_BUS_FMT_RGB888_1X32_PADHI", "RGB888_1X32_PADHI", 0, FORMAT_MBUS },

    { V4L2_FMT_INTEL_IPU4_ISYS_META, "V4L2_FMT_INTEL_IPU4_ISYS_META", "META_DATA", 0, FORMAT_MBUS },

    { GET_FOURCC_FMT('y','0','3','2'), "y032", "y032", 24, FORMAT_FOURCC },
    { GET_FOURCC_FMT('N','V','1','2'), "NV12", "NV12", 12, FORMAT_FOURCC },
    { GET_FOURCC_FMT('b','V','0','K'), "bV0K", "bV0K", 16, FORMAT_FOURCC },
    { GET_FOURCC_FMT('b','V','0','G'), "bV0G", "bV0G", 16, FORMAT_FOURCC },
    { GET_FOURCC_FMT('V','4','2','0'), "V420", "V420", 24, FORMAT_FOURCC },
    { GET_FOURCC_FMT('B','A','1','0'), "BA10", "BA10", 16, FORMAT_FOURCC },
    { GET_FOURCC_FMT('B','A','1','2'), "BA12", "BA12", 16, FORMAT_FOURCC },
    { GET_FOURCC_FMT('G','R','1','0'), "GR10", "GR10", 16, FORMAT_FOURCC },
};

const char *CameraUtils::pixelCode2String(int code)
{
    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (gFormatMapping[i].pixelCode == code) {
            return gFormatMapping[i].fullName;
        }
    }

    LOGE("Invalid Pixel Format: %d", code);
    return "INVALID FORMAT";
}

int CameraUtils::string2PixelCode(const char *code)
{
    Check(code == nullptr, -1, "Invalid null pixel format.");

    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (strcmp(gFormatMapping[i].fullName, code) == 0) {
            return gFormatMapping[i].pixelCode;
        }
    }

    LOGE("Invalid Pixel Format: %s", code);
    return -1;
}

const string CameraUtils::fourcc2String(int format4cc)
{
    char fourccBuf[5];
    CLEAR(fourccBuf);
    snprintf(fourccBuf, sizeof(fourccBuf), "%c%c%c%c", (format4cc >> 24) & 0xff,
            (format4cc >> 16) & 0xff, (format4cc >> 8) & 0xff, format4cc & 0xff);

    return string(fourccBuf);
}

const char *CameraUtils::format2string(int format)
{
    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (gFormatMapping[i].pixelCode == format) {
            return gFormatMapping[i].shortName;
        }
    }

    LOGW("Not in our format list :%x", format);
    return fourcc2String(format).c_str();
}

unsigned int CameraUtils::fourcc2UL(char *str4cc)
{
    Check(str4cc == nullptr, 0, "Invalid null string.");
    Check(strlen(str4cc) != 4, 0, "Invalid string %s, should be 4cc.", str4cc);

    return FOURCC_TO_UL(str4cc[0], str4cc[1], str4cc[2], str4cc[3]);
}

bool CameraUtils::isPlanarFormat(int format)
{
    return (format == V4L2_PIX_FMT_NV12 || format == V4L2_PIX_FMT_NV21
         || format == V4L2_PIX_FMT_YUV420 || format == V4L2_PIX_FMT_YVU420
         || format == V4L2_PIX_FMT_YUV422P || format == V4L2_PIX_FMT_NV16);
}

bool CameraUtils::isRaw(int format)
{
    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (gFormatMapping[i].pixelCode == format) {
            // Both normal raw and vector raw treated as raw here.
            return gFormatMapping[i].type == FORMAT_RAW_VEC || gFormatMapping[i].type == FORMAT_RAW;
        }
    }

    return false;
}

bool CameraUtils::isVectorRaw(int format)
{
    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (gFormatMapping[i].pixelCode == format) {
            return gFormatMapping[i].type == FORMAT_RAW_VEC;
        }
    }

    return false;
}

int CameraUtils::getBpp(int format)
{
    int size = ARRAY_SIZE(gFormatMapping);
    for (int i = 0; i < size; i++) {
        if (gFormatMapping[i].pixelCode == format) {
            return gFormatMapping[i].bpp;
        }
    }

    LOGE("There is no bpp supplied for format %s", pixelCode2String(format));
    return -1;
}

/**
 * Get the stride which is also known as aligned bype per line in some context.
 * Mainly used for locate the start of next line.
 */
int CameraUtils::getStride(int format, int width)
{
    int bpl = width * getBpp(format) / 8;
    if (isPlanarFormat(format)) {
        bpl = width;
    }
    return ALIGN_64(bpl);
}

/*
 * Calc Isys output buffer size.
 *
 *  Why alignment is 64?
 *  The IPU DMA unit must transimit at leat 64 bytes one time.
 *
 *  Why need extra size? It's due to a hardware issue: the DMA unit is a power of
 *  two, and a line should be transferred as few units as possible.
 *  The result is that up to line length more data than the image size
 *  may be transferred to memory after the image.
 *
 *  Another limition is the GDA(Global Dynamic Allocator) allocation unit size(1024). For low
 *  resolution it gives a bigger number. Use larger one to avoid
 *  memory corruption.
 *  for example: 320x480 UVVY, which bpl is 640, less than 1024, in this case, driver will
 *  allocate 1024 bytes for the last line.
 */
int CameraUtils::getFrameSize(int format, int width, int height, bool needAlignedHeight, bool needExtraSize)
{
    LOG1("@%s get buffer size for %s %dx%d", __func__, pixelCode2String(format), width, height);

    int alignedBpl = getStride(format, width);

    // Get frame size with aligned height taking in count for internal buffers.
    // To garantee PSYS kernel like GDC always get enough buffer size to process.
    // This is to satisfy the PSYS kernel, like GDC, input alignment requirement.
    if (needAlignedHeight) {
        height = ALIGN_64(height);
        LOG1("@%s buffer aligned height %d", __func__, height);
    }
    int bufferHeight = isPlanarFormat(format) ? (height * getBpp(format) / 8) : height;

    if (!needExtraSize) {
        LOG1("%s: no need extra size, frame size is %d", __func__, alignedBpl * bufferHeight);
        return alignedBpl * bufferHeight;
    }

    // Extra size should be at least one alignedBpl
    int extraSize = isPlanarFormat(format) ? alignedBpl * getBpp(format) / 8 : alignedBpl;
    extraSize = std::max(extraSize , 1024);

    return alignedBpl * bufferHeight + extraSize;
}

int CameraUtils::getNumOfPlanes(int format)
{
    switch(format) {
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_SGRBG8:
        case V4L2_FMT_INTEL_IPU4_ISYS_META:
            return 1;
        case V4L2_FMT_INTEL_IPU4_ISA_CFG:
            return 2;
        //Add more when needed...
        default:
            return 1;
    }
}

void CameraUtils::getDeviceName(const char* entityName, string& deviceNodeName, bool isSubDev)
{

    const char *filePrefix = "video";
    const char *dirPath = "/sys/class/video4linux/";
    if (isSubDev)
        filePrefix = "v4l-subdev";

    DIR *dp = opendir(dirPath);
    Check((dp == nullptr), VOID_VALUE, "@%s, Fail open : %s", __func__, dirPath);

    struct dirent *dirp = nullptr;
    while ((dirp = readdir(dp)) != nullptr) {
        if ((dirp->d_type == DT_LNK) && (strncmp(dirp->d_name, filePrefix, strlen(filePrefix)) == 0)) {
            string subDeviceName = dirPath;
            subDeviceName += dirp->d_name;
            subDeviceName += "/name";
            int fd = open(subDeviceName.c_str(), O_RDONLY);
            Check((fd < 0), VOID_VALUE, "@%s, open file %s failed. err: %s",
                  __func__, subDeviceName.c_str(), strerror(errno));

            char buf[128] = {'\0'};
            int len = read(fd, buf, sizeof(buf));
            close(fd);
            len--; // remove "\n"
            if (len == (int)strlen(entityName) && memcmp(buf, entityName, len) == 0) {
                deviceNodeName = "/dev/";
                deviceNodeName += dirp->d_name;
                break;
            }
        }
    }
    closedir(dp);
}

void CameraUtils::getSubDeviceName(const char* entityName, string& deviceNodeName)
{
     getDeviceName(entityName, deviceNodeName, true);
}

int CameraUtils::getInterlaceHeight(int field, int height)
{
    if (SINGLE_FIELD(field))
        return height/2;
    else
        return height;
}

bool CameraUtils::isHdrPsysPipe(TuningMode tuningMode)
{
    return (tuningMode == TUNING_MODE_VIDEO_HDR ||
            tuningMode == TUNING_MODE_VIDEO_HDR2 ||
            tuningMode == TUNING_MODE_VIDEO_HLC);
}

bool CameraUtils::isUllPsysPipe(TuningMode tuningMode)
{
    return (tuningMode == TUNING_MODE_VIDEO_ULL ||
            tuningMode == TUNING_MODE_VIDEO_CUSTOM_AIC);
}

ConfigMode CameraUtils::getConfigModeByName(const char* ConfigName)
{
    ConfigMode configMode = CAMERA_STREAM_CONFIGURATION_MODE_END;

    if (ConfigName == nullptr) {
        LOGE("%s, the ConfigName is nullptr", __func__);
    } else if (strcmp(ConfigName, "AUTO") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_AUTO;
    } else if (strcmp(ConfigName, "HDR") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_HDR;
    } else if (strcmp(ConfigName, "ULL") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_ULL;
    } else if (strcmp(ConfigName, "HLC") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_HLC;
    } else if (strcmp(ConfigName, "NORMAL") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_NORMAL;
    } else if (strcmp(ConfigName, "HIGH_SPEED") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_CONSTRAINED_HIGH_SPEED;
    } else if (strcmp(ConfigName, "CUSTOM_AIC") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_CUSTOM_AIC;
    } else if (strcmp(ConfigName, "VIDEO_LL") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_VIDEO_LL;
    } else if (strcmp(ConfigName, "STILL_CAPTURE") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_STILL_CAPTURE;
    } else if (strcmp(ConfigName, "HDR2") == 0) {
        configMode = CAMERA_STREAM_CONFIGURATION_MODE_HDR2;
    } else if (strcmp(ConfigName, "NONE") == 0) {
        LOG1("%s, the detected internal 'NONE' ConfigName", __func__);
    } else {
        LOGE("%s, the ConfigName %s is not supported", __func__, ConfigName);
    }

    LOG2("%s, configMode = %d", __func__, configMode);
    return configMode;
}

void CameraUtils::getConfigModeFromString(string str, vector<ConfigMode> &cfgModes)
{
    bool split = true;
    ConfigMode mode;
    string resultStr, modeStr = str;

    while(split) {
        size_t pos = 0;
        if ((pos = modeStr.find(",")) == string::npos) {
            mode = getConfigModeByName(modeStr.c_str());
            split = false;
        } else {
            resultStr = modeStr.substr(0, pos);
            modeStr = modeStr.substr(pos + 1);
            mode = getConfigModeByName(resultStr.c_str());
        }
        cfgModes.push_back(mode);
    }
}

ConfigMode CameraUtils::getConfigModeBySceneMode(camera_scene_mode_t sceneMode)
{
    ConfigMode configMode = CAMERA_STREAM_CONFIGURATION_MODE_END;

    switch(sceneMode) {
        case SCENE_MODE_NORMAL:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_NORMAL;
            break;
        case SCENE_MODE_ULL:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_ULL;
            break;
        case SCENE_MODE_HDR:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_HDR;
            break;
        case SCENE_MODE_HLC:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_HLC;
            break;
        case SCENE_MODE_CUSTOM_AIC:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_CUSTOM_AIC;
            break;
        case SCENE_MODE_VIDEO_LL:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_VIDEO_LL;
            break;
        case SCENE_MODE_HDR2:
            configMode = CAMERA_STREAM_CONFIGURATION_MODE_HDR2;
            break;
        default:
            // There is no corresponding ConfigMode for some scene.
            LOG2("there is no corresponding ConfigMode for scene %d", sceneMode);
            break;
    }
    return configMode;
}

camera_scene_mode_t CameraUtils::getSceneModeByName(const char* sceneName)
{
    if (sceneName == nullptr) return SCENE_MODE_MAX;
    else if (strcmp(sceneName, "AUTO") == 0) return SCENE_MODE_AUTO;
    else if (strcmp(sceneName, "HDR") == 0) return SCENE_MODE_HDR;
    else if (strcmp(sceneName, "ULL") == 0) return SCENE_MODE_ULL;
    else if (strcmp(sceneName, "HLC") == 0) return SCENE_MODE_HLC;
    else if (strcmp(sceneName, "VIDEO_LL") == 0) return SCENE_MODE_VIDEO_LL;
    else if (strcmp(sceneName, "NORMAL") == 0) return SCENE_MODE_NORMAL;
    else if (strcmp(sceneName, "CUSTOM_AIC") == 0) return SCENE_MODE_CUSTOM_AIC;
    else if (strcmp(sceneName, "HDR2") == 0) return SCENE_MODE_HDR2;

    return SCENE_MODE_MAX;
}

camera_awb_mode_t CameraUtils::getAwbModeByName(const char* awbName)
{
    if (awbName == nullptr) return AWB_MODE_MAX;
    else if (strcmp(awbName, "AUTO") == 0) return AWB_MODE_AUTO;
    else if (strcmp(awbName, "INCANDESCENT") == 0) return AWB_MODE_INCANDESCENT;
    else if (strcmp(awbName, "FLUORESCENT") == 0) return AWB_MODE_FLUORESCENT;
    else if (strcmp(awbName, "DAYLIGHT") == 0) return AWB_MODE_DAYLIGHT;
    else if (strcmp(awbName, "FULL_OVERCAST") == 0) return AWB_MODE_FULL_OVERCAST;
    else if (strcmp(awbName, "PARTLY_OVERCAST") == 0) return AWB_MODE_PARTLY_OVERCAST;
    else if (strcmp(awbName, "SUNSET") == 0) return AWB_MODE_SUNSET;
    else if (strcmp(awbName, "VIDEO_CONFERENCE") == 0) return AWB_MODE_VIDEO_CONFERENCE;
    else if (strcmp(awbName, "MANUAL_CCT_RANGE") == 0) return AWB_MODE_MANUAL_CCT_RANGE;
    else if (strcmp(awbName, "MANUAL_WHITE_POINT") == 0) return AWB_MODE_MANUAL_WHITE_POINT;
    else if (strcmp(awbName, "MANUAL_GAIN") == 0) return AWB_MODE_MANUAL_GAIN;
    else if (strcmp(awbName, "MANUAL_COLOR_TRANSFORM") == 0) return AWB_MODE_MANUAL_COLOR_TRANSFORM;

    return AWB_MODE_MAX;
}

int CameraUtils::findXmlId(const char *sensorCfgName)
{
    int camNum = PlatformData::numberOfCameras();
    for (int cameraId = 0; cameraId < camNum; cameraId++) {
        if (!strcmp(PlatformData::getSensorName(cameraId), sensorCfgName))
            return cameraId;
    }
    return -1;
}

unsigned int CameraUtils::getMBusFormat(int cameraId, unsigned int pixelCode)
{
    int outputFmt = PlatformData::getISysFormat(cameraId);

    if (pixelCode == 0) {
        switch (outputFmt) {
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_NV16:
            pixelCode = V4L2_MBUS_FMT_UYVY8_1X16;
            break;
        case V4L2_PIX_FMT_YUYV:
            pixelCode = V4L2_MBUS_FMT_YUYV8_1X16;
            break;
        case V4L2_PIX_FMT_BGR24:
        case V4L2_PIX_FMT_XBGR32:
            pixelCode = MEDIA_BUS_FMT_RGB888_1X24;
            break;
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_XRGB32:
            pixelCode = MEDIA_BUS_FMT_RGB565_1X16;
            break;
        case V4L2_PIX_FMT_SGRBG8:
        case V4L2_PIX_FMT_SGRBG8V32:
            pixelCode = V4L2_MBUS_FMT_SGRBG8_1X8;
            break;
        default:
            LOGE("No input format to match the output: %s", pixelCode2String(outputFmt));
            break;
        }
    }

    return pixelCode;

}

// TODO: This should be combined with PlatformData::getISysBestResolution
int CameraUtils::getBestISysResolution(int cameraId, int field, int previewWidth,
                                                int previewHeight, camera_resolution_t &resolution)
{
    LOG1("@%s, camera Id:%d, field:%d", __func__, cameraId, field);
    vector <camera_resolution_t> res;
    //The supported resolutions are saved in res with ascending order(small -> bigger)
    PlatformData::getSupportedISysSizes(cameraId, res);

    for (auto const& size : res) {
        if ((previewWidth == size.width) && (previewHeight == size.height)) {
            resolution.width = size.width;
            resolution.height = size.height;
            LOG1("@%s: Find the best ISYS resolution(%d)x(%d)",  __func__, resolution.width, resolution.height);
            return 0;
        }
    }

    // Get the biggest one in the supported list
    resolution.width = res.back().width;
    resolution.height = res.back().height;
    LOG1("@%s: Use the biggest ISYS resolution(%d)x(%d)",  __func__, resolution.width, resolution.height);
    return 0;

}

void* CameraUtils::dlopenLibrary(const char* name, int flags)
{
    Check((name == nullptr), nullptr, "%s, invalid parameters", __func__);

    void* handle = dlopen(name, flags);

    const char* lError = dlerror();
    if (lError) {
        if (handle == nullptr) {
            LOGW("%s, handle is NULL", __func__);
        }
        LOGW("%s, dlopen Error: %s", __func__, lError);
        return nullptr;
    }

    LOG1("%s, handle %p, name %s has been opened", __func__, handle, name);
    return handle;
}

void* CameraUtils::dlsymLibrary(void* handle, const char* str)
{
    Check((handle == nullptr || str == nullptr), nullptr, "%s, invalid parameters", __func__);

    void* sym = dlsym(handle, str);

    const char* lError = dlerror();
    if (lError) {
        if (sym == nullptr) {
            LOGW("%s, symbol is nullptr", __func__);
        }
        LOGW("%s, dlopen Error: %s", __func__, lError);
        return nullptr;
    }

    LOG1("%s, handle %p, str %s has been found", __func__, handle, str);
    return sym;
}

int CameraUtils::dlcloseLibrary(void* handle)
{
    Check((handle == nullptr), BAD_VALUE, "%s, invalid parameters", __func__);

    dlclose(handle);
    LOG1("%s, handle %p has been closed", __func__, handle);
    return OK;
}

vector<string> CameraUtils::splitString(const char* srcStr, char delim)
{
    vector<string> tokens;
    stringstream ss(srcStr);
    string item;

    for (size_t i = 0; std::getline(ss, item, delim); i++) {
        tokens.push_back(item);
    }

    return tokens;
}

nsecs_t CameraUtils::systemTime()
{
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return nsecs_t(t.tv_sec)*1000000000LL + t.tv_nsec;
}

} //namespace icamera
