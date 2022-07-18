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

AUTOMAKE_OPTIONS=foreign
lib_LTLIBRARIES = libcamhal.la

apiincludedir = $(pkgincludedir)/api
linuxincludedir = $(pkgincludedir)/linux
utilsincludedir = $(pkgincludedir)/utils
apiinclude_HEADERS = include/api/*.h
linuxinclude_HEADERS = include/linux/*.h
utilsinclude_HEADERS = include/utils/*.h

pkgconfdir = $(sysconfdir)/camera

if IPU_VERSION_SET
pkgconf_DATA = config/$(IPU_VERSION)/*.xml
else
pkgconf_DATA = config/ipu4/*.xml
endif

libcamhal_la_SOURCES = \
    src/hal/CameraHal.cpp\
    src/hal/ICamera.cpp \
    src/core/CaptureUnit.cpp \
    src/core/DeviceBase.cpp \
    src/core/CameraStream.cpp \
    src/core/CameraDevice.cpp \
    src/core/RequestThread.cpp \
    src/core/ProcessorManager.cpp \
    src/core/SwImageProcessor.cpp \
    src/core/BufferQueue.cpp \
    src/core/CameraBuffer.cpp \
    src/core/CameraEvent.cpp \
    src/core/LensHw.cpp \
    src/core/SensorHwCtrl.cpp \
    src/core/SyncManager.cpp \
    src/iutils/CameraLog.cpp \
    src/iutils/CameraDump.cpp \
    src/iutils/Trace.cpp \
    src/iutils/ScopedAtrace.cpp \
    src/iutils/Thread.cpp \
    src/iutils/Utils.cpp \
    src/iutils/SwImageConverter.cpp \
    src/iutils/CameraShm.cpp \
    src/platformdata/PlatformData.cpp \
    src/platformdata/CameraProfiles.cpp \
    src/platformdata/PolicyProfiles.cpp \
    src/metadata/icamera_metadata_base.cpp \
    src/metadata/CameraMetadata.cpp \
    src/metadata/Parameters.cpp \
    src/metadata/ParameterHelper.cpp \
    src/isp_control/IspControlUtils.cpp \
    src/v4l2/SysCall.cpp \
    src/v4l2/MediaControl.cpp \
    src/v4l2/V4l2DevBase.cpp \
    src/v4l2/V4l2Dev.cpp \
    src/v4l2/V4l2Buffer.cpp \
    src/v4l2/V4l2SubDev.cpp \
    src/v4l2/V4l2DeviceFactory.cpp \
    src/core/SofSource.cpp \
    src/core/CsiMetaDevice.cpp \
    src/3a/I3AControlFactory.cpp

# FILE_SOURCE_S
libcamhal_la_SOURCES += \
    src/core/FileSource.cpp
# FILE_SOURCE_E
# CUSTOM_WEIGHT_GRID_S
libcamhal_la_SOURCES += \
    src/platformdata/TunningProfiles.cpp
# CUSTOM_WEIGHT_GRID_E

libcamhal_la_CPPFLAGS = -std=c++11 -Wall -Werror -fstack-protector -fPIE -fPIC -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security
libcamhal_la_CPPFLAGS += -D__STDC_FORMAT_MACROS -DHAVE_PTHREADS -DHAVE_LINUX_OS -DHAVE_IA_TYPES -DHAVE_PRCTL
libcamhal_la_CPPFLAGS += -Iinclude -Iinclude/api -Iinclude/utils -Iinclude/linux -Isrc/platformdata -Isrc/v4l2 -Isrc/core -Isrc/ -Isrc/metadata
libcamhal_la_CPPFLAGS += -Isrc/3a/intel3a -Isrc/3a/external -Isrc/3a -Isrc/platformdata/gc -Isrc/iutils
libcamhal_la_CPPFLAGS += $(if $(findstring -faligned-new, $(shell $(CC) -faligned-new 2>&1)), , -faligned-new)

libcamhal_la_LDFLAGS = -lexpat -ldl -fPIE -fPIC -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wl,-z,relro -Wl,-z,now

# TODO build aal with cmake
if CAL_BUILD

libcamhal_la_CPPFLAGS += -Iaal -Iaal/chrome -Isrc/jpeg
libcamhal_la_CPPFLAGS += -DCAL_BUILD -DDUMP_IMAGE

libcamhal_la_SOURCES += \
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
    aal/chrome/Camera3Buffer.cpp \
    aal/JpegProcessor.cpp \
    src/jpeg/ColorConverter.cpp \
    src/jpeg/ExifCreater.cpp \
    src/jpeg/EXIFMaker.cpp \
    src/jpeg/EXIFMetaData.cpp \
    src/jpeg/IJpeg.cpp \
    src/jpeg/JpegMaker.cpp
# SW_JPEG_ENCODE_S
libcamhal_la_SOURCES += src/jpeg/SWJpegEncoder.cpp
# SW_JPEG_ENCODE_E

libcamhal_la_LDFLAGS += -L/build/iclrvp/usr/lib64/ $(LIBCAMERA_CLIENT_LIBS) $(LIBCAMERA_METADATA_LIBS) \
                        $(LIBCAMERA_COMMON_LIBS) $(LIBCBM_LIBS) $(LIBCAMERA_JPEG_LIBS) -ljpeg

libcamhal_la_CPPFLAGS += -I/build/iclrvp/usr/include/android/ \
                         -I/build/iclrvp/usr/include/android/system/core/include/ \
                         -I/build/iclrvp/usr/include/android/hardware/libhardware/include/ \
                         -I/build/iclrvp/usr/include/
endif #CAL_BUILD

if ENABLE_VIRTUAL_IPU_PIPE
libcamhal_la_CPPFLAGS += -DENABLE_VIRTUAL_IPU_PIPE
libcamhal_la_SOURCES += \
    src/ATEUnit.cpp
endif #ENABLE_VIRTUAL_IPU_PIPE

if BYPASS_MODE
libcamhal_la_CPPFLAGS += -DBYPASS_MODE

else

libcamhal_la_LDFLAGS += -lia_aiq \
                -lia_aiqb_parser \
                -lia_cmc_parser \
                -lia_exc \
                -lia_mkn \
                -lia_bcomp \
                -lia_nvm \
                -lia_log \
                -lia_dvs \
                -lia_lard\
                $(IA_IMAGING_LIBS) \
                $(LIBIACSS_LIBS) \
                $(LIBYUV_LIBS)

libcamhal_la_CPPFLAGS += $(IA_IMAGING_CFLAGS) \
                         $(LIBIACSS_CFLAGS)

if CAL_BUILD
libcamhal_la_CPPFLAGS += $(LIBIPU4P_CFLAGS)
else
libcamhal_la_CPPFLAGS += $(LIBIPU4_CFLAGS)
endif #CAL_BUILD

libcamhal_la_SOURCES += \
    src/core/PSysProcessor.cpp \
    src/core/SensorOB.cpp \
    src/core/IspParamAdaptor.cpp \
    src/core/psysprocessor/PSysDAG.cpp \
    src/core/psysprocessor/PipeExecutor.cpp \
    src/core/psysprocessor/PolicyManager.cpp \
    src/core/psysprocessor/PSysPipe.cpp \
    src/core/pgprocessor/PGParamAdapt.cpp \
    src/core/pgprocessor/PGBase.cpp \
    src/core/pgprocessor/Hp4KPreGDC.cpp \
    src/core/pgprocessor/Hp4KPostGDC.cpp \
    src/core/pgprocessor/HpHdrPreGDC.cpp \
    src/core/pgprocessor/HpHdrPostGDC.cpp \
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
    src/3a/AiqUnit.cpp \
    src/3a/IntelMkn.cpp \
    src/metadata/ParameterGenerator.cpp \
    src/platformdata/CameraConf.cpp \
    src/platformdata/gc/GcManagerCore.cpp \
    src/platformdata/gc/GraphConfigManager.cpp \
    src/platformdata/gc/FormatUtils.cpp \
    src/platformdata/gc/GraphUtils.cpp \
    src/platformdata/gc/GraphConfig.cpp

# INTEL_DVS_S
libcamhal_la_SOURCES += src/3a/IntelDvs.cpp
# INTEL_DVS_E
# LOCAL_TONEMAP_S
libcamhal_la_SOURCES += src/3a/Ltm.cpp
# LOCAL_TONEMAP_E
# CUSTOMIZED_3A_S
libcamhal_la_SOURCES += \
    src/3a/external/Customized3A.cpp \
    src/3a/external/CustomizedAic.cpp
# CUSTOMIZED_3A_E

# LITE_PROCESSING_S
libcamhal_la_SOURCES += \
    src/core/psyslite/WeavingProcessor.cpp \
    src/core/psyslite/PSysPipeBase.cpp \
    src/core/psyslite/WeavingPipeline.cpp \
    src/core/psyslite/CscProcessor.cpp \
    src/core/psyslite/CscPipeline.cpp \
    src/core/psyslite/ScaleProcessor.cpp \
    src/core/psyslite/ScalePipeline.cpp \
    src/core/psyslite/FisheyeProcessor.cpp \
    src/core/psyslite/FisheyePipeline.cpp \
    src/core/psyslite/PSysP2pLite.cpp
# LITE_PROCESSING_E

# ia_pal, ia_gaic auxiliary are used by ia_p2p and ia_isp_bxt. They are not included inside of ia_p2p and ia_isp_bxt currently.
libcamhal_la_LDFLAGS += -lia_coordinate -lia_ltm -lia_dvs -lia_isp_bxt -lbroxton_ia_pal -lia_ob

endif #BYPASS_MODE

if BUILD_RPM
.PHONY: rpm
rpm:
	./tools/rpm/build_rpm.sh $(DESTDIR)
endif
