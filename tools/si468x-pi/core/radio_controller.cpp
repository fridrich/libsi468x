// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "radio_controller.hpp"
#include "si468x.h"
#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace si468x_pi {

RadioController::RadioController(const Settings& settings, IControllerEvents* events,
                                 const std::string& cache_path)
    : m_settings(settings), m_events(events),
      m_cache_path(cache_path.empty() ? default_cache_path() : cache_path)
{
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
    // the chip takes the output route only at boot
    int audio_mode = SI468X_AUDIO_ANALOG;
    if (m_settings.device.audio == "i2s") {
        audio_mode = SI468X_AUDIO_I2S;
    } else if (m_settings.device.audio == "both") {
        audio_mode = SI468X_AUDIO_SIMUL;
    }
    si468x_set_audio_output(audio_mode);

    const int boot_mode = (m_settings.device.mode == "fmhd") ? SI468X_BOOT_FMHD : SI468X_BOOT_DAB;
    const int rc = si468x_init(m_settings.device.spi.c_str(), m_settings.device.reset_gpio, boot_mode);
    if (rc != SI468X_SUCCESS) {
        notifyError(formatError(rc));
        notifyState(ControllerState::Error);
        return rc;
    }

    if (boot_mode == SI468X_BOOT_FMHD) {
        si468x_set_rds_region(m_settings.device.region == "us" ? SI468X_REGION_US : SI468X_REGION_EUROPE);
    }

    applyVolume(m_current_volume);
    notifyState(ControllerState::Idle);
    return SI468X_SUCCESS;
}

bool RadioController::loadCache(const std::string& path)
{
    Cache c;
    std::vector<std::string> warnings;
    if (!si468x_pi::load_cache(path, c, &warnings) || c.stations.empty()) {
        return false;
    }
    std::vector<Station> st;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stations = c.stations;
        normalize_stations(m_stations);
        if (c.volume >= 0 && c.volume <= 63) {
            m_current_volume = c.volume;
            m_saved_volume = m_current_volume;
        }
        st = m_stations;
    }
    notifyStationListChanged(st);
    return true;
}

bool RadioController::saveCache(const std::string& path) const
{
    Cache c;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        c.stations = m_stations;
        c.volume = m_current_volume;
        if (!m_current_station.channel.empty() || m_current_station.service_id != 0) {
            c.last_played.channel = m_current_station.channel;
            c.last_played.service_id = m_current_station.service_id;
            c.last_played.band = m_current_station.band.empty() ? m_settings.device.mode : m_current_station.band;
        }
    }
    return si468x_pi::save_cache(path, c);
}

const std::vector<Station>& RadioController::stations() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stations;
}

void RadioController::setStations(const std::vector<Station>& stations)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stations = stations;
        normalize_stations(m_stations);
    }
    notifyStationListChanged(m_stations);
}

bool RadioController::start()
{
    if (m_running.load()) {
        return true;
    }
    m_running = true;
    m_worker = std::thread(&RadioController::threadLoop, this);
    return true;
}

void RadioController::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }
    m_abort_scan = true;
    post({ CommandType::Shutdown });
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RadioController::post(const Command& cmd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(cmd);
    m_cv.notify_one();
}

void RadioController::resetPolls()
{
    m_last_dls_poll = 0;
    m_last_sig_poll = 0;
    m_last_info_poll = 0;
    m_last_ens_poll = 0;
    m_last_ev_poll = 0;
    m_sync_lost_start = 0;
    m_consecutive_spi_errors = 0;
    m_signal_lost = false;
}

void RadioController::setSimulatedPlaying(const Station& st)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = ControllerState::Playing;
    m_current_station = st;
    resetPolls();
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

bool RadioController::isSignalLost() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_signal_lost;
}

