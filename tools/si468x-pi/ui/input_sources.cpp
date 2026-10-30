// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_sources.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/plugin.h"

#include <dlfcn.h>
#include <iostream>

namespace si468x_pi {

namespace {

using RunFn = int (*)(const si468x_pi_input_host*, const char*);

struct PluginCtx {
    InputQueue& queue;
    StopPredicate stop_requested;
};

void pluginPush(void* ctx, si468x_pi_action action)
{
    if (action >= SI468X_PI_UP && action < SI468X_PI_ACTION_COUNT) {
        static_cast<PluginCtx*>(ctx)->queue.push(static_cast<InputAction>(action));
    }
}

int pluginStop(void* ctx)
{
    return static_cast<PluginCtx*>(ctx)->stop_requested() ? 1 : 0;
}

RunFn loadInputPlugin(const std::string& name)
{
    void* handle = loadPlugin("input", name, SI468X_PI_INPUT_ABI);
    if (!handle) {
        return nullptr;
    }
    auto run = reinterpret_cast<RunFn>(dlsym(handle, "si468x_pi_input_run"));
    if (!run) {
        std::cerr << "Input plugin " << name << ": si468x_pi_input_run missing" << std::endl;
    }
    return run;
}

} // namespace

std::vector<std::thread> startInputSources(const Settings& settings, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive)
{
    std::vector<std::thread> threads;
    if (!settings.input_configured) {
        if (interactive) {
            threads.emplace_back(runStdinReader, std::ref(queue), stop_requested);
        }
        return threads;
    }

    for (const InputConfig& in : settings.input) {
        if (in.plugin.empty()) { // "stdin", the only built-in type parse_inputs() accepts
            if (interactive) {
                threads.emplace_back(runStdinReader, std::ref(queue), stop_requested);
            }
            continue;
        }
        RunFn run = loadInputPlugin(in.plugin);
        if (!run) {
            continue;
        }
        threads.emplace_back([run, plugin = in.plugin, config = in.config_json, &queue, stop_requested]() {
            PluginCtx ctx{ queue, stop_requested };
            const si468x_pi_input_host host{ &ctx, pluginPush, pluginStop };
            if (int rc = run(&host, config.c_str())) {
                std::cerr << "Input plugin " << plugin << " returned " << rc << std::endl;
            }
        });
    }
    return threads;
}

} // namespace si468x_pi
