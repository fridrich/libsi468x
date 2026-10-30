// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/settings.hpp"
#include "input_queue.hpp"

#ifdef SI468X_PI_VENDORED_JSON
#include "nlohmann/json.hpp"
#else
#include <nlohmann/json.hpp>
#endif

#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace si468x_pi {

using StopPredicate = std::function<bool()>;

// Terminal keys on stdin. Runs until stop_requested() or EOF.
void runStdinReader(InputQueue& queue, const StopPredicate& stop_requested);

// Starts the "input" entries (stdin or plugins).
// Bad entries are logged and skipped: the radio keeps playing without them.
// Join the threads after stop_requested() turns true.
std::vector<std::thread> startInputSources(const nlohmann::json& inputs, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive);

std::vector<std::thread> startInputSources(const Settings& settings, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive);

// True when startInputSources() would start at least one source: a plugin, or
// the terminal when interactive is true.
bool hasInputSources(const nlohmann::json& inputs, bool interactive);
bool hasInputSources(const Settings& settings, bool interactive);

} // namespace si468x_pi
