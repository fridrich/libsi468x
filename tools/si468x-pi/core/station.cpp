// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "station.hpp"

#include <algorithm>

namespace si468x_pi {

const std::vector<Channel>& band3_channels()
{
    // Must match the table in libsi468x src/si468x.cpp (si468x_get_freq_index).
    static const std::vector<Channel> table = {
        { "5A", 174928000 },  { "5B", 176640000 },  { "5C", 178352000 },  { "5D", 180064000 },
        { "6A", 181936000 },  { "6B", 183648000 },  { "6C", 185360000 },  { "6D", 187072000 },
        { "7A", 188928000 },  { "7B", 190640000 },  { "7C", 192352000 },  { "7D", 194064000 },
        { "8A", 195936000 },  { "8B", 197648000 },  { "8C", 199360000 },  { "8D", 201072000 },
        { "9A", 202928000 },  { "9B", 204640000 },  { "9C", 206352000 },  { "9D", 208064000 },
        { "10A", 209936000 }, { "10N", 210096000 }, { "10B", 211648000 }, { "10C", 213360000 },
        { "10D", 215072000 }, { "11A", 216928000 }, { "11N", 217088000 }, { "11B", 218640000 },
        { "11C", 220352000 }, { "11D", 222064000 }, { "12A", 223936000 }, { "12N", 224096000 },
        { "12B", 225648000 }, { "12C", 227360000 }, { "12D", 229072000 }, { "13A", 230784000 },
        { "13B", 232496000 }, { "13C", 234208000 }, { "13D", 235776000 }, { "13E", 237488000 },
        { "13F", 239200000 },
    };
    return table;
}

const Channel* find_channel(const std::string& label)
{
    for (const Channel& c : band3_channels()) {
        if (label == c.label) {
            return &c;
        }
    }
    return nullptr;
}

uint32_t channel_frequency(const std::string& label)
{
    const Channel* c = find_channel(label);
    return c ? c->frequency_hz : 0;
}

const std::vector<std::string>& default_scan_channels()
{
    static const std::vector<std::string> list = {
        "5A",  "5B",  "5C",  "5D",  "6A",  "6B",  "6C",  "6D",  "7A",  "7B",
        "7C",  "7D",  "8A",  "8B",  "8C",  "8D",  "9A",  "9B",  "9C",  "9D",
        "10A", "10B", "10C", "10D", "11A", "11B", "11C", "11D", "12A", "12B",
        "12C", "12D", "13A", "13B", "13C", "13D", "13E", "13F",
    };
    return list;
}

bool same_station(const Station& a, const Station& b)
{
    return a.channel == b.channel && a.service_id == b.service_id;
}

void sort_stations(std::vector<Station>& stations)
{
    std::stable_sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) {
        if (a.band != b.band) {
            return a.band < b.band;
        }
        if (a.band == "fm") {
            uint32_t fa = a.service_id;
            uint32_t fb = b.service_id;
            if (fa == 0 && !a.channel.empty()) {
                try {
                    fa = static_cast<uint32_t>(std::stod(a.channel) * 1000000.0 + 0.5);
                } catch (...) {}
            }
            if (fb == 0 && !b.channel.empty()) {
                try {
                    fb = static_cast<uint32_t>(std::stod(b.channel) * 1000000.0 + 0.5);
                } catch (...) {}
            }
            if (fa != fb) {
                return fa < fb;
            }
            if (a.component_id != b.component_id) {
                return a.component_id < b.component_id;
            }
            if (a.program != b.program) {
                return a.program < b.program;
            }
            return a.channel < b.channel;
        }
        if (a.program != b.program) {
            return a.program < b.program;
        }
        return a.channel < b.channel;
    });
}

void normalize_stations(std::vector<Station>& stations)
{
    std::vector<Station> out;
    out.reserve(stations.size());

    for (const Station& s : stations) {
        if (s.program.empty()) {
            continue;
        }
        if (s.band != "fm" && !find_channel(s.channel)) {
            continue;
        }
        if (s.band != "fm" && s.service_id > 0xFFFF) {
            continue;
        }
        bool duplicate = false;
        for (const Station& kept : out) {
            if (same_station(kept, s)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            out.push_back(s);
        }
    }

    sort_stations(out);
    stations.swap(out);
}

} // namespace si468x_pi
