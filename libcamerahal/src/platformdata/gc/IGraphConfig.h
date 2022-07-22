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
#pragma once

#include <string>
#include "HalStream.h"
#include "Parameters.h"
#include "iutils/CameraLog.h"
#include "iutils/Errors.h"
#include "iutils/Utils.h"

#ifndef BYPASS_MODE
#include "ia_isp_bxt_types.h"
#endif

typedef uint32_t ia_uid;

namespace icamera {

/**
 * Stream id associated with still capture.
 */
static const int32_t STILL_STREAM_ID = 60000;
/**
 * Stream id associated with video stream.
 */
static const int32_t VIDEO_STREAM_ID = 60001;

class IGraphConfig {
public:
    virtual ~IGraphConfig() = default;

     class ConnectionConfig {
     public:
         ConnectionConfig(): mSourceStage(0),
                             mSourceTerminal(0),
                             mSourceIteration(0),
                             mSinkStage(0),
                             mSinkTerminal(0),
                             mSinkIteration(0),
                             mConnectionType(0) {}

         ConnectionConfig(ia_uid sourceStage,
                          ia_uid sourceTerminal,
                          ia_uid sourceIteration,
                          ia_uid sinkStage,
                          ia_uid sinkTerminal,
                          ia_uid sinkIteration,
                          int connectionType):
                             mSourceStage(sourceStage),
                             mSourceTerminal(sourceTerminal),
                             mSourceIteration(sourceIteration),
                             mSinkStage(sinkStage),
                             mSinkTerminal(sinkTerminal),
                             mSinkIteration(sinkIteration),
                             mConnectionType(connectionType) {}
         void dump() {
             LOGE("connection src 0x%x (0x%x) sink 0x%x(0x%x)",
                     mSourceStage, mSourceTerminal,
                     mSinkStage, mSinkTerminal);
         }

         ia_uid mSourceStage;
         ia_uid mSourceTerminal;
         ia_uid mSourceIteration;
         ia_uid mSinkStage;
         ia_uid mSinkTerminal;
         ia_uid mSinkIteration;
         int mConnectionType;
     };
    /**
     * \struct PortFormatSettings
     * Format settings for a port in the graph
     */
     struct PortFormatSettings {
         int32_t      enabled;
         uint32_t     terminalId; /**< Unique terminal id (is a fourcc code) */
         int32_t      width;    /**< Width of the frame in pixels */
         int32_t      height;   /**< Height of the frame in lines */
         int32_t      fourcc;   /**< Frame format */
         int32_t      bpl;      /**< Bytes per line*/
         int32_t      bpp;      /**< Bits per pixel */
     };

    /**
     * \struct PipelineConnection
     * Group port format, connection, stream, edge port for
     * pipeline configuration
     */
     struct PipelineConnection {
         PipelineConnection() : stream(nullptr), hasEdgePort(false) { CLEAR(portFormatSettings); }
         PortFormatSettings portFormatSettings;
         ConnectionConfig connectionConfig;
         HalStream *stream;
         bool hasEdgePort;
     };

    virtual void getCSIOutputResolution(camera_resolution_t &reso) = 0;
    virtual status_t getGdcKernelSetting(uint32_t& kernelId, camera_resolution_t& resolution) = 0;
    virtual status_t graphGetStreamIds(std::vector<int32_t> &streamIds) = 0;
    virtual int getGraphId(void) = 0;
    virtual int getStreamIdByPgName(std::string pgName) = 0;
    virtual int getPgIdByPgName(std::string pgName) = 0;
    virtual int getDolInfo(float &gain, std::string &mode) = 0;
#ifndef BYPASS_MODE
    virtual ia_isp_bxt_program_group *getProgramGroup(int32_t streamId) = 0;
    virtual int getProgramGroup(std::string pgName, ia_isp_bxt_program_group * programGroupForPG) = 0;
#endif
    virtual status_t getPgNames(std::vector<std::string>* pgNames) = 0;
    virtual status_t pipelineGetInternalConnections(std::vector<std::string>& pgList,
                        std::vector<PipelineConnection> &confVector) = 0;

};
}
