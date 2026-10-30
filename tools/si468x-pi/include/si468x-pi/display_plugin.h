/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SI468X_PI_DISPLAY_PLUGIN_H
#define SI468X_PI_DISPLAY_PLUGIN_H

#include "plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI468X_PI_DISPLAY_ABI 1
#define SI468X_PI_DISPLAY_HIGHLIGHT 1

extern const int si468x_pi_display_abi;

void* si468x_pi_display_open(const char* config, int* cols, int* rows, unsigned* features);
void si468x_pi_display_present(void* handle, const char* const* lines,
                               int cols, int rows, int highlight);
void si468x_pi_display_close(void* handle);

#ifdef __cplusplus
}
#endif

#endif /* SI468X_PI_DISPLAY_PLUGIN_H */
