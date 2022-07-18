#
#  Copyright (C) 2017 Intel Corporation
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

set(V4L2_SRCS
    ${V4L2_DIR}/SysCall.cpp
    ${V4L2_DIR}/MediaControl.cpp
    ${V4L2_DIR}/V4l2DevBase.cpp
    ${V4L2_DIR}/V4l2Dev.cpp
    ${V4L2_DIR}/V4l2Buffer.cpp
    ${V4L2_DIR}/V4l2SubDev.cpp
    ${V4L2_DIR}/V4l2DeviceFactory.cpp
    CACHE INTERNAL "v4l2 sources"
    )

