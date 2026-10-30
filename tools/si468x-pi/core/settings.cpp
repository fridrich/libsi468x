// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace si468x_pi {

using json = nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace {

void add_warning(std::vector<std::string>* warnings, const std::string& text)
{
    if (warnings) {
        warnings->push_back(text);
    }
}

std::string env_or_empty(const char* name)
{
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

std::string read_string(const json& obj, const char* key, const std::string& fallback,
                        const std::string& where, std::vector<std::string>* warnings)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (!it->is_string()) {
        add_warning(warnings, where + "." + key + ": expected a string, using default");
        return fallback;
    }
    return it->get<std::string>();
}

int read_int(const json& obj, const char* key, int fallback, int lo, int hi,
             const std::string& where, std::vector<std::string>* warnings)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        add_warning(warnings, where + "." + key + ": expected an integer, using default");
        return fallback;
    }
    long long v = it->get<long long>();
    if (v < lo || v > hi) {
        add_warning(warnings, where + "." + key + ": out of range, clamped");
        v = v < lo ? lo : hi;
    }
    return static_cast<int>(v);
}

bool read_bool(const json& obj, const char* key, bool fallback, const std::string& where,
               std::vector<std::string>* warnings)
{
    auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (!it->is_boolean()) {
        add_warning(warnings, where + "." + key + ": expected true/false, using default");
        return fallback;
    }
    return it->get<bool>();
}

std::string read_enum(const json& obj, const char* key, const std::string& fallback,
                      const std::vector<std::string>& allowed, const std::string& where,
                      std::vector<std::string>* warnings)
{
    std::string v = read_string(obj, key, fallback, where, warnings);
    for (const std::string& a : allowed) {
        if (v == a) {
            return v;
        }
    }
    add_warning(warnings, where + "." + key + ": unknown value '" + v + "', using default");
    return fallback;
}

bool parse_json_text(const std::string& text, json& out, const std::string& path,
                     std::vector<std::string>* warnings)
{
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        add_warning(warnings, path + ": malformed JSON, using defaults");
        out = json::object();
        return true;
    }
    if (!out.is_object()) {
        add_warning(warnings, path + ": top level is not an object, using defaults");
        out = json::object();
    }
    return true;
}

void parse_inputs(const json& root, Settings& out, std::vector<std::string>* warnings)
{
    auto it = root.find("input");
    if (it == root.end()) {
        return;
    }
    out.input_configured = true;
    out.input.clear();

    if (!it->is_array()) {
        add_warning(warnings, "input: expected an array, starting no input source");
        return;
    }

    int index = 0;
    for (const json& entry : *it) {
        const std::string where = "input[" + std::to_string(index++) + "]";
        if (!entry.is_object()) {
            add_warning(warnings, where + ": expected an object, skipped");
            continue;
        }
        InputConfig cfg;
        cfg.type = read_string(entry, "type", "", where, warnings);
        cfg.plugin = read_string(entry, "plugin", "", where, warnings);
        cfg.device = read_string(entry, "device", "", where, warnings);
        cfg.config_json = entry.dump();

        if (cfg.type.empty() && cfg.plugin.empty()) {
            add_warning(warnings, where + ": needs \"type\" or \"plugin\", skipped");
            continue;
        }
        if (!cfg.type.empty() && cfg.type != "stdin") {
            add_warning(warnings, where + ": unknown built-in type '" + cfg.type + "', skipped");
            continue;
        }
        out.input.push_back(cfg);
    }
}

void parse_displays(const json& root, Settings& out, std::vector<std::string>* warnings)
{
    auto it = root.find("display");
    if (it == root.end()) {
        out.display.clear();
        out.display.push_back(DisplayConfig{});
        return;
    }
    out.display_configured = true;
    out.display.clear();

    if (!it->is_array()) {
        add_warning(warnings, "display: expected an array, using no display");
        return;
    }

    int index = 0;
    for (const json& entry : *it) {
        const std::string where = "display[" + std::to_string(index++) + "]";
        if (!entry.is_object()) {
            add_warning(warnings, where + ": expected an object, skipped");
            continue;
        }
        DisplayConfig cfg;
        cfg.type = read_string(entry, "type", "", where, warnings);
        cfg.plugin = read_string(entry, "plugin", "", where, warnings);
        cfg.device = read_string(entry, "device", "", where, warnings);
        cfg.cols = read_int(entry, "cols", cfg.cols, 1, 1000, where, warnings);
        cfg.rows = read_int(entry, "rows", cfg.rows, 1, 1000, where, warnings);
        cfg.config_json = entry.dump();

        if (cfg.type.empty() && cfg.plugin.empty()) {
            add_warning(warnings, where + ": needs \"type\" or \"plugin\", skipped");
            continue;
        }
        if (!cfg.type.empty() && cfg.type != "tty") {
            add_warning(warnings, where + ": unknown built-in type '" + cfg.type + "', skipped");
            continue;
        }
        out.display.push_back(cfg);
    }
}

