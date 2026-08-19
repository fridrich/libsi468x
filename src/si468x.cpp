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

#include <dirent.h>

#include "si468x.h"
#include "si468x_internal.h"
#include "si468x_firmware.h"

static int spi_fd = -1;
static int sysfs_gpio_pin = -1;
static int sysfs_ce1_pin = -1;
static uint32_t active_frequency = 0;
static int active_audio_mode = 0; // 0 = Analog Only, 1 = I2S Digital, 2 = Simultaneous Analog + I2S
static int active_volume = 50;    // Standard default volume (0 to 63)

// Global debug logging status flag
extern "C" {
    int si468x_debug_active = 0;
}

void si468x_enable_debug(int enable)
{
    si468x_debug_active = enable;
}

static int si468x_enable_service_data(void);
static int si468x_enable_rds(void);

// Helper to determine the sysfs GPIO base for the primary pinctrl chip
static int get_sysfs_gpio_base(void)
{
    DIR* dir = opendir("/sys/class/gpio");
    if (!dir) {
        return 0; // fallback
    }
    struct dirent* entry;
    int base_offset = 0;
    bool found = false;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.find("gpiochip") == 0) {
            std::string label_path = "/sys/class/gpio/" + name + "/label";
            std::ifstream label_file(label_path);
            if (label_file) {
                std::string label;
                std::getline(label_file, label);
                if (label.find("pinctrl-") != std::string::npos ||
                        label.find("rp1-gpio") != std::string::npos ||
                        label.find("13040000.pinctrl") != std::string::npos) {
                    std::string base_path = "/sys/class/gpio/" + name + "/base";
                    std::ifstream base_file(base_path);
                    if (base_file) {
                        base_file >> base_offset;
                        found = true;
                        break;
                    }
                }
            }
        }
    }
    closedir(dir);
    return found ? base_offset : 0;
}

/* Low-level GPIO RSTB management */
static bool gpio_init(int pin)
{
    if (pin < 0) {
        SI468X_ERR << "libsi468x: Invalid GPIO pin: " << pin << std::endl;
        return false;
    }

    int base = get_sysfs_gpio_base();
    sysfs_gpio_pin = base + pin;

    // Only resolve CE1 SPI contention on Raspberry Pi boards (base offset 0)
    if (base == 0) {
        sysfs_ce1_pin = base + 7; // BCM GPIO 7 is CE1
    }
    else {
        sysfs_ce1_pin = -1;
    }

    SI468X_LOG << "libsi468x: Mapping logic GPIO " << pin << " to sysfs GPIO " << sysfs_gpio_pin << std::endl;

    // Export primary reset pin if not already exported
    std::string rst_path = "/sys/class/gpio/gpio" + std::to_string(sysfs_gpio_pin);
    if (access(rst_path.c_str(), F_OK) < 0) {
        std::ofstream export_file("/sys/class/gpio/export");
        if (export_file) {
            export_file << sysfs_gpio_pin;
            export_file.flush();
        }
    }

    // Export CE1 pin if enabled
    if (sysfs_ce1_pin >= 0) {
        std::string ce1_path = "/sys/class/gpio/gpio" + std::to_string(sysfs_ce1_pin);
        if (access(ce1_path.c_str(), F_OK) < 0) {
            std::ofstream export_file("/sys/class/gpio/export");
            if (export_file) {
                export_file << sysfs_ce1_pin;
                export_file.flush();
            }
        }
    }

    // Wait a brief moment for the sysfs nodes to register and set permissions
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Set direction of reset pin to "out"
    std::string rst_dir_path = rst_path + "/direction";
    std::ofstream rst_dir_file(rst_dir_path);
    if (!rst_dir_file) {
        SI468X_ERR << "libsi468x: Failed to set sysfs reset GPIO direction to out!" << std::endl;
        return false;
    }
    rst_dir_file << "out";
    rst_dir_file.flush();

    // Set direction of CE1 pin to "out" and drive high (if enabled)
    if (sysfs_ce1_pin >= 0) {
        std::string ce1_path = "/sys/class/gpio/gpio" + std::to_string(sysfs_ce1_pin);
        std::string ce1_dir_path = ce1_path + "/direction";
        std::ofstream ce1_dir_file(ce1_dir_path);
        if (ce1_dir_file) {
            ce1_dir_file << "out";
            ce1_dir_file.flush();
        }

        std::string ce1_val_path = ce1_path + "/value";
        std::ofstream ce1_val_file(ce1_val_path);
        if (ce1_val_file) {
            ce1_val_file << "1"; // Drive CE1 high to resolve bus contention
            ce1_val_file.flush();
        }
    }

    return true;
}

static void gpio_set_rst(bool high)
{
    if (sysfs_gpio_pin < 0) {
        return;
    }
    std::string val_path = "/sys/class/gpio/gpio" + std::to_string(sysfs_gpio_pin) + "/value";
    std::ofstream val_file(val_path);
    if (val_file) {
        val_file << (high ? "1" : "0");
        val_file.flush();
    }
}

static void gpio_shutdown()
{
    if (sysfs_gpio_pin >= 0) {
        gpio_set_rst(false); // Hold in reset for safety
        std::ofstream unexport_file("/sys/class/gpio/unexport");
        if (unexport_file) {
            unexport_file << sysfs_gpio_pin;
            unexport_file.flush();
        }
        sysfs_gpio_pin = -1;
    }
    if (sysfs_ce1_pin >= 0) {
        std::ofstream unexport_file("/sys/class/gpio/unexport");
        if (unexport_file) {
            unexport_file << sysfs_ce1_pin;
            unexport_file.flush();
        }
        sysfs_ce1_pin = -1;
    }
}

/* Low-level SPI transfer helper */
static int spi_transfer(const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (spi_fd < 0) {
        return -1;
    }

    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));

    std::vector<uint8_t> dummy_tx;
    if (!tx && rx && length > 0) {
        dummy_tx.assign(length, 0x00);
        tr.tx_buf = (unsigned long)dummy_tx.data();
    }
    else {
        tr.tx_buf = (unsigned long)tx;
    }

    tr.rx_buf = (unsigned long)rx;
    tr.len = length;
    tr.speed_hz = SI468X_SPI_SPEED_HZ;
    tr.bits_per_word = SI468X_SPI_BITS_PER_WORD;

    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1) {
        return -1;
    }
    return 0;
}

/*
 * Transmit a command packet and wait for response.
 * Uses reference polling loop to check CTS via 7-byte READ_RESP queries.
 */
