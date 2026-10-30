// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_manager.hpp"

#include "utils.hpp"

#include <algorithm>
#include <chrono>

namespace si468x_pi {

namespace {

// The volume level stays on the last line for a second after the last key, so
// that it is there while the key repeats and goes away after.
const uint64_t VOLUME_MS = 1000;

// U+2588 FULL BLOCK: the filled part of the volume bar.
const char* const BAR_FULL = "\xe2\x96\x88";

} // namespace

std::string volumeLine(int cols, int percent, bool muted)
{
    if (cols <= 0) {
        return std::string();
    }
    if (muted) {
        return utf8Fit("Muted", static_cast<size_t>(cols));
    }
    percent = std::max(0, std::min(100, percent));
    size_t full = (static_cast<size_t>(cols) * static_cast<size_t>(percent) + 50) / 100;
    if (percent > 0 && full == 0) {
        full = 1;
    }
    std::string line;
    for (size_t i = 0; i < static_cast<size_t>(cols); ++i) {
        line += i < full ? BAR_FULL : " ";
    }
    return line;
}

UIManager::UIManager(std::unique_ptr<IDisplay> display, int tick_ms)
    : m_display(std::move(display)),
      m_tick_ms(std::max(20, tick_ms)),
      m_exit(false),
      m_screenChanged(false),
      m_lastInputTime(now_ms())
{
    m_thread = std::thread(&UIManager::run, this);
}

UIManager::~UIManager()
{
    m_exit = true;
    m_cond.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    Frame blank(m_display->cols(), m_display->rows(), now_ms());
    blank.lines.assign(blank.rows, std::string(blank.cols, ' '));
    m_display->present(blank);
}

void UIManager::pushScreen(std::shared_ptr<UIScreen> screen)
{
    if (!screen) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastInputTime = now_ms();
        m_historyStack.push_back(screen);
        m_screenChanged = true;
    }
    m_cond.notify_one();
}

bool UIManager::popScreen()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastInputTime = now_ms();
        if (m_historyStack.size() <= 1) {
            return false;
        }
        m_historyStack.pop_back();
        m_screenChanged = true;
    }
    m_cond.notify_one();
    return true;
}

void UIManager::resetToRoot()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastInputTime = now_ms();
        if (m_historyStack.size() <= 1) {
            return;
        }
        m_historyStack.resize(1);
        m_screenChanged = true;
    }
    m_cond.notify_one();
}

void UIManager::processInput(InputAction action)
{
    // HOME is the one action the screens do not see: it returns to the root
    // screen (the radio) from wherever the user is.
    if (action == InputAction::HOME) {
        resetToRoot();
        return;
    }

    std::shared_ptr<UIScreen> screen;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastInputTime = now_ms();
        screen = m_historyStack.empty() ? nullptr : m_historyStack.back();
    }
    if (screen) {
        screen->handleInput(action);
        m_screenChanged = true;
        m_cond.notify_one();
    }
}

void UIManager::showVolume(int percent, bool muted)
{
    m_volumePercent = percent;
    m_volumeMuted = muted;
    m_volumeUntil = now_ms() + VOLUME_MS;
    m_screenChanged = true;
    m_cond.notify_one();
}

void UIManager::run()
{
    const auto tick = std::chrono::milliseconds(m_tick_ms);
    std::vector<std::string> shown;
    int shown_highlight = -1;
    while (!m_exit) {
        std::shared_ptr<UIScreen> screen;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait_for(lock, tick, [this]() { return m_exit || m_screenChanged; });
            if (m_exit) {
                break;
            }
            screen = m_historyStack.empty() ? nullptr : m_historyStack.back();
            m_screenChanged = false;
        }
        if (!screen) {
            continue;
        }

        Frame frame(m_display->cols(), m_display->rows(), now_ms());
        frame.highlights = m_display->canHighlight();
        screen->render(frame);
        if (frame.now < m_volumeUntil) {
            frame.set(frame.rows - 1, volumeLine(frame.cols, m_volumePercent, m_volumeMuted));
            if (frame.highlight == frame.rows - 1) {
                frame.highlight = -1; // the line shows the level now
            }
        }
        for (auto& line : frame.lines) {
            line = utf8Fit(line, frame.cols);
        }
        // most ticks repeat the last frame
        if (frame.lines == shown && frame.highlight == shown_highlight) {
            continue;
        }
        shown = frame.lines;
        shown_highlight = frame.highlight;
        m_display->present(frame);
    }
}

} // namespace si468x_pi
