/*
 *    Copyright (C) 2026
 *    si468x_scan.cpp - Diagnostic DAB Frequency Scanner Utility
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <cstring>
#include "si468x.h"

struct DABChannel {
    const char* label;
    uint32_t frequency_hz;
};

static const DABChannel DAB_BAND_III[] = {
    { "5A",  174928000 }, { "5B",  176640000 }, { "5C",  178352000 }, { "5D",  180064000 },
    { "6A",  181936000 }, { "6B",  183648000 }, { "6C",  185360000 }, { "6D",  187072000 },
    { "7A",  188928000 }, { "7B",  190640000 }, { "7C",  192352000 }, { "7D",  194064000 },
    { "8A",  195936000 }, { "8B",  197648000 }, { "8C",  199360000 }, { "8D",  201072000 },
    { "9A",  202928000 }, { "9B",  204640000 }, { "9C",  206352000 }, { "9D",  208064000 },
    { "10A", 209936000 }, { "10B", 211648000 }, { "10C", 213360000 }, { "10D", 215072000 },
    { "11A", 216928000 }, { "11B", 218640000 }, { "11C", 220352000 }, { "11D", 222064000 },
    { "12A", 223936000 }, { "12B", 225648000 }, { "12C", 227360000 }, { "12D", 229072000 },
    { "13A", 230784000 }, { "13B", 232496000 }, { "13C", 234208000 }, { "13D", 235776000 },
    { "13E", 237488000 }, { "13F", 239200000 }
};

int main(int argc, char** argv)
{
    std::cout << "==================================================" << std::endl;
    std::cout << "  libsi468x DAB Band III Diagnostic Frequency Scan" << std::endl;
    std::cout << "==================================================" << std::endl;

    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23; // Broadcom GPIO 23 (Physical Pin 16)

    if (argc > 1) {
        spi_dev = argv[1];
    }
    if (argc > 2) {
        rst_pin = std::stoi(argv[2]);
    }

    std::cout << "SPI Device: " << spi_dev << std::endl;
    std::cout << "Reset Pin:  GPIO " << rst_pin << std::endl;
    std::cout << "==================================================" << std::endl;

    // Boot the chip in DAB mode
    int ret = si468x_init(spi_dev, rst_pin, SI468X_BOOT_DAB);
    if (ret != SI468X_SUCCESS) {
        std::cerr << "Initialization failed! (Code: " << ret << ")" << std::endl;
        std::cerr << "Please ensure SPI and GPIO permissions are correct (try running under sudo)." << std::endl;
        return 1;
    }

    std::cout << "Chip booted successfully in DAB mode!" << std::endl;
    std::cout << "Starting sweep of all 38 standard Band III channels..." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << " CH | Frequency  | RSSI   | SNR | Sync status" << std::endl;
    std::cout << "----|------------|--------|-----|-----------------" << std::endl;

    int total_ensembles = 0;
    int total_stations = 0;

    for (const auto& ch : DAB_BAND_III) {
        std::cout << " " << std::left << std::setw(3) << ch.label << "| "
                  << std::right << std::setw(3) << (ch.frequency_hz / 1000000) << "."
                  << std::left << std::setw(5) << (ch.frequency_hz % 1000000 / 1000) << " MHz| ";

        // Tune to the channel frequency
        ret = si468x_set_frequency(ch.frequency_hz);
        if (ret != SI468X_SUCCESS) {
            std::cout << "Tuning failed" << std::endl;
            continue;
        }

        // Wait for RF and AGC lock
        std::this_thread::sleep_for(std::chrono::milliseconds(600));

        // Query signal metrics
        si468x_signal_status_t status;
        ret = si468x_get_signal_status(&status);
        if (ret != SI468X_SUCCESS) {
            std::cout << "Query failed" << std::endl;
            continue;
        }

        std::cout << std::right << std::setw(3) << (int)status.rssi << " dBuV | "
                  << std::setw(3) << (int)status.snr << " | ";

        if (status.sync_status) {
            std::cout << "[SYNCED] *** Ensemble Found! ***" << std::endl;
            total_ensembles++;

            // Wait a moment for FIC database processing to finish
            std::this_thread::sleep_for(std::chrono::milliseconds(400));

            // Fetch service list from chip's memory
            si468x_service_t services[32];
            std::memset(services, 0, sizeof(services));
            int num_services = si468x_get_service_list(services, 32);

            if (num_services > 0) {
                std::cout << "    ================================================" << std::endl;
                std::cout << "      Discovered " << num_services << " active services on Channel " << ch.label << ":" << std::endl;
                std::cout << "    ================================================" << std::endl;
                for (int s = 0; s < num_services; s++) {
                    std::cout << "      " << std::setw(2) << (s + 1) << ". "
                              << std::left << std::setw(17) << services[s].label
                              << " (" << std::left << std::setw(8) << services[s].short_label << ") "
                              << " | SId: 0x" << std::hex << services[s].service_id
                              << " | CompId: " << std::dec << services[s].component_id
                              << " | DAB+ AAC" << std::endl;
                    total_stations++;
                }
                std::cout << "    ================================================" << std::endl;
            }
            else {
                std::cout << "      (Ensemble locked, but no active service tables read yet)" << std::endl;
            }
        }
        else {
            std::cout << "No Signal" << std::endl;
        }
    }

    // Release hardware bus
    si468x_shutdown();

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "Scan Complete!" << std::endl;
    std::cout << "  Total Active Ensembles Discovered: " << total_ensembles << std::endl;
    std::cout << "  Total Active Radio Stations Found: " << total_stations << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