void RadioController::threadLoop()
{
    notifyState(ControllerState::Idle);

    while (m_running.load()) {
        Command cmd;
        bool has_cmd = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const int wait_ms = (m_state == ControllerState::Playing) ? 50 : 500;
            m_cv.wait_for(lock, std::chrono::milliseconds(wait_ms), [this]() {
                return !m_queue.empty() || !m_running.load();
            });

            if (!m_running.load() && m_queue.empty()) {
                break;
            }

            if (!m_queue.empty()) {
                cmd = m_queue.front();
                m_queue.pop_front();
                has_cmd = true;
                m_busy = true;
            }
        }

        if (has_cmd) {
            executeCommand(cmd);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_busy = false;
            }
            m_idle_cv.notify_all();

            if (cmd.type == CommandType::Shutdown) {
                break;
            }
        } else if (state() == ControllerState::Playing) {
            pollTelemetry();
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
    case CommandType::Scan:
        executeScan(cmd.bool_arg);
        break;
    case CommandType::Seek:
        executeSeek(cmd.bool_arg);
        break;
    case CommandType::VolumeUp:
        m_muted = false;
        applyVolume(std::min(63, m_current_volume + step));
        break;
    case CommandType::VolumeDown:
        m_muted = false;
        applyVolume(std::max(0, m_current_volume - step));
        break;
    case CommandType::SetVolume:
        m_muted = false;
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
    case CommandType::Shutdown:
        executeShutdown();
        break;
    }
}

void RadioController::executePlay(const Station& station)
{
    if (m_settings.device.mode == "fmhd" || station.band == "fm") {
        const uint32_t freq_hz = fm_frequency_hz(station);
        if (freq_hz == 0) {
            notifyError("Invalid FM frequency: " + station.channel);
            notifyState(ControllerState::Error);
            return;
        }

        if (si468x_get_frequency() != freq_hz) {
            notifyState(ControllerState::Tuning);
            const int rc = si468x_tune_fm(freq_hz / 1000);
            if (rc != SI468X_SUCCESS) {
                notifyError("FM tune failed: " + formatError(rc));
                notifyState(ControllerState::Error);
                return;
            }
            m_current_frequency = freq_hz;
        }

        Station fm = station;
        fm.band = "fm";
        startedPlaying(fm);
        return;
    }

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
    if (comp_id == 0) {
        si468x_service_t services[64];
        const int count = si468x_get_service_list(services, 64);
        for (int i = 0; i < count; ++i) {
            if (services[i].service_id == station.service_id) {
                comp_id = services[i].component_id;
                break;
            }
        }
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
    startedPlaying(station);
}

void RadioController::startedPlaying(const Station& station)
{
    si468x_set_volume(static_cast<uint8_t>(m_muted ? 0 : m_current_volume));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_station = station;
        resetPolls();
    }
    notifyStation(station);
    notifyState(ControllerState::Playing);
    if (!m_cache_path.empty()) {
        saveCache(m_cache_path);
    }
}

void RadioController::executeStop()
{
    if (m_state == ControllerState::Playing) {
        si468x_stop_service();
    }
    notifyState(ControllerState::Stopped);
}

void RadioController::executeSeek(bool seek_up)
{
    notifyState(ControllerState::Tuning);
    const int rc = si468x_fm_seek_start(seek_up ? 1 : 0, 1);
    if (rc != SI468X_SUCCESS) {
        notifyError("FM seek failed: " + formatError(rc));
        notifyState(ControllerState::Error);
        return;
    }
    si468x_fm_status_t fm = {};
    if (si468x_get_fm_status(&fm) == SI468X_SUCCESS) {
        m_current_frequency = fm.frequency_hz;
        char ch[32] = {};
        std::snprintf(ch, sizeof(ch), "%.1f", fm.frequency_hz / 1000000.0);
        char ps[9] = {};
        si468x_get_rds_station_name(ps, sizeof(ps));
        std::string prog = ps[0] ? ps : std::string(ch) + " MHz";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_current_station = Station{ ch, prog, prog, fm.frequency_hz, 0, false, "fm" };
        }
        notifyStation(m_current_station);
    }
    notifyState(ControllerState::Playing);
    if (!m_cache_path.empty()) {
        saveCache(m_cache_path);
    }
}

