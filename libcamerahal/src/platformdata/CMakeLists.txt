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

set(PLATFORMDATA_SRCS
    ${PLATFORMDATA_DIR}/PlatformData.cpp
    ${PLATFORMDATA_DIR}/CameraProfiles.cpp
    ${PLATFORMDATA_DIR}/PolicyProfiles.cpp
    CACHE INTERNAL "platformdata sources"
   )
# CUSTOM_WEIGHT_GRID_S
    set(PLATFORMDATA_SRCS
        ${PLATFORMDATA_SRCS}
        ${PLATFORMDATA_DIR}/TunningProfiles.cpp
        CACHE INTERNAL "platformdata sources"
        )
# CUSTOM_WEIGHT_GRID_E

if (NOT BYPASS_MODE)
    set(PLATFORMDATA_SRCS
        ${PLATFORMDATA_SRCS}
        ${PLATFORMDATA_DIR}/CameraConf.cpp
        ${PLATFORMDATA_DIR}/gc/FormatUtils.cpp
        ${PLATFORMDATA_DIR}/gc/GraphUtils.cpp
        CACHE INTERNAL "platformdata sources"
        )
    if (USE_STATIC_GRAPH)
        set(PLATFORMDATA_SRCS
            ${PLATFORMDATA_SRCS}
            ${PLATFORMDATA_DIR}/gc/staticgraph/GraphConfigManager.cpp
            ${PLATFORMDATA_DIR}/gc/staticgraph/GraphConfig.cpp
            CACHE INTERNAL "platformdata sources"
        )
    else()
        set(PLATFORMDATA_SRCS
            ${PLATFORMDATA_SRCS}
            ${PLATFORMDATA_DIR}/gc/GraphConfigManager.cpp
            ${PLATFORMDATA_DIR}/gc/GcManagerCore.cpp
            ${PLATFORMDATA_DIR}/gc/GraphConfig.cpp
            CACHE INTERNAL "platformdata sources"
        )
    endif()
endif() #BYPASS_MODE

