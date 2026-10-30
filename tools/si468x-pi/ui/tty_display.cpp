// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tty_display.hpp"

#include <unistd.h>

namespace si468x_pi {

TtyDisplay::TtyDisplay(int cols, int rows, std::FILE* out)
    : m_cols(cols), m_rows(rows), m_out(out), m_ansi(isatty(fileno(out)))
{
}

std::pair<int, int> ttySize(const Settings& settings)
{
    for (const auto& d : settings.display) {
        if (d.type == "tty") {
            return { d.cols > 0 ? d.cols : 80, d.rows > 0 ? d.rows : 25 };
        }
    }
    return { 80, 25 };
}

void TtyDisplay::present(const Frame& frame)
{
    // frame size, not ours: mirrored next to a smaller display
    const std::string border = "+" + std::string(frame.cols, '-') + "+\n";
    std::string out = m_ansi ? "\033[H\033[2J" : "";
    out += border;
    for (size_t row = 0; row < frame.lines.size(); ++row) {
        // reverse video for the focused row; the line is exactly cols wide
        const bool inverse = m_ansi && static_cast<int>(row) == frame.highlight;
        out += "|" + std::string(inverse ? "\033[7m" : "") + frame.lines[row]
               + (inverse ? "\033[27m" : "") + "|\n";
    }
    out += border;
    std::fputs(out.c_str(), m_out);
    std::fflush(m_out);
}

} // namespace si468x_pi
