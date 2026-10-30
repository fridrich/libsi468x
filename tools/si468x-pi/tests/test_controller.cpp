// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/events.hpp"
#include "core/radio_controller.hpp"
#include "core/settings.hpp"
#include "core/station.hpp"
#include "fixtures.h"
#include "mock_chip.h"
#include "screens.hpp"
#include "si468x.h"
#include "si468x_internal.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const std::string& message)
{
    if (!cond) {
        std::cerr << "FAIL: " << message << std::endl;
        ++failures;
    }
}

class RecordingSink : public si468x_pi::IControllerEvents {
public:
    std::function<void(const std::string&, int)> on_progress_cb;

    void onState(si468x_pi::ControllerState s) override
    {
        states.push_back(s);
    }

    void onError(const std::string& msg) override
    {
        errors.push_back(msg);
    }

    void onVolume(int pct, bool m) override
    {
        volumes.push_back({ pct, m });
    }

    void onScanProgress(const std::string& ch, int found, int cur, int tot) override
    {
        progress.push_back({ ch, found, cur, tot });
        if (on_progress_cb) {
            on_progress_cb(ch, found);
        }
    }

    void onStationListChanged(const std::vector<si468x_pi::Station>& list) override
    {
        station_lists.push_back(list);
    }

    void onSignal(const si468x_signal_status_t& s) override
    {
        signals.push_back(s);
    }

    void onDls(const std::string& dls) override
    {
        dls_texts.push_back(dls);
    }

    void onSignalLost(bool lost) override
    {
        signal_lost_events.push_back(lost);
    }

    void onTime(const si468x_time_t& t) override
    {
        times.push_back(t);
    }

    void onAudio(const si468x_audio_info_t& a) override
    {
        audios.push_back(a);
    }

    void onMot(const std::string& p, const si468x_mot_slideshow_t&) override
    {
        mot_paths.push_back(p);
    }

    struct ProgressEntry {
        std::string channel;
        int found;
        int current_idx;
        int total;
    };

    std::vector<si468x_pi::ControllerState> states;
    std::vector<std::string> errors;
    std::vector<std::pair<int, bool>> volumes;
    std::vector<ProgressEntry> progress;
    std::vector<std::vector<si468x_pi::Station>> station_lists;
    std::vector<si468x_signal_status_t> signals;
    std::vector<std::string> dls_texts;
    std::vector<bool> signal_lost_events;
    std::vector<si468x_time_t> times;
    std::vector<si468x_audio_info_t> audios;
    std::vector<std::string> mot_paths;
};

void put_label(std::vector<uint8_t>& v, const char* label)
{
    for (int i = 0; i < 16; i++) {
        v.push_back(label && *label ? static_cast<uint8_t>(*label++) : 0x20);
    }
}

void put_service(std::vector<uint8_t>& v, uint32_t sid, uint8_t b4, const char* label,
                 const std::vector<std::pair<uint16_t, uint8_t>>& comps)
{
    v.insert(v.end(), { static_cast<uint8_t>(sid), static_cast<uint8_t>(sid >> 8),
                        static_cast<uint8_t>(sid >> 16), static_cast<uint8_t>(sid >> 24) });
    v.push_back(b4);
    v.push_back(static_cast<uint8_t>(comps.size()));
    v.push_back(0x00); // SICHARSET 0
    v.push_back(0xF0);
    put_label(v, label);
    for (const auto& c : comps) {
        v.insert(v.end(), { static_cast<uint8_t>(c.first), static_cast<uint8_t>(c.first >> 8), c.second, 0x00 });
    }
}

std::vector<uint8_t> make_service_list_payload()
{
    std::vector<uint8_t> p = { 0x01, 0x00, 1, 0, 0, 0 };
    put_service(p, 0x43D1, 0x02, "RTS Premiere", { { 0x0007, (63 << 2) | 0x02 } });
    return p;
}

std::vector<uint8_t> make_two_service_payload()
{
    std::vector<uint8_t> p = { 0x01, 0x00, 2, 0, 0, 0 };
    put_service(p, 0x43D1, 0x02, "RTS Premiere", { { 0x0007, (63 << 2) | 0x02 } });
    put_service(p, 0x43D3, 0x04, "Couleur 3", { { 0x0009, (63 << 2) | 0x02 } });
    return p;
}

