/*
 *    Copyright (C) 2026
 *    si468x.cpp - Hardware Driver Core Implementation for libsi468x
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_WIRINGPI
#include <wiringPi.h>
#include <wiringPiSPI.h>
#endif

#include "si468x.h"
#include "si468x_internal.h"
#include "si468x_firmware.h"

static int spi_fd = -1;
static int rst_gpio_pin = -1;
static uint32_t active_frequency = 0;

/* Low-level GPIO RSTB management */
static bool gpio_init(int pin)
{
    if (pin < 0) {
        std::cerr << "libsi468x: Invalid GPIO pin: " << pin << std::endl;
        return false;
    }
    rst_gpio_pin = pin;

#ifdef HAVE_WIRINGPI
    // Initialize wiringPi using standard Broadcom BCM GPIO numbering
    if (wiringPiSetupGpio() < 0) {
        std::cerr << "libsi468x: Failed to initialize wiringPi!" << std::endl;
        return false;
    }
    pinMode(pin, OUTPUT);

    // Configure BCM GPIO 7 (CE1) as an input with PUD_UP (pull-up)
    // to hold secondary chip select high, resolving bus contention
    pinMode(7, INPUT);
    pullUpDnControl(7, PUD_UP);
    return true;
#else
    std::clog << "libsi468x: Compiling in mock GPIO mode (wiringPi absent)." << std::endl;
    return true;
#endif
}

static void gpio_set_rst(bool high)
{
    if (rst_gpio_pin < 0) {
        return;
    }

#ifdef HAVE_WIRINGPI
    digitalWrite(rst_gpio_pin, high ? HIGH : LOW);
#endif
}

static void gpio_shutdown()
{
    if (rst_gpio_pin >= 0) {
        gpio_set_rst(false); // Hold in reset
        rst_gpio_pin = -1;
    }
}

/* Low-level SPI transfer helper */
static int spi_transfer(const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (spi_fd < 0) {
        return -1;
    }

#ifdef HAVE_WIRINGPI
    std::vector<uint8_t> buf(length);
    if (tx) {
        std::memcpy(buf.data(), tx, length);
    }
    else {
        std::memset(buf.data(), 0, length);
    }

    if (wiringPiSPIDataRW(0, buf.data(), length) < 0) {
        return -1;
    }

    if (rx) {
        std::memcpy(rx, buf.data(), length);
    }
    return 0;
#else
    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = length;
    tr.speed_hz = SI468X_SPI_SPEED_HZ;
    tr.bits_per_word = SI468X_SPI_BITS_PER_WORD;

    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1) {
        return -1;
    }
    return 0;
#endif
}

/*
 * Transmit a command packet and wait for response.
 * Uses reference polling loop to check CTS via 7-byte READ_RESP queries.
 */
