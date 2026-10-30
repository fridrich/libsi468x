// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace si468x_pi {

// One screenful of text. Screens fill it; UIManager fits every line to exactly
// cols code points and the display shows it.
struct Frame {
    Frame(int frame_cols, int frame_rows, uint64_t frame_now)
        : cols(frame_cols), rows(frame_rows), now(frame_now), lines(frame_rows)
    {
    }

    void set(int row, const std::string& text)
    {
        if (row >= 0 && row < rows) {
            lines[row] = text;
        }
    }

    // Ask the display to draw this row inverted, for the focused item.
    // Returns false when the display cannot do that: the screen marks the
    // item in the text instead.
    bool mark(int row)
    {
        if (!highlights || row < 0 || row >= rows) {
            return false;
        }
        highlight = row;
        return true;
    }

    int cols;
    int rows;
    uint64_t now;                   // now_ms() of this tick; screens animate from it
    std::vector<std::string> lines; // UTF-8
    bool highlights = false;        // UIManager: the display draws mark() itself
    int highlight = -1;             // the row a screen marked, -1 none
};

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual int cols() const = 0;
    virtual int rows() const = 0;
    // Called when the frame text or highlight changed.
    virtual void present(const Frame& frame) = 0;
    // Whether present() draws Frame::highlight inverted instead of the screens
    // wrapping the focused item in > and <.
    virtual bool canHighlight() const { return false; }
};

// The same frame on several displays (LCD + terminal), at the smallest size.
class MultiDisplay : public IDisplay {
public:
    explicit MultiDisplay(std::vector<std::unique_ptr<IDisplay>> displays)
        : m_displays(std::move(displays))
    {
    }

    int cols() const override
    {
        int n = INT_MAX;
        for (const auto& d : m_displays) {
            n = std::min(n, d->cols());
        }
        return n;
    }

    int rows() const override
    {
        int n = INT_MAX;
        for (const auto& d : m_displays) {
            n = std::min(n, d->rows());
        }
        return n;
    }

    void present(const Frame& frame) override
    {
        for (auto& d : m_displays) {
            d->present(frame);
        }
    }

    // Only when every display can: a mixed pair keeps the text markers so that
    // both show the same thing.
    bool canHighlight() const override
    {
        bool all = true;
        for (const auto& d : m_displays) {
            all = all && d->canHighlight();
        }
        return all;
    }

private:
    std::vector<std::unique_ptr<IDisplay>> m_displays;
};

} // namespace si468x_pi
