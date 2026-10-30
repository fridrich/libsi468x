// SPDX-FileCopyrightText: 2026 si468x-pi authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "station.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <tuple>

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

const std::vector<std::string>& default_scan_channels()
{
    static const std::vector<std::string> list = [] {
        std::vector<std::string> l;
        for (const Channel& c : band3_channels()) {
            if (std::string(c.label).back() != 'N') {
                l.push_back(c.label);
            }
        }
        return l;
    }();
    return list;
}

uint32_t fm_frequency_hz(const Station& s)
{
    if (s.service_id != 0 || s.channel.empty()) {
        return s.service_id;
    }
    try {
        return static_cast<uint32_t>(std::stod(s.channel) * 1000000.0 + 0.5);
    } catch (...) {
        return 0;
    }
}

namespace {

// U+00C0..U+017F (Latin-1 Supplement, Latin Extended-A) as lowercase ASCII;
// "" for the two non-letters, which stay as they are
const char* const LATIN_FOLD[0x180 - 0xC0] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // U+00C0
    "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss", // U+00D0
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // U+00E0
    "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y", // U+00F0
    "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d", // U+0100
    "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g", // U+0110
    "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i", // U+0120
    "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l", // U+0130
    "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o", // U+0140
    "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s", // U+0150
    "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u", // U+0160
    "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s", // U+0170
};

// Sort key in the order of a dictionary: case and accents ignored, so that
// "Rete Uno" < "RTS Première" < "SRF" and "Écho" sorts with the E
std::string fold(const std::string& s)
{
    std::string key;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        if (c < 0x80) {
            key += static_cast<char>(std::tolower(c));
            ++i;
            continue;
        }
        const size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        const unsigned cp = len == 2 && i + 1 < s.size() ? (c & 0x1Fu) << 6 | (s[i + 1] & 0x3Fu) : 0;
        if (cp >= 0xC0 && cp < 0x180 && *LATIN_FOLD[cp - 0xC0]) {
            key += LATIN_FOLD[cp - 0xC0];
        } else {
            key.append(s, i, len);
        }
        i += len;
    }
    return key;
}

} // namespace

void sort_stations(std::vector<Station>& stations)
{
    std::stable_sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) {
        if (a.band != b.band) {
            return a.band < b.band;
        }
        if (a.band == "fm") {
            return std::make_tuple(fm_frequency_hz(a), a.component_id, std::cref(a.program), std::cref(a.channel)) <
                   std::make_tuple(fm_frequency_hz(b), b.component_id, std::cref(b.program), std::cref(b.channel));
        }
        return std::make_tuple(fold(a.program), std::cref(a.program), std::cref(a.channel)) <
               std::make_tuple(fold(b.program), std::cref(b.program), std::cref(b.channel));
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
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const Station& kept) {
            return kept.channel == s.channel && kept.service_id == s.service_id;
        });
        if (!duplicate) {
            out.push_back(s);
        }
    }

    sort_stations(out);
    stations.swap(out);
}

} // namespace si468x_pi