static int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms = 1000)
{
    // 1. Write command over SPI
    if (spi_transfer(cmd, nullptr, cmd_len) < 0) {
        return SI468X_ERROR_SPI;
    }

    // 2. Poll CTS using 7-byte READ_RESP (0x00) transfers in a loop
    bool cts_high = false;
    auto start_time = std::chrono::steady_clock::now();

    for (int retry = 0; retry < 50; retry++) {
        uint8_t poll_tx[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        uint8_t poll_rx[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

        if (spi_transfer(poll_tx, poll_rx, 7) == 0) {
            // poll_rx[1] is the co-processor's status byte; Bit 7 (0x80) is the CTS flag
            if (poll_rx[1] & 0x80) {
                // Check for Command Error (ERR bit 6)
                if (poll_rx[1] & 0x40) {
                    std::cerr << "libsi468x: WARNING: Command Error (Status: 0x"
                              << std::hex << (int)poll_rx[1] << std::dec << ")" << std::endl;
                }
                cts_high = true;
                break;
            }
        }

        // Enforce maximum execution timeout window
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time
                       ).count();
        if (elapsed >= timeout_ms) {
            break;
        }

        // Progressive delay: ((retry * 5) + 1) * 50 microseconds
        int delay_us = ((retry * 5) + 1) * 50;
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }

    if (!cts_high) {
        return SI468X_ERROR_TIMEOUT;
    }

    // 3. Read the response payload if requested
    if (resp && resp_len > 0) {
        std::vector<uint8_t> tx_dummy(resp_len, 0x00);
        std::memset(resp, 0, resp_len);
        if (spi_transfer(tx_dummy.data(), resp, resp_len) < 0) {
            return SI468X_ERROR_SPI;
        }
    }

    return SI468X_SUCCESS;
}

/* Streams a firmware image from RAM arrays using WRITE_FUT packets */
static int upload_firmware_memory(const unsigned char* data, size_t length)
{
    // Send LOAD_INIT (must be 2 bytes: 0x06, 0x00)
    uint8_t load_init_cmd[2] = { SI468X_CMD_LOAD_INIT, 0x00 };
    if (send_command(load_init_cmd, 2, nullptr, 0) != SI468X_SUCCESS) {
        return SI468X_ERROR_FIRMWARE;
    }

    const size_t chunk_size = 512;
    std::vector<uint8_t> packet(chunk_size + 4); // 4 header bytes + chunk

    size_t remaining = length;
    const unsigned char* ptr = data;

    while (remaining > 0) {
        size_t bytes_to_write = (remaining < chunk_size) ? remaining : chunk_size;

        // Build WRITE_FUT command packet (header must be 0x04, 0x00, 0x00, 0x00)
        packet[0] = SI468X_CMD_WRITE_FUT;
        packet[1] = 0x00;
        packet[2] = 0x00;
        packet[3] = 0x00;
        std::memcpy(&packet[4], ptr, bytes_to_write);

        // Transmit packet and read the 7-byte response to unblock the bootloader
        uint8_t fut_resp[7];
        if (send_command(packet.data(), bytes_to_write + 4, fut_resp, 7) != SI468X_SUCCESS) {
            return SI468X_ERROR_FIRMWARE;
        }

        ptr += bytes_to_write;
        remaining -= bytes_to_write;
    }
    return SI468X_SUCCESS;
}

/* Public C-API Implementation */

int si468x_init(const char* spi_device, int rst_pin, int boot_mode)
{
    std::clog << "libsi468x: Initializing driver library..." << std::endl;

    // 1. Initialize GPIO
    if (!gpio_init(rst_pin)) {
        return SI468X_ERROR_GPIO;
    }

    // 2. Hard Reset Chip
    gpio_set_rst(false); // Reset low
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Allow full voltage discharge
    gpio_set_rst(true);  // Reset high
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Allow bootloader to stabilize

    // 3. Open SPI Bus
#ifdef HAVE_WIRINGPI
    spi_fd = wiringPiSPISetup(0, 32000000); // 32 MHz (matches radio_cli exactly)
    if (spi_fd < 0) {
        std::cerr << "libsi468x: Error opening SPI bus via wiringPi!" << std::endl;
        gpio_shutdown();
        return SI468X_ERROR_SPI;
    }
#else
    spi_fd = open(spi_device, O_RDWR);
    if (spi_fd < 0) {
        std::cerr << "libsi468x: Error opening SPI device: " << spi_device << std::endl;
        gpio_shutdown();
        return SI468X_ERROR_SPI;
    }

    // Configure SPI Mode, speed, bits
    uint8_t mode = SI468X_SPI_MODE;
    uint8_t bits = SI468X_SPI_BITS_PER_WORD;
    uint32_t speed = SI468X_SPI_SPEED_HZ;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
            ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        std::cerr << "libsi468x: Error configuring SPI bus parameters!" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_SPI;
    }
#endif

    // 4. Send POWER_UP (0x01) with reference crystal and clock dividers
    uint8_t power_up_cmd[5] = { SI468X_CMD_POWER_UP, 0x00, 0x1F, 0x7F, 0x00 };
    if (send_command(power_up_cmd, 5, nullptr, 0) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_TIMEOUT;
    }

    // 5. Upload statically embedded ROM Patch
    std::clog << "libsi468x: Loading embedded ROM patch..." << std::endl;
    if (upload_firmware_memory(fw_rom_patch_bin, fw_rom_patch_bin_len) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    // Post-ROM upload settling delay (50ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 6. Select and upload statically embedded Application Firmware
    const unsigned char* app_fw = nullptr;
    unsigned int app_fw_len = 0;

    if (boot_mode == SI468X_BOOT_DAB) {
        std::clog << "libsi468x: Selecting embedded DAB v6.0.6 firmware..." << std::endl;
        app_fw = fw_dab_radio_bin;
        app_fw_len = fw_dab_radio_bin_len;
    }
    else if (boot_mode == SI468X_BOOT_FMHD) {
        std::clog << "libsi468x: Selecting embedded FMHD v5.1.3 firmware..." << std::endl;
        app_fw = fw_fmhd_radio_bin;
        app_fw_len = fw_fmhd_radio_bin_len;
    }
    else {
        std::cerr << "libsi468x: Invalid boot_mode!" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    std::clog << "libsi468x: Loading embedded application firmware..." << std::endl;
    if (upload_firmware_memory(app_fw, app_fw_len) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    // Post-Application upload settling delay (100ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 7. Send BOOT (0x07, must be 5 bytes) and read 2-byte response to boot both ROM + App images
    std::clog << "libsi468x: Booting application image..." << std::endl;
    uint8_t boot_cmd[5] = { SI468X_CMD_BOOT, 0x00, 0x00, 0x00, 0x00 };
    uint8_t boot_resp[2];
    if (send_command(boot_cmd, 5, boot_resp, 2) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_BOOT;
    }

    // Wait for the on-chip application operating system to boot and stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Enable direct onboard analog headphone output unconditionally upon system init
    if (si468x_set_audio_output(0) != SI468X_SUCCESS) {
        std::cerr << "libsi468x: Warning: Failed to configure default Analog output." << std::endl;
    }

    // Diagnostic query: Print raw chip revision info over SPI
    uint8_t rev_cmd[1] = { 0x10 };
    uint8_t rev_resp[16];
    std::memset(rev_resp, 0, sizeof(rev_resp));
    if (send_command(rev_cmd, 1, rev_resp, 16) == SI468X_SUCCESS) {
        std::clog << "libsi468x: Raw GET_REV_INFO Response: ";
        for (int i = 0; i < 16; i++) {
            std::clog << "0x" << std::hex << (int)rev_resp[i] << " ";
        }
        std::clog << std::dec << std::endl;
    }

    std::clog << "libsi468x: Boot complete. Chip running successfully!" << std::endl;
    return SI468X_SUCCESS;
}

int si468x_shutdown(void)
{
    std::clog << "libsi468x: Shutting down chip and releasing bus..." << std::endl;

    // Hold in reset
    gpio_set_rst(false);

    if (spi_fd >= 0) {
        close(spi_fd);
        spi_fd = -1;
    }
    gpio_shutdown();
    return SI468X_SUCCESS;
}

int si468x_clear_service_list(void)
{
    uint8_t cmd[2] = { 0x80, 0x02 }; // Opcode 0x80 + ACTION 0x02 (Clear Service List)
    return send_command(cmd, 2, nullptr, 0);
}

/* Map center frequencies (in Hz) to the exact, firmware-defined co-processor channel indices */
static uint8_t si468x_get_freq_index(uint32_t frequency_hz)
{
    switch (frequency_hz) {
    case 174928000:
        return 0;   // 5A
    case 176640000:
        return 1;   // 5B
    case 178352000:
        return 2;   // 5C
    case 180064000:
        return 3;   // 5D
    case 181936000:
        return 4;   // 6A
    case 183648000:
        return 5;   // 6B
    case 185360000:
        return 6;   // 6C
    case 187072000:
        return 7;   // 6D
    case 188928000:
        return 8;   // 7A
    case 190640000:
        return 9;   // 7B
    case 192352000:
        return 10;  // 7C
    case 194064000:
        return 11;  // 7D
    case 195936000:
        return 12;  // 8A
    case 197648000:
        return 13;  // 8B
    case 199360000:
        return 14;  // 8C
    case 201072000:
        return 15;  // 8D
    case 202928000:
        return 16;  // 9A
    case 204640000:
        return 17;  // 9B
    case 206352000:
        return 18;  // 9C
    case 208064000:
        return 19;  // 9D
    case 209936000:
        return 20;  // 10A
    case 211648000:
        return 21;  // 10B
    case 213360000:
        return 22;  // 10C
    case 215072000:
        return 24;  // 10D (Shifted because index 23 is interstitial!)
    case 216928000:
        return 25;  // 11A
    case 218640000:
        return 27;  // 11B (Shifted because index 26 is interstitial!)
    case 220352000:
        return 28;  // 11C
    case 222064000:
        return 29;  // 11D
    case 223936000:
        return 30;  // 12A (Shifted because index 31 is interstitial!)
    case 225648000:
        return 32;  // 12B
    case 227360000:
        return 33;  // 12C
    case 229072000:
        return 34;  // 12D
    case 230784000:
        return 35;  // 13A
    case 232496000:
        return 36;  // 13B
    case 234208000:
        return 37;  // 13C
    case 235776000:
        return 38;  // 13D
    case 237488000:
        return 39;  // 13E
    case 239200000:
        return 40;  // 13F
    default:
        return 0;
    }
}

int si468x_set_frequency(uint32_t frequency_hz)
{
    active_frequency = frequency_hz;

    // Cleanly wipe the on-chip service database list to prevent cross-frequency accumulation
    si468x_clear_service_list();

    // Map the requested frequency (in Hz) to its standard European Frequency Index (0-40) complied with firmware
    uint8_t freq_index = si468x_get_freq_index(frequency_hz);

    std::clog << "libsi468x: Tuning chip to freq_index " << (int)freq_index << " (" << frequency_hz << " Hz)..." << std::endl;

    // Build DAB_TUNE_FREQ packet (must be exactly 6 bytes)
    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_DAB_TUNE_FREQ;
    cmd[1] = 0x00;
    cmd[2] = freq_index; // Pass the frequency index at Byte 2 (matches reference binary)
    cmd[3] = 0x00;
    cmd[4] = 0x00;
    cmd[5] = 0x00;

    if (send_command(cmd, 6, nullptr, 0, 5000) != SI468X_SUCCESS) {
        return SI468X_ERROR_SPI;
    }

    // Wait for the RF synthesizers to lock and acquire OFDM sync (up to 1 second)
    bool locked = false;
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Poll signal status using correct 2-byte command
        si468x_signal_status_t status;
        if (si468x_get_signal_status(&status) == SI468X_SUCCESS) {
            if (status.sync_status == 1) {
                locked = true;
                break;
            }
        }
    }

    if (!locked) {
        return SI468X_ERROR_TIMEOUT; // No signal found on this frequency
    }

    // Give the co-processor 3000ms to accumulate and decode the complete FIC database tables from the air
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    return SI468X_SUCCESS;
}

uint32_t si468x_get_frequency(void)
{
    return active_frequency;
}

int si468x_play_service(uint32_t service_id, uint32_t component_id)
{
    std::clog << "libsi468x: Starting service playback (SId: 0x"
              << std::hex << service_id << ", CompId: " << std::dec << component_id << ")..." << std::endl;

    // Build 12-byte command packet matching native binary exactly:
    // - Byte 0: Opcode 0xB3 (START_DIGITAL)
    // - Byte 1: SCIdS index (try 0 first, if fails try 1)
    // - Byte 2..3: 0x00 (reserved)
    // - Byte 4..7: Service ID (32-bit Little-Endian)
    // - Byte 8..11: Global Component ID (32-bit Little-Endian)
    uint8_t cmd[12] = {
        0xB3, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };

    // Send first attempt with SCIdS = 0
    int ret = send_command(cmd, 12, nullptr, 0);
    if (ret != SI468X_SUCCESS) {
        // Try fallback attempt with SCIdS = 1 just like native play_station()
        cmd[1] = 0x01;
        ret = send_command(cmd, 12, nullptr, 0);
    }

    if (ret == SI468X_SUCCESS) {
        // Unconditionally force the analog headphone routing property (Property 0x0800 = 0x0001)
        // right after triggering playback to ensure the physical DAC audio path is connected!
        si468x_set_audio_output(0);
    }

    return ret;
}

int si468x_stop_service(void)
{
    std::clog << "libsi468x: Stopping service playback..." << std::endl;

    // Build 12-byte STOP_DIGITAL command matching native binary exactly (using opcode 0xB4)
    uint8_t cmd[12] = {
        0xB4, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    // Send first attempt with SCIdS = 0
    int ret = send_command(cmd, 12, nullptr, 0);
    if (ret == SI468X_SUCCESS) {
        return SI468X_SUCCESS;
    }

    // Try fallback attempt with SCIdS = 1 just like native play_station()
    cmd[1] = 0x01;
    return send_command(cmd, 12, nullptr, 0);
}

int si468x_set_volume(uint8_t volume)
{
    if (volume > 63) {
        volume = 63;
    }
    std::clog << "libsi468x: Setting volume property to " << (int)volume << std::endl;

    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = (SI468X_PROP_AUDIO_VOLUME >> 8) & 0xFF;
    cmd[3] = SI468X_PROP_AUDIO_VOLUME & 0xFF;
    cmd[4] = 0x00;
    cmd[5] = volume;

    return send_command(cmd, 6, nullptr, 0);
}

void si468x_decode_short_label(const char* long_label, uint16_t char_mask, char* short_label)
{
    int dst = 0;
    for (int i = 0; i < 16; i++) {
        // Bit 15 represents the first character (index 0) of the long label
        if ((char_mask & (0x8000 >> i))) {
            short_label[dst++] = long_label[i];
            if (dst >= 8) {
                break;    // Short label is max 8 characters
            }
        }
    }
    short_label[dst] = '\0';
}

int si468x_get_ensemble_info(char* label, uint16_t* ueid)
{
    if (!label) {
        return -1;
    }

    // Write { 0xB4, 0x00 } (2 bytes) and read 26 response parameter bytes over SPI
    uint8_t cmd[2] = { 0xB4, 0x00 };
    uint8_t resp[26];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 26) != SI468X_SUCCESS) {
        return -1;
    }

    if (ueid) {
        *ueid = resp[5] | ((uint16_t)resp[6] << 8);
    }

    // Extract Ensemble Label (16 bytes starting exactly at resp[7] past 16-bit Ensemble ID)
    std::memcpy(label, &resp[7], 16);
    label[16] = '\0';
    return 0;
}

int si468x_get_component_info(uint32_t service_id, uint32_t component_id, char* label, char* short_label, uint8_t* subchannel_id)
{
    // Write 12-byte command: Opcode 0xBB + Service ID + 32-bit global Component ID
    uint8_t cmd[12] = {
        0xBB, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };
    uint8_t resp[29];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 12, resp, 29) != SI468X_SUCCESS) {
        return -1;
    }

    // Subchannel ID is at resp[7] & 0x3F (low 6 bits)
    if (subchannel_id) {
        *subchannel_id = resp[7] & 0x3F;
    }

    // Component Label starts at resp[9] (16 bytes)
    char comp_label[17];
    std::memcpy(comp_label, &resp[9], 16);
    comp_label[16] = '\0';
    if (label) {
        std::strcpy(label, comp_label);
    }

    // Short Label Character flag mask is at resp[25..26] (16-bit Little-Endian)
    uint16_t char_mask = resp[25] | ((uint16_t)resp[26] << 8);

    if (short_label) {
        si468x_decode_short_label(comp_label, char_mask, short_label);
    }

    return 0;
}

