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

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Starting libsi468x Test Suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    bool success = true;

    success &= test_short_label_decoding();
    success &= test_initialization_failure();

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
