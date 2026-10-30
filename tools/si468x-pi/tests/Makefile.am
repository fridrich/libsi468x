## SPDX-FileCopyrightText: 2026 Fridrich Strba
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow -pthread
AM_LDFLAGS = -pthread

noinst_PROGRAMS = test_settings test_ui test_display test_plugins
check_LTLIBRARIES = si468x-pi-example.la si468x-pi-badabi.la si468x-pi-badsym.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/tools/si468x-pi \
	-I$(top_srcdir)/tools/si468x-pi/core \
	-I$(top_srcdir)/tools/si468x-pi/ui \
	-I$(top_srcdir)/tools/si468x-pi/include \
	-I$(top_srcdir)/tools/si468x-pi/third_party

si468x_pi_example_la_SOURCES = example.c
si468x_pi_example_la_CFLAGS = -Wall -Wextra -I$(top_srcdir)/tools/si468x-pi/include
si468x_pi_example_la_LDFLAGS = -module -avoid-version -rpath $(abs_builddir)/.libs

si468x_pi_badabi_la_SOURCES = bad_abi.c
si468x_pi_badabi_la_CFLAGS = -Wall -Wextra -I$(top_srcdir)/tools/si468x-pi/include
si468x_pi_badabi_la_LDFLAGS = -module -avoid-version -rpath $(abs_builddir)/.libs

si468x_pi_badsym_la_SOURCES = bad_sym.c
si468x_pi_badsym_la_CFLAGS = -Wall -Wextra -I$(top_srcdir)/tools/si468x-pi/include
si468x_pi_badsym_la_LDFLAGS = -module -avoid-version -rpath $(abs_builddir)/.libs

test_settings_SOURCES = test_settings.cpp
test_settings_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

test_ui_SOURCES = test_ui.cpp
test_ui_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

test_display_SOURCES = test_display.cpp
test_display_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

test_plugins_SOURCES = test_plugins.cpp
test_plugins_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la @LIBDL@

TESTS = test_settings test_ui test_display test_plugins

all-local: $(check_LTLIBRARIES)
	@mkdir -p si468x-pi-plugins
	@cp -f .libs/si468x-pi-example.so si468x-pi-plugins/ 2>/dev/null || :

clean-local:
	rm -rf si468x-pi-plugins
