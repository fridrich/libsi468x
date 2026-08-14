SUBDIRS = src tests
ACLOCAL_AMFLAGS = -I m4

# Install pkg-config configuration file
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsi468x.pc

# Distribute formatting configurations inside the release tarball
EXTRA_DIST = astyle.options

# Artistic Style (astyle) formatting target (fully recursive and future-proof)
astyle:
	astyle --options=$(srcdir)/astyle.options --recursive "$(srcdir)/*.h" "$(srcdir)/*.cpp"

# Create an empty firmware directory in the release tarball, ignoring local binaries
dist-hook:
	mkdir -p $(distdir)/firmware