int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms)
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
                // Check for Command Error (ERR bit 6), silencing expected queue-empty codes for 0x84/0xBB/0x80
                if ((poll_rx[1] & 0x40) && cmd && cmd[0] != 0x84 && cmd[0] != 0xBB && cmd[0] != 0x80) {
                    SI468X_ERR << "libsi468x: WARNING: Command Error (Status: 0x"
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

    // 3. Read the response payload. If none was requested, exit immediately (matches native binary exactly!)
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
        packet[0] = SI468X_CMD_HOST_LOAD;
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

class RDS_decode
{
public:
    char radio_text[65];
    int8_t last_segment;
    uint8_t last_toggle;
    uint8_t group_type;
    uint8_t group_type_B;
    uint8_t traffic_program;
    uint8_t program_type;
    uint8_t block_B_low_5;
    bool complete_reported;

    RDS_decode()
    {
        std::memset(radio_text, 0, sizeof(radio_text));
        last_segment = -1;
        last_toggle = 0;
        group_type = 0;
        group_type_B = 0;
        traffic_program = 0;
        program_type = 0;
        block_B_low_5 = 0;
        complete_reported = false;
    }

    bool is_allowed_RT_char(uint8_t c)
    {
        // Accept all standard ASCII (32-126) and extended Latin/EBU characters (128-255), plus CR/LF
        return (c >= 32 && c <= 126) || (c >= 128) || c == '\r' || c == '\n';
    }

    void decode_RT_block(uint8_t segment_address, uint8_t text_A_B_toggle, uint8_t is_block_D, uint16_t block_data)
    {
        uint8_t char1 = block_data >> 8;
        uint8_t char2 = block_data & 0xFF;
        uint8_t step_size = 2 - group_type_B;

        if (last_toggle != text_A_B_toggle) {
            if (last_segment != -1) {
                std::memset(radio_text, 0, sizeof(radio_text));
            }
            last_toggle = text_A_B_toggle;
            complete_reported = false; // Reset complete state on text toggle change
        }
        last_segment = segment_address;

        if (is_allowed_RT_char(char1)) {
            int pos = (segment_address * step_size * 2) + (is_block_D * step_size);
            if (pos >= 0 && pos < 64) {
                radio_text[pos] = char1;
            }
        }

        if (step_size == 2 && is_allowed_RT_char(char2)) {
            int pos = (segment_address * step_size * 2) + (is_block_D * step_size) + 1;
            if (pos >= 0 && pos < 64) {
                radio_text[pos] = char2;
            }
        }
    }

    void decode_block_A(uint16_t block_A) {}

    void decode_block_B(uint16_t block_B)
    {
        group_type = block_B >> 12;
        group_type_B = (block_B >> 11) & 0x01;
        traffic_program = (block_B >> 10) & 0x01;
        program_type = (block_B >> 5) & 0x1F;
        block_B_low_5 = block_B & 0x1F;
    }

    void decode_block_C(uint16_t block_C)
    {
        if (group_type_B == 0 && group_type == 2) {
            uint8_t segment_address = block_B_low_5 & 0x0F;
            uint8_t text_A_B_toggle = (block_B_low_5 >> 4) & 0x01;
            decode_RT_block(segment_address, text_A_B_toggle, 0, block_C);
        }
    }

    void decode_block_D(uint16_t block_D)
    {
        if (group_type == 2) {
            uint8_t segment_address = block_B_low_5 & 0x0F;
            uint8_t text_A_B_toggle = (block_B_low_5 >> 4) & 0x01;
            decode_RT_block(segment_address, text_A_B_toggle, 1, block_D);
        }
    }

    void decode(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
    {
        decode_block_A(a);
        decode_block_B(b);
        decode_block_C(c);
        decode_block_D(d);
    }
};

static RDS_decode rds_decoder;

static uint32_t custom_freqs[48];
static int custom_freq_count = 0;

/* Public C-API Implementation */

int si468x_init(const char* spi_device, int rst_pin, int boot_mode)
{

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
    spi_fd = open(spi_device, O_RDWR);
    if (spi_fd < 0) {
        SI468X_ERR << "libsi468x: Error opening SPI device: " << spi_device << std::endl;
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
        SI468X_ERR << "libsi468x: Error configuring SPI bus parameters!" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_SPI;
    }

    // 4. Send POWER_UP (0x01) with complete 16-byte reference crystal and clock configuration (matches radio_cli)
    uint8_t power_up_cmd[16] = {
        SI468X_CMD_POWER_UP, 0x00, 0x1F, 0x7F, 0x00,
        0xF8, 0x24, 0x01, 0x20, 0x10, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00
    };
    if (send_command(power_up_cmd, 16, nullptr, 0) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_TIMEOUT;
    }

    // 5. Upload statically embedded ROM Patch
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
        app_fw = fw_dab_radio_bin;
        app_fw_len = fw_dab_radio_bin_len;
    }
    else if (boot_mode == SI468X_BOOT_FMHD) {
        app_fw = fw_fmhd_radio_bin;
        app_fw_len = fw_fmhd_radio_bin_len;
    }
    else {
        SI468X_ERR << "libsi468x: Invalid boot_mode!" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    if (upload_firmware_memory(app_fw, app_fw_len) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    // Post-Application upload settling delay (100ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 7. Send BOOT (0x07, must be 5 bytes) and read 2-byte response to boot both ROM + App images
    uint8_t boot_cmd[5] = { SI468X_CMD_BOOT, 0x00, 0x00, 0x00, 0x00 };
    uint8_t boot_resp[2];
    if (send_command(boot_cmd, 5, boot_resp, 2) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_BOOT;
    }

    // Wait for the on-chip application operating system to boot and stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Enable default audio output upon system init
    if (si468x_set_audio_output(active_audio_mode) != SI468X_SUCCESS) {
        SI468X_ERR << "libsi468x: Warning: Failed to configure default audio output." << std::endl;
    }

    // Enable the co-processor's decoder based on boot mode upon system init (before playback starts)
    if (boot_mode == SI468X_BOOT_DAB) {
        si468x_enable_service_data();
    }
    else if (boot_mode == SI468X_BOOT_FMHD) {
        si468x_enable_rds();
        si468x_set_rds_region(SI468X_REGION_EUROPE); // Set standard European de-emphasis (50 us) by default on boot
    }

    // Diagnostic query: Print raw chip revision info over SPI
    uint8_t rev_cmd[1] = { 0x10 };
    uint8_t rev_resp[16];
    std::memset(rev_resp, 0, sizeof(rev_resp));
    send_command(rev_cmd, 1, rev_resp, 16);

    SI468X_LOG << "libsi468x: Boot complete. Chip running successfully!" << std::endl;
    return SI468X_SUCCESS;
}

int si468x_shutdown(void)
{
    SI468X_LOG << "libsi468x: Shutting down chip and releasing bus..." << std::endl;

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
    if (custom_freq_count > 0) {
        for (int i = 0; i < custom_freq_count; i++) {
            if (custom_freqs[i] == frequency_hz) {
                return i;
            }
        }
        return 0;
    }

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

static int si468x_enable_service_data(void)
{
    uint8_t cmd[6];

    // Set Property 0xB200 to 0x003F (63) to enable PAD/XPAD decoding (Little-Endian)
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x00; // Low byte of Property 0xB200
    cmd[3] = 0xB2; // High byte of Property 0xB200
    cmd[4] = 0x3F; // Low byte of Value (0x3F)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0xB201 to 0x000C (12) (Little-Endian)
    cmd[2] = 0x01; // Low byte of Property 0xB201
    cmd[3] = 0xB2; // High byte of Property 0xB201
    cmd[4] = 0x0C; // Low byte of Value (0x0C)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0xB202 to 0x07D0 (2000) (Little-Endian)
    cmd[2] = 0x02; // Low byte of Property 0xB202
    cmd[3] = 0xB2; // High byte of Property 0xB202
    cmd[4] = 0xD0; // Low byte of Value (0xD0)
    cmd[5] = 0x07; // High byte of Value (0x07)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0xB203 to 0x07D0 (2000) (Little-Endian)
    cmd[2] = 0x03; // Low byte of Property 0xB203
    cmd[3] = 0xB2; // High byte of Property 0xB203
    cmd[4] = 0xD0; // Low byte of Value (0xD0)
    cmd[5] = 0x07; // High byte of Value (0x07)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0xB204 to 0x07D0 (2000) (Little-Endian)
    cmd[2] = 0x04; // Low byte of Property 0xB204
    cmd[3] = 0xB2; // High byte of Property 0xB204
    cmd[4] = 0xD0; // Low byte of Value (0xD0)
    cmd[5] = 0x07; // High byte of Value (0x07)
    send_command(cmd, 6, nullptr, 0);

    return SI468X_SUCCESS;
}

static int si468x_enable_rds(void)
{
    uint8_t cmd[6];

    // Set Property 0x1500 to 0x0001 (FM_RDS_CONFIG: Enable RDS) (Little-Endian)
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x00; // Low byte of Property 0x1500
    cmd[3] = 0x15; // High byte of Property 0x1500
    cmd[4] = 0x01; // Low byte of Value (0x01)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0x1501 to 0x0001 (FM_RDS_INT_SOURCE: Enable FIFO interrupt) (Little-Endian)
    cmd[2] = 0x01; // Low byte of Property 0x1501
    cmd[3] = 0x15; // High byte of Property 0x1501
    cmd[4] = 0x01; // Low byte of Value (0x01)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0x1502 to 0x0001 (FM_RDS_INT_FIFO_COUNT: Interrupt threshold = 1) (Little-Endian)
    cmd[2] = 0x02; // Low byte of Property 0x1502
    cmd[3] = 0x15; // High byte of Property 0x1502
    cmd[4] = 0x01; // Low byte of Value (0x01)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    return SI468X_SUCCESS;
}

int si468x_play_service(uint32_t service_id, uint32_t component_id)
{
    SI468X_LOG << "libsi468x: Starting service playback (SId: 0x"
               << std::hex << service_id << ", CompId: " << std::dec << component_id << ")..." << std::endl;

    // Build 12-byte command packet matching native binary exactly:
    // - Byte 0: Opcode 0x81 (START_SERVICE)
    // - Byte 1: SCIdS index (try 0 first, if fails try 1)
    // - Byte 2..3: 0x00 (reserved)
    // - Byte 4..7: Service ID (32-bit Little-Endian)
    // - Byte 8..11: Global Component ID (32-bit Little-Endian)
    uint8_t cmd[12] = {
        SI468X_CMD_START_DIGITAL_SERVICE, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };

    // Send first attempt with SCIdS = 0 (reads 0 response parameter bytes, matches radio_cli!)
    int ret = send_command(cmd, 12, nullptr, 0);

    if (ret != SI468X_SUCCESS) {
        // Try fallback attempt with SCIdS = 1 just like native play_station()
        cmd[1] = 0x01;
        ret = send_command(cmd, 12, nullptr, 0);
    }

    if (ret == SI468X_SUCCESS) {
        // Turn on the on-chip PAD/XPAD decoder so that DLS text and MOT slideshow are dynamically decoded
        si468x_enable_service_data();

        return SI468X_SUCCESS;
    }

    return SI468X_ERROR_SPI;
}

int si468x_stop_service(void)
{
    SI468X_LOG << "libsi468x: Stopping service playback..." << std::endl;

    // Build 12-byte STOP_DIGITAL command matching native binary exactly (using opcode 0x82)
    uint8_t cmd[12] = {
        SI468X_CMD_STOP_DIGITAL_SERVICE, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    // Send first attempt with SCIdS = 0 (reads 0 response parameter bytes, matches radio_cli!)
    int ret = send_command(cmd, 12, nullptr, 0);

    if (ret != SI468X_SUCCESS) {
        // Try fallback attempt with SCIdS = 1 just like native play_station()
        cmd[1] = 0x01;
        ret = send_command(cmd, 12, nullptr, 0);
    }

    if (ret == SI468X_SUCCESS) {
        return SI468X_SUCCESS;
    }

    return SI468X_ERROR_SPI;
}

int si468x_set_volume(uint8_t volume)
{
    if (volume > SI468X_VOLUME_MAX) {
        volume = SI468X_VOLUME_MAX;
    }
    active_volume = volume;

    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = SI468X_PROP_AUDIO_ANALOG_VOLUME & 0xFF;        // Low byte of ID (0x00)
    cmd[3] = (SI468X_PROP_AUDIO_ANALOG_VOLUME >> 8) & 0xFF; // High byte of ID (0x03)
    cmd[4] = volume & 0xFF;                          // Low byte of Value
    cmd[5] = 0x00;                                   // High byte of Value (0x00)

    uint8_t resp[7];
    std::memset(resp, 0, sizeof(resp));

    // Capture and print the raw 7-byte response parameters for maximum debugging visibility
    int ret = send_command(cmd, 6, resp, 7);
    SI468X_LOG << "libsi468x: SET_VOLUME Property 0x0300 Raw Response: ";
    for (int i = 0; i < 7; i++) {
        SI468X_LOG << "0x" << std::hex << (int)resp[i] << " ";
    }
    SI468X_LOG << std::dec << " (ret: " << ret << ")" << std::endl;

    return ret;
}

// Strict ETSI TS 101 756 EBU Latin (Charset 0) table for the 0x80 to 0x9F range
static const uint16_t ebu_latin_80_9F[32] = {
    0x00E1, 0x00E0, 0x00E9, 0x00E8, 0x00ED, 0x00EC, 0x00F3, 0x00F2, // 80 - 87
    0x00FA, 0x00F9, 0x00D1, 0x00C7, 0x015E, 0x00DF, 0x00A1, 0x0132, // 88 - 8F
    0x00E2, 0x00E4, 0x00EA, 0x00EB, 0x00EE, 0x00EF, 0x00F4, 0x00F6, // 90 - 97
    0x00FB, 0x00FC, 0x00F1, 0x00E7, 0x015F, 0x011F, 0x0131, 0x0133  // 98 - 9F
};

static void decode_dab_string_to_utf8(const uint8_t* in, int in_len, uint8_t charset, char* out, int max_out_len)
{
    int out_idx = 0;

    if (charset == 6) {
        // UCS-2 (16-bit Big-Endian) to UTF-8
        for (int i = 0; i < in_len - 1 && out_idx < max_out_len - 4; i += 2) {
            uint16_t codepoint = (in[i] << 8) | in[i + 1];
            if (codepoint == 0x0000) {
                break; // Stop on explicit UCS-2 null terminator
            }
            else if (codepoint < 0x0080) {
                out[out_idx++] = (char)codepoint;
            }
            else if (codepoint < 0x0800) {
                out[out_idx++] = 0xC0 | (codepoint >> 6);
                out[out_idx++] = 0x80 | (codepoint & 0x3F);
            }
            else {
                out[out_idx++] = 0xE0 | (codepoint >> 12);
                out[out_idx++] = 0x80 | ((codepoint >> 6) & 0x3F);
                out[out_idx++] = 0x80 | (codepoint & 0x3F);
            }
        }
    }
    else {
        // Charset 0 (EBU Latin) or Charset 15 (UTF-8 fallback)
        for (int i = 0; i < in_len && out_idx < max_out_len - 4; i++) {
            uint8_t c = in[i];
            if (c == 0x00) {
                break;
            }
            if (charset == 0) {
                if (c >= 0x80 && c <= 0x9F) {
                    // ETSI Specific EBU Latin range (e.g. 0x82 is 'é')
                    uint16_t codepoint = ebu_latin_80_9F[c - 0x80];
                    if (codepoint < 0x0080) {
                        out[out_idx++] = (char)codepoint;
                    }
                    else if (codepoint < 0x0800) {
                        out[out_idx++] = 0xC0 | (codepoint >> 6);
                        out[out_idx++] = 0x80 | (codepoint & 0x3F);
                    }
                    else {
                        out[out_idx++] = 0xE0 | (codepoint >> 12);
                        out[out_idx++] = 0x80 | ((codepoint >> 6) & 0x3F);
                        out[out_idx++] = 0x80 | (codepoint & 0x3F);
                    }
                }
                else if (c >= 0xA0) {
                    // Standard ISO-8859-1 mapping to UTF-8
                    out[out_idx++] = 0xC0 | (c >> 6);
                    out[out_idx++] = 0x80 | (c & 0x3F);
                }
                else {
                    out[out_idx++] = c;
                }
            }
            else {
                out[out_idx++] = c;
            }
        }
    }
    out[out_idx] = '\0';
}

void si468x_decode_short_label(const uint8_t* raw_label, int raw_len, uint16_t char_mask, uint8_t charset, char* short_label, int max_len)
{
    uint8_t temp_raw[16];
    int dst = 0;

    // Extract the raw, unencoded bytes corresponding to the short label using the mask
    for (int i = 0; i < raw_len; i++) {
        // Bit 15 represents the first character (index 0) of the long label
        if ((char_mask & (0x8000 >> i))) {
            temp_raw[dst++] = raw_label[i];
            if (dst >= 8) {
                break;
            }
        }
    }

    // Dynamically decode the extracted raw bytes into standard UTF-8
    decode_dab_string_to_utf8(temp_raw, dst, charset, short_label, max_len);
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

    // Extract Ensemble Label (16 bytes starting exactly at resp[7])
    uint8_t charset = (resp[23] >> 4) & 0x0F; // Charset is typically at byte 18 of parameters (resp[23])
    decode_dab_string_to_utf8(&resp[7], 16, charset, label, 32);

    return 0;
}

int si468x_get_component_info(uint32_t service_id, uint32_t component_id, char* label, char* short_label, uint16_t* out_char_mask, uint8_t* subchannel_id)
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

    int ret = send_command(cmd, 12, resp, 29);
    if (ret != SI468X_SUCCESS) {
        return -1;
    }

    // Subchannel ID is at resp[7] & 0x3F (low 6 bits)
    if (subchannel_id) {
        *subchannel_id = resp[7] & 0x3F;
    }

    // Component Label starts at resp[9] (16 bytes)
    char comp_label[32];
    uint8_t charset = (resp[6] >> 4) & 0x0F;
    decode_dab_string_to_utf8(&resp[9], 16, charset, comp_label, sizeof(comp_label));

    if (label) {
        std::strcpy(label, comp_label);
    }

    // Short Label Character flag mask is at resp[25..26] (16-bit Little-Endian)
    uint16_t char_mask = resp[25] | ((uint16_t)resp[26] << 8);
    if (out_char_mask) {
        *out_char_mask = char_mask;
    }

    if (short_label) {
        si468x_decode_short_label(&resp[9], 16, char_mask, charset, short_label, 17);
    }

    return 0;
}

int si468x_get_service_list(si468x_service_t* list, int max_services)
{
    if (!list || max_services <= 0) {
        return 0;
    }


    // Step 1: Send GET_DIGITAL_SERVICE_LIST (0x80) command to query database size (7-byte read)
    uint8_t size_cmd[2] = { 0x80, 0x00 };
    uint8_t size_resp[7];
    std::memset(size_resp, 0, sizeof(size_resp));

    if (send_command(size_cmd, 2, size_resp, 7) != SI468X_SUCCESS) {
        return 0;
    }

    // Bytes 5-6 (Response Parameter Bytes 1-2) store the 16-bit database size in Little-Endian
    uint16_t db_size = size_resp[5] | ((uint16_t)size_resp[6] << 8);

    if (db_size == 0 || db_size > 2048) {
        return 0;
    }

    // Step 2: Send GET_DIGITAL_SERVICE_LIST (0x80) again to download full payload (db_size + 7 bytes)
    uint16_t full_resp_len = db_size + 7;
    std::vector<uint8_t> resp(full_resp_len, 0x00);

    if (send_command(size_cmd, 2, resp.data(), full_resp_len) != SI468X_SUCCESS) {
        return 0;
    }

    // Shift index to find number of services in this ensemble (Parameter Byte 5)
    uint8_t num_services = resp[9];

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
        char service_label[32];
        uint8_t charset = (resp[offset + 7] >> 4) & 0x0F;
        decode_dab_string_to_utf8(&resp[offset + 8], 16, charset, service_label, sizeof(service_label));

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

int si468x_get_service_info(uint32_t service_id, si468x_service_info_t* info)
{
    if (!info) {
        return -1;
    }

    // Command 0xC0 (DAB_GET_SERVICE_INFO)
    uint8_t cmd[8];
    cmd[0] = 0xC0; // Opcode
    cmd[1] = 0x00; // INTACK
    cmd[2] = 0x00; // Reserved
    cmd[3] = 0x00; // Reserved
    cmd[4] = service_id & 0xFF;         // Service ID (Little-Endian)
    cmd[5] = (service_id >> 8) & 0xFF;  // Service ID
    cmd[6] = (service_id >> 16) & 0xFF; // Service ID
    cmd[7] = (service_id >> 24) & 0xFF; // Service ID

    uint8_t resp[26];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 8, resp, 26) != SI468X_SUCCESS) {
        return SI468X_ERROR_SPI;
    }

    // Check validity flag (Bit 7 of resp[1])
    if ((resp[1] & 0x80) != 0) {
        // Extract Label (16 bytes at resp[9..24])
        char raw_label[17];
        std::memset(raw_label, 0, sizeof(raw_label));
        std::memcpy(raw_label, &resp[9], 16);

        // Extract PTY (bits 1-5 of resp[5])
        info->pty = (resp[5] >> 1) & 0x1F;
        info->charset = resp[7];
        info->ecc = resp[8];

        decode_dab_string_to_utf8((uint8_t*)raw_label, 16, info->charset, info->label, sizeof(info->label));

        return SI468X_SUCCESS;
    }

    // Invalid response or service not found
    std::memset(info, 0, sizeof(si468x_service_info_t));
    return -2;
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
    active_audio_mode = enable_i2s;

    if (spi_fd < 0) {
        // If SPI is not open yet, store the mode statically so si468x_init can apply it dynamically during boot
        return SI468X_SUCCESS;
    }

    // Set Property 0x0800 (AUDIO_OUT_SEL): 0x0001 (Analog), 0x0002 (I2S), 0x0003 (Simultaneous Analog + I2S)
    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x00; // Low byte of Property ID 0x0800
    cmd[3] = 0x08; // High byte of Property ID 0x0800
    
    uint16_t out_sel = 0x0001;
    if (enable_i2s == SI468X_AUDIO_I2S) {
        out_sel = 0x0002;
    } else if (enable_i2s == SI468X_AUDIO_SIMUL) {
        out_sel = 0x0003;
    }
    cmd[4] = out_sel & 0xFF;
    cmd[5] = (out_sel >> 8) & 0xFF;

    int ret = send_command(cmd, 6, nullptr, 0);
    if (ret != SI468X_SUCCESS) {
        return ret;
    }

    if (enable_i2s == SI468X_AUDIO_I2S || enable_i2s == SI468X_AUDIO_SIMUL) {
        // Set Property 0x0200 (DIGITAL_IO_OUTPUT_FORMAT) to 0xC000 in Little-Endian (I2S Master)
        cmd[0] = SI468X_CMD_SET_PROPERTY;
        cmd[1] = 0x00;
        cmd[2] = 0x00; // Low byte of Property ID 0x0200
        cmd[3] = 0x02; // High byte of Property ID 0x0200
        cmd[4] = 0x00; // Low byte of Value (0x00)
        cmd[5] = 0xC0; // High byte of Value (0xC0 - Master Mode)
        ret = send_command(cmd, 6, nullptr, 0);
        if (ret != SI468X_SUCCESS) {
            return ret;
        }

        // Set Property 0x0202 (DIGITAL_IO_OUTPUT_FORMAT_MASK) to 0x1000 in Little-Endian
        cmd[0] = SI468X_CMD_SET_PROPERTY;
        cmd[1] = 0x00;
        cmd[2] = 0x02; // Low byte of Property ID 0x0202
        cmd[3] = 0x02; // High byte of Property ID 0x0202
        cmd[4] = 0x00; // Low byte of Value (0x00)
        cmd[5] = 0x10; // High byte of Value (0x10)
        ret = send_command(cmd, 6, nullptr, 0);
    }
    return ret;
}

int si468x_get_dls_text(char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return -1;
    }

    // Command 0x84 (DAB_GET_DIGITAL_SERVICE_DATA) with parameter 0x01 (DLS Text)
    uint8_t cmd[2] = { 0x84, 0x01 };
    std::vector<uint8_t> resp(2073, 0x00);

    if (send_command(cmd, 2, resp.data(), 2073) != SI468X_SUCCESS) {
        return -2;
    }

    // Check DLS presence flag (Bit 0 of resp[5])
    bool dls_present = resp[5] & 0x01;
    if (!dls_present) {
        out_text[0] = '\0';
        return 0; // No active DLS text present
    }

    // Extract DLS length (16-bit Little-Endian word at resp[19..20])
    uint16_t dls_len = resp[19] | ((uint16_t)resp[20] << 8);

    // The first 2 bytes (resp[25] and resp[26]) are DLS Control Bytes (header)
    // The actual text characters start at resp[27]
    if (dls_len <= 2 || dls_len > 130) {
        out_text[0] = '\0';
        return 0; // No active DLS text in the queue
    }

    uint16_t text_len = dls_len - 2;

    if (text_len >= max_len) {
        text_len = max_len - 1;
    }

    uint8_t charset = (resp[25] >> 4) & 0x0F; // Charset is typically the upper 4 bits of the DLS Control Byte

    // Dynamic UTF-8 DLS string payload parsing starting exactly at resp[27]
    decode_dab_string_to_utf8(&resp[27], text_len, charset, out_text, max_len);

    static std::string last_dls_text = "";
    std::string current_dls(out_text);

    if (current_dls != last_dls_text) {
        last_dls_text = current_dls;
        return 1; // New text frame assembled!
    }

    return 0; // No change
    }

    // MOT Slideshow static state
    #define SLS_MAX_SEGMENTS  80
    #define SLS_MAX_SEG_SIZE  512
    static uint8_t mot_segment_buf[SLS_MAX_SEGMENTS * SLS_MAX_SEG_SIZE];
    static uint16_t mot_segment_len[SLS_MAX_SEGMENTS];
    static uint8_t mot_segment_bitmap[32]; // 256 bits max
    static uint16_t mot_transport_id = 0;
    static uint32_t mot_byte_counter = 0;
    static uint32_t mot_expected_length = 0;
    static uint8_t mot_highest_segment = 0;
    static uint8_t mot_total_segments = 0;
    static uint32_t mot_last_activity = 0;

    static void clear_mot_buffer() {
    std::memset(mot_segment_len, 0, sizeof(mot_segment_len));
    std::memset(mot_segment_bitmap, 0, sizeof(mot_segment_bitmap));
    mot_transport_id = 0;
    mot_byte_counter = 0;
    mot_highest_segment = 0;
    mot_total_segments = 0;
    mot_expected_length = 0;
    }

    static bool all_mot_segments_received() {
    uint8_t segments_to_check = mot_total_segments;
    if (segments_to_check == 0) {
        segments_to_check = mot_highest_segment + 1;
    }
    if (segments_to_check == 0) return false;

    for (uint8_t i = 0; i < segments_to_check; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        if (!(mot_segment_bitmap[byte_idx] & (1 << bit_idx))) {
            return false;
        }
    }
    return true;
    }

    int si468x_get_mot_slideshow(si468x_mot_slideshow_t* slideshow)
    {
    if (!slideshow) {
        return -1;
    }

    slideshow->is_new = 0;

    // Timeout cleanup
    uint32_t now = (uint32_t)time(NULL);
    if (mot_transport_id != 0 && mot_last_activity > 0 && (now - mot_last_activity) > 30) {
        clear_mot_buffer();
    }

    // Polling opcode 0x84, param 0x01
    uint8_t cmd[2] = { 0x84, 0x01 };
    std::vector<uint8_t> resp(2073, 0x00);

    if (send_command(cmd, 2, resp.data(), 2073) != SI468X_SUCCESS) {
        return -2;
    }

    uint16_t byte_count = resp[19] | ((uint16_t)resp[20] << 8);
    if (byte_count == 0) {
        return 0;
    }

    uint8_t group_type = (resp[8] >> 6) & 0x03;

    // Is it MOT Header?
    if (group_type == 0x01 && resp[27] == 0x80 && resp[28] == 0x00 && resp[29] == 0x12 && byte_count < 200) {
        uint16_t tid = (resp[30] << 8) | resp[31];
        uint32_t new_len = (((uint32_t)resp[35] << 12) | ((uint32_t)resp[36] << 4) | ((uint32_t)resp[37] >> 4)) & 0x00FFFF;

        if (new_len > 0) {
            if (mot_expected_length == 0 || (mot_transport_id != 0 && tid != mot_transport_id)) {
                clear_mot_buffer();
                mot_expected_length = new_len;
                mot_transport_id = tid;
            }
            if (mot_byte_counter >= mot_expected_length && all_mot_segments_received()) {
                mot_total_segments = mot_highest_segment + 1;
                slideshow->image_data = mot_segment_buf;
                slideshow->image_size = mot_expected_length;
                slideshow->transport_id = mot_transport_id;
                slideshow->is_new = 1;
                return 1;
            }
        }
    }
    // Is it MOT Body Segment?
    else if (group_type == 0x01 && (resp[27] == 0x00 || resp[27] == 0x80) && resp[29] == 0x12) {
        uint16_t tid = (resp[30] << 8) | resp[31];
        uint8_t seg_num = resp[28];

        if (mot_transport_id == 0) {
            mot_transport_id = tid;
        }

        if (tid == mot_transport_id) {
            uint8_t byte_idx = seg_num / 8;
            uint8_t bit_idx = seg_num % 8;

            if (!(mot_segment_bitmap[byte_idx] & (1 << bit_idx))) {
                uint16_t data_len = byte_count - 11;
                if (seg_num < SLS_MAX_SEGMENTS && data_len <= SLS_MAX_SEG_SIZE) {
                    std::memcpy(&mot_segment_buf[seg_num * SLS_MAX_SEG_SIZE], &resp[34], data_len);
                    mot_segment_len[seg_num] = data_len;
                    mot_segment_bitmap[byte_idx] |= (1 << bit_idx);
                    mot_byte_counter += data_len;
                    if (seg_num > mot_highest_segment) {
                        mot_highest_segment = seg_num;
                    }
                    mot_last_activity = now;

                    if (mot_expected_length > 0 && mot_byte_counter >= mot_expected_length && all_mot_segments_received()) {
                        mot_total_segments = mot_highest_segment + 1;
                        slideshow->image_data = mot_segment_buf;
                        slideshow->image_size = mot_expected_length;
                        slideshow->transport_id = mot_transport_id;
                        slideshow->is_new = 1;
                        return 1;
                    }
                }
            } else if (seg_num == 0 && mot_expected_length == 0 && mot_highest_segment > 0) {
                if (all_mot_segments_received()) {
                    mot_total_segments = mot_highest_segment + 1;

                    // We don't have the exact header length, but we have all segments. Sum lengths.
                    uint32_t calc_len = 0;
                    for (int i=0; i<mot_total_segments; i++) calc_len += mot_segment_len[i];

                    // To keep it clean and sequential like SI4684-DAB-Receiver, we compact the buffer
                    // so there are no 512-byte padding gaps between variable length segments!
                    uint32_t cursor = 0;
                    for (int i = 0; i < mot_total_segments; i++) {
                        if (cursor != i * SLS_MAX_SEG_SIZE) {
                            std::memmove(&mot_segment_buf[cursor], &mot_segment_buf[i * SLS_MAX_SEG_SIZE], mot_segment_len[i]);
                        }
                        cursor += mot_segment_len[i];
                    }

                    slideshow->image_data = mot_segment_buf;
                    slideshow->image_size = calc_len;
                    slideshow->transport_id = mot_transport_id;
                    slideshow->is_new = 1;
                    return 1;
                }
            }
        }
    }

    return 0;
    }

    int si468x_get_chip_info(si468x_chip_info_t* info)
{
    if (!info) {
        return -1;
    }
    uint8_t rev_cmd[1] = { 0x10 };
    uint8_t rev_resp[16];
    std::memset(rev_resp, 0, sizeof(rev_resp));
    if (send_command(rev_cmd, 1, rev_resp, 16) != SI468X_SUCCESS) {
        return -1;
    }
    info->chip_id = rev_resp[5];
    info->rom_id = rev_resp[6];
    info->fw_version = (rev_resp[7] << 8) | rev_resp[8];
    info->patch_version = (rev_resp[9] << 8) | rev_resp[10];
    return SI468X_SUCCESS;
}

int si468x_set_frequency_table(const uint32_t* freqs, int count)
{
    if (!freqs || count <= 0 || count > 48) {
        return -1;
    }

    // Prepare command packet
    size_t cmd_len = 4 + (count * 4);
    std::vector<uint8_t> cmd(cmd_len, 0);
    cmd[0] = 0xB8; // Opcode: DAB_SET_FREQ_LIST
    cmd[1] = count;

    for (int i = 0; i < count; i++) {
        uint32_t f = freqs[i];
        cmd[4 + i * 4]     = f & 0xFF;
        cmd[5 + i * 4]     = (f >> 8) & 0xFF;
        cmd[6 + i * 4]     = (f >> 16) & 0xFF;
        cmd[7 + i * 4]     = (f >> 24) & 0xFF;
        custom_freqs[i] = f;
    }
    custom_freq_count = count;

    if (send_command(cmd.data(), cmd_len, nullptr, 0) != SI468X_SUCCESS) {
        return -1;
    }
    return SI468X_SUCCESS;
}

int si468x_tune_fm(uint32_t frequency_khz)
{
    // Command size = 7 bytes
    uint8_t cmd[7] = { 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint32_t freq_10khz = frequency_khz / 10;
    cmd[2] = freq_10khz & 0xFF;
    cmd[3] = (freq_10khz >> 8) & 0xFF;

    if (send_command(cmd, 7, nullptr, 0) != SI468X_SUCCESS) {
        return -1;
    }

    // Give the RF synthesizer 300ms to lock and stabilize on-chip
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    return SI468X_SUCCESS;
}

int si468x_get_fm_status(si468x_fm_status_t* status)
{
    if (!status) {
        return -1;
    }

    uint8_t cmd[2] = { 0x32, 0x01 }; // Opcode 0x32 (FM_RSQ_STATUS) with INTACK = 1
    uint8_t resp[23];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 23) != SI468X_SUCCESS) {
        return -1;
    }

    uint32_t freq_10khz = resp[7] | (resp[8] << 8);
    status->frequency_hz = freq_10khz * 10000;
    status->rssi = resp[9];
    status->snr = resp[10];
    status->multipath = resp[11];
    status->freq_offset = (int8_t)resp[12];

    // HD synchronization flag: Bit 0 of Parameter Byte 1 (resp[5] & 0x01)
    status->hd_synced = resp[5] & 0x01;

    // RDS synchronization flag: Bit 0 of Parameter Byte 2 (resp[6] & 0x01)
    status->rds_synced = resp[6] & 0x01;

    return SI468X_SUCCESS;
}

int si468x_get_rds_text(char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return -1;
    }
    if (spi_fd < 0) {
        return -1;
    }

    bool updated = false;
    uint8_t cmd[2] = { 0x34, 0x00 }; // Opcode 0x34 (FM_RDS_STATUS) with INTACK = 0
    uint8_t resp[21];

    // Flush the RDS FIFO (up to 24 groups) and decode
    for (int i = 0; i < 24; i++) {
        std::memset(resp, 0, sizeof(resp));
        if (send_command(cmd, 2, resp, 21) != SI468X_SUCCESS) {
            break;
        }

        uint8_t rds_sync = resp[5] & 0x02;   // Bit 1 of Parameter Byte 1
        uint8_t fifo_used = resp[7] & 0x1F;  // Low 5 bits of Parameter Byte 3

        if (!rds_sync) {
            if (fifo_used == 0) {
                break;
            }
            continue;
        }

        // Combine blocks matching standard FMHD mode 0x34 (FM_RDS_STATUS) offsets
        uint16_t block_A = (resp[10] << 8) | resp[9];
        uint16_t block_B = (resp[16] << 8) | resp[15];
        uint16_t block_C = (resp[18] << 8) | resp[17];
        uint16_t block_D = (resp[20] << 8) | resp[19];

        // Parse block error codes
        uint8_t err_A = resp[12] >> 6;
        uint8_t err_B = (resp[12] >> 4) & 0x03;
        uint8_t err_C = (resp[12] >> 2) & 0x03;
        uint8_t err_D = resp[12] & 0x03;

        // Decode only if header blocks have no uncorrectable errors
        if (err_A < 3 && err_B < 3) {
            uint8_t old_toggle = rds_decoder.last_toggle;
            std::string old_text = rds_decoder.radio_text;

            rds_decoder.decode_block_A(block_A);
            rds_decoder.decode_block_B(block_B);

            if (err_C < 3) {
                rds_decoder.decode_block_C(block_C);
            }
            if (err_D < 3) {
                rds_decoder.decode_block_D(block_D);
            }

            if (rds_decoder.last_toggle != old_toggle || old_text != rds_decoder.radio_text) {
                updated = true;
            }
        }

        if (fifo_used == 0) {
            break;
        }
    }

    // Check if the current RadioText string is complete (no un-compiled 'holes' i.e. '\0' bytes before the end of the string)
    // Find the end marker (either '\r' or max 64)
    size_t check_len = 64;
    for (size_t i = 0; i < 64; i++) {
        if (rds_decoder.radio_text[i] == '\r') {
            check_len = i;
            break;
        }
    }

    // A string must have at least some non-space, non-null characters to be valid
    bool has_content = false;
    for (size_t i = 0; i < check_len; i++) {
        if (rds_decoder.radio_text[i] != '\0' && rds_decoder.radio_text[i] != ' ') {
            has_content = true;
            break;
        }
    }

    bool is_complete = false;
    if (has_content) {
        bool holes = false;
        for (size_t i = 0; i < check_len; i++) {
            if (rds_decoder.radio_text[i] == '\0') {
                holes = true;
                break;
            }
        }
        if (!holes) {
            is_complete = true;
        }
    }

    if (is_complete && !rds_decoder.complete_reported) {
        // Expand the raw RDS string (using standard ETSI EBU Latin 0 charset) into UTF-8
        decode_dab_string_to_utf8((const uint8_t*)rds_decoder.radio_text, check_len, 0, out_text, max_len);
        rds_decoder.complete_reported = true;
        return 1; // Return 1 to indicate a newly completed string has been assembled!
    }

    return 0; // Return 0 if the string is still compiling or has already been reported
}