void test_error_mapping()
{
    using namespace si468x_pi;

    check(RadioController::formatError(SI468X_SUCCESS) == "success", "error map SUCCESS");
    check(RadioController::formatError(SI468X_ERROR_SPI).find("SPI") != std::string::npos, "error map SPI");
    check(RadioController::formatError(SI468X_ERROR_GPIO).find("GPIO") != std::string::npos, "error map GPIO");
    check(RadioController::formatError(SI468X_ERROR_FIRMWARE).find("Firmware") != std::string::npos, "error map FIRMWARE");
    check(RadioController::formatError(SI468X_ERROR_BOOT).find("boot") != std::string::npos, "error map BOOT");
    check(RadioController::formatError(SI468X_ERROR_TIMEOUT).find("timeout") != std::string::npos, "error map TIMEOUT");
    check(RadioController::formatError(SI468X_ERROR_COMMAND).find("rejected") != std::string::npos, "error map COMMAND");
    check(RadioController::formatError(SI468X_ERROR_PARAM).find("parameter") != std::string::npos, "error map PARAM");
    check(RadioController::formatError(SI468X_ERROR_INVALID_REPLY).find("reply") != std::string::npos, "error map INVALID_REPLY");
    check(RadioController::formatError(SI468X_ERROR_NOT_FOUND).find("not found") != std::string::npos, "error map NOT_FOUND");
    check(RadioController::formatError(-999).find("Unknown") != std::string::npos, "error map unknown");
}

void test_init_failure_handling()
{
    using namespace si468x_pi;

    Settings s;
    s.device.spi = "/dev/spidev-nonexistent";
    s.device.reset_gpio = -1;

    RecordingSink sink;
    RadioController ctrl(s, &sink);

    const int rc = ctrl.initHardware();
    check(rc != SI468X_SUCCESS, "initHardware fails cleanly without hardware");
    check(ctrl.state() == ControllerState::Error, "state is Error on init failure");
    check(!sink.errors.empty(), "error event published on init failure");
}

void test_tune_and_play()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.volume.start = 50;
    RadioController ctrl(s, &sink);

    check(ctrl.start(false), "start controller without hardware boot");
    ctrl.waitIdle();
    check(ctrl.state() == ControllerState::Idle, "starts in Idle state");

    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));

    const auto list_data = make_service_list_payload();
    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(list_data.size()),
                                          static_cast<uint8_t>(list_data.size() >> 8) });
    sl_rep.insert(sl_rep.end(), list_data.begin(), list_data.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);
    chip.expect(SI468X_CMD_START_DIGITAL_SERVICE, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.postPlay(st);
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Playing, "controller state is Playing");
    check(ctrl.currentFrequency() == 223936000, "current frequency is 12A (223936000 Hz)");
    check(ctrl.currentStation().service_id == 0x43D1, "current station matches");

    auto tunes = chip.sent(SI468X_CMD_DAB_TUNE_FREQ);
    check(tunes.size() == 1, "exactly one DAB_TUNE_FREQ sent");
    if (!tunes.empty()) {
        check(tunes[0][2] == 30, "tunes[0] has frequency index 30 (12A)");
    }

    auto plays = chip.sent(SI468X_CMD_START_DIGITAL_SERVICE);
    check(plays.size() == 1, "exactly one START_DIGITAL_SERVICE sent");
    if (!plays.empty()) {
        const std::vector<uint8_t> want_play = { 0x81, 0x00, 0x00, 0x00, 0xD1, 0x43, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 };
        check(plays[0] == want_play, "START_DIGITAL_SERVICE command bytes match exact SId/CompId");
    }

    const bool saw_tuning = std::find(sink.states.begin(), sink.states.end(), ControllerState::Tuning) != sink.states.end();
    const bool saw_playing = std::find(sink.states.begin(), sink.states.end(), ControllerState::Playing) != sink.states.end();
    check(saw_tuning, "recorded Tuning state transition");
    check(saw_playing, "recorded Playing state transition");
}

void test_tune_failure_active_frequency()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    RadioController ctrl(s, &sink);
    ctrl.start(false);
    ctrl.waitIdle();

    const uint32_t prev_freq = ctrl.currentFrequency();

    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);

    Station bad_st{ "5A", "No Signal", "No Signal", 0x1111, 1, false };
    ctrl.postPlay(bad_st);
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Error, "state is Error on tune timeout");
    check(ctrl.currentFrequency() == prev_freq, "active_frequency is NOT set on failure");
    check(!sink.errors.empty(), "error event was published");
}

