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

LOCAL_SRC_FILES += \
    halv3/Camera3HWI.cpp \
    halv3/Camera2Module.cpp \
    halv3/Camera3Channel.cpp \
    halv3/StreamBuffer.cpp

ifeq ($(BOARD_GRAPHIC_IS_GEN),true)
LOCAL_SRC_FILES += halv3/GenGfx.cpp
else ifeq ($(BOARD_USES_MINIGBM),true)
LOCAL_SRC_FILES += halv3/OpenSourceGFX.cpp
endif
LOCAL_SHARED_LIBRARIES += libivp
