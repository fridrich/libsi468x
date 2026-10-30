## SPDX-FileCopyrightText: 2026 si468x-pi authors
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CPPFLAGS = -I$(srcdir) -I$(srcdir)/core -I$(srcdir)/include -I$(top_srcdir)/include
AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow -O2

if SI468X_PI_VENDORED_JSON
AM_CPPFLAGS += -I$(srcdir)/third_party
endif

bin_PROGRAMS = si468x-pi
noinst_LTLIBRARIES = libsi468x-pi-core.la

libsi468x_pi_core_la_SOURCES = \
	core/settings.cpp \
	core/settings.hpp \
	core/station.cpp \
	core/station.hpp \
	core/version.hpp

si468x_pi_SOURCES = si468x-pi.cpp
si468x_pi_LDADD = libsi468x-pi-core.la $(top_builddir)/src/libsi468x.la

SUBDIRS = . tests

EXTRA_DIST = README.md third_party/nlohmann/LICENSE.MIT third_party/nlohmann/json.hpp
