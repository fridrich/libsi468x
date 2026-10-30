// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "display_interface.hpp"

#include <memory>
#include <string>
#include <vector>

namespace si468x_pi {

class PluginDisplay : public IDisplay {
public:
    static std::unique_ptr<PluginDisplay> open(const std::string& name, const std::string& config);
    ~PluginDisplay() override;

    int cols() const override { return m_cols; }
    int rows() const override { return m_rows; }
    bool canHighlight() const override { return m_highlights; }
    void present(const Frame& frame) override;

private:
    using PresentFn = void (*)(void*, const char* const*, int, int, int);
    using CloseFn = void (*)(void*);

    PluginDisplay(void* handle, PresentFn present_fn, CloseFn close_fn, int cols, int rows, bool highlights);

    void* m_handle;
    PresentFn m_present;
    CloseFn m_close;
    int m_cols;
    int m_rows;
    bool m_highlights;
};

} // namespace si468x_pi