int si468x_get_time(si468x_time_t* time)
{
    if (!time) {
        return -1;
    }

    // Write 2-byte command for DAB_GET_TIME (Opcode 0xBC, Param Byte 0 = 0x01)
    uint8_t cmd[2] = { 0xBC, 0x01 };
    uint8_t resp[15];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 15) != SI468X_SUCCESS) {
        return -1;
    }

    // Parse time fields from standard offsets (Parameter Bytes 9 to 14)
    time->year = resp[9] | ((uint16_t)resp[10] << 8);
    time->month = resp[11];
    time->day = resp[12];
    time->hours = resp[13];
    time->minutes = resp[14];

    return SI468X_SUCCESS;
    }

    int si468x_get_audio_info(si468x_audio_info_t* info)
    {
    if (!info) {
        return -1;
    }

    // Write 2-byte command for DAB_GET_AUDIO_INFO (Opcode 0xBD, INTACK = 0x00)
    uint8_t cmd[2] = { 0xBD, 0x00 };
    uint8_t resp[16];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 16) != SI468X_SUCCESS) {
        return -1;
    }

    // Check Bit 7 of Parameter Byte 1 (resp[5] in full-duplex) to see if audio info is valid
    if (resp[1] == 0x80) { // The SI4684 codebase expects SPIbuffer[1] == 0x80 for valid audio
        info->bitrate = resp[5] | ((uint16_t)resp[6] << 8);
        info->sample_rate = resp[7] | ((uint16_t)resp[8] << 8);
        info->audio_mode = resp[9] & 0x03;
        return SI468X_SUCCESS;
    }

    // Info not valid or stream not locked
    info->bitrate = 0;
    info->sample_rate = 0;
    info->audio_mode = 0;
    return SI468X_SUCCESS; // Not an SPI error, just not playing
    }

    int si468x_get_event_status(si468x_event_status_t* status)
{
    if (!status) {
        return -1;
    }

    // Write 2-byte command for DAB_GET_EVENT_STATUS (Opcode 0x12, Param Byte 0 = 0x01 to clear INTACK)
    uint8_t cmd[2] = { 0x12, 0x01 };
    uint8_t resp[6];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 6) != SI468X_SUCCESS) {
        return -1;
    }

    // Extract event status bits from Parameter Byte 1 (resp[5])
    status->reconf = resp[5] & 0x01; // Bit 0 represents database reconfigurations
    status->annc = (resp[5] >> 1) & 0x01; // Bit 1 represents announcement alerts

    return SI468X_SUCCESS;
}

