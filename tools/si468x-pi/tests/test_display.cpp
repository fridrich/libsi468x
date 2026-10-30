// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.h"
#include "display_interface.hpp"
#include "tty_display.hpp"
#include "ui_manager.hpp"
#include "utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

class FakeDisplay : public si468x_pi::IDisplay {
public:
    FakeDisplay(int cols, int rows, bool highlight)
        : m_cols(cols), m_rows(rows), m_highlight(highlight)
    {
    }

    int cols() const override { return m_cols; }
    int rows() const override { return m_rows; }
    bool canHighlight() const override { return m_highlight; }
    void present(const si468x_pi::Frame& frame) override
    {
        last = frame;
        ++presents;
    }

    si468x_pi::Frame last { 0, 0, 0 };
    int presents = 0;

private:
    int m_cols;
    int m_rows;
    bool m_highlight;
};

std::string read_all(std::FILE* f)
{
    std::fflush(f);
    std::rewind(f);
    std::string out;
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    return out;
}

void test_tty_plain()
{
    using namespace si468x_pi;

    std::FILE* f = std::tmpfile();
    check(f != nullptr, "tmpfile");
    if (!f) {
        return;
    }

    TtyDisplay display(10, 3, f);
    check(display.cols() == 10 && display.rows() == 3, "tty geometry");
    check(!display.canHighlight(), "redirected output has no highlight");

    Frame frame(10, 3, 0);
    frame.set(0, "abc       ");
    frame.set(1, "def       ");
    frame.set(2, "          ");
    display.present(frame);

    const std::string first = read_all(f);
    const std::string expected =
        "+----------+\n"
        "|abc       |\n"
        "|def       |\n"
        "|          |\n"
        "+----------+\n";
    check(first == expected, "plain frame is a box of exact width");

    frame.set(0, "xyz       ");
    display.present(frame);
    check(read_all(f).find("|xyz       |") != std::string::npos, "changed frame is redrawn");

    std::fclose(f);
}

void test_tty_ansi()
{
    using namespace si468x_pi;

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    check(master >= 0, "open pty master");
    if (master < 0) {
        return;
    }
    check(grantpt(master) == 0, "grantpt");
    check(unlockpt(master) == 0, "unlockpt");
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    const char* slave_name = ptsname(master);
    check(slave_name != nullptr, "ptsname");
    int slave = slave_name ? open(slave_name, O_RDWR | O_NOCTTY) : -1;
    check(slave >= 0, "open pty slave");
    if (slave < 0) {
        close(master);
        return;
    }

    std::FILE* out = fdopen(slave, "w");
    check(out != nullptr, "fdopen pty");
    if (out) {
        TtyDisplay display(10, 2, out);
        check(display.canHighlight(), "terminal output can highlight");

        Frame frame(10, 2, 0);
        frame.set(0, "first     ");
        frame.set(1, "second    ");
        frame.highlight = 1;
        display.present(frame);
        std::fflush(out);

        std::string got;
        char buf[512];
        for (;;) {
            const ssize_t n = read(master, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            got.append(buf, static_cast<size_t>(n));
        }
        check(got.find("\033[H\033[2J") != std::string::npos, "terminal clears the screen");
        check(got.find("\033[7m") != std::string::npos, "focused row is inverted");
        check(got.find("\033[27m") != std::string::npos, "inverse video ends");
        std::fclose(out);
    }
    close(master);
}

void test_multi_display()
{
    using namespace si468x_pi;

    std::vector<std::unique_ptr<IDisplay>> displays;
    auto* first = new FakeDisplay(20, 4, true);
    auto* second = new FakeDisplay(10, 2, true);
    displays.emplace_back(first);
    displays.emplace_back(second);
    MultiDisplay multi(std::move(displays));

    check(multi.cols() == 10, "multi display takes the smallest width");
    check(multi.rows() == 2, "multi display takes the smallest height");
    check(multi.canHighlight(), "multi display highlights when all can");

    Frame frame(10, 2, 0);
    frame.set(0, "hello     ");
    frame.set(1, "world     ");
    multi.present(frame);
    check(first->presents == 1 && second->presents == 1, "multi display mirrors the frame");
    check(second->last.lines[0] == "hello     ", "mirrored frame content");

    std::vector<std::unique_ptr<IDisplay>> mixed;
    mixed.emplace_back(new FakeDisplay(10, 2, true));
    mixed.emplace_back(new FakeDisplay(10, 2, false));
    MultiDisplay mixed_display(std::move(mixed));
    check(!mixed_display.canHighlight(), "multi display keeps markers when one cannot highlight");

}

void test_volume_line()
{
    using namespace si468x_pi;

    check(volumeLine(0, 50, false).empty(), "zero-width volume line");
    check(volumeLine(4, 0, false) == "    ", "zero volume is empty");
    check(volumeLine(4, 50, false) == "\xe2\x96\x88\xe2\x96\x88  ", "half volume fills two cells");
    check(volumeLine(4, 100, false) == "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88",
          "full volume fills the line");
    check(volumeLine(4, 1, false) == "\xe2\x96\x88   ", "small volume keeps one cell");
    check(volumeLine(6, 50, true) == "Muted ", "muted line");
}

} // namespace

int main()
{
    std::cout << "si468x-pi display tests" << std::endl;

    test_tty_plain();
    test_tty_ansi();
    test_multi_display();
    test_volume_line();

    return report("display");
}
