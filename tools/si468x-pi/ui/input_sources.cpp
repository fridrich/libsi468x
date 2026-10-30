// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_sources.hpp"
#include "plugin_loader.hpp"
#include "si468x-pi/input_plugin.h"

#include <dlfcn.h>
#include <iostream>
#include <memory>

static_assert(int(si468x_pi::InputAction::UP) == SI468X_PI_UP &&
              int(si468x_pi::InputAction::DOWN) == SI468X_PI_DOWN &&
              int(si468x_pi::InputAction::LEFT) == SI468X_PI_LEFT &&
              int(si468x_pi::InputAction::RIGHT) == SI468X_PI_RIGHT &&
              int(si468x_pi::InputAction::ENTER) == SI468X_PI_ENTER &&
              int(si468x_pi::InputAction::MENU) == SI468X_PI_MENU &&
              int(si468x_pi::InputAction::QUIT) == SI468X_PI_QUIT &&
              int(si468x_pi::InputAction::VOLUME_UP) == SI468X_PI_VOLUME_UP &&
              int(si468x_pi::InputAction::VOLUME_DOWN) == SI468X_PI_VOLUME_DOWN &&
              int(si468x_pi::InputAction::MUTE) == SI468X_PI_MUTE &&
              int(si468x_pi::InputAction::HOME) == SI468X_PI_HOME &&
              int(si468x_pi::InputAction::NONE) == SI468X_PI_ACTION_COUNT,
              "input_plugin.h out of sync with InputAction");

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

std::vector<std::thread> startInputSources(const nlohmann::json& inputs, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive)
{
    nlohmann::json list = inputs;
    if (list.is_null()) {
        list = {{ { "type", "stdin" } }};
    }

    std::vector<std::thread> threads;
    if (!list.is_array()) {
        std::cerr << "si468x-pi: \"input\" must be an array" << std::endl;
        return threads;
    }

    for (const auto& entry : list) {
        const std::string plugin = entry.is_object() ? entry.value("plugin", "") : "";
        const std::string type = entry.is_object() ? entry.value("type", "") : "";
        if (!plugin.empty()) {
            RunFn run = loadInputPlugin(plugin);
            if (!run) {
                continue;
            }
            threads.emplace_back([run, plugin, config = entry.dump(), &queue, stop_requested]() {
                PluginCtx ctx{ queue, stop_requested };
                const si468x_pi_input_host host{ &ctx, pluginPush, pluginStop };
                if (int rc = run(&host, config.c_str())) {
                    std::cerr << "Input plugin " << plugin << " returned " << rc << std::endl;
                }
            });
        } else if (type == "stdin") {
            if (interactive) {
                threads.emplace_back(runStdinReader, std::ref(queue), stop_requested);
            }
        } else {
            std::cerr << "si468x-pi: unknown input " << entry.dump() << std::endl;
        }
    }
    return threads;
}

std::vector<std::thread> startInputSources(const Settings& settings, InputQueue& queue,
                                           const StopPredicate& stop_requested, bool interactive)
{
    if (!settings.input_configured) {
        std::vector<std::thread> threads;
        if (interactive) {
            threads.emplace_back(runStdinReader, std::ref(queue), stop_requested);
        }
        return threads;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& in : settings.input) {
        if (!in.config_json.empty()) {
            try {
                arr.push_back(nlohmann::json::parse(in.config_json));
            } catch (...) {
                if (!in.plugin.empty()) {
                    arr.push_back({ { "plugin", in.plugin } });
                } else if (!in.type.empty()) {
                    arr.push_back({ { "type", in.type } });
                }
            }
        } else if (!in.plugin.empty()) {
            arr.push_back({ { "plugin", in.plugin } });
        } else if (!in.type.empty()) {
            arr.push_back({ { "type", in.type } });
        }
    }
    return startInputSources(arr, queue, stop_requested, interactive);
}

bool hasInputSources(const nlohmann::json& inputs, bool interactive)
{
    if (inputs.is_null()) {
        return interactive;
    }
    if (!inputs.is_array()) {
        return false;
    }
    for (const auto& entry : inputs) {
        if (!entry.is_object()) {
            continue;
        }
        if (!entry.value("plugin", "").empty()) {
            return true;
        }
        if (entry.value("type", "") == "stdin" && interactive) {
            return true;
        }
    }
    return false;
}

bool hasInputSources(const Settings& settings, bool interactive)
{
    if (!settings.input_configured) {
        return interactive;
    }
    for (const auto& entry : settings.input) {
        if (!entry.plugin.empty()) {
            return true;
        }
        if (entry.type == "stdin" && interactive) {
            return true;
        }
    }
    return false;
}

} // namespace si468x_pi
