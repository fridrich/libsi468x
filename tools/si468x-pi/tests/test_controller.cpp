// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/events.hpp"
#include "core/radio_controller.hpp"
#include "core/settings.hpp"
#include "core/station.hpp"
#include "fixtures.h"
#include "mock_chip.h"
#include "si468x.h"
#include "si468x_internal.h"

#include <algorithm>
#include <iostream>
#include <string>
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

    std::vector<si468x_pi::ControllerState> states;
    std::vector<std::string> errors;
    std::vector<std::pair<int, bool>> volumes;
};

void put_label(std::vector<uint8_t>& v, const char* label)
{
    for (int i = 0; i < 16; i++) {
        v.push_back(label && *label ? static_cast<uint8_t>(*label++) : 0x20);
    }
}

void put_service(std::vector<uint8_t>& v, uint32_t sid, uint8_t pty, const char* label,
                 const std::vector<std::pair<uint16_t, uint8_t>>& comps)
{
    v.insert(v.end(), { static_cast<uint8_t>(sid), static_cast<uint8_t>(sid >> 8),
                        static_cast<uint8_t>(sid >> 16), static_cast<uint8_t>(sid >> 24) });
    v.insert(v.end(), { 0, 0, 0, static_cast<uint8_t>((comps.size() << 4) | (pty & 0x1F)) });
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

    // Prepare mock chip expectations for tuning 12A and playing 0x43D1
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 })); // STCINT set
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 })); // SVRLIST set

    // Service list reply
    const auto list_data = make_service_list_payload();
    std::vector<uint8_t> sl_rep = reply({ 0x80, 0x80, 0x00, 0xC0,
                                          static_cast<uint8_t>(list_data.size()),
                                          static_cast<uint8_t>(list_data.size() >> 8) });
    sl_rep.insert(sl_rep.end(), list_data.begin(), list_data.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl_rep);

    // Play service reply
    chip.expect(SI468X_CMD_START_DIGITAL_SERVICE, reply({ 0x80, 0x80, 0x00, 0xC0 }));

    Station st{ "12A", "RTS Premiere", "RTS Prem", 0x43D1, 7, false };
    ctrl.postPlay(st);
    ctrl.waitIdle();

    check(ctrl.state() == ControllerState::Playing, "controller state is Playing");
    check(ctrl.currentFrequency() == 223936000, "current frequency is 12A (223936000 Hz)");
    check(ctrl.currentStation().service_id == 0x43D1, "current station matches");

    // Assert exact command bytes sent to chip
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

    // Verify sink recorded state transitions
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

    // Channel 5A: STCINT set but no lock (times out waiting for ACQ/VALID)
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    // DIGRAD returns no signal
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

    // Test mute toggle
    ctrl.postMute();
    ctrl.waitIdle();
    check(ctrl.isMuted(), "mute toggles muted on");

    ctrl.postMute();
    ctrl.waitIdle();
    check(!ctrl.isMuted(), "second mute unmutes");

    // Test direct set volume
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

    // Prepare tune & play
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

    // Assert STOP_DIGITAL_SERVICE command bytes
    auto stops = chip.sent(SI468X_CMD_STOP_DIGITAL_SERVICE);
    check(stops.size() == 1, "exactly one STOP_DIGITAL_SERVICE frame sent before shutdown");
    if (!stops.empty()) {
        const std::vector<uint8_t> want_stop = { 0x82, 0x00, 0x00, 0x00, 0xD1, 0x43, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 };
        check(stops[0] == want_stop, "STOP_DIGITAL_SERVICE contains playing SId and ComponentId");
    }
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all controller tests passed" << std::endl;
    return 0;
}
