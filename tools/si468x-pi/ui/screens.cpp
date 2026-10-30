// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "screens.hpp"

#include "ui_manager.hpp"

#include <algorithm>
#include <functional>
#include <map>

namespace si468x_pi {

namespace {

// Lines of at most width code points, broken at spaces; longer words are cut.
std::vector<std::string> wrapWords(const std::string& text, size_t width)
{
    std::vector<std::string> lines;
    if (width == 0) {
        return lines;
    }
    std::string line;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string word = text.substr(start, end - start);
        start = end + 1;
        while (utf8Length(word) > width) {
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            lines.push_back(utf8Substr(word, 0, width));
            word = utf8Substr(word, width, std::string::npos);
        }
        if (word.empty()) {
            continue;
        }
        if (line.empty()) {
            line = word;
        } else if (utf8Length(line) + 1 + utf8Length(word) <= width) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// Header on row 0, the list below with the focused item in the middle.
void renderList(
    Frame& frame, const std::string& header, size_t itemsCount, size_t selectedIndex,
    const std::function<std::string(size_t index, bool isFocused, int width)>& getItemString)
{
    frame.set(0, header);

    if (itemsCount == 0) {
        frame.set(1, "Empty!");
        return;
    }
    if (frame.rows < 2) {
        return;
    }

    const int width = frame.cols;
    const int list_lines = std::max(frame.rows - 1, 1);
    const int count = static_cast<int>(itemsCount);

    // Everything fits: show each item once, the block centred and the focus on
    // the selected item.
    if (count <= list_lines) {
        const int first_row = (list_lines - count) / 2;
        for (int i = 0; i < count; ++i) {
            const bool is_focused = static_cast<size_t>(i) == selectedIndex;
            std::string option = getItemString(static_cast<size_t>(i), is_focused, width);
            if (is_focused && !frame.mark(1 + first_row + i)) {
                option = width < 16 ? ">" + option
                                    : ">" + utf8Fit(option, width - 2) + "<";
            }
            frame.set(1 + first_row + i, option);
        }
        return;
    }

    // More items than lines: a centred window that wraps around.
    const int focused_line = list_lines / 2;
    for (int line = 0; line < list_lines; ++line) {
        const int offset = line - focused_line;
        const size_t item_index =
            ((static_cast<int>(selectedIndex) + offset) % count + count) % count;

        std::string option = getItemString(item_index, offset == 0, width);
        // displays that draw the row inverted get the whole width, the others
        // need the two markers
        if (offset == 0 && !frame.mark(1 + line)) {
            option = width < 16 ? ">" + option : ">" + utf8Fit(option, width - 2) + "<";
        }
        frame.set(1 + line, option);
    }
}

} // namespace

void ScanningScreen::setScanningStatus(const std::string& channel, int foundCount)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channel = channel;
    m_found = foundCount;
}

void ScanningScreen::setAbortCallback(std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_abortCallback = std::move(cb);
}

void ScanningScreen::render(Frame& frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    frame.set(0, m_channel.empty() ? "Scanning Band III" : "Scanning: " + m_channel);
    if (frame.rows > 1) {
        frame.set(1, "Found: " + std::to_string(m_found) + " stations");
    }
    if (frame.rows > 2) {
        frame.set(2, "MENU stops scanning");
    }
}

void ScanningScreen::handleInput(InputAction action)
{
    if (action == InputAction::MENU || action == InputAction::QUIT) {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cb = m_abortCallback;
        }
        if (cb) {
            cb();
        }
    }
}

RadioScreen::RadioScreen() : m_signalPresent(true), m_signalChanged(now_ms())
{
}

void RadioScreen::setChannelName(const std::string& channel_name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channelName = channel_name;
}

void RadioScreen::setProgramName(const std::string& program_name,
                                 const std::string& short_program)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_programName != program_name || m_shortProgramName != short_program) {
        m_programName = program_name;
        m_shortProgramName = short_program;
        m_dlsQueue.clear();
        m_dls.clear();
    }
}

void RadioScreen::setDLS(const std::string& dls)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_dlsQueue.empty() || m_dlsQueue.back() != dls) {
        m_dlsQueue.push_back(dls);
    }
}

void RadioScreen::setSignalPresent(bool present, uint64_t now)
{
    if (m_signalPresent.exchange(present) != present) {
        m_signalChanged = now;
    }
}

bool RadioScreen::signalLost(uint64_t now) const
{
    if (m_signalPresent) {
        return false;
    }
    const uint64_t changed = m_signalChanged;
    return now > changed && now - changed > SIGNAL_LOST_MS;
}

