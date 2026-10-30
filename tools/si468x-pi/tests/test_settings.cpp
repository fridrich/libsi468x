// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tests for settings, cache and the canonical channel table.

#include "check.h"
#include "settings.hpp"
#include "station.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

bool contains(const std::vector<std::string>& list, const std::string& text)
{
    for (const std::string& s : list) {
        if (s.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void write_file(const std::string& path, const std::string& text)
{
    std::ofstream out(path);
    out << text;
}

void test_channels()
{
    using namespace si468x_pi;

    auto freq = [](const char* label) {
        const Channel* c = find_channel(label);
        return c ? c->frequency_hz : 0;
    };
    check(band3_channels().size() == 41, "Band III table has 41 entries");
    check(freq("12A") == 223936000, "12A maps to 223936000 Hz");
    check(freq("10N") == 210096000, "10N maps to 210096000 Hz");
    check(freq("5A") == 174928000, "5A maps to 174928000 Hz");
    check(freq("13F") == 239200000, "13F maps to 239200000 Hz");
    check(find_channel("99Z") == nullptr, "find_channel rejects unknown labels");

    check(default_scan_channels().size() == 38, "default scan list has 38 channels");
    bool has_n = false;
    for (const std::string& label : default_scan_channels()) {
        if (!label.empty() && label.back() == 'N') {
            has_n = true;
        }
    }
    check(!has_n, "default scan list skips N channels");
}

void test_normalize()
{
    using namespace si468x_pi;

    Station a { "12A", "Beta", "Beta", 2, 0x1000, false };
    Station b { "12A", "Alpha", "Alpha", 1, 0x2000, false };
    Station dup = b;
    Station unknown { "99Z", "Ghost", "Ghost", 3, 0, false };
    Station empty { "12A", "", "", 4, 0, false };
    Station spi { "12A", "SPI Service", "SPI", 0xE1400103, 0, true, "dab" };
    Station other = a;
    other.channel = "12B";

    std::vector<Station> list = { a, b, dup, unknown, empty, spi, other };
    normalize_stations(list);

    check(list.size() == 3, "normalize keeps three stations and drops SPI (>0xFFFF)");
    check(list[0].program == "Alpha", "normalize sorts by program");
    check(list[1].program == "Beta" && list[1].channel == "12A", "same program sorts by channel");
    check(list[2].program == "Beta" && list[2].channel == "12B", "second channel kept");

    Station fm1 { "100.1", "100.1 MHz", "100.1 MHz", 100100000, 0, false, "fm" };
    Station fm2 { "88.5", "88.5 MHz", "88.5 MHz", 88500000, 0, false, "fm" };
    Station fm3 { "91.3", "SRF 1", "SRF 1", 91300000, 0, false, "fm" };
    std::vector<Station> fmlist = { fm1, fm2, fm3 };
    normalize_stations(fmlist);
    check(fmlist.size() == 3, "normalize keeps three FM stations");
    check(fmlist[0].channel == "88.5", "FM sorts 88.5 first");
    check(fmlist[1].channel == "91.3", "FM sorts 91.3 second");
    check(fmlist[2].channel == "100.1", "FM sorts 100.1 third");
}

void test_defaults()
{
    using namespace si468x_pi;

    Settings s;
    std::vector<std::string> warnings;
    check(parse_settings("{}", s, &warnings), "empty object parses");
    check(warnings.empty(), "empty object produces no warnings");
    check(s.device.spi == "/dev/spidev0.0", "default SPI device");
    check(s.device.reset_gpio == 23, "default reset GPIO");
    check(s.device.mode == "dab", "default mode");
    check(s.device.audio == "analog", "default audio route");
    check(s.scan.channels.size() == 38, "default scan channels");
    check(s.scan.rssi_min == 8, "default RSSI minimum");
    check(s.scan.include_data, "data services included by default");
    check(s.volume.start == 50 && s.volume.step == 4, "default volume");
    check(s.ui.tick_ms == 100 && !s.ui.mot, "default UI");
    check(s.poll.signal_ms == 1000 && s.poll.dls_ms == 300 && s.poll.info_ms == 5000,
          "default poll intervals");
    check(!s.input_configured && s.input.empty(), "input is absent by default");
    check(s.display.size() == 1 && s.display[0].type == "tty" && s.display[0].cols == 80
              && s.display[0].rows == 25,
          "display defaults to an 80x25 tty");
}

void test_parse_and_validate()
{
    using namespace si468x_pi;

    const char* text = R"({
      "device": {"spi": "/dev/spidev1.0", "reset_gpio": 24, "mode": "fmhd",
                 "audio": "i2s", "region": "us"},
      "input": [{"type": "stdin"}, {"plugin": "evdev", "device": "/dev/input/event3"}],
      "display": [{"type": "tty", "cols": 20, "rows": 4},
                  {"plugin": "lcd", "device": "/dev/lcd"}],
      "scan": {"channels": ["12A", "99Z", "5A"], "rssi_min": 12, "include_data": false},
      "volume": {"start": 63, "step": 2},
      "ui": {"tick_ms": 200, "mot": true},
      "poll": {"signal_ms": 900, "dls_ms": 250, "info_ms": 4000}
    })";

    Settings s;
    std::vector<std::string> warnings;
    check(parse_settings(text, s, &warnings), "full settings parse");
    check(s.device.spi == "/dev/spidev1.0", "custom SPI device");
    check(s.device.reset_gpio == 24, "custom reset GPIO");
    check(s.device.mode == "fmhd", "custom mode");
    check(s.device.audio == "i2s", "custom audio");
    check(s.device.region == "us", "custom region");
    check(s.input.size() == 2, "two input sources");
    check(s.input[0].type == "stdin", "first input is stdin");
    check(s.input[1].plugin == "evdev" && s.input[1].config_json.find("/dev/input/event3") != std::string::npos,
          "second input is evdev, its config passed whole");
    check(s.display.size() == 2, "two displays");
    check(s.display[0].cols == 20 && s.display[0].rows == 4, "custom tty geometry");
    check(s.display[1].plugin == "lcd", "second display is lcd");
    check(s.scan.channels.size() == 2, "unknown channel is skipped");
    check(s.scan.channels[0] == "12A" && s.scan.channels[1] == "5A", "valid channels kept");
    check(!s.scan.include_data, "include_data override");
    check(s.scan.rssi_min == 12, "rssi_min override");
    check(contains(warnings, "99Z"), "unknown channel warns");

    Settings clamped;
    warnings.clear();
    const char* bad = R"({
      "volume": {"start": 500, "step": 0},
      "ui": {"tick_ms": 1},
      "poll": {"signal_ms": 1, "dls_ms": 1, "info_ms": 1},
      "device": {"mode": "shortwave", "reset_gpio": 100},
      "scan": {"channels": ["99Z"]}
    })";
    check(parse_settings(bad, clamped, &warnings), "invalid values still parse");
    check(clamped.volume.start == 100, "volume start clamps to 100");
    check(clamped.volume.step == 1, "volume step clamps to 1");
    check(clamped.ui.tick_ms == 50, "UI tick clamps to 50 ms");
    check(clamped.poll.signal_ms == 500, "signal poll clamps to 500 ms");
    check(clamped.poll.dls_ms == 200, "DLS poll clamps to 200 ms");
    check(clamped.poll.info_ms == 1000, "info poll clamps to 1000 ms");
    check(clamped.device.mode == "dab", "unknown mode falls back");
    check(clamped.device.reset_gpio == 53, "GPIO clamps to 53");
    check(clamped.scan.channels.size() == 38, "all-invalid channel list falls back");
    check(!warnings.empty(), "invalid values warn");
}

