// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_sources.hpp"

#include <cerrno>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace si468x_pi {

namespace {

// Reads one byte with a bounded wait. Returns 1 on success, 0 on timeout,
// -1 on EOF/error.
int readByte(char& c, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, timeout_ms);
    if (r <= 0) {
        return r;
    }
    return ::read(STDIN_FILENO, &c, 1) == 1 ? 1 : -1;
}

} // namespace

void runStdinReader(InputQueue& queue, const StopPredicate& stop_requested)
{
    struct termios oldt;
    bool restore = false;
    if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0) {
            restore = true;
        }
    }

    while (!stop_requested()) {
        char c = 0;
        const int r = readByte(c, 500);
        if (r < 0) {
            break;
        }
        if (r == 0) {
            continue;
        }

        if (c == '\033') {
            char seq[2] = { 0, 0 };
            if (readByte(seq[0], 50) > 0 && seq[0] == '[' && readByte(seq[1], 50) > 0) {
                switch (seq[1]) {
                case 'A':
                    queue.push(InputAction::UP);
                    continue;
                case 'B':
                    queue.push(InputAction::DOWN);
                    continue;
                case 'C':
                    queue.push(InputAction::RIGHT);
                    continue;
                case 'D':
                    queue.push(InputAction::LEFT);
                    continue;
                default:
                    break;
                }
            }
            // just ESC: cancel/back
            queue.push(InputAction::MENU);
        } else if (c == 'q' || c == 'x' || c == '.') {
            queue.push(InputAction::QUIT);
            break;
        } else if (c == 'u') {
            queue.push(InputAction::UP);
        } else if (c == 'd') {
            queue.push(InputAction::DOWN);
        } else if (c == 'l') {
            queue.push(InputAction::LEFT);
        } else if (c == 'r') {
            queue.push(InputAction::RIGHT);
        } else if (c == 'e' || c == '\n' || c == '\r') {
            queue.push(InputAction::ENTER);
        } else if (c == 'm') {
            queue.push(InputAction::MENU);
        } else if (c == '+' || c == '=') {
            queue.push(InputAction::VOLUME_UP);
        } else if (c == '-') {
            queue.push(InputAction::VOLUME_DOWN);
        } else if (c == '0') {
            queue.push(InputAction::MUTE);
        } else if (c == 'h') {
            queue.push(InputAction::HOME);
        }
    }

    if (restore) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

} // namespace si468x_pi