int si468x_set_rds_region(int region)
{
    // Configure FMHD_DEEMPHASIS property (0x3900) dynamically
    uint16_t deemph_val = (region == SI468X_REGION_EUROPE) ? 0x3900 : 0x0000; // 0x3900 = 50us (Europe), 0x0000 = 75us (US default)

    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x00; // Low byte of Property 0x3900 (FMHD_DEEMPHASIS)
    cmd[3] = 0x39; // High byte of Property 0x3900 (FMHD_DEEMPHASIS)
    cmd[4] = deemph_val & 0xFF;         // Low byte of Value
    cmd[5] = (deemph_val >> 8) & 0xFF;  // High byte of Value

    if (send_command(cmd, 6, nullptr, 0) != SI468X_SUCCESS) {
        return -1;
    }

    return SI468X_SUCCESS;
}

const char* si468x_get_protection_text(uint8_t level)
{
    static const char* const protection_text[] = {
        "", "UEP-1", "UEP-2", "UEP-3", "UEP-4", "UEP-5",
        "EEP-A1", "EEP-A2", "EEP-A3", "EEP-A4", "EEP-B1", "EEP-B2", "EEP-B3", "EEP-B4"
    };
    if (level < sizeof(protection_text) / sizeof(protection_text[0])) {
        return protection_text[level];
    }
    return "";
}

