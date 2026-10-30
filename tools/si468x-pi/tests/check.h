// SPDX-FileCopyrightText: 2026 Fridrich Strba
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const std::string& message)
{
    if (!cond) {
        std::cerr << "FAIL: " << message << std::endl;
        ++failures;
    }
}

// main()'s exit status
int report(const char* suite)
{
    if (failures != 0) {
        std::cerr << failures << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all " << suite << " tests passed" << std::endl;
    return 0;
}

} // namespace
