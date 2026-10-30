// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "si468x-pi/input_plugin.h"

#include <map>
#include <string>

namespace si468x_pi {

struct EvdevConfig {
    std::string device;
    bool grab = false;
    std::map<int, enum si468x_pi_action> key_map;
};

EvdevConfig parse_evdev_config(const char* json_str);
int key_code_from_name(const std::string& name);
enum si468x_pi_action action_from_name(const std::string& name);

} // namespace si468x_pi
