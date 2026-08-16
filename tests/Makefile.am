noinst_PROGRAMS = test_si468x si468x_scan_dab si468x_scan_fm si468x_play_dab si468x_play_fm si468x_mask_compare

test_si468x_SOURCES = test_si468x.cpp
test_si468x_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
test_si468x_LDADD = $(top_builddir)/src/libsi468x.la

si468x_scan_dab_SOURCES = si468x_scan_dab.cpp
si468x_scan_dab_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_scan_dab_LDADD = $(top_builddir)/src/libsi468x.la

si468x_scan_fm_SOURCES = si468x_scan_fm.cpp
si468x_scan_fm_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_scan_fm_LDADD = $(top_builddir)/src/libsi468x.la

si468x_play_dab_SOURCES = si468x_play_dab.cpp
si468x_play_dab_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_play_dab_LDADD = $(top_builddir)/src/libsi468x.la

si468x_play_fm_SOURCES = si468x_play_fm.cpp
si468x_play_fm_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_play_fm_LDADD = $(top_builddir)/src/libsi468x.la

si468x_mask_compare_SOURCES = si468x_mask_compare.cpp
si468x_mask_compare_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src
si468x_mask_compare_LDADD = $(top_builddir)/src/libsi468x.la

# Standard Automake test suite harness
TESTS = test_si468x
