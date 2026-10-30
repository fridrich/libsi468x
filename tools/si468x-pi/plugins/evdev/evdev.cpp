// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "si468x-pi/plugin.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <map>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" {
const char si468x_pi_plugin_name[] = "evdev";
const char si468x_pi_plugin_description[] = "Linux evdev input (/dev/input/event*)";
const int si468x_pi_input_abi = SI468X_PI_INPUT_ABI;

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config);
}

namespace {

using KeyMap = std::map<int, enum si468x_pi_action>;

const std::map<std::string, int> s_key_names = {
    { "KEY_UP", KEY_UP },
    { "KEY_DOWN", KEY_DOWN },
    { "KEY_LEFT", KEY_LEFT },
    { "KEY_RIGHT", KEY_RIGHT },
    { "KEY_ENTER", KEY_ENTER },
    { "KEY_OK", KEY_OK },
    { "KEY_MENU", KEY_MENU },
    { "KEY_ESC", KEY_ESC },
    { "KEY_BACK", KEY_BACK },
    { "KEY_POWER", KEY_POWER },
    { "KEY_VOLUMEUP", KEY_VOLUMEUP },
    { "KEY_VOLUMEDOWN", KEY_VOLUMEDOWN },
    { "KEY_MUTE", KEY_MUTE },
    { "KEY_HOMEPAGE", KEY_HOMEPAGE },
    { "KEY_HOME", KEY_HOME },
};

const std::map<std::string, enum si468x_pi_action> s_action_names = {
    { "UP", SI468X_PI_UP },
    { "DOWN", SI468X_PI_DOWN },
    { "LEFT", SI468X_PI_LEFT },
    { "RIGHT", SI468X_PI_RIGHT },
    { "ENTER", SI468X_PI_ENTER },
    { "MENU", SI468X_PI_MENU },
    { "QUIT", SI468X_PI_QUIT },
    { "VOLUME_UP", SI468X_PI_VOLUME_UP },
    { "VOLUME_DOWN", SI468X_PI_VOLUME_DOWN },
    { "MUTE", SI468X_PI_MUTE },
    { "HOME", SI468X_PI_HOME },
};

const KeyMap s_default_map = {
    { KEY_UP, SI468X_PI_UP },
    { KEY_DOWN, SI468X_PI_DOWN },
    { KEY_LEFT, SI468X_PI_LEFT },
    { KEY_RIGHT, SI468X_PI_RIGHT },
    { KEY_ENTER, SI468X_PI_ENTER },
    { KEY_OK, SI468X_PI_ENTER },
    { KEY_MENU, SI468X_PI_MENU },
    { KEY_ESC, SI468X_PI_QUIT },
    { KEY_BACK, SI468X_PI_MENU },
    { KEY_POWER, SI468X_PI_QUIT },
    { KEY_VOLUMEUP, SI468X_PI_VOLUME_UP },
    { KEY_VOLUMEDOWN, SI468X_PI_VOLUME_DOWN },
    { KEY_MUTE, SI468X_PI_MUTE },
    { KEY_HOMEPAGE, SI468X_PI_HOME },
    { KEY_HOME, SI468X_PI_HOME },
};

struct EvdevConfig {
    std::string device;
    bool grab = false;
    KeyMap key_map = s_default_map;
};

// {"device": "...", "grab": true, "map": {"KEY_UP": "UP", "115": "VOLUME_UP"}};
// keys by name or code, merged over the defaults; anything malformed is ignored
EvdevConfig parse_config(const char* json_str)
{
    EvdevConfig cfg;
    const nlohmann::json j = nlohmann::json::parse(json_str ? json_str : "", nullptr, false);
    if (!j.is_object()) {
        return cfg;
    }
    try {
        cfg.device = j.value("device", "");
        cfg.grab = j.value("grab", false);
    } catch (const nlohmann::json::exception&) { // wrong value types
        return EvdevConfig{};
    }
    const auto map = j.find("map");
    if (map == j.end() || !map->is_object()) {
        return cfg;
    }
    for (const auto& item : map->items()) {
        const auto name = s_key_names.find(item.key());
        int code = -1;
        if (name != s_key_names.end()) {
            code = name->second;
        } else {
            try {
                code = std::stoi(item.key());
            } catch (...) {
            }
        }
        const auto act = item.value().is_string() ? s_action_names.find(item.value().get<std::string>())
                                                  : s_action_names.end();
        if (code >= 0 && act != s_action_names.end()) {
            cfg.key_map[code] = act->second;
        }
    }
    return cfg;
}

int find_best_device(const KeyMap& key_map)
{
    int best_fd = -1;
    int best_matches = 0;

    for (int i = 0; i < 32; ++i) {
        const std::string path = "/dev/input/event" + std::to_string(i);
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        uint8_t key_bits[(KEY_MAX + 7) / 8] = {};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
            close(fd);
            continue;
        }

        int matches = 0;
        for (const auto& kv : key_map) {
            int code = kv.first;
            if (code >= 0 && code <= KEY_MAX) {
                if (key_bits[code / 8] & (1 << (code % 8))) {
                    ++matches;
                }
            }
        }

        if (matches > best_matches) {
            if (best_fd >= 0) {
                close(best_fd);
            }
            best_fd = fd;
            best_matches = matches;
        } else {
            close(fd);
        }
    }

    return best_fd;
}

} // namespace

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config)
{
    if (!host) {
        return -1;
    }

    const EvdevConfig cfg = parse_config(config);

    int fd = -1;
    if (!cfg.device.empty()) {
        fd = open(cfg.device.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "evdev: failed to open " << cfg.device << std::endl;
            return 0; // Skip missing device cleanly without terminating radio
        }
    } else {
        fd = find_best_device(cfg.key_map);
        if (fd < 0) {
            std::cerr << "evdev: no matching /dev/input/event* device found" << std::endl;
            return 0;
        }
    }

    if (cfg.grab) {
        ioctl(fd, EVIOCGRAB, 1);
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!host->stop_requested(host->ctx)) {
        int pr = poll(&pfd, 1, 100);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            continue;
        }

        struct input_event ev[16];
        ssize_t bytes = read(fd, ev, sizeof(ev));
        if (bytes <= 0) {
            break;
        }

        const size_t count = static_cast<size_t>(bytes) / sizeof(struct input_event);
        for (size_t i = 0; i < count; ++i) {
            if (ev[i].type == EV_KEY && (ev[i].value == 1 || ev[i].value == 2)) {
                auto it = cfg.key_map.find(ev[i].code);
                if (it != cfg.key_map.end()) {
                    host->push(host->ctx, it->second);
                }
            }
        }
    }

    if (cfg.grab) {
        ioctl(fd, EVIOCGRAB, 0);
    }
    close(fd);
    return 0;
}