void RadioController::executeScan(bool auto_play)
{
    notifyState(ControllerState::Scanning);
    m_abort_scan = false;

    const bool fm = m_settings.device.mode == "fmhd";
    std::vector<Station> found;
    bool aborted = false;

    if (fm) {
        for (uint32_t freq_khz = 87500; freq_khz <= 108000; freq_khz += 100) {
            if (m_abort_scan.exchange(false) || !m_running.load()) {
                aborted = true;
                break;
            }
            char ch_buf[16];
            std::snprintf(ch_buf, sizeof(ch_buf), "%.1f", freq_khz / 1000.0);
            notifyScanProgress(ch_buf, static_cast<int>(found.size()), (freq_khz - 87500) / 100, 206);
            if (si468x_tune_fm(freq_khz) != SI468X_SUCCESS) {
                continue;
            }
            si468x_fm_status_t status = {};
            if (si468x_get_fm_status(&status) == SI468X_SUCCESS && status.valid &&
                status.rssi >= m_settings.scan.rssi_min) {
                char ps[9] = {};
                si468x_get_rds_station_name(ps, sizeof(ps));
                std::string prog = ps[0] ? ps : std::string(ch_buf) + " MHz";
                found.push_back(Station{ ch_buf, prog, prog, freq_khz * 1000, 0, false, "fm" });
            }
        }
    } else {
        const auto& channels = m_settings.scan.channels.empty() ? default_scan_channels()
                                                                : m_settings.scan.channels;
        const int total = static_cast<int>(channels.size());
        for (int idx = 0; idx < total; ++idx) {
            if (m_abort_scan.exchange(false) || !m_running.load()) {
                aborted = true;
                break;
            }

            const std::string& ch_name = channels[idx];
            notifyScanProgress(ch_name, static_cast<int>(found.size()), idx, total);

            const Channel* ch = find_channel(ch_name);
            if (!ch || si468x_set_frequency(ch->frequency_hz) != SI468X_SUCCESS) {
                continue;
            }

            si468x_signal_status_t sig;
            if (si468x_get_signal_status(&sig) != SI468X_SUCCESS ||
                !sig.sync_status || sig.rssi < m_settings.scan.rssi_min) {
                continue;
            }

            si468x_ensemble_info_t ens;
            si468x_get_ensemble_info(&ens);

            si468x_service_t svcs[64];
            const int count = si468x_get_service_list(svcs, 64);
            for (int i = 0; i < count; ++i) {
                if ((svcs[i].is_data_service && !m_settings.scan.include_data) || svcs[i].service_id > 0xFFFF) {
                    continue;
                }
                si468x_service_info_t sinfo;
                std::string short_label;
                if (si468x_get_service_info(svcs[i].service_id, &sinfo) == SI468X_SUCCESS) {
                    short_label = sinfo.short_label;
                }
                if (short_label.empty()) {
                    short_label = svcs[i].short_label;
                }
                found.push_back(Station{
                    ch_name,
                    rtrim(svcs[i].label),
                    rtrim(short_label),
                    svcs[i].service_id,
                    static_cast<uint16_t>(svcs[i].component_id),
                    svcs[i].is_data_service != 0
                });
            }
            notifyScanProgress(ch_name, static_cast<int>(found.size()), idx + 1, total);
        }
    }

    normalize_stations(found);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stations = found;
    }
    notifyStationListChanged(found);

    // first audio service, else the first one
    const auto audio = std::find_if(found.begin(), found.end(), [](const Station& s) { return !s.data; });
    const Station to_play = audio != found.end() ? *audio : found.empty() ? Station{} : found.front();

    if (!m_cache_path.empty()) {
        Cache c;
        c.complete = !aborted;
        c.stations = found;
        c.volume = m_current_volume;
        const Station& last = to_play.channel.empty() ? m_current_station : to_play;
        if (!last.channel.empty()) {
            c.last_played = { last.channel, last.service_id, fm ? "fm" : "dab" };
        }
        si468x_pi::save_cache(m_cache_path, c);
    }

    if (auto_play && !found.empty()) {
        executePlay(to_play);
    } else {
        notifyState(ControllerState::Idle);
    }
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
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = s;
    }
    if (m_events) {
        m_events->onState(s);
    }
}