int si468x_get_service_list(si468x_service_t* list, int max_services)
{
    if (!list || max_services <= 0) {
        return 0;
    }

    std::clog << "libsi468x: Querying on-chip service database..." << std::endl;

    // Step 1: Send GET_DIGITAL_SERVICE_LIST (0x80) command to query database size (7-byte read)
    uint8_t size_cmd[2] = { 0x80, 0x00 };
    uint8_t size_resp[7];
    std::memset(size_resp, 0, sizeof(size_resp));

    if (send_command(size_cmd, 2, size_resp, 7) != SI468X_SUCCESS) {
        return 0;
    }

    // Bytes 5-6 (Response Parameter Bytes 1-2) store the 16-bit database size in Little-Endian
    uint16_t db_size = size_resp[5] | ((uint16_t)size_resp[6] << 8);
    std::clog << "libsi468x: Chip reported database payload size: " << db_size << " bytes." << std::endl;

    if (db_size == 0 || db_size > 2048) {
        return 0;
    }

    // Step 2: Send GET_DIGITAL_SERVICE_LIST (0x80) again to download full payload (db_size + 7 bytes)
    uint16_t full_resp_len = db_size + 7;
    std::vector<uint8_t> resp(full_resp_len, 0x00);

    if (send_command(size_cmd, 2, resp.data(), full_resp_len) != SI468X_SUCCESS) {
        return 0;
    }

    // Shift index by 7 to bypass the 4-byte SPI status/padding overhead and 3 header bytes
    uint8_t num_services = resp[7];
    std::clog << "libsi468x: Chip reported " << (int)num_services << " active services." << std::endl;

    if (num_services == 0) {
        return 0;
    }

    int services_count = 0;
    size_t offset = 13; // Data payload starts at index 13 of the response parameters (after 5-byte header)

    for (int i = 0; i < num_services; i++) {
        if (services_count >= max_services) {
            break;
        }
        if (offset + 24 > full_resp_len) {
            break;    // Buffer boundary safety guard
        }

        // 1. Service ID (SId): 32-bit Little Endian (offset 0-3)
        uint32_t service_id = resp[offset] |
                              ((uint32_t)resp[offset + 1] << 8) |
                              ((uint32_t)resp[offset + 2] << 16) |
                              ((uint32_t)resp[offset + 3] << 24);

        // 2. Number of Components (offset 5, low 4 bits)
        uint8_t num_components = resp[offset + 5] & 0x0F;

        // 3. Label: 16-character array (offset 8-23)
        char service_label[17];
        std::memcpy(service_label, &resp[offset + 8], 16);
        service_label[16] = '\0';

        // 4. Subchannel ID (SubChId) is stored at offset 24
        uint8_t subchannel_id = resp[offset + 24];

        // Offset advancement past the 24-byte Service Header
        offset += 24;

        // 5. Parse Components of this service
        for (int c = 0; c < num_components; c++) {
            if (offset + 4 > full_resp_len) {
                break;
            }

            // Component block is exactly 4 bytes:
            // - Component ID: 12-bit value packed in a 16-bit Little Endian word (offset 0-1)
            uint16_t component_id = (resp[offset] | ((uint16_t)resp[offset + 1] << 8)) & 0x0FFF;

            // Store the first audio component of the service in our output list
            if (c == 0) {
                list[services_count].service_id = service_id;
                list[services_count].component_id = component_id;
                list[services_count].audio_type = subchannel_id; // Reuse audio_type to pass Subchannel ID cleanly!
                list[services_count].bitrate = 0;                // Resolved dynamically during playback

                std::strncpy(list[services_count].label, service_label, 16);
                list[services_count].label[16] = '\0';

                // Reconstruct clean fallback short label dynamically (copy first 8 characters and strip trailing spaces)
                std::strncpy(list[services_count].short_label, service_label, 8);
                list[services_count].short_label[8] = '\0';
                for (int len = 7; len >= 0; len--) {
                    if (list[services_count].short_label[len] == ' ' || list[services_count].short_label[len] == '\0') {
                        list[services_count].short_label[len] = '\0';
                    }
                    else {
                        break;
                    }
                }

                services_count++;
            }

            offset += 4; // Component Entry is 4 bytes
        }
    }

    return services_count;
}

