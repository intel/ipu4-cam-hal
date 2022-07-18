#
#  Copyright (C) 2018 Intel Corporation
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

include $(call all-subdir-makefiles)

LOCAL_SRC_FILES += \
    aal/Camera3HALModule.cpp \
    aal/Camera3HAL.cpp \
    aal/RequestManager.cpp \
    aal/ResultProcessor.cpp \
    aal/Camera3Stream.cpp \
    aal/MetadataConvert.cpp \
    aal/PostProcessor.cpp \
    aal/IntelAEStateMachine.cpp \
    aal/IntelAFStateMachine.cpp \
    aal/IntelAWBStateMachine.cpp \
    aal/Camera3AMetadata.cpp \
    aal/android/Camera3Buffer.cpp \
    aal/android/Camera3Format.cpp \
    aal/JpegProcessor.cpp

ifeq ($(HAS_IVP_SUPPORT), false)
LOCAL_SHARED_LIBRARIES += \
    libva \
    libva-android
LOCAL_C_INCLUDES += \
    $(TARGET_OUT_HEADERS)/libva
else
LOCAL_CFLAGS += -DENABLE_IVP
endif

ifeq ($(BOARD_GRAPHIC_IS_GEN),true)
LOCAL_SRC_FILES += aal/android/GenGfx.cpp
else ifeq ($(BOARD_USES_MINIGBM),true)
LOCAL_SRC_FILES += aal/android/OpenSourceGFX.cpp
endif

LOCAL_SHARED_LIBRARIES += libivp \
                          libnativewindow

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/linux \
                    $(LOCAL_PATH)/aal/android \
                    $(LOCAL_PATH)/aal \
                    hardware/libhardware/include/hardware
