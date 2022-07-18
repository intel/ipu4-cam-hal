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
    src/platformdata/CameraProfiles.cpp \
    src/platformdata/PolicyProfiles.cpp \
    src/platformdata/PlatformData.cpp

# CUSTOM_WEIGHT_GRID_S
LOCAL_SRC_FILES += \
    src/platformdata/TunningProfiles.cpp
# CUSTOM_WEIGHT_GRID_E

ifeq ($(USE_BYPASS_MODE),false)
LOCAL_SRC_FILES += \
    src/platformdata/CameraConf.cpp \
    src/platformdata/gc/GraphConfig.cpp \
    src/platformdata/gc/FormatUtils.cpp \
    src/platformdata/gc/GcManagerCore.cpp \
    src/platformdata/gc/GraphConfigManager.cpp
endif
