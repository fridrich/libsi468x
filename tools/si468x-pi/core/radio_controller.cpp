// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "radio_controller.hpp"
#include "si468x.h"

#include <algorithm>
#include <iostream>

namespace si468x_pi {

RadioController::RadioController(const Settings& settings, IControllerEvents* events)
    : m_settings(settings), m_events(events)
{
    // Convert settings percent (0..100) to chip volume (0..63)
    const int start_pct = std::clamp(settings.volume.start, 0, 100);
    m_current_volume = std::clamp((start_pct * 63 + 50) / 100, 0, 63);
    m_saved_volume = m_current_volume;
}

RadioController::~RadioController()
{
    stop();
}

std::string RadioController::formatError(int code)
{
    switch (code) {
    case SI468X_SUCCESS:
        return "success";
    case SI468X_ERROR_SPI:
        return "SPI communication error (check connections/permissions)";
    case SI468X_ERROR_GPIO:
        return "GPIO error (check pin number/permissions)";
    case SI468X_ERROR_FIRMWARE:
        return "Firmware image missing or invalid";
    case SI468X_ERROR_BOOT:
        return "Chip boot failed";
    case SI468X_ERROR_TIMEOUT:
        return "Hardware response timeout";
    case SI468X_ERROR_COMMAND:
        return "Command rejected by chip";
    case SI468X_ERROR_PARAM:
        return "Invalid parameter";
    case SI468X_ERROR_INVALID_REPLY:
        return "Invalid reply from chip";
    case SI468X_ERROR_NOT_FOUND:
        return "Service or item not found";
    default:
        return "Unknown error (" + std::to_string(code) + ")";
    }
}

int RadioController::initHardware()
{
    const int boot_mode = (m_settings.device.mode == "fmhd") ? SI468X_BOOT_FMHD : SI468X_BOOT_DAB;
    const int rc = si468x_init(m_settings.device.spi.c_str(), m_settings.device.reset_gpio, boot_mode);
    if (rc != SI468X_SUCCESS) {
        notifyError(formatError(rc));
        notifyState(ControllerState::Error);
        return rc;
    }

    int audio_mode = SI468X_AUDIO_ANALOG;
    if (m_settings.device.audio == "i2s") {
        audio_mode = SI468X_AUDIO_I2S;
    } else if (m_settings.device.audio == "both") {
        audio_mode = SI468X_AUDIO_SIMUL;
    }
    si468x_set_audio_output(audio_mode);

    if (boot_mode == SI468X_BOOT_FMHD) {
        si468x_set_rds_region(SI468X_REGION_EUROPE);
    }

    applyVolume(m_current_volume);
    notifyState(ControllerState::Idle);
    return SI468X_SUCCESS;
}

bool RadioController::start(bool auto_boot)
{
    if (m_running.load()) {
        return true;
    }
    m_running = true;
    m_worker = std::thread(&RadioController::threadLoop, this, auto_boot);
    return true;
}

void RadioController::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back({ CommandType::Shutdown, {}, 0 });
    }
    m_cv.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RadioController::postPlay(const Station& station)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::Play, station, 0 });
    m_cv.notify_one();
}

void RadioController::postStop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::Stop, {}, 0 });
    m_cv.notify_one();
}

void RadioController::postVolumeUp()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::VolumeUp, {}, 0 });
    m_cv.notify_one();
}

void RadioController::postVolumeDown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::VolumeDown, {}, 0 });
    m_cv.notify_one();
}

void RadioController::postSetVolume(int chip_volume)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::SetVolume, {}, chip_volume });
    m_cv.notify_one();
}

void RadioController::postMute()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::Mute, {}, 0 });
    m_cv.notify_one();
}

void RadioController::postAudioOutput(int output_mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::AudioOutput, {}, output_mode });
    m_cv.notify_one();
}

void RadioController::postShutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ CommandType::Shutdown, {}, 0 });
    m_cv.notify_one();
}

void RadioController::waitIdle()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle_cv.wait(lock, [this]() {
        return m_queue.empty() && !m_busy;
    });
}

ControllerState RadioController::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

int RadioController::volume() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_volume;
}

int RadioController::volumePercent() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_current_volume * 100 + 31) / 63;
}

bool RadioController::isMuted() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_muted;
}

uint32_t RadioController::currentFrequency() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_frequency;
}

const Station& RadioController::currentStation() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_station;
}

void RadioController::setEvents(IControllerEvents* events)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events = events;
}

