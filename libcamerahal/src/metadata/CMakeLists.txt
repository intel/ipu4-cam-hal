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

set(METADATA_SRCS
    ${METADATA_DIR}/icamera_metadata_base.cpp
    ${METADATA_DIR}/CameraMetadata.cpp
    ${METADATA_DIR}/Parameters.cpp
    ${METADATA_DIR}/ParameterHelper.cpp
    CACHE INTERNAL "metadata sources"
    )

if (NOT BYPASS_MODE)
    set(METADATA_SRCS
        ${METADATA_SRCS}
        ${METADATA_DIR}/ParameterGenerator.cpp
        CACHE INTERNAL "metadata sources"
        )
endif() #BYPASS_MODE
