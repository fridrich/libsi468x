// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// si468x-pi - terminal control application for the uGreen DAB Board.
// CLI, settings, cache and station table; RF handling lives in radio_controller.

#include "core/settings.hpp"
#include "core/station.hpp"
#include "core/version.hpp"
#include "ui/demo_ui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <getopt.h>

namespace {

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

void print_summary(const si468x_pi::Settings& s, const si468x_pi::Cache& cache, bool verbose)
{
    std::printf("device: %s reset_gpio=%d mode=%s audio=%s region=%s\n", s.device.spi.c_str(),
                s.device.reset_gpio, s.device.mode.c_str(), s.device.audio.c_str(),
                s.device.region.c_str());
    std::printf("scan: %zu channels, rssi_min=%d, include_data=%s\n", s.scan.channels.size(),
                s.scan.rssi_min, s.scan.include_data ? "true" : "false");
    std::printf("cache: %zu stations, complete=%s\n", cache.stations.size(),
                cache.complete ? "true" : "false");
    if (verbose) {
        std::printf("input: %zu configured\n", s.input.size());
        std::printf("display: %zu configured\n", s.display.size());
        for (const si468x_pi::Station& st : cache.stations) {
            std::printf("  %-4s %-32s sid=%u comp=0x%04x%s\n", st.channel.c_str(),
                        st.program.c_str(), st.service_id, st.component_id,
                        st.data ? " [data]" : "");
        }
    }
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

    bool have_spi = false;
    bool have_gpio = false;
    bool have_mode = false;
    bool have_audio = false;
    std::string spi;
    std::string mode;
    std::string audio;
    int gpio = 0;

    enum {
        OPT_RESCAN = 1000,
        OPT_CONFIG_EXAMPLE,
        OPT_VERSION,
        OPT_DEMO_UI,
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
            have_spi = true;
            break;
        case 'g':
            if (!parse_int(optarg, gpio) || gpio < 0 || gpio > 53) {
                std::fprintf(stderr, "si468x-pi: invalid --reset-gpio value '%s'\n", optarg);
                return 2;
            }
            have_gpio = true;
            break;
        case 'm':
            mode = optarg;
            if (mode != "dab" && mode != "fmhd") {
                std::fprintf(stderr, "si468x-pi: invalid --mode value '%s'\n", optarg);
                return 2;
            }
            have_mode = true;
            break;
        case 'a':
            audio = optarg;
            if (audio != "analog" && audio != "i2s" && audio != "both") {
                std::fprintf(stderr, "si468x-pi: invalid --audio value '%s'\n", optarg);
                return 2;
            }
            have_audio = true;
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

    if (have_spi) {
        settings.device.spi = spi;
    }
    if (have_gpio) {
        settings.device.reset_gpio = gpio;
    }
    if (have_mode) {
        settings.device.mode = mode;
    }
    if (have_audio) {
        settings.device.audio = audio;
    }

    si468x_pi::Cache cache;
    warnings.clear();
    const std::string cache_path = si468x_pi::default_cache_path();
    si468x_pi::load_cache(cache_path, cache, &warnings);
    print_warnings(warnings);

    if (demo_ui) {
        return si468x_pi::run_demo_ui(settings, cache);
    }

    if (verbose) {
        std::fprintf(stderr, "si468x-pi: settings '%s', cache '%s'\n", settings_path.c_str(),
                     cache_path.c_str());
        if (no_scan) {
            std::fprintf(stderr, "si468x-pi: --no-scan selected\n");
        }
        if (rescan) {
            std::fprintf(stderr, "si468x-pi: --rescan selected\n");
        }
        if (service_id) {
            std::fprintf(stderr, "si468x-pi: service id %d\n", service_id);
        }
        if (!programme.empty()) {
            std::fprintf(stderr, "si468x-pi: programme '%s'\n", programme.c_str());
        }
    }

    print_summary(settings, cache, verbose);
    std::fprintf(stderr, "si468x-pi: radio controller is not available in this build\n");
    return 0;
}
