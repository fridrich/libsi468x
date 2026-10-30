/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "si468x-pi/plugin.h"

const char si468x_pi_plugin_name[] = "badabi";
const char si468x_pi_plugin_description[] = "plugin with mismatched ABI";

/* wrong ABI version */
const int si468x_pi_input_abi = 999;

int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config)
{
    (void)host;
    (void)config;
    return 0;
}
