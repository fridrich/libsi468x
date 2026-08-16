AC_PREREQ([2.69])
AC_INIT([libsi468x], [0.0.0], [bug@ugreen.io])
AC_CONFIG_SRCDIR([src/si468x.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror dist-bzip2 dist-xz])

# Setup the archiver tool for shared libraries
AM_PROG_AR

# Initialize Libtool
LT_INIT

# Check for compiler programs
AC_PROG_CXX
AC_PROG_CC

# Check for header files
AC_CHECK_HEADERS([stdint.h stdlib.h string.h unistd.h fcntl.h sys/ioctl.h])

# Check for the xxd utility (required for compile-time firmware conversion)
AC_CHECK_PROG([XXD], [xxd], [xxd])
if test -z "$XXD"; then
    AC_MSG_ERROR([xxd tool not found! Please install xxd (part of vim-common) or ensure it is in PATH.])
fi

# No longer require wiringPi library; using native Linux standard driver APIs
LIBS_WIRINGPI=""
AC_SUBST([LIBS_WIRINGPI])

# Configure options for firmware paths
AC_ARG_WITH([fw-rom-patch],
    [AS_HELP_STRING([--with-fw-rom-patch=PATH], [Path to fw_rom_patch.bin (default: firmware/fw_rom_patch.bin)])],
    [FW_ROM_PATCH="$(cd "$(dirname "$withval")" 2>/dev/null && pwd)/$(basename "$withval")"],
    [FW_ROM_PATCH="\$(top_srcdir)/firmware/fw_rom_patch.bin"])

AC_ARG_WITH([fw-dab-radio],
    [AS_HELP_STRING([--with-fw-dab-radio=PATH], [Path to fw_dab_radio.bin (default: firmware/fw_dab_radio.bin)])],
    [FW_DAB_RADIO="$(cd "$(dirname "$withval")" 2>/dev/null && pwd)/$(basename "$withval")"],
    [FW_DAB_RADIO="\$(top_srcdir)/firmware/fw_dab_radio.bin"])

AC_ARG_WITH([fw-fmhd-radio],
    [AS_HELP_STRING([--with-fw-fmhd-radio=PATH], [Path to fw_fmhd_radio.bin (default: firmware/fw_fmhd_radio.bin)])],
    [FW_FMHD_RADIO="$(cd "$(dirname "$withval")" 2>/dev/null && pwd)/$(basename "$withval")"],
    [FW_FMHD_RADIO="\$(top_srcdir)/firmware/fw_fmhd_radio.bin"])

# Substitute firmware variables in Makefiles
AC_SUBST([FW_ROM_PATCH])
AC_SUBST([FW_DAB_RADIO])
AC_SUBST([FW_FMHD_RADIO])

# Output Makefiles and pkg-config files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    tests/Makefile
    libsi468x.pc
])
AC_OUTPUT
