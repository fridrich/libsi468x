// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

namespace si468x_pi {

enum class InputAction {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    ENTER,
    MENU,
    QUIT,
    VOLUME_UP,
    VOLUME_DOWN,
    MUTE,
    HOME,
    NONE,
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
