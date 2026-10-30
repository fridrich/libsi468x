# si468x-pi: DAB/FM Receiver for Raspberry Pi

`si468x-pi` is a high-performance terminal radio application and headless daemon for the Silicon Labs Si4684/Si4689 receiver boards (such as the uGreen DAB Board) on Raspberry Pi devices (including Pi Zero, Zero 2 W, Pi 3, Pi 4, and Pi 5).

## Hardware Setup

### Default Pinout (Raspberry Pi 40-pin Header)

| Si468x Pin | Header Pin | BCM GPIO | Function |
|---|---|---|---|
| MOSI | Pin 19 | GPIO 10 | SPI0 MOSI |
| MISO | Pin 21 | GPIO 9 | SPI0 MISO |
| SCLK | Pin 23 | GPIO 11 | SPI0 SCLK |
| CSB  | Pin 24 | GPIO 8 | SPI0 CE0 |
| RSTB | Pin 16 | GPIO 23 | Active-Low Reset |
| 3V3  | Pin 1  | 3.3V | Power |
| GND  | Pin 6  | GND | Ground |

### Audio Routing

The Si468x chip provides both an internal stereo DAC (routed to the on-board 3.5mm analog audio jack) and digital I2S audio output.
- **`analog`**: Analog headphone output (DAC enabled, I2S pins disabled).
- **`i2s`**: Master digital I2S stream (48 kHz / 16-bit) routed to external I2S DACs (e.g. PCM5102A).
- **`both`**: Simultaneous analog DAC and digital I2S output.

The chip takes the route only when it boots: set `device.audio` or `--audio analog|i2s|both` before starting; changing it means restarting `si468x-pi`.

## Running Unprivileged (Non-Root)

To run `si468x-pi` as an unprivileged user (or via systemd service under a dedicated service user), install the following udev rule:

`/etc/udev/rules.d/99-si468x.rules`:
```udev
# SPI access
SUBSYSTEM=="spidev", KERNEL=="spidev0.0", GROUP="dialout", MODE="0660"

# GPIO sysfs access for hardware reset
SUBSYSTEM=="gpio", KERNEL=="gpio*", GROUP="dialout", MODE="0660"
```

Reload udev rules:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Ensure your user is in the `dialout` group:
```bash
sudo usermod -aG dialout $USER
```

## Configuration

Options, keys, settings keys and file locations: `man si468x-pi`. Start from the defaults:
```bash
si468x-pi --config-example > ~/.config/si468x-pi/si468x-pi.json
```

## Systemd Daemon Deployment

Install systemd service:
```bash
sudo cp examples/si468x-pi.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now si468x-pi.service
```

View live radio logs:
```bash
journalctl -u si468x-pi.service -f
```

## Hardware Plugins

Plugins extend `si468x-pi` with external hardware without rebuilding the main application binary. Shared modules reside in `/usr/local/lib/si468x-pi/`:

### 1. `evdev` (Remote Control Input)
Reads remote controls whose USB dongle appears as a keyboard (`/dev/input/event*`). Without `"device"` it picks the input device carrying most of the mapped keys, together with its sibling nodes (a 2.4 GHz dongle often has a keyboard and a consumer-control node), and waits for it if it is not plugged in yet or goes away. Keys are grabbed by default so that they do not reach the console. A held up/down/volume button repeats; other buttons act once per press. Unmapped keys are logged with their code.
```json
"input": [
  {
    "plugin": "evdev",
    "name": "remote",
    "grab": true,
    "map": {
      "KEY_UP": "up",
      "KEY_DOWN": "down",
      "KEY_ENTER": "enter",
      "KEY_VOLUMEUP": "volume_up",
      "KEY_VOLUMEDOWN": "volume_down",
      "KEY_MUTE": "mute",
      "KEY_F19": "none"
    }
  }
]
```
`"device"` (`/dev/input/eventN`) fixes the device, `"name"` limits the search to devices whose name contains it (case-insensitive). `"map"` keys are `KEY_*` names or decimal codes (as printed by `evtest`), merged over the defaults; actions are `up`, `down`, `left`, `right`, `enter`, `menu`, `quit`, `volume_up`, `volume_down`, `mute`, `home`, `none`.

### 2. `fb` (OLED / Framebuffer Display)
Renders an 8×8 font to Linux framebuffers (`/dev/fb0` by default) at 16 or 32 bpp, with reverse-video highlighting.
```json
"display": [
  {
    "plugin": "fb",
    "device": "/dev/fb1"
  }
]
```

### 3. `lcd` (HD44780 Character Display)
Drives HD44780 displays through the Linux kernel `charlcd` driver (`/dev/lcd`) with UTF-8 to HD44780 character mapping. The size comes from the device tree (`display-width-chars`, `display-height-chars`).
```json
"display": [
  {
    "plugin": "lcd"
  }
]
```
