// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace si468x_pi {

inline uint64_t now_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// How long a lost signal stays hidden before the radio screen says so.
static const uint64_t SIGNAL_LOST_MS = 5000;

// UTF-8 helpers count code points, not bytes: DAB labels arrive as UTF-8 and
// must fit the display's columns.
inline size_t utf8Length(const std::string& s)
{
    size_t n = 0;
    for (unsigned char c : s) {
        n += (c & 0xC0) != 0x80;
    }
    return n;
}

inline std::string utf8Substr(const std::string& s, size_t start, size_t count)
{
    size_t i = 0;
    size_t cp = 0;
    auto advance = [&]() {
        do {
            ++i;
        } while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    };
    while (i < s.size() && cp < start) {
        advance();
        ++cp;
    }
    const size_t from = i;
    for (cp = 0; i < s.size() && cp < count; ++cp) {
        advance();
    }
    return s.substr(from, i - from);
}

// Truncated or space-padded to exactly width code points.
inline std::string utf8Fit(const std::string& s, size_t width)
{
    std::string r = utf8Substr(s, 0, width);
    r.append(width - utf8Length(r), ' ');
    return r;
}

} // namespace si468x_pi
