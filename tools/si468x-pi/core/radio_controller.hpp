// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/events.hpp"
#include "core/settings.hpp"
#include "core/station.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace si468x_pi {

/**
 * @brief Thread-safe owner of the Si468x tuner hardware.
 *
 * Mandate: RadioController is the SINGLE thread allowed to call libsi468x functions.
 * All chip interactions (SPI transactions, frequency tuning, playback control, volume)
 * execute sequentially inside the controller's worker thread via its command queue.
 */
class RadioController {
public:
    enum class CommandType {
        Play,
        Stop,
        Scan,
        VolumeUp,
        VolumeDown,
        SetVolume,
        Mute,
        AudioOutput,
        Shutdown,
    };

    struct Command {
        CommandType type;
        Station station;
        int int_arg = 0;
        bool bool_arg = false;
    };

    explicit RadioController(const Settings& settings, IControllerEvents* events = nullptr,
                            const std::string& cache_path = "");
    ~RadioController();

    // Start controller thread. If auto_boot is true, calls initHardware() to boot chip.
    bool start(bool auto_boot = true);

    // Stop thread and shut down hardware.
    void stop();

    // Map libsi468x error codes to human-readable user messages.
    static std::string formatError(int code);

    // Explicit hardware init (called during start() or for test verification).
    int initHardware();

    // Cache management
    bool loadCache(const std::string& path);
    bool saveCache(const std::string& path) const;
    void setCachePath(const std::string& path);
    const std::string& cachePath() const;

    // Station list
    const std::vector<Station>& stations() const;
    void setStations(const std::vector<Station>& stations);

    // Thread-safe command posting
    void postPlay(const Station& station);
    void postStop();
    void postScan(bool auto_play = true);
    void abortScan();
    bool isScanning() const;
    void postVolumeUp();
    void postVolumeDown();
    void postSetVolume(int chip_volume);
    void postMute();
    void postAudioOutput(int output_mode);
    void postShutdown();

    // Wait until command queue is empty and worker thread is idle (for test synchronization).
    void waitIdle();

    // State and telemetry accessors
    ControllerState state() const;
    int volume() const;
    int volumePercent() const;
    bool isMuted() const;
    uint32_t currentFrequency() const;
    const Station& currentStation() const;

    void setEvents(IControllerEvents* events);

private:
    void threadLoop(bool auto_boot);
    void executeCommand(const Command& cmd);
    void executePlay(const Station& station);
    void executeStop();
    void executeScan(bool auto_play);
    void executeShutdown();
    void applyVolume(int chip_volume);
    void notifyState(ControllerState s);
    void notifyVolume(int percent, bool muted);
    void notifyError(const std::string& msg);
    void notifyScanProgress(const std::string& channel, int found, int current_idx, int total);
    void notifyStationListChanged(const std::vector<Station>& stations);

    Settings m_settings;
    IControllerEvents* m_events = nullptr;
    std::string m_cache_path;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_idle_cv;
    std::deque<Command> m_queue;

    std::thread m_worker;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_abort_scan{ false };
    bool m_busy = false;

    ControllerState m_state = ControllerState::Idle;
    int m_current_volume = 50;       // 0..63 chip units
    int m_saved_volume = 50;         // for mute toggle
    bool m_muted = false;
    uint32_t m_current_frequency = 0;
    Station m_current_station;
    std::vector<Station> m_stations;
};

} // namespace si468x_pi
