# si468x-pi

Terminal control application for the uGreen DAB Board (Si468x) on a
Raspberry Pi.  The Si468x co-processor handles RF, DAB decoding and audio;
the host only sends commands over SPI and renders a text UI.

## Build

```sh
./autogen.sh
./configure --disable-firmware
make
make check
```

`--disable-firmware` builds without the embedded images, which is enough for
all tests that do not need a chip.  A real radio needs the firmware images in
the repository root (or `--with-fw-*` paths).

The application is optional:

```sh
./configure --disable-firmware --disable-si468x-pi
```

builds the library alone.

Needs nlohmann_json >= 3.9 (`nlohmann-json3-dev`, `nlohmann_json-devel`).

Usage: `man si468x-pi` (`doc/si468x-pi.1`); hardware, permissions and plugins:
`docs/si468x-pi.md`.

## Licence

GPL-2.0-or-later.  See the SPDX headers in each file.
