/*
 *    Copyright (C) 2026
 *    si468x_play_dab.cpp - Unified DAB Playback Utility
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <csignal>
#include "si468x.h"

static std::atomic<bool> running(true);

void signal_handler(int signum)
{
    std::cout << "\n[Signal] Caught signal " << signum << ". Performing clean shutdown..." << std::endl;
    running = false;
}

uint32_t dab_freq_from_string(const std::string& name)
{
    static const std::map<std::string, uint32_t> freq_map = {
        {"5A", 174928000}, {"5B", 176640000}, {"5C", 178352000}, {"5D", 180064000},
        {"6A", 181936000}, {"6B", 183648000}, {"6C", 185360000}, {"6D", 187072000},
        {"7A", 188928000}, {"7B", 190640000}, {"7C", 192352000}, {"7D", 194064000},
        {"8A", 195936000}, {"8B", 197648000}, {"8C", 199360000}, {"8D", 201072000},
        {"9A", 202928000}, {"9B", 204640000}, {"9C", 206352000}, {"9D", 208064000},
        {"10A", 209936000}, {"10B", 211648000}, {"10C", 213360000}, {"10D", 215072000},
        {"11A", 216928000}, {"11B", 218640000}, {"11C", 220352000}, {"11D", 222064000},
        {"12A", 223936000}, {"12B", 225648000}, {"12C", 227360000}, {"12D", 229072000},
        {"13A", 230784000}, {"13B", 232496000}, {"13C", 234208000}, {"13D", 235776000},
        {"13E", 237488000}, {"13F", 239200000}
    };
    auto it = freq_map.find(name);
    if (it != freq_map.end()) {
        return it->second;
    }
    return 0; // Not found
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc < 3) {
        std::cerr << "Usage: sudo ./si468x_play_dab <ensemble> <service_id> [i2s]" << std::endl;
        std::cerr << "Example: sudo ./si468x_play_dab 12A 17361 i2s" << std::endl;
        return 1;
    }

    std::string ensemble_str = argv[1];
    uint32_t frequency_hz = dab_freq_from_string(ensemble_str);
    if (frequency_hz == 0) {
        std::cerr << "Invalid DAB ensemble string. Use formats like '12A' or '7A'." << std::endl;
        return 1;
    }

    uint32_t target_sid = std::stoul(argv[2]);
    bool enable_i2s = false;

    if (argc >= 4) {
        std::string out_mode = argv[3];
        if (out_mode == "i2s" || out_mode == "1") {
            enable_i2s = true;
        }
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "        libsi468x DAB/DAB+ Playback Utility       " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Target Ensemble:  " << ensemble_str << " (" << (frequency_hz / 1000000.0) << " MHz)" << std::endl;
    std::cout << "Target ServiceID: " << target_sid << std::endl;
    std::cout << "Audio Output:     " << (enable_i2s ? "I2S Digital (hw:2,0)" : "Analog Jack (Internal)") << std::endl;
    std::cout << "==================================================" << std::endl;

    if (si468x_set_audio_output(enable_i2s ? 1 : 0) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to pre-set audio path." << std::endl;
    }

    std::cout << "libsi468x: Booting chip in DAB mode..." << std::endl;
    if (si468x_init("/dev/spidev0.0", 23, SI468X_BOOT_DAB) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    std::cout << "libsi468x: Tuning synthesizer to " << ensemble_str << "..." << std::endl;
    if (si468x_set_frequency(frequency_hz) != SI468X_SUCCESS) {
        std::cerr << "DAB tuning failed!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    std::cout << "libsi468x: Waiting for DAB multiplex lock..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    si468x_signal_status_t status;
    if (si468x_get_signal_status(&status) != 0 || status.sync_status == 0) {
        std::cerr << "No stable DAB sync achieved! RSSI: " << (int)status.rssi << " dBuV" << std::endl;
        si468x_shutdown();
        return 1;
    }

    char ensemble_label[17];
    uint16_t ueid = 0;
    if (si468x_get_ensemble_info(ensemble_label, &ueid) == 0) {
        std::cout << "Locked onto Ensemble: '" << ensemble_label << "' (EId: 0x" << std::hex << ueid << std::dec << ")" << std::endl;
    }

    std::cout << "Querying on-chip service database..." << std::endl;
    si468x_service_t services[32];
    std::memset(services, 0, sizeof(services));
    int num_services = si468x_get_service_list(services, 32);

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
        std::cerr << "Service ID " << target_sid << " was not found in this ensemble!" << std::endl;
        std::cerr << "Available services on " << ensemble_str << ":" << std::endl;
        for (int i = 0; i < num_services; i++) {
            if (services[i].service_id < 0x10000 && std::strlen(services[i].label) > 0) {
                std::cerr << "  - " << services[i].label << " | SId: " << services[i].service_id << std::endl;
            }
        }
        si468x_shutdown();
        return 1;
    }

    std::cout << "Found Station: '" << station_name << "' (CompId: " << target_comp << ")" << std::endl;
    std::cout << "Triggering playback..." << std::endl;
    if (si468x_play_service(target_sid, target_comp) != SI468X_SUCCESS) {
        std::cerr << "Playback failed!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    std::cout << "libsi468x: Applying standard 50 volume..." << std::endl;
    si468x_set_volume(50);

    if (enable_i2s) {
        std::cout << "\n--------------------------------------------------" << std::endl;
        std::cout << "  To loop this digital audio to your speakers,    " << std::endl;
        std::cout << "  run the following ALSA loopback command on      " << std::endl;
        std::cout << "  another terminal window:                        " << std::endl;
        std::cout << "                                                  " << std::endl;
        std::cout << "  arecord -D hw:2,0 -f S16_LE -r 48000 -c 2 | aplay -D hw:3,0" << std::endl;
        std::cout << "--------------------------------------------------" << std::endl;
    }

    std::cout << "Press Ctrl+C to stop playback." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    char dls_text[129] = {0};
    int sec = 0;

    while (running) {
        si468x_signal_status_t play_status;
        if (si468x_get_signal_status(&play_status) == SI468X_SUCCESS) {
            std::cout << "\r[Telemetry] RSSI: " << std::setw(2) << (int)play_status.rssi << " dBuV | "
                      << "SNR: " << std::setw(2) << (int)play_status.snr << " dB" << std::flush;
        }

        if (si468x_get_dls_text(dls_text, sizeof(dls_text)) == 1) {
            std::cout << "\n[DLS] " << dls_text << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sec++;
    }

    std::cout << "\nlibsi468x: Stopping playback..." << std::endl;
    si468x_stop_service();
    si468x_shutdown();
    return 0;
}
