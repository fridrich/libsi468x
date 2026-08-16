noinst_PROGRAMS = test_si468x si468x_scan si468x_play_test si468x_fm_test si468x_fm_scan si468x_mask_compare si468x_play

test_si468x_SOURCES = test_si468x.cpp
test_si468x_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
test_si468x_LDADD = $(top_builddir)/src/libsi468x.la

si468x_scan_SOURCES = si468x_scan.cpp
si468x_scan_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_scan_LDADD = $(top_builddir)/src/libsi468x.la

si468x_play_test_SOURCES = si468x_play_test.cpp
si468x_play_test_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_play_test_LDADD = $(top_builddir)/src/libsi468x.la

si468x_fm_test_SOURCES = si468x_fm_test.cpp
si468x_fm_test_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_fm_test_LDADD = $(top_builddir)/src/libsi468x.la

si468x_fm_scan_SOURCES = si468x_fm_scan.cpp
si468x_fm_scan_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_fm_scan_LDADD = $(top_builddir)/src/libsi468x.la

si468x_mask_compare_SOURCES = si468x_mask_compare.cpp
si468x_mask_compare_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_mask_compare_LDADD = $(top_builddir)/src/libsi468x.la

si468x_play_SOURCES = si468x_play.cpp
si468x_play_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_play_LDADD = $(top_builddir)/src/libsi468x.la

# Standard Automake test suite harness
TESTS = test_si468x
