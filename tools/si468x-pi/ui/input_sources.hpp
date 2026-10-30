// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/settings.hpp"
#include "input_queue.hpp"

#include <functional>
#include <thread>
#include <vector>

namespace si468x_pi {

using StopPredicate = std::function<bool()>;

// Terminal keys on stdin. Runs until stop_requested() or EOF.
void runStdinReader(InputQueue& queue, const StopPredicate& stop_requested);

// Starts the "input" entries (stdin or plugins); stdin alone when "input" is absent.
// stdin only when interactive. Plugins that fail to load are logged and skipped:
// the radio keeps playing without them. Join the threads after stop_requested() turns true.
std::vector<std::thread> startInputSources(const Settings& settings, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive);

} // namespace si468x_pi
