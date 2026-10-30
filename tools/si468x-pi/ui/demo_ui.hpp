// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "settings.hpp"
#include "station.hpp"

namespace si468x_pi {

// Runs the real UI stack with synthetic station data. No chip access.
int run_demo_ui(const Settings& settings, const Cache& cache);

} // namespace si468x_pi
