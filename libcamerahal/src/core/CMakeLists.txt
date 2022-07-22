#
#  Copyright (C) 2017-2018 Intel Corporation
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

set(CORE_SRCS
    ${CORE_DIR}/CaptureUnit.cpp
    ${CORE_DIR}/DeviceBase.cpp
    ${CORE_DIR}/CameraStream.cpp
    ${CORE_DIR}/CameraDevice.cpp
    ${CORE_DIR}/ProcessorManager.cpp
    ${CORE_DIR}/RequestThread.cpp
    ${CORE_DIR}/SwImageProcessor.cpp
    ${CORE_DIR}/BufferQueue.cpp
    ${CORE_DIR}/CameraBuffer.cpp
    ${CORE_DIR}/CameraEvent.cpp
    ${CORE_DIR}/LensHw.cpp
    ${CORE_DIR}/SensorHwCtrl.cpp
    ${CORE_DIR}/SofSource.cpp
    ${CORE_DIR}/CsiMetaDevice.cpp
    ${CORE_DIR}/SyncManager.cpp
    CACHE INTERNAL "core sources"
    )

# FILE_SOURCE_S
    set(CORE_SRCS
        ${CORE_SRCS}
        ${CORE_DIR}/FileSource.cpp
        CACHE INTERNAL "core sources"
        )
# FILE_SOURCE_E

if (NOT BYPASS_MODE)

    set(CORE_SRCS
        ${CORE_SRCS}
        ${CORE_DIR}/SensorOB.cpp
        ${CORE_DIR}/IspParamAdaptor.cpp
        ${CORE_DIR}/psysprocessor/PolicyManager.cpp
        CACHE INTERNAL "core sources"
        )

# IPU_PROCESSING_S
    set(CORE_SRCS
        ${CORE_SRCS}
        ${CORE_DIR}/PSysProcessor.cpp
        ${CORE_DIR}/SensorOB.cpp
        ${CORE_DIR}/IspParamAdaptor.cpp
        ${CORE_DIR}/psysprocessor/PSysDAG.cpp
        ${CORE_DIR}/psysprocessor/PipeExecutor.cpp
        ${CORE_DIR}/psysprocessor/PolicyManager.cpp
        ${CORE_DIR}/psysprocessor/PSysPipe.cpp
        ${CORE_DIR}/pgprocessor/PGParamAdapt.cpp
        ${CORE_DIR}/pgprocessor/PGBase.cpp
        ${CORE_DIR}/pgprocessor/Hp4KPreGDC.cpp
        ${CORE_DIR}/pgprocessor/Hp4KPostGDC.cpp
        ${CORE_DIR}/pgprocessor/HpHdrPreGDC.cpp
        ${CORE_DIR}/pgprocessor/HpHdrPostGDC.cpp
        CACHE INTERNAL "core sources"
       )

# LITE_PROCESSING_S
    set(CORE_SRCS
        ${CORE_SRCS}
        ${CORE_DIR}/psyslite/WeavingProcessor.cpp
        ${CORE_DIR}/psyslite/PSysPipeBase.cpp
        ${CORE_DIR}/psyslite/WeavingPipeline.cpp
        ${CORE_DIR}/psyslite/CscProcessor.cpp
        ${CORE_DIR}/psyslite/CscPipeline.cpp
        ${CORE_DIR}/psyslite/ScaleProcessor.cpp
        ${CORE_DIR}/psyslite/ScalePipeline.cpp
        ${CORE_DIR}/psyslite/FisheyeProcessor.cpp
        ${CORE_DIR}/psyslite/FisheyePipeline.cpp
        ${CORE_DIR}/psyslite/PSysP2pLite.cpp
        CACHE INTERNAL "core sources"
        )
# LITE_PROCESSING_E
# IPU_PROCESSING_E

endif() #BYPASS_MODE