const char* si468x_get_audio_mode_text(uint8_t mode)
{
    static const char* const audio_mode_text[] = {
        "Dual", "Mono", "Stereo", "Joint stereo"
    };
    if (mode < sizeof(audio_mode_text) / sizeof(audio_mode_text[0])) {
        return audio_mode_text[mode];
    }
    return "";
}

const char* si468x_get_service_type_text(uint8_t type)
{
    static const char* const service_type_text[] = {
        "TPEG", "Data", "FIDC", "MSC", "DAB+", "DAB", "FIC", "XPAD", "-"
    };
    if (type < sizeof(service_type_text) / sizeof(service_type_text[0])) {
        return service_type_text[type];
    }
    return "";
}

int si468x_fm_seek_start(int seek_up, int wrap)
{
    // Write 2-byte command for FM_SEEK_START (Opcode 0x31)
    uint8_t cmd[2];
    cmd[0] = SI468X_CMD_FM_SEEK_START;
    cmd[1] = ((seek_up & 0x01) << 1) | (wrap & 0x01);

    if (send_command(cmd, 2, nullptr, 0, 5000) != SI468X_SUCCESS) {
        return -1;
    }

    return SI468X_SUCCESS;
}

int si468x_dab_get_announcement_support(uint32_t service_id, uint32_t component_id, uint16_t* asw_flags)
{
    if (!asw_flags) {
        return -1;
    }

    // Write 12-byte command for DAB_GET_ANNOUNCEMENT_SUPPORT_INFO (Opcode 0xB5)
    uint8_t cmd[12] = {
        0xB5, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };

    uint8_t resp[6];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 12, resp, 6) != SI468X_SUCCESS) {
        return -1;
    }

    *asw_flags = resp[4] | ((uint16_t)resp[5] << 8);
    return SI468X_SUCCESS;
}