int si468x_get_signal_status(si468x_signal_status_t* status)
{
    if (!status) {
        return -1;
    }

    // 2-byte packet required (Opcode + INTACK) and 24-byte response payload (total 28 bytes)
    uint8_t cmd[2] = { SI468X_CMD_DAB_DIGRAD_STATUS, 0x00 };
    std::vector<uint8_t> resp(28, 0x00);

    if (send_command(cmd, 2, resp.data(), 28) != SI468X_SUCCESS) {
        // Return simulated parameters if physical bus is closed (mock fallback)
        if (spi_fd < 0) {
            status->rssi = 45;       // 45 dBuV (decent signal)
            status->snr = 18;        // 18 dB SNR (clean sync)
            status->freq_offset = 0;
            status->sync_status = 1; // Synced
            return SI468X_SUCCESS;
        }
        return SI468X_ERROR_SPI;
    }

    // Parse DAB_DIGRAD_STATUS Response (24 parameter bytes):
    // resp[0..3] : SPI status and padding bytes (Byte 0 is STATUS, Bytes 1-3 is padding)
    // resp[4]    : Response parameter Byte 0 (state/status)
    // resp[5]    : Response parameter Byte 1 (digital status: Bit 1 is SYNC, Bit 2 is FIC_SYNC)
    // resp[6]    : Response parameter Byte 2 (acq status: Bit 2 is ACQ)
    // resp[7]    : Response parameter Byte 3 (RSSI value in dBuV!)
    // resp[10]   : Response parameter Byte 6 (SNR value in dB!)
    // resp[21..22] : Response parameter Byte 17-18 (Antenna Tuning Cap)
    std::clog << "libsi468x: Raw DIGRAD_STATUS: ";
    for (int i = 0; i < 28; i++) {
        std::clog << "0x" << std::hex << (int)resp[i] << " ";
    }
    std::clog << std::dec << std::endl;

    status->rssi = resp[7];  // Aligned with native binary offset (Byte 3)
    status->snr = resp[10];  // Aligned with native binary offset (Byte 6)
    status->freq_offset = 0; // Handled as secondary telemetry

    // Lock achieved if OFDM Frame Sync (Bit 1 of Byte 1), FIC Sync (Bit 2 of Byte 1),
    // or Signal Acquisition (Bit 2 of Byte 2) are high.
    status->sync_status = ((resp[5] & 0x06) || (resp[6] & 0x04)) ? 1 : 0;

    return SI468X_SUCCESS;
}

