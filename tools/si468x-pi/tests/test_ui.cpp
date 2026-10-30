// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "display_interface.hpp"
#include "input_queue.hpp"
#include "screens.hpp"
#include "ui_manager.hpp"
#include "utils.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const std::string& message)
{
    if (!cond) {
        std::cerr << "FAIL: " << message << std::endl;
        ++failures;
    }
}

bool has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

si468x_pi::Station station(const std::string& channel, const std::string& program,
                           uint32_t sid, bool data = false)
{
    si468x_pi::Station s;
    s.channel = channel;
    s.program = program;
    s.short_program = program;
    s.service_id = sid;
    s.data = data;
    return s;
}

void test_utf8()
{
    using namespace si468x_pi;

    check(utf8Length("RTS Première") == 12, "UTF-8 length counts code points");
    check(utf8Substr("RTS Première", 4, 4) == "Prem", "UTF-8 substring");
    check(utf8Substr("aéb", 1, 1) == "é", "UTF-8 substring keeps a character whole");
    check(utf8Fit("é", 3) == "é  ", "UTF-8 fit pads by code points");
    check(utf8Fit("abcdef", 4) == "abcd", "UTF-8 fit truncates");
    check(utf8Fit("ab", 4) == "ab  ", "UTF-8 fit pads");
    check(utf8Length(utf8Fit("RTS Première", 8)) == 8, "UTF-8 fit yields exactly the width");
}

void test_radio_screen()
{
    using namespace si468x_pi;

    const uint64_t t = now_ms();

    RadioScreen radio;
    radio.setProgramName("RTS Première", "RTS Prem");
    radio.setChannelName("12A");
    radio.setDLS("Hello world");

    Frame large(20, 4, t);
    radio.render(large);
    check(large.lines[0] == "RTS Première", "radio program row");
    check(large.lines[1] == "12A", "radio channel row");
    check(large.lines[2].empty(), "blank row before the marquee");
    check(large.lines[3] == "Hello world", "short DLS is shown whole");

    RadioScreen small;
    small.setProgramName("RTS Première", "RTS Prem");
    small.setChannelName("12A");
    Frame two(20, 2, t);
    small.render(two);
    check(two.lines[0] == "RTS Première", "two-row program");
    check(two.lines[1] == "12A", "two-row channel when there is no DLS");

    small.setDLS("Short");
    Frame two_dls(20, 2, t + 1);
    small.render(two_dls);
    check(two_dls.lines[1] == "Short", "two-row DLS replaces the channel");

    RadioScreen long_name;
    long_name.setProgramName("A very long programme label", "Short");
    Frame narrow(10, 2, t);
    long_name.render(narrow);
    check(narrow.lines[0] == "Short", "long program uses the short label");

    RadioScreen marquee;
    marquee.setProgramName("P");
    marquee.setDLS("abcdefghijklmno");
    Frame m1(10, 2, t);
    marquee.render(m1);
    check(m1.lines[1] == "abcdefghij", "marquee starts at the beginning");
    Frame m2(10, 2, t + 2 * RadioScreen::DLS_STEP_MS);
    marquee.render(m2);
    check(m2.lines[1] == "cdefghijkl", "marquee advances by code points");

    RadioScreen queue;
    queue.setProgramName("P");
    queue.setDLS("A");
    Frame q1(20, 2, t);
    queue.render(q1);
    check(q1.lines[1] == "A", "first DLS message");
    queue.setDLS("B");
    Frame q2(20, 2, t + 500);
    queue.render(q2);
    check(q2.lines[1] == "A", "second message waits its turn");
    Frame q3(20, 2, t + RadioScreen::DLS_REST_MS + 1);
    queue.render(q3);
    check(q3.lines[1] == "B", "second message follows after the rest");

    RadioScreen wrap;
    wrap.setProgramName("P");
    wrap.setDLS("alpha beta gamma delta");
    Frame w(10, 5, t);
    wrap.render(w);
    check(w.lines[3] == "alpha beta", "wrapped DLS first line");
    check(w.lines[4] == "gamma", "wrapped DLS second line");

    RadioScreen lost;
    lost.setProgramName("P");
    lost.setChannelName("12A");
    lost.setSignalPresent(false, t);
    Frame l1(20, 2, t + 3000);
    lost.render(l1);
    check(l1.lines[1] == "12A", "recent signal loss keeps the channel");
    Frame l2(20, 2, t + SIGNAL_LOST_MS + 1);
    lost.render(l2);
    check(l2.lines[1] == RadioScreen::NO_SIGNAL_TEXT, "old signal loss shows the message");
    lost.setSignalPresent(true, t + SIGNAL_LOST_MS + 2);
    Frame l3(20, 2, t + SIGNAL_LOST_MS + 3);
    lost.render(l3);
    check(l3.lines[1] != RadioScreen::NO_SIGNAL_TEXT, "signal returns");
}

