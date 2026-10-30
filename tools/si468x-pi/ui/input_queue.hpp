// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "si468x-pi/plugin.h"

#include <condition_variable>
#include <mutex>
#include <queue>

namespace si468x_pi {

// Same values as the plugin ABI, so plugin actions convert directly.
enum class InputAction {
    UP = SI468X_PI_UP,
    DOWN = SI468X_PI_DOWN,
    LEFT = SI468X_PI_LEFT,
    RIGHT = SI468X_PI_RIGHT,
    ENTER = SI468X_PI_ENTER,
    MENU = SI468X_PI_MENU,
    QUIT = SI468X_PI_QUIT,
    VOLUME_UP = SI468X_PI_VOLUME_UP,
    VOLUME_DOWN = SI468X_PI_VOLUME_DOWN,
    MUTE = SI468X_PI_MUTE,
    HOME = SI468X_PI_HOME,
    NONE = SI468X_PI_ACTION_COUNT,
};

class InputQueue {
public:
    void push(InputAction action);
    InputAction pop(int timeout_ms = 500);

private:
    std::queue<InputAction> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
};

} // namespace si468x_pi
