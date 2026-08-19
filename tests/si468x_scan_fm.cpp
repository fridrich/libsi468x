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

    // Configure European 100 kHz frequency grid (spacing = 10 * 10 kHz)
    std::cout << "Configuring European FM 100 kHz seek grid..." << std::endl;
    if (si468x_set_property(SI468X_PROP_FM_SEEK_FREQUENCY_SPACING, 10) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to set seek grid spacing property!" << std::endl;
    }

    // Configure hardware seek RSSI and SNR validation thresholds dynamically using the test thresholds
    std::cout << "Configuring hardware seek thresholds: RSSI >= " << rssi_threshold << " dBuV, SNR >= " << snr_threshold << " dB..." << std::endl;
    if (si468x_set_property(SI468X_PROP_FM_VALID_RSSI_THRESHOLD, rssi_threshold) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to set on-chip RSSI threshold property!" << std::endl;
    }
    if (si468x_set_property(SI468X_PROP_FM_VALID_SNR_THRESHOLD, snr_threshold) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to set on-chip SNR threshold property!" << std::endl;
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
    std::cout << "Starting autonomous FM seek-driven sweep..." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    int total_stations_found = 0;
    int total_rds_decoded = 0;

    // Start by tuning once to 87.5 MHz (the bottom of the FM band)
    uint32_t current_freq_khz = 87500;
    if (si468x_tune_fm(current_freq_khz) != SI468X_SUCCESS) {
        std::cerr << "Initial tuning failed!" << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (current_freq_khz < 108000) {
        std::cout << "Seeking for the next active carrier..." << std::flush;
        
        // Trigger seek up, wrap around disabled (0)
        if (si468x_fm_seek_start(1, 0) != SI468X_SUCCESS) {
            std::cerr << "Seek start failed!" << std::endl;
            break;
        }

        // Wait up to 5 seconds for the seek to complete and frequency to lock
        auto seek_start_time = std::chrono::steady_clock::now();
        bool seek_success = false;
        si468x_fm_status_t status;

        while (std::chrono::steady_clock::now() - seek_start_time < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            if (si468x_get_fm_status(&status) == SI468X_SUCCESS) {
                uint32_t status_khz = status.frequency_hz / 1000;
                
                // If the frequency has moved and is valid, seek is complete!
                if (status_khz > current_freq_khz && status_khz <= 108000) {
                    current_freq_khz = status_khz;
                    seek_success = true;
                    break;
                }
                
                // If the frequency remained the same and we've waited a bit, it means no more stations
                if (status_khz == current_freq_khz && std::chrono::steady_clock::now() - seek_start_time > std::chrono::seconds(1)) {
                    break;
                }
            }
        }

        if (!seek_success) {
            std::cout << "\nSeek finished (reached band limit)." << std::endl;
            break;
        }

        double mhz = current_freq_khz / 1000.0;
        int8_t rssi_signed = (int8_t)status.rssi;

        total_stations_found++;
        std::cout << "\n--------------------------------------------------" << std::endl;
        std::cout << "FOUND: " << std::fixed << std::setprecision(1) << mhz << " MHz" << std::endl;
        std::cout << "  RSSI:      " << (int)rssi_signed << " dBuV" << std::endl;
        std::cout << "  SNR:       " << (int)status.snr << " dB" << std::endl;
        std::cout << "  Offset:    " << (int)status.freq_offset << " kHz" << std::endl;
        std::cout << "  HD Sync:   " << (status.hd_synced ? "YES" : "NO") << std::endl;
        std::cout << "  RDS Sync:  " << (status.rds_synced ? "YES" : "NO") << std::endl;
        std::cout << "  DWELL: Waiting up to 12s to acquire RDS Station Text/Name..." << std::endl;

        // Stop and dwell on this station for up to 12 seconds to acquire RDS text and name
        auto dwell_start = std::chrono::steady_clock::now();
        bool rds_text_acquired = false;
        bool rds_name_acquired = false;
        char rds_text[65];
        char rds_name[9];
        std::memset(rds_text, 0, sizeof(rds_text));
        std::memset(rds_name, 0, sizeof(rds_name));

        while (std::chrono::steady_clock::now() - dwell_start < std::chrono::seconds(12)) {
            // Update sync metrics
            if (si468x_get_fm_status(&status) == SI468X_SUCCESS && status.rds_synced) {
                // Poll for station name (PS)
                int name_updated = si468x_get_rds_station_name(rds_name, sizeof(rds_name));
                if (name_updated > 0 && std::strlen(rds_name) > 0) {
                    std::cout << "  -> Station Name: \"" << rds_name << "\"" << std::endl;
                    rds_name_acquired = true;
                }

                // Poll for RadioText (RT)
                int updated = si468x_get_rds_text(rds_text, sizeof(rds_text));
                if (updated > 0 && std::strlen(rds_text) > 0) {
                    std::cout << "  -> RDS Text:     \"" << rds_text << "\"" << std::endl;
                    rds_text_acquired = true;
                    total_rds_decoded++;
                    break; // Stop dwelling immediately once RDS Text is decoded successfully
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }

        if (!rds_text_acquired) {
            std::cout << "  -> RDS Text:     (No RDS metadata decoded within dwell time)" << std::endl;
        }
        std::cout << "--------------------------------------------------" << std::endl;
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
