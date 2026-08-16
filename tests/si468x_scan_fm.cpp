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
    std::cout << "\n[Signal] Interrupted. Performing clean hardware shutdown..." << std::endl;
    si468x_shutdown();
    std::exit(signum);
}

int main(int argc, char* argv[])
{
    si468x_enable_debug(1); // Enable diagnostic logging in tests
    // Register signal handlers for clean teardown on Ctrl+C/termination
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23;
    int rssi_threshold = 18; // minimum RSSI in dBuV to consider a station active (eliminates low-level white noise)
    int snr_threshold = 10;   // minimum SNR in dB (ensures decent quality carrier lock)

    if (argc > 1) {
        spi_dev = argv[1];
    }
    if (argc > 2) {
        rst_pin = std::stoi(argv[2]);
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "     libsi468x Autonomous FM & RDS Band Scanner    " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "SPI Device:     " << spi_dev << std::endl;
    std::cout << "Reset Pin:      " << rst_pin << std::endl;
    std::cout << "FM Scan Range:  87.5 MHz - 108.0 MHz (100 kHz steps)" << std::endl;
    std::cout << "Thresholds:     RSSI >= " << rssi_threshold << " dBuV | SNR >= " << snr_threshold << " dB" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Initializing chip in FM-HD mode..." << std::endl;
    if (si468x_init(spi_dev, rst_pin, SI468X_BOOT_FMHD) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    // Read chip info
    si468x_chip_info_t info;
    if (si468x_get_chip_info(&info) == SI468X_SUCCESS) {
        std::cout << "Chip ID: 0x" << std::hex << (int)info.chip_id
                  << " | ROM: 0x" << (int)info.rom_id
                  << " | FW: " << std::dec << (info.fw_version >> 8) << "." << (info.fw_version & 0xFF)
                  << " | Patch: " << (info.patch_version >> 8) << "." << (info.patch_version & 0xFF) << std::endl;
    }
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "Starting full FM band sweep..." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    int total_stations_found = 0;
    int total_rds_decoded = 0;

    // Sweep from 87.5 MHz (87500 kHz) to 108.0 MHz (108000 kHz) in 100 kHz steps
    for (uint32_t freq_khz = 87500; freq_khz <= 108000; freq_khz += 100) {
        double mhz = freq_khz / 1000.0;

        // Print progress
        std::cout << "\rScanning: " << std::fixed << std::setprecision(1) << mhz << " MHz... " << std::flush;

        if (si468x_tune_fm(freq_khz) != SI468X_SUCCESS) {
            continue;
        }

        // Read signal metrics
        si468x_fm_status_t status;
        if (si468x_get_fm_status(&status) == SI468X_SUCCESS) {
            int8_t rssi_signed = (int8_t)status.rssi;

            // Check if active station is found (including strict absolute frequency offset check <= 15 kHz)
            if (rssi_signed >= rssi_threshold && status.snr >= snr_threshold && std::abs((int)status.freq_offset) <= 15) {
                total_stations_found++;
                std::cout << "\n--------------------------------------------------" << std::endl;
                std::cout << "FOUND: " << std::fixed << std::setprecision(1) << mhz << " MHz" << std::endl;
                std::cout << "  RSSI:      " << (int)rssi_signed << " dBuV" << std::endl;
                std::cout << "  SNR:       " << (int)status.snr << " dB" << std::endl;
                std::cout << "  Offset:    " << (int)status.freq_offset << " kHz" << std::endl;
                std::cout << "  HD Sync:   " << (status.hd_synced ? "YES" : "NO") << std::endl;
                std::cout << "  RDS Sync:  " << (status.rds_synced ? "YES" : "NO") << std::endl;
                std::cout << "  DWELL: Waiting up to 12s to acquire RDS Station Text..." << std::endl;

                // Stop and dwell on this station for up to 12 seconds to acquire RDS text
                auto dwell_start = std::chrono::steady_clock::now();
                bool rds_text_acquired = false;
                char rds_text[65];
                std::memset(rds_text, 0, sizeof(rds_text));

                while (std::chrono::steady_clock::now() - dwell_start < std::chrono::seconds(12)) {
                    // Update sync metrics
                    if (si468x_get_fm_status(&status) == SI468X_SUCCESS && status.rds_synced) {
                        int updated = si468x_get_rds_text(rds_text, sizeof(rds_text));
                        if (updated > 0 && std::strlen(rds_text) > 0) {
                            std::cout << "  -> RDS Text: \"" << rds_text << "\"" << std::endl;
                            rds_text_acquired = true;
                            total_rds_decoded++;
                            break; // Stop dwelling immediately once RDS is decoded successfully
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                }

                if (!rds_text_acquired) {
                    std::cout << "  -> RDS Text: (No RDS metadata decoded within dwell time)" << std::endl;
                }
                std::cout << "--------------------------------------------------" << std::endl;
            }
        }
    }

    std::cout << "\rScan Sweep Complete!" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "  Summary:" << std::endl;
    std::cout << "  Total Active Stations Discovered:  " << total_stations_found << std::endl;
    std::cout << "  Total RDS Station Labels Decoded:  " << total_rds_decoded << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Performing clean hardware shutdown..." << std::endl;
    si468x_shutdown();
    return 0;
}
