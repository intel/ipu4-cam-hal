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

#include <linux/videodev2.h>
#include <string.h>
#include <vector>
#include <string>

#include "CameraTypes.h"

namespace icamera {

typedef int64_t nsecs_t;

#define DAG_VIDEO_PRE_POST_GDC "video-pipe-full-dss.2600.B0.ff.fw"
#define DAG_VIDEO_PRE_POST_GDC_HP "video-pipe-normal-hp"
#define DAG_VIDEO_ISA_PRE_POST_GDC "video_isa_video_full"
#define DAG_STILL_PRE_POST_GDC "still.pre-post-gdc-combined.ff"
#define DAG_VIDEO_HDR "video_hdr_hqns"
#define DAG_VIDEO_HDR_GDC "video_hdr_hqns_gdc"
#define DAG_VIDEO_ULL "video_ull_hqns"
#define DAG_VIDEO_PRE_GDC_LL "video_pre_gdc_ll"
#define DAG_VIDEO_HP_HDR "video_hdr_hp"

#define ALIGN(val, alignment) (((val)+(alignment)-1) & ~((alignment)-1))
#define ALIGN_64(val) ALIGN(val, 64)
#define ALIGN_32(val) ALIGN(val, 32)
#define ALIGN_16(val) ALIGN(val, 16)
#define ALIGN_8(val)  ALIGN(val, 8)

#define ARRAY_SIZE(array)    (sizeof(array) / sizeof((array)[0]))

#define CLEAR(x) memset (&(x), 0, sizeof (x))

// macro CLIP is used to clip the Number value to between the Min and Max
#define CLIP(Number, Max, Min)    ((Number) > (Max) ? (Max) : ((Number) < (Min) ? (Min) : (Number)))

#ifndef UNUSED
#define UNUSED(param) (void)(param)
#endif

#define SINGLE_FIELD(field) ((field == V4L2_FIELD_TOP) || (field == V4L2_FIELD_BOTTOM) || \
                             (field == V4L2_FIELD_ALTERNATE))
/**
 * Use to check input parameter and if failed, return err_code and print error message
 */
#define VOID_VALUE
#define Check(condition, err_code, err_msg, args...) \
            do { \
                if (condition) { \
                    LOGE(err_msg, ##args);\
                    return err_code;\
                }\
            } while (0)

#define CheckAndClean(condition, err_code, clean, err_msg, args...) \
            do { \
                if (condition) { \
                    LOGE(err_msg, ##args);\
                    clean;\
                    return err_code;\
                }\
            } while (0)

/**
 * Use to check input parameter and if failed, return err_code and print warning message,
 * this should be used for non-vital error checking.
 */
#define CheckWarning(condition, err_code, err_msg, args...) \
            do { \
                if (condition) { \
                    LOGW(err_msg, ##args);\
                    return err_code;\
                }\
            } while (0)

// As above but no return.
#define CheckWarningNoReturn(condition, err_msg, args...) \
                            do { \
                                if (condition) { \
                                    LOGW(err_msg, ##args);\
                                }\
                            } while (0)

// macro delete array and set it to null
#define DELETE_ARRAY_AND_NULLIFY(var) \
    do { \
        delete[] (var); \
        var = nullptr; \
    } while(0)

/**
 *  \macro TIMEVAL2USECS
 *  Convert timeval struct to value in microseconds
 *  Helper macro to convert timeval struct to microsecond values stored in a
 *  long long signed value (equivalent to int64_t)
 */
#define TIMEVAL2USECS(x) (int64_t)(((x).tv_sec*1000000000LL + \
                                    (x).tv_usec*1000LL)/1000LL)

// macro for float comparaion with 0
#define EPSILON 0.00001

// macro for the MAX FILENAME
#define MAX_SYS_NAME 64
#define MAX_TARGET_NAME 256

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private:declarations for a class
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&) = delete; \
    TypeName& operator=(const TypeName&) = delete
#endif

/**
 *  Maker note maximum sizes
 *  Section 1 is used for Normal capture
 *  Section 2 is used for RAW captures
 */
#define MAKERNOTE_SECTION1_SIZE 56000
#define MAKERNOTE_SECTION2_SIZE 110592

// macro for memcpy
#ifndef MEMCPY_S
#define MEMCPY_S(dest, dmax, src, smax) memcpy((dest), (src), std::min((size_t)(dmax), (size_t)(smax)))
#endif

#define FOURCC_TO_UL(a,b,c,d) \
    ((uint32_t)(a) | ((uint32_t)(b)<<8) | ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))

//Internal useful tool for format
namespace CameraUtils {

    int getFileContent(const char* filename, char* buffer, int maxSize);

    const char *pixelCode2String(int code);

    int string2PixelCode(const char *code);

    const char *format2string (int format);

    int string2format(const char *str);

    const string fourcc2String(int format4cc);

    unsigned int fourcc2UL(char *str4cc);

    bool isPlanarFormat(int format);

    bool isRaw(int format);

    bool isVectorRaw(int format);

    int getBpp(int format);

    int getStride (int format, int width);

    int getFrameSize(int format, int width, int height, bool needAlignedHeight = false, bool needExtraSize = true);

    int getNumOfPlanes(int format);

    void getDeviceName(const char* entityName, string& deviceNodeName, bool isSubDev = false);

    void getSubDeviceName(const char* entityName, string& deviceNodeName);

    int getInterlaceHeight(int field, int height);

    bool isHdrPsysPipe(TuningMode tuningMode);

    bool isUllPsysPipe(TuningMode tuningMode);

    ConfigMode getConfigModeByName(const char* ConfigName);

    ConfigMode getConfigModeBySceneMode(camera_scene_mode_t sceneMode);

    void getConfigModeFromString(string str, vector<ConfigMode> &cfgModes);

    camera_scene_mode_t getSceneModeByName(const char* sceneName);

    camera_awb_mode_t getAwbModeByName(const char* awbName);

    int findXmlId(const char *sensorCfgName);
    unsigned int getMBusFormat(int cameraId, unsigned int pixelCode);

    int getBestISysResolution(int cameraId,int field,int previewWidth,int previewHeight,camera_resolution_t & resolution);

    void* dlopenLibrary(const char* name, int flags);
    void* dlsymLibrary(void* handle, const char* str);
    int dlcloseLibrary(void* handle);

    /**
     * Spit the given srcStr by delim into a vector of sub strings.
     */
    vector<string> splitString(const char* srcStr, char delim);

    nsecs_t systemTime();
}

} //namespace icamera
