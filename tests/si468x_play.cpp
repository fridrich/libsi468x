#include <iostream>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include "si468x.h"

static bool running = true;

// Signal handler to perform clean shutdown upon program termination
void signal_handler(int signum)
{
    std::cout << "\n[Signal] Caught signal " << signum << ". Stopping playback..." << std::endl;
    running = false;
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    uint32_t frequency_hz = 107000000; // Default: FM 107.0 MHz (107000 kHz)
    bool is_dab = false;
    bool enable_i2s = false;

    if (argc > 1) {
        uint32_t arg_val = std::stoul(argv[1]);
        if (arg_val > 108000) {
            // DAB frequency in Hz (e.g. 223936000)
            frequency_hz = arg_val;
            is_dab = true;
        }
        else {
            // FM frequency in kHz (e.g. 107000)
            frequency_hz = arg_val * 1000;
            is_dab = false;
        }
    }

    if (argc > 2) {
        std::string out_arg = argv[2];
        if (out_arg == "i2s" || out_arg == "1") {
            enable_i2s = true;
        }
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "        libsi468x Unified Playback Utility        " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Mode:             " << (is_dab ? "DAB/DAB+" : "FM Analog") << std::endl;
    std::cout << "Target Frequency: " << (frequency_hz / 1000000.0) << " MHz" << std::endl;
    std::cout << "Audio Output:     " << (enable_i2s ? "I2S Digital (hw:3,0)" : "Analog Jack (Internal)") << std::endl;
    std::cout << "==================================================" << std::endl;

    // Configure audio output to the selected mode BEFORE initialization!
    std::cout << "libsi468x: Pre-configuring audio path for boot..." << std::endl;
    if (si468x_set_audio_output(enable_i2s ? 1 : 0) != SI468X_SUCCESS) {
        std::cerr << "Warning: Failed to pre-set audio path!" << std::endl;
    }

    std::cout << "libsi468x: Initializing chip..." << std::endl;
    int boot_mode = is_dab ? SI468X_BOOT_DAB : SI468X_BOOT_FMHD;
    if (si468x_init("/dev/spidev0.0", 23, boot_mode) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    std::cout << "libsi468x: Tuning carrier..." << std::endl;
    if (is_dab) {
        if (si468x_set_frequency(frequency_hz) != SI468X_SUCCESS) {
            std::cerr << "DAB tuning failed!" << std::endl;
            si468x_shutdown();
            return 1;
        }

        // Wait 3 seconds for lock and retrieve service list
        std::this_thread::sleep_for(std::chrono::seconds(3));
        si468x_service_t services[32];
        std::memset(services, 0, sizeof(services));
        int num_services = si468x_get_service_list(services, 32);
        if (num_services <= 0) {
            std::cerr << "No DAB services found on this frequency!" << std::endl;
            si468x_shutdown();
            return 1;
        }

        // Find the first audio service
        int play_index = -1;
        for (int s = 0; s < num_services; s++) {
            if (services[s].service_id < 0x10000 && std::strlen(services[s].label) > 0 && services[s].label[0] != ' ') {
                play_index = s;
                break;
            }
        }
        if (play_index == -1) {
            play_index = 0;
        }

        std::cout << "libsi468x: Activating playback on \"" << services[play_index].label << "\"..." << std::endl;
        si468x_play_service(services[play_index].service_id, services[play_index].component_id);
    }
    else {
        // FM Mode
        if (si468x_tune_fm(frequency_hz / 1000) != SI468X_SUCCESS) {
            std::cerr << "FM tuning failed!" << std::endl;
            si468x_shutdown();
            return 1;
        }
    }

    // Set standard operating volume AFTER tuning is complete
    std::cout << "libsi468x: Setting audio volume to 50..." << std::endl;
    si468x_set_volume(50);

    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << "   Live Radio Playback Active!                    " << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    if (enable_i2s) {
        std::cout << "  To loop this digital audio to your speakers,    " << std::endl;
        std::cout << "  run the following ALSA loopback command on      " << std::endl;
        std::cout << "  another terminal window:                        " << std::endl;
        std::cout << "                                                  " << std::endl;
        std::cout << "  arecord -D hw:2,0 -f S16_LE -r 48000 -c 2 | aplay -D hw:3,0" << std::endl;
        std::cout << "                                                  " << std::endl;
        std::cout << "  (hw:2,0 is the 'dabboard' I2S capture card)     " << std::endl;
        std::cout << "  (hw:3,0 is the default USB Audio output)        " << std::endl;
        std::cout << "--------------------------------------------------" << std::endl;
    }

    std::cout << "Press Ctrl+C to stop and shut down." << std::endl;

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "libsi468x: Stopping playback..." << std::endl;
    if (is_dab) {
        si468x_stop_service();
    }
    si468x_shutdown();
    std::cout << "Exited cleanly." << std::endl;
    return 0;
}
