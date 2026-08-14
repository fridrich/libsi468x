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

libsi468x_la_CPPFLAGS = -I$(top_srcdir)/include
libsi468x_la_LDFLAGS = -version-info 0:0:0 -no-undefined @LIBS_WIRINGPI@

include_HEADERS = $(top_srcdir)/include/si468x.h

# Generated file cleanups
CLEANFILES = si468x_firmware.cpp

# Compile-time auto-generation rule for si468x_firmware.cpp using xxd or robust fallbacks
si468x_firmware.cpp:
	@echo "Generating $@..."
	@echo "/* Auto-generated from firmware binaries */" > $@
	@echo "#include \"si468x_firmware.h\"" >> $@
	@echo "" >> $@
	@if [ -f "$(FW_ROM_PATCH)" ]; then \
		echo "  Found ROM patch at $(FW_ROM_PATCH), embedding..."; \
		cp "$(FW_ROM_PATCH)" fw_rom_patch.bin; \
		xxd -i fw_rom_patch.bin | sed 's/unsigned char/const unsigned char/' | sed 's/unsigned int/const unsigned int/' >> $@; \
		rm -f fw_rom_patch.bin; \
	else \
		echo "  WARNING: ROM patch not found at $(FW_ROM_PATCH)! Generating dummy array..."; \
		echo "const unsigned char fw_rom_patch_bin[] = { 0x00 };" >> $@; \
		echo "const unsigned int fw_rom_patch_bin_len = 0;" >> $@; \
	fi
	@echo "" >> $@
	@if [ -f "$(FW_DAB_RADIO)" ]; then \
		echo "  Found DAB firmware at $(FW_DAB_RADIO), embedding..."; \
		cp "$(FW_DAB_RADIO)" fw_dab_radio.bin; \
		xxd -i fw_dab_radio.bin | sed 's/unsigned char/const unsigned char/' | sed 's/unsigned int/const unsigned int/' >> $@; \
		rm -f fw_dab_radio.bin; \
	else \
		echo "  WARNING: DAB firmware not found at $(FW_DAB_RADIO)! Generating dummy array..."; \
		echo "const unsigned char fw_dab_radio_bin[] = { 0x00 };" >> $@; \
		echo "const unsigned int fw_dab_radio_bin_len = 0;" >> $@; \
	fi
	@echo "" >> $@
	@if [ -f "$(FW_FMHD_RADIO)" ]; then \
		echo "  Found FMHD firmware at $(FW_FMHD_RADIO), embedding..."; \
		cp "$(FW_FMHD_RADIO)" fw_fmhd_radio.bin; \
		xxd -i fw_fmhd_radio.bin | sed 's/unsigned char/const unsigned char/' | sed 's/unsigned int/const unsigned int/' >> $@; \
		rm -f fw_fmhd_radio.bin; \
	else \
		echo "  WARNING: FMHD firmware not found at $(FW_FMHD_RADIO)! Generating dummy array..."; \
		echo "const unsigned char fw_fmhd_radio_bin[] = { 0x00 };" >> $@; \
		echo "const unsigned int fw_fmhd_radio_bin_len = 0;" >> $@; \
	fi
