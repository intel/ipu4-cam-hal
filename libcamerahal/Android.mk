#  Copyright (C) 2011 The Android Open Source Project
#  Copyright (C) 2016-2018 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


ifeq ($(USE_CAMERA_HAL_SOC),true)
ifeq ($(USE_CAMERA_STUB),false)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_CFLAGS += -fno-short-enums -DHAVE_PTHREADS
LOCAL_CFLAGS += -Wno-unused-parameter -Wno-missing-field-initializers
LOCAL_CLANG_CFLAGS += -Wno-c++11-narrowing
LOCAL_CFLAGS += -DHAVE_ANDROID_OS -DHAVE_IA_TYPES

#For BYPASS_MODE: USE_BYPASS_MODE := true
#if enable pSys: USE_BYPASS_MODE := false
USE_BYPASS_MODE := false
ifeq ($(USE_BYPASS_MODE),true)
LOCAL_CFLAGS += -DBYPASS_MODE
endif

LOCAL_SHARED_LIBRARIES:= \
    libbinder \
    liblog \
    libutils \
    libcutils \
    libcamera_client \
    libui \
    libdl \
    libexpat \
    libhardware

ifeq ($(USE_BYPASS_MODE),false)
#libraries from libmfldadvci
LOCAL_SHARED_LIBRARIES+= \
    libia_aiq \
    libia_cmc_parser \
    libia_exc \
    libia_mkn \
    libia_bcomp \
    libia_nvm \
    libia_dvs \
    libia_ltm \
    libia_ob \
    libia_log
LOCAL_SHARED_LIBRARIES+= \
    libia_camera \
    libia_cipf \
    libia_gcss \
    libia_isp_bxt \
    libia_camera_bxtB0
endif

ifeq ($(BOARD_GRAPHIC_IS_GEN),true)
LOCAL_CFLAGS += -DGRAPHIC_IS_GEN
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/ufo
endif

# JPEG conversion libraries and includes.
LOCAL_SHARED_LIBRARIES += \
	libjpeg \
	libcamera_metadata

LOCAL_C_INCLUDES += external/jpeg \
    frameworks/native/include/media/hardware \
    system/core/include \
    external/expat/lib \
    $(call include-path-for, libhardware) \
    $(call include-path-for, camera) \
    $(LOCAL_PATH)/src/jpeg

ifeq ($(BOARD_USE_HALV3),true)
include $(LOCAL_PATH)/halv3/Android.mk
else
include $(LOCAL_PATH)/halv1/Android.mk
endif

# HAL code that shared with Linux
include $(LOCAL_PATH)/src/Android.mk

LOCAL_MODULE_OWNER := intel
LOCAL_MULTILIB := 32
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := libcamhal_profile.xml
LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_SHARED_LIBRARY)

endif #ifeq ($(USE_CAMERA_STUB),false)
endif #ifeq ($(USE_CAMERA_HAL_SOC),true)
