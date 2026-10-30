// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/station.hpp"
#include "si468x.h"

#include <string>
#include <vector>

namespace si468x_pi {

enum class ControllerState {
    Idle,
    Tuning,
    Playing,
    Stopped,
    Scanning,
    Error,
};

inline const char* toString(ControllerState state)
{
    switch (state) {
    case ControllerState::Idle:
        return "Idle";
    case ControllerState::Tuning:
        return "Tuning";
    case ControllerState::Playing:
        return "Playing";
    case ControllerState::Stopped:
        return "Stopped";
    case ControllerState::Scanning:
        return "Scanning";
    case ControllerState::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

class IControllerEvents {
public:
    virtual ~IControllerEvents() = default;

    virtual void onState(ControllerState /*state*/) {}
    virtual void onScanProgress(const std::string& /*channel*/, int /*found*/, int /*current_idx*/, int /*total*/) {}
    virtual void onStation(const Station& /*station*/) {}
    virtual void onStationListChanged(const std::vector<Station>& /*stations*/) {}
    virtual void onSignal(const si468x_signal_status_t& /*signal*/) {}
    virtual void onEnsemble(const si468x_ensemble_info_t& /*ensemble*/) {}
    virtual void onDls(const std::string& /*dls*/) {}
    virtual void onTime(const si468x_time_t& /*time*/) {}
    virtual void onAudio(const si468x_audio_info_t& /*audio*/) {}
    virtual void onError(const std::string& /*message*/) {}
    virtual void onVolume(int /*percent*/, bool /*muted*/) {}
    virtual void onMot(const std::string& /*file_path*/, const si468x_mot_slideshow_t& /*slide*/) {}
    virtual void onSignalLost(bool /*lost*/) {}
};

} // namespace si468x_pi
