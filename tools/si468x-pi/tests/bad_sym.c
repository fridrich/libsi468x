/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "si468x-pi/input_plugin.h"

/* Missing si468x_pi_plugin_name and si468x_pi_plugin_description */

const int si468x_pi_input_abi = SI468X_PI_INPUT_ABI;

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config)
{
    (void)host;
    (void)config;
    return 0;
}
