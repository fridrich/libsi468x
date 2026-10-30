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

Audio output can be switched at runtime from the interactive menu or via `--audio analog|i2s|both` without requiring a chip reboot.

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

The default configuration file is searched in:
1. `--config <path>` (command-line override)
2. `$XDG_CONFIG_HOME/si468x-pi/si468x-pi.json` (or `~/.config/si468x-pi/si468x-pi.json`)
3. `/etc/si468x-pi/si468x-pi.json`

Generate a template configuration:
```bash
si468x-pi --config-example > ~/.config/si468x-pi/si468x-pi.json
```

### Key Settings

```json
{
  "device": {
    "spi": "/dev/spidev0.0",
    "reset_gpio": 23,
    "mode": "dab",
    "audio": "analog"
  },
  "volume": {
    "start": 50,
    "step": 4
  },
  "scan": {
    "rssi_min": 8,
    "include_data": false
  }
}
```

## Interactive Keyboard Controls

| Key | Action | Description |
|---|---|---|
| `Up` / `Down`, `u` / `d` | `UP` / `DOWN` | Navigate station lists and menus |
| `Enter`, `e` | `ENTER` | Select station or menu option |
| `Menu`, `m`, `Esc`, `Left` | `MENU` / `LEFT` | Open main menu or back |
| `Home`, `h` | `HOME` | Return directly to radio screen |
| `+`, `=` | `VOLUME_UP` | Increase volume (step size configurable) |
| `-` | `VOLUME_DOWN` | Decrease volume |
| `0` | `MUTE` | Toggle audio mute |
| `q`, `x` | `QUIT` | Exit application cleanly |

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
Reads Linux input event devices (`/dev/input/event*`) such as IR remotes, USB keypads, and GPIO buttons.
```json
"input": [
  {
    "plugin": "evdev",
    "device": "/dev/input/event0",
    "map": {
      "KEY_UP": "UP",
      "KEY_DOWN": "DOWN",
      "KEY_ENTER": "ENTER",
      "KEY_VOLUMEUP": "VOLUME_UP",
      "KEY_VOLUMEDOWN": "VOLUME_DOWN",
      "KEY_MUTE": "MUTE"
    }
  }
]
```

### 2. `fb` (OLED / Framebuffer Display)
Renders an 8×8 font to Linux framebuffers (`/dev/fb0`, `/dev/fb1`) supporting RGB565 and XRGB8888 with reverse-video highlighting.
```json
"display": [
  {
    "plugin": "fb",
    "device": "/dev/fb1"
  }
]
```

### 3. `lcd` (HD44780 Character Display)
Drives HD44780 displays connected through the Linux kernel `charlcd` driver (`/dev/lcd`) with automatic UTF-8 to HD44780 code page translation.
```json
"display": [
  {
    "plugin": "lcd",
    "device": "/dev/lcd"
  }
]
```
