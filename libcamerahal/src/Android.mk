#
#  Copyright (C) 2014-2017 Intel Corporation
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

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/src/core \
    $(LOCAL_PATH)/src/isp_control  \
    $(LOCAL_PATH)/src/metadata  \
    $(LOCAL_PATH)/src/platformdata  \
    $(LOCAL_PATH)/src/platformdata/gc  \
    $(LOCAL_PATH)/src/iutils  \
    $(LOCAL_PATH)/src/v4l2    \
    $(LOCAL_PATH)/src        \
    $(LOCAL_PATH)/src/3a        \
    $(LOCAL_PATH)/include/    \
    $(LOCAL_PATH)/include/api \
    $(TARGET_OUT_HEADERS)/libmfldadvci \
    $(TARGET_OUT_HEADERS)/ia-imaging-control \
    $(TARGET_OUT_HEADERS)/libipu
