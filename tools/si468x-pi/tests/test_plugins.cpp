// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.h"
#include "input_sources.hpp"
#include "plugin_display.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/plugin.h"

#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

// TEST_PLUGIN_DIR: example.c and the rejection cases (set by Makefile.am)
std::string testPlugin(const std::string& name)
{
    return std::string(TEST_PLUGIN_DIR) + "/si468x-pi-" + name + ".so";
}

void test_input_plugin()
{
    using namespace si468x_pi;

    InputQueue queue;
    std::atomic<bool> stop(false);
    const StopPredicate stop_requested = [&] { return stop.load(); };

    // broken entries are skipped, stdin needs a terminal: only the example starts
    Settings s;
    parse_settings("{\"input\": [{\"plugin\": \"" + testPlugin("example") + "\", \"keys\": \"d+hme\"},"
                   " {\"plugin\": \"nonexistent\"}, {\"plugin\": \"/nonexistent/plugin.so\"},"
                   " {\"type\": \"bogus\"}, {\"type\": \"stdin\"}]}",
                   s);
    auto threads = startInputSources(s, queue, stop_requested, false);
    check(threads.size() == 1, "only the valid plugin thread starts");

    check(queue.pop(2000) == InputAction::DOWN, "first action DOWN");
    check(queue.pop(2000) == InputAction::VOLUME_UP, "second action VOLUME_UP");
    check(queue.pop(2000) == InputAction::HOME, "third action HOME");
    check(queue.pop(2000) == InputAction::MUTE, "fourth action MUTE");
    check(queue.pop(2000) == InputAction::ENTER, "fifth action ENTER");

    stop = true;
    for (auto& t : threads) {
        t.join();
    }
}

void test_display_plugin()
{
    using namespace si468x_pi;

    const std::string file = "/tmp/si468x-pi-display-plugin-" + std::to_string(getpid()) + ".txt";
    unlink(file.c_str());
    {
        auto display = PluginDisplay::open(testPlugin("example"), "{\"file\":\"" + file + "\"}");
        check(display != nullptr, "open display plugin example");
        if (display) {
            check(display->cols() == 16 && display->rows() == 2, "geometry 16x2");
            check(!display->canHighlight(), "example display cannot highlight");

            Frame frame(16, 2, 0);
            frame.set(0, "Radio Example   ");
            frame.set(1, "11D             ");
            display->present(frame);
            frame.highlight = 1;
            display->present(frame);
        }
    }
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    unlink(file.c_str());
    check(text.str() ==
              "frame 16 x 2\nRadio Example   \n11D             \n"
              "frame 16 x 2, highlighted row 1\nRadio Example   \n11D             \n"
              "closed\n",
          "display plugin gets the exact frames and is closed");

    check(!PluginDisplay::open(testPlugin("example"), "{}"), "missing 'file' in config fails");
    check(!PluginDisplay::open("nonexistent", "{}"), "open nonexistent plugin fails");
    check(!PluginDisplay::open("/nonexistent/plugin.so", "{}"), "open nonexistent path fails");
    check(!PluginDisplay::open("", "{}"), "empty plugin name fails");
}

void test_loader()
{
    using namespace si468x_pi;

    check(loadPlugin("input", testPlugin("example"), SI468X_PI_INPUT_ABI) != nullptr, "load plugin by absolute path");
    check(!loadPlugin("input", testPlugin("badabi"), SI468X_PI_INPUT_ABI), "mismatched ABI rejected");
    check(!loadPlugin("input", testPlugin("badsym"), SI468X_PI_INPUT_ABI), "missing name symbols rejected");
    check(!loadPlugin("input", "/nonexistent/si468x-pi-missing.so", SI468X_PI_INPUT_ABI), "bad path rejected");
    check(!loadPlugin("input", "", SI468X_PI_INPUT_ABI), "empty name rejected");
}

} // namespace

int main()
{
    std::cout << "si468x-pi plugin tests" << std::endl;

    test_input_plugin();
    test_display_plugin();
    test_loader();

    return report("plugin");
}