void test_station_list()
{
    using namespace si468x_pi;

    std::vector<Station> stations = {
        station("12A", "Alpha", 1),
        station("12A", "Beta", 2),
        station("12B", "Beta", 3),
        station("12A", "Data", 4, true),
    };

    StationListScreen list(stations, nullptr);
    Frame frame(30, 5, 0);
    list.render(frame);
    check(frame.lines[0] == "Select: 1/4", "station list header");
    check(frame.lines[1].size() == 30 && frame.lines[1].front() == '>'
              && frame.lines[1].back() == '<',
          "focused row is marked and exact width");
    check(has(frame.lines[1], "Alpha"), "focused row shows the first station");
    check(has(frame.lines[4], "[data]"), "data service is marked");
    check(frame.lines[2].size() == 30 && frame.lines[2].substr(frame.lines[2].size() - 3) == "12A",
          "duplicate program gets a channel suffix");
    check(frame.lines[3].size() == 30 && frame.lines[3].substr(frame.lines[3].size() - 3) == "12B",
          "second channel suffix");

    list.handleInput(InputAction::DOWN);
    Frame down(30, 5, 0);
    list.render(down);
    check(down.lines[0] == "Select: 2/4", "down moves the selection");
    check(has(down.lines[2], "Beta") && has(down.lines[2], "12A"), "second station focused");

    list.handleInput(InputAction::UP);
    list.handleInput(InputAction::UP);
    Frame wrapped(30, 5, 0);
    list.render(wrapped);
    check(wrapped.lines[0] == "Select: 4/4", "up wraps to the last station");
    check(has(wrapped.lines[4], "Data"), "last station focused");

    Station selected;
    bool called = false;
    list.setSelectCallback([&](const Station& s) {
        selected = s;
        called = true;
    });
    list.handleInput(InputAction::ENTER);
    check(called && selected.program == "Data", "enter selects the focused station");
    check(list.isSelected(), "selection state");
    list.resetSelected();
    check(!list.isSelected(), "selection reset");

    StationListScreen highlighted(stations, nullptr);
    Frame hframe(30, 5, 0);
    hframe.highlights = true;
    highlighted.render(hframe);
    check(hframe.highlight == 1, "highlight row for the focused item");
    check(!has(hframe.lines[1], ">"), "highlight display does not use text markers");

    StationListScreen empty({}, nullptr);
    Frame eframe(20, 3, 0);
    empty.render(eframe);
    check(has(eframe.lines[1], "Empty"), "empty station list");

    list.setIndex(2);
    Frame indexed(30, 5, 0);
    list.render(indexed);
    check(indexed.lines[0] == "Select: 3/4", "setIndex moves the selection");
}

