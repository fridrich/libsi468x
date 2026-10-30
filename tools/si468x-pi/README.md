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

## Run

```sh
# print the default configuration
tools/si468x-pi/si468x-pi --config-example

# after installation
si468x-pi --config /etc/si468x-pi/si468x-pi.json
si468x-pi -S 17361            # play a service id
si468x-pi -p "RTS"            # play the first matching label
```

Settings live in `$XDG_CONFIG_HOME/si468x-pi/si468x-pi.json` (or
`~/.config/...`), the station cache in
`$XDG_CACHE_HOME/si468x-pi/stations.json` (or `~/.cache/...`).

## Licence

GPL-2.0-or-later.  See the SPDX headers in each file.  The bundled
`third_party/nlohmann/json.hpp` is MIT licensed; its licence is in
`third_party/nlohmann/LICENSE.MIT`.