void test_volume_and_mute()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.volume.start = 50;
    s.volume.step = 5;
    RadioController ctrl(s, &sink);
    ctrl.start(false);
    ctrl.waitIdle();

    const int initial_vol = ctrl.volume();

    ctrl.postVolumeUp();
    ctrl.waitIdle();
    check(ctrl.volume() > initial_vol, "volume up increases volume");
    const int vol_after_up = ctrl.volume();

    ctrl.postVolumeDown();
    ctrl.waitIdle();
    check(ctrl.volume() < vol_after_up, "volume down decreases volume");

    ctrl.postMute();
    ctrl.waitIdle();
    check(ctrl.isMuted(), "mute toggles muted on");

    ctrl.postMute();
    ctrl.waitIdle();
    check(!ctrl.isMuted(), "second mute unmutes");

    ctrl.postSetVolume(40);
    ctrl.waitIdle();
    check(ctrl.volume() == 40, "direct setVolume sets exact level");
}

void test_shutdown_sequence()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    RadioController ctrl(s, &sink);
    ctrl.start(false);
    ctrl.waitIdle();

    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));

    const auto list_data = make_service_list_payload();
    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(list_data.size()),
                                          static_cast<uint8_t>(list_data.size() >> 8) });
    sl_rep.insert(sl_rep.end(), list_data.begin(), list_data.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);
    chip.expect(SI468X_CMD_START_DIGITAL_SERVICE, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.postPlay(st);
    ctrl.waitIdle();
    check(ctrl.state() == ControllerState::Playing, "playing before shutdown");

    chip.writes.clear();

    ctrl.postShutdown();
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Stopped, "state is Stopped after shutdown");

    auto stops = chip.sent(SI468X_CMD_STOP_DIGITAL_SERVICE);
    check(stops.size() == 1, "exactly one STOP_DIGITAL_SERVICE frame sent before shutdown");
    if (!stops.empty()) {
        const std::vector<uint8_t> want_stop = { 0x82, 0x00, 0x00, 0x00, 0xD1, 0x43, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 };
        check(stops[0] == want_stop, "STOP_DIGITAL_SERVICE contains playing SId and ComponentId");
    }
}

void test_two_channel_mock_scan_and_cache_roundtrip()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    const std::string cache_file = "/tmp/si468x-pi-test-cache-" + std::to_string(getpid()) + ".json";
    unlink(cache_file.c_str());

    Settings s;
    s.scan.channels = { "5A", "12A" };
    s.scan.rssi_min = 8;
    RadioController ctrl(s, &sink, cache_file);
    ctrl.start(false);
    ctrl.waitIdle();

    // Channel 1: 5A -> no signal
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);

    // Channel 2: 12A -> locked with 2 services
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_ENSEMBLE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0, 0x43, 0xF0 }));
    const auto list_data = make_two_service_payload();
    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(list_data.size()),
                                          static_cast<uint8_t>(list_data.size() >> 8) });
    sl_rep.insert(sl_rep.end(), list_data.begin(), list_data.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);

    // Service info for the 2 services
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    // Auto-play expectation for the first service ("Couleur 3" sorted first)
    chip.expect(SI468X_CMD_START_DIGITAL_SERVICE, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    ctrl.postScan(true);
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Playing, "auto-played station after scan");
    check(ctrl.stations().size() == 2, "found 2 stations from 12A");
    if (ctrl.stations().size() >= 2) {
        check(ctrl.stations()[0].program == "Couleur 3", "stations are sorted by program name");
        check(ctrl.stations()[1].program == "RTS Premiere", "second station sorted");
    }

    // Cache round-trip verification
    Cache loaded;
    std::vector<std::string> warnings;
    check(load_cache(cache_file, loaded, &warnings), "cache file written and loads");
    check(loaded.complete == true, "cache is marked complete");
    check(loaded.stations.size() == 2, "cache contains both stations");
    if (loaded.stations.size() >= 2 && ctrl.stations().size() >= 2) {
        check(loaded.stations[0].service_id == ctrl.stations()[0].service_id, "cached station 0 matches");
        check(loaded.stations[1].service_id == ctrl.stations()[1].service_id, "cached station 1 matches");
    }

    unlink(cache_file.c_str());
}