int si468x_dab_get_announcement_info(int buf_empty, uint32_t* service_id, uint32_t* component_id, uint16_t* asw_flags)
{
    // Write 2-byte command for DAB_GET_ANNOUNCEMENT_INFO (Opcode 0xB6)
    uint8_t cmd[2];
    cmd[0] = 0xB6;
    cmd[1] = buf_empty & 0x01;

    uint8_t resp[14];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 14) != SI468X_SUCCESS) {
        return -1;
    }

    if (service_id) {
        *service_id = resp[4] | ((uint32_t)resp[5] << 8) | ((uint32_t)resp[6] << 16) | ((uint32_t)resp[7] << 24);
    }
    if (component_id) {
        *component_id = resp[8] | ((uint32_t)resp[9] << 8) | ((uint32_t)resp[10] << 16) | ((uint32_t)resp[11] << 24);
    }
    if (asw_flags) {
        *asw_flags = resp[12] | ((uint16_t)resp[13] << 8);
    }

    return SI468X_SUCCESS;
}

int si468x_dab_get_service_linking(uint32_t service_id, uint8_t* link_info, int max_len)
{
    if (!link_info || max_len <= 0) {
        return -1;
    }

    // Write 6-byte command for DAB_GET_SERVICE_LINKING_INFO (Opcode 0xB7)
    uint8_t cmd[6];
    cmd[0] = 0xB7;
    cmd[1] = 0x00;
    cmd[2] = service_id & 0xFF;
    cmd[3] = (service_id >> 8) & 0xFF;
    cmd[4] = (service_id >> 16) & 0xFF;
    cmd[5] = (service_id >> 24) & 0xFF;

    uint8_t resp[256];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 6, resp, 256) != SI468X_SUCCESS) {
        return -1;
    }

    uint16_t data_size = resp[4] | ((uint16_t)resp[5] << 8);
    int copy_len = (data_size < max_len) ? data_size : max_len;
    std::memcpy(link_info, &resp[6], copy_len);

    return copy_len;
}

