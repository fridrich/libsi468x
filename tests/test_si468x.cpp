/*
 *    Copyright (C) 2026
 *    test_si468x.cpp - Test Suite for libsi468x Driver Library
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <chrono>
#include "si468x.h"
#include "si468x_internal.h"
#include "mock_chip.h"
#include "fixtures.h"

// Macro helper for clean assertions
#define ASSERT_TRUE(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "FAIL: " << msg << " (Assertion '" #expr "' failed)" << std::endl; \
            return false; \
        } \
    } while (0)

static bool test_short_label_decoding()
{
    std::cout << "Running test: test_short_label_decoding..." << std::endl;

    char short_label[17];

    // Test Case 1: Standard 7-character mask (0xFE00)
    std::memset(short_label, 0, sizeof(short_label));
    const uint8_t raw1[] = "Station 1";
    si468x_decode_short_label(raw1, 9, 0xFE00, 0, short_label, sizeof(short_label));
    ASSERT_TRUE(std::strcmp(short_label, "Station") == 0, "0xFE00 on 'Station 1' must yield 'Station'");

    // Test Case 2: Discontiguous characters (0xF080 -> index 0,1,2,3 and index 8)
    std::memset(short_label, 0, sizeof(short_label));
    const uint8_t raw2[] = "Station 2";
    si468x_decode_short_label(raw2, 9, 0xF080, 0, short_label, sizeof(short_label));
    ASSERT_TRUE(std::strcmp(short_label, "Stat2") == 0, "0xF080 on 'Station 2' must yield 'Stat2'");

    // Test Case 3: Boundary case - Full mask (0xFFFF). Must truncate to max 8 characters.
    std::memset(short_label, 0, sizeof(short_label));
    const uint8_t raw3[] = "1234567890abcdef";
    si468x_decode_short_label(raw3, 16, 0xFFFF, 0, short_label, sizeof(short_label));
    ASSERT_TRUE(std::strcmp(short_label, "12345678") == 0, "0xFFFF must truncate long string to 8 chars");

    // Test Case 4: Boundary case - Empty mask (0x0000)
    std::memset(short_label, 0, sizeof(short_label));
    const uint8_t raw4[] = "Station 4";
    si468x_decode_short_label(raw4, 9, 0x0000, 0, short_label, sizeof(short_label));
    ASSERT_TRUE(std::strcmp(short_label, "") == 0, "0x0000 mask must yield empty string");

    // Test Case 5: Complex custom mask (0xAAAA -> binary 1010101010101010)
    // Matches indices: 0, 2, 4, 6, 8, 10, 12, 14
    // String: "a b c d e f g h" (spaces are at odd indices)
    std::memset(short_label, 0, sizeof(short_label));
    const uint8_t raw5[] = "abcdefghijklmnop";
    si468x_decode_short_label(raw5, 16, 0xAAAA, 0, short_label, sizeof(short_label));
    ASSERT_TRUE(std::strcmp(short_label, "acegikmo") == 0, "0xAAAA mask must extract alternating characters");

    // Truncation fills the buffer, never splits a UTF-8 sequence (old bound: max_len - 4)
    const uint8_t raw6[] = "LifeStyl";
    si468x_decode_short_label(raw6, 8, 0xFF00, 0, short_label, 9);
    ASSERT_TRUE(std::strcmp(short_label, "LifeStyl") == 0, "8 chars fit a 9-byte buffer");
    const uint8_t raw7[] = "Caf\x82";
    si468x_decode_short_label(raw7, 4, 0xF000, 0, short_label, 5);
    ASSERT_TRUE(std::strcmp(short_label, "Caf") == 0, "2-byte char dropped whole when it does not fit");
    si468x_decode_short_label(raw7, 4, 0xF000, 0, short_label, 6);
    ASSERT_TRUE(std::strcmp(short_label, "Caf\xC3\xA9") == 0, "EBU 0x82 -> e-acute when it fits");
    const uint8_t raw8[] = { 0x00, 'A', 0x00, 'b', 0x00, 0xC9 }; // UCS-2 "AbÉ"
    si468x_decode_short_label(raw8, 6, 0xA000, 6, short_label, 9);
    ASSERT_TRUE(std::strcmp(short_label, "A\xC3\x89") == 0, "UCS-2: mask bits select characters, not bytes");

    std::cout << "PASS: test_short_label_decoding" << std::endl;
    return true;
}

static bool test_initialization_failure()
{
    std::cout << "Running test: test_initialization_failure (Diagnostic validation)..." << std::endl;

    // Call si468x_init with an invalid SPI path and non-existent GPIO.
    // Since we are running on host without real board attached, we verify that
    // the driver returns expected error codes rather than crashing or freezing!

    // We expect SI468X_ERROR_GPIO because exporting an invalid/locked pin (e.g. -5)
    // should fail during standard Linux sysfs gpio export stage.
    int ret = si468x_init("/dev/spidev-nonexistent", -5, SI468X_BOOT_DAB);
    ASSERT_TRUE(ret == SI468X_ERROR_GPIO, "Invalid GPIO pin must safely return SI468X_ERROR_GPIO");

    std::cout << "PASS: test_initialization_failure" << std::endl;
    return true;
}

static bool test_api_signatures_uninitialized()
{
    std::cout << "Running test: test_api_signatures_uninitialized..." << std::endl;

    char label[17];
    char short_label[9];
    uint8_t subchannel_id = 0;

    // 1. Verify si468x_get_component_info signature and uninitialized failure
    int ret_comp = si468x_get_component_info(0xf226, 6, label, short_label, nullptr, &subchannel_id);
    ASSERT_TRUE(ret_comp == -1, "si468x_get_component_info must fail safely when uninitialized");

    // 2. Verify si468x_get_dls_text signature and uninitialized failure
    char dls_text[129];
    int ret_dls = si468x_get_dls_text(dls_text, sizeof(dls_text));
    ASSERT_TRUE(ret_dls == SI468X_ERROR_SPI, "si468x_get_dls_text must fail safely when uninitialized");

    // 3. Verify si468x_get_time signature and uninitialized failure
    si468x_time_t dtime;
    int ret_time = si468x_get_time(&dtime);
    ASSERT_TRUE(ret_time == -1, "si468x_get_time must fail safely when uninitialized");

    // 4. Verify si468x_get_audio_info signature and uninitialized failure
    si468x_audio_info_t ainfo;
    int ret_ainfo = si468x_get_audio_info(&ainfo);
    ASSERT_TRUE(ret_ainfo == -1, "si468x_get_audio_info must fail safely when uninitialized");

    // 5. Verify si468x_get_service_info signature and uninitialized failure
    si468x_service_info_t sinfo;
    int ret_sinfo = si468x_get_service_info(0x12345678, &sinfo);
    ASSERT_TRUE(ret_sinfo == -1, "si468x_get_service_info must fail safely when uninitialized");

    // 6. Verify si468x_get_event_status signature and uninitialized failure
    si468x_event_status_t estatus;
    int ret_ev = si468x_get_event_status(&estatus);
    ASSERT_TRUE(ret_ev == -1, "si468x_get_event_status must fail safely when uninitialized");

    std::cout << "PASS: test_api_signatures_uninitialized" << std::endl;
    return true;
}

static bool test_new_fm_rds_api_uninitialized()
{
    std::cout << "Running test: test_new_fm_rds_api_uninitialized..." << std::endl;

    // 1. Verify si468x_get_chip_info failures when uninitialized
    si468x_chip_info_t info;
    int ret_chip = si468x_get_chip_info(&info);
    ASSERT_TRUE(ret_chip == -1, "si468x_get_chip_info must fail safely when uninitialized");

    // 2. Verify si468x_set_frequency_table boundary parameters
    uint32_t freqs[3] = { 174928000, 188928000, 223936000 };
    int ret_freq_table_invalid = si468x_set_frequency_table(nullptr, 3);
    ASSERT_TRUE(ret_freq_table_invalid == SI468X_ERROR_PARAM, "si468x_set_frequency_table must fail on null pointer");

    int ret_freq_table_uninit = si468x_set_frequency_table(freqs, 3);
    ASSERT_TRUE(ret_freq_table_uninit == -1, "si468x_set_frequency_table must fail when uninitialized");

    // Failed table upload must not activate it: 5B (not in freqs) still maps via standard table
    ASSERT_TRUE(si468x_set_frequency(176640000) != SI468X_ERROR_PARAM,
                "failed si468x_set_frequency_table must not replace standard channel table");

    // 3. Verify si468x_tune_fm signature and uninitialized failure
    int ret_tune = si468x_tune_fm(98100);
    ASSERT_TRUE(ret_tune == -1, "si468x_tune_fm must fail safely when uninitialized");

    // 4. Verify si468x_get_fm_status signature and uninitialized failure
    si468x_fm_status_t status;
    int ret_status = si468x_get_fm_status(&status);
    ASSERT_TRUE(ret_status == -1, "si468x_get_fm_status must fail safely when uninitialized");

    // 5. Verify si468x_get_rds_text signature and uninitialized failure
    char rds_text[65];
    int ret_rds = si468x_get_rds_text(rds_text, sizeof(rds_text));
    ASSERT_TRUE(ret_rds == -1, "si468x_get_rds_text must fail safely when uninitialized");

    // 6. Verify si468x_set_rds_region signature and uninitialized failure
    int ret_reg = si468x_set_rds_region(SI468X_REGION_EUROPE);
    ASSERT_TRUE(ret_reg == -1, "si468x_set_rds_region must fail safely when uninitialized");

    std::cout << "PASS: test_new_fm_rds_api_uninitialized" << std::endl;
    return true;
}

static bool test_audio_output_modes()
{
    std::cout << "Running test: test_audio_output_modes..." << std::endl;

    // Call si468x_set_audio_output with SI468X_AUDIO_ANALOG, SI468X_AUDIO_I2S, and SI468X_AUDIO_SIMUL.
    // All of these should return SI468X_SUCCESS since spi_fd is < 0 (uninitialized)
    // and the value is statically stored in active_audio_mode.
    int ret_analog = si468x_set_audio_output(SI468X_AUDIO_ANALOG);
    ASSERT_TRUE(ret_analog == SI468X_SUCCESS, "Setting Analog output mode must return SI468X_SUCCESS when uninitialized");

    int ret_i2s = si468x_set_audio_output(SI468X_AUDIO_I2S);
    ASSERT_TRUE(ret_i2s == SI468X_SUCCESS, "Setting I2S output mode must return SI468X_SUCCESS when uninitialized");

    int ret_simul = si468x_set_audio_output(SI468X_AUDIO_SIMUL);
    ASSERT_TRUE(ret_simul == SI468X_SUCCESS, "Setting Simultaneous output mode must return SI468X_SUCCESS when uninitialized");

    std::cout << "PASS: test_audio_output_modes" << std::endl;
    return true;
}

static bool test_telemetry_mappings()
{
    std::cout << "Running test: test_telemetry_mappings..." << std::endl;

    // Test Case 1: Protection levels
    ASSERT_TRUE(std::strcmp(si468x_get_protection_text(1), "UEP-1") == 0, "Index 1 must map to 'UEP-1'");
    ASSERT_TRUE(std::strcmp(si468x_get_protection_text(6), "EEP-A1") == 0, "Index 6 must map to 'EEP-A1'");
    ASSERT_TRUE(std::strcmp(si468x_get_protection_text(13), "EEP-B4") == 0, "Index 13 must map to 'EEP-B4'");
    ASSERT_TRUE(std::strcmp(si468x_get_protection_text(50), "") == 0, "Out of bounds protection level index must map to empty string");

    // Test Case 2: Audio Modes
    ASSERT_TRUE(std::strcmp(si468x_get_audio_mode_text(1), "Mono") == 0, "Index 1 must map to 'Mono'");
    ASSERT_TRUE(std::strcmp(si468x_get_audio_mode_text(2), "Stereo") == 0, "Index 2 must map to 'Stereo'");
    ASSERT_TRUE(std::strcmp(si468x_get_audio_mode_text(50), "") == 0, "Out of bounds audio mode index must map to empty string");

    // Test Case 3: Service Types
    ASSERT_TRUE(std::strcmp(si468x_get_service_type_text(4), "DAB+") == 0, "Index 4 must map to 'DAB+'");
    ASSERT_TRUE(std::strcmp(si468x_get_service_type_text(5), "DAB") == 0, "Index 5 must map to 'DAB'");
    ASSERT_TRUE(std::strcmp(si468x_get_service_type_text(50), "") == 0, "Out of bounds service type index must map to empty string");

    std::cout << "PASS: test_telemetry_mappings" << std::endl;
    return true;
}

static bool test_mot_slideshow_uninitialized()
{
    std::cout << "Running test: test_mot_slideshow_uninitialized..." << std::endl;

    si468x_mot_slideshow_t slide;
    int ret = si468x_get_mot_slideshow(&slide);
    ASSERT_TRUE(ret == SI468X_ERROR_SPI, "si468x_get_mot_slideshow must fail safely when uninitialized");

    int ret_null = si468x_get_mot_slideshow(nullptr);
    ASSERT_TRUE(ret_null == SI468X_ERROR_PARAM, "si468x_get_mot_slideshow must fail gracefully with null pointer");

    std::cout << "PASS: test_mot_slideshow_uninitialized" << std::endl;
    return true;
}

static bool test_new_seek_and_telemetry_uninitialized()
{
    std::cout << "Running test: test_new_seek_and_telemetry_uninitialized..." << std::endl;

    // Verify FM seek fails safely
    int ret_seek = si468x_fm_seek_start(1, 1);
    ASSERT_TRUE(ret_seek == -1, "si468x_fm_seek_start must fail safely when uninitialized");

    // Verify DAB announcement support fails safely
    uint16_t asw = 0;
    int ret_ann_sup = si468x_dab_get_announcement_support(0x12345678, 0x11223344, &asw);
    ASSERT_TRUE(ret_ann_sup == -1, "si468x_dab_get_announcement_support must fail safely when uninitialized");

    // Verify DAB announcement info fails safely
    uint32_t sid, cid;
    int ret_ann_info = si468x_dab_get_announcement_info(0, &sid, &cid, &asw);
    ASSERT_TRUE(ret_ann_info == -1, "si468x_dab_get_announcement_info must fail safely when uninitialized");

    // Verify DAB service linking fails safely
    uint8_t link_buf[32];
    int ret_link = si468x_dab_get_service_linking(0x12345678, link_buf, sizeof(link_buf));
    ASSERT_TRUE(ret_link == -1, "si468x_dab_get_service_linking must fail safely when uninitialized");

    // Verify FMHD PSD text retrieval fails safely
    char psd_buf[128];
    int ret_psd = si468x_fmhd_get_psd_text(0, 0, psd_buf, sizeof(psd_buf));
    ASSERT_TRUE(ret_psd == -1, "si468x_fmhd_get_psd_text must fail safely when uninitialized");

    // Verify FMHD station info fails safely
    char sis_buf[128];
    int ret_sis = si468x_fmhd_get_station_info(1, sis_buf, sizeof(sis_buf));
    ASSERT_TRUE(ret_sis == -1, "si468x_fmhd_get_station_info must fail safely when uninitialized");

    // Verify DAB subchannel info retrieval fails safely
    si468x_subchan_info_t sub_info;
    int ret_sub = si468x_dab_get_subchan_info(0x12345678, 0x11223344, &sub_info);
    ASSERT_TRUE(ret_sub == -1, "si468x_dab_get_subchan_info must fail safely when uninitialized");

    // Verify FMHD alert message fails safely
    char alert_buf[128];
    int ret_alert = si468x_fmhd_get_alert_message(alert_buf, sizeof(alert_buf));
    ASSERT_TRUE(ret_alert == -1, "si468x_fmhd_get_alert_message must fail safely when uninitialized");

    // Verify DAB frequency info retrieval fails safely
    si468x_freq_element_t freq_el;
    int ret_freq = si468x_dab_get_freq_info(&freq_el, 1);
    ASSERT_TRUE(ret_freq == -1, "si468x_dab_get_freq_info must fail safely when uninitialized");

    // Verify RF AGC status fails safely
    si468x_agc_status_t agc_status;
    int ret_agc = si468x_get_agc_status(&agc_status);
    ASSERT_TRUE(ret_agc == -1, "si468x_get_agc_status must fail safely when uninitialized");

    // Verify DAB-specific event status fails safely
    si468x_dab_event_status_t dab_ev_status;
    int ret_dab_ev = si468x_dab_get_event_status(&dab_ev_status);
    ASSERT_TRUE(ret_dab_ev == -1, "si468x_dab_get_event_status must fail safely when uninitialized");

    // Verify DAB antenna calibration fails safely
    uint16_t peak_antcap = 0;
    int ret_calib = si468x_dab_calibrate_antenna(223936000, &peak_antcap);
    ASSERT_TRUE(ret_calib == -1, "si468x_dab_calibrate_antenna must fail safely when uninitialized");

    // Off-table frequency must be rejected, not silently tuned to 5A
    ASSERT_TRUE(si468x_set_frequency(223000000) == SI468X_ERROR_PARAM,
                "si468x_set_frequency must reject frequency not in channel table");

    // Verify DAB other ensemble info retrieval fails safely
    uint16_t oe_eids[8];
    int ret_oe = si468x_dab_get_other_ensemble_info(0x12345678, oe_eids, 8);
    ASSERT_TRUE(ret_oe == -1, "si468x_dab_get_other_ensemble_info must fail safely when uninitialized");

    // Verify DAB BER diagnostics fails safely
    uint32_t b_cnt = 0, f_cnt = 0;
    int ret_ber = si468x_dab_get_ber_info(0, &b_cnt, &f_cnt);
    ASSERT_TRUE(ret_ber == -1, "si468x_dab_get_ber_info must fail safely when uninitialized");

    // Verify FMHD BER diagnostics fails safely
    uint32_t fmhd_b_cnt = 0;
    int ret_fmhd_ber = si468x_fmhd_get_ber_info(0, &fmhd_b_cnt);
    ASSERT_TRUE(ret_fmhd_ber == -1, "si468x_fmhd_get_ber_info must fail safely when uninitialized");

    std::cout << "PASS: test_new_seek_and_telemetry_uninitialized" << std::endl;
    return true;
}

static bool test_error_definitions()
{
    std::cout << "Running test: test_error_definitions..." << std::endl;

    ASSERT_TRUE(SI468X_SUCCESS == 0, "SI468X_SUCCESS must be 0");
    ASSERT_TRUE(SI468X_ERROR_SPI == -1, "SI468X_ERROR_SPI must be -1");
    ASSERT_TRUE(SI468X_ERROR_GPIO == -2, "SI468X_ERROR_GPIO must be -2");
    ASSERT_TRUE(SI468X_ERROR_FIRMWARE == -3, "SI468X_ERROR_FIRMWARE must be -3");
    ASSERT_TRUE(SI468X_ERROR_BOOT == -4, "SI468X_ERROR_BOOT must be -4");
    ASSERT_TRUE(SI468X_ERROR_TIMEOUT == -5, "SI468X_ERROR_TIMEOUT must be -5");
    ASSERT_TRUE(SI468X_ERROR_COMMAND == -6, "SI468X_ERROR_COMMAND must be -6");
    ASSERT_TRUE(SI468X_ERROR_PARAM == -7, "SI468X_ERROR_PARAM must be -7");
    ASSERT_TRUE(SI468X_ERROR_INVALID_REPLY == -8, "SI468X_ERROR_INVALID_REPLY must be -8");
    ASSERT_TRUE(SI468X_ERROR_NOT_FOUND == -9, "SI468X_ERROR_NOT_FOUND must be -9");

    std::cout << "PASS: test_error_definitions" << std::endl;
    return true;
}

static bool test_framing_cts_poll()
{
    std::cout << "Running test: test_framing_cts_poll..." << std::endl;

    MockChip chip;
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED, 2);
    si468x_signal_status_t st;
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_SUCCESS, "CTS on 3rd poll must succeed");
    ASSERT_TRUE(chip.reads.size() == 4, "3 CTS polls + 1 payload read");
    ASSERT_TRUE(chip.reads.back() == 24, "DIGRAD payload read is 1 + reply length");
    ASSERT_TRUE(chip.bad_reads == 0, "every read starts with RD_REPLY");
    ASSERT_TRUE(chip.sent(SI468X_CMD_DAB_DIGRAD_STATUS).size() == 1, "one DIGRAD write");

    std::cout << "PASS: test_framing_cts_poll" << std::endl;
    return true;
}

static long ms_since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
}

static bool test_send_command_err_timeout()
{
    std::cout << "Running test: test_send_command_err_timeout..." << std::endl;

    MockChip chip;
    const uint8_t cmd[2] = { 0x12, 0x00 };
    uint8_t resp[13];

    // ERR is an error by default, but resp is still read for the error code
    chip.expect(0x12, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x03 }));
    ASSERT_TRUE(send_command(cmd, 2, resp, 13) == SI468X_ERROR_COMMAND, "ERR must return SI468X_ERROR_COMMAND");
    ASSERT_TRUE(resp[5] == 0x03, "error code readable after ERR");

    chip.expect(0x12, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x03 }));
    ASSERT_TRUE(send_command(cmd, 2, resp, 13, 1000, SC_ERR_OK) == SI468X_SUCCESS, "SC_ERR_OK accepts ERR");
    ASSERT_TRUE(resp[1] == 0xC0, "caller sees ERR in resp[1]");

    // CTS never set
    chip.expect(0x12, MockChip::cts(), 1 << 30);
    auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(send_command(cmd, 2, resp, 13, 20) == SI468X_ERROR_TIMEOUT, "no CTS must time out");
    long el = ms_since(t0);
    ASSERT_TRUE(el >= 20 && el < 60, "timeout honoured within 20-60 ms");

    // CTS after ~450 polls (>= 400 ms): the old 50-poll cap would have failed
    chip.expect(0x12, FX_FUNC_INFO, 450);
    t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(send_command(cmd, 2, resp, 13) == SI468X_SUCCESS, "late CTS within timeout_ms must succeed");
    ASSERT_TRUE(ms_since(t0) >= 300, "loop really waited");

    // read_reply: one transfer, RD_REPLY opcode
    size_t n = chip.reads.size();
    ASSERT_TRUE(read_reply(resp, 13) == SI468X_SUCCESS && chip.reads.size() == n + 1, "read_reply is one transfer");
    ASSERT_TRUE(chip.bad_reads == 0 && resp[5] == 0x06, "read_reply re-reads the reply buffer");

    // wait_stc
    chip.current = FX_DIGRAD_SYNCED; // status 0x81
    ASSERT_TRUE(wait_stc(100) == SI468X_SUCCESS, "STCINT set -> success");
    chip.current = FX_DIGRAD_PRE_STC; // status 0x80
    ASSERT_TRUE(wait_stc(30) == SI468X_ERROR_TIMEOUT, "no STCINT -> timeout");

    std::cout << "PASS: test_send_command_err_timeout" << std::endl;
    return true;
}

static bool test_stop_service_ids()
{
    std::cout << "Running test: test_stop_service_ids..." << std::endl;

    MockChip chip;
    ASSERT_TRUE(si468x_play_service(0x43D1, 7) == SI468X_SUCCESS, "play");
    ASSERT_TRUE(si468x_stop_service() == SI468X_SUCCESS, "stop");
    auto stop = chip.sent(SI468X_CMD_STOP_DIGITAL_SERVICE);
    ASSERT_TRUE(stop.size() == 1, "one STOP frame");
    const std::vector<uint8_t> want = { 0x82, 0, 0, 0, 0xD1, 0x43, 0, 0, 7, 0, 0, 0 };
    ASSERT_TRUE(stop[0] == want, "STOP carries SId/CompId of the playing service (zeros -> ERR on chip)");

    std::cout << "PASS: test_stop_service_ids" << std::endl;
    return true;
}

static bool test_dab_tune_waits_stc()
{
    std::cout << "Running test: test_dab_tune_waits_stc..." << std::endl;

    MockChip chip;
    // First tune rejected (chip busy), retry accepted with STCINT; locked; list after 2 event polls
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x18 }));
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00 }));
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(si468x_set_frequency(223936000) == SI468X_SUCCESS, "12A tune after retry");
    ASSERT_TRUE(ms_since(t0) < 3000 + 1000, "no fixed 3 s FIC sleep beyond the retry's STC wait");
    ASSERT_TRUE(si468x_get_frequency() == 223936000, "active_frequency set on success");
    auto tunes = chip.sent(SI468X_CMD_DAB_TUNE_FREQ);
    ASSERT_TRUE(tunes.size() == 2 && tunes[1][2] == 30, "retried DAB_TUNE_FREQ index 30");
    ASSERT_TRUE(chip.sent(SI468X_CMD_DAB_GET_EVENT_STATUS).size() == 2, "stops polling at SVRLIST");
    ASSERT_TRUE(chip.sent(SI468X_CMD_GET_DIGITAL_SERVICE_LIST).empty(), "no bogus {0x80,0x02}");

    // STC but no VALID/ACQ (empty channel): rejected after the short grace, not 2.5 s
    chip.writes.clear();
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(si468x_set_frequency(174928000) == SI468X_ERROR_TIMEOUT, "no lock -> timeout");
    ASSERT_TRUE(ms_since(t0) < 1000, "fast reject of empty channel");
    ASSERT_TRUE(si468x_get_frequency() == 0, "active_frequency cleared on failure");

    // STCINT never set -> timeout, no lock polling
    chip.writes.clear();
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x80, 0x80, 0x00, 0xC0 }));
    ASSERT_TRUE(si468x_set_frequency(174928000) == SI468X_ERROR_TIMEOUT, "no STC -> timeout");
    ASSERT_TRUE(chip.sent(SI468X_CMD_DAB_DIGRAD_STATUS).empty(), "no lock polling before STC");

    std::cout << "PASS: test_dab_tune_waits_stc" << std::endl;
    return true;
}

static void put_label(std::vector<uint8_t>& v, const char* l)
{
    for (int i = 0; i < 16; i++) {
        v.push_back(i < (int)std::strlen(l) ? (uint8_t)l[i] : ' ');
    }
}

static void put_service(std::vector<uint8_t>& v, uint32_t sid, uint8_t b4, const char* label,
                        std::initializer_list<std::pair<uint16_t, uint8_t>> comps)
{
    v.insert(v.end(), { (uint8_t)sid, (uint8_t)(sid >> 8), (uint8_t)(sid >> 16), (uint8_t)(sid >> 24) });
    v.push_back(b4);
    v.push_back((uint8_t)comps.size());
    v.push_back(0x00); // SICHARSET 0
    v.push_back(0xF0); // reserved; old code took charset from its high nibble (15)
    put_label(v, label);
    for (auto& c : comps) {
        v.insert(v.end(), { (uint8_t)c.first, (uint8_t)(c.first >> 8), c.second, 0x00 });
    }
}

// 3 services: DAB+ audio; MOT first then audio; data only
static std::vector<uint8_t> service_list_payload()
{
    std::vector<uint8_t> p = { 0x01, 0x00, 3, 0, 0, 0 };
    put_service(p, 0x43D1, 0x02, "RTS Premiere", { { 0x0007, 63 << 2 | 0x02 } });
    put_service(p, 0x43D3, 0x04, "Couleur 3", { { 0xC010, 60 << 2 }, { 0x0009, 63 << 2 } });
    put_service(p, 0xE1400103, 0x01, "SSG_SPI", { { 0xC010, 60 << 2 } });
    return p;
}

static bool test_dab_metadata()
{
    std::cout << "Running test: test_dab_metadata..." << std::endl;

    MockChip chip;
    char label[64];
    uint16_t eid = 0;

    // Ensemble: ECC 0xE1 must not leak into charset (old code: 0x0E -> raw 0x82 byte)
    std::vector<uint8_t> ens = reply({ 0x80, 0x80, 0x00, 0xC0, 0x43, 0xF0 });
    put_label(ens, "M\x82tropolitain 1");
    ens.insert(ens.end(), { 0xE1, 0x00, 0x00, 0xFF });
    chip.expect(SI468X_CMD_DAB_GET_ENSEMBLE_INFO, ens);
    ASSERT_TRUE(si468x_get_ensemble_info(label, &eid) == SI468X_SUCCESS, "ensemble info");
    ASSERT_TRUE(eid == 0xF043, "EId at resp[5..6]");
    ASSERT_TRUE(std::strcmp(label, "M\xC3\xA9tropolitain 1 ") == 0, "EBU Latin 0x82 -> UTF-8 e-acute");
    ASSERT_TRUE(chip.reads.back() == SI468X_REP_ENSEMBLE_INFO, "ensemble read 27");

    // Component: charset resp[8] (UCS-2), no SubChId, ERR -> NOT_FOUND
    std::vector<uint8_t> comp = reply({ 0x80, 0x80, 0x00, 0xC0, 0x00, 0x00, 0x05, 0x06,
                                        0x00, 0x41, 0x00, 0x62 });
    comp.resize(SI468X_REP_COMPONENT_INFO, 0x00);
    chip.expect(SI468X_CMD_DAB_GET_COMPONENT_INFO, comp);
    char short_label[17];
    uint8_t subch = 99;
    ASSERT_TRUE(si468x_get_component_info(0x43D1, 7, label, short_label, nullptr, &subch) == SI468X_SUCCESS, "component info");
    ASSERT_TRUE(std::strcmp(label, "Ab") == 0, "UCS-2 label via CHARSETID resp[8]");
    ASSERT_TRUE(subch == 0, "component info provides no SubChId");
    chip.expect(SI468X_CMD_DAB_GET_COMPONENT_INFO, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x01 }));
    ASSERT_TRUE(si468x_get_component_info(0x43D1, 0x63, label, short_label, nullptr, &subch) == SI468X_ERROR_NOT_FOUND,
                "component ERR -> NOT_FOUND");

    // Service info: ERR -> NOT_FOUND; charset masked
    si468x_service_info_t si;
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x01 }));
    ASSERT_TRUE(si468x_get_service_info(0xDEADBEEF, &si) == SI468X_ERROR_NOT_FOUND, "unknown SId -> NOT_FOUND");
    std::vector<uint8_t> sinfo = reply({ 0x80, 0x80, 0x00, 0xC0, 0x0A, 0x01, 0xF0, 0xE1 });
    put_label(sinfo, "RTS Premiere");
    sinfo.resize(SI468X_REP_SERVICE_INFO, 0x00);
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, sinfo);
    ASSERT_TRUE(si468x_get_service_info(0x43D1, &si) == SI468X_SUCCESS, "service info");
    ASSERT_TRUE(si.charset == 0 && si.pty == 5 && si.ecc == 0xE1, "charset & 0x0F, PTY, ECC");
    ASSERT_TRUE(si.char_mask == 0 && si.short_label[0] == '\0', "mask 0 -> empty short label");

    // CHAR_ABREV 0xE0C0 at resp[25..26]: chars 0,1,2,8,9 of "RTS Premiere" -> "RTSie"
    sinfo[25] = 0xC0;
    sinfo[26] = 0xE0;
    chip.expect(SI468X_CMD_DAB_GET_SERVICE_INFO, sinfo);
    ASSERT_TRUE(si468x_get_service_info(0x43D1, &si) == SI468X_SUCCESS && si.char_mask == 0xE0C0, "char_mask LE16");
    ASSERT_TRUE(std::strcmp(si.short_label, "RTSie") == 0, "short label from mask");

    // Service list: header, then one re-read of header + payload
    std::vector<uint8_t> pl = service_list_payload();
    std::vector<uint8_t> sl = reply({ 0x80, 0x80, 0x00, 0xC0, (uint8_t)pl.size(), (uint8_t)(pl.size() >> 8) });
    sl.insert(sl.end(), pl.begin(), pl.end());
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, sl);
    chip.writes.clear();
    si468x_service_t svc[8];
    int n = si468x_get_service_list(svc, 8);
    ASSERT_TRUE(n == 3, "3 services");
    ASSERT_TRUE(chip.sent(SI468X_CMD_GET_DIGITAL_SERVICE_LIST).size() == 1, "list queried once");
    ASSERT_TRUE(chip.reads.back() == sl.size(), "full read = 7 + SIZE");
    ASSERT_TRUE(svc[0].service_id == 0x43D1 && svc[0].component_id == 7 && svc[0].subchannel_id == 7
                && svc[0].tmid == 0 && svc[0].audio_type == SI468X_AUDIO_TYPE_DAB_PLUS && svc[0].pty == 1
                && svc[0].charset == 0 && !svc[0].is_data_service, "service a");
    ASSERT_TRUE(std::strcmp(svc[0].label, "RTS Premiere    ") == 0 && std::strcmp(svc[0].short_label, "RTS Prem") == 0, "labels a");
    ASSERT_TRUE(svc[1].component_id == 0x0009 && svc[1].num_components == 2 && !svc[1].is_data_service, "b: audio picked over MOT");
    ASSERT_TRUE(svc[2].service_id == 0xE1400103 && svc[2].is_data_service && svc[2].tmid == 3
                && svc[2].component_id == 0xC010 && svc[2].subchannel_id == 0 && svc[2].audio_type == 60, "c: data only");

    // Truncated inside service c: 2 entries, never read past len (ASan)
    std::vector<uint8_t> cut(sl.begin(), sl.end() - 10);
    std::vector<uint8_t> heap(cut); // exact-size heap copy so ASan sees overreads
    ASSERT_TRUE(si468x_parse_service_list(heap.data(), heap.size(), svc, 8) == 2, "truncated list keeps complete services");
    ASSERT_TRUE(si468x_parse_service_list(heap.data(), 20, svc, 8) == SI468X_ERROR_INVALID_REPLY, "nothing complete -> INVALID_REPLY");

    // ERR / SIZE 0 = list not ready
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_LIST, reply({ 0xC0, 0x80, 0x00, 0xC0, 0x00, 0x00 }));
    ASSERT_TRUE(si468x_get_service_list(svc, 8) == 0, "list not ready -> 0");

    std::cout << "PASS: test_dab_metadata" << std::endl;
    return true;
}

static bool test_signal_status()
{
    std::cout << "Running test: test_signal_status..." << std::endl;

    si468x_signal_status_t st;
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_ERROR_SPI, "closed bus -> SPI error, no fake success");

    MockChip chip;
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_SUCCESS, "synced");
    ASSERT_TRUE(st.rssi == 48 && st.snr == 2 && st.cnr == 24 && st.fic_quality == 100, "RSSI/SNR/CNR/FIC");
    ASSERT_TRUE(st.fib_errors == 0 && st.tune_index == 30 && st.freq_offset == 60 && st.antcap == 19, "FIB/index/FFT/antcap");
    ASSERT_TRUE(st.sync_status == 1, "VALID && ACQ -> synced");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_ACQUIRING);
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_SUCCESS && st.snr == 0 && st.sync_status == 1, "SNR -21 clamped to 0");

    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_NOSIG);
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_SUCCESS && st.sync_status == 0 && st.rssi == 15, "no signal");

    // ACQINT/RSSIHINT interrupt bits alone (old rule: resp[5] & 0x06) are not a lock
    std::vector<uint8_t> intr = FX_DIGRAD_NOSIG;
    intr[5] = 0x06;
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, intr);
    ASSERT_TRUE(si468x_get_signal_status(&st) == SI468X_SUCCESS && st.sync_status == 0, "interrupt bits are not sync");

    std::cout << "PASS: test_signal_status" << std::endl;
    return true;
}

static std::vector<uint8_t> rds_group(uint8_t fifo_used, uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t ble = 0)
{
    return reply({ 0x80, 0x80, 0x00, 0xC0, 0x02, 0x02, 0x00, 0x00, (uint8_t)a, (uint8_t)(a >> 8), fifo_used, ble,
                   (uint8_t)a, (uint8_t)(a >> 8), (uint8_t)b, (uint8_t)(b >> 8),
                   (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)d, (uint8_t)(d >> 8) });
}

static bool test_time_audio_fm_rds()
{
    std::cout << "Running test: test_time_audio_fm_rds..." << std::endl;

    MockChip chip;

    // DAB_GET_TIME: 2026-10-29 15:25:42 at reply 0x04..0x0A
    si468x_time_t t;
    chip.expect(SI468X_CMD_DAB_GET_TIME, reply({ 0x80, 0x80, 0x00, 0xC0, 0xEA, 0x07, 0x0A, 0x1D, 0x0F, 0x19, 0x2A }));
    ASSERT_TRUE(si468x_get_time(&t) == SI468X_SUCCESS, "time");
    ASSERT_TRUE(t.year == 2026 && t.month == 10 && t.day == 29 && t.hours == 15 && t.minutes == 25 && t.seconds == 42, "time fields");
    ASSERT_TRUE(chip.reads.back() == SI468X_REP_TIME, "time read 12");
    chip.expect(SI468X_CMD_DAB_GET_TIME, reply({ 0x80, 0x80, 0x00, 0xC0, 0, 0, 0, 0, 0, 0, 0 }));
    ASSERT_TRUE(si468x_get_time(&t) == SI468X_ERROR_NOT_FOUND, "year 0 -> NOT_FOUND");

    // DAB_GET_AUDIO_INFO: 64 kbps, 48000 Hz, SBR=1 PS=0 mode=2, DRC 3
    si468x_audio_info_t ai;
    chip.expect(SI468X_CMD_DAB_GET_AUDIO_INFO, reply({ 0x80, 0x80, 0x00, 0xC0, 64, 0, 0x80, 0xBB, 0x06, 0x03 }));
    ASSERT_TRUE(si468x_get_audio_info(&ai) == SI468X_SUCCESS, "audio info");
    ASSERT_TRUE(ai.bitrate == 64 && ai.sample_rate == 48000 && ai.audio_mode == 2 && ai.sbr == 1 && ai.ps == 0
                && ai.drc_gain == 3, "audio info fields");

    // FM_RSQ_STATUS: 101.7 MHz, FREQOFF -2, RSSI 40, SNR 25, MULT 3, VALID; RDS FIFO empty
    si468x_fm_status_t fm;
    std::vector<uint8_t> rsq = reply({ 0x80, 0x80, 0x00, 0xC0, 0x1F, 0x81, 0xBA, 0x27, 0xFE, 40, 25, 3 });
    rsq.resize(SI468X_REP_FM_RSQ_STATUS, 0x00);
    chip.expect(SI468X_CMD_FM_RSQ_STATUS, rsq);
    chip.expect(SI468X_CMD_FM_RDS_STATUS, rds_group(0, 0, 0, 0, 0));
    ASSERT_TRUE(si468x_get_fm_status(&fm) == SI468X_SUCCESS, "fm status");
    ASSERT_TRUE(fm.frequency_hz == 101700000 && fm.freq_offset == -2 && fm.rssi == 40 && fm.snr == 25
                && fm.multipath == 3 && fm.valid == 1 && fm.hd_synced == 0, "RSQ fields; interrupt bits ignored");
    ASSERT_TRUE(fm.rds_synced == 1, "rds_synced from RDSSYNC of FM_RDS_STATUS");
    ASSERT_TRUE(chip.sent(SI468X_CMD_FM_RSQ_STATUS).back()[1] == 0x00, "RSQ without STCACK");

    // RDS: PS "RADIO 1 " over four 0A groups, then empty FIFO (stale blocks must not be decoded)
    char ps[16];
    const uint16_t seg[4] = { 0x5241, 0x4449, 0x4F20, 0x3120 }; // "RA" "DI" "O " "1 "
    for (int i = 0; i < 4; i++) {
        chip.expect(SI468X_CMD_FM_RDS_STATUS, rds_group(4 - i, 0x4321, 0x0000 | i, 0, seg[i]));
    }
    ASSERT_TRUE(si468x_get_rds_station_name(ps, sizeof(ps)) == 1 && std::strcmp(ps, "RADIO 1") == 0, "PS assembled");
    ASSERT_TRUE(chip.sent(SI468X_CMD_FM_RDS_STATUS).back()[1] == 0x01, "RDS INTACK dequeues");
    chip.expect(SI468X_CMD_FM_RDS_STATUS, rds_group(0, 0x4321, 0x0000, 0, 0x5858)); // stale "XX"
    ASSERT_TRUE(si468x_get_rds_station_name(ps, sizeof(ps)) == 0 && std::strcmp(ps, "RADIO 1") == 0, "empty FIFO ignored");

    // RT 2A segment 0 "Hell" + segment 1 "o\r"
    char rt[80];
    chip.expect(SI468X_CMD_FM_RDS_STATUS, rds_group(2, 0x4321, 0x2000, 0x4865, 0x6C6C));
    chip.expect(SI468X_CMD_FM_RDS_STATUS, rds_group(1, 0x4321, 0x2001, 0x6F0D, 0x2020));
    ASSERT_TRUE(si468x_get_rds_text(rt, sizeof(rt)) == 1 && std::strcmp(rt, "Hello") == 0, "RadioText assembled");

    std::cout << "PASS: test_time_audio_fm_rds" << std::endl;
    return true;
}

static bool test_dab_aux()
{
    std::cout << "Running test: test_dab_aux..." << std::endl;

    MockChip chip;
    const std::vector<uint8_t> ok = { 0x80, 0x80, 0x00, 0xC0 }; // status bytes 0..3

    auto rep = [&](std::initializer_list<uint8_t> data) {
        std::vector<uint8_t> r = reply({});
        r.insert(r.end(), ok.begin(), ok.end());
        r.insert(r.end(), data);
        return r;
    };

    // Event status: flags 0x95, active 0x0C, version 0x1234
    si468x_dab_event_status_t ev;
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, rep({ 0x95, 0x0C, 0x34, 0x12 }));
    ASSERT_TRUE(si468x_dab_get_event_status(&ev) == SI468X_SUCCESS, "event status");
    ASSERT_TRUE(ev.recfg_int && !ev.recfg_warn_int && ev.anno_int && !ev.oeserv_int && ev.servlink_int
                && !ev.freqinfo_int && ev.svrlist_int, "interrupt flags");
    ASSERT_TRUE(!ev.anno_active && ev.oeserv_active && ev.servlink_active && !ev.svrlist_active, "active flags");
    ASSERT_TRUE(ev.svrlist_ver == 0x1234 && chip.reads.back() == 9, "version, read 9");
    ASSERT_TRUE(chip.sent(SI468X_CMD_DAB_GET_EVENT_STATUS).back()[1] == 0x01, "EVENT_ACK");

    // Deprecated wrapper uses 0xB3, never 0x12 (GET_FUNC_INFO)
    si468x_event_status_t old;
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, rep({ 0x40, 0x00, 0x00, 0x00 }));
    ASSERT_TRUE(si468x_get_event_status(&old) == SI468X_SUCCESS && old.reconf == 1 && old.annc == 0, "wrapper");
    ASSERT_TRUE(chip.sent(0x12).empty(), "no GET_FUNC_INFO as event status");

    // Subchannel: DAB+ (4), EEP-A3 (8), 72 kbps, 54 CU at 300
    si468x_subchan_info_t sc;
    chip.expect(SI468X_CMD_DAB_GET_SUBCHAN_INFO, rep({ 4, 8, 72, 0, 54, 0, 0x2C, 0x01 }));
    ASSERT_TRUE(si468x_dab_get_subchan_info(0x43D1, 7, &sc) == SI468X_SUCCESS, "subchan");
    ASSERT_TRUE(sc.service_mode == 4 && sc.protection_info == 8 && sc.bitrate == 72 && sc.num_cu == 54
                && sc.cu_address == 300, "subchan fields");

    // Freq info: 2 elements, header then one re-read
    si468x_freq_element_t fe[4];
    chip.expect(SI468X_CMD_DAB_GET_FREQ_INFO, rep({ 2, 0, 0, 0,
                                                   0x41, 0x40, 0, 0, 0xC0, 0x6A, 0x03, 0, 30, 1, 2, 3,
                                                   0x42, 0x40, 0, 0, 0x50, 0xAB, 0x02, 0, 0, 4, 5, 6 }));
    size_t nreads = chip.reads.size();
    ASSERT_TRUE(si468x_dab_get_freq_info(fe, 4) == 2, "2 elements");
    ASSERT_TRUE(chip.reads.back() == 9 + 24 && chip.reads.size() > nreads, "re-read 9 + 12*N");
    ASSERT_TRUE(fe[0].id == 0x4041 && fe[0].frequency_hz == 223936000 && fe[0].freq_index == 30
                && fe[0].rnm == 1 && fe[0].continuity == 2 && fe[0].control == 3, "element 0");
    ASSERT_TRUE(fe[1].id == 0x4042 && fe[1].frequency_hz == 174928000 && fe[1].control == 6, "element 1");
    ASSERT_TRUE(si468x_dab_get_freq_info(fe, 1) <= 1, "clamped to max_elements");

    chip.expect(SI468X_CMD_DAB_GET_FREQ_INFO, rep({ 0xFF, 0xFF, 0xFF, 0x7F }));
    ASSERT_TRUE(si468x_dab_get_freq_info(fe, 4) == SI468X_ERROR_INVALID_REPLY, "absurd count rejected");

    // Other ensembles: 2 EIDs
    uint16_t eids[4];
    chip.expect(SI468X_CMD_DAB_GET_OE_SERVICES_INFO, rep({ 4, 0, 2, 0, 0x41, 0x40, 0x42, 0x40 }));
    ASSERT_TRUE(si468x_dab_get_other_ensemble_info(0x43D1, eids, 4) == 2 && eids[0] == 0x4041 && eids[1] == 0x4042, "OE EIDs");

    // Service linking: SIZE 10 = one linkset with one link
    uint8_t link[32];
    chip.expect(0xB7, rep({ 10, 0, 1, 0, 0x01, 0x00, 0xC0, 1, 0xD1, 0x43, 0, 0, 0, 0 }));
    ASSERT_TRUE(si468x_dab_get_service_linking(0x43D1, link, sizeof(link)) == 12, "NUM_LINKSETS + rsvd + SIZE");
    ASSERT_TRUE(link[0] == 1 && link[2] == 0x01 && link[4] == 0xC0 && link[5] == 1 && link[6] == 0xD1, "linkset bytes");
    ASSERT_TRUE(si468x_dab_get_service_linking(0x43D1, link, 3) <= 3, "clamped to max_len");

    // Announcements
    uint16_t asu = 0, asw = 0;
    uint32_t a_sid = 0, a_cid = 0;
    chip.expect(0xB5, rep({ 0, 0, 0x22, 0x11 }));
    ASSERT_TRUE(si468x_dab_get_announcement_support(0x43D1, 0, &asu) == SI468X_SUCCESS && asu == 0x1122, "ASU resp[7..8]");
    chip.expect(0xB6, rep({ 0x01, 0x03, 0x07, 0x0A, 0x02, 0x00, 0xD1, 0x43, 0x07, 0x00, 1, 2 }));
    ASSERT_TRUE(si468x_dab_get_announcement_info(0, &a_sid, &a_cid, &asw) == SI468X_SUCCESS, "anno info");
    ASSERT_TRUE(asw == 0x0002 && a_sid == 0x43D1 && a_cid == 7, "ASW/ID1/ID2");

    // BER
    uint32_t eb = 0, tb = 0;
    chip.expect(SI468X_CMD_DAB_TEST_GET_BER_INFO, rep({ 5, 0, 0, 0, 0x40, 0x42, 0x0F, 0 }));
    ASSERT_TRUE(si468x_dab_get_ber_info(1, &eb, &tb) == SI468X_SUCCESS && eb == 5 && tb == 1000000, "ERR_BITS/TOTAL_BITS");
    ASSERT_TRUE(chip.sent(SI468X_CMD_DAB_TEST_GET_BER_INFO).back()[1] == 1, "clear flag");

    // AGC (Linux layout +1)
    si468x_agc_status_t agc;
    std::vector<uint8_t> ar = rep({});
    for (int i = 5; i < 24; i++) {
        ar.push_back((uint8_t)i);
    }
    chip.expect(SI468X_CMD_GET_AGC_STATUS, ar);
    ASSERT_TRUE(si468x_get_agc_status(&agc) == SI468X_SUCCESS && agc.vhflna == 15 && agc.lmhfrtankhi == 20
                && agc.rfindex == 22, "AGC index sweep");

    // HD (unit tests only): station info and alert re-read 7 + LENGTH
    char txt[16];
    chip.expect(SI468X_CMD_HD_GET_STATION_INFO, rep({ 5, 0, 'H', 'e', 'l', 'l', 'o' }));
    ASSERT_TRUE(si468x_fmhd_get_station_info(1, txt, sizeof(txt)) == 1 && std::strcmp(txt, "Hello") == 0, "SIS");
    chip.expect(SI468X_CMD_HD_GET_ALERT_MSG, rep({ 3, 0, 'A', 'B', 'C' }));
    ASSERT_TRUE(si468x_fmhd_get_alert_message(txt, 3) == 1 && std::strcmp(txt, "AB") == 0, "alert clamped");
    chip.expect(SI468X_CMD_HD_GET_PSD_DECODE, rep({ 0, 0, 0, 4, 'S', 'o', 'n', 'g' }));
    ASSERT_TRUE(si468x_fmhd_get_psd_text(0, 0, txt, sizeof(txt)) == 1 && std::strcmp(txt, "Song") == 0, "PSD");
    std::vector<uint8_t> hb = rep({ 9, 0, 0, 0 });
    hb.resize(SI468X_REP_HD_BER, 0);
    chip.expect(SI468X_CMD_HD_TEST_GET_BER_INFO, hb);
    ASSERT_TRUE(si468x_fmhd_get_ber_info(0, &eb) == SI468X_SUCCESS && eb == 9 && chip.reads.back() == 45, "HD BER");

    std::cout << "PASS: test_dab_aux" << std::endl;
    return true;
}

static std::vector<uint8_t> prop_frame(uint16_t id, uint16_t v)
{
    return { 0x13, 0x00, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)v, (uint8_t)(v >> 8) };
}

static bool test_property_values()
{
    std::cout << "Running test: test_property_values..." << std::endl;

    MockChip chip;
    ASSERT_TRUE(si468x_set_rds_region(SI468X_REGION_EUROPE) == SI468X_SUCCESS, "region EU");
    ASSERT_TRUE(chip.writes.back() == prop_frame(0x3900, 1), "50 us = 1 (old: 0x3900 -> 75 us)");
    ASSERT_TRUE(si468x_set_rds_region(SI468X_REGION_US) == SI468X_SUCCESS, "region US");
    ASSERT_TRUE(chip.writes.back() == prop_frame(0x3900, 0), "75 us = 0");

    chip.writes.clear();
    ASSERT_TRUE(si468x_set_audio_output(SI468X_AUDIO_ANALOG) == SI468X_SUCCESS, "analog");
    ASSERT_TRUE(chip.writes.size() == 1 && chip.writes[0] == prop_frame(0x0800, 0x8001), "analog: one frame, INTBOUTEN kept");

    chip.writes.clear();
    ASSERT_TRUE(si468x_set_audio_output(SI468X_AUDIO_I2S) == SI468X_SUCCESS, "i2s");
    ASSERT_TRUE(chip.writes.size() == 3 && chip.writes[0] == prop_frame(0x0800, 0x8002)
                && chip.writes[1] == prop_frame(0x0200, 0x8000) && chip.writes[2] == prop_frame(0x0202, 0x1000), "i2s frames");

    chip.writes.clear();
    ASSERT_TRUE(si468x_set_audio_output(SI468X_AUDIO_SIMUL) == SI468X_SUCCESS, "simul");
    ASSERT_TRUE(chip.writes.size() == 3 && chip.writes[0] == prop_frame(0x0800, 0x8003), "simul pins");

    si468x_set_audio_output(SI468X_AUDIO_ANALOG); // restore default for later tests
    std::cout << "PASS: test_property_values" << std::endl;
    return true;
}

static bool test_fm_tune()
{
    std::cout << "Running test: test_fm_tune..." << std::endl;

    MockChip chip;
    chip.expect(SI468X_CMD_FM_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 })); // STCINT set
    ASSERT_TRUE(si468x_tune_fm(101700) == SI468X_SUCCESS, "FM tune");
    const std::vector<uint8_t> want = { 0x30, 0x00, 0xBA, 0x27, 0x01, 0x00, 0x00 };
    ASSERT_TRUE(chip.sent(SI468X_CMD_FM_TUNE_FREQ).back() == want, "101.7 MHz frame, ANTCAP 1");
    ASSERT_TRUE(chip.writes.back() == std::vector<uint8_t>({ 0x32, 0x01 }), "STCACK after STC");

    chip.expect(SI468X_CMD_FM_TUNE_FREQ, reply({ 0x80, 0x80, 0x00, 0xC0 })); // no STC
    ASSERT_TRUE(si468x_tune_fm(101700) == SI468X_ERROR_TIMEOUT, "no STC -> timeout");

    std::cout << "PASS: test_fm_tune" << std::endl;
    return true;
}

static std::vector<uint8_t> dsd(uint8_t src, uint8_t dscty, uint8_t buff, const std::vector<uint8_t>& payload)
{
    uint16_t n = payload.size();
    std::vector<uint8_t> r = reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, buff, 0x03, (uint8_t)(src << 6 | dscty),
                                     0xD1, 0x43, 0, 0, 7, 0, 0, 0, 0, 0, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, 0 });
    r.insert(r.end(), payload.begin(), payload.end());
    return r;
}

static bool test_dls_mot()
{
    std::cout << "Running test: test_dls_mot..." << std::endl;

    MockChip chip;
    char dls[129];

    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, FX_DSD_DLS_TEXT);
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 1 && std::strcmp(dls, "Juste mesure") == 0, "captured DLS");
    ASSERT_TRUE(std::find(chip.reads.begin(), chip.reads.end(), FX_DSD_DLS_TEXT.size()) != chip.reads.end(),
                "re-read 25 + BYTE_COUNT");
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 0 && std::strcmp(dls, "Juste mesure") == 0, "unchanged -> 0, text kept");

    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, FX_DSD_DLS_DLPLUS);
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 0 && std::strcmp(dls, "Juste mesure") == 0, "DL Plus ignored");

    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, FX_DSD_EMPTY);
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 0, "empty queue (ERR) is no data, not an error");

    // DLS charset 4 -> UCS-2: "Été"
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(2, 0, 0, { 0x00, 0x40, 0x00, 0xC9, 0x00, 0x74, 0x00, 0xE9 }));
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 1 && std::strcmp(dls, "\xC3\x89t\xC3\xA9") == 0, "charset 4 = UCS-2");
    ASSERT_TRUE(si468x_get_dls_text(dls, 3) == 0 && std::strcmp(dls, "\xC3\x89") == 0, "truncated on char boundary");

    // Clear command
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(2, 0, 0, { 0x11, 0x00 }));
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 1 && dls[0] == '\0', "DLS clear");

    // Mixed queue: MOT header, MOT body, DLS. Reading DLS must not lose the MOT packets.
    const std::vector<uint8_t> mot_hdr = { 0, 0, 0x80, 0x00, 0x12, 0x12, 0x34, 0, 0, 0, 0x00, 0x00, 0x40, 0, 0 };
    const std::vector<uint8_t> mot_body = { 0, 0, 0x00, 0x00, 0x12, 0x12, 0x34, 0, 0, 0xFF, 0xD8, 0xFF, 0xD9, 0, 0 };
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(1, 60, 2, mot_hdr));
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(1, 60, 1, mot_body));
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(2, 0, 0, { 0x00, 0x00, 'H', 'i' }));
    ASSERT_TRUE(si468x_get_dls_text(dls, sizeof(dls)) == 1 && std::strcmp(dls, "Hi") == 0, "DLS behind MOT packets");
    si468x_mot_slideshow_t sl;
    ASSERT_TRUE(si468x_get_mot_slideshow(&sl) == 1 && sl.is_new == 1, "MOT assembled by the DLS call's pump");
    ASSERT_TRUE(sl.image_size == 4 && sl.transport_id == 0x1234 && sl.image_data[0] == 0xFF && sl.image_data[1] == 0xD8, "JPEG bytes");
    ASSERT_TRUE(si468x_get_mot_slideshow(&sl) == 0 && sl.is_new == 0, "reported once");

    // Short MOT packet (byte_count < 11): no underflow, ignored
    chip.expect(SI468X_CMD_GET_DIGITAL_SERVICE_DATA, dsd(1, 60, 0, { 0, 0, 0x00, 0x01, 0x12, 0x12, 0x34, 0, 0, 0 }));
    ASSERT_TRUE(si468x_get_mot_slideshow(&sl) == 0, "short segment ignored");

    std::cout << "PASS: test_dls_mot" << std::endl;
    return true;
}

static bool test_calibrate_antenna()
{
    std::cout << "Running test: test_calibrate_antenna..." << std::endl;

    MockChip chip;
    // Previously tuned to 12A
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));
    ASSERT_TRUE(si468x_set_frequency(223936000) == SI468X_SUCCESS, "initial tune");

    // Sweep on 5A: RSSI 20 everywhere, 60 at ANTCAP 37
    for (int antcap = 1; antcap <= 128; antcap++) {
        chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
        std::vector<uint8_t> d = FX_DIGRAD_NOSIG;
        d[7] = (antcap == 37) ? 60 : 20;
        for (int i = 0; i < 5; i++) {
            chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, d);
        }
    }
    // Restore tune of 12A
    chip.expect(SI468X_CMD_DAB_TUNE_FREQ, reply({ 0x81, 0x80, 0x00, 0xC0 }));
    chip.expect(SI468X_CMD_DAB_DIGRAD_STATUS, FX_DIGRAD_SYNCED);
    chip.expect(SI468X_CMD_DAB_GET_EVENT_STATUS, reply({ 0x81, 0x80, 0x00, 0xC0, 0x01, 0x01, 0x27, 0x00 }));

    uint16_t peak = 0;
    ASSERT_TRUE(si468x_dab_calibrate_antenna(174928000, &peak) == SI468X_SUCCESS, "calibrate");
    ASSERT_TRUE(peak == 37, "peak ANTCAP");
    auto tunes = chip.sent(SI468X_CMD_DAB_TUNE_FREQ);
    ASSERT_TRUE(tunes[37][2] == 0 && tunes[37][4] == 37, "sweep frame carries ANTCAP LE16");
    ASSERT_TRUE(tunes.back()[2] == 30 && tunes.back()[4] == 0 && tunes.back()[5] == 0, "re-tune 12A with ANTCAP 0");
    ASSERT_TRUE(si468x_get_frequency() == 223936000, "active frequency restored");

    std::cout << "PASS: test_calibrate_antenna" << std::endl;
    return true;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Starting libsi468x Test Suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    bool success = true;

    success &= test_short_label_decoding();
    success &= test_initialization_failure();
    success &= test_api_signatures_uninitialized();
    success &= test_new_fm_rds_api_uninitialized();
    success &= test_audio_output_modes();
    success &= test_telemetry_mappings();
    success &= test_mot_slideshow_uninitialized();
    success &= test_new_seek_and_telemetry_uninitialized();
    success &= test_error_definitions();
    success &= test_framing_cts_poll();
    success &= test_send_command_err_timeout();
    success &= test_stop_service_ids();
    success &= test_dab_tune_waits_stc();
    success &= test_dab_metadata();
    success &= test_signal_status();
    success &= test_time_audio_fm_rds();
    success &= test_dab_aux();
    success &= test_property_values();
    success &= test_fm_tune();
    success &= test_dls_mot();
    success &= test_calibrate_antenna();

    std::cout << "========================================" << std::endl;
    if (success) {
        std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0; // Return success to Automake test harness
    }
    else {
        std::cout << "SOME TESTS FAILED!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 1; // Return failure to Automake test harness
    }
}
