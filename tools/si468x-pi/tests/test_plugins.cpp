// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_sources.hpp"
#include "plugin_display.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/display_plugin.h"
#include "si468x-pi/input_plugin.h"

#include <atomic>
#include <climits>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
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

std::string getPluginPath(const std::string& base_name)
{
    std::vector<std::string> candidates = {
        "./si468x-pi-plugins/" + base_name,
        "./.libs/" + base_name,
        "../.libs/" + base_name,
    };
    char exe_buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (n > 0) {
        std::string dir(exe_buf, n);
        size_t slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir = dir.substr(0, slash);
            candidates.push_back(dir + "/" + base_name);
            candidates.push_back(dir + "/si468x-pi-plugins/" + base_name);
            candidates.push_back(dir + "/.libs/" + base_name);
            candidates.push_back(dir + "/../.libs/" + base_name);
            if (dir.size() >= 6 && dir.substr(dir.size() - 6) == "/.libs") {
                std::string parent = dir.substr(0, dir.size() - 6);
                candidates.push_back(parent + "/si468x-pi-plugins/" + base_name);
                candidates.push_back(parent + "/.libs/" + base_name);
            }
        }
    }
    for (const auto& c : candidates) {
        char abs_path[PATH_MAX];
        if (realpath(c.c_str(), abs_path) != nullptr) {
            return std::string(abs_path);
        }
    }
    return "";
}

void test_input_plugin()
{
    using namespace si468x_pi;

    InputQueue queue;
    std::atomic<bool> stop(false);
    const StopPredicate stop_requested = [&] { return stop.load(); };

    nlohmann::json inputs = nlohmann::json::array({
        { { "plugin", "example" }, { "keys", "d+hmeX" } },
        { { "plugin", "nonexistent" } },
        { { "plugin", "/nonexistent/plugin.so" } },
        { { "type", "bogus" } },
        { { "type", "stdin" } }
    });

    auto threads = startInputSources(inputs, queue, stop_requested, false);
    check(threads.size() == 1, "startInputSources starts only valid plugin thread");

    check(queue.pop(2000) == InputAction::DOWN, "first action DOWN");
    check(queue.pop(2000) == InputAction::VOLUME_UP, "second action VOLUME_UP");
    check(queue.pop(2000) == InputAction::HOME, "third action HOME");
    check(queue.pop(2000) == InputAction::MUTE, "fourth action MUTE");
    check(queue.pop(2000) == InputAction::ENTER, "fifth action ENTER");
    check(queue.pop(100) == InputAction::NONE, "out of range foreign action was dropped");

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
        auto display = PluginDisplay::open("example", "{\"plugin\":\"example\",\"file\":\"" + file + "\"}");
        check(display != nullptr, "open display plugin example");
        if (display) {
            check(display->cols() == 16 && display->rows() == 2, "geometry 16x2");
            check(!display->canHighlight(), "example display cannot highlight");

            Frame frame(16, 2, 0);
            frame.set(0, "Radio Example   ");
            frame.set(1, "11D             ");
            display->present(frame);
            display->present(frame);

            frame.set(1, "Now playing     ");
            display->present(frame);

            frame.highlight = 1;
            display->present(frame);
            frame.highlight = 0;
            display->present(frame);
        }
    }
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    unlink(file.c_str());
    check(text.str() ==
              "frame 16 x 2\nRadio Example   \n11D             \n"
              "frame 16 x 2\nRadio Example   \nNow playing     \n"
              "frame 16 x 2, highlighted row 1\nRadio Example   \nNow playing     \n"
              "frame 16 x 2, highlighted row 0\nRadio Example   \nNow playing     \n"
              "closed\n",
          "display plugin exact frames written and closed");

    check(!PluginDisplay::open("nonexistent", "{}"), "open nonexistent plugin fails");
    check(!PluginDisplay::open("/nonexistent/plugin.so", "{}"), "open nonexistent path fails");
    check(!PluginDisplay::open("example", "{}"), "missing 'file' in config fails");
    check(!PluginDisplay::open("", "{}"), "empty plugin name fails");
}

void test_rejections_and_naming()
{
    using namespace si468x_pi;

    std::string example_so = getPluginPath("si468x-pi-example.so");
    check(!example_so.empty(), "found si468x-pi-example.so");
    check(example_so.find("si468x-pi-example.so") != std::string::npos, "filename matches si468x-pi-<name>.so");

    void* h_abs = loadPlugin("input", example_so, SI468X_PI_INPUT_ABI);
    check(h_abs != nullptr, "load plugin by absolute path");

    std::string badabi_so = getPluginPath("si468x-pi-badabi.so");
    check(!badabi_so.empty(), "found si468x-pi-badabi.so");
    void* h_badabi = loadPlugin("input", badabi_so, SI468X_PI_INPUT_ABI);
    check(h_badabi == nullptr, "mismatched ABI rejected");

    std::string badsym_so = getPluginPath("si468x-pi-badsym.so");
    check(!badsym_so.empty(), "found si468x-pi-badsym.so");
    void* h_badsym = loadPlugin("input", badsym_so, SI468X_PI_INPUT_ABI);
    check(h_badsym == nullptr, "missing required symbol rejected");

    void* h_badpath = loadPlugin("input", "/nonexistent/si468x-pi-missing.so", SI468X_PI_INPUT_ABI);
    check(h_badpath == nullptr, "bad path rejected");

    void* h_empty = loadPlugin("input", "", SI468X_PI_INPUT_ABI);
    check(h_empty == nullptr, "empty name rejected");
}

void test_has_input_sources()
{
    using namespace si468x_pi;

    InputQueue queue;
    std::atomic<bool> stop(false);
    const StopPredicate stop_requested = [&] { return stop.load(); };

    check(startInputSources(nlohmann::json::object(), queue, stop_requested, false).empty(),
          "non-array input json starts nothing");

    nlohmann::json stdin_source = nlohmann::json::array();
    stdin_source.push_back({ { "type", "stdin" } });
    nlohmann::json plugin_source = nlohmann::json::array();
    plugin_source.push_back({ { "plugin", "evdev" } });

    check(!hasInputSources(nlohmann::json(), false), "default without terminal has no sources");
    check(hasInputSources(nlohmann::json(), true), "default with terminal has source");
    check(!hasInputSources(nlohmann::json::array(), false), "empty array has no sources");
    check(hasInputSources(stdin_source, true), "stdin with terminal has source");
    check(!hasInputSources(stdin_source, false), "stdin without terminal has no sources");
    check(hasInputSources(plugin_source, false), "plugin without terminal has source");
    check(!hasInputSources(nlohmann::json::object(), false), "object has no sources");

    Settings default_s;
    check(!hasInputSources(default_s, false), "default Settings without terminal");
    check(hasInputSources(default_s, true), "default Settings with terminal");

    Settings empty_arr_s;
    empty_arr_s.input_configured = true;
    check(!hasInputSources(empty_arr_s, false), "empty configured Settings has no sources");

    Settings plugin_s;
    plugin_s.input_configured = true;
    plugin_s.input.push_back(InputConfig{ "", "example", "", "{}" });
    check(hasInputSources(plugin_s, false), "plugin Settings without terminal has source");
}

} // namespace

int main()
{
    std::cout << "si468x-pi plugin tests" << std::endl;

    test_input_plugin();
    test_display_plugin();
    test_rejections_and_naming();
    test_has_input_sources();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all plugin tests passed" << std::endl;
    return 0;
}
