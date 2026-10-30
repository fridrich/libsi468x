/*
 * SPDX-FileCopyrightText: 2026 Fridrich Strba
 * SPDX-License-Identifier: MPL-2.0
 *    si468x_probe.cpp - Raw command access for hardware verification
 *
 *    sudo si468x_probe [--fmhd] [--tune <ch>] [--play <SId> <CompId>] [--fm <kHz>]
 *                      [--repeat N] [--delay-ms D] [-- <cmd hex> <read_len> ...]
 *    Without commands: boot, then decode GET_PART_INFO + GET_FUNC_INFO (board check).
 *    Output is lib-indexed: resp[i] = reply byte i-1.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include "si468x.h"
#include "si468x_internal.h"

struct Cmd {
    std::vector<uint8_t> bytes;
    size_t read_len;
};

static uint32_t channel_hz(const std::string& ch)
{
    static const std::map<std::string, uint32_t> m = {
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
    auto it = m.find(ch);
    return it == m.end() ? 0 : it->second;
}

static bool parse_hex(const char* s, std::vector<uint8_t>& out)
{
    size_t n = std::strlen(s);
    if (n == 0 || n % 2) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2) {
        char b[3] = { s[i], s[i + 1], 0 };
        char* end;
        out.push_back((uint8_t)std::strtoul(b, &end, 16));
        if (*end) {
            return false;
        }
    }
    return true;
}

static int run(const Cmd& c, uint8_t* resp)
{
    std::memset(resp, 0, c.read_len);
    int ret = send_command(c.bytes.data(), c.bytes.size(), resp, c.read_len);
    std::printf("cmd");
    for (uint8_t b : c.bytes) {
        std::printf(" %02x", b);
    }
    std::printf(" -> ret %d\n", ret);
    for (size_t i = 0; i < c.read_len; i++) {
        std::printf("  resp[%2zu]=0x%02x%s", i, resp[i], (i % 6 == 5) ? "\n" : "");
    }
    if (c.read_len >= 5) {
        std::printf("\n  status: CTS=%d ERR=%d STCINT=%d DSRVINT=%d PUP_STATE=%d%s\n",
                    !!(resp[1] & 0x80), !!(resp[1] & 0x40), !!(resp[1] & 0x01),
                    !!(resp[1] & 0x10), resp[4] >> 6,
                    (resp[1] & 0x40 && c.read_len > 5) ? (" errcode=" + std::to_string(resp[5])).c_str() : "");
    }
    else if (c.read_len) {
        std::printf("\n");
    }
    return ret;
}

int main(int argc, char** argv)
{
    int mode = SI468X_BOOT_DAB, repeat = 0, delay_ms = 0;
    uint32_t tune_hz = 0, fm_khz = 0, sid = 0, comp = 0;
    bool play = false;
    std::vector<Cmd> cmds;

    int i = 1;
    for (; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fmhd") {
            mode = SI468X_BOOT_FMHD;
        }
        else if (a == "--tune" && i + 1 < argc) {
            tune_hz = channel_hz(argv[++i]);
            if (!tune_hz) {
                std::fprintf(stderr, "unknown channel %s\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--play" && i + 2 < argc) {
            sid = std::strtoul(argv[++i], nullptr, 0);
            comp = std::strtoul(argv[++i], nullptr, 0);
            play = true;
        }
        else if (a == "--fm" && i + 1 < argc) {
            fm_khz = std::strtoul(argv[++i], nullptr, 0);
            mode = SI468X_BOOT_FMHD;
        }
        else if (a == "--repeat" && i + 1 < argc) {
            repeat = std::atoi(argv[++i]);
        }
        else if (a == "--delay-ms" && i + 1 < argc) {
            delay_ms = std::atoi(argv[++i]);
        }
        else if (a == "--") {
            i++;
            break;
        }
        else {
            std::fprintf(stderr, "usage: %s [--fmhd] [--tune ch] [--play SId CompId] [--fm kHz] "
                         "[--repeat N] [--delay-ms D] [-- <cmd hex> <read_len> ...]\n", argv[0]);
            return 2;
        }
    }
    for (; i + 1 < argc; i += 2) {
        Cmd c;
        if (!parse_hex(argv[i], c.bytes)) {
            std::fprintf(stderr, "bad hex: %s\n", argv[i]);
            return 2;
        }
        c.read_len = std::strtoul(argv[i + 1], nullptr, 0);
        cmds.push_back(c);
    }
    if (i < argc) {
        std::fprintf(stderr, "missing read_len after %s\n", argv[i]);
        return 2;
    }

    si468x_enable_debug(1);
    int ret = si468x_init("/dev/spidev0.0", 23, mode);
    std::printf("si468x_init(%s) -> %d\n", mode == SI468X_BOOT_DAB ? "DAB" : "FMHD", ret);
    if (ret != SI468X_SUCCESS) {
        std::printf("FAIL: boot. Check SPI enabled (/dev/spidev0.0), RSTB on BCM 23, run as root.\n");
        return 1;
    }
    if (tune_hz) {
        std::printf("si468x_set_frequency(%u) -> %d\n", tune_hz, si468x_set_frequency(tune_hz));
    }
    if (fm_khz) {
        std::printf("si468x_tune_fm(%u) -> %d\n", fm_khz, si468x_tune_fm(fm_khz));
    }
    if (play) {
        std::printf("si468x_play_service(0x%x, %u) -> %d\n", sid, comp, si468x_play_service(sid, comp));
    }

    uint8_t resp[4096];
    if (cmds.empty()) {
        // Board check: GET_PART_INFO (0x08) and GET_FUNC_INFO (0x12), reply lengths + 1
        int fail = 0;
        if (run({ { 0x08, 0x00 }, 24 }, resp) == SI468X_SUCCESS && !(resp[1] & 0x40)) {
            std::printf("part: Si%u chiprev %u romid %u\n", resp[9] | (resp[10] << 8), resp[5], resp[6]);
        }
        else {
            fail = 1;
        }
        if (run({ { 0x12, 0x00 }, 13 }, resp) == SI468X_SUCCESS && !(resp[1] & 0x40)) {
            std::printf("firmware: %u.%u.%u svn %u\n", resp[5], resp[6], resp[7],
                        resp[9] | (resp[10] << 8) | (resp[11] << 16) | ((uint32_t)resp[12] << 24));
        }
        else {
            fail = 1;
        }
        if ((resp[4] & 0xC0) != 0xC0) {
            std::printf("PUP_STATE %d, expected 3 (application running)\n", resp[4] >> 6);
            fail = 1;
        }
        std::printf(fail ? "FAIL: board check\n" : "PASS: board check\n");
        si468x_shutdown();
        return fail;
    }

    for (size_t k = 0; k < cmds.size(); k++) {
        int n = (k + 1 == cmds.size()) ? repeat + 1 : 1;
        for (int r = 0; r < n; r++) {
            if (delay_ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            run(cmds[k], resp);
        }
    }
    si468x_shutdown();
    return 0;
}
