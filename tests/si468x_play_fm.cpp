/*
 *    Copyright (C) 2026
 *    si468x_play_fm.cpp - Unified FM Playback Utility
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include "si468x.h"

static std::atomic<bool> running(true);

void signal_handler(int signum)
{
    std::cout << "\n[Signal] Caught signal " << signum << ". Performing clean shutdown..." << std::endl;
    running = false;
}

int main(int argc, char* argv[])
{
    si468x_enable_debug(1); // Enable diagnostic logging in tests
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc < 2) {
        std::cerr << "Usage: sudo ./si468x_play_fm <frequency_khz> [i2s]" << std::endl;
        std::cerr << "Example: sudo ./si468x_play_fm 107000 i2s" << std::endl;
        return 1;
    }

    uint32_t frequency_khz = std::stoul(argv[1]);
    bool enable_i2s = false;

    if (argc >= 3) {
        std::string out_mode = argv[2];
        if (out_mode == "i2s" || out_mode == "1") {
            enable_i2s = true;
        }
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "        libsi468x FM Radio Playback Utility       " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Target Frequency: " << std::fixed << std::setprecision(1) << (frequency_khz / 1000.0) << " MHz" << std::endl;
    std::cout << "Audio Output:     " << (enable_i2s ? "I2S Digital (hw:2,0)" : "Analog Jack (Internal)") << std::endl;
    std::cout << "==================================================" << std::endl;

    if (si468x_set_audio_output(enable_i2s ? SI468X_AUDIO_I2S : SI468X_AUDIO_ANALOG) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to pre-set audio path." << std::endl;
    }

    std::cout << "libsi468x: Booting chip in FMHD mode..." << std::endl;
    if (si468x_init("/dev/spidev0.0", 23, SI468X_BOOT_FMHD) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    std::cout << "libsi468x: Tuning synthesizer..." << std::endl;
    if (si468x_tune_fm(frequency_khz) != SI468X_SUCCESS) {
        std::cerr << "FM tuning failed!" << std::endl;
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

    char rds_text[65] = {0};
    int sec = 0;

    while (running) {
        si468x_fm_status_t status;
        if (si468x_get_fm_status(&status) == SI468X_SUCCESS) {
            std::cout << "\r[Telemetry] RSSI: " << std::setw(2) << (int)status.rssi << " dBuV | "
                      << "SNR: " << std::setw(2) << (int)status.snr << " dB | "
                      << "Offset: " << std::setw(2) << (int)status.freq_offset << " kHz" << std::flush;
        }

        if (si468x_get_rds_text(rds_text, sizeof(rds_text)) == 1) {
            std::cout << "\n[RDS RT] " << rds_text << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sec++;
    }

    std::cout << "\nlibsi468x: Shutting down chip and releasing bus..." << std::endl;
    si468x_shutdown();
    return 0;
}
