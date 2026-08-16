/*
 *    Copyright (C) 2026
 *    test_si468x.cpp - Test Suite for libsi468x Driver Library
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include "si468x.h"
#include "si468x_internal.h"

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

    char short_label[10];

    // Test Case 1: Standard 7-character mask (0xFE00)
    std::memset(short_label, 0, sizeof(short_label));
    si468x_decode_short_label("Station 1", 0xFE00, short_label);
    ASSERT_TRUE(std::strcmp(short_label, "Station") == 0, "0xFE00 on 'Station 1' must yield 'Station'");

    // Test Case 2: Discontiguous characters (0xF080 -> index 0,1,2,3 and index 8)
    std::memset(short_label, 0, sizeof(short_label));
    si468x_decode_short_label("Station 2", 0xF080, short_label);
    ASSERT_TRUE(std::strcmp(short_label, "Stat2") == 0, "0xF080 on 'Station 2' must yield 'Stat2'");

    // Test Case 3: Boundary case - Full mask (0xFFFF). Must truncate to max 8 characters.
    std::memset(short_label, 0, sizeof(short_label));
    si468x_decode_short_label("1234567890abcdef", 0xFFFF, short_label);
    ASSERT_TRUE(std::strcmp(short_label, "12345678") == 0, "0xFFFF must truncate long string to 8 chars");

    // Test Case 4: Boundary case - Empty mask (0x0000)
    std::memset(short_label, 0, sizeof(short_label));
    si468x_decode_short_label("Station 4", 0x0000, short_label);
    ASSERT_TRUE(std::strcmp(short_label, "") == 0, "0x0000 mask must yield empty string");

    // Test Case 5: Complex custom mask (0xAAAA -> binary 1010101010101010)
    // Matches indices: 0, 2, 4, 6, 8, 10, 12, 14
    // String: "a b c d e f g h" (spaces are at odd indices)
    std::memset(short_label, 0, sizeof(short_label));
    si468x_decode_short_label("abcdefghijklmnop", 0xAAAA, short_label);
    ASSERT_TRUE(std::strcmp(short_label, "acegikmo") == 0, "0xAAAA mask must extract alternating characters");

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
    ASSERT_TRUE(ret_dls == -2, "si468x_get_dls_text must fail safely when uninitialized");

    // 3. Verify si468x_get_time signature and uninitialized failure
    si468x_time_t dtime;
    int ret_time = si468x_get_time(&dtime);
    ASSERT_TRUE(ret_time == -1, "si468x_get_time must fail safely when uninitialized");

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
    ASSERT_TRUE(ret_freq_table_invalid == -1, "si468x_set_frequency_table must fail on null pointer");

    int ret_freq_table_uninit = si468x_set_frequency_table(freqs, 3);
    ASSERT_TRUE(ret_freq_table_uninit == -1, "si468x_set_frequency_table must fail when uninitialized");

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

    std::cout << "PASS: test_new_fm_rds_api_uninitialized" << std::endl;
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
