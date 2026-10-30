/*
 * SPDX-FileCopyrightText: 2026 Fridrich Strba
 * SPDX-License-Identifier: MPL-2.0
 *    mock_chip.h - Fake Si468x behind the test transport seam
 *
 *    Models the real reply buffer: re-readable, every RD_REPLY restarts at resp[0].
 *    A command write (rx == nullptr) loads the next queued reply if its opcode matches,
 *    otherwise a bare CTS status. busy_polls RD_REPLYs return all-zero (CTS low) first.
 */

#ifndef MOCK_CHIP_H
#define MOCK_CHIP_H

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>
#include "si468x_internal.h"

struct MockChip {
    struct Reply {
        int opcode;
        std::vector<uint8_t> bytes;
        int busy_polls;
    };
    std::deque<Reply> queue;
    std::vector<uint8_t> current;
    int busy = 0;
    std::vector<std::vector<uint8_t>> writes; // command frames
    std::vector<size_t> reads;                // RD_REPLY lengths, CTS polls included
    int bad_reads = 0;                        // reads whose TX did not start with RD_REPLY (0x00)

    static std::vector<uint8_t> cts() { return { 0x00, 0x80, 0x80, 0x00, 0xC0 }; }

    void expect(int opcode, const std::vector<uint8_t>& bytes, int busy_polls = 0)
    {
        queue.push_back({ opcode, bytes, busy_polls });
    }

    // Writes with this opcode, in order
    std::vector<std::vector<uint8_t>> sent(int opcode) const
    {
        std::vector<std::vector<uint8_t>> out;
        for (auto& w : writes) {
            if (!w.empty() && w[0] == opcode) {
                out.push_back(w);
            }
        }
        return out;
    }

    static int xfer(const uint8_t* tx, uint8_t* rx, size_t len, void* ctx)
    {
        MockChip* m = static_cast<MockChip*>(ctx);
        if (!rx) {
            m->writes.emplace_back(tx, tx + len);
            if (!m->queue.empty() && m->queue.front().opcode == tx[0]) {
                m->current = m->queue.front().bytes;
                m->busy = m->queue.front().busy_polls;
                m->queue.pop_front();
            }
            else {
                m->current = cts();
                m->busy = 0;
            }
            return 0;
        }
        m->reads.push_back(len);
        if (!tx || tx[0] != 0x00) {
            m->bad_reads++;
        }
        std::memset(rx, 0, len);
        if (m->busy > 0) {
            m->busy--;
            return 0;
        }
        std::memcpy(rx, m->current.data(), std::min(len, m->current.size()));
        return 0;
    }

    MockChip() : current(cts()) { si468x_set_test_transport(xfer, this); }
    ~MockChip() { si468x_set_test_transport(nullptr, nullptr); }
};

#endif
