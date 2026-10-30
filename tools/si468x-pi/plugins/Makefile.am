## SPDX-FileCopyrightText: 2026 Fridrich Strba
## SPDX-License-Identifier: GPL-2.0-or-later

AM_CPPFLAGS = -I$(top_srcdir)/tools/si468x-pi/include $(JSON_CFLAGS)
AM_CFLAGS = -Wall -Wextra
AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wshadow
AM_LDFLAGS = -module -avoid-version -shared

plugindir = $(libdir)/si468x-pi
plugin_LTLIBRARIES =
EXTRA_DIST = plugin_util.h

if ENABLE_PLUGIN_EVDEV
plugin_LTLIBRARIES += si468x-pi-evdev.la
si468x_pi_evdev_la_SOURCES = evdev/evdev.cpp
endif

if ENABLE_PLUGIN_FB
plugin_LTLIBRARIES += si468x-pi-fb.la
si468x_pi_fb_la_SOURCES = fb/fb.c fb/font8x8_basic.h fb/font8x8_ext_latin.h
endif

if ENABLE_PLUGIN_LCD
plugin_LTLIBRARIES += si468x-pi-lcd.la
si468x_pi_lcd_la_SOURCES = lcd/lcd.c lcd/hd44780_charset.h
endif