void parse_scan(const json& root, Settings& out, std::vector<std::string>* warnings)
{
    auto it = root.find("scan");
    if (it == root.end() || !it->is_object()) {
        if (it != root.end()) {
            add_warning(warnings, "scan: expected an object, using defaults");
        }
        return;
    }
    const json& scan = *it;

    auto ch = scan.find("channels");
    if (ch != scan.end()) {
        if (!ch->is_array()) {
            add_warning(warnings, "scan.channels: expected an array, using defaults");
        } else {
            std::vector<std::string> list;
            int index = 0;
            for (const json& label : *ch) {
                const std::string where = "scan.channels[" + std::to_string(index++) + "]";
                if (!label.is_string()) {
                    add_warning(warnings, where + ": expected a string, skipped");
                    continue;
                }
                const std::string text = label.get<std::string>();
                if (!find_channel(text)) {
                    add_warning(warnings, where + ": unknown channel '" + text + "', skipped");
                    continue;
                }
                list.push_back(text);
            }
            if (list.empty()) {
                add_warning(warnings, "scan.channels: no valid channels, using the default list");
            } else {
                out.scan.channels.swap(list);
            }
        }
    }

    out.scan.rssi_min = read_int(scan, "rssi_min", out.scan.rssi_min, 0, 100, "scan", warnings);
    out.scan.include_data = read_bool(scan, "include_data", out.scan.include_data, "scan", warnings);
}

void parse_settings_object(const json& root, Settings& out, std::vector<std::string>* warnings)
{
    auto dev = root.find("device");
    if (dev != root.end()) {
        if (!dev->is_object()) {
            add_warning(warnings, "device: expected an object, using defaults");
        } else {
            out.device.spi = read_string(*dev, "spi", out.device.spi, "device", warnings);
            out.device.reset_gpio =
                read_int(*dev, "reset_gpio", out.device.reset_gpio, 0, 53, "device", warnings);
            out.device.mode =
                read_enum(*dev, "mode", out.device.mode, { "dab", "fmhd" }, "device", warnings);
            out.device.audio = read_enum(*dev, "audio", out.device.audio,
                                         { "analog", "i2s", "both" }, "device", warnings);
            out.device.region = read_enum(*dev, "region", out.device.region,
                                          { "europe", "us" }, "device", warnings);
        }
    }

    parse_inputs(root, out, warnings);
    parse_displays(root, out, warnings);
    parse_scan(root, out, warnings);

    auto vol = root.find("volume");
    if (vol != root.end()) {
        if (!vol->is_object()) {
            add_warning(warnings, "volume: expected an object, using defaults");
        } else {
            out.volume.start = read_int(*vol, "start", out.volume.start, 0, 100, "volume", warnings);
            out.volume.step = read_int(*vol, "step", out.volume.step, 1, 100, "volume", warnings);
        }
    }

    auto ui = root.find("ui");
    if (ui != root.end()) {
        if (!ui->is_object()) {
            add_warning(warnings, "ui: expected an object, using defaults");
        } else {
            out.ui.tick_ms = read_int(*ui, "tick_ms", out.ui.tick_ms, 50, 2000, "ui", warnings);
            out.ui.mot = read_bool(*ui, "mot", out.ui.mot, "ui", warnings);
        }
    }

    auto poll = root.find("poll");
    if (poll != root.end()) {
        if (!poll->is_object()) {
            add_warning(warnings, "poll: expected an object, using defaults");
        } else {
            out.poll.signal_ms =
                read_int(*poll, "signal_ms", out.poll.signal_ms, 500, 60000, "poll", warnings);
            out.poll.dls_ms = read_int(*poll, "dls_ms", out.poll.dls_ms, 200, 60000, "poll", warnings);
            out.poll.info_ms =
                read_int(*poll, "info_ms", out.poll.info_ms, 1000, 600000, "poll", warnings);
        }
    }
}

