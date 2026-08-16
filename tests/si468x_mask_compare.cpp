#include <iostream>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include "si468x.h"
#include "si468x_internal.h"

int main()
{
    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23;
    uint32_t channel_12a_hz = 223936000; // Channel 12A (SRG SSR F01)

    std::cout << "==================================================" << std::endl;
    std::cout << "   libsi468x Short Label Mask Verification Tool   " << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Initializing chip..." << std::endl;
    if (si468x_init(spi_dev, rst_pin, SI468X_BOOT_DAB) != SI468X_SUCCESS) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    std::cout << "libsi468x: Tuning to Channel 12A (223.936 MHz)..." << std::endl;
    if (si468x_set_frequency(channel_12a_hz) != SI468X_SUCCESS) {
        std::cerr << "Tuning failed!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Wait up to 3 seconds for frame sync lock
    std::cout << "Waiting for signal lock..." << std::endl;
    bool locked = false;
    for (int i = 0; i < 30; i++) {
        si468x_signal_status_t status;
        if (si468x_get_signal_status(&status) == 0 && status.sync_status) {
            locked = true;
            std::cout << "Locked! RSSI: " << (int)status.rssi << " dBuV | SNR: " << (int)status.snr << " dB" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!locked) {
        std::cerr << "Failed to lock onto Channel 12A. Please verify antenna." << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Wait 12 seconds for database compilation (active polling of signal status to flush interrupts)
    std::cout << "Waiting 12 seconds for station names to compile from air..." << std::endl;
    for (int i = 0; i < 24; i++) {
        si468x_signal_status_t status;
        si468x_get_signal_status(&status);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Retrieve service list (up to 32 services)
    si468x_service_t services[32];
    std::memset(services, 0, sizeof(services));
    int num_services = si468x_get_service_list(services, 32);

    if (num_services <= 0) {
        std::cerr << "No services found in database payload!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << " " << std::left << std::setw(20) << "Long Label"
              << " | " << std::setw(6) << "Mask"
              << " | " << std::setw(12) << "On-Chip Short"
              << " | " << std::setw(12) << "Local Decoded"
              << " | Match?" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;

    int matches = 0;

    for (int s = 0; s < num_services; s++) {
        char comp_label[17];
        char comp_short_label[9];
        uint16_t char_mask = 0;
        uint8_t subchannel_id = 0;

        std::memset(comp_label, 0, sizeof(comp_label));
        std::memset(comp_short_label, 0, sizeof(comp_short_label));

        // Fetch detailed component specs (Opcode 0xBB)
        int comp_ret = si468x_get_component_info(services[s].service_id, services[s].component_id, comp_label, comp_short_label, &char_mask, &subchannel_id);

        if (comp_ret == 0) {
            // Run our host-side decoder locally on the long label with the retrieved mask
            char local_short_label[9];
            std::memset(local_short_label, 0, sizeof(local_short_label));
            si468x_decode_short_label(comp_label, char_mask, local_short_label);

            bool equal = (std::strcmp(comp_short_label, local_short_label) == 0);
            if (equal) {
                matches++;
            }

            std::cout << " " << std::left << std::setw(20) << comp_label
                      << " | 0x" << std::hex << std::setw(4) << std::setfill('0') << char_mask << std::dec << std::setfill(' ')
                      << " | " << std::left << std::setw(12) << comp_short_label
                      << " | " << std::left << std::setw(12) << local_short_label
                      << " | " << (equal ? "YES" : "NO!") << std::endl;
        }
        else {
            std::cout << " " << std::left << std::setw(20) << services[s].label
                      << " | Failed to query component metadata" << std::endl;
        }
    }

    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "Verification Complete!" << std::endl;
    std::cout << "  Total Components Checked: " << num_services << std::endl;
    std::cout << "  Total Decoder Matches:    " << matches << " / " << num_services << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << "libsi468x: Performing clean hardware shutdown..." << std::endl;
    si468x_shutdown();
    return (matches == num_services) ? 0 : 1;
}