int si468x_fmhd_get_psd_text(int program, int field, char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return -1;
    }

    // Write 3-byte command for HD_GET_PSD_DECODE (Opcode 0x95)
    uint8_t cmd[3];
    cmd[0] = SI468X_CMD_HD_GET_PSD_DECODE;
    cmd[1] = program & 0xFF;
    cmd[2] = field & 0xFF;

    uint8_t resp[256];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 3, resp, 256) != SI468X_SUCCESS) {
        return -1;
    }

    uint8_t length = resp[7];
    uint8_t charset = resp[6];

    if (length == 0) {
        out_text[0] = '\0';
        return 0;
    }

    // Decode utilizing our universal decoder engine
    decode_dab_string_to_utf8(&resp[8], length, charset, out_text, max_len);
    return 1;
}

int si468x_fmhd_get_station_info(int info_select, char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return -1;
    }

    // Write 2-byte command for HD_GET_STATION_INFO (Opcode 0x94)
    uint8_t cmd[2];
    cmd[0] = SI468X_CMD_HD_GET_STATION_INFO;
    cmd[1] = info_select & 0xFF;

    uint8_t resp[256];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 256) != SI468X_SUCCESS) {
        return -1;
    }

    uint16_t length = resp[4] | ((uint16_t)resp[5] << 8);
    if (length == 0) {
        out_text[0] = '\0';
        return 0;
    }

    int copy_len = (length < max_len - 1) ? length : max_len - 1;
    std::memcpy(out_text, &resp[6], copy_len);
    out_text[copy_len] = '\0';

    return 1;
}

