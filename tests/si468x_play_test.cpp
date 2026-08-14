/*
 *    Copyright (C) 2026
 *    si468x_play_test.cpp - Interactive Long Play and DLS Metadata Test Utility
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include "si468x.h"

int main()
{
    std::cout << "==================================================" << std::endl;
    std::cout << "  libsi468x Interactive Play and Metadata Test Tool" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 1. Initialize Chip in DAB Mode
    std::cout << "Initializing driver on /dev/spidev0.0..." << std::endl;
    int ret = si468x_init("/dev/spidev0.0", 23, SI468X_BOOT_DAB);
    if (ret != 0) {
        std::cerr << "Initialization failed! Code: " << ret << std::endl;
        return 1;
    }

    // 2. Tune to Channel 12A (224096000 Hz / 224.096 MHz)
    uint32_t frequency_hz = 224096000;
    std::cout << "Tuning to Channel 12A (224.096 MHz)..." << std::endl;
    ret = si468x_set_frequency(frequency_hz);
    if (ret != 0) {
        std::cerr << "Tuning failed! Code: " << ret << std::endl;
        si468x_shutdown();
        return 1;
    }

    // 3. Poll for lock
    si468x_signal_status_t status;
    ret = si468x_get_signal_status(&status);
    if (ret != 0 || status.sync_status == 0) {
        std::cerr << "No stable DAB sync achieved on 12A! RSSI: "
                  << (int)status.rssi << " dBuV" << std::endl;
        si468x_shutdown();
        return 1;
    }

    char ensemble_label[17];
    uint16_t ueid = 0;
    if (si468x_get_ensemble_info(ensemble_label, &ueid) == 0) {
        std::cout << "Locked onto Ensemble: '" << ensemble_label
                  << "' (EId: 0x" << std::hex << ueid << std::dec << ")" << std::endl;
    }

    // 4. Retrieve Service List and locate Service ID 17361 (0x43D1)
    std::cout << "Querying on-chip service database to resolve Service 17361..." << std::endl;
    si468x_service_t services[32];
    int num_services = si468x_get_service_list(services, 32);

    uint32_t target_sid = 17361; // 0x43D1
    uint32_t target_comp = 0;
    bool found = false;
    std::string station_name = "";

    for (int i = 0; i < num_services; i++) {
        if (services[i].service_id == target_sid) {
            target_comp = services[i].component_id;
            station_name = services[i].label;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Service ID 17361 was not found in this ensemble!" << std::endl;
        std::cerr << "Available services on 12A:" << std::endl;
        for (int i = 0; i < num_services; i++) {
            std::cerr << "  - " << services[i].label << " | SId: " << services[i].service_id
                      << " | CompId: " << services[i].component_id << std::endl;
        }
        si468x_shutdown();
        return 1;
    }

    std::cout << "Found Station: '" << station_name << "' (SId: 0x" << std::hex << target_sid
              << ", CompId: " << std::dec << target_comp << ")" << std::endl;

    // 5. Play Service
    std::cout << "Triggering playback..." << std::endl;
    ret = si468x_play_service(target_sid, target_comp);
    if (ret != 0) {
        std::cerr << "Playback failed! Code: " << ret << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Unmute & Set Volume
    si468x_set_volume(55);

    // 6. Launch active telemetry polling thread
    std::atomic<bool> running(true);
    std::thread poll_thread([&running, target_sid, target_comp]() {
        int sec = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            sec++;

            si468x_signal_status_t play_status;
            if (si468x_get_signal_status(&play_status) == 0) {
                std::cout << "\r[Play " << std::setw(3) << sec << "s] RSSI: "
                          << (int)play_status.rssi << " dBuV | SNR: " << (int)play_status.snr << " dB";

                // Fetch dynamic component info
                char comp_label[17];
                char comp_short[9];
                uint8_t sub_id = 0;
                std::memset(comp_label, 0, sizeof(comp_label));
                std::memset(comp_short, 0, sizeof(comp_short));
                if (si468x_get_component_info(target_sid, target_comp, comp_label, comp_short, &sub_id) == 0) {
                    if (std::strlen(comp_label) > 0) {
                        std::cout << " | Comp: '" << comp_label << "' (" << comp_short << ") | SubCh: " << (int)sub_id;
                    }
                }

                std::cout << std::flush;
            }

            // Query dynamic DLS scrolling text
            char dls_text[129];
            int dls_ret = si468x_get_dls_text(dls_text, sizeof(dls_text));
            if (dls_ret > 0) {
                std::cout << "\n        [DLS UPDATE] " << dls_text << std::endl;
            }
        }
    });

    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "  Playback active! Streaming audio to board jack." << std::endl;
    std::cout << "  Waiting for dynamic DLS text to compile... " << std::endl;
    std::cout << "  Press ENTER to stop playback and exit." << std::endl;
    std::cout << "--------------------------------------------------\n" << std::endl;

    // Wait for ENTER
    std::cin.get();

    // 7. Cleanup & Stop
    std::cout << "Stopping playback..." << std::endl;
    running = false;
    if (poll_thread.joinable()) {
        poll_thread.join();
    }

    si468x_stop_service();
    si468x_shutdown();
    std::cout << "Exited cleanly." << std::endl;
    return 0;
}
