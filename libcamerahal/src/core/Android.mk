#
#  Copyright (C) 2014-2018 Intel Corporation
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

LOCAL_SRC_FILES += \
    src/core/BufferQueue.cpp  \
    src/core/CameraDevice.cpp  \
    src/core/ProcessorManager.cpp \
    src/core/RequestThread.cpp  \
    src/core/CameraStream.cpp  \
    src/core/CaptureUnit.cpp  \
    src/core/DeviceBase.cpp  \
    src/core/SwImageProcessor.cpp \
    src/core/CameraEvent.cpp  \
    src/core/CameraBuffer.cpp  \
    src/core/CsiMetaDevice.cpp \
    src/core/SofSource.cpp \
    src/core/SensorHwCtrl.cpp \
    src/core/SyncManager.cpp \
    src/core/LensHw.cpp

# FILE_SOURCE_S
LOCAL_SRC_FILES += \
    src/core/FileSource.cpp
# FILE_SOURCE_E

ifeq ($(USE_BYPASS_MODE),false)
LOCAL_SRC_FILES += \
    src/core/PSysProcessor.cpp \
    src/core/SensorOB.cpp \
    src/core/IspParamAdaptor.cpp \
    src/core/psysprocessor/PSysDAG.cpp \
    src/core/psysprocessor/PipeExecutor.cpp \
    src/core/psysprocessor/PolicyManager.cpp \
    src/core/psysprocessor/PSysPipe.cpp \
    src/core/pgprocessor/PGParamAdapt.cpp \
    src/core/pgprocessor/PGBase.cpp \
    src/core/pgprocessor/Hp4KPreGDC.cpp \
    src/core/pgprocessor/Hp4KPostGDC.cpp \
    src/core/pgprocessor/HpHdrPreGDC.cpp \
    src/core/pgprocessor/HpHdrPostGDC.cpp

# LITE_PROCESSING_S
LOCAL_SRC_FILES += \
    src/core/psyslite/PSysP2pLite.cpp \
    src/core/psyslite/WeavingProcessor.cpp \
    src/core/psyslite/WeavingPipeline.cpp \
    src/core/psyslite/CscProcessor.cpp \
    src/core/psyslite/CscPipeline.cpp \
    src/core/psyslite/ScaleProcessor.cpp \
    src/core/psyslite/ScalePipeline.cpp \
    src/core/psyslite/FisheyeProcessor.cpp \
    src/core/psyslite/FisheyePipeline.cpp \
    src/core/psyslite/PSysPipeBase.cpp
# LITE_PROCESSING_E

LOCAL_C_INCLUDES += \
    $(LIBBXT_PSS_DEBUG_INCLUDES)
endif
