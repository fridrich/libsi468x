// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_display.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/plugin.h"

#include <dlfcn.h>
#include <iostream>

namespace si468x_pi {

PluginDisplay::PluginDisplay(void* handle, PresentFn present_fn, CloseFn close_fn, int cols, int rows, bool highlights)
    : m_handle(handle), m_present(present_fn), m_close(close_fn), m_cols(cols), m_rows(rows),
      m_highlights(highlights)
{
}

PluginDisplay::~PluginDisplay()
{
    if (m_handle && m_close) {
        m_close(m_handle);
    }
}

std::unique_ptr<PluginDisplay> PluginDisplay::open(const std::string& name, const std::string& config)
{
    void* lib = loadPlugin("display", name, SI468X_PI_DISPLAY_ABI);
    if (!lib) {
        return nullptr;
    }

    using OpenFn = void* (*)(const char*, int*, int*, unsigned*);
    auto openFn = reinterpret_cast<OpenFn>(dlsym(lib, "si468x_pi_display_open"));
    auto present = reinterpret_cast<PresentFn>(dlsym(lib, "si468x_pi_display_present"));
    auto close = reinterpret_cast<CloseFn>(dlsym(lib, "si468x_pi_display_close"));
    if (!openFn || !present || !close) {
        std::cerr << "Display plugin " << name << ": si468x_pi_display_open/present/close missing" << std::endl;
        return nullptr;
    }

    int cols = 0;
    int rows = 0;
    unsigned features = 0;
    void* handle = openFn(config.c_str(), &cols, &rows, &features);
    if (!handle) {
        return nullptr;
    }
    if (cols <= 0 || rows <= 0) {
        std::cerr << "Display plugin " << name << ": invalid size " << cols << " x " << rows << std::endl;
        close(handle);
        return nullptr;
    }

    const bool can_hl = (features & SI468X_PI_DISPLAY_HIGHLIGHT) != 0;
    return std::unique_ptr<PluginDisplay>(new PluginDisplay(handle, present, close, cols, rows, can_hl));
}

void PluginDisplay::present(const Frame& frame)
{
    std::vector<const char*> lines;
    lines.reserve(frame.lines.size());
    for (const auto& line : frame.lines) {
        lines.push_back(line.c_str());
    }
    m_present(m_handle, lines.data(), frame.cols, static_cast<int>(lines.size()), frame.highlight);
}

} // namespace si468x_pi
