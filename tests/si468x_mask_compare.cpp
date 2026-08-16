#include <iostream>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include "si468x.h"
#include "si468x_internal.h"

struct StationRef {
    std::string long_label;
    std::string short_program;
};

// Helper to parse stations.json without external JSON library dependencies
std::map<uint32_t, StationRef> load_stations_ref()
{
    std::map<uint32_t, StationRef> stations;
    std::ifstream file("/home/fstrba/reverse-enginner/stations.json");
    if (!file) {
        std::cerr << "Warning: Could not open stations.json for comparative verification!" << std::endl;
        return stations;
    }
    std::string line;
    uint32_t current_sid = 0;
    std::string current_program = "";
    std::string current_short = "";

    while (std::getline(file, line)) {
        if (line.find("\"service_id\":") != std::string::npos) {
            size_t pos = line.find(":");
            std::string sub = line.substr(pos + 1);
            size_t comma = sub.find(",");
            if (comma != std::string::npos) {
                sub = sub.substr(0, comma);
            }
            std::stringstream ss(sub);
            ss >> current_sid;
        }
        else if (line.find("\"program\":") != std::string::npos) {
            size_t start = line.find("\"program\":") + 10;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                current_program = line.substr(first_quote + 1, second_quote - first_quote - 1);
                while (!current_program.empty() && current_program.back() == ' ') {
                    current_program.pop_back();
                }
            }
        }
        else if (line.find("\"short_program\":") != std::string::npos) {
            size_t start = line.find("\"short_program\":") + 16;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                current_short = line.substr(first_quote + 1, second_quote - first_quote - 1);
            }
            if (current_sid != 0) {
                StationRef ref;
                ref.long_label = current_program;
                ref.short_program = current_short;
                stations[current_sid] = ref;
                current_sid = 0;
            }
        }
    }
    return stations;
}

int main()
{
    const char* spi_dev = "/dev/spidev0.0";
    int rst_pin = 23;
    uint32_t channel_12a_hz = 223936000; // Channel 12A (SRG SSR F01, Ensemble ID 0x4041)

    std::cout << "==========================================================================" << std::endl;
    std::cout << "   libsi468x Short Label Mask Verification Tool                           " << std::endl;
    std::cout << "==========================================================================" << std::endl;

    std::cout << "Loading reference labels from stations.json..." << std::endl;
    auto ref_stations = load_stations_ref();
    std::cout << "Loaded " << ref_stations.size() << " reference station records." << std::endl;

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

    // Retrieve initial service list to find SId for compilation trigger
    si468x_service_t services[32];
    std::memset(services, 0, sizeof(services));
    int num_services = si468x_get_service_list(services, 32);

    if (num_services <= 0) {
        std::cerr << "No services found in database payload!" << std::endl;
        si468x_shutdown();
        return 1;
    }

    // Find first audio service SId (typically < 0x10000)
    int play_index = 0;
    for (int s = 0; s < num_services; s++) {
        if (services[s].service_id < 0x10000) {
            play_index = s;
            break;
        }
    }

    // Start decoding the first valid audio service component to trigger dynamic mask compilation
    std::cout << "libsi468x: Triggering playback on audio service (SId: 0x"
              << std::hex << services[play_index].service_id << std::dec << ") to compile station masks..." << std::endl;
    si468x_play_service(services[play_index].service_id, services[play_index].component_id);

    // Wait 15 seconds quietly for the background decoders to parse the FIC/FIB tables
    std::cout << "Waiting 15 seconds quietly for station names and masks to compile..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(15));

    // Retrieve service list again while playback is active to read fully compiled records
    std::memset(services, 0, sizeof(services));
    num_services = si468x_get_service_list(services, 32);

    if (num_services <= 0) {
        std::cerr << "No services found in database payload after waiting!" << std::endl;
        si468x_stop_service();
        si468x_shutdown();
        return 1;
    }

    std::cout << "\n--------------------------------------------------------------------------------------------------" << std::endl;
    std::cout << " " << std::left << std::setw(18) << "Long Label"
              << " | " << std::setw(6) << "Mask"
              << " | " << std::setw(12) << "On-Chip"
              << " | " << std::setw(12) << "Local"
              << " | " << std::setw(12) << "stations.json"
              << " | Valid?" << std::endl;
    std::cout << "--------------------------------------------------------------------------------------------------" << std::endl;

    int total_checked = 0;
    int matches = 0;

    for (int s = 0; s < num_services; s++) {
        // Skip data services for label comparison
        if (services[s].service_id >= 0x10000) {
            continue;
        }

        char comp_label[17];
        char comp_short_label[9];
        uint16_t char_mask = 0;
        uint8_t subchannel_id = 0;

        std::memset(comp_label, 0, sizeof(comp_label));
        std::memset(comp_short_label, 0, sizeof(comp_short_label));

        // Construct globally-unique 32-bit Service ID (Ensemble ID 0x4041 << 16 | SId)
        uint32_t global_sid = (0x4041 << 16) | (services[s].service_id & 0xFFFF);

        // Fetch detailed component specs (Opcode 0xBB) using global SId and global Component ID
        int comp_ret = si468x_get_component_info(global_sid, services[s].component_id, comp_label, comp_short_label, &char_mask, &subchannel_id);

        if (comp_ret == 0 && std::strlen(services[s].label) > 0 && services[s].label[0] != ' ') {
            total_checked++;

            char local_short_label[9];
            std::memset(local_short_label, 0, sizeof(local_short_label));

            // Use our local C-API mask decoder to reconstruct the short labels!
            si468x_decode_short_label(services[s].label, char_mask, local_short_label);

            std::string ref_short = "(missing)";
            std::string clean_comp_label(services[s].label);
            while (!clean_comp_label.empty() && clean_comp_label.back() == ' ') {
                clean_comp_label.pop_back();
            }

            auto it = ref_stations.find(services[s].service_id);
            if (it != ref_stations.end()) {
                ref_short = it->second.short_program;
            }

            std::string clean_local(local_short_label);
            while (!clean_local.empty() && clean_local.back() == ' ') {
                clean_local.pop_back();
            }

            bool equal = (clean_local == ref_short);
            if (equal) {
                matches++;
            }

            std::cout << " " << std::left << std::setw(18) << clean_comp_label
                      << " | 0x" << std::hex << std::setw(4) << std::setfill('0') << char_mask << std::dec << std::setfill(' ')
                      << " | " << std::left << std::setw(12) << comp_short_label
                      << " | " << std::left << std::setw(12) << local_short_label
                      << " | " << std::left << std::setw(12) << ref_short
                      << " | " << (equal ? "YES" : "NO!") << std::endl;
        }
    }

    std::cout << "--------------------------------------------------------------------------------------------------" << std::endl;
    std::cout << "Verification Complete!" << std::endl;
    std::cout << "  Total Active Stations Checked: " << total_checked << std::endl;
    std::cout << "  Total JSON Reference Matches:  " << matches << " / " << total_checked << std::endl;
    std::cout << "==================================================" << std::endl;

    si468x_shutdown();
    return (matches == total_checked && total_checked > 0) ? 0 : 1;
}
