// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote_config.hpp"
#include "si468x-pi/input_plugin.h"

#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

extern "C" {
const char si468x_pi_plugin_name[] = "evdev";
const char si468x_pi_plugin_description[] = "Linux evdev input (/dev/input/event*)";
const int si468x_pi_input_abi = SI468X_PI_INPUT_ABI;

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config);
}

namespace {

int find_best_device(const std::map<int, enum si468x_pi_action>& key_map)
{
    int best_fd = -1;
    int best_matches = 0;

    for (int i = 0; i < 32; ++i) {
        const std::string path = "/dev/input/event" + std::to_string(i);
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        uint8_t key_bits[(KEY_MAX + 7) / 8] = {};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
            close(fd);
            continue;
        }

        int matches = 0;
        for (const auto& kv : key_map) {
            int code = kv.first;
            if (code >= 0 && code <= KEY_MAX) {
                if (key_bits[code / 8] & (1 << (code % 8))) {
                    ++matches;
                }
            }
        }

        if (matches > best_matches) {
            if (best_fd >= 0) {
                close(best_fd);
            }
            best_fd = fd;
            best_matches = matches;
        } else {
            close(fd);
        }
    }

    return best_fd;
}

} // namespace

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config)
{
    if (!host) {
        return -1;
    }

    const si468x_pi::EvdevConfig cfg = si468x_pi::parse_evdev_config(config);

    int fd = -1;
    if (!cfg.device.empty()) {
        fd = open(cfg.device.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "evdev: failed to open " << cfg.device << std::endl;
            return 0; // Skip missing device cleanly without terminating radio
        }
    } else {
        fd = find_best_device(cfg.key_map);
        if (fd < 0) {
            std::cerr << "evdev: no matching /dev/input/event* device found" << std::endl;
            return 0;
        }
    }

    if (cfg.grab) {
        ioctl(fd, EVIOCGRAB, 1);
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!host->stop_requested(host->ctx)) {
        int pr = poll(&pfd, 1, 100);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            continue;
        }

        struct input_event ev[16];
        ssize_t bytes = read(fd, ev, sizeof(ev));
        if (bytes <= 0) {
            break;
        }

        const size_t count = static_cast<size_t>(bytes) / sizeof(struct input_event);
        for (size_t i = 0; i < count; ++i) {
            if (ev[i].type == EV_KEY && (ev[i].value == 1 || ev[i].value == 2)) {
                auto it = cfg.key_map.find(ev[i].code);
                if (it != cfg.key_map.end()) {
                    host->push(host->ctx, it->second);
                }
            }
        }
    }

    if (cfg.grab) {
        ioctl(fd, EVIOCGRAB, 0);
    }
    close(fd);
    return 0;
}