void test_malformed()
{
    using namespace si468x_pi;

    Settings s;
    s.device.spi = "sentinel";
    std::vector<std::string> warnings;
    check(parse_settings("{not json", s, &warnings), "malformed JSON does not abort");
    check(s.device.spi == "/dev/spidev0.0", "malformed JSON falls back to defaults");
    check(!warnings.empty(), "malformed JSON warns");

    warnings.clear();
    check(parse_settings("[]", s, &warnings), "non-object top level does not abort");
    check(!warnings.empty(), "non-object top level warns");
}

void test_paths()
{
    using namespace si468x_pi;

    setenv("XDG_CONFIG_HOME", "/tmp/si468x-pi-cfg", 1);
    setenv("XDG_CACHE_HOME", "/tmp/si468x-pi-cache", 1);
    check(default_settings_path() == "/tmp/si468x-pi-cfg/si468x-pi/si468x-pi.json",
          "XDG config path");
    check(default_cache_path() == "/tmp/si468x-pi-cache/si468x-pi/stations.json", "XDG cache path");

    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
    setenv("HOME", "/home/tester", 1);
    check(default_settings_path() == "/home/tester/.config/si468x-pi/si468x-pi.json",
          "HOME config fallback");
    check(default_cache_path() == "/home/tester/.cache/si468x-pi/stations.json",
          "HOME cache fallback");

    unsetenv("HOME");
    check(default_settings_path().empty(), "no HOME means no settings path");
    check(default_cache_path().empty(), "no HOME means no cache path");

    setenv("HOME", "/home/tester", 1);
}

