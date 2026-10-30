// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
// Key map and configuration of the evdev input plugin: the
// "input" entry of si468x-pi.json, key code -> InputAction
#ifndef SI468X_PI_REMOTE_CONFIG_HPP
#define SI468X_PI_REMOTE_CONFIG_HPP

#include <map>
#include <string>

#include "si468x-pi/plugin.h"

/* One "input" entry of si468x-pi.json, see docs/si468x-pi.md. Key codes are the Linux input event
 * codes (linux/input-event-codes.h), the map holds the actions of
 * enum si468x_pi_action and -1 for a key that is deliberately ignored. */
struct RemoteConfig {
    std::string device;     // "" = find a device, else /dev/input/eventN
    std::string name;       // "" = any device name, else a case-insensitive part of it
    bool grab = true;       // EVIOCGRAB: keep the keys away from the console
    std::map<int, int> map; // key code -> si468x_pi_action, -1 = ignored
};

/* Fills cfg with the defaults for the HiFiBerry remote and applies the JSON
 * object the plugin got as config. Returns false with a message in error if
 * the entry is unusable; cfg then holds the defaults. An empty config is
 * valid. */
bool parseRemoteConfig(const std::string& json, RemoteConfig& cfg, std::string& error);

/* True if the key code is in the map, then action is the si468x_pi_action or
 * -1 for a key that is recognised but ignored. False if the key is unknown. */
bool lookupKey(const RemoteConfig& cfg, int code, int& action);

/* "KEY_UP" <-> 103. keyCodeFromName also accepts a decimal number, as printed
 * by evtest and by the plugin's log. nullptr/false if unknown. */
const char* keyName(int code);
bool keyCodeFromName(const std::string& name, int& code);

/* "up", "down", "left", "right", "enter", "menu", "quit", "volume_up",
 * "volume_down", "mute", "home", "none"; nullptr/false if unknown. */
const char* actionName(int action);
bool actionFromName(const std::string& name, int& action);

/* True for the actions a button that is held down repeats: up, down,
 * volume_up and volume_down. Everything else happens once per press, so that
 * a long press on enter, menu, home, mute or quit cannot repeat. */
bool repeatableAction(int action);

#endif // SI468X_PI_REMOTE_CONFIG_HPP
