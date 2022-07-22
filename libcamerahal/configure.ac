#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([libcamhal], [1.0])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/v4l2/SysCall.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE(libcamhal, 1.0)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lcamhal':
#AC_CHECK_LIB([camhal], [main])
# FIXME: Replace `main' with a function in `-lexpat':
#AC_CHECK_LIB([expat], [main])
# FIXME: Replace `main' with a function in `-lgmock':
#AC_CHECK_LIB([gmock], [main])
# FIXME: Replace `main' with a function in `-lgtest':
#AC_CHECK_LIB([gtest], [main])
# FIXME: Replace `main' with a function in `-lpthread':
#AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h malloc.h memory.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_PID_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_FORK
AC_HEADER_MAJOR
AC_FUNC_MALLOC
AC_CHECK_FUNCS([clock_gettime dup2 getpagesize gettimeofday memmove memset rmdir sqrt strcasecmp strchr strdup strerror strrchr strstr strtoul])

# check ipu version
AC_ARG_WITH([ipu],
        AC_HELP_STRING([--with-ipu=VERSION],
               [Choose ipu version [default=ipu4]]),
               [], [with_ipu=ipu4])
AM_CONDITIONAL([IPU_SYSVER_IPU4], [test "x$with_ipu" == "xipu4"])
AS_IF([test "x$with_ipu" == "xipu4"], [AC_DEFINE([IPU_SYSVER_IPU4], [], [IPU4 system])])

# Handle bypass condition
AC_ARG_WITH(bypass, AC_HELP_STRING([--with-bypass], [enable bypass mode]))

if test "x$with_bypass" == "xyes"
then
    AC_MSG_NOTICE([You have enabled by pass mode.])
else
    AC_MSG_NOTICE([bypass mode disabled.])

# Checking the dependancy
    PKG_CHECK_MODULES([IA_IMAGING], [ia_imaging])
    PKG_CHECK_MODULES([LIBIACSS], [libiacss])
    PKG_CHECK_MODULES([LIBIPU4], [libipu4], [],
        [AS_IF([test x"$with_cal" = xyes], [PKG_CHECK_MODULES([LIBIPU4P], [libipu4p])], [AC_MSG_ERROR([libipu not found])])])
fi
AM_CONDITIONAL(BYPASS_MODE, test "x$with_bypass" = "xyes")

if test "x$with_ate" == "xyes"
then
    AC_MSG_NOTICE([You have enabled ate feature.])
else
    AC_MSG_NOTICE([ate feature disabled.])
fi
AM_CONDITIONAL(ENABLE_VIRTUAL_IPU_PIPE, test "x$with_ate" = "xyes")

# Build rpm package condition
AC_ARG_WITH([rpm], [AC_HELP_STRING([--with-rpm], [build rpm package])], [], [with_rpm=yes])

if test x"$with_rpm" = xyes
then
    AC_MSG_NOTICE([rpm package is built.])
else
    AC_MSG_NOTICE([You have disabled rpm build.])
fi
AM_CONDITIONAL(BUILD_RPM, test "x$with_rpm" == "xyes")

# Build cal project
AC_ARG_WITH([cal], AC_HELP_STRING([--with-cal], [build cal]), [with_cal=yes], [])

if test x"$with_cal" = xyes
then
    AC_MSG_NOTICE([cal is built.])

    AC_ARG_WITH([base-version], AS_HELP_STRING([--with-base-version=VERSION], \
        [libbase version]), [], [with_base_version=no])
    AS_IF([test "x$with_base_version" != xno], [BASEVER=$with_base_version],
        [AC_MSG_ERROR([libbase version is not set])])

    PKG_CHECK_MODULES([CROS_CAMERA_ANDROID_HEADERS], [cros-camera-android-headers])
    PKG_CHECK_MODULES([LIBCAB], [libcab])
    PKG_CHECK_MODULES([LIBCAMERA_CLIENT], [libcamera_client])
    PKG_CHECK_MODULES([LIBCAMERA_COMMON], [libcamera_common])
    PKG_CHECK_MODULES([LIBCAMERA_JPEG], [libcamera_jpeg])
    PKG_CHECK_MODULES([LIBCAMERA_METADATA], [libcamera_metadata])
    PKG_CHECK_MODULES([LIBCAMERA_V4L2_DEVICE], [libcamera_v4l2_device])
    PKG_CHECK_MODULES([LIBCBM], [libcbm])
    PKG_CHECK_MODULES([LIBCHROME], [libchrome-$BASEVER])
    PKG_CHECK_MODULES([LIBMOJO], [libmojo-$BASEVER])
    PKG_CHECK_MODULES([LIBSYNC], [libsync])
    PKG_CHECK_MODULES([LIBYUV], [libyuv])
fi
AM_CONDITIONAL(CAL_BUILD, test "x$with_cal" == "xyes")

AC_SUBST([IPU_VERSION])
AM_CONDITIONAL(IPU_VERSION_SET, test ! -z "$IPU_VERSION")

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