ojson defaults_to_json(const Settings& s)
{
    ojson root;
    root["device"] = {
        { "spi", s.device.spi },
        { "reset_gpio", s.device.reset_gpio },
        { "mode", s.device.mode },
        { "audio", s.device.audio },
        { "region", s.device.region },
    };
    root["input"] = ojson::array({ ojson{ { "type", "stdin" } } });
    root["display"] = ojson::array({
        ojson{ { "type", "tty" }, { "cols", 78 }, { "rows", 22 } },
    });
    root["scan"] = {
        { "channels", s.scan.channels },
        { "rssi_min", s.scan.rssi_min },
        { "include_data", s.scan.include_data },
    };
    root["volume"] = {
        { "start", s.volume.start },
        { "step", s.volume.step },
    };
    root["ui"] = {
        { "tick_ms", s.ui.tick_ms },
        { "mot", s.ui.mot },
    };
    root["poll"] = {
        { "signal_ms", s.poll.signal_ms },
        { "dls_ms", s.poll.dls_ms },
        { "info_ms", s.poll.info_ms },
    };
    return root;
}

bool write_all(int fd, const std::string& data)
{
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

std::string default_settings_path()
{
    std::string base = env_or_empty("XDG_CONFIG_HOME");
    if (base.empty()) {
        std::string home = env_or_empty("HOME");
        if (home.empty()) {
            return std::string();
        }
        base = home + "/.config";
    }
    return base + "/si468x-pi/si468x-pi.json";
}

std::string default_cache_path(const std::string& mode)
{
    std::string base = env_or_empty("XDG_CACHE_HOME");
    if (base.empty()) {
        std::string home = env_or_empty("HOME");
        if (home.empty()) {
            return std::string();
        }
        base = home + "/.cache";
    }
    if (mode == "fmhd") {
        return base + "/si468x-pi/stations-fmhd.json";
    }
    return base + "/si468x-pi/stations.json";
}

bool parse_settings(const std::string& text, Settings& out, std::vector<std::string>* warnings)
{
    out = Settings{};
    json root;
    if (!parse_json_text(text, root, "settings", warnings)) {
        return false;
    }
    parse_settings_object(root, out, warnings);
    return true;
}

bool load_settings(const std::string& path, Settings& out, std::vector<std::string>* warnings)
{
    out = Settings{};

    if (path.empty()) {
        add_warning(warnings, "no settings path (HOME and XDG_CONFIG_HOME unset), using defaults");
        return true;
    }

    std::ifstream in(path);
    if (!in) {
        if (errno == ENOENT) {
            return true;
        }
        add_warning(warnings, path + ": cannot read settings, using defaults");
        return false;
    }

    std::ostringstream buf;
    buf << in.rdbuf();
    json root;
    if (!parse_json_text(buf.str(), root, path, warnings)) {
        return false;
    }
    out = Settings{};
    parse_settings_object(root, out, warnings);
    return true;
}

std::string example_settings_json()
{
    return defaults_to_json(Settings{}).dump(2) + "\n";
}

bool make_parent_dirs(const std::string& path, std::string* error)
{
    size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0) {
        return true;
    }
    const std::string dir = path.substr(0, slash);

    std::string current;
    size_t pos = 0;
    while (pos < dir.size()) {
        size_t next = dir.find('/', pos);
        if (next == std::string::npos) {
            next = dir.size();
        }
        current = dir.substr(0, next);
        if (!current.empty()) {
            if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                if (error) {
                    *error = current + ": " + std::strerror(errno);
                }
                return false;
            }
        }
        pos = next + 1;
    }
    return true;
}

