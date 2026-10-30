AC_PREREQ([2.69])
AC_INIT([libsi468x], [0.0.0])
AC_CONFIG_SRCDIR([src/si468x.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects dist-bzip2 dist-xz])

# Setup the archiver tool for shared libraries
AM_PROG_AR

# Initialize Libtool
LT_INIT

# Check for compiler programs
AC_PROG_CXX
AC_PROG_CC

# Check for header files
AC_CHECK_HEADERS([stdint.h stdlib.h string.h unistd.h fcntl.h sys/ioctl.h])

# Optional si468x-pi application (tools/si468x-pi).  Disabling it keeps a pure
# library build and must not require a C++17 compiler or JSON headers.
AC_ARG_ENABLE([si468x-pi],
    [AS_HELP_STRING([--disable-si468x-pi], [do not build the si468x-pi terminal application])],
    [], [enable_si468x_pi=yes])
AM_CONDITIONAL([ENABLE_SI468X_PI], [test "x$enable_si468x_pi" = xyes])

AC_ARG_WITH([json],
    [AS_HELP_STRING([--with-json=system|vendored], [JSON header source for si468x-pi (default: vendored)])],
    [], [with_json=vendored])
AM_CONDITIONAL([SI468X_PI_VENDORED_JSON], [test "x$with_json" = "xvendored"])

LIBDL=""
AC_SUBST([LIBDL])

if test "x$enable_si468x_pi" = xyes; then
    case "$with_json" in
        system|vendored) ;;
        *) AC_MSG_ERROR([--with-json must be 'system' or 'vendored']) ;;
    esac

    save_CXXFLAGS="$CXXFLAGS"
    CXXFLAGS="$CXXFLAGS -std=c++17"
    AC_LANG_PUSH([C++])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if __cplusplus < 201703L
#error "C++17 is required by si468x-pi"
#endif
        ]], [[]])],
        [],
        [AC_MSG_ERROR([si468x-pi needs a C++17 compiler (try --disable-si468x-pi)])])
    AC_LANG_POP([C++])
    CXXFLAGS="$save_CXXFLAGS"

    if test "x$with_json" = "xsystem"; then
        AC_CHECK_HEADER([nlohmann/json.hpp], [],
            [AC_MSG_ERROR([nlohmann/json.hpp not found; install nlohmann-json3-dev or use --with-json=vendored])])
    fi

    AC_CHECK_LIB([dl], [dlopen], [LIBDL="-ldl"], [LIBDL=""])
fi

# Firmware images embedded at build time (never distributed). Missing/empty = configure error.
AC_ARG_ENABLE([firmware],
    [AS_HELP_STRING([--disable-firmware], [build without embedded firmware (si468x_init returns SI468X_ERROR_FIRMWARE)])],
    [], [enable_firmware=yes])

AC_ARG_WITH([fw-rom-patch],
    [AS_HELP_STRING([--with-fw-rom-patch=PATH], [ROM patch image (default: ../fw_rom_patch.bin)])],
    [FW_ROM_PATCH="$withval"], [FW_ROM_PATCH="$srcdir/../fw_rom_patch.bin"])
AC_ARG_WITH([fw-dab-radio],
    [AS_HELP_STRING([--with-fw-dab-radio=PATH], [DAB application image (default: ../fw_dab_radio.bin)])],
    [FW_DAB_RADIO="$withval"], [FW_DAB_RADIO="$srcdir/../fw_dab_radio.bin"])
AC_ARG_WITH([fw-fmhd-radio],
    [AS_HELP_STRING([--with-fw-fmhd-radio=PATH], [FMHD application image (default: ../fw_fmhd_radio.bin)])],
    [FW_FMHD_RADIO="$withval"], [FW_FMHD_RADIO="$srcdir/../fw_fmhd_radio.bin"])

if test "x$enable_firmware" = xyes; then
    AC_CHECK_PROG([XXD], [xxd], [xxd])
    if test -z "$XXD"; then
        AC_MSG_ERROR([xxd not found (package xxd or vim-common), or use --disable-firmware])
    fi
    for fw_var in FW_ROM_PATCH FW_DAB_RADIO FW_FMHD_RADIO; do
        eval fw_file=\$$fw_var
        if test ! -s "$fw_file"; then
            AC_MSG_ERROR([firmware image '$fw_file' missing or empty (set --with-fw-*=PATH or use --disable-firmware)])
        fi
        fw_file="$(cd "$(dirname "$fw_file")" && pwd)/$(basename "$fw_file")"
        eval $fw_var=\$fw_file
        AC_MSG_NOTICE([firmware: $fw_file])
    done
    AC_DEFINE([SI468X_HAS_FIRMWARE], [1], [Firmware images embedded])
else
    FW_ROM_PATCH= FW_DAB_RADIO= FW_FMHD_RADIO=
    AC_DEFINE([SI468X_HAS_FIRMWARE], [0], [Firmware images embedded])
fi
AM_CONDITIONAL([HAVE_FIRMWARE], [test "x$enable_firmware" = xyes])

# No longer require wiringPi library; using native Linux standard driver APIs
LIBS_WIRINGPI=""
AC_SUBST([LIBS_WIRINGPI])

AC_SUBST([FW_ROM_PATCH])
AC_SUBST([FW_DAB_RADIO])
AC_SUBST([FW_FMHD_RADIO])

# Check for Doxygen (gracefully disables documentation targets if not installed)
AC_CHECK_PROG([DOXYGEN], [doxygen], [doxygen])
AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
if test -z "$DOXYGEN"; then
    AC_MSG_WARN([doxygen tool not found! Doxygen documentation generation will be skipped during build.])
fi

# Output Makefiles and pkg-config files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    tests/Makefile
    tools/Makefile
    tools/si468x-pi/Makefile
    tools/si468x-pi/tests/Makefile
    libsi468x.pc
    Doxyfile
])
AC_OUTPUT
