## SPDX-FileCopyrightText: 2026 si468x-pi authors
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow

noinst_PROGRAMS = test_settings

test_settings_SOURCES = test_settings.cpp
test_settings_CPPFLAGS = \
	-I$(top_srcdir)/tools/si468x-pi \
	-I$(top_srcdir)/tools/si468x-pi/core \
	-I$(top_srcdir)/tools/si468x-pi/third_party
test_settings_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

TESTS = test_settings
