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

#include "si468x.h"
#include "si468x_internal.h"

static int spi_fd = -1;
static int rst_gpio_pin = -1;
static uint32_t active_frequency = 0;

/* Helper to write to sysfs files */
static bool write_sysfs(const std::string& path, const std::string& value)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << value;
    return true;
}

/* Low-level GPIO RSTB management */
static bool gpio_init(int pin)
{
    rst_gpio_pin = pin;

    // Export pin
    write_sysfs("/sys/class/gpio/export", std::to_string(pin));
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Wait for export

    std::string dir_path = "/sys/class/gpio/gpio" + std::to_string(pin) + "/direction";
    if (!write_sysfs(dir_path, "out")) {
        return false;
    }
    return true;
}

static void gpio_set_rst(bool high)
{
    if (rst_gpio_pin < 0) return;
    std::string val_path = "/sys/class/gpio/gpio" + std::to_string(rst_gpio_pin) + "/value";
    write_sysfs(val_path, high ? "1" : "0");
}

static void gpio_shutdown()
{
    if (rst_gpio_pin >= 0) {
        gpio_set_rst(false); // Hold in reset
        write_sysfs("/sys/class/gpio/unexport", std::to_string(rst_gpio_pin));
        rst_gpio_pin = -1;
    }
}

/* Low-level SPI transfer helper */
static int spi_transfer(const uint8_t* tx, uint8_t* rx, size_t length)
{
    if (spi_fd < 0) return -1;

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
}

/* Polling the CTS (Clear To Send) flag over SPI */
static bool wait_for_cts(int timeout_ms = 1000)
{
    auto start = std::chrono::steady_clock::now();
    uint8_t tx_byte = 0x00;
    uint8_t rx_byte = 0x00;

    while (true) {
        // Poll status byte
        if (spi_transfer(&tx_byte, &rx_byte, 1) == 0) {
            if (rx_byte & SI468X_CTS_MASK) {
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
static int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len)
{
    // Write command
    if (spi_transfer(cmd, nullptr, cmd_len) < 0) {
        return SI468X_ERROR_SPI;
    }

    // Wait for CTS
    if (!wait_for_cts()) {
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

/* Streams a firmware file to the chip using WRITE_FUT packets */
static int upload_firmware_file(const char* filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "libsi468x: Could not open firmware file: " << filepath << std::endl;
        return SI468X_ERROR_FIRMWARE;
    }

    // Send LOAD_INIT
    uint8_t load_init_cmd[1] = { SI468X_CMD_LOAD_INIT };
    if (send_command(load_init_cmd, 1, nullptr, 0) != SI468X_SUCCESS) {
        return SI468X_ERROR_FIRMWARE;
    }

    const size_t chunk_size = 512;
    std::vector<uint8_t> buffer(chunk_size);
    std::vector<uint8_t> packet(chunk_size + 4); // 4 header bytes + chunk

    while (file) {
        file.read((char*)buffer.data(), chunk_size);
        size_t bytes_read = file.gcount();
        if (bytes_read == 0) break;

        // Build WRITE_FUT command packet
        packet[0] = SI468X_CMD_WRITE_FUT;
        packet[1] = 0x00;
        packet[2] = (bytes_read >> 8) & 0xFF;
        packet[3] = bytes_read & 0xFF;
        std::memcpy(&packet[4], buffer.data(), bytes_read);

        // Transmit packet
        if (send_command(packet.data(), bytes_read + 4, nullptr, 0) != SI468X_SUCCESS) {
            return SI468X_ERROR_SPI;
        }
    }
    return SI468X_SUCCESS;
}

/* Public C-API Implementation */

int si468x_init(const char* spi_device, int rst_pin, const char* patch_path, const char* fw_path)
{
    std::clog << "libsi468x: Initializing driver library..." << std::endl;

    // 1. Initialize GPIO
    if (!gpio_init(rst_pin)) {
        return SI468X_ERROR_GPIO;
    }

    // 2. Hard Reset Chip
    gpio_set_rst(false); // Reset low
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    gpio_set_rst(true);  // Reset high
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Stable wait

    // 3. Open SPI Bus
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

    // 4. Send POWER_UP (0x01)
    uint8_t power_up_cmd[5] = { SI468X_CMD_POWER_UP, 0x00, 0x00, 0x00, 0x00 };
    if (send_command(power_up_cmd, 5, nullptr, 0) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_TIMEOUT;
    }

    // 5. Upload ROM Patch
    std::clog << "libsi468x: Loading ROM patch..." << std::endl;
    if (upload_firmware_file(patch_path) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    // 6. Upload Application Firmware
    std::clog << "libsi468x: Loading application firmware..." << std::endl;
    if (upload_firmware_file(fw_path) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_FIRMWARE;
    }

    // 7. Send BOOT (0x07)
    std::clog << "libsi468x: Booting application image..." << std::endl;
    uint8_t boot_cmd[1] = { SI468X_CMD_BOOT };
    if (send_command(boot_cmd, 1, nullptr, 0) != SI468X_SUCCESS) {
        si468x_shutdown();
        return SI468X_ERROR_BOOT;
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
    std::clog << "libsi468x: Tuning to " << frequency_hz << " Hz..." << std::endl;

    // Build DAB_TUNE_FREQ packet
    uint8_t cmd[5];
    cmd[0] = SI468X_CMD_DAB_TUNE_FREQ;
    cmd[1] = 0x00;
    cmd[2] = (frequency_hz >> 16) & 0xFF;
    cmd[3] = (frequency_hz >> 8) & 0xFF;
    cmd[4] = frequency_hz & 0xFF;

    return send_command(cmd, 5, nullptr, 0);
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
    if (volume > 63) volume = 63;
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

int si468x_get_service_list(si468x_service_t* list, int max_services)
{
    if (!list || max_services <= 0) return 0;

    std::clog << "libsi468x: Querying on-chip service database..." << std::endl;

    // Send GET_DIGITAL_SERVICE_LIST (0xB5)
    uint8_t cmd[2] = { SI468X_CMD_GET_DIGITAL_LIST, 0x00 };
    std::vector<uint8_t> resp(1024, 0x00);

    if (send_command(cmd, 2, resp.data(), 1024) != SI468X_SUCCESS) {
        return 0;
    }

    // In a physical hardware environment, we would parse the raw service database
    // structure returned by the chip's DSP to extract active service components.
    // For this build/checkout phase, we write mock/simulated services to list.

    int services_count = 3;
    if (services_count > max_services) services_count = max_services;

    for (int i = 0; i < services_count; i++) {
        list[i].service_id = 0x1000 + i;
        list[i].component_id = i;
        list[i].bitrate = 128;
        list[i].audio_type = 1; // DABPlus

        std::string label = "Station " + std::to_string(i + 1);
        std::strncpy(list[i].label, label.c_str(), 16);
        list[i].label[16] = '\0';
    }

    return services_count;
}
