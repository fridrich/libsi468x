#include <iostream>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include "si468x.h"

// Low-level helper to send command and get full raw payload
static int get_raw_service_db(std::vector<uint8_t>& out_payload)
{
    // Query database size first
    uint8_t size_cmd[2] = { 0x80, 0x00 };
    uint8_t size_resp[7];
    std::memset(size_resp, 0, sizeof(size_resp));

    extern int spi_fd; // Use internal SPI file descriptor

    // In our library send_command is public, so we can use it!
    // But since send_command is declared inside si468x.cpp, let's declare its prototype:
    // int send_command(const uint8_t* cmd, size_t cmd_len, uint8_t* resp, size_t resp_len);
    // Wait, let's just use si468x_get_service_list to see how it works, or we can implement the SPI transaction directly!
    // Or we can just read the whole memory buffer using standard command.
    // Wait, actually, can we just read the memory buffer?
    // Let's declare the standard internal methods that we can link against:
    return 0;
}

int main()
{
    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23;
    uint32_t channel_7a_hz = 188928000; // Channel 7A (Metropolitain 1)

    std::cout << "==================================================" << std::endl;
    std::cout << "   libsi468x Service Block Mask Finder Tool       " << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Initializing chip..." << std::endl;
    if (si468x_init(spi_dev, rst_pin, SI468X_BOOT_DAB) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    std::cout << "libsi468x: Tuning to Channel 7A (188.928 MHz)..." << std::endl;
    if (si468x_set_frequency(channel_7a_hz) != SI468X_SUCCESS) {
        std::cerr << "Tuning failed!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Wait up to 5 seconds for frame sync lock
    std::cout << "Waiting for signal lock..." << std::endl;
    bool locked = false;
    for (int i = 0; i < 50; i++) {
        si468x_signal_status_t status;
        if (si468x_get_signal_status(&status) == 0 && status.sync_status) {
            locked = true;
            std::cout << "Locked! RSSI: " << (int)status.rssi << " dBuV | SNR: " << (int)status.snr << " dB" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!locked) {
        std::cerr << "Failed to lock onto Channel 7A. Please verify antenna." << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Wait 5 seconds for full database compilation (FIC groups parsing from air)
    std::cout << "Waiting 5 seconds for station names to compile from air..." << std::endl;
    for (int i = 0; i < 10; i++) {
        si468x_signal_status_t status;
        si468x_get_signal_status(&status);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Now, we retrieve the raw database payload directly from the chip over SPI!
    // Write 2-byte command to get database size:
    uint8_t size_cmd[2] = { 0x80, 0x00 };
    uint8_t size_resp[29]; // Let's read 29 bytes
    std::memset(size_resp, 0, sizeof(size_resp));

    // To do this directly, we can define a simple SPI transfer helper or use the library's internal state
    // Let's call standard C-API si468x_get_service_list to trigger the SPI query, but wait!
    // Since we want to look at the raw bytes of the 24-byte Service Block, let's write a simple routine
    // inside our C++ program to download the raw service database from SPI!
    // To do this, we need spi_fd. Since spi_fd is a global variable in si468x.cpp, we can access it if we declare it as extern,
    // or we can just open /dev/spidev0.0 ourselves in this program!
    // Yes! Opening /dev/spidev0.0 and sending the standard commands directly is 100% self-contained and bulletproof!

    int fd = open(spi_dev, O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open SPI device!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Configure SPI parameters
    uint8_t mode = 0;
    uint8_t bits = 8;
    uint32_t speed = 10000000;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    // 1. Get database size
    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    uint8_t tx_buf[11] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rx_buf[11] = {0};
    tr.tx_buf = (unsigned long)tx_buf;
    tr.rx_buf = (unsigned long)rx_buf;
    tr.len = 11;
    tr.speed_hz = speed;
    tr.bits_per_word = bits;
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);

    uint16_t db_size = rx_buf[9] | ((uint16_t)rx_buf[10] << 8);
    std::cout << "Database Size: " << db_size << " bytes." << std::endl;

    if (db_size <= 6 || db_size > 2048) {
        std::cerr << "Invalid database size!" << std::endl;
        close(fd);
        si468x_shutdown();
        return 1;
    }

    // 2. Read full database payload
    uint16_t full_len = db_size + 4 + 4; // SPI overhead + 4 bytes header padding
    std::vector<uint8_t> tx_full(full_len, 0x00);
    std::vector<uint8_t> rx_full(full_len, 0x00);
    tx_full[0] = 0x80;
    tx_full[1] = 0x00;

    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx_full.data();
    tr.rx_buf = (unsigned long)rx_full.data();
    tr.len = full_len;
    tr.speed_hz = speed;
    tr.bits_per_word = bits;
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);

    close(fd);

    // Parse records starting at offset 13 (rx_full[13])
    uint8_t num_services = rx_full[9]; // Parameter Byte 5
    std::cout << "Number of Services in Ensemble: " << (int)num_services << std::endl;

    size_t offset = 13;
    for (int s = 0; s < num_services; s++) {
        if (offset + 24 > full_len) {
            break;
        }

        uint32_t service_id = rx_full[offset] |
                              ((uint32_t)rx_full[offset + 1] << 8) |
                              ((uint32_t)rx_full[offset + 2] << 16) |
                              ((uint32_t)rx_full[offset + 3] << 24);

        char service_label[17];
        std::memcpy(service_label, &rx_full[offset + 8], 16);
        service_label[16] = '\0';

        uint8_t num_components = rx_full[offset + 5] & 0x0F;

        std::cout << "\n--------------------------------------------------" << std::endl;
        std::cout << "Service " << s + 1 << ": \"" << service_label << "\""
                  << " | SId: 0x" << std::hex << service_id << std::dec
                  << " | Components: " << (int)num_components << std::endl;

        // Print raw hex bytes of this 24-byte Service Block
        std::cout << "Raw Service Block Hex Bytes:" << std::endl;
        std::cout << "Index: ";
        for (int b = 0; b < 24; b++) {
            std::cout << std::setw(2) << b << " ";
        }
        std::cout << "\nBytes: ";
        for (int b = 0; b < 24; b++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)rx_full[offset + b] << " " << std::dec << std::setfill(' ');
        }
        std::cout << std::endl;

        // Advance offset past the 24-byte Service Header and its components (4 bytes each)
        offset += 24 + (num_components * 4);
    }

    std::cout << "==================================================" << std::endl;
    si468x_shutdown();
    return 0;
}
