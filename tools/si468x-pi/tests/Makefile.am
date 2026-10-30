## SPDX-FileCopyrightText: 2026 Fridrich Strba
## SPDX-License-Identifier: GPL-2.0-or-later

pi = $(top_srcdir)/tools/si468x-pi
AM_CPPFLAGS = -I$(pi) -I$(pi)/core -I$(pi)/ui -I$(pi)/include $(JSON_CFLAGS) \
	-I$(top_srcdir)/tests -I$(top_srcdir)/src -I$(top_srcdir)/include \
	-DPLUGIN_DIR=\"$(abs_top_builddir)/tools/si468x-pi/plugins/.libs\" \
	-DTEST_PLUGIN_DIR=\"$(abs_builddir)/.libs\" \
	-DEXAMPLE_JSON=\"$(abs_top_srcdir)/tools/si468x-pi/examples/si468x-pi.json\"
AM_CFLAGS = -Wall -Wextra
AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow -pthread
AM_LDFLAGS = -pthread
LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la @LIBDL@

# loader rejection cases
check_LTLIBRARIES = si468x-pi-badabi.la si468x-pi-badsym.la
plugin_ldflags = -module -avoid-version -rpath $(abs_builddir)/.libs
si468x_pi_badabi_la_SOURCES = bad_abi.c
si468x_pi_badabi_la_LDFLAGS = $(plugin_ldflags)
si468x_pi_badsym_la_SOURCES = bad_sym.c
si468x_pi_badsym_la_LDFLAGS = $(plugin_ldflags)

TESTS = test_settings test_ui test_display test_plugins test_controller
check_PROGRAMS = $(TESTS)
AM_DEFAULT_SOURCE_EXT = .cpp
EXTRA_DIST = check.h

test_controller_LDADD = $(LDADD) $(top_builddir)/src/libsi468x.la
