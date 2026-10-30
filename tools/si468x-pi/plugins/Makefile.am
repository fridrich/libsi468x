## SPDX-FileCopyrightText: 2026 Fridrich Strba
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CPPFLAGS = \
	-I$(top_srcdir)/tools/si468x-pi/include \
	-I$(top_srcdir)/tools/si468x-pi/third_party

AM_CFLAGS = -Wall -Wextra
AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow

plugindir = $(libdir)/si468x-pi
plugin_LTLIBRARIES =

if ENABLE_PLUGIN_EVDEV
plugin_LTLIBRARIES += si468x-pi-evdev.la
si468x_pi_evdev_la_SOURCES = evdev/evdev.cpp evdev/remote_config.cpp evdev/remote_config.hpp
si468x_pi_evdev_la_LDFLAGS = -module -avoid-version -shared -rpath $(plugindir)
endif

if ENABLE_PLUGIN_FB
plugin_LTLIBRARIES += si468x-pi-fb.la
si468x_pi_fb_la_SOURCES = fb/fb.c fb/font8x8_basic.h fb/font8x8_ext_latin.h
si468x_pi_fb_la_LDFLAGS = -module -avoid-version -shared -rpath $(plugindir)
endif

if ENABLE_PLUGIN_LCD
plugin_LTLIBRARIES += si468x-pi-lcd.la
si468x_pi_lcd_la_SOURCES = lcd/lcd.c
si468x_pi_lcd_la_LDFLAGS = -module -avoid-version -shared -rpath $(plugindir)
endif

all-local: $(plugin_LTLIBRARIES)
	@mkdir -p $(top_builddir)/tools/si468x-pi/tests/si468x-pi-plugins
	@for p in evdev fb lcd; do \
		if test -f .libs/si468x-pi-$$p.so; then \
			cp -f .libs/si468x-pi-$$p.so $(top_builddir)/tools/si468x-pi/tests/si468x-pi-plugins/ 2>/dev/null || : ; \
		fi \
	done
