// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// si468x-pi - terminal control application for the uGreen DAB Board.
// CLI, settings, cache and station table; RF handling lives in radio_controller.

#include "core/events.hpp"
#include "core/radio_controller.hpp"
#include "core/settings.hpp"
#include "core/station.hpp"
#include "core/version.hpp"
#include "si468x.h"
#include "ui/demo_ui.hpp"
#include "ui/display_interface.hpp"
#include "ui/input_queue.hpp"
#include "ui/input_sources.hpp"
#include "ui/plugin_display.hpp"
#include "ui/screens.hpp"
#include "ui/tty_display.hpp"
#include "ui/ui_manager.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#include <getopt.h>

namespace {

std::atomic<bool> g_stop_requested{ false };

void handle_signal(int sig)
{
    (void)sig;
    g_stop_requested = true;
}

void install_signal_handlers()
{
    struct sigaction sa = {};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

void print_usage(FILE* out)
{
    std::fprintf(out,
                 "Usage: si468x-pi [options]\n"
                 "\n"
                 "Options:\n"
                 "  -c, --config FILE       settings file (default: XDG search path)\n"
                 "  -s, --spi DEV           SPI device (default /dev/spidev0.0)\n"
                 "  -g, --reset-gpio N      BCM reset pin (default 23)\n"
                 "  -m, --mode dab|fmhd     boot mode (default dab)\n"
                 "  -a, --audio analog|i2s|both\n"
                 "  -S, --service-id N      play this service after tuning its channel\n"
                 "  -p, --programme NAME    play the first station whose label contains NAME\n"
                 "  -n, --no-scan           use the cache; do not scan if it is empty\n"
                 "      --rescan            scan even if the cache is non-empty\n"
                 "      --log               enable libsi468x diagnostic logging\n"
                 "      --config-example    print the example JSON and exit\n"
                 "  -v, --verbose           enable diagnostics\n"
                 "  -h, --help              show this help\n"
                 "      --version           show the version\n");
}

void print_version()
{
    std::printf("si468x-pi %s\n", SI468X_PI_VERSION);
}

bool parse_int(const char* text, int& out)
{
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    long v = std::strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

void print_warnings(const std::vector<std::string>& warnings)
{
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "si468x-pi: warning: %s\n", w.c_str());
    }
}

class AppSink : public si468x_pi::IControllerEvents {
public:
    AppSink(std::shared_ptr<si468x_pi::RadioScreen> r,
            std::shared_ptr<si468x_pi::StationListScreen> sl,
            std::shared_ptr<si468x_pi::ScanningScreen> sc,
            std::shared_ptr<si468x_pi::InfoScreen> inf,
            si468x_pi::UIManager* u)
        : m_radio(r), m_station_list(sl), m_scanning(sc), m_info(inf), m_ui(u)
    {
    }

    void onState(si468x_pi::ControllerState s) override
    {
        if (s == si468x_pi::ControllerState::Playing && m_ui) {
            m_ui->resetToRoot();
        }
    }

    void onScanProgress(const std::string& ch, int found, int cur, int tot) override
    {
        (void)cur;
        (void)tot;
        m_scanning->setScanningStatus(ch, found);
    }

    void onStation(const si468x_pi::Station& st) override
    {
        m_radio->setChannelName(st.channel);
        m_radio->setProgramName(st.program, st.short_program);
    }

    void onStationListChanged(const std::vector<si468x_pi::Station>& stations) override
    {
        m_station_list->setStations(stations);
    }

    void onSignal(const si468x_signal_status_t& sig) override
    {
        m_info->setSignal(sig);
        m_radio->setSignalPresent(sig.sync_status != 0);
    }

    void onDls(const std::string& dls) override
    {
        m_radio->setDLS(dls);
    }

    void onTime(const si468x_time_t& t) override
    {
        m_info->setTime(t);
    }

    void onAudio(const si468x_audio_info_t& a) override
    {
        m_info->setAudio(a);
    }

    void onEnsemble(const si468x_ensemble_info_t& ens) override
    {
        m_info->setEnsemble(ens);
    }

    void onVolume(int pct, bool muted) override
    {
        m_ui->showVolume(pct, muted);
    }