void test_example()
{
    using namespace si468x_pi;

    const std::string text = example_settings_json();
    check(text.find("\"type\": \"stdin\"") != std::string::npos, "example has stdin");
    check(text.find("\"type\": \"tty\"") != std::string::npos, "example has tty");

    Settings s;
    std::vector<std::string> warnings;
    check(parse_settings(text, s, &warnings), "example parses");
    check(warnings.empty(), "example produces no warnings");
    check(s.input.size() == 1 && s.input[0].type == "stdin", "example input round-trip");
    check(s.display.size() == 1 && s.display[0].cols == 80 && s.display[0].rows == 25,
          "example display round-trip");
    check(s.scan.channels.size() == 38, "example scan round-trip");
    check(read_file(EXAMPLE_JSON) == text, "examples/si468x-pi.json is the --config-example output");
}

void test_missing_settings()
{
    using namespace si468x_pi;

    Settings s;
    std::vector<std::string> warnings;
    check(load_settings("/tmp/si468x-pi-does-not-exist.json", s, &warnings),
          "missing settings file is not an error");
    check(warnings.empty(), "missing settings file does not warn");
}

void test_cache()
{
    using namespace si468x_pi;

    char tmpl[] = "/tmp/si468x-pi-test-XXXXXX";
    char* dir = mkdtemp(tmpl);
    check(dir != nullptr, "create temp directory");
    if (!dir) {
        return;
    }
    const std::string base(dir);
    const std::string path = base + "/cache/stations.json";

    Cache cache;
    cache.volume = 42;
    cache.complete = false;
    cache.last_played.channel = "12A";
    cache.last_played.service_id = 17361;
    cache.stations = {
        Station { "12A", "RTS Première", "RTS Prem", 17361, 0x1234, false },
        Station { "12A", "RTS Première", "RTS Prem", 17361, 0x1234, false },
        Station { "5A", "Data Service", "Data", 99, 0, true },
        Station { "99Z", "Dropped", "Dropped", 100, 0, false },
    };

    std::string error;
    check(save_cache(path, cache, &error), "cache saves: " + error);
    check(read_file(path).find("\"version\": 1") != std::string::npos, "cache has version 1");
    check(access((path + ".tmp").c_str(), F_OK) != 0, "temporary file is renamed");

    Cache loaded;
    std::vector<std::string> warnings;
    check(load_cache(path, loaded, &warnings), "cache loads");
    check(warnings.empty(), "clean cache produces no warnings");
    check(loaded.volume == 42, "volume round-trip");
    check(!loaded.complete, "complete flag round-trip");
    check(loaded.last_played.valid() && loaded.last_played.service_id == 17361,
          "last played round-trip");
    check(loaded.stations.size() == 2, "duplicates and unknown channels are dropped");
    check(loaded.stations[0].program == "Data Service" && loaded.stations[0].data,
          "data service kept and flagged");
    check(loaded.stations[1].program == "RTS Première", "UTF-8 label round-trip");
    check(loaded.stations[1].component_id == 0x1234, "component id round-trip");

    Cache fm_cache;
    fm_cache.last_played.channel = "88.5";
    fm_cache.last_played.service_id = 88500000;
    fm_cache.last_played.band = "fm";
    fm_cache.stations = {
        Station { "88.5", "88.5 MHz", "88.5 MHz", 88500000, 0, false, "fm" },
    };
    const std::string fm_path = base + "/cache/stations-fmhd.json";
    check(save_cache(fm_path, fm_cache, &error), "fm cache saves");
    Cache fm_loaded;
    warnings.clear();
    check(load_cache(fm_path, fm_loaded, &warnings), "fm cache loads");
    check(fm_loaded.last_played.valid() && fm_loaded.last_played.service_id == 88500000 && fm_loaded.last_played.band == "fm",
          "fm last played round-trip");
    unlink(fm_path.c_str());

    write_file(path, "{\"version\": 2}");
    Cache future;
    warnings.clear();
    check(load_cache(path, future, &warnings), "future cache version is not fatal");
    check(future.stations.empty(), "future cache version starts empty");
    check(contains(warnings, "version"), "future cache version warns");

    write_file(path, "{broken");
    Cache broken;
    warnings.clear();
    check(load_cache(path, broken, &warnings), "malformed cache is not fatal");
    check(broken.stations.empty(), "malformed cache starts empty");
    check(!warnings.empty(), "malformed cache warns");

    Cache missing;
    warnings.clear();
    check(load_cache(base + "/missing.json", missing, &warnings), "missing cache is fine");
    check(missing.stations.empty(), "missing cache is empty");

    unlink(path.c_str());
    rmdir((base + "/cache").c_str());
    rmdir(base.c_str());
}

} // namespace

int main()
{
    std::cout << "si468x-pi settings tests" << std::endl;

    test_channels();
    test_normalize();
    test_defaults();
    test_parse_and_validate();
    test_malformed();
    test_paths();
    test_example();
    test_missing_settings();
    test_cache();

    return report("settings");
}
