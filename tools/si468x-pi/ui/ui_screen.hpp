// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "display_interface.hpp"
#include "input_queue.hpp"

namespace si468x_pi {

class UIScreen {
public:
    virtual ~UIScreen() = default;

    // UI thread, every tick while on top of the stack. Must not block.
    virtual void render(Frame& frame) = 0;
    virtual void handleInput(InputAction action) = 0;
};

} // namespace si468x_pi
