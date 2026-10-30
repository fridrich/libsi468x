## SPDX-FileCopyrightText: 2026 Fridrich Strba
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow -pthread
AM_LDFLAGS = -pthread

noinst_PROGRAMS = test_settings test_ui test_display

AM_CPPFLAGS = \
	-I$(top_srcdir)/tools/si468x-pi \
	-I$(top_srcdir)/tools/si468x-pi/core \
	-I$(top_srcdir)/tools/si468x-pi/ui \
	-I$(top_srcdir)/tools/si468x-pi/third_party

test_settings_SOURCES = test_settings.cpp
test_settings_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

test_ui_SOURCES = test_ui.cpp
test_ui_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

test_display_SOURCES = test_display.cpp
test_display_LDADD = $(top_builddir)/tools/si468x-pi/libsi468x-pi-core.la

TESTS = test_settings test_ui test_display