bool load_cache(const std::string& path, Cache& out, std::vector<std::string>* warnings)
{
    out = Cache{};

    if (path.empty()) {
        return true;
    }

    std::ifstream in(path);
    if (!in) {
        if (errno == ENOENT) {
            return true;
        }
        add_warning(warnings, path + ": cannot read cache, starting empty");
        return false;
    }

    std::ostringstream buf;
    buf << in.rdbuf();
    json root = json::parse(buf.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        add_warning(warnings, path + ": malformed cache, starting empty");
        return true;
    }

    int version = 1;
    auto it = root.find("version");
    if (it != root.end() && it->is_number_integer()) {
        version = it->get<int>();
    }
    if (version != 1) {
        add_warning(warnings, path + ": unsupported cache version " + std::to_string(version)
                                  + ", starting empty");
        return true;
    }

    out.version = 1;
    out.complete = read_bool(root, "complete", true, "cache", warnings);

    auto lp = root.find("last_played");
    if (lp != root.end() && lp->is_object()) {
        std::string channel = read_string(*lp, "channel", "", "last_played", warnings);
        uint32_t sid = 0;
        auto sid_it = lp->find("service_id");
        if (sid_it != lp->end() && sid_it->is_number_unsigned()) {
            sid = sid_it->get<uint32_t>();
        } else if (sid_it != lp->end() && sid_it->is_number_integer()) {
            long long v = sid_it->get<long long>();
            sid = v > 0 ? static_cast<uint32_t>(v) : 0;
        }
        std::string band = read_string(*lp, "band", "dab", "last_played", warnings);
        if (!channel.empty() && sid != 0 && (band == "fm" || find_channel(channel))) {
            out.last_played.channel = channel;
            out.last_played.service_id = sid;
        }
    }

    auto vol = root.find("volume");
    if (vol != root.end() && vol->is_number_integer()) {
        int v = vol->get<int>();
        if (v >= 0 && v <= 63) {
            out.volume = v;
        } else {
            add_warning(warnings, "cache.volume: out of range, ignored");
        }
    }

    auto stations = root.find("stations");
    if (stations != root.end()) {
        if (!stations->is_array()) {
            add_warning(warnings, "cache.stations: expected an array, ignored");
        } else {
            int index = 0;
            for (const json& entry : *stations) {
                const std::string where = "station[" + std::to_string(index++) + "]";
                if (!entry.is_object()) {
                    add_warning(warnings, where + ": expected an object, skipped");
                    continue;
                }
                Station s;
                s.channel = read_string(entry, "channel", "", where, warnings);
                s.program = read_string(entry, "program", "", where, warnings);
                s.short_program = read_string(entry, "short_program", "", where, warnings);
                auto sid = entry.find("service_id");
                if (sid != entry.end() && sid->is_number_unsigned()) {
                    s.service_id = sid->get<uint32_t>();
                } else if (sid != entry.end() && sid->is_number_integer()) {
                    long long v = sid->get<long long>();
                    s.service_id = v > 0 ? static_cast<uint32_t>(v) : 0;
                }
                auto comp = entry.find("component_id");
                if (comp != entry.end() && comp->is_number_integer()) {
                    long long v = comp->get<long long>();
                    if (v >= 0 && v <= 0xFFFF) {
                        s.component_id = static_cast<uint16_t>(v);
                    }
                }
                s.data = read_bool(entry, "data", false, where, warnings);
                s.band = read_string(entry, "band", "dab", where, warnings);
                if (s.program.empty() || s.service_id == 0) {
                    add_warning(warnings, where + ": missing program or service_id, skipped");
                    continue;
                }
                out.stations.push_back(s);
            }
            normalize_stations(out.stations);
        }
    }

    return true;
}

bool save_cache(const std::string& path, const Cache& cache, std::string* error)
{
    if (path.empty()) {
        if (error) {
            *error = "empty cache path";
        }
        return false;
    }
    if (!make_parent_dirs(path, error)) {
        return false;
    }

    Cache copy = cache;
    normalize_stations(copy.stations);

    ojson root;
    root["version"] = 1;
    if (copy.last_played.valid()) {
        root["last_played"] = {
            { "channel", copy.last_played.channel },
            { "service_id", copy.last_played.service_id },
        };
    }
    if (copy.volume >= 0) {
        root["volume"] = copy.volume;
    }
    root["complete"] = copy.complete;
    ojson stations = ojson::array();
    for (const Station& s : copy.stations) {
        stations.push_back(ojson{
            { "channel", s.channel },
            { "program", s.program },
            { "short_program", s.short_program },
            { "service_id", s.service_id },
            { "component_id", s.component_id },
            { "data", s.data },
            { "band", s.band },
        });
    }
    root["stations"] = std::move(stations);

    const std::string text = root.dump(2) + "\n";
    const std::string tmp = path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        if (error) {
            *error = tmp + ": " + std::strerror(errno);
        }
        return false;
    }

    bool ok = write_all(fd, text);
    if (ok) {
        ok = ::fsync(fd) == 0;
    }
    int saved_errno = errno;
    ::close(fd);

    if (!ok) {
        ::unlink(tmp.c_str());
        if (error) {
            *error = tmp + ": " + std::strerror(saved_errno);
        }
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        int rename_errno = errno;
        ::unlink(tmp.c_str());
        if (error) {
            *error = path + ": " + std::strerror(rename_errno);
        }
        return false;
    }
    return true;
}

} // namespace si468x_pi