void test_rssi_min_and_data_service_skip()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.scan.channels = { "5A", "12A" };
    s.scan.rssi_min = 20; // threshold 20 dBµV
    s.scan.include_data = false;
    RadioController ctrl(s, &sink);
    ctrl.start(false);
    ctrl.waitIdle();

    // 5A: Locked, but RSSI = 15 dBµV (< 20) -> must be skipped!
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    std::vector<uint8_t> low_rssi = FX_DIGRAD_SYNCED;
    low_rssi[7] = 15; // RSSI 15
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, low_rssi);

    // 12A: Locked, RSSI = 48 dBµV (>= 20) -> accepted
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_ENSEMBLE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    // 1 audio service + 1 data service
    std::vector<uint8_t> p = { 0x01, 0x00, 2, 0, 0, 0 };
    put_service(p, 0x43D1, 0x02, "RTS Premiere", { { 0x0007, (63 << 2) | 0x02 } });
    put_service(p, 0xE1400103, 0x01, "Data Service", { { 0xC010, 60 << 2 } }); // data service

    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(p.size()),
                                          static_cast<uint8_t>(p.size() >> 8) });
    sl_rep.insert(sl_rep.end(), p.begin(), p.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    ctrl.postScan(false); // auto_play false
    ctrl.waitIdle();

    check(ctrl.stations().size() == 1, "low RSSI channel and data service skipped");
    if (!ctrl.stations().empty()) {
        check(ctrl.stations()[0].program == "RTS Premiere", "only audio service retained");
    }
}

void test_abort_between_channels_keeps_partial_cache()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    const std::string cache_file = "/tmp/si468x-pi-test-partial-" + std::to_string(getpid()) + ".json";
    unlink(cache_file.c_str());

    Settings s;
    s.scan.channels = { "12A", "12B" };
    s.scan.rssi_min = 8;
    RadioController ctrl(s, &sink, cache_file);
    ctrl.start(false);
    ctrl.waitIdle();

    // 12A succeeds with 1 station
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_ENSEMBLE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    const auto list_data = make_service_list_payload();
    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(list_data.size()),
                                          static_cast<uint8_t>(list_data.size() >> 8) });
    sl_rep.insert(sl_rep.end(), list_data.begin(), list_data.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    // Abort scan immediately after 12A completes
    sink.on_progress_cb = [&](const std::string& ch, int found) {
        if (ch == "12A" && found > 0) {
            ctrl.abortScan();
        }
    };

    ctrl.postScan(false);
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Idle, "state is Idle after aborted scan");
    check(ctrl.stations().size() == 1, "stations from 12A preserved in controller");

    Cache loaded;
    std::vector<std::string> warnings;
    check(load_cache(cache_file, loaded, &warnings), "partial cache written to disk");
    check(loaded.complete == false, "partial cache marked complete=false");
    check(loaded.stations.size() == 1, "partial cache contains 1 station");

    unlink(cache_file.c_str());
}

void test_warm_start_does_not_scan()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    const std::string cache_file = "/tmp/si468x-pi-test-warm-" + std::to_string(getpid()) + ".json";
    unlink(cache_file.c_str());

    // Write a pre-existing cache file
    Cache existing;
    existing.version = 1;
    existing.complete = true;
    existing.stations = {
        Station{ "12A", "Warm Station 1", "Warm 1", 0x1111, 1, false },
        Station{ "12A", "Warm Station 2", "Warm 2", 0x2222, 2, false },
    };
    save_cache(cache_file, existing);

    Settings s;
    RadioController ctrl(s, &sink, cache_file);
    check(ctrl.loadCache(cache_file), "load existing cache succeeds");
    check(ctrl.stations().size() == 2, "stations loaded from cache");

    ctrl.start(false);
    ctrl.waitIdle();

    // Verify no tune commands were sent to the chip
    check(chip.sent(SI468X_CMD_DAB_TUNE_FREQ).empty(), "warm start does not perform tuning/scan");

    unlink(cache_file.c_str());
}

void test_scanning_screen_abort_wiring()
{
    using namespace si468x_pi;

    ScanningScreen screen;
    bool aborted = false;
    screen.setAbortCallback([&]() { aborted = true; });

    screen.handleInput(InputAction::UP);
    check(!aborted, "UP does not abort");

    screen.handleInput(InputAction::MENU);
    check(aborted, "MENU triggers abort callback");
}

