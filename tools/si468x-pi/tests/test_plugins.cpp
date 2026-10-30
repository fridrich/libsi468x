// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.h"
#include "input_sources.hpp"
#include "plugin_display.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/plugin.h"

#include <atomic>
#include <fstream>
#include <linux/input.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// PLUGIN_DIR: the built plugins; TEST_PLUGIN_DIR: the rejection cases (set by Makefile.am)
std::string built(const char* dir, const std::string& name)
{
    const std::string path = std::string(dir) + "/si468x-pi-" + name + ".so";
    return access(path.c_str(), F_OK) == 0 ? path : std::string();
}

void test_loader()
{
    using namespace si468x_pi;

    const std::string lcd = built(PLUGIN_DIR, "lcd");
    if (!lcd.empty()) {
        check(loadPlugin("display", lcd, SI468X_PI_DISPLAY_ABI) != nullptr, "load plugin by absolute path");
    }
    const std::string badabi = built(TEST_PLUGIN_DIR, "badabi");
    const std::string badsym = built(TEST_PLUGIN_DIR, "badsym");
    check(!badabi.empty() && !badsym.empty(), "rejection test plugins built");
    check(!loadPlugin("input", badabi, SI468X_PI_INPUT_ABI), "mismatched ABI rejected");
    check(!loadPlugin("input", badsym, SI468X_PI_INPUT_ABI), "missing name symbols rejected");
    check(!loadPlugin("input", "/nonexistent/si468x-pi-missing.so", SI468X_PI_INPUT_ABI), "bad path rejected");
    check(!loadPlugin("input", "", SI468X_PI_INPUT_ABI), "empty name rejected");

    check(!PluginDisplay::open("nonexistent", "{}"), "open nonexistent plugin fails");
    check(!PluginDisplay::open("/nonexistent/plugin.so", "{}"), "open nonexistent path fails");
    check(!PluginDisplay::open("", "{}"), "empty plugin name fails");
}

void test_evdev_plugin()
{
    using namespace si468x_pi;

    const std::string evdev = built(PLUGIN_DIR, "evdev");
    if (evdev.empty()) {
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

    // broken entries are skipped, stdin needs a terminal: only evdev starts
    Settings s;
    std::vector<std::string> warnings;
    parse_settings("{\"input\": [{\"plugin\": \"" + evdev + "\", \"device\": \"/dev/fd/" + std::to_string(pfd[0])
                       + "\"}, {\"plugin\": \"nonexistent\"}, {\"plugin\": \"/nonexistent/plugin.so\"},"
                         " {\"type\": \"bogus\"}, {\"type\": \"stdin\"}]}",
                   s, &warnings);
    auto threads = startInputSources(s, queue, stop_requested, false);
    check(threads.size() == 1, "only the evdev thread starts");

    auto inject_key = [&](uint16_t code) {
        struct input_event ev[2] = {};
        ev[0].type = EV_KEY;
        ev[0].code = code;
        ev[0].value = 1;
        ev[1].type = EV_SYN;
        ev[1].code = SYN_REPORT;
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

    const std::string fb = built(PLUGIN_DIR, "fb");
    if (fb.empty()) {
        std::cout << "skip: fb plugin not built" << std::endl;
        return;
    }

    const std::string out_path = "/tmp/si468x-pi-test-fb-" + std::to_string(getpid()) + ".bin";
    unlink(out_path.c_str());
    {
        auto disp = PluginDisplay::open(fb, "{\"file\":\"" + out_path + "\"}");
        check(disp != nullptr, "open fb display plugin with test file");
        if (disp) {
            check(disp->cols() == 16 && disp->rows() == 2, "fb geometry 16x2");
            check(disp->canHighlight(), "fb supports highlight");

            Frame frame(16, 2, 0);
            frame.set(0, "Radio Playing   ");
            frame.set(1, "12A DAB+        ");
            frame.highlight = 0;
            disp->present(frame);
        }
    }

    std::ifstream in(out_path, std::ios::binary | std::ios::ate);
    check(in && in.tellg() == 128 * 16 * 2, "fb output is exactly 128x16 at 16 bpp (4096 bytes)");
    unlink(out_path.c_str());
}

void test_lcd_plugin()
{
    using namespace si468x_pi;

    const std::string lcd = built(PLUGIN_DIR, "lcd");
    if (lcd.empty()) {
        std::cout << "skip: lcd plugin not built" << std::endl;
        return;
    }

    const std::string out_path = "/tmp/si468x-pi-test-lcd-" + std::to_string(getpid()) + ".txt";
    unlink(out_path.c_str());
    {
        auto disp = PluginDisplay::open(lcd, "{\"file\":\"" + out_path + "\"}");
        check(disp != nullptr, "open lcd display plugin with test file");
        if (disp) {
            check(disp->cols() == 16 && disp->rows() == 2, "lcd geometry 16x2");
            check(!disp->canHighlight(), "lcd does not support highlight");

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
    check(line1.size() == 16 && line2.size() == 16, "lcd lines 16 wide");
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

    test_loader();
    test_evdev_plugin();
    test_fb_plugin();
    test_lcd_plugin();

    return report("plugin");
}