void RadioScreen::render(Frame& frame)
{
    const size_t cols = frame.cols;
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool lost = signalLost(frame.now);

    std::string program = m_programName;
    if (utf8Length(program) > cols && !m_shortProgramName.empty()) {
        program = m_shortProgramName;
    }
    frame.set(0, program);
    if (frame.rows > 2) {
        frame.set(1, m_channelName);
    }
    if (frame.rows < 2) {
        return;
    }

    // 5+ rows: the DLS word-wrapped one blank row below the channel; else a
    // one-row marquee
    const size_t area = frame.rows > 4 ? static_cast<size_t>(frame.rows) - 3 : 1;
    // One pass of the message: the marquee once round, or one DLS_LINE_MS per
    // line.
    auto passMs = [&]() -> uint64_t {
        if (area > 1) {
            return wrapWords(m_dls, cols).size() * DLS_LINE_MS;
        }
        const size_t len = utf8Length(m_dls);
        return len > cols ? (len + utf8Length(DLS_GAP)) * DLS_STEP_MS : 0;
    };
    // The next message waits until the current one went round once.
    if (!lost && (m_dls.empty() || frame.now - m_dlsStart >= passMs() + DLS_REST_MS)) {
        if (!m_dlsQueue.empty()) {
            m_dls = m_dlsQueue.front();
            m_dlsQueue.pop_front();
        }
        m_dlsStart = frame.now;
    }

    if (area > 1) {
        if (lost) {
            frame.set(frame.rows - 1, NO_SIGNAL_TEXT);
            return;
        }
        // first window stays until its lines are read, then one line up per
        // DLS_LINE_MS
        const auto lines = wrapWords(m_dls, cols);
        const size_t read = (frame.now - m_dlsStart) / DLS_LINE_MS + 1;
        const size_t last = lines.size() > area ? lines.size() - area : 0;
        const size_t first = std::min(read > area ? read - area : 0, last);
        const size_t shown = std::min(area, lines.size());
        for (size_t i = 0; i < shown; ++i) { // bottom-aligned
            frame.set(frame.rows - static_cast<int>(shown) + static_cast<int>(i), lines[first + i]);
        }
        return;
    }

    std::string text;
    if (lost) {
        text = NO_SIGNAL_TEXT;
    } else if (passMs() == 0) {
        text = m_dls;
    } else {
        const size_t step = std::min<uint64_t>(
            (frame.now - m_dlsStart) / DLS_STEP_MS, utf8Length(m_dls) + utf8Length(DLS_GAP));
        text = utf8Substr(m_dls + DLS_GAP + m_dls, step, cols);
    }
    // two lines: the channel only while there is no DLS
    frame.set(frame.rows - 1, text.empty() && frame.rows == 2 ? m_channelName : text);
}

void RadioScreen::handleInput(InputAction action)
{
    if (m_inputCallback) {
        m_inputCallback(action);
    }
}

void RadioScreen::setInputCallback(std::function<void(InputAction)> cb)
{
    m_inputCallback = std::move(cb);
}

StationListScreen::StationListScreen(const std::vector<Station>& stations, UIManager* uiManager)
    : m_stations(stations), m_uiManager(uiManager), m_index(0), m_selected(false)
{
    std::map<std::string, int> count;
    for (const auto& s : m_stations) {
        ++count[s.program];
    }
    for (const auto& s : m_stations) {
        m_showChannel.push_back(count[s.program] > 1);
    }
}

void StationListScreen::render(Frame& frame)
{
    const size_t index = m_index;
    const int width = frame.cols;

    std::string header =
        "Select: " + std::to_string(index + 1) + "/" + std::to_string(m_stations.size());
    if (utf8Length(header) > static_cast<size_t>(width) && width >= 4) {
        header = std::to_string(index + 1) + "/" + std::to_string(m_stations.size());
    }

    renderList(frame, header, m_stations.size(), index, [&](size_t idx, bool isFocused, int w) {
        const Station& station = m_stations[idx];
        std::string name =
            isFocused && w < 16 && !station.short_program.empty() ? station.short_program
                                                                 : station.program;
        if (station.data) {
            name += " [data]";
        }
        // focused: ">name<" or ">name", unless the display marks the row itself
        const size_t room =
            isFocused && !frame.highlights ? static_cast<size_t>(w >= 16 ? w - 2 : w - 1)
                                           : static_cast<size_t>(w);
        // name listed more than once: channel right-aligned
        const std::string channel = m_showChannel[idx] ? " " + station.channel : "";
        if (room <= channel.size()) {
            return name;
        }
        const size_t nameRoom = room - channel.size();
        const size_t len = utf8Length(name);
        // focused and too long: its end every other NAME_SWITCH_MS
        if (isFocused && len > nameRoom && frame.now / NAME_SWITCH_MS % 2) {
            name = utf8Substr(name, len - nameRoom, nameRoom);
        }
        return channel.empty() ? name : utf8Fit(name, nameRoom) + channel;
    });
}

void StationListScreen::handleInput(InputAction action)
{
    if (m_stations.empty()) {
        if (action == InputAction::LEFT || action == InputAction::MENU) {
            if (m_uiManager) {
                m_uiManager->popScreen();
            }
        }
        return;
    }
    if (action == InputAction::UP) {
        m_index = m_index == 0 ? m_stations.size() - 1 : m_index - 1;
    } else if (action == InputAction::DOWN) {
        m_index = m_index == m_stations.size() - 1 ? 0 : m_index + 1;
    } else if (action == InputAction::ENTER) {
        m_selected = true;
        if (m_selectCallback) {
            m_selectCallback(m_stations[m_index]);
        }
    } else if (action == InputAction::LEFT || action == InputAction::MENU) {
        if (m_uiManager) {
            m_uiManager->popScreen();
        }
    }
}