void test_dls_and_telemetry_polling()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    RadioController ctrl(s, &sink);

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.setSimulatedPlaying(st);
    check(ctrl.state() == ControllerState::Playing, "playing station");

    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, FX_DSD_DLS_TEXT);
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    std::vector<uint8_t> time_rep = reply({ 0x80, 0x80, 0x00, 0xC0, 0xEA, 0x07, 0x00, 0x0A, 0x1E, 0x0C, 0x1E, 0x00 });
    chip.expect(SI468X_CMD_DAB_GET_TIME, time_rep);

    ctrl.pollTelemetry(1000);
    ctrl.pollTelemetry(5000);

    check(!sink.dls_texts.empty(), "DLS text received");
    if (!sink.dls_texts.empty()) {
        check(sink.dls_texts.back() == "Juste mesure", "exact DLS text matches captured broadcast");
    }
    check(!sink.signals.empty(), "signal status received");
    if (!sink.signals.empty()) {
        check(sink.signals.front().rssi == 48, "signal RSSI matches");
    }
    check(!sink.times.empty(), "time metrics received");
    if (!sink.times.empty()) {
        check(sink.times.back().year == 2026, "time year matches");
    }
}

void test_signal_loss_banner_timing()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.poll.signal_ms = 500;
    RadioController ctrl(s, &sink);

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.setSimulatedPlaying(st);

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    ctrl.pollTelemetry(1000);
    check(!ctrl.isSignalLost(), "signal initially present");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);
    ctrl.pollTelemetry(1500);
    check(!ctrl.isSignalLost(), "signal not marked lost before 2 s");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);
    ctrl.pollTelemetry(2500);
    check(!ctrl.isSignalLost(), "signal still not marked lost at 1.5 s");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);
    ctrl.pollTelemetry(3600);
    check(ctrl.isSignalLost(), "signal marked lost after 2 s without sync");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    ctrl.pollTelemetry(4200);
    check(!ctrl.isSignalLost(), "signal restored immediately when sync returns");
}

void test_spi_consecutive_errors()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.poll.signal_ms = 500;
    RadioController ctrl(s, &sink);

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.setSimulatedPlaying(st);
    check(ctrl.state() == ControllerState::Playing, "playing before SPI error test");

    si468x_set_test_transport([](const uint8_t*, uint8_t*, size_t, void*) -> int {
        return -1;
    }, nullptr);

    ctrl.pollTelemetry(1000);
    check(ctrl.state() == ControllerState::Playing, "first SPI error does not abort");

    ctrl.pollTelemetry(1600);
    check(ctrl.state() == ControllerState::Error, "second consecutive SPI error transitions to Error state");

    si468x_set_test_transport(nullptr, nullptr);
}

void test_synthetic_polling_flat_memory()
{
    using namespace si468x_pi;

    MockChip chip;
    RecordingSink sink;

    Settings s;
    s.poll.dls_ms = 300;
    s.poll.signal_ms = 1000;
    RadioController ctrl(s, &sink);

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.setSimulatedPlaying(st);

    for (int i = 0; i < 20; i++) {
        chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, FX_DSD_EMPTY);
        chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    }

    uint64_t simulated_now = 10000;
    for (int step = 0; step < 6000; ++step) {
        simulated_now += 100;
        ctrl.pollTelemetry(simulated_now);
    }

    check(ctrl.state() == ControllerState::Playing, "controller still playing after 10 min synthetic polling");
}

} // namespace

int main()
{
    std::cout << "si468x-pi controller tests" << std::endl;

    test_error_mapping();
    test_init_failure_handling();
    test_tune_and_play();
    test_tune_failure_active_frequency();
    test_volume_and_mute();
    test_shutdown_sequence();
    test_two_channel_mock_scan_and_cache_roundtrip();
    test_rssi_min_and_data_service_skip();
    test_abort_between_channels_keeps_partial_cache();
    test_warm_start_does_not_scan();
    test_scanning_screen_abort_wiring();
    test_dls_and_telemetry_polling();
    test_signal_loss_banner_timing();
    test_spi_consecutive_errors();
    test_synthetic_polling_flat_memory();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all controller tests passed" << std::endl;
    return 0;
}
