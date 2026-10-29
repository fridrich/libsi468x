lib_LTLIBRARIES = libsi468x.la

# Substitutions from configure
FW_ROM_PATCH = @FW_ROM_PATCH@
FW_DAB_RADIO = @FW_DAB_RADIO@
FW_FMHD_RADIO = @FW_FMHD_RADIO@

# Standard distributed sources
libsi468x_la_SOURCES = \
    si468x.cpp \
    si468x_internal.h \
    si468x_firmware.h

# Non-distributed generated sources (excluded from release tarballs for legal safety)
nodist_libsi468x_la_SOURCES = \
    si468x_firmware.cpp

AM_CXXFLAGS = -Wall -Wextra -Wshadow

libsi468x_la_CPPFLAGS = -I$(top_srcdir)/include
libsi468x_la_LDFLAGS = -version-info 0:0:0 -no-undefined @LIBS_WIRINGPI@

include_HEADERS = $(top_srcdir)/include/si468x.h

# Generated file cleanups
CLEANFILES = si468x_firmware.cpp

# Firmware images are real prerequisites: regenerated when an image changes, never a stale dummy
if HAVE_FIRMWARE
si468x_firmware.cpp: $(FW_ROM_PATCH) $(FW_DAB_RADIO) $(FW_FMHD_RADIO)
	@echo "Generating $@ from firmware images"
	@rm -rf fwgen && mkdir fwgen
	@cp "$(FW_ROM_PATCH)" fwgen/fw_rom_patch.bin
	@cp "$(FW_DAB_RADIO)" fwgen/fw_dab_radio.bin
	@cp "$(FW_FMHD_RADIO)" fwgen/fw_fmhd_radio.bin
	@{ echo '/* Auto-generated from firmware binaries */'; echo '#include "si468x_firmware.h"'; \
	  for f in fw_rom_patch fw_dab_radio fw_fmhd_radio; do \
	    (cd fwgen && $(XXD) -i $$f.bin) | sed 's/^unsigned/const unsigned/'; \
	  done; } > $@.tmp
	@rm -rf fwgen
	@mv $@.tmp $@
else
si468x_firmware.cpp:
	@echo "Generating $@ without firmware (--disable-firmware)"
	@{ echo '#include "si468x_firmware.h"'; \
	  for f in fw_rom_patch fw_dab_radio fw_fmhd_radio; do \
	    echo "const unsigned char $${f}_bin[] = { 0x00 };"; echo "const unsigned int $${f}_bin_len = 0;"; \
	  done; } > $@
endif
