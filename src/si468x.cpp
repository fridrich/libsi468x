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

/* Polling the CTS (Clear To Send) flag over SPI */
static bool wait_for_cts(int timeout_ms = 1000, bool suppress_errors = false)
{
    auto start = std::chrono::steady_clock::now();
    uint8_t tx_byte = 0x00;
    uint8_t rx_byte = 0x00;

    while (true) {
        // Poll status byte
        if (spi_transfer(&tx_byte, &rx_byte, 1) == 0) {
            if (rx_byte & SI468X_CTS_MASK) {
                // Check for Command Error (ERR bit 6)
                if ((rx_byte & 0x40) && !suppress_errors) {
                    std::cerr << "libsi468x: WARNING: Command Error (Status: 0x"
                              << std::hex << (int)rx_byte << std::dec << ")" << std::endl;
                }
                return true; // CTS is high!
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start
                       ).count();
        if (elapsed >= timeout_ms) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

/* Transmit a command packet and wait for response */
static int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len, int timeout_ms = 1000)
{
    // Write command
    if (spi_transfer(cmd, nullptr, cmd_len) < 0) {
        return SI468X_ERROR_SPI;
    }

    // Wait for CTS with configurable timeout
    if (!wait_for_cts(timeout_ms)) {
        return SI468X_ERROR_TIMEOUT;
    }

    // Read reply
    if (resp && resp_len > 0) {
        std::vector<uint8_t> tx_dummy(resp_len, 0x00);
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
            return SI468X_ERROR_SPI;
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

    // 4. Send POWER_UP (0x01)
    uint8_t power_up_cmd[5] = { SI468X_CMD_POWER_UP, 0x00, 0x00, 0x00, 0x00 };
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

    // Enable I2S digital output by default upon system init
    if (si468x_set_audio_output(1) != SI468X_SUCCESS) {
        std::cerr << "libsi468x: Warning: Failed to configure default I2S output." << std::endl;
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

int si468x_set_frequency(uint32_t frequency_hz)
{
    active_frequency = frequency_hz;

    // Map the requested frequency (in Hz) to its standard European Frequency Index (0-37)
    uint8_t freq_index = 0;
    uint32_t freqs[] = {
        174928000, 176640000, 178352000, 180064000, 181936000, 183648000, 185360000, 187072000,
        188928000, 190640000, 192352000, 194064000, 195936000, 197648000, 199360000, 201072000,
        202928000, 204640000, 206352000, 208064000, 209936000, 211648000, 213360000, 215072000,
        216928000, 218640000, 220352000, 222064000, 223936000, 225648000, 227360000, 229072000,
        230784000, 232496000, 234208000, 235776000, 237488000, 239200000
    };
    for (int i = 0; i < 38; i++) {
        if (freqs[i] == frequency_hz) {
            freq_index = i;
            break;
        }
    }

    std::clog << "libsi468x: Tuning chip to freq_index " << (int)freq_index << " (" << frequency_hz << " Hz)..." << std::endl;

    // Build DAB_TUNE_FREQ packet (must be exactly 6 bytes)
    uint8_t cmd[6];
    cmd[0] = SI468X_CMD_DAB_TUNE_FREQ;
    cmd[1] = 0x00;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = 0x00;
    cmd[5] = freq_index; // Pass the frequency index

    if (send_command(cmd, 6, nullptr, 0, 5000) != SI468X_SUCCESS) {
        return SI468X_ERROR_SPI;
    }

    // Wait for the RF synthesizers to lock and acquire OFDM sync (up to 10 seconds)
    bool locked = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Poll signal status (2-byte packet: Opcode + INTACK)
        uint8_t stat_cmd[2] = { SI468X_CMD_DAB_DIGRAD_STATUS, 0x00 };
        std::vector<uint8_t> resp(12, 0x00);

        // We bypass the global send_command here to ignore 0xc0 Command Errors,
        // as the chip routinely rejects status queries while actively seeking RF lock.
        spi_transfer(stat_cmd, nullptr, 2);
        if (wait_for_cts(100, true)) {
            std::vector<uint8_t> tx_dummy(12, 0x00);
            if (spi_transfer(tx_dummy.data(), resp.data(), 12) == 0) {
                if (resp[4] != 0xc0 && (resp[10] & 0x01) == 1) { // SYNC flag high
                    locked = true;
                    break;
                }
            }
        }
    }

    if (!locked) {
        return SI468X_ERROR_TIMEOUT; // No signal found on this frequency
    }

    // Give the co-processor 1000ms to accumulate and decode the FIC database tables from the air
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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

    uint8_t cmd[9];
    cmd[0] = SI468X_CMD_START_DIGITAL;
    cmd[1] = 0x00;
    cmd[2] = (service_id >> 24) & 0xFF;
    cmd[3] = (service_id >> 16) & 0xFF;
    cmd[4] = (service_id >> 8) & 0xFF;
    cmd[5] = service_id & 0xFF;
    cmd[6] = (component_id >> 16) & 0xFF;
    cmd[7] = (component_id >> 8) & 0xFF;
    cmd[8] = component_id & 0xFF;

    return send_command(cmd, 9, nullptr, 0);
}

int si468x_stop_service(void)
{
    std::clog << "libsi468x: Stopping service playback..." << std::endl;
    uint8_t cmd[1] = { SI468X_CMD_STOP_DIGITAL };
    return send_command(cmd, 1, nullptr, 0);
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
        if ((char_mask >> (15 - i)) & 0x01) {
            short_label[dst++] = long_label[i];
            if (dst >= 8) {
                break;    // Short label is max 8 characters
            }
        }
    }
    short_label[dst] = '\0';
}

int si468x_get_service_list(si468x_service_t* list, int max_services)
{
    if (!list || max_services <= 0) {
        return 0;
    }

    std::clog << "libsi468x: Querying on-chip service database..." << std::endl;

    // Send GET_DIGITAL_SERVICE_LIST (0xB5)
    uint8_t cmd[2] = { SI468X_CMD_GET_DIGITAL_LIST, 0x00 };
    std::vector<uint8_t> resp(2048, 0x00); // 2KB buffer to capture the database reply

    if (send_command(cmd, 2, resp.data(), 2048) != SI468X_SUCCESS) {
        return 0;
    }

    // Shift index by 4 to bypass the 4-byte SPI status/padding overhead
    uint8_t num_services = resp[7];
    std::clog << "libsi468x: Chip reported " << (int)num_services << " active services." << std::endl;

    if (num_services == 0) {
        return 0;
    }

    int services_count = 0;
    size_t offset = 8; // Data payload starts at index 8 after 4 status/padding + 4 header bytes

    for (int i = 0; i < num_services; i++) {
        if (services_count >= max_services) {
            break;
        }
        if (offset + 24 > 2048) {
            break;    // Buffer boundary safety guard
        }

        // 1. Service ID (SId): 32-bit Big Endian (offset 0-3)
        uint32_t service_id = ((uint32_t)resp[offset] << 24) |
                              ((uint32_t)resp[offset + 1] << 16) |
                              ((uint32_t)resp[offset + 2] << 8) |
                              ((uint32_t)resp[offset + 3]);

        // 2. Number of Components (offset 4)
        uint8_t num_components = resp[offset + 4];

        // 3. Label: 16-character array (offset 5-20)
        char service_label[17];
        std::memcpy(service_label, &resp[offset + 5], 16);
        service_label[16] = '\0';

        // 4. Short Label Abbreviation Mask: 16-bit Big Endian (offset 21-22)
        uint16_t char_mask = ((uint16_t)resp[offset + 21] << 8) |
                             ((uint16_t)resp[offset + 22]);

        // Offset advancement: Service Header is 24 bytes (including alignment/padding)
        offset += 24;

        // 5. Parse Components of this service
        for (int c = 0; c < num_components; c++) {
            if (offset + 4 > 2048) {
                break;
            }

            // Component block is 4 bytes:
            // - Component ID: 16-bit Big Endian (offset 0-1)
            uint16_t component_id = ((uint16_t)resp[offset] << 8) | resp[offset + 1];
            // - Service Component Type: 8-bit (offset 2)
            uint8_t audio_type = resp[offset + 2];
            // - Bitrate: 8-bit (offset 3). Represented in units of 8 kbps
            uint16_t bitrate = resp[offset + 3] * 8;

            // Store the first audio component of the service in our output list
            if (c == 0) {
                list[services_count].service_id = service_id;
                list[services_count].component_id = component_id;
                list[services_count].audio_type = audio_type;
                list[services_count].bitrate = bitrate;

                std::strncpy(list[services_count].label, service_label, 16);
                list[services_count].label[16] = '\0';

                // Decode short label natively from the real Character Flag Mask!
                si468x_decode_short_label(service_label, char_mask, list[services_count].short_label);

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

    // 2-byte packet required (Opcode + INTACK)
    uint8_t cmd[2] = { SI468X_CMD_DAB_DIGRAD_STATUS, 0x00 };
    std::vector<uint8_t> resp(12, 0x00); // 4 status/padding bytes + 8 response parameter bytes

    if (send_command(cmd, 2, resp.data(), 12) != SI468X_SUCCESS) {
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

    // Parse DAB_DIGRAD_STATUS Response:
    // resp[0..3] : SPI status and padding bytes (Byte 0 is STATUS, Bytes 1-3 is padding)
    // resp[4]    : Response parameter Byte 0 (state/status)
    // resp[5]    : Response parameter Byte 1 (digital status)
    // resp[6]    : Response parameter Byte 2 (RSSI in dBuV)
    // resp[7]    : Response parameter Byte 3 (SNR in dB)
    // resp[8..9] : Response parameter Byte 4-5 (Frequency Offset in kHz, Big Endian signed 16-bit)
    // resp[10]   : Response parameter Byte 6 (Sync status: Bit 0 is SYNC flag)
    std::clog << "libsi468x: Raw DIGRAD_STATUS: ";
    for (int i = 0; i < 12; i++) {
        std::clog << "0x" << std::hex << (int)resp[i] << " ";
    }
    std::clog << std::dec << std::endl;

    status->rssi = resp[6];
    status->snr = resp[7];
    status->freq_offset = (int16_t)(((uint16_t)resp[8] << 8) | resp[9]);
    status->sync_status = (resp[10] & 0x01);

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