void test_menu()
{
    using namespace si468x_pi;

    MenuScreen menu(nullptr);
    check(menu.optionCount() == 6, "menu option count");
    check(menu.option(0) == "Rescan" && menu.option(1) == "Stations"
              && menu.option(2) == "Audio output" && menu.option(3) == "Info"
              && menu.option(4) == "Back to radio" && menu.option(5) == "Exit",
          "menu options");

    Frame frame(20, 8, 0);
    menu.render(frame);
    check(frame.lines[0] == "Menu", "menu header");
    check(has(frame.lines[1], "Rescan"), "menu starts on Rescan");

    int selected = -1;
    menu.setSelectCallback([&](int index) { selected = index; });
    menu.handleInput(InputAction::DOWN);
    Frame down(20, 8, 0);
    menu.render(down);
    check(has(down.lines[2], "Stations"), "menu moves down");
    menu.handleInput(InputAction::ENTER);
    check(selected == 1, "menu enter reports the index");

    menu.handleInput(InputAction::LEFT);
    check(selected == 1, "menu left does not select");

    menu.handleInput(InputAction::DOWN);
    menu.handleInput(InputAction::DOWN);
    menu.handleInput(InputAction::DOWN);
    menu.handleInput(InputAction::ENTER); // Back to radio with no manager
    check(selected == 1, "back to radio does not call the option callback");
}

class RecordingDisplay : public si468x_pi::IDisplay {
public:
    RecordingDisplay(int cols, int rows) : m_cols(cols), m_rows(rows) {}

    int cols() const override { return m_cols; }
    int rows() const override { return m_rows; }
    void present(const si468x_pi::Frame& frame) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last = frame;
        ++m_presents;
        m_cond.notify_all();
    }

    bool waitFor(const std::function<bool(const si468x_pi::Frame&)>& predicate, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
            return m_presents > 0 && predicate(m_last);
        });
    }

private:
    int m_cols;
    int m_rows;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    si468x_pi::Frame m_last { 0, 0, 0 };
    int m_presents = 0;
};

class TestScreen : public si468x_pi::UIScreen {
public:
    explicit TestScreen(std::string name) : m_name(std::move(name)) {}

    void render(si468x_pi::Frame& frame) override
    {
        frame.set(0, m_name);
        frame.set(1, "actions:" + std::to_string(m_actions));
    }

    void handleInput(si468x_pi::InputAction) override { ++m_actions; }

    int actions() const { return m_actions; }

private:
    std::string m_name;
    int m_actions = 0;
};

void test_ui_manager()
{
    using namespace si468x_pi;

    auto starts = [](const Frame& f, size_t row, const std::string& text) {
        return f.lines.size() > row && f.lines[row].rfind(text, 0) == 0;
    };

    auto display_owner = std::make_unique<RecordingDisplay>(20, 4);
    auto* display = display_owner.get();
    {
        UIManager ui(std::move(display_owner), 20);

        auto root = std::make_shared<TestScreen>("root");
        auto child = std::make_shared<TestScreen>("child");
        ui.setScreen(root);
        check(display->waitFor([&](const Frame& f) { return starts(f, 0, "root"); }, 2000),
              "root screen renders");

        ui.pushScreen(child);
        check(display->waitFor([&](const Frame& f) { return starts(f, 0, "child"); }, 2000),
              "pushed screen renders");

        ui.processInput(InputAction::DOWN);
        check(child->actions() == 1, "input reaches the top screen");
        check(display->waitFor([&](const Frame& f) { return starts(f, 1, "actions:1"); }, 2000),
              "screen re-renders after input");

        check(ui.popScreen(), "pop a child screen");
        check(display->waitFor([&](const Frame& f) { return starts(f, 0, "root"); }, 2000),
              "root resumes after pop");

        ui.pushScreen(child);
        check(display->waitFor([&](const Frame& f) { return starts(f, 0, "child"); }, 2000),
              "child rendered again");
        const int actions_before = child->actions();
        ui.processInput(InputAction::HOME);
        check(child->actions() == actions_before, "HOME is not passed to the screen");
        check(display->waitFor([&](const Frame& f) { return starts(f, 0, "root"); }, 2000),
              "HOME returns to the root");
        check(!ui.popScreen(), "the root cannot be popped");

        ui.showVolume(50, false);
        check(display->waitFor(
                  [](const Frame& f) {
                      return f.lines.size() > 3 && f.lines[3] == volumeLine(20, 50, false);
                  },
                  2000),
              "volume overlay draws on the last line");
    }
}

} // namespace

int main()
{
    std::cout << "si468x-pi UI tests" << std::endl;

    test_utf8();
    test_radio_screen();
    test_station_list();
    test_menu();
    test_ui_manager();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all UI tests passed" << std::endl;
    return 0;
}
