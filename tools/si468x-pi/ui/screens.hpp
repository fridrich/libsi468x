// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "station.hpp"
#include "ui_screen.hpp"
#include "utils.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace si468x_pi {

class UIManager;

class ScanningScreen : public UIScreen {
public:
    void setScanningStatus(const std::string& channel, int foundCount);
    void render(Frame& frame) override;
    void handleInput(InputAction action) override;

private:
    std::mutex m_mutex;
    std::string m_channel;
    int m_found = 0;
};

class RadioScreen : public UIScreen {
public:
    RadioScreen();

    void setChannelName(const std::string& channel_name);
    void setProgramName(const std::string& program_name, const std::string& short_program = "");
    void setDLS(const std::string& dls);
    void setSignalPresent(bool present, uint64_t now = now_ms());
    void render(Frame& frame) override;
    void handleInput(InputAction action) override;
    void setInputCallback(std::function<void(InputAction)> cb);

    // DLS marquee: one step per DLS_STEP_MS; each message runs once, then
    // rests DLS_REST_MS. 5+ rows: word-wrapped, DLS_LINE_MS per line, scrolls
    // up if it overflows.
    static constexpr uint64_t DLS_STEP_MS = 200;
    static constexpr uint64_t DLS_LINE_MS = 1000;
    static constexpr uint64_t DLS_REST_MS = 1000;
    static constexpr const char* DLS_GAP = "     ";
    static constexpr const char* NO_SIGNAL_TEXT = "-- no signal --";

private:
    bool signalLost(uint64_t now) const;

    std::mutex m_mutex;
    std::string m_channelName;
    std::string m_programName;
    std::string m_shortProgramName;
    std::string m_dls;
    uint64_t m_dlsStart = 0;
    std::deque<std::string> m_dlsQueue;
    std::atomic<bool> m_signalPresent;
    std::atomic<uint64_t> m_signalChanged;
    std::function<void(InputAction)> m_inputCallback;
};

class StationListScreen : public UIScreen {
public:
    StationListScreen(const std::vector<Station>& stations, UIManager* uiManager);

    void render(Frame& frame) override;
    void handleInput(InputAction action) override;
    void setSelectCallback(std::function<void(const Station&)> cb);
    void setIndex(size_t index);
    bool isSelected() const;
    void resetSelected();

    // focused name longer than the line: start and end take turns
    static constexpr uint64_t NAME_SWITCH_MS = 1000;

private:
    std::vector<Station> m_stations;
    std::vector<bool> m_showChannel;
    UIManager* m_uiManager;
    std::atomic<size_t> m_index;
    bool m_selected;
    std::function<void(const Station&)> m_selectCallback;
};

class MenuScreen : public UIScreen {
public:
    explicit MenuScreen(UIManager* uiManager);

    void render(Frame& frame) override;
    void handleInput(InputAction action) override;
    void setSelectCallback(std::function<void(int)> cb);

    size_t optionCount() const { return m_options.size(); }
    std::string option(size_t index) const;

private:
    std::vector<std::string> m_options;
    UIManager* m_uiManager;
    std::atomic<size_t> m_index;
    std::function<void(int)> m_selectCallback;
};

} // namespace si468x_pi
