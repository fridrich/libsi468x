#include <iostream>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include "si468x.h"

// Signal handler to perform clean shutdown upon program termination
void signal_handler(int signum)
{
    std::cout << "\n[Signal] Caught signal " << signum << ". Performing clean shutdown..." << std::endl;
    si468x_shutdown();
    std::exit(signum);
}

int main(int argc, char* argv[])
{
    // Register signal handlers for clean teardown on Ctrl+C/termination
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    uint32_t frequency_khz = 98100; // Default: 98.1 MHz
    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23;

    if (argc > 1) {
        frequency_khz = std::stoul(argv[1]);
    }
    if (argc > 2) {
        spi_dev = argv[2];
    }
    if (argc > 3) {
        rst_pin = std::stoi(argv[3]);
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "  libsi468x FM / FM-HD Receiver & RDS Tester     " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Target Frequency: " << (frequency_khz / 1000.0) << " MHz (" << frequency_khz << " kHz)" << std::endl;
    std::cout << "SPI Device:       " << spi_dev << std::endl;
    std::cout << "Reset Pin:        " << rst_pin << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Initializing chip in FM-HD mode..." << std::endl;
    if (si468x_init(spi_dev, rst_pin, SI468X_BOOT_FMHD) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    // Read chip info
    si468x_chip_info_t info;
    if (si468x_get_chip_info(&info) == SI468X_SUCCESS) {
        std::cout << "==================================================" << std::endl;
        std::cout << "  Chip Information: " << std::endl;
        std::cout << "  Chip ID:       0x" << std::hex << (int)info.chip_id << std::dec << std::endl;
        std::cout << "  ROM ID:        0x" << std::hex << (int)info.rom_id << std::dec << std::endl;
        std::cout << "  FW Version:    " << (info.fw_version >> 8) << "." << (info.fw_version & 0xFF) << std::endl;
        std::cout << "  Patch Version: " << (info.patch_version >> 8) << "." << (info.patch_version & 0xFF) << std::endl;
        std::cout << "==================================================" << std::endl;
    }

    std::cout << "libsi468x: Tuning to " << (frequency_khz / 1000.0) << " MHz..." << std::endl;
    if (si468x_tune_fm(frequency_khz) != SI468X_SUCCESS) {
        std::cerr << "Failed to tune to frequency!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    std::cout << "Tuned successfully. Monitoring signal & RDS. Press Ctrl+C to exit." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    char rds_text[65];
    std::memset(rds_text, 0, sizeof(rds_text));

    while (true) {
        si468x_fm_status_t status;
        bool synced = false;
        if (si468x_get_fm_status(&status) == SI468X_SUCCESS) {
            std::cout << "[Signal] RSSI: " << (int)status.rssi << " dBuV | "
                      << "SNR: " << (int)status.snr << " dB | "
                      << "Multipath: " << (int)status.multipath << " | "
                      << "Offset: " << (int)status.freq_offset << " kHz | "
                      << "HD: " << (status.hd_synced ? "SYNCED" : "----") << " | "
                      << "RDS: " << (status.rds_synced ? "SYNCED" : "----")
                      << std::endl;
            synced = status.rds_synced;
        }

        if (synced) {
            char new_rds[65];
            std::memset(new_rds, 0, sizeof(new_rds));
            int updated = si468x_get_rds_text(new_rds, sizeof(new_rds));
            if (updated > 0 && std::strlen(new_rds) > 0) {
                std::cout << "\n[RDS Update] " << new_rds << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Cleanup (not typically reached due to while(true), but present for safety)
    si468x_shutdown();
    return 0;
}
