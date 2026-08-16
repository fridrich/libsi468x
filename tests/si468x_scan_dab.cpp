/*
 *    Copyright (C) 2026
 *    si468x_scan.cpp - Diagnostic DAB Frequency Scanner Utility with Active Playback Test
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
    { "5A",  174928000 },
    { "5B",  176640000 },
    { "5C",  178352000 },
    { "5D",  180064000 },
    { "6A",  181936000 },
    { "6B",  183648000 },
    { "6C",  185360000 },
    { "6D",  187072000 },
    { "7A",  188928000 },
    { "7B",  190640000 },
    { "7C",  192352000 },
    { "7D",  194064000 },
    { "8A",  195936000 },
    { "8B",  197648000 },
    { "8C",  199360000 },
    { "8D",  201072000 },
    { "9A",  202928000 },
    { "9B",  204640000 },
    { "9C",  206352000 },
    { "9D",  208064000 },
    { "10A", 209936000 },
    { "10B", 211648000 },
    { "10C", 213360000 },
    { "10D", 215072000 },
    { "11A", 216928000 },
    { "11B", 218640000 },
    { "11C", 220352000 },
    { "11D", 222064000 },
    { "12A", 223936000 },
    { "12B", 225648000 },
    { "12C", 227360000 },
    { "12D", 229072000 },
    { "13A", 230784000 },
    { "13B", 232496000 },
    { "13C", 234208000 },
    { "13D", 235776000 },
    { "13E", 237488000 },
    { "13F", 239200000 }
};

int main(int argc, char** argv)
{
    std::cout << "==================================================" << std::endl;
    std::cout << "  libsi468x DAB Band III Diagnostic Frequency Scan" << std::endl;
    std::cout << "  (Active 10-Second Headphone Audio Playback Test) " << std::endl;
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

    // Boot the chip in DAB mode (analog audio is unconditionally enabled by default on init!)
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
        std::cout.flush();

        // Tune to the channel frequency
        ret = si468x_set_frequency(ch.frequency_hz);
        if (ret != SI468X_SUCCESS) {
            std::cout << "Tuning cmd rejected" << std::endl;
            continue;
        }

        // Query final signal status (library handles RF lock internally)
        si468x_signal_status_t status;
        ret = si468x_get_signal_status(&status);

        if (ret != SI468X_SUCCESS || status.sync_status == 0) {
            std::cout << "  " << (int)status.rssi << " dBuV |   0 | No Signal" << std::endl;
            continue;
        }

        std::cout << std::right << std::setw(3) << (int)status.rssi << " dBuV | "
                  << std::setw(3) << (int)status.snr << " | ";

        // Query the current tuned Ensemble Name
        char ensemble_label[17];
        uint16_t ensemble_id = 0;
        std::memset(ensemble_label, 0, sizeof(ensemble_label));
        if (si468x_get_ensemble_info(ensemble_label, &ensemble_id) == 0) {
            std::cout << "[SYNCED] *** Ensemble: " << ensemble_label << " (EId: 0x" << std::hex << ensemble_id << std::dec << ") ***" << std::endl;
        }
        else {
            std::cout << "[SYNCED] *** Ensemble Found! ***" << std::endl;
        }
        total_ensembles++;

        // Fetch service list from chip's memory
        si468x_service_t services[32];
        std::memset(services, 0, sizeof(services));
        int num_services = si468x_get_service_list(services, 32);

        if (num_services > 0) {
            std::cout << "    ================================================" << std::endl;
            std::cout << "      Discovered " << num_services << " active services on Channel " << ch.label << ":" << std::endl;
            std::cout << "    ================================================" << std::endl;
            for (int s = 0; s < num_services; s++) {
                // Dynamically fetch detailed component info and short label directly from the chip over SPI (Opcode 0xBB)
                char comp_label[17];
                char comp_short_label[9];
                uint8_t subchannel_id = 0;
                std::memset(comp_label, 0, sizeof(comp_label));
                std::memset(comp_short_label, 0, sizeof(comp_short_label));

                uint16_t char_mask = 0;
                int comp_ret = si468x_get_component_info(services[s].service_id, services[s].component_id, comp_label, comp_short_label, &char_mask, &subchannel_id);

                std::cout << "      " << std::setw(2) << (s + 1) << ". "
                          << std::left << std::setw(17) << services[s].label;

                if (comp_ret == 0 && std::strlen(comp_short_label) > 0 && comp_short_label[0] != ' ') {
                    std::cout << " (" << std::left << std::setw(8) << comp_short_label << ") "
                              << " | SId: 0x" << std::hex << services[s].service_id
                              << " | CompId: " << std::dec << services[s].component_id
                              << " | SubChId: " << (int)subchannel_id
                              << " | Mask: 0x" << std::hex << std::setw(4) << std::setfill('0') << char_mask << std::dec << std::setfill(' ')
                              << " | CompLabel: '" << comp_label << "'";
                }
                else {
                    std::cout << " (" << std::left << std::setw(8) << services[s].short_label << ") "
                              << " | SId: 0x" << std::hex << services[s].service_id
                              << " | CompId: " << std::dec << services[s].component_id
                              << " | SubChId: " << (int)services[s].audio_type; // Display database Subchannel ID
                }
                std::cout << " | DAB+ AAC" << std::endl;
                total_stations++;
            }
            std::cout << "    ================================================" << std::endl;

            // ACTIVE AUDIO PLAYBACK DIAGNOSTIC TEST (Plays the 1st discovered station for 20 seconds!)
            std::cout << "    >>> [TEST] Activating 20-Second Hardware Headphone Playback..." << std::endl;
            std::cout << "    >>> Station: '" << services[0].label << "' (SId: 0x" << std::hex << services[0].service_id
                      << ", CompId: " << std::dec << services[0].component_id << ")" << std::endl;

            // Trigger actual hardware play command
            int play_ret = si468x_play_service(services[0].service_id, services[0].component_id);
            if (play_ret == SI468X_SUCCESS) {
                // Un-mute the analog DAC by setting hardware volume property 0x0300 to 55
                si468x_set_volume(55);

                std::cout << "    >>> Playback active! Streaming analog audio directly to board headphone jack..." << std::endl;
                for (int sec = 1; sec <= 20; sec++) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                    // Periodically print signal updates during playback to monitor stability
                    si468x_signal_status_t play_status;
                    if (si468x_get_signal_status(&play_status) == SI468X_SUCCESS) {
                        std::cout << "        [Play Sec " << sec << "/20] RSSI: " << (int)play_status.rssi
                                  << " dBuV | SNR: " << (int)play_status.snr << " dB";

                        // Query dynamic component info during playback of the active service!
                        char play_comp_label[17];
                        char play_comp_short[9];
                        uint8_t play_subchannel_id = 0;
                        std::memset(play_comp_label, 0, sizeof(play_comp_label));
                        std::memset(play_comp_short, 0, sizeof(play_comp_short));

                        if (si468x_get_component_info(services[0].service_id, services[0].component_id, play_comp_label, play_comp_short, nullptr, &play_subchannel_id) == 0) {
                            std::cout << " | PlayLabel: '" << play_comp_label << "' (" << play_comp_short << ") | SubChId: " << (int)play_subchannel_id;
                        }
                        std::cout << std::endl;

                        // Query and display any live DLS scrolling text (song/track info) dynamically!
                        char dls_text[129];
                        int dls_ret = si468x_get_dls_text(dls_text, sizeof(dls_text));
                        if (dls_ret > 0) {
                            std::cout << "        [DLS UPDATE] " << dls_text << std::endl;
                        }
                    }
                }

                std::cout << "    >>> Stopping playback..." << std::endl;
                si468x_stop_service();
            }
            else {
                std::cerr << "    >>> [ERROR] START_DIGITAL play command rejected by chip! (Code: " << play_ret << ")" << std::endl;
            }
            std::cout << "    ================================================" << std::endl;

        }
        else {
            std::cout << "      (Ensemble locked, but no active service tables read)" << std::endl;
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