void RadioController::pollTelemetry(uint64_t now_ms_override)
{
    const uint64_t now = (now_ms_override != 0) ? now_ms_override : now_ms();

    if (m_settings.device.mode == "fmhd") {
        const uint64_t sig_interval = m_settings.poll.signal_ms > 0 ? static_cast<uint64_t>(m_settings.poll.signal_ms) : 500;
        if (now >= m_last_sig_poll + sig_interval) {
            m_last_sig_poll = now;
            si468x_fm_status_t fm = {};
            if (si468x_get_fm_status(&fm) == SI468X_SUCCESS) {
                si468x_signal_status_t sig = {};
                sig.rssi = fm.rssi;
                sig.snr = fm.snr;
                sig.sync_status = fm.valid;
                notifySignal(sig);

                char ps[9] = {};
                if (si468x_get_rds_station_name(ps, sizeof(ps)) > 0) {
                    const std::string ps_str = rtrim(ps);
                    if (!ps_str.empty()) {
                        m_current_station.program = ps_str;
                        m_current_station.short_program = ps_str;
                    }
                }
                char rt[65] = {};
                if (si468x_get_rds_text(rt, sizeof(rt)) > 0) {
                    const std::string rt_str = rtrim(rt);
                    if (!rt_str.empty()) {
                        notifyDls(rt_str);
                    }
                }
            }
        }
        return;
    }

    // 1. DLS text (poll.dls_ms)
    const uint64_t dls_interval = m_settings.poll.dls_ms > 0 ? static_cast<uint64_t>(m_settings.poll.dls_ms) : 300;
    if (now >= m_last_dls_poll + dls_interval) {
        m_last_dls_poll = now;
        char dls_buf[129] = {};
        const int rc = si468x_get_dls_text(dls_buf, sizeof(dls_buf));
        if (rc == 1 && dls_buf[0] != '\0') {
            notifyDls(std::string(dls_buf));
        }
    }

    // 2. Signal status (poll.signal_ms)
    const uint64_t sig_interval = m_settings.poll.signal_ms > 0 ? static_cast<uint64_t>(m_settings.poll.signal_ms) : 500;
    if (now >= m_last_sig_poll + sig_interval) {
        m_last_sig_poll = now;
        si468x_signal_status_t sig;
        const int rc = si468x_get_signal_status(&sig);
        if (rc == SI468X_SUCCESS) {
            m_consecutive_spi_errors = 0;
            notifySignal(sig);
            if (sig.sync_status) {
                m_sync_lost_start = 0;
                if (m_signal_lost) {
                    m_signal_lost = false;
                    notifySignalLost(false);
                }
            } else {
                if (m_sync_lost_start == 0) {
                    m_sync_lost_start = now;
                } else if (now - m_sync_lost_start >= 2000 && !m_signal_lost) {
                    m_signal_lost = true;
                    notifySignalLost(true);
                }
            }
        } else if (rc == SI468X_ERROR_SPI) {
            ++m_consecutive_spi_errors;
            if (m_consecutive_spi_errors >= 2) {
                notifyError("SPI connection lost during playback");
                executeStop();
                notifyState(ControllerState::Error);
            }
        }
    }

    // 3. Info metrics: UTC time and audio parameters (poll.info_ms)
    const uint64_t info_interval = m_settings.poll.info_ms > 0 ? static_cast<uint64_t>(m_settings.poll.info_ms) : 5000;
    if (now >= m_last_info_poll + info_interval) {
        m_last_info_poll = now;
        si468x_time_t t;
        if (si468x_get_time(&t) == SI468X_SUCCESS) {
            notifyTime(t);
        }
        si468x_audio_info_t a;
        if (si468x_get_audio_info(&a) == SI468X_SUCCESS) {
            notifyAudio(a);
        }
    }

    // 4. Ensemble info (10 s)
    if (now >= m_last_ens_poll + 10000) {
        m_last_ens_poll = now;
        si468x_ensemble_info_t ens;
        if (si468x_get_ensemble_info(&ens) == SI468X_SUCCESS) {
            notifyEnsemble(ens);
        }
    }

    // 5. Acknowledge DAB events (2 s)
    if (now >= m_last_ev_poll + 2000) {
        m_last_ev_poll = now;
        si468x_dab_event_status_t ev;
        si468x_dab_get_event_status(&ev);
    }

    // 6. MOT Slideshow
    if (m_settings.ui.mot) {
        si468x_mot_slideshow_t slide;
        if (si468x_get_mot_slideshow(&slide) == 1 && slide.image_data && slide.image_size > 0) {
            std::string ext = "bin";
            if (slide.image_size >= 4 && slide.image_data[0] == 0x89 &&
                slide.image_data[1] == 'P' && slide.image_data[2] == 'N' && slide.image_data[3] == 'G') {
                ext = "png";
            } else if (slide.image_size >= 3 && slide.image_data[0] == 0xFF &&
                       slide.image_data[1] == 0xD8 && slide.image_data[2] == 0xFF) {
                ext = "jpg";
            }
            const std::string path = "/tmp/si468x-pi-mot-" + std::to_string(m_current_station.service_id) + "." + ext;
            std::ofstream out(path, std::ios::binary);
            if (out) {
                out.write(reinterpret_cast<const char*>(slide.image_data), slide.image_size);
            }
            notifyMot(path, slide);
        }
    }
}

} // namespace si468x_pi
