// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "station.hpp"

namespace si468x_pi {

struct DeviceSettings {
    std::string spi = "/dev/spidev0.0";
    int reset_gpio = 23;
    std::string mode = "dab";      // dab | fmhd
    std::string audio = "analog";  // analog | i2s | both
    std::string region = "europe"; // europe | us
};

struct InputConfig {
    std::string type;          // "stdin" for the built-in source
    std::string plugin;        // plugin name when type is empty
    std::string device;        // optional shortcut, also present in config_json
    std::string config_json;   // serialized object handed to the plugin
};

struct DisplayConfig {
    std::string type = "tty"; // "tty" for the built-in display
    std::string plugin;       // plugin name when type is empty
    std::string device;
    int cols = 80;
    int rows = 25;
    std::string config_json;
};

struct ScanSettings {
    std::vector<std::string> channels = default_scan_channels();
    int rssi_min = 8;
    bool include_data = true;
};

struct VolumeSettings {
    int start = 50; // percent, 0..100
    int step = 4;   // percent, >= 1
};

struct UiSettings {
    int tick_ms = 100;
    bool mot = false;
};

struct PollSettings {
    int signal_ms = 1000;
    int dls_ms = 300;
    int info_ms = 5000;
};

struct Settings {
    DeviceSettings device;
    std::vector<InputConfig> input;
    std::vector<DisplayConfig> display = { DisplayConfig{} };
    bool input_configured = false;  // "input" key present (may be an empty array)
    bool display_configured = false;
    ScanSettings scan;
    VolumeSettings volume;
    UiSettings ui;
    PollSettings poll;
};

struct LastPlayed {
    std::string channel;
    uint32_t service_id = 0;
    std::string band = "dab";
    bool valid() const { return !channel.empty() || service_id != 0; }
};

struct Cache {
    int version = 1;
    LastPlayed last_played;
    int volume = -1; // chip units 0..63; -1 = not stored
    bool complete = true;
    std::vector<Station> stations;
};

/// XDG settings path: $XDG_CONFIG_HOME/si468x-pi/si468x-pi.json, else
/// ~/.config/..., else empty when neither variable is set.
std::string default_settings_path();

/// XDG cache path: $XDG_CACHE_HOME/si468x-pi/stations[-fmhd].json, else ~/.cache/...
std::string default_cache_path(const std::string& mode = "dab");

/// Parse a settings file.  A missing file is not an error and leaves defaults.
/// Malformed JSON or bad values are reported through `warnings` and fall back
/// to defaults; the function only returns false on an unreadable file.
bool load_settings(const std::string& path, Settings& out,
                   std::vector<std::string>* warnings = nullptr);

/// Parse settings from a string (used by tests and --config-example checks).
bool parse_settings(const std::string& text, Settings& out,
                    std::vector<std::string>* warnings = nullptr);

/// Serialize the built-in defaults as the example configuration.
std::string example_settings_json();

/// Read the station cache.  Missing file, unknown version or malformed JSON
/// yields an empty cache and a warning.
bool load_cache(const std::string& path, Cache& out,
                std::vector<std::string>* warnings = nullptr);

/// Write the cache atomically (temp file in the same directory + rename).
bool save_cache(const std::string& path, const Cache& cache,
                std::string* error = nullptr);

/// Cache/settings directory helpers, exposed for tests.
bool make_parent_dirs(const std::string& path, std::string* error = nullptr);

} // namespace si468x_pi