void RadioController::threadLoop(bool auto_boot)
{
    if (auto_boot) {
        initHardware();
    } else {
        notifyState(ControllerState::Idle);
    }

    while (m_running.load()) {
        Command cmd;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return !m_queue.empty() || !m_running.load();
            });

            if (!m_running.load() && m_queue.empty()) {
                break;
            }

            cmd = m_queue.front();
            m_queue.pop_front();

            // Coalesce consecutive volume/mute commands
            while (!m_queue.empty() &&
                   (m_queue.front().type == CommandType::VolumeUp ||
                    m_queue.front().type == CommandType::VolumeDown ||
                    m_queue.front().type == CommandType::SetVolume)) {
                if (cmd.type == CommandType::SetVolume) {
                    cmd = m_queue.front();
                    m_queue.pop_front();
                } else {
                    break;
                }
            }

            m_busy = true;
        }

        executeCommand(cmd);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_busy = false;
        }
        m_idle_cv.notify_all();

        if (cmd.type == CommandType::Shutdown) {
            break;
        }
    }
}

void RadioController::executeCommand(const Command& cmd)
{
    const int step = std::max(1, (m_settings.volume.step * 63 + 50) / 100);

    switch (cmd.type) {
    case CommandType::Play:
        executePlay(cmd.station);
        break;
    case CommandType::Stop:
        executeStop();
        break;
    case CommandType::VolumeUp:
        if (m_muted) {
            m_muted = false;
        }
        applyVolume(std::min(63, m_current_volume + step));
        break;
    case CommandType::VolumeDown:
        if (m_muted) {
            m_muted = false;
        }
        applyVolume(std::max(0, m_current_volume - step));
        break;
    case CommandType::SetVolume:
        if (m_muted) {
            m_muted = false;
        }
        applyVolume(std::clamp(cmd.int_arg, 0, 63));
        break;
    case CommandType::Mute:
        if (m_muted) {
            m_muted = false;
            applyVolume(m_saved_volume > 0 ? m_saved_volume : 50);
        } else {
            m_saved_volume = m_current_volume;
            m_muted = true;
            applyVolume(0);
        }
        break;
    case CommandType::AudioOutput:
        si468x_set_audio_output(cmd.int_arg);
        break;
    case CommandType::Shutdown:
        executeShutdown();
        break;
    }
}

void RadioController::executePlay(const Station& station)
{
    const Channel* chan = find_channel(station.channel);
    if (!chan) {
        notifyError("Unknown channel: " + station.channel);
        notifyState(ControllerState::Error);
        return;
    }

    if (si468x_get_frequency() != chan->frequency_hz) {
        notifyState(ControllerState::Tuning);
        const int rc = si468x_set_frequency(chan->frequency_hz);
        if (rc != SI468X_SUCCESS) {
            notifyError("Tune failed: " + formatError(rc));
            notifyState(ControllerState::Error);
            return;
        }
        m_current_frequency = chan->frequency_hz;
    }

    uint32_t comp_id = station.component_id;
    si468x_service_t services[64];
    const int count = si468x_get_service_list(services, 64);
    if (count > 0 && comp_id == 0) {
        for (int i = 0; i < count; ++i) {
            if (services[i].service_id == station.service_id) {
                comp_id = services[i].component_id;
                break;
            }
        }
    }

    if (comp_id == 0) {
        si468x_service_info_t sinfo;
        si468x_get_service_info(station.service_id, &sinfo);
    }

    int rc = si468x_play_service(station.service_id, comp_id);
    if (rc != SI468X_SUCCESS && comp_id != 0) {
        rc = si468x_play_service(station.service_id, 0);
    }
    if (rc != SI468X_SUCCESS) {
        notifyError("Play service failed: " + formatError(rc));
        notifyState(ControllerState::Error);
        return;
    }

    if (m_muted) {
        si468x_set_volume(0);
    } else {
        si468x_set_volume(static_cast<uint8_t>(m_current_volume));
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_station = station;
    }
    notifyState(ControllerState::Playing);
}

void RadioController::executeStop()
{
    if (m_state == ControllerState::Playing) {
        si468x_stop_service();
    }
    notifyState(ControllerState::Stopped);
}

void RadioController::executeShutdown()
{
    if (m_state == ControllerState::Playing) {
        si468x_stop_service();
    }
    si468x_shutdown();
    notifyState(ControllerState::Stopped);
}

void RadioController::applyVolume(int chip_volume)
{
    m_current_volume = std::clamp(chip_volume, 0, 63);
    si468x_set_volume(static_cast<uint8_t>(m_current_volume));
    const int pct = (m_current_volume * 100 + 31) / 63;
    notifyVolume(pct, m_muted);
}

void RadioController::notifyState(ControllerState s)
{
    IControllerEvents* ev = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = s;
        ev = m_events;
    }
    if (ev) {
        ev->onState(s);
    }
}

void RadioController::notifyVolume(int percent, bool muted)
{
    IControllerEvents* ev = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ev = m_events;
    }
    if (ev) {
        ev->onVolume(percent, muted);
    }
}

void RadioController::notifyError(const std::string& msg)
{
    IControllerEvents* ev = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ev = m_events;
    }
    if (ev) {
        ev->onError(msg);
    }
}

} // namespace si468x_pi
