// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "display_interface.hpp"
#include "settings.hpp"

#include <cstdio>
#include <utility>

namespace si468x_pi {

// Draws each changed frame in a box. On a terminal it clears the screen first,
// so diagnostics should go to stderr (2>log).
class TtyDisplay : public IDisplay {
public:
    TtyDisplay(int cols, int rows, std::FILE* out = stdout);

    int cols() const override { return m_cols; }
    int rows() const override { return m_rows; }
    bool canHighlight() const override { return m_ansi; }
    void present(const Frame& frame) override;

private:
    int m_cols;
    int m_rows;
    std::FILE* m_out;
    bool m_ansi;
};

// cols x rows of the first "tty" display entry, else 80 x 25.
std::pair<int, int> ttySize(const Settings& settings);

} // namespace si468x_pi
