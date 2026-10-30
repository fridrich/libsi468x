// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_queue.hpp"

#include <chrono>

namespace si468x_pi {

void InputQueue::push(InputAction action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(action);
    m_cond.notify_one();
}

InputAction InputQueue::pop(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                        [this]() { return !m_queue.empty(); })) {
        InputAction action = m_queue.front();
        m_queue.pop();
        return action;
    }
    return InputAction::NONE;
}

} // namespace si468x_pi