void StationListScreen::setSelectCallback(std::function<void(const Station&)> cb)
{
    m_selectCallback = std::move(cb);
}

void StationListScreen::setIndex(size_t index)
{
    if (index < m_stations.size()) {
        m_index = index;
    }
}

void StationListScreen::setStations(const std::vector<Station>& stations)
{
    m_stations = stations;
    if (m_index >= m_stations.size()) {
        m_index = 0;
    }
    m_showChannel.clear();
    std::map<std::string, int> count;
    for (const auto& s : m_stations) {
        ++count[s.program];
    }
    for (const auto& s : m_stations) {
        m_showChannel.push_back(count[s.program] > 1);
    }
}

bool StationListScreen::isSelected() const
{
    return m_selected;
}

void StationListScreen::resetSelected()
{
    m_selected = false;
}

MenuScreen::MenuScreen(UIManager* uiManager) : m_uiManager(uiManager), m_index(0)
{
    m_options = {
        "Rescan", "Stations", "Info", "Back to radio", "Exit",
    };
}

void MenuScreen::render(Frame& frame)
{
    renderList(frame, "Menu", m_options.size(), m_index,
               [&](size_t idx, bool, int) { return m_options[idx]; });
}

void MenuScreen::handleInput(InputAction action)
{
    if (action == InputAction::UP) {
        m_index = m_index == 0 ? m_options.size() - 1 : m_index - 1;
    } else if (action == InputAction::DOWN) {
        m_index = m_index == m_options.size() - 1 ? 0 : m_index + 1;
    } else if (action == InputAction::ENTER) {
        if (m_options[m_index] == "Back to radio") {
            if (m_uiManager) {
                m_uiManager->popScreen();
            }
        } else if (m_selectCallback) {
            m_selectCallback(static_cast<int>(m_index));
        }
    } else if (action == InputAction::MENU || action == InputAction::LEFT) {
        if (m_uiManager) {
            m_uiManager->popScreen();
        }
    }
}

void MenuScreen::setSelectCallback(std::function<void(int)> cb)
{
    m_selectCallback = std::move(cb);
}

std::string MenuScreen::option(size_t index) const
{
    return index < m_options.size() ? m_options[index] : std::string();
}

InfoScreen::InfoScreen(UIManager* uiManager) : m_uiManager(uiManager)
{
}

void InfoScreen::setSignal(const si468x_signal_status_t& sig)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sig = sig;
}

void InfoScreen::setAudio(const si468x_audio_info_t& audio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio = audio;
}

void InfoScreen::setTime(const si468x_time_t& time)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_time = time;
}

void InfoScreen::setEnsemble(const si468x_ensemble_info_t& ens)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ens = ens;
}

void InfoScreen::render(Frame& frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    frame.set(0, m_page == 0 ? "Info: Signal (1/2)" : "Info: Service (2/2)");

    if (m_page == 0) {
        if (frame.rows > 1) {
            frame.set(1, "RSSI: " + std::to_string(m_sig.rssi) + " dBuV  SNR: " + std::to_string(m_sig.snr) + " dB");
        }
        if (frame.rows > 2) {
            frame.set(2, "CNR: " + std::to_string(m_sig.cnr) + " dB  FIB: " + std::to_string(m_sig.fib_errors));
        }
        if (frame.rows > 3) {
            if (m_time.year > 0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u",
                              m_time.year, m_time.month, m_time.day, m_time.hours, m_time.minutes);
                frame.set(3, "Time: " + std::string(buf));
            } else {
                frame.set(3, "Time: --:--");
            }
        }
    } else {
        if (frame.rows > 1) {
            std::string mode_str = "Mono";
            if (m_audio.audio_mode == 2) {
                mode_str = "Stereo";
            } else if (m_audio.audio_mode == 3) {
                mode_str = "J-Stereo";
            }
            frame.set(1, std::to_string(m_audio.bitrate) + " kbps " + mode_str + " " + std::to_string(m_audio.sample_rate) + "Hz");
        }
        if (frame.rows > 2) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%04X (ECC 0x%02X)", m_ens.eid, m_ens.ecc);
            frame.set(2, "Ens: " + std::string(buf));
        }
        if (frame.rows > 3) {
            frame.set(3, m_ens.label[0] ? std::string(m_ens.label) : "Ensemble info");
        }
    }
}

void InfoScreen::handleInput(InputAction action)
{
    if (action == InputAction::UP || action == InputAction::DOWN) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_page = 1 - m_page;
    } else if (action == InputAction::MENU || action == InputAction::LEFT || action == InputAction::ENTER) {
        if (m_uiManager) {
            m_uiManager->popScreen();
        }
    }
}

} // namespace si468x_pi
