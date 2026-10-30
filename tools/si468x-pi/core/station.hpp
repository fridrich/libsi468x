// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace si468x_pi {

/// One DAB Band III channel from the canonical table.
struct Channel {
    const char* label;
    uint32_t frequency_hz;
};

/// A station identity is (channel, service_id).  component_id is the raw
/// 16-bit component word from si468x_service_t and is passed unchanged to
/// si468x_play_service().
struct Station {
    std::string channel;
    std::string program;
    std::string short_program;
    uint32_t service_id = 0;
    uint16_t component_id = 0;
    bool data = false;
    std::string band = "dab";
};

/// Full Band III table, including the 10N/11N/12N entries used by the chip's
/// frequency index table.  Scan defaults deliberately skip the N channels.
const std::vector<Channel>& band3_channels();

/// Look up a label ("12A", "10N", ...).  Returns nullptr for unknown labels.
const Channel* find_channel(const std::string& label);

/// Canonical 38-channel DAB scan list (no N channels).
const std::vector<std::string>& default_scan_channels();

/// FM stations: service_id, else the channel parsed as MHz; 0 if neither.
uint32_t fm_frequency_hz(const Station& s);

void sort_stations(std::vector<Station>& stations);

/// Remove duplicates by (channel, service_id), drop entries with an empty
/// program or an unknown channel, and sort by program/channel.
void normalize_stations(std::vector<Station>& stations);

} // namespace si468x_pi
