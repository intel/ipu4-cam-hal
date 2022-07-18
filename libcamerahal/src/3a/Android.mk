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
    src/3a/I3AControlFactory.cpp \
    src/3a/IntelMkn.cpp

ifeq ($(USE_BYPASS_MODE),false)
LOCAL_SRC_FILES += \
    src/3a/AiqUtils.cpp \
    src/3a/intel3a/Intel3A.cpp \
    src/3a/intel3a/Intel3AParameter.cpp \
    src/3a/intel3a/Intel3AResult.cpp \
    src/3a/AiqResult.cpp \
    src/3a/AiqResultStorage.cpp \
    src/3a/AiqStatistics.cpp \
    src/3a/AiqPlus.cpp \
    src/3a/SensorManager.cpp \
    src/3a/LensManager.cpp \
    src/3a/AiqCore.cpp \
    src/3a/AiqEngine.cpp \
    src/3a/AiqSetting.cpp \
    src/3a/AiqUnit.cpp

# INTEL_DVS_S
LOCAL_SRC_FILES += src/3a/IntelDvs.cpp
# INTEL_DVS_E
# LOCAL_TONEMAP_S
LOCAL_SRC_FILES += src/3a/Ltm.cpp
# LOCAL_TONEMAP_E
# CUSTOMIZED_3A_S
LOCAL_SRC_FILES += \
    src/3a/external/Customized3A.cpp \
    src/3a/external/CustomizedAic.cpp
# CUSTOMIZED_3A_E
endif

