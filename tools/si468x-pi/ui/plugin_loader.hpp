// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace si468x_pi {

/* kind "input" or "display", name e.g. "example":
 * Searches:
 *   1. <directory of the executable>/si468x-pi-plugins/si468x-pi-<name>.so
 *   2. <SI468X_PI_PLUGIN_DIR>/si468x-pi-<name>.so
 *   3. If name contains '/', it is treated as a direct file path.
 *
 * Checks si468x_pi_<kind>_abi == abi, logs name and description.
 * Returns the dlopen() handle, never unloaded; nullptr after logging why.
 */
void* loadPlugin(const std::string& kind, const std::string& name, int abi);

} // namespace si468x_pi
