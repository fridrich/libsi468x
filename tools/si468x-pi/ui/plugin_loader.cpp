// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_loader.hpp"
#include "si468x-pi/plugin.h"

#include <climits>
#include <dlfcn.h>
#include <iostream>
#include <unistd.h>
#include <vector>

#ifndef SI468X_PI_PLUGIN_DIR
#define SI468X_PI_PLUGIN_DIR "/usr/local/lib/si468x-pi"
#endif

namespace si468x_pi {

namespace {

std::string exeDir()
{
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return ".";
    }
    std::string dir(buf, n);
    const size_t slash = dir.rfind('/');
    if (slash != std::string::npos) {
        dir = dir.substr(0, slash);
    } else {
        dir = ".";
    }
    if (dir.size() >= 6 && dir.substr(dir.size() - 6) == "/.libs") {
        dir = dir.substr(0, dir.size() - 6);
    }
    return dir;
}

} // namespace

void* loadPlugin(const std::string& kind, const std::string& name, int abi)
{
    if (name.empty()) {
        std::cerr << "si468x-pi: " << kind << " plugin without name" << std::endl;
        return nullptr;
    }

    std::vector<std::string> paths;
    if (name.find('/') != std::string::npos) {
        paths.push_back(name);
    } else {
        const std::string file = "si468x-pi-" + name + ".so";
        const std::string exe = exeDir();
        paths.push_back(exe + "/si468x-pi-plugins/" + file);
        paths.push_back(std::string(SI468X_PI_PLUGIN_DIR) + "/" + file);
    }

    for (const auto& path : paths) {
        if (access(path.c_str(), F_OK) != 0) {
            continue;
        }
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::cerr << "Plugin " << path << ": " << dlerror() << std::endl;
            return nullptr;
        }

        const std::string abi_sym = "si468x_pi_" + kind + "_abi";
        auto version = static_cast<const int*>(dlsym(handle, abi_sym.c_str()));
        if (!version || *version != abi) {
            std::cerr << "Plugin " << path << ": ABI "
                      << (version ? std::to_string(*version) : "missing")
                      << ", need " << abi << std::endl;
            dlclose(handle);
            return nullptr;
        }

        auto pluginName = static_cast<const char*>(dlsym(handle, "si468x_pi_plugin_name"));
        auto description = static_cast<const char*>(dlsym(handle, "si468x_pi_plugin_description"));
        if (!pluginName || !description) {
            std::cerr << "Plugin " << path << ": si468x_pi_plugin_name/description missing" << std::endl;
            dlclose(handle);
            return nullptr;
        }

        std::cerr << kind << " plugin " << pluginName << " (" << description << "): " << path << std::endl;
        return handle;
    }

    std::cerr << "Plugin " << name << " not found:";
    for (const auto& path : paths) {
        std::cerr << " " << path;
    }
    std::cerr << std::endl;
    return nullptr;
}

} // namespace si468x_pi
