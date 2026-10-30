/*
 * SPDX-FileCopyrightText: 2026 Fridrich Strba
 * SPDX-License-Identifier: MPL-2.0
 *    si468x.cpp - Hardware Driver Core Implementation for libsi468x
 *    resp[] indexing: resp[0] is clocked in with the RD_REPLY opcode (garbage), status byte 0 is resp[1]
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
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
static uint16_t boot_part = 0; // GET_PART_INFO PART after boot (4684, 4688, 4689 ...)
static uint32_t playing_sid = 0, playing_comp = 0; // STOP_DIGITAL_SERVICE needs them; zeros -> ERR

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

static si468x_transport_fn test_transport = nullptr;
static void* test_transport_ctx = nullptr;

void si468x_set_test_transport(si468x_transport_fn fn, void* ctx)
{
    test_transport = fn;
    test_transport_ctx = ctx;
}

static bool bus_open(void)
{
    return spi_fd >= 0 || test_transport;
}

/* Low-level SPI transfer helper */
static int spi_transfer(const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (test_transport) {
        return test_transport(tx, rx, length, test_transport_ctx);
    }
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

static uint8_t last_status[7];

uint8_t si468x_last_status(int i)
{
    return (i >= 0 && i < 7) ? last_status[i] : 0;
}

/* One RD_REPLY. Reply buffer is re-readable: every read restarts at status byte 0. */
int read_reply(uint8_t* resp, size_t len)
{
    std::vector<uint8_t> tx(len, 0x00); // tx[0] = RD_REPLY opcode
    if (spi_transfer(tx.data(), resp, len) < 0) {
        return SI468X_ERROR_SPI;
    }
    return SI468X_SUCCESS;
}

/*
 * Write command, poll CTS with 7-byte RD_REPLYs until timeout_ms, then read resp_len bytes.
 * ERR -> SI468X_ERROR_COMMAND (resp still read, resp[5] = error code) unless SC_ERR_OK.
 */
int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms, unsigned flags)
{
    if (spi_transfer(cmd, nullptr, cmd_len) < 0) {
        return SI468X_ERROR_SPI;
    }

    auto start_time = std::chrono::steady_clock::now();
    int delay_us = 50;
    for (;;) {
        uint8_t poll[7];
        if (read_reply(poll, sizeof(poll)) != SI468X_SUCCESS) {
            return SI468X_ERROR_SPI;
        }
        if (poll[1] & SI468X_CTS_MASK) {
            std::memcpy(last_status, poll, sizeof(last_status));
            break;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time).count();
        if (elapsed >= timeout_ms) {
            return SI468X_ERROR_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        delay_us = std::min(delay_us * 2, 1000);
    }

    if (resp && resp_len > 0 && read_reply(resp, resp_len) != SI468X_SUCCESS) {
        return SI468X_ERROR_SPI;
    }

    if ((last_status[1] & 0x40) && !(flags & SC_ERR_OK)) {
        SI468X_ERR << "libsi468x: Command 0x" << std::hex << (int)cmd[0]
                   << " error (status 0x" << (int)last_status[1]
                   << ", code 0x" << (int)last_status[5] << ")" << std::dec << std::endl;
        return SI468X_ERROR_COMMAND;
    }
    return SI468X_SUCCESS;
}

/* Poll status until STCINT or timeout */
int wait_stc(int timeout_ms)
{
    auto start_time = std::chrono::steady_clock::now();
    for (;;) {
        uint8_t st[7];
        if (read_reply(st, sizeof(st)) != SI468X_SUCCESS) {
            return SI468X_ERROR_SPI;
        }
        if (st[1] & 0x01) {
            return SI468X_SUCCESS;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time).count();
        if (elapsed >= timeout_ms) {
            return SI468X_ERROR_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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
    char program_service[9];
    uint8_t ps_segments_received;
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
        std::memset(program_service, ' ', 8);
        program_service[8] = '\0';
        ps_segments_received = 0;
        last_segment = -1;
        last_toggle = 0;
        group_type = 0;
        group_type_B = 0;
        traffic_program = 0;
        program_type = 0;
        block_B_low_5 = 0;
        complete_reported = false;
    }

    void reset()
    {
        std::memset(radio_text, 0, sizeof(radio_text));
        std::memset(program_service, ' ', 8);
        program_service[8] = '\0';
        ps_segments_received = 0;
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

    void decode_PS_block(uint8_t segment_address, uint16_t block_D)
    {
        uint8_t char1 = block_D >> 8;
        uint8_t char2 = block_D & 0xFF;

        if (segment_address < 4) {
            int pos = segment_address * 2;
            if (is_allowed_RT_char(char1)) {
                program_service[pos] = char1;
            }
            if (is_allowed_RT_char(char2)) {
                program_service[pos + 1] = char2;
            }
            ps_segments_received |= (1 << segment_address); // Mark segment as received
        }
    }

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
        else if (group_type == 0) {
            uint8_t segment_address = block_B_low_5 & 0x03;
            decode_PS_block(segment_address, block_D);
        }
    }
};

static RDS_decode rds_decoder;

static uint32_t custom_freqs[48];
static int custom_freq_count = 0;

/* Public C-API Implementation */

int si468x_init(const char* spi_device, int rst_pin, int boot_mode)
{
    // Refuse before touching the chip: dummy arrays would "upload" nothing
#if defined(SI468X_HAS_FIRMWARE) && !SI468X_HAS_FIRMWARE
    SI468X_ERR << "libsi468x: built with --disable-firmware" << std::endl;
    return SI468X_ERROR_FIRMWARE;
#endif
    if (fw_rom_patch_bin_len == 0 || fw_dab_radio_bin_len == 0 || fw_fmhd_radio_bin_len == 0) {
        return SI468X_ERROR_FIRMWARE;
    }

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
    if ((si468x_last_status(4) & 0xC0) != 0x80) { // PUP_STATE 2 = bootloader
        SI468X_ERR << "libsi468x: not in bootloader after POWER_UP (status 0x" << std::hex
                   << (int)si468x_last_status(4) << std::dec << ")" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_BOOT;
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

    // 7. BOOT (2 bytes), then PUP_STATE 3 = application running
    uint8_t boot_cmd[2] = { SI468X_CMD_BOOT, 0x00 };
    uint8_t st[7] = {};
    bool running = send_command(boot_cmd, 2, nullptr, 0, 2000) == SI468X_SUCCESS;
    for (int i = 0; running && i < 100; i++) { // CTS may precede PUP_STATE 3; allow 1 s
        if (read_reply(st, sizeof(st)) != SI468X_SUCCESS) {
            running = false;
        }
        else if ((st[4] & 0xC0) == 0xC0) {
            break;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (!running || (st[4] & 0xC0) != 0xC0 || (st[1] & 0x40)) {
        SI468X_ERR << "libsi468x: BOOT failed (status 0x" << std::hex << (int)st[1] << " 0x" << (int)st[4]
                   << std::dec << ")" << std::endl;
        si468x_shutdown();
        return SI468X_ERROR_BOOT;
    }

    // Wait for the on-chip application operating system to boot and stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    if (apply_audio_output(active_audio_mode) != SI468X_SUCCESS) {
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

    si468x_chip_info_t ci;
    if (si468x_get_chip_info(&ci) == SI468X_SUCCESS) {
        boot_part = ci.part;
        SI468X_LOG << "libsi468x: Si" << ci.part << " rev " << (int)ci.chip_rev << ", fw "
                   << (int)ci.fw_major << "." << (int)ci.fw_minor << "." << (int)ci.fw_build << std::endl;
    }

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

/* Deprecated no-op: no "clear" command exists ({0x80,0x02} was rejected with ERR); tune rebuilds the list */
int si468x_clear_service_list(void)
{
    return SI468X_SUCCESS;
}

/* Map center frequencies (in Hz) to the exact, firmware-defined co-processor channel indices */
/* -1 = not in table; never fall back to index 0 (5A) silently */
static int si468x_get_freq_index(uint32_t frequency_hz)
{
    if (custom_freq_count > 0) {
        for (int i = 0; i < custom_freq_count; i++) {
            if (custom_freqs[i] == frequency_hz) {
                return i;
            }
        }
        return -1;
    }

    // Official Silicon Labs DAB Ensemble Frequencies (Index 0 to 40)
    static const uint32_t standard_freqs[] = {
        174928000, // 0: 5A
        176640000, // 1: 5B
        178352000, // 2: 5C
        180064000, // 3: 5D
        181936000, // 4: 6A
        183648000, // 5: 6B
        185360000, // 6: 6C
        187072000, // 7: 6D
        188928000, // 8: 7A
        190640000, // 9: 7B
        192352000, // 10: 7C
        194064000, // 11: 7D
        195936000, // 12: 8A
        197648000, // 13: 8B
        199360000, // 14: 8C
        201072000, // 15: 8D
        202928000, // 16: 9A
        204640000, // 17: 9B
        206352000, // 18: 9C
        208064000, // 19: 9D
        209936000, // 20: 10A
        210096000, // 21: 10N
        211648000, // 22: 10B
        213360000, // 23: 10C
        215072000, // 24: 10D
        216928000, // 25: 11A
        217088000, // 26: 11N
        218640000, // 27: 11B
        220352000, // 28: 11C
        222064000, // 29: 11D
        223936000, // 30: 12A
        224096000, // 31: 12N
        225648000, // 32: 12B
        227360000, // 33: 12C
        229072000, // 34: 12D
        230784000, // 35: 13A
        232496000, // 36: 13B
        234208000, // 37: 13C
        235776000, // 38: 13D
        237488000, // 39: 13E
        239200000  // 40: 13F
    };

    for (int i = 0; i < 41; i++) {
        if (standard_freqs[i] == frequency_hz) {
            return i;
        }
    }

    return -1;
}

/*
 * Tune, STC, lock check, service list wait. Q4 (probe): STC ~0.5 s on 12A with VALID+ACQ already set,
 * ~2.1 s on an empty channel with VALID never set. Lock rule: VALID && ACQ once after STC.
 */
int si468x_set_frequency(uint32_t frequency_hz)
{
    int freq_index = si468x_get_freq_index(frequency_hz);
    if (freq_index < 0) {
        return SI468X_ERROR_PARAM;
    }
    active_frequency = 0;

    // DAB_TUNE_FREQ: index at byte 2, ANTCAP 0 = front-end calibration
    uint8_t cmd[6] = { SI468X_CMD_DAB_TUNE_FREQ, 0x00, (uint8_t)freq_index, 0x00, 0x00, 0x00 };

    // Tune while the chip is still busy -> ERR 0x18; let it finish, retry once
    int rc = send_command(cmd, 6, nullptr, 0, 5000);
    if (rc == SI468X_ERROR_COMMAND) {
        wait_stc(3000);
        rc = send_command(cmd, 6, nullptr, 0, 5000);
    }
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    rc = wait_stc(3000);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // ponytail: 500 ms grace beyond a single check, for marginal signals; drop if scans show no use
    bool locked = false;
    for (int i = 0; i < 6 && !locked; i++) {
        if (i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        si468x_signal_status_t st;
        locked = si468x_get_signal_status(&st) == SI468X_SUCCESS && st.sync_status;
    }
    if (!locked) {
        return SI468X_ERROR_TIMEOUT;
    }

    // Service list available (event SVRLIST), max 5 s
    for (int t = 0; t < 5000; t += 100) {
        si468x_dab_event_status_t ev;
        if (si468x_dab_get_event_status(&ev) == SI468X_SUCCESS && (ev.svrlist_int || ev.svrlist_active)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    active_frequency = frequency_hz;
    return SI468X_SUCCESS;
}

uint32_t si468x_get_frequency(void)
{
    return active_frequency;
}

static int si468x_enable_service_data(void)
{
    // DAB_VALID_* acquisition timing: chip defaults (were 63/12/2000/2000/2000)
    static const uint16_t props[][2] = {
        { SI468X_PROP_DAB_VALID_RSSI_TIME, 30 },
        { SI468X_PROP_DAB_VALID_RSSI_THRESHOLD, 12 },
        { SI468X_PROP_DAB_VALID_ACQ_TIME, 2000 },
        { SI468X_PROP_DAB_VALID_SYNC_TIME, 1200 },
        { SI468X_PROP_DAB_VALID_DETECT_TIME, 35 },
        { SI468X_PROP_DAB_XPAD_ENABLE, 0x003F }, // DLS + slideshow
    };
    for (auto& p : props) {
        si468x_set_property(p[0], p[1]);
    }
    return SI468X_SUCCESS;
}

static int si468x_enable_rds(void)
{
    uint8_t cmd[6];

    // Set Property 0x3C02 to 0x0041 (FM_RDS_CONFIG: Enable RDS block decoding with 1-2 correctable errors) (Little-Endian)
    cmd[0] = SI468X_CMD_SET_PROPERTY;
    cmd[1] = 0x00;
    cmd[2] = 0x02; // Low byte of Property 0x3C02
    cmd[3] = 0x3C; // High byte of Property 0x3C02
    cmd[4] = 0x41; // Low byte of Value (0x41)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0x3C00 to 0x0001 (FM_RDS_INTERRUPT_SOURCE: Enable RDS received FIFO data polling) (Little-Endian)
    cmd[2] = 0x00; // Low byte of Property 0x3C00
    cmd[3] = 0x3C; // High byte of Property 0x3C00
    cmd[4] = 0x01; // Low byte of Value (0x01)
    cmd[5] = 0x00; // High byte of Value (0x00)
    send_command(cmd, 6, nullptr, 0);

    // Set Property 0x3C01 to 0x0001 (FM_RDS_INT_FIFO_COUNT: Set interrupt/poll threshold = 1 group) (Little-Endian)
    cmd[2] = 0x01; // Low byte of Property 0x3C01
    cmd[3] = 0x3C; // High byte of Property 0x3C01
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
        playing_sid = service_id;
        playing_comp = component_id;

        // Re-apply DAB_VALID timing + XPAD enable (DLS/slideshow)
        si468x_enable_service_data();

        return SI468X_SUCCESS;
    }

    return SI468X_ERROR_SPI;
}

int si468x_stop_service(void)
{
    SI468X_LOG << "libsi468x: Stopping service playback..." << std::endl;

    // Same layout as START_DIGITAL_SERVICE: SId, CompId of the playing service
    uint8_t cmd[12] = {
        SI468X_CMD_STOP_DIGITAL_SERVICE, 0x00, 0x00, 0x00,
        (uint8_t)(playing_sid & 0xFF),
        (uint8_t)((playing_sid >> 8) & 0xFF),
        (uint8_t)((playing_sid >> 16) & 0xFF),
        (uint8_t)((playing_sid >> 24) & 0xFF),
        (uint8_t)(playing_comp & 0xFF),
        (uint8_t)((playing_comp >> 8) & 0xFF),
        (uint8_t)((playing_comp >> 16) & 0xFF),
        (uint8_t)((playing_comp >> 24) & 0xFF)
    };

    // Send first attempt with SCIdS = 0 (reads 0 response parameter bytes, matches radio_cli!)
    int ret = send_command(cmd, 12, nullptr, 0);

    if (ret != SI468X_SUCCESS) {
        // Try fallback attempt with SCIdS = 1 just like native play_station()
        cmd[1] = 0x01;
        ret = send_command(cmd, 12, nullptr, 0);
    }

    if (ret == SI468X_SUCCESS) {
        playing_sid = playing_comp = 0;
    }
    return ret;
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

// Append one code point as UTF-8; false (nothing written) if it would not fit before the NUL
static bool put_utf8(char* out, int& idx, int max_out_len, uint16_t cp)
{
    int n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
    if (idx + n >= max_out_len) {
        return false;
    }
    if (n == 1) {
        out[idx++] = (char)cp;
    }
    else if (n == 2) {
        out[idx++] = 0xC0 | (cp >> 6);
        out[idx++] = 0x80 | (cp & 0x3F);
    }
    else {
        out[idx++] = 0xE0 | (cp >> 12);
        out[idx++] = 0x80 | ((cp >> 6) & 0x3F);
        out[idx++] = 0x80 | (cp & 0x3F);
    }
    return true;
}

/*
 * DAB/RDS label -> NUL-terminated UTF-8, truncated on a character boundary to fit max_out_len.
 * Charset 0 EBU Latin, 6 UCS-2 BE, 15 UTF-8; others passed through. CR/LF -> space, other controls dropped.
 */
static void decode_dab_string_to_utf8(const uint8_t* in, int in_len, uint8_t charset, char* out, int max_out_len)
{
    if (max_out_len <= 0) {
        return;
    }
    int out_idx = 0;
    int step = (charset == 6) ? 2 : 1;

    for (int i = 0; i + step <= in_len; i += step) {
        uint16_t cp = (step == 2) ? ((in[i] << 8) | in[i + 1]) : in[i];
        if (cp == 0) {
            break;
        }
        if (cp < 0x20) {
            if ((cp == 0x0A || cp == 0x0D) && !put_utf8(out, out_idx, max_out_len, ' ')) {
                break;
            }
            continue;
        }
        if (charset == 0 && cp >= 0x80 && cp <= 0x9F) {
            cp = ebu_latin_80_9F[cp - 0x80];
        }
        if (step == 1 && charset != 0) {
            // Raw bytes; for UTF-8 keep a multi-byte sequence whole
            int n = 1;
            if (charset == 15 && cp >= 0xC0) {
                n = cp >= 0xE0 ? (cp >= 0xF0 ? 4 : 3) : 2;
            }
            if (out_idx + n >= max_out_len || i + n > in_len) {
                break;
            }
            for (int k = 0; k < n; k++) {
                out[out_idx++] = in[i + k];
            }
            i += n - 1;
            continue;
        }
        if (!put_utf8(out, out_idx, max_out_len, cp)) {
            break;
        }
    }
    out[out_idx] = '\0';
}

void si468x_decode_short_label(const uint8_t* raw_label, int raw_len, uint16_t char_mask, uint8_t charset, char* short_label, int max_len)
{
    uint8_t temp_raw[16];
    int dst = 0;
    int w = (charset == 6) ? 2 : 1; // mask bits index characters; UCS-2 has 2 bytes each

    // Bit 15 = first character; at most 8 characters
    for (int i = 0; i < 16 && (i + 1) * w <= raw_len && dst < 8 * w; i++) {
        if (char_mask & (0x8000 >> i)) {
            for (int k = 0; k < w; k++) {
                temp_raw[dst++] = raw_label[i * w + k];
            }
        }
    }
    decode_dab_string_to_utf8(temp_raw, dst, charset, short_label, max_len);
}

static uint16_t le16(const uint8_t* p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Variable reply: header read, then one re-read of header + payload (no second query) */
static int reread(std::vector<uint8_t>& resp, size_t total)
{
    if (total > SI468X_REP_MAX_VAR) {
        return SI468X_ERROR_INVALID_REPLY;
    }
    resp.assign(total, 0);
    return read_reply(resp.data(), total);
}

int si468x_get_ensemble_info(si468x_ensemble_info_t* info)
{
    if (!info) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_GET_ENSEMBLE_INFO, 0x00 };
    uint8_t resp[SI468X_REP_ENSEMBLE_INFO] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // EID resp[5..6], LABEL resp[7..22], ECC resp[23], CHARSET resp[24], CHAR_ABREV resp[25..26]
    info->eid = le16(&resp[5]);
    info->ecc = resp[23];
    info->charset = resp[24] & 0x0F;
    info->char_mask = le16(&resp[25]);
    decode_dab_string_to_utf8(&resp[7], 16, info->charset, info->label, sizeof(info->label));
    si468x_decode_short_label(&resp[7], 16, info->char_mask, info->charset, info->short_label, sizeof(info->short_label));
    return SI468X_SUCCESS;
}

int si468x_get_component_info(uint32_t service_id, uint32_t component_id, char* label, size_t label_len,
                              char* short_label, size_t short_len, uint16_t* out_char_mask)
{
    if (!label || label_len == 0 || (short_label && short_len == 0)) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[12] = {
        SI468X_CMD_DAB_GET_COMPONENT_INFO, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF),
        (uint8_t)(component_id & 0xFF),
        (uint8_t)((component_id >> 8) & 0xFF),
        (uint8_t)((component_id >> 16) & 0xFF),
        (uint8_t)((component_id >> 24) & 0xFF)
    };
    uint8_t resp[SI468X_REP_COMPONENT_INFO] = {};

    label[0] = '\0';
    int ret = send_command(cmd, 12, resp, sizeof(resp), 1000, SC_ERR_OK);
    if (ret != SI468X_SUCCESS) {
        return ret;
    }
    if (resp[1] & 0x40) {
        return SI468X_ERROR_NOT_FOUND; // component has no info/label
    }

    // LANG resp[7], CHARSETID resp[8], LABEL resp[9..24], CHAR_ABREV resp[25..26]
    uint8_t charset = resp[8] & 0x3F;
    uint16_t char_mask = le16(&resp[25]);
    decode_dab_string_to_utf8(&resp[9], 16, charset, label, (int)std::min<size_t>(label_len, 4096));
    if (short_label) {
        si468x_decode_short_label(&resp[9], 16, char_mask, charset, short_label, (int)std::min<size_t>(short_len, 4096));
    }
    if (out_char_mask) {
        *out_char_mask = char_mask;
    }
    return SI468X_SUCCESS;
}

/*
 * List layout (DAB_Service_List_Handler.c): payload at resp[7]: version u16, NUM_SERVICES, 3 reserved;
 * per service 24 bytes (SId u32, PTY/PD, LOCAL/CAId/NUM_COMP, SICHARSET, rsvd, label[16]),
 * then NUM_COMP x 4 bytes (component word u16, ASCTy/DSCTy/PS/CA, flags).
 * One entry per service: first TMID 0 (audio stream) component, else first component as data.
 */
int si468x_parse_service_list(const uint8_t* resp, size_t len, si468x_service_t* list, int max_services)
{
    if (len < 13) {
        return SI468X_ERROR_INVALID_REPLY;
    }
    int num_services = resp[9];
    int count = 0;
    size_t o = 13;
    bool truncated = false;

    for (int i = 0; i < num_services && count < max_services; i++) {
        if (o + 24 > len) {
            truncated = true;
            break;
        }
        size_t c = o + 24;
        size_t first = 0, audio = 0;
        int ncomp = resp[o + 5] & 0x0F;
        for (int k = 0; k < ncomp; k++, c += 4) {
            if (c + 4 > len) {
                truncated = true;
                break;
            }
            uint16_t w = le16(&resp[c]);
            if (!first) {
                first = c;
            }
            if (!audio && (w >> 14) == 0) {
                audio = c;
            }
        }
        if (truncated) {
            break;
        }

        if (first) {
            size_t pick = audio ? audio : first;
            uint16_t w = le16(&resp[pick]);
            si468x_service_t* e = &list[count++];
            std::memset(e, 0, sizeof(*e));
            e->service_id = le32(&resp[o]);
            e->component_id = w;
            e->tmid = w >> 14;
            e->subchannel_id = (e->tmid <= 1) ? (w & 0x3F) : 0;
            e->audio_type = (resp[pick + 2] >> 2) & 0x3F;
            e->pty = (resp[o + 4] >> 1) & 0x1F;
            e->is_data_service = audio ? 0 : 1;
            e->num_components = ncomp;
            e->charset = resp[o + 6] & 0x0F;

            decode_dab_string_to_utf8(&resp[o + 8], 16, e->charset, e->label, sizeof(e->label));

            // List has no CHAR_ABREV: first 8 characters, trailing spaces stripped
            si468x_decode_short_label(&resp[o + 8], 16, 0xFF00, e->charset, e->short_label, sizeof(e->short_label));
            for (size_t n = std::strlen(e->short_label); n > 0 && e->short_label[n - 1] == ' '; n--) {
                e->short_label[n - 1] = '\0';
            }
        }
        o = c;
    }
    return (count == 0 && truncated) ? SI468X_ERROR_INVALID_REPLY : count;
}

int si468x_get_service_list(si468x_service_t* list, int max_services)
{
    if (!list || max_services <= 0) {
        return 0;
    }

    // Header: SIZE at resp[5..6]; ERR or SIZE 0 = list not ready
    uint8_t cmd[2] = { SI468X_CMD_GET_DIGITAL_SERVICE_LIST, 0x00 };
    uint8_t hdr[SI468X_REP_DSL_HDR] = {};
    int rc = send_command(cmd, 2, hdr, sizeof(hdr), 1000, SC_ERR_OK);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    uint16_t size = le16(&hdr[5]);
    if ((hdr[1] & 0x40) || size == 0) {
        return 0;
    }
    if (size > SI468X_REP_MAX_VAR) {
        return SI468X_ERROR_INVALID_REPLY;
    }

    // Re-read header + payload (reply buffer is re-readable; no second query)
    std::vector<uint8_t> resp(SI468X_REP_DSL_HDR + size);
    rc = read_reply(resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    return si468x_parse_service_list(resp.data(), resp.size(), list, max_services);
}

int si468x_get_service_info(uint32_t service_id, si468x_service_info_t* info)
{
    if (!info) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[8] = {
        SI468X_CMD_DAB_GET_SERVICE_INFO, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF)
    };
    uint8_t resp[SI468X_REP_SERVICE_INFO] = {};
    std::memset(info, 0, sizeof(*info));

    int rc = send_command(cmd, 8, resp, sizeof(resp), 1000, SC_ERR_OK);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    if (resp[1] & 0x40) {
        return SI468X_ERROR_NOT_FOUND; // unknown SId (Q6)
    }

    // PTY resp[5] bits 1-5, SICHARSET resp[7] & 0x0F, ECC resp[8], LABEL resp[9..24]
    info->pty = (resp[5] >> 1) & 0x1F;
    info->charset = resp[7] & 0x0F;
    info->ecc = resp[8];
    decode_dab_string_to_utf8(&resp[9], 16, info->charset, info->label, sizeof(info->label));
    info->char_mask = le16(&resp[25]);
    si468x_decode_short_label(&resp[9], 16, info->char_mask, info->charset, info->short_label, sizeof(info->short_label));
    return SI468X_SUCCESS;
}

int si468x_get_signal_status(si468x_signal_status_t* status)
{
    if (!status) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_DIGRAD_STATUS, 0x00 }; // no ACKs
    uint8_t resp[SI468X_REP_DIGRAD_STATUS] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // resp[5] = interrupt bits (ignored); resp[6]: FICERR 0x08, ACQ 0x04, VALID 0x01
    status->rssi = resp[7];
    int8_t snr = (int8_t)resp[8]; // negative while acquiring
    status->snr = snr > 0 ? snr : 0;
    status->fic_quality = resp[9];
    status->cnr = resp[10];
    status->fib_errors = le16(&resp[11]);
    status->tune_index = resp[17];
    status->freq_offset = (int8_t)resp[18]; // raw FFT_OFFSET, unit undocumented
    status->antcap = le16(&resp[19]);
    status->sync_status = (resp[6] & 0x05) == 0x05; // VALID && ACQ

    return SI468X_SUCCESS;
}

// The chip takes the output route only at boot: record it for si468x_init().
int si468x_set_audio_output(int enable_i2s)
{
    if (bus_open() || enable_i2s < SI468X_AUDIO_ANALOG || enable_i2s > SI468X_AUDIO_SIMUL) {
        return SI468X_ERROR_PARAM;
    }
    active_audio_mode = enable_i2s;
    return SI468X_SUCCESS;
}

int apply_audio_output(int mode)
{
    // PIN_CONFIG_ENABLE: INTBOUTEN 0x8000 (default on), I2SOUTEN 0x0002, DACOUTEN 0x0001
    bool i2s = (mode == SI468X_AUDIO_I2S || mode == SI468X_AUDIO_SIMUL);
    uint16_t pins = 0x8000 | (i2s ? 0x0002 : 0) | (mode != SI468X_AUDIO_I2S ? 0x0001 : 0);
    int ret = si468x_set_property(SI468X_PROP_PIN_CONFIG_ENABLE, pins);
    if (ret != SI468X_SUCCESS || !i2s) {
        return ret;
    }

    // DIGITAL_IO_OUTPUT_SELECT: MASTER 0x8000. OUTPUT_FORMAT: 16-bit sample (0x1000), slot = sample size, I2S framing
    ret = si468x_set_property(SI468X_PROP_DIGITAL_IO_OUTPUT_SELECT, 0x8000);
    return ret != SI468X_SUCCESS ? ret : si468x_set_property(SI468X_PROP_DIGITAL_IO_OUTPUT_FORMAT, 0x1000);
}

/*
 * GET_DIGITAL_SERVICE_DATA dispatcher (one reader for all consumers): each 0x84 ACK pops one
 * packet, so DLS and MOT must share one reader or they steal each other's packets.
 * Header (lib idx): DSRVPCKTINT resp[5]&1, BUFF_COUNT resp[6], DATA_SRC resp[8]>>6 (1 PAD data, 2 PAD DLS),
 * DSCTY resp[8]&0x3F, SId resp[9..12], CompId resp[13..16], BYTE_COUNT resp[19..20], payload resp[25].
 * Single consumer: static state, no locking.
 */
static std::string dls_text;
static bool dls_changed = false;

static void dls_feed(const uint8_t* p, uint16_t n)
{
    if (n < 2) {
        return;
    }
    if (p[0] & 0x10) {
        // Command: 1 = clear display, 2 = DL Plus (ignored)
        if ((p[0] & 0x0F) == 0x01 && !dls_text.empty()) {
            dls_text.clear();
            dls_changed = true;
        }
        return;
    }
    uint8_t charset = (p[1] >> 4) & 0x0F;
    if (charset == 4) {
        charset = 6; // DLS charset 4 is UCS-2
    }
    char buf[400]; // 128 chars, up to 3 UTF-8 bytes each
    decode_dab_string_to_utf8(p + 2, n - 2, charset, buf, sizeof(buf));
    if (dls_text != buf) {
        dls_text = buf;
        dls_changed = true;
    }
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
static si468x_mot_slideshow_t mot_done; // completed image, is_new = 1 until fetched

static void clear_mot_buffer()
{
    std::memset(mot_segment_len, 0, sizeof(mot_segment_len));
    std::memset(mot_segment_bitmap, 0, sizeof(mot_segment_bitmap));
    mot_transport_id = 0;
    mot_byte_counter = 0;
    mot_highest_segment = 0;
    mot_total_segments = 0;
    mot_expected_length = 0;
}

static bool all_mot_segments_received()
{
    unsigned segments_to_check = mot_total_segments ? mot_total_segments : mot_highest_segment + 1u;
    for (unsigned i = 0; i < segments_to_check; i++) {
        if (!(mot_segment_bitmap[i / 8] & (1 << (i % 8)))) {
            return false;
        }
    }
    return true;
}

static void mot_complete(uint32_t size)
{
    mot_total_segments = mot_highest_segment + 1;
    mot_done.image_data = mot_segment_buf;
    mot_done.image_size = size;
    mot_done.transport_id = mot_transport_id;
    mot_done.is_new = 1;
}

/* p = payload (old code's resp[25 + k] == p[k]), n = BYTE_COUNT */
static void mot_feed(const uint8_t* p, uint16_t n)
{
    uint32_t now = (uint32_t)time(NULL);
    if (mot_transport_id != 0 && mot_last_activity > 0 && (now - mot_last_activity) > 30) {
        clear_mot_buffer();
    }

    // MOT header
    if (n >= 13 && n < 200 && p[2] == 0x80 && p[3] == 0x00 && p[4] == 0x12) {
        uint16_t tid = (p[5] << 8) | p[6];
        uint32_t new_len = (((uint32_t)p[10] << 12) | ((uint32_t)p[11] << 4) | ((uint32_t)p[12] >> 4)) & 0x00FFFF;
        if (new_len > 0) {
            if (mot_expected_length == 0 || (mot_transport_id != 0 && tid != mot_transport_id)) {
                clear_mot_buffer();
                mot_expected_length = new_len;
                mot_transport_id = tid;
            }
            if (mot_byte_counter >= mot_expected_length && all_mot_segments_received()) {
                mot_complete(mot_expected_length);
            }
        }
    }
    // MOT body segment: data p[9 .. n-3] (2 trailing CRC bytes)
    else if (n >= 11 && (p[2] == 0x00 || p[2] == 0x80) && p[4] == 0x12) {
        uint16_t tid = (p[5] << 8) | p[6];
        uint8_t seg_num = p[3];
        if (mot_transport_id == 0) {
            mot_transport_id = tid;
        }
        if (tid != mot_transport_id) {
            return;
        }
        uint8_t byte_idx = seg_num / 8;
        uint8_t bit_idx = seg_num % 8;

        if (!(mot_segment_bitmap[byte_idx] & (1 << bit_idx))) {
            uint16_t data_len = n - 11;
            if (seg_num < SLS_MAX_SEGMENTS && data_len <= SLS_MAX_SEG_SIZE) {
                std::memcpy(&mot_segment_buf[seg_num * SLS_MAX_SEG_SIZE], &p[9], data_len);
                mot_segment_len[seg_num] = data_len;
                mot_segment_bitmap[byte_idx] |= (1 << bit_idx);
                mot_byte_counter += data_len;
                if (seg_num > mot_highest_segment) {
                    mot_highest_segment = seg_num;
                }
                mot_last_activity = now;
                if (mot_expected_length > 0 && mot_byte_counter >= mot_expected_length && all_mot_segments_received()) {
                    mot_complete(mot_expected_length);
                }
            }
        }
        else if (seg_num == 0 && mot_expected_length == 0 && mot_highest_segment > 0 && all_mot_segments_received()) {
            // No header seen but all segments present: compact out the 512-byte slot padding
            mot_total_segments = mot_highest_segment + 1;
            uint32_t cursor = 0;
            for (unsigned i = 0; i < mot_total_segments; i++) {
                if (cursor != i * SLS_MAX_SEG_SIZE) {
                    std::memmove(&mot_segment_buf[cursor], &mot_segment_buf[i * SLS_MAX_SEG_SIZE], mot_segment_len[i]);
                }
                cursor += mot_segment_len[i];
            }
            mot_complete(cursor);
        }
    }
}

/* Pop up to max_packets packets and route them; empty queue (ERR) is not an error */
static int pump_service_data(int max_packets)
{
    uint8_t cmd[2] = { SI468X_CMD_GET_DIGITAL_SERVICE_DATA, 0x01 };
    std::vector<uint8_t> resp(SI468X_REP_DSD_HDR);

    for (int k = 0; k < max_packets; k++) {
        resp.assign(SI468X_REP_DSD_HDR, 0);
        int rc = send_command(cmd, 2, resp.data(), resp.size(), 1000, SC_ERR_OK);
        if (rc != SI468X_SUCCESS) {
            return rc;
        }
        if ((resp[1] & 0x40) || !(resp[5] & 0x01)) {
            return SI468X_SUCCESS; // queue empty
        }
        uint16_t n = le16(&resp[19]);
        if (n > 0 && (rc = reread(resp, SI468X_REP_DSD_HDR + n)) != SI468X_SUCCESS) {
            return rc;
        }
        uint8_t src = resp[8] >> 6;
        if (src == 2) {
            dls_feed(&resp[SI468X_REP_DSD_HDR], n);
        }
        else if (src == 1) {
            mot_feed(&resp[SI468X_REP_DSD_HDR], n);
        }
        if (resp[6] == 0) { // BUFF_COUNT: nothing left
            break;
        }
    }
    return SI468X_SUCCESS;
}

int si468x_get_dls_text(char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }
    int rc = pump_service_data(8);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // Copy, truncated on a UTF-8 character boundary
    size_t n = std::min(dls_text.size(), (size_t)max_len - 1);
    while (n > 0 && n < dls_text.size() && ((uint8_t)dls_text[n] & 0xC0) == 0x80) {
        n--;
    }
    std::memcpy(out_text, dls_text.data(), n);
    out_text[n] = '\0';

    int changed = dls_changed ? 1 : 0;
    dls_changed = false;
    return changed;
}

int si468x_get_mot_slideshow(si468x_mot_slideshow_t* slideshow)
{
    if (!slideshow) {
        return SI468X_ERROR_PARAM;
    }
    slideshow->is_new = 0;
    int rc = pump_service_data(8);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    if (!mot_done.is_new) {
        return 0;
    }
    *slideshow = mot_done;
    mot_done.is_new = 0;
    return 1;
}

int si468x_get_chip_info(si468x_chip_info_t* info)
{
    if (!info) {
        return SI468X_ERROR_PARAM;
    }

    // GET_PART_INFO: CHIPREV resp[5], ROMID resp[6], PART resp[9..10]
    uint8_t part_cmd[2] = { SI468X_CMD_GET_PART_INFO, 0x00 };
    uint8_t part[SI468X_REP_PART_INFO] = {};
    int rc = send_command(part_cmd, 2, part, sizeof(part));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // GET_FUNC_INFO: REVEXT resp[5], REVBRANCH resp[6], REVINT resp[7], SVNID resp[9..12]
    uint8_t func_cmd[2] = { SI468X_CMD_GET_FUNC_INFO, 0x00 };
    uint8_t func[SI468X_REP_FUNC_INFO] = {};
    rc = send_command(func_cmd, 2, func, sizeof(func));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    info->chip_rev = part[5];
    info->rom_id = part[6];
    info->part = le16(&part[9]);
    info->fw_major = func[5];
    info->fw_minor = func[6];
    info->fw_build = func[7];
    info->svn_id = le32(&func[9]);
    return SI468X_SUCCESS;
}

int si468x_set_frequency_table(const uint32_t* freqs, int count)
{
    if (!freqs || count <= 0 || count > 48) {
        return SI468X_ERROR_PARAM;
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
    }

    int rc = send_command(cmd.data(), cmd_len, nullptr, 0);

    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // Commit only after chip accepted it; index lookup must match chip's table
    std::copy(freqs, freqs + count, custom_freqs);
    custom_freq_count = count;
    return SI468X_SUCCESS;
}

int si468x_tune_fm(uint32_t frequency_khz)
{
    rds_decoder.reset();

    // TUNE_MODE<<2: 0 analog, 2 hybrid (HD parts only, Si4689); FREQ in 10 kHz, ANTCAP 1, PROG_ID 0
    uint32_t f = frequency_khz / 10;
    uint8_t mode = (boot_part == 4689) ? (2 << 2) : 0;
    uint8_t cmd[7] = { SI468X_CMD_FM_TUNE_FREQ, mode, (uint8_t)(f & 0xFF), (uint8_t)(f >> 8), 0x01, 0x00, 0x00 };
    int rc = send_command(cmd, 7, nullptr, 0);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    rc = wait_stc(1000);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // STCACK
    uint8_t ack[2] = { SI468X_CMD_FM_RSQ_STATUS, 0x01 };
    return send_command(ack, 2, nullptr, 0);
}

static uint8_t rds_sync_last = 0; // RDSSYNC from the last FM_RDS_STATUS
static void si468x_update_rds_decoder(void);

int si468x_get_fm_status(si468x_fm_status_t* status)
{
    if (!status) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_FM_RSQ_STATUS, 0x00 };
    uint8_t resp[SI468X_REP_FM_RSQ_STATUS] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // resp[5] = interrupt bits (ignored); resp[6]: BLTF 0x80, HDDETECTED 0x20, AFCRL 0x02, VALID 0x01
    status->frequency_hz = le16(&resp[7]) * 10000u;
    status->freq_offset = (int8_t)resp[9];
    status->rssi = resp[10];
    status->snr = resp[11];
    status->multipath = resp[12];
    status->hd_synced = (resp[6] & 0x20) != 0;
    status->valid = resp[6] & 0x01;

    // RSQ has no RDS sync; take it from the RDS decoder (also drains the FIFO into it)
    si468x_update_rds_decoder();
    status->rds_synced = rds_sync_last;
    return SI468X_SUCCESS;
}

/*
 * Drain FM_RDS_STATUS (INTACK, dequeues one group per call).
 * RDSFIFOUSED counts groups including the returned one (1 = last group); 0 = empty, blocks stale.
 */
static void si468x_update_rds_decoder(void)
{
    if (!bus_open()) {
        return;
    }

    uint8_t cmd[2] = { SI468X_CMD_FM_RDS_STATUS, 0x01 };
    uint8_t resp[SI468X_REP_FM_RDS_STATUS];

    for (int i = 0; i < 24; i++) {
        if (send_command(cmd, 2, resp, sizeof(resp)) != SI468X_SUCCESS) {
            break;
        }
        rds_sync_last = (resp[6] & 0x02) ? 1 : 0;
        uint8_t fifo_used = resp[11];
        if (fifo_used == 0) {
            break;
        }

        if (rds_sync_last) {
            // BLE resp[12]: A 0xC0, B 0x30, C 0x0C, D 0x03 (3 = uncorrectable)
            uint8_t err_A = resp[12] >> 6;
            uint8_t err_B = (resp[12] >> 4) & 0x03;
            uint8_t err_C = (resp[12] >> 2) & 0x03;
            uint8_t err_D = resp[12] & 0x03;
            if (err_A < 3 && err_B < 3) {
                rds_decoder.decode_block_B(le16(&resp[15]));
                if (err_C < 3) {
                    rds_decoder.decode_block_C(le16(&resp[17]));
                }
                if (err_D < 3) {
                    rds_decoder.decode_block_D(le16(&resp[19]));
                }
            }
        }
        if (fifo_used <= 1) {
            break;
        }
    }
}

int si468x_get_rds_text(char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }
    if (!bus_open()) {
        return SI468X_ERROR_SPI;
    }

    std::string old_text = rds_decoder.radio_text;

    // Pull and parse all latest RDS groups from the hardware FIFO
    si468x_update_rds_decoder();

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

    // If the RadioText changed, reset the complete_reported state
    if (rds_decoder.radio_text != old_text) {
        rds_decoder.complete_reported = false;
    }

    if (is_complete && !rds_decoder.complete_reported) {
        // Expand the raw RDS string (using standard ETSI EBU Latin 0 charset) into UTF-8
        decode_dab_string_to_utf8((const uint8_t*)rds_decoder.radio_text, check_len, 0, out_text, max_len);
        rds_decoder.complete_reported = true;
        return 1; // Return 1 to indicate a newly completed string has been assembled!
    }

    return 0; // Return 0 if the string is still compiling or has already been reported
}

int si468x_get_rds_station_name(char* out_name, int max_len)
{
    if (!out_name || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }
    if (!bus_open()) {
        return SI468X_ERROR_SPI;
    }

    static std::string last_station_name = "";

    // Pull and parse all latest RDS groups from the hardware FIFO
    si468x_update_rds_decoder();

    // Only report once all 4 segments (8 characters) of the station name have been received
    if (rds_decoder.ps_segments_received != 0x0F) {
        out_name[0] = '\0';
        return 0; // Segment collection incomplete
    }

    // Clean up trailing spaces of the 8-character station name
    int end_pos = 8;
    while (end_pos > 0 && (rds_decoder.program_service[end_pos - 1] == ' ' || rds_decoder.program_service[end_pos - 1] == '\0')) {
        end_pos--;
    }

    std::string current_name(rds_decoder.program_service, end_pos);

    if (current_name != last_station_name) {
        last_station_name = current_name;
        decode_dab_string_to_utf8((const uint8_t*)rds_decoder.program_service, end_pos, 0, out_name, max_len);
        return 1; // New completed station name received
    }

    // Still return the station name even if not updated, but return 0 to indicate no change
    decode_dab_string_to_utf8((const uint8_t*)rds_decoder.program_service, end_pos, 0, out_name, max_len);
    return 0;
}

int si468x_get_time(si468x_time_t* time)
{
    if (!time) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_GET_TIME, 0x01 }; // TIME_TYPE 1 = UTC
    uint8_t resp[SI468X_REP_TIME] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    time->year = le16(&resp[5]);
    time->month = resp[7];
    time->day = resp[8];
    time->hours = resp[9];
    time->minutes = resp[10];
    time->seconds = resp[11];
    return time->year ? SI468X_SUCCESS : SI468X_ERROR_NOT_FOUND; // no time received yet
}

int si468x_get_audio_info(si468x_audio_info_t* info)
{
    if (!info) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_GET_AUDIO_INFO, 0x00 };
    uint8_t resp[SI468X_REP_AUDIO_INFO] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // bitrate 0 = not decoding (fields then 0, still success)
    info->bitrate = le16(&resp[5]);
    info->sample_rate = le16(&resp[7]);
    info->ps = (resp[9] >> 3) & 0x01;
    info->sbr = (resp[9] >> 2) & 0x01;
    info->audio_mode = resp[9] & 0x03;
    info->drc_gain = resp[10];
    return SI468X_SUCCESS;
}

/* Deprecated: wrapper over DAB_GET_EVENT_STATUS (0xB3); the old 0x12 is GET_FUNC_INFO */
int si468x_get_event_status(si468x_event_status_t* status)
{
    if (!status) {
        return SI468X_ERROR_PARAM;
    }
    si468x_dab_event_status_t ev;
    int rc = si468x_dab_get_event_status(&ev);
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    status->reconf = ev.recfg_int | ev.recfg_warn_int;
    status->annc = ev.anno_int;
    return SI468X_SUCCESS;
}

int si468x_set_rds_region(int region)
{
    // FM_AUDIO_DE_EMPHASIS: 0 = 75 us, 1 = 50 us, 2 = off
    return si468x_set_property(SI468X_PROP_FM_AUDIO_DE_EMPHASIS, region == SI468X_REGION_EUROPE ? 1 : 0);
}

int si468x_set_property(uint16_t property_id, uint16_t value)
{
    uint8_t cmd[6] = {
        SI468X_CMD_SET_PROPERTY, 0x00,
        (uint8_t)(property_id & 0xFF), (uint8_t)(property_id >> 8),
        (uint8_t)(value & 0xFF), (uint8_t)(value >> 8)
    };
    return send_command(cmd, 6, nullptr, 0);
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
    // Clean and reset the RDS decoder state to isolate different frequencies
    rds_decoder.reset();

    // Build 6-byte packet for FM_SEEK_START (Opcode 0x31)
    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_FM_SEEK_START;
    cmd[1] = 0x00; // Tune Mode: Analog Only (Seek is only supported on analog carriers in hardware)
    cmd[2] = ((seek_up & 0x01) << 1) | (wrap & 0x01);
    cmd[3] = 0x00;
    cmd[4] = 0x01; // ANTCAP Low byte (Set to 0x0001 for peak RF performance)
    cmd[5] = 0x00; // ANTCAP High byte

    int rc = send_command(cmd, 6, nullptr, 0, 5000);

    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    return SI468X_SUCCESS;
}

int si468x_dab_get_announcement_support(uint32_t service_id, uint16_t* asu_flags)
{
    if (!asu_flags) {
        return SI468X_ERROR_PARAM;
    }

    // SRC 0 = current ensemble; reply NUM_IDS resp[5], ASU resp[7..8], IDs from resp[9] (not returned)
    uint8_t cmd[8] = {
        0xB5, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF)
    };
    uint8_t resp[SI468X_REP_ANN_SUPPORT_HDR] = {};
    int rc = send_command(cmd, 8, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    *asu_flags = le16(&resp[7]);
    return SI468X_SUCCESS;
}

int si468x_dab_get_announcement_info(uint8_t* queue_size, uint32_t* service_id, uint32_t* component_id, uint16_t* asw_flags)
{
    uint8_t cmd[1] = { 0xB6 };
    uint8_t resp[SI468X_REP_ANN_INFO] = {};
    int rc = send_command(cmd, 1, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // Q_SIZE resp[6] & 0x1F, CLUSTER resp[7], STAT/REGION/SRC resp[8], ASW resp[9..10], ID1 resp[11..12], ID2 resp[13..14]
    if (queue_size) {
        *queue_size = resp[6] & 0x1F;
    }
    if (asw_flags) {
        *asw_flags = le16(&resp[9]);
    }
    if (service_id) {
        *service_id = le16(&resp[11]);
    }
    if (component_id) {
        *component_id = le16(&resp[13]);
    }
    return SI468X_SUCCESS;
}

int si468x_dab_get_service_linking(uint32_t service_id, uint8_t* link_info, int max_len)
{
    if (!link_info || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[8] = {
        0xB7, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF)
    };
    std::vector<uint8_t> resp(SI468X_REP_LINKING_HDR);
    int rc = send_command(cmd, 8, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // SIZE resp[5..6] counts bytes from LSN_0 (resp[9]) on (total read = 9 + SIZE)
    uint16_t size = le16(&resp[5]);
    if (size > 0 && (rc = reread(resp, SI468X_REP_LINKING_HDR + size)) != SI468X_SUCCESS) {
        return rc;
    }

    // Output: NUM_LINKSETS, reserved, then linksets (LSN u16, flags, NUM_LINKS, LINKID u32 ...)
    int n = std::min<int>(2 + size, max_len);
    std::memcpy(link_info, &resp[7], n);
    return n;
}

int si468x_fmhd_get_psd_text(int program, int field, char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[3] = { SI468X_CMD_HD_GET_PSD_DECODE, (uint8_t)program, (uint8_t)field };
    std::vector<uint8_t> resp(SI468X_REP_HD_PSD_HDR);
    int rc = send_command(cmd, 3, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // DATATYPE resp[7], LENGTH resp[8], DATA0 resp[9]
    uint8_t length = resp[8];
    if (length == 0) {
        out_text[0] = '\0';
        return 0;
    }
    if ((rc = reread(resp, SI468X_REP_HD_PSD_HDR + length)) != SI468X_SUCCESS) {
        return rc;
    }
    decode_dab_string_to_utf8(&resp[9], length, 0, out_text, max_len);
    return 1;
}

int si468x_fmhd_get_station_info(int info_select, char* out_text, int max_len)
{
    if (!out_text || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_HD_GET_STATION_INFO, (uint8_t)info_select };
    std::vector<uint8_t> resp(SI468X_REP_HD_STATION_HDR);
    int rc = send_command(cmd, 2, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // LENGTH resp[5..6], DATA0 resp[7]
    uint16_t length = le16(&resp[5]);
    if (length == 0) {
        out_text[0] = '\0';
        return 0;
    }
    if ((rc = reread(resp, SI468X_REP_HD_STATION_HDR + length)) != SI468X_SUCCESS) {
        return rc;
    }
    int n = std::min<int>(length, max_len - 1);
    std::memcpy(out_text, &resp[7], n);
    out_text[n] = '\0';
    return 1;
}

int si468x_dab_get_subchan_info(uint32_t service_id, uint32_t component_id, si468x_subchan_info_t* info)
{
    if (!info) {
        return SI468X_ERROR_PARAM;
    }

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
    uint8_t resp[SI468X_REP_SUBCHAN_INFO] = {};
    int rc = send_command(cmd, 12, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    info->service_mode = resp[5];    // si468x_get_service_type_text()
    info->protection_info = resp[6]; // si468x_get_protection_text()
    info->bitrate = le16(&resp[7]);
    info->num_cu = le16(&resp[9]);
    info->cu_address = le16(&resp[11]);
    return SI468X_SUCCESS;
}

int si468x_fmhd_get_alert_message(char* alert_text, int max_len)
{
    if (!alert_text || max_len <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_HD_GET_ALERT_MSG, (uint8_t)0x00 };
    std::vector<uint8_t> resp(SI468X_REP_HD_ALERT_HDR);
    int rc = send_command(cmd, 2, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // LENGTH resp[5..6], DATA0 resp[7]
    uint16_t length = le16(&resp[5]);
    if (length == 0) {
        alert_text[0] = '\0';
        return 0;
    }
    if ((rc = reread(resp, SI468X_REP_HD_ALERT_HDR + length)) != SI468X_SUCCESS) {
        return rc;
    }
    int n = std::min<int>(length, max_len - 1);
    std::memcpy(alert_text, &resp[7], n);
    alert_text[n] = '\0';
    return 1;
}

int si468x_dab_get_freq_info(si468x_freq_element_t* elements, int max_elements)
{
    if (!elements || max_elements <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_GET_FREQ_INFO, 0x00 };
    std::vector<uint8_t> resp(SI468X_REP_FREQ_INFO_HDR);
    int rc = send_command(cmd, 2, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // LENGTH_FI_LIST resp[5..8] = element count; 12-byte elements from resp[9]
    uint32_t num = le32(&resp[5]);
    if (num == 0) {
        return 0;
    }
    if (num > (SI468X_REP_MAX_VAR - SI468X_REP_FREQ_INFO_HDR) / 12) {
        return SI468X_ERROR_INVALID_REPLY;
    }
    if ((rc = reread(resp, SI468X_REP_FREQ_INFO_HDR + 12 * num)) != SI468X_SUCCESS) {
        return rc;
    }

    int n = std::min<uint32_t>(num, max_elements);
    for (int i = 0; i < n; i++) {
        const uint8_t* e = &resp[SI468X_REP_FREQ_INFO_HDR + 12 * i];
        elements[i].id = le32(&e[0]);
        elements[i].frequency_hz = le32(&e[4]) * 1000;
        elements[i].freq_index = e[8];
        elements[i].rnm = e[9];
        elements[i].continuity = e[10];
        elements[i].control = e[11];
    }
    return n;
}

/* Linux si468x driver layout (+1). Q3: if DAB fw rejects 0x17, this returns SI468X_ERROR_COMMAND */
int si468x_get_agc_status(si468x_agc_status_t* status)
{
    if (!status) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_GET_AGC_STATUS, 0x00 };
    uint8_t resp[SI468X_REP_AGC_STATUS] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    status->vhflna = resp[15];
    status->vhfcatt = resp[16];
    status->vhfrattlo = resp[17];
    status->vhfratthi = resp[18];
    status->lmhfrtanklo = resp[19];
    status->lmhfrtankhi = resp[20];
    status->rfindex = resp[22];
    return SI468X_SUCCESS;
}

int si468x_dab_get_event_status(si468x_dab_event_status_t* status)
{
    if (!status) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[2] = { SI468X_CMD_DAB_GET_EVENT_STATUS, 0x01 }; // EVENT_ACK
    uint8_t resp[SI468X_REP_DAB_EVENT_STATUS] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    status->recfg_int = (resp[5] >> 7) & 0x01;
    status->recfg_warn_int = (resp[5] >> 6) & 0x01;
    status->anno_int = (resp[5] >> 4) & 0x01;
    status->oeserv_int = (resp[5] >> 3) & 0x01;
    status->servlink_int = (resp[5] >> 2) & 0x01;
    status->freqinfo_int = (resp[5] >> 1) & 0x01;
    status->svrlist_int = resp[5] & 0x01;
    status->anno_active = (resp[6] >> 4) & 0x01;
    status->oeserv_active = (resp[6] >> 3) & 0x01;
    status->servlink_active = (resp[6] >> 2) & 0x01;
    status->freqinfo_active = (resp[6] >> 1) & 0x01;
    status->svrlist_active = resp[6] & 0x01;
    status->svrlist_ver = le16(&resp[7]);
    return SI468X_SUCCESS;
}

/*
 * Per ANTCAP 1..128 tune, wait STC, average 5 RSSI reads, keep peak.
 * RSSI from DAB_DIGRAD_STATUS (TEST_GET_RSSI 0xE5 format unverified, Q10). ~128 tunes: tens of seconds.
 * Afterwards re-tunes the previously active frequency with ANTCAP 0.
 */
int si468x_dab_calibrate_antenna(uint32_t frequency_hz, uint16_t* peak_antcap)
{
    if (!peak_antcap) {
        return SI468X_ERROR_PARAM;
    }
    if (!bus_open()) {
        return SI468X_ERROR_SPI;
    }
    int freq_index = si468x_get_freq_index(frequency_hz);
    if (freq_index < 0) {
        return SI468X_ERROR_PARAM;
    }

    uint32_t restore = active_frequency;
    uint16_t best = 0;
    int peak_rssi = 0;
    for (uint16_t antcap = 1; antcap <= 128; antcap++) {
        uint8_t cmd[6] = { SI468X_CMD_DAB_TUNE_FREQ, 0x00, (uint8_t)freq_index, 0x00,
                           (uint8_t)(antcap & 0xFF), (uint8_t)(antcap >> 8) };
        int rc = send_command(cmd, 6, nullptr, 0, 5000);
        if (rc == SI468X_ERROR_COMMAND) { // previous tune still busy
            wait_stc(3000);
            rc = send_command(cmd, 6, nullptr, 0, 5000);
        }
        if (rc != SI468X_SUCCESS) {
            continue;
        }
        if (wait_stc(3000) != SI468X_SUCCESS) {
            continue;
        }

        int sum = 0;
        for (int i = 0; i < 5; i++) {
            si468x_signal_status_t st;
            if (si468x_get_signal_status(&st) == SI468X_SUCCESS) {
                sum += st.rssi;
            }
        }
        if (sum / 5 > peak_rssi) {
            peak_rssi = sum / 5;
            best = antcap;
        }
    }

    *peak_antcap = best;
    active_frequency = 0;
    if (restore) {
        si468x_set_frequency(restore);
    }
    return SI468X_SUCCESS;
}

int si468x_dab_get_other_ensemble_info(uint32_t service_id, uint16_t* eids, int max_eids)
{
    if (!eids || max_eids <= 0) {
        return SI468X_ERROR_PARAM;
    }

    uint8_t cmd[8] = {
        SI468X_CMD_DAB_GET_OE_SERVICES_INFO, 0x00, 0x00, 0x00,
        (uint8_t)(service_id & 0xFF),
        (uint8_t)((service_id >> 8) & 0xFF),
        (uint8_t)((service_id >> 16) & 0xFF),
        (uint8_t)((service_id >> 24) & 0xFF)
    };
    std::vector<uint8_t> resp(SI468X_REP_OE_SERVICES_HDR);
    int rc = send_command(cmd, 8, resp.data(), resp.size());
    if (rc != SI468X_SUCCESS) {
        return rc;
    }

    // SIZE resp[5..6], NUM_EIDS resp[7], EIDs u16 from resp[9]
    uint8_t num = resp[7];
    if (num == 0) {
        return 0;
    }
    if ((rc = reread(resp, SI468X_REP_OE_SERVICES_HDR + 2 * num)) != SI468X_SUCCESS) {
        return rc;
    }
    int n = std::min<int>(num, max_eids);
    for (int i = 0; i < n; i++) {
        eids[i] = le16(&resp[SI468X_REP_OE_SERVICES_HDR + 2 * i]);
    }
    return n;
}

int si468x_dab_get_ber_info(int clear, uint32_t* err_bits, uint32_t* total_bits)
{
    uint8_t cmd[2] = { SI468X_CMD_DAB_TEST_GET_BER_INFO, (uint8_t)(clear & 0x01) };
    uint8_t resp[SI468X_REP_DAB_BER] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    if (err_bits) {
        *err_bits = le32(&resp[5]);
    }
    if (total_bits) {
        *total_bits = le32(&resp[9]);
    }
    return SI468X_SUCCESS;
}

/* HD only (not on Si4684/Si4688): returns PIDS_BLOCK_ERRORS of the 10 counters at resp[5..44] */
int si468x_fmhd_get_ber_info(int clear, uint32_t* ber_count)
{
    uint8_t cmd[2] = { SI468X_CMD_HD_TEST_GET_BER_INFO, (uint8_t)(clear & 0x01) };
    uint8_t resp[SI468X_REP_HD_BER] = {};
    int rc = send_command(cmd, 2, resp, sizeof(resp));
    if (rc != SI468X_SUCCESS) {
        return rc;
    }
    if (ber_count) {
        *ber_count = le32(&resp[5]);
    }
    return SI468X_SUCCESS;
}

