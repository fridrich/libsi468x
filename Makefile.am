SUBDIRS = src tests tools
ACLOCAL_AMFLAGS = -I m4
AM_DISTCHECK_CONFIGURE_FLAGS = --disable-firmware

# Install pkg-config configuration file
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsi468x.pc

# Distribute formatting configurations inside the release tarball
EXTRA_DIST = astyle.options Doxyfile.in docs/si468x-pi.md

# Support generating Doxygen documentation automatically during the build process
if HAVE_DOXYGEN
all-local: Doxyfile
	$(DOXYGEN) Doxyfile

clean-local:
	rm -rf doc/
endif

# Artistic Style (astyle) formatting target (fully recursive and future-proof)
astyle:
	astyle --options=$(srcdir)/astyle.options --recursive "$(srcdir)/*.h" "$(srcdir)/*.cpp"