int si468x_set_audio_output(int enable_i2s)
{
    std::clog << "libsi468x: Configuring audio output path (I2S: " << enable_i2s << ")..." << std::endl;

    // Set Property 0x0800 (output pin config enable)
    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x08;
    cmd[3] = 0x00;
    cmd[4] = 0x00;
    cmd[5] = enable_i2s ? 0x02 : 0x01; // 0x02 = Digital I2S, 0x01 = Analog

    int ret = send_command(cmd, 6, nullptr, 0);
    if (ret != SI468X_SUCCESS) {
        return ret;
    }

    if (enable_i2s) {
        // Set Property 0x0200 to 0x8000 (enable digital audio IO block)
        cmd[0] = SI468X_CMD_SET_PROPERTY;
        cmd[1] = 0x00;
        cmd[2] = 0x02;
        cmd[3] = 0x00;
        cmd[4] = 0x80;
        cmd[5] = 0x00;
        ret = send_command(cmd, 6, nullptr, 0);
        if (ret != SI468X_SUCCESS) {
            return ret;
        }

        // Set Property 0x0202 to 0x1000 (digital audio format select)
        cmd[0] = SI468X_CMD_SET_PROPERTY;
        cmd[1] = 0x00;
        cmd[2] = 0x02;
        cmd[3] = 0x02;
        cmd[4] = 0x10;
        cmd[5] = 0x00;
        ret = send_command(cmd, 6, nullptr, 0);
        if (ret != SI468X_SUCCESS) {
            return ret;
        }
    }
    return SI468X_SUCCESS;
}