    void onError(const std::string& msg) override
    {
        std::fprintf(stderr, "si468x-pi: error: %s\n", msg.c_str());
    }

    void onSignalLost(bool lost) override
    {
        m_radio->setSignalPresent(!lost);
    }

private:
    std::shared_ptr<si468x_pi::RadioScreen> m_radio;
    std::shared_ptr<si468x_pi::StationListScreen> m_station_list;
    std::shared_ptr<si468x_pi::ScanningScreen> m_scanning;
    std::shared_ptr<si468x_pi::InfoScreen> m_info;
    si468x_pi::UIManager* m_ui;
};

int run_application(const si468x_pi::Settings& settings, const si468x_pi::Cache& cache,
                    const std::string& cache_path, bool rescan, bool no_scan,
                    int service_id, const std::string& programme)
{
    install_signal_handlers();

    const auto [tty_cols, tty_rows] = si468x_pi::ttySize(settings);

    std::vector<std::unique_ptr<si468x_pi::IDisplay>> displays;
    if (isatty(STDOUT_FILENO)) {
        displays.push_back(std::make_unique<si468x_pi::TtyDisplay>(tty_cols, tty_rows));
    }
    for (const auto& dcfg : settings.display) {
        if (!dcfg.plugin.empty()) {
            auto pdisp = si468x_pi::PluginDisplay::open(dcfg.plugin, dcfg.config_json);
            if (pdisp) {
                displays.push_back(std::move(pdisp));
            } else {
                std::fprintf(stderr, "si468x-pi: warning: failed to open display plugin '%s'\n",
                             dcfg.plugin.c_str());
            }
        }
    }
    if (displays.empty()) {
        displays.push_back(std::make_unique<si468x_pi::TtyDisplay>(tty_cols, tty_rows));
    }

    auto multi_display = std::make_unique<si468x_pi::MultiDisplay>(std::move(displays));
    si468x_pi::UIManager ui(std::move(multi_display), 20);

    auto radio = std::make_shared<si468x_pi::RadioScreen>();
    auto station_list = std::make_shared<si468x_pi::StationListScreen>(cache.stations, &ui);
    auto menu = std::make_shared<si468x_pi::MenuScreen>(&ui);
    auto scanning = std::make_shared<si468x_pi::ScanningScreen>();
    auto info = std::make_shared<si468x_pi::InfoScreen>(&ui);

    AppSink sink(radio, station_list, scanning, info, &ui);

    si468x_pi::RadioController controller(settings, &sink, cache_path);
    controller.applyCache(cache);

    if (int rc = controller.initHardware()) {
        std::fprintf(stderr, "si468x-pi: hardware init failed: %s\n",
                     si468x_pi::RadioController::formatError(rc).c_str());
        return 1;
    }
    controller.start();

    auto play_next_station = [&](int delta) {
        const auto stations = controller.stations();
        if (stations.empty()) {
            return;
        }
        const auto cur = controller.currentStation();
        size_t idx = 0;
        for (size_t i = 0; i < stations.size(); ++i) {
            if (stations[i].service_id == cur.service_id &&
                (cur.channel.empty() || stations[i].channel == cur.channel)) {
                idx = i;
                break;
            }
        }
        const size_t next_idx = (idx + delta + stations.size()) % stations.size();
        station_list->setIndex(next_idx);
        controller.postPlay(stations[next_idx]);
    };

    radio->setInputCallback([&](si468x_pi::InputAction action) {
        if (action == si468x_pi::InputAction::MENU) {
            ui.pushScreen(menu);
        } else if (action == si468x_pi::InputAction::ENTER) {
            ui.pushScreen(station_list);
        } else if (action == si468x_pi::InputAction::UP || action == si468x_pi::InputAction::LEFT) {
            play_next_station(-1);
        } else if (action == si468x_pi::InputAction::DOWN || action == si468x_pi::InputAction::RIGHT) {
            play_next_station(1);
        }
    });

    station_list->setSelectCallback([&](const si468x_pi::Station& st) {
        controller.postPlay(st);
        ui.resetToRoot();
    });

    scanning->setAbortCallback([&]() {
        controller.abortScan();
        ui.resetToRoot();
    });

    menu->setSelectCallback([&](int index) {
        if (index == 0) {
            ui.pushScreen(scanning);
            controller.postScan(true);
        } else if (index == 1) {
            ui.pushScreen(station_list);
        } else if (index == 2) {
            ui.pushScreen(info);
        } else if (index == 4) {
            g_stop_requested = true;
        }
    });

    ui.pushScreen(radio);

    si468x_pi::InputQueue input_queue;
    auto input_threads = si468x_pi::startInputSources(
        settings, input_queue, [&]() { return g_stop_requested.load(); }, isatty(STDIN_FILENO));

    const auto st_list = controller.stations();
    if (rescan || (st_list.empty() && !no_scan)) {
        ui.pushScreen(scanning);
        controller.postScan(true);
    } else if (!st_list.empty()) {
        si468x_pi::Station target = st_list.front();
        if (service_id != 0) {
            for (size_t i = 0; i < st_list.size(); ++i) {
                if (st_list[i].service_id == static_cast<uint32_t>(service_id)) {
                    target = st_list[i];
                    station_list->setIndex(i);
                    break;
                }
            }
        } else if (!programme.empty()) {
            for (size_t i = 0; i < st_list.size(); ++i) {
                if (st_list[i].program.find(programme) != std::string::npos) {
                    target = st_list[i];
                    station_list->setIndex(i);
                    break;
                }
            }
        } else if (cache.last_played.valid()) {
            const auto& lp = cache.last_played;
            const bool fm = settings.device.mode == "fmhd" || lp.band == "fm";
            // FM: frequency or label; DAB: service id, on the same channel if known
            const auto it = std::find_if(st_list.begin(), st_list.end(), [&](const si468x_pi::Station& st) {
                if (fm) {
                    return (lp.service_id != 0 && st.service_id == lp.service_id) ||
                           (!lp.channel.empty() && st.channel == lp.channel);
                }
                return lp.service_id != 0 && st.service_id == lp.service_id &&
                       (lp.channel.empty() || st.channel == lp.channel);
            });
            if (it != st_list.end()) {
                target = *it;
                station_list->setIndex(it - st_list.begin());
            } else if (fm) {
                target.channel = lp.channel;
                target.service_id = lp.service_id;
                target.band = "fm";
                target.program = lp.channel.empty() ? "" : lp.channel + " MHz";
            }
        }
        controller.postPlay(target);
    }

    while (!g_stop_requested.load()) {
        si468x_pi::InputAction action = input_queue.pop(100);
        if (g_stop_requested.load()) {
            break;
        }
        if (action == si468x_pi::InputAction::NONE) {
            continue;
        }
        if (action == si468x_pi::InputAction::QUIT) {
            break;
        }
        if (action == si468x_pi::InputAction::VOLUME_UP) {
            controller.postVolumeUp();
            continue;
        }
        if (action == si468x_pi::InputAction::VOLUME_DOWN) {
            controller.postVolumeDown();
            continue;
        }
        if (action == si468x_pi::InputAction::MUTE) {
            controller.postMute();
            continue;
        }
        ui.processInput(action);
    }

    g_stop_requested = true;
    input_queue.push(si468x_pi::InputAction::QUIT);
    for (auto& t : input_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    controller.saveCache(cache_path);
    controller.stop();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string config_path;
    int service_id = 0;
    std::string programme;
    bool no_scan = false;
    bool rescan = false;
    bool verbose = false;
    bool demo_ui = false;
    bool log_enabled = false;

    std::optional<std::string> spi;
    std::optional<std::string> mode;
    std::optional<std::string> audio;
    std::optional<int> gpio;

    enum {
        OPT_RESCAN = 1000,
        OPT_CONFIG_EXAMPLE,
        OPT_VERSION,
        OPT_DEMO_UI,
        OPT_LOG,
    };

    static const struct option long_options[] = {
        { "config", required_argument, nullptr, 'c' },
        { "spi", required_argument, nullptr, 's' },
        { "reset-gpio", required_argument, nullptr, 'g' },
        { "mode", required_argument, nullptr, 'm' },
        { "audio", required_argument, nullptr, 'a' },
        { "service-id", required_argument, nullptr, 'S' },
        { "programme", required_argument, nullptr, 'p' },
        { "no-scan", no_argument, nullptr, 'n' },
        { "rescan", no_argument, nullptr, OPT_RESCAN },
        { "log", no_argument, nullptr, OPT_LOG },
        { "config-example", no_argument, nullptr, OPT_CONFIG_EXAMPLE },
        { "verbose", no_argument, nullptr, 'v' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, OPT_VERSION },
        { "demo-ui", no_argument, nullptr, OPT_DEMO_UI },
        { nullptr, 0, nullptr, 0 },
    };

    int option = 0;
    while ((option = getopt_long(argc, argv, "c:s:g:m:a:S:p:nvh", long_options, nullptr)) != -1) {
        switch (option) {
        case 'c':
            config_path = optarg;
            break;
        case 's':
            spi = optarg;
            break;
        case 'g': {
            int value = 0;
            if (!parse_int(optarg, value) || value < 0 || value > 53) {
                std::fprintf(stderr, "si468x-pi: invalid --reset-gpio value '%s'\n", optarg);
                return 2;
            }
            gpio = value;
            break;
        }
        case 'm':
            mode = optarg;
            if (*mode != "dab" && *mode != "fmhd") {
                std::fprintf(stderr, "si468x-pi: invalid --mode value '%s'\n", optarg);
                return 2;
            }
            break;
        case 'a':
            audio = optarg;
            if (*audio != "analog" && *audio != "i2s" && *audio != "both") {
                std::fprintf(stderr, "si468x-pi: invalid --audio value '%s'\n", optarg);
                return 2;
            }
            break;
        case 'S': {
            int value = 0;
            if (!parse_int(optarg, value) || value <= 0) {
                std::fprintf(stderr, "si468x-pi: invalid --service-id value '%s'\n", optarg);
                return 2;
            }
            service_id = value;
            break;
        }
        case 'p':
            programme = optarg;
            break;
        case 'n':
            no_scan = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_usage(stdout);
            return 0;
        case OPT_RESCAN:
            rescan = true;
            break;
        case OPT_LOG:
            log_enabled = true;
            break;
        case OPT_CONFIG_EXAMPLE:
            std::fputs(si468x_pi::example_settings_json().c_str(), stdout);
            return 0;
        case OPT_VERSION:
            print_version();
            return 0;
        case OPT_DEMO_UI:
            demo_ui = true;
            break;
        default:
            print_usage(stderr);
            return 2;
        }
    }

    if (optind < argc) {
        std::fprintf(stderr, "si468x-pi: unexpected argument '%s'\n", argv[optind]);
        return 2;
    }

    if (log_enabled) {
        si468x_enable_debug(1);
    }

    si468x_pi::Settings settings;
    std::vector<std::string> warnings;

    const std::string settings_path =
        config_path.empty() ? si468x_pi::default_settings_path() : config_path;
    if (!si468x_pi::load_settings(settings_path, settings, &warnings)) {
        print_warnings(warnings);
        std::fprintf(stderr, "si468x-pi: cannot read settings file '%s'\n", settings_path.c_str());
        return 1;
    }
    print_warnings(warnings);

    settings.device.spi = spi.value_or(settings.device.spi);
    settings.device.reset_gpio = gpio.value_or(settings.device.reset_gpio);
    settings.device.mode = mode.value_or(settings.device.mode);
    settings.device.audio = audio.value_or(settings.device.audio);

    si468x_pi::Cache cache;
    warnings.clear();
    const std::string cache_path = si468x_pi::default_cache_path(settings.device.mode);
    si468x_pi::load_cache(cache_path, cache, &warnings);
    print_warnings(warnings);

    if (demo_ui) {
        return si468x_pi::run_demo_ui(settings, cache);
    }

    if (verbose) {
        std::fprintf(stderr, "si468x-pi: settings '%s', cache '%s'\n", settings_path.c_str(),
                     cache_path.c_str());
    }

    return run_application(settings, cache, cache_path, rescan, no_scan, service_id, programme);
}
