AC_PREREQ([2.69])
AC_INIT([libsi468x], [1.0.0], [bug@ugreen.io])
AC_CONFIG_SRCDIR([src/si468x.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

# Setup the archiver tool for shared libraries
AM_PROG_AR

# Initialize Libtool
LT_INIT

# Check for compiler programs
AC_PROG_CXX
AC_PROG_CC

# Check for header files
AC_CHECK_HEADERS([stdint.h stdlib.h string.h unistd.h fcntl.h sys/ioctl.h])

# Output Makefiles
AC_CONFIG_FILES([
    Makefile
    src/Makefile
])
AC_OUTPUT
