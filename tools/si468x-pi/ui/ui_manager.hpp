// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "display_interface.hpp"
#include "input_queue.hpp"
#include "ui_screen.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace si468x_pi {

class UIManager {
public:
    explicit UIManager(std::unique_ptr<IDisplay> display, int tick_ms = 100);
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    void setScreen(std::shared_ptr<UIScreen> newScreen);

    // Pushes a new screen onto the stack, pausing the previous top.
    void pushScreen(std::shared_ptr<UIScreen> screen);

    // Pops the top screen, resuming the screen below it. False when the stack
    // would become empty.
    bool popScreen();

    // Clears the history stack and resets back to the root.
    void resetToRoot(std::shared_ptr<UIScreen> rootScreen);
    void resetToRoot();

    void processInput(InputAction action);

    // The volume keys: the last line shows the level for a moment (a second
    // after the last key), then the screen has its line back.
    void showVolume(int percent, bool muted);

private:
    void run();

    std::unique_ptr<IDisplay> m_display;
    std::vector<std::shared_ptr<UIScreen>> m_historyStack;
    int m_tick_ms;
    std::atomic<bool> m_exit;
    std::atomic<bool> m_screenChanged; // render now, not at the next tick
    std::atomic<uint64_t> m_lastInputTime;
    std::atomic<int> m_volumePercent { -1 };
    std::atomic<bool> m_volumeMuted { false };
    std::atomic<uint64_t> m_volumeUntil { 0 };
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;
};

// The line the volume keys put over the last line of the screen: a bar of
// block characters as wide as the screen, or "Muted"; exactly cols code points.
std::string volumeLine(int cols, int percent, bool muted);

} // namespace si468x_pi
