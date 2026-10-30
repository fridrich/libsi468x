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
#include <linux/input.h>
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

void test_evdev_plugin()
{
    using namespace si468x_pi;

    std::string evdev_so = getPluginPath("si468x-pi-evdev.so");
    if (evdev_so.empty()) {
        std::cout << "skip: evdev plugin not built" << std::endl;
        return;
    }

    int pfd[2];
    if (pipe(pfd) != 0) {
        check(false, "pipe creation failed");
        return;
    }

    InputQueue queue;
    std::atomic<bool> stop(false);
    const StopPredicate stop_requested = [&] { return stop.load(); };

    const std::string dev_path = "/dev/fd/" + std::to_string(pfd[0]);
    nlohmann::json cfg = nlohmann::json::array({
        { { "plugin", "evdev" }, { "device", dev_path } }
    });

    auto threads = startInputSources(cfg, queue, stop_requested, false);
    check(!threads.empty(), "started evdev plugin thread");

    auto inject_key = [&](uint16_t code) {
        struct input_event ev[2] = {};
        ev[0].type = EV_KEY;
        ev[0].code = code;
        ev[0].value = 1;
        ev[1].type = EV_SYN;
        ev[1].code = SYN_REPORT;
        ev[1].value = 0;
        ssize_t w = write(pfd[1], ev, sizeof(ev));
        (void)w;
    };

    inject_key(KEY_UP);
    check(queue.pop(2000) == InputAction::UP, "evdev translates KEY_UP to UP");

    inject_key(KEY_DOWN);
    check(queue.pop(2000) == InputAction::DOWN, "evdev translates KEY_DOWN to DOWN");

    inject_key(KEY_ENTER);
    check(queue.pop(2000) == InputAction::ENTER, "evdev translates KEY_ENTER to ENTER");

    inject_key(KEY_VOLUMEUP);
    check(queue.pop(2000) == InputAction::VOLUME_UP, "evdev translates KEY_VOLUMEUP to VOLUME_UP");

    inject_key(KEY_MUTE);
    check(queue.pop(2000) == InputAction::MUTE, "evdev translates KEY_MUTE to MUTE");

    inject_key(KEY_POWER);
    check(queue.pop(2000) == InputAction::QUIT, "evdev translates KEY_POWER to QUIT");

    stop = true;
    close(pfd[1]);
    for (auto& t : threads) {
        t.join();
    }
    close(pfd[0]);
}

void test_fb_plugin()
{
    using namespace si468x_pi;

    std::string fb_so = getPluginPath("si468x-pi-fb.so");
    if (fb_so.empty()) {
        std::cout << "skip: fb plugin not built" << std::endl;
        return;
    }

    const std::string out_path = "/tmp/si468x-pi-test-fb-" + std::to_string(getpid()) + ".bin";
    unlink(out_path.c_str());

    {
        auto disp = PluginDisplay::open("fb", "{\"plugin\":\"fb\",\"file\":\"" + out_path + "\"}");
        check(disp != nullptr, "open fb display plugin with test file");
        if (disp) {
            check(disp->cols() == 16 && disp->rows() == 2, "fb geometry 16x2");
            check(disp->canHighlight() == true, "fb supports highlight");

            Frame frame(16, 2, 0);
            frame.set(0, "Radio Playing   ");
            frame.set(1, "12A DAB+        ");
            frame.highlight = 0;
            disp->present(frame);
        }
    }

    FILE* f = fopen(out_path.c_str(), "rb");
    check(f != nullptr, "fb output file created");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        check(sz == 128 * 16 * 2, "fb output size is exact 128x16 16bpp (4096 bytes)");
    }
    unlink(out_path.c_str());
}

void test_lcd_plugin()
{
    using namespace si468x_pi;

    std::string lcd_so = getPluginPath("si468x-pi-lcd.so");
    if (lcd_so.empty()) {
        std::cout << "skip: lcd plugin not built" << std::endl;
        return;
    }

    const std::string out_path = "/tmp/si468x-pi-test-lcd-" + std::to_string(getpid()) + ".txt";
    unlink(out_path.c_str());

    {
        auto disp = PluginDisplay::open("lcd", "{\"plugin\":\"lcd\",\"file\":\"" + out_path + "\"}");
        check(disp != nullptr, "open lcd display plugin with test file");
        if (disp) {
            check(disp->cols() == 16 && disp->rows() == 2, "lcd geometry 16x2");
            check(disp->canHighlight() == false, "lcd does not support highlight");

            Frame frame(16, 2, 0);
            frame.set(0, "Helene & Jörg   ");
            frame.set(1, "21°C äöü        ");
            disp->present(frame);
        }
    }

    std::ifstream in(out_path);
    std::string header, line1, line2;
    std::getline(in, header);
    std::getline(in, line1);
    std::getline(in, line2);
    unlink(out_path.c_str());

    check(header == "frame 16 x 2", "lcd header");
    check(line1.size() == 16, "lcd line 1 width 16");
    check(line2.size() == 16, "lcd line 2 width 16");
    check(static_cast<uint8_t>(line1[10]) == 0xEF, "ö mapped to 0xEF");
    check(static_cast<uint8_t>(line2[2]) == 0xDF, "° mapped to 0xDF");
    check(static_cast<uint8_t>(line2[5]) == 0xE1, "ä mapped to 0xE1");
    check(static_cast<uint8_t>(line2[6]) == 0xEF, "ö mapped to 0xEF");
    check(static_cast<uint8_t>(line2[7]) == 0xF5, "ü mapped to 0xF5");
}

} // namespace

int main()
{
    std::cout << "si468x-pi plugin tests" << std::endl;

    test_input_plugin();
    test_display_plugin();
    test_rejections_and_naming();
    test_has_input_sources();
    test_evdev_plugin();
    test_fb_plugin();
    test_lcd_plugin();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all plugin tests passed" << std::endl;
    return 0;
}
