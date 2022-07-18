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

#define LOG_TAG "CameraLog"

#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>

#include "iutils/Utils.h"

#include "CameraLog.h"
#include "CameraDump.h"
#include "Trace.h"

namespace icamera {
void __attribute__ ((constructor)) start_logging(void);
void __attribute__ ((destructor)) end_logging(void);

int gLogLevel = 0;
int gPrintfEnable = 0;
char *gLogModules = nullptr;
int gPerfLevel = 0;
int gEnforceDvs = 0;
bool gIsDumpMediaTopo = false;
bool gIsDumpMediaInfo = false;


void start_logging(void)
{
    openlog("Intel-IPU", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);
}

void end_logging(void)
{
    closelog();
}

static void printLog(const char *module, const int level, const char *fmt, va_list ap)
{
    const char *levelStr = nullptr;
    int priority;

    switch(level) {
        case CAMERA_DEBUG_LOG_LEVEL1:
            levelStr = "LV1";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_LEVEL2:
            levelStr = "LV2";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_REQ_STATE:
            levelStr = "REQ";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_AIQ:
            levelStr = "AIQ";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_XML:
            levelStr = "XML";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_DBG:
            levelStr = "DBG";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_INFO:
            levelStr = "INF";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_ERR:
            levelStr = "ERR";
            priority = LOG_ERR;
        break;
        case CAMERA_DEBUG_LOG_WARNING:
            levelStr = "WAR";
            priority = LOG_WARNING;
        break;
        case CAMERA_DEBUG_LOG_VERBOSE:
            levelStr = "VER";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_VC_SYNC:
            levelStr = "VCSYNC";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_GRAPH:
            levelStr = "GRAPH";
            priority = LOG_DEBUG;
        break;
        case CAMERA_DEBUG_LOG_DEVICE_START:
            levelStr = "DEVSTART";
            priority = LOG_DEBUG;
        break;
        default:
            levelStr = "UKN";
            priority = LOG_DEBUG;
        break;
    }
    char format[1024];
    snprintf(format, sizeof(format), "[%s] [libcamhal]%s: %s", levelStr, module, fmt);

    vsyslog(priority, format, ap);
}

static void getLogTime(char *timeBuf, int bufLen)
{
    // The format of time is: 01-22 15:24:53.071
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t nowtime = tv.tv_sec;
    struct tm* nowtm = localtime(&nowtime);
    if (nowtm) { // If nowtm is nullptr, simply print nothing for time info
        char tmbuf[bufLen];
        CLEAR(tmbuf);
        strftime(tmbuf, bufLen, "%m-%d %H:%M:%S", nowtm);
        snprintf(timeBuf, bufLen, "%s.%03ld", tmbuf, tv.tv_usec/1000);
    }
}

static void printLog(const char *module, const char *level, const char *fmt, va_list ap)
{
    // Add time into beginning of the log.
    const int BUF_LEN = 64;
    char timeBuf[BUF_LEN] = {'\0'};

    getLogTime(timeBuf, BUF_LEN);

    fprintf(stdout, "%s: [%s]: CamHAL_%s:", timeBuf, level, module);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
}

namespace Log {

void setDebugLevel(void)
{
    const char* PROP_CAMERA_HAL_DEBUG = "cameraDebug";
    const char* PROP_CAMERA_HAL_MODULES = "cameraModules";
    const char* PROP_CAMERA_HAL_PERF  = "cameraPerf";
    const char* PROP_CAMERA_HAL_DVS = "cameraDvs";
    const char* PROP_CAMERA_HAL_PRINTFLOG = "cameraPrintfLog";

    // debug
    char *dbgLevel = getenv(PROP_CAMERA_HAL_DEBUG);
    if (dbgLevel) {
        gLogLevel = strtoul(dbgLevel, nullptr, 0);
        LOG1("Debug level is 0x%x", gLogLevel);

        // to enable both LOG1 and LOG2 traces
        if (gLogLevel & CAMERA_DEBUG_LOG_LEVEL2)
            gLogLevel |= CAMERA_DEBUG_LOG_LEVEL1;
    }

    //modules
    gLogModules = getenv(PROP_CAMERA_HAL_MODULES);

    // performance
    char *perfLevel = getenv(PROP_CAMERA_HAL_PERF);
    if (perfLevel) {
        gPerfLevel = strtoul(perfLevel, nullptr, 0);
        LOGD("Performance level is 0x%x", gPerfLevel);

        // bitmask of tracing categories
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_TRACES) {
            LOGD("Perf KPI start/end trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_TRACES_BREAKDOWN) {
            LOGD("Perf KPI breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_IOCTL_BREAKDOWN) {
            LOGD("Perf IOCTL breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_MEMORY) {
            LOGD("Perf memory breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_MEDIA_TOPO_LEVEL) {
            gIsDumpMediaTopo = true;
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_MEDIA_CONTROLLER_LEVEL) {
            gIsDumpMediaInfo = true;
        }
        ScopedAtrace::setTraceLevel(gPerfLevel);
    }

    // File Dump debug level
    CameraDump::setDumpLevel();

    // Enforce DVS for debugging
    char *dvs = getenv(PROP_CAMERA_HAL_DVS);
    if (dvs) {
        gEnforceDvs = strtoul(dvs, nullptr, 0);
        LOGD("EnforceDvs level is 0x%x", gEnforceDvs);
    }

    //enable Syslog
    char *printfLog_enable = getenv(PROP_CAMERA_HAL_PRINTFLOG);
    if (printfLog_enable) {
        gPrintfEnable = strtoul(printfLog_enable, nullptr, 0);
        LOGD("Libcamhal print log in terminal");
    }
}

bool isDebugLevelEnable(int level)
{
    return gLogLevel & level;
}

bool isModulePrintAble(const char *module)
{
    if (gLogModules == nullptr) {
        return true;
    } else if (strstr(gLogModules, module) != nullptr) {
        return true;
    } else {
        return false;
    }
}

bool isDumpMediaTopo(void)
{
    return gIsDumpMediaTopo;
}

bool isDumpMediaInfo(void)
{
    return gIsDumpMediaInfo;
}

void print_log(bool enable, const char *module, const int level, const char *format, ...)
{
    if (!enable && (level != CAMERA_DEBUG_LOG_ERR))
        return;

    if (!isModulePrintAble(module)) {
        return;
    }

    va_list arg;
    va_start(arg, format);
    if (!gPrintfEnable) {
        printLog(module, level, format, arg);
    } else {
        const char *levelStr = nullptr;

        switch(level) {
            case CAMERA_DEBUG_LOG_LEVEL1:
                levelStr = "LV1";
            break;
            case CAMERA_DEBUG_LOG_LEVEL2:
                levelStr = "LV2";
            break;
            case CAMERA_DEBUG_LOG_REQ_STATE:
                levelStr = "REQ";
            break;
            case CAMERA_DEBUG_LOG_AIQ:
                levelStr = "AIQ";
            break;
            case CAMERA_DEBUG_LOG_XML:
                levelStr = "XML";
            break;
            case CAMERA_DEBUG_LOG_DBG:
                levelStr = "DBG";
            break;
            case CAMERA_DEBUG_LOG_INFO:
                levelStr = "INF";
            break;
            case CAMERA_DEBUG_LOG_ERR:
                levelStr = "ERR";
            break;
            case CAMERA_DEBUG_LOG_WARNING:
                levelStr = "WAR";
            break;
            case CAMERA_DEBUG_LOG_VERBOSE:
                levelStr = "VER";
            break;
            case CAMERA_DEBUG_LOG_VC_SYNC:
                levelStr = "VCSYNC";
            break;
            case CAMERA_DEBUG_LOG_GRAPH:
                levelStr = "GRAPH";
            break;
            case CAMERA_DEBUG_LOG_DEVICE_START:
                levelStr = "DEVSTART";
            break;
            default:
                levelStr = "UKN";
            break;
        }

        printLog(module, levelStr, format, arg);

    }
    va_end(arg);
}

void ccaPrintError(const char *fmt, va_list ap)
{
    if (!gPrintfEnable) {
        printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_ERR, fmt, ap);
    } else {
        printLog("CCA_DEBUG", "ERROR", fmt, ap);
    }
}

void ccaPrintInfo(const char *fmt, va_list ap)
{
    if (gLogLevel & CAMERA_DEBUG_LOG_AIQ) {
        if (!gPrintfEnable) {
            printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_DBG, fmt, ap);
        } else {
            printLog("CCA_DEBUG", "INFO", fmt, ap);
        }
    }
}

void ccaPrintDebug(const char *fmt, va_list ap)
{
    if (gLogLevel & CAMERA_DEBUG_LOG_AIQ) {
        if (!gPrintfEnable) {
            printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_DBG, fmt, ap);
        } else {
            printLog("CCA_DEBUG", "DBG", fmt, ap);
        }
    }
}

} // namespace Log

#ifdef HAVE_ANDROID_OS

void __camera_hal_log(bool condition, int prio, const char *tag,
                      const char *fmt, ...)
{
    if (condition) {
        va_list ap;
        va_start(ap, fmt);
        if (gLogLevel & CAMERA_DEBUG_LOG_PERSISTENT) {
            int errnoCopy;
            unsigned int maxTries = 20;
            do {
                errno = 0;
                __android_log_vprint(prio, tag, fmt, ap);
                errnoCopy = errno;
                if (errnoCopy == EAGAIN)
                    usleep(2000); /* sleep 2ms */
            } while(errnoCopy == EAGAIN && maxTries--);
        } else {
            __android_log_vprint(prio, tag, fmt, ap);
        }
    }
}

#endif //HAVE_ANDROID_OS


} //namespace icamera
