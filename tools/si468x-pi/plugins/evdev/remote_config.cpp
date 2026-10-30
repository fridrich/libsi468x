// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote_config.hpp"

#ifdef SI468X_PI_VENDORED_JSON
#include "nlohmann/json.hpp"
#else
#include <nlohmann/json.hpp>
#endif

#include <linux/input.h>

namespace si468x_pi {

namespace {

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

void populate_default_map(std::map<int, enum si468x_pi_action>& map)
{
    map[KEY_UP] = SI468X_PI_UP;
    map[KEY_DOWN] = SI468X_PI_DOWN;
    map[KEY_LEFT] = SI468X_PI_LEFT;
    map[KEY_RIGHT] = SI468X_PI_RIGHT;
    map[KEY_ENTER] = SI468X_PI_ENTER;
    map[KEY_OK] = SI468X_PI_ENTER;
    map[KEY_MENU] = SI468X_PI_MENU;
    map[KEY_ESC] = SI468X_PI_QUIT;
    map[KEY_BACK] = SI468X_PI_MENU;
    map[KEY_POWER] = SI468X_PI_QUIT;
    map[KEY_VOLUMEUP] = SI468X_PI_VOLUME_UP;
    map[KEY_VOLUMEDOWN] = SI468X_PI_VOLUME_DOWN;
    map[KEY_MUTE] = SI468X_PI_MUTE;
    map[KEY_HOMEPAGE] = SI468X_PI_HOME;
    map[KEY_HOME] = SI468X_PI_HOME;
}

} // namespace

int key_code_from_name(const std::string& name)
{
    const auto it = s_key_names.find(name);
    return it != s_key_names.end() ? it->second : -1;
}

enum si468x_pi_action action_from_name(const std::string& name)
{
    const auto it = s_action_names.find(name);
    return it != s_action_names.end() ? it->second : SI468X_PI_ACTION_COUNT;
}

EvdevConfig parse_evdev_config(const char* json_str)
{
    EvdevConfig cfg;
    populate_default_map(cfg.key_map);

    if (!json_str || !*json_str) {
        return cfg;
    }

    try {
        const nlohmann::json j = nlohmann::json::parse(json_str);
        if (j.is_object()) {
            cfg.device = j.value("device", "");
            cfg.grab = j.value("grab", false);
            const auto it = j.find("map");
            if (it != j.end() && it->is_object()) {
                for (const auto& item : it->items()) {
                    int code = key_code_from_name(item.key());
                    if (code < 0) {
                        try {
                            code = std::stoi(item.key());
                        } catch (...) {
                            code = -1;
                        }
                    }
                    if (code >= 0 && item.value().is_string()) {
                        const enum si468x_pi_action act = action_from_name(item.value().get<std::string>());
                        if (act != SI468X_PI_ACTION_COUNT) {
                            cfg.key_map[code] = act;
                        }
                    }
                }
            }
        }
    } catch (...) {
        // Fallback to default
    }

    return cfg;
}

} // namespace si468x_pi
