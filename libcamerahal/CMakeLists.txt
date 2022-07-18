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

cmake_minimum_required(VERSION 2.8)

project(libcamhal)

option(USE_STATIC_GRAPH "Use static graph" OFF)

#---------------------------- Fold settings ----------------------------

# Source directories
set(SRC_ROOT_DIR        ${CMAKE_CURRENT_LIST_DIR}/src)

set(3A_DIR              ${SRC_ROOT_DIR}/3a)
set(CORE_DIR            ${SRC_ROOT_DIR}/core)
set(HAL_DIR             ${SRC_ROOT_DIR}/hal)
set(IIO_DIR             ${SRC_ROOT_DIR}/iio)
set(ISP_CTRL_DIR        ${SRC_ROOT_DIR}/isp_control)
set(IUTILS_DIR          ${SRC_ROOT_DIR}/iutils)
set(METADATA_DIR        ${SRC_ROOT_DIR}/metadata)
set(PLATFORMDATA_DIR    ${SRC_ROOT_DIR}/platformdata)
set(V4L2_DIR            ${SRC_ROOT_DIR}/v4l2)
set(IPU_RUNTIME_DIR     ${SRC_ROOT_DIR}/ipu_runtime)

# CMake module directory
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Install directories
# CMAKE_INSTALL_PREFIX is defined when executing cmake and supposed to be ./out/libcamhal
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}" CACHE PATH "Target location for install command." FORCE)
endif()

message(STATUS "install prefix: " ${CMAKE_INSTALL_PREFIX})

#------------------------- Compilation settings -------------------------

# -fPIC in xos way
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-faligned-new COMPILER_SUPPORTS_ALIGNED_NEW)
if (COMPILER_SUPPORTS_ALIGNED_NEW)
    add_definitions(-faligned-new)
endif()

add_compile_options(-std=c++11
                    -Wall -Werror
                    -fstack-protector
                    -fPIE -fPIC
                    -D_FORTIFY_SOURCE=2
                    -Wformat -Wformat-security
                    )

# code coverage.
include(code-coverage)

add_definitions(-D__STDC_FORMAT_MACROS
                -DHAVE_PTHREADS
                -DHAVE_LINUX_OS
                -DHAVE_IA_TYPES
                -DHAVE_PRCTL
                )
if (BYPASS_MODE)
        add_definitions(-DBYPASS_MODE)
endif() #BYPASS_MODE

if (USE_STATIC_GRAPH)
        add_definitions(-DUSE_STATIC_GRAPH)
endif() #USE_STATIC_GRAPH




include_directories(include /usr/include/libiaiq
                    include/api include/utils
                    src
                    src/v4l2 src/core src/metadata
                    src/platformdata src/platformdata/gc
                    src/3a src/3a/intel3a src/3a/external
                    )

set(LIBCAMHAL_LD_FLAGS "-lexpat -fPIE -fPIC -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wl,-z,relro -Wl,-z,now")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LIBCAMHAL_LD_FLAGS}")

add_subdirectory(src)

# Set source files
set(LIBCAMHAL_SRCS
    ${3A_SRCS}
    ${CORE_SRCS}
    ${HAL_SRCS}
    ${IPS_CTRL_SRCS}
    ${IUTILS_SRCS}
    ${METADATA_SRCS}
    ${PLATFORMDATA_SRCS}
    ${V4L2_SRCS}
    )

if (NOT BYPASS_MODE)
    set(LIBCAMHAL_SRCS ${LIBCAMHAL_SRCS} ${IIO_SRCS})
endif() #BYPASS_MODE



# Add libcamhal using the specified sources
add_library(camhal SHARED ${LIBCAMHAL_SRCS})
add_library(camhal_static STATIC ${LIBCAMHAL_SRCS})
set_target_properties(camhal_static PROPERTIES OUTPUT_NAME "camhal")


#---------------------------- Link settings ----------------------------
target_link_libraries(camhal ${CMAKE_DL_LIBS})
target_link_libraries(camhal_static ${CMAKE_DL_LIBS})

# Link libraries, the building will be interrupted if libs/includes not found
if (NOT BYPASS_MODE)

    # Link ia_imaging
    find_package(IA_IMAGING)
    include_directories(${IA_IMAGING_INCLUDE_DIRS})
    target_link_libraries(camhal ${IA_IMAGING_LIBS})
    target_link_libraries(camhal_static ${IA_IMAGING_LIBS})

    # Link libiacss
    find_package(LIBIACSS)
    include_directories(${LIBIACSS_INCLUDE_DIRS})
    target_link_libraries(camhal ${LIBIACSS_LIBS})
    target_link_libraries(camhal_static ${LIBIACSS_LIBS})


    # Include libipu(4) headers
    find_package(LIBIPU)
    include_directories(${LIBIPU_INCLUDE_DIRS})

endif() #BYPASS_MODE


#--------------------------- Install settings ---------------------------

# Install headers
install(DIRECTORY include/ DESTINATION include)

# Install configure files
# By default ipu4 is used
install(DIRECTORY config/ipu4/ DESTINATION share/defaults/etc/camera)

# Install libraries
install(TARGETS camhal camhal_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )


#---------------------------- cpack settings ----------------------------

# The package file name can be specified when executing "cpack", like
#     cpack  -D CPACK_PACKAGE_FILE_NAME="your own name"
# By default, the file name is
#     ${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}.
set(CPACK_PACKAGE_NAME "libcamhal")
set(CPACK_PACKAGE_VERSION "1.0.0")
if (CMAKE_VERSION VERSION_LESS "3.8.2")
    set(CPACK_RPM_USER_BINARY_SPECFILE ${PROJECT_SOURCE_DIR}/cmake/libcamhal.spec.cmakein)
else()
    set(CPACK_RPM_USER_BINARY_SPECFILE ${PROJECT_SOURCE_DIR}/cmake/libcamhal.spec-3.8.cmakein)
endif()

# List of CPack generators to use.
set(CPACK_GENERATOR "RPM")

include(CPack)