int si468x_dab_get_subchan_info(uint32_t service_id, uint32_t component_id, si468x_subchan_info_t* info)
{
    if (!info) {
        return -1;
    }

    // Write 12-byte command for DAB_GET_SUBCHAN_INFO (Opcode 0xBE)
    uint8_t cmd[12] = {
        SI468X_CMD_DAB_GET_SUBCHAN_INFO, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };

    uint8_t resp[12];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 12, resp, 12) != SI468X_SUCCESS) {
        return -1;
    }

    info->service_mode = resp[4];
    info->protection_info = resp[5];
    info->bitrate = resp[6] | ((uint16_t)resp[7] << 8);
    info->num_cu = resp[8] | ((uint16_t)resp[9] << 8);
    info->cu_address = resp[10] | ((uint16_t)resp[11] << 8);

    return SI468X_SUCCESS;
}

int si468x_fmhd_get_alert_message(char* alert_text, int max_len)
{
    if (!alert_text || max_len <= 0) {
        return -1;
    }

    // Write 2-byte command for HD_GET_ALERT_MSG (Opcode 0x96)
    uint8_t cmd[2] = { SI468X_CMD_HD_GET_ALERT_MSG, 0x00 };
    uint8_t resp[256];
    std::memset(resp, 0, sizeof(resp));

    if (send_command(cmd, 2, resp, 256) != SI468X_SUCCESS) {
        return -1;
    }

    uint16_t length = resp[4] | ((uint16_t)resp[5] << 8);
    if (length == 0) {
        alert_text[0] = '\0';
        return 0;
    }

    int copy_len = (length < max_len - 1) ? length : max_len - 1;
    std::memcpy(alert_text, &resp[6], copy_len);
    alert_text[copy_len] = '\0';

    return 1;
}
