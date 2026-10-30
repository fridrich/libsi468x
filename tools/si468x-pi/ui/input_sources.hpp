// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "input_queue.hpp"

#include <functional>

namespace si468x_pi {

using StopPredicate = std::function<bool()>;

// Terminal keys on stdin. Runs until stop_requested() or EOF.
void runStdinReader(InputQueue& queue, const StopPredicate& stop_requested);

} // namespace si468x_pi
