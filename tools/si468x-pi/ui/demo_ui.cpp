// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo_ui.hpp"

#include "input_queue.hpp"
#include "input_sources.hpp"
#include "screens.hpp"
#include "tty_display.hpp"
#include "ui_manager.hpp"
#include "utils.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace si468x_pi {

namespace {

volatile std::sig_atomic_t g_signal_stop = 0;

void handleSignal(int)
{
    g_signal_stop = 1;
}

std::vector<Station> demo_stations()
{
    return {
        { "12A", "RTS Première", "RTS Prem", 17361, 0, false },
        { "12A", "Option Musique", "Option", 17400, 0, false },
        { "12A", "Couleur 3", "C3", 17410, 0, false },
        { "12A", "Data service", "Data", 17420, 0, true },
    };
}

} // namespace

int run_demo_ui(const Settings& settings, const Cache& cache)
{
    g_signal_stop = 0;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::vector<Station> stations = cache.stations.empty() ? demo_stations() : cache.stations;

    auto radio = std::make_shared<RadioScreen>();
    radio->setProgramName("si468x-pi demo", "si468x-pi");
    radio->setChannelName("DAB+ no hardware");
    radio->setDLS("Terminal demo: arrows navigate, Enter selects, m menu, +/- volume, 0 mute, x quits.");

    const auto [cols, rows] = ttySize(settings);
    UIManager ui(std::make_unique<TtyDisplay>(cols, rows), settings.ui.tick_ms);
    ui.pushScreen(radio);

    bool scanning = false;
    size_t scan_step = 0;
    uint64_t scan_next = 0;
    int scan_found = 0;
    auto scanning_screen = std::make_shared<ScanningScreen>();

    auto start_scan = [&]() {
        scanning = true;
        scan_step = 0;
        scan_found = static_cast<int>(stations.size());
        scan_next = now_ms() + 200;
        const std::string first =
            settings.scan.channels.empty() ? std::string() : settings.scan.channels.front();
        scanning_screen->setScanningStatus(first, scan_found);
        ui.pushScreen(scanning_screen);
    };

    auto menu = std::make_shared<MenuScreen>(&ui);
    menu->setSelectCallback([&](int index) {
        if (index == 0) {
            start_scan();
        } else if (index == 1) {
            auto list = std::make_shared<StationListScreen>(stations, &ui);
            list->setSelectCallback([&](const Station& station) {
                radio->setChannelName(station.channel + " · DAB+ (demo)");
                radio->setProgramName(station.program, station.short_program);
                radio->setDLS("Playing " + station.program + " (demo)");
                radio->setSignalPresent(true);
                ui.resetToRoot();
            });
            ui.pushScreen(list);
        } else if (index == 2) {
            radio->setDLS("Info: RSSI --, SNR --, CNR -- (demo)");
            ui.resetToRoot();
        } else if (index == 4) {
            g_signal_stop = 1;
        }
    });

    radio->setInputCallback([&](InputAction action) {
        if (action == InputAction::MENU) {
            ui.pushScreen(menu);
        }
    });

    InputQueue queue;
    std::atomic<bool> stop { false };
    std::thread stdin_thread([&]() {
        runStdinReader(queue, [&]() { return stop.load() || g_signal_stop != 0; });
    });

    int volume = std::max(0, std::min(100, settings.volume.start));
    int step = std::max(1, settings.volume.step);
    bool muted = false;

    while (g_signal_stop == 0) {
        const uint64_t now = now_ms();
        if (scanning && now >= scan_next) {
            const size_t limit = std::min<size_t>(settings.scan.channels.size(), 6);
            if (scan_step < limit) {
                scanning_screen->setScanningStatus(settings.scan.channels[scan_step], scan_found);
                ++scan_step;
                scan_next = now + 250;
            } else {
                scanning = false;
                radio->setChannelName("DAB+ no hardware");
                radio->setDLS("Demo scan complete: " + std::to_string(scan_found) + " stations.");
                ui.resetToRoot();
            }
        }

        const InputAction action = queue.pop(settings.ui.tick_ms);
        if (action == InputAction::NONE) {
            continue;
        }
        if (action == InputAction::QUIT) {
            break;
        }
        if (action == InputAction::VOLUME_UP) {
            muted = false;
            volume = std::min(100, volume + step);
            ui.showVolume(volume, false);
            continue;
        }
        if (action == InputAction::VOLUME_DOWN) {
            muted = false;
            volume = std::max(0, volume - step);
            ui.showVolume(volume, false);
            continue;
        }
        if (action == InputAction::MUTE) {
            muted = !muted;
            ui.showVolume(volume, muted);
            continue;
        }
        if (scanning && action == InputAction::MENU) {
            scanning = false;
            ui.resetToRoot();
            continue;
        }
        ui.processInput(action);
    }

    stop = true;
    queue.push(InputAction::QUIT);
    if (stdin_thread.joinable()) {
        stdin_thread.join();
    }
    return 0;
}

} // namespace si468x_pi
