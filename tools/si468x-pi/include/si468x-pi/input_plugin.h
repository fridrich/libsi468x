/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SI468X_PI_INPUT_PLUGIN_H
#define SI468X_PI_INPUT_PLUGIN_H

#include "plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI468X_PI_INPUT_ABI 1

enum si468x_pi_action {
    SI468X_PI_UP,
    SI468X_PI_DOWN,
    SI468X_PI_LEFT,
    SI468X_PI_RIGHT,
    SI468X_PI_ENTER,
    SI468X_PI_MENU,
    SI468X_PI_QUIT,
    SI468X_PI_VOLUME_UP,
    SI468X_PI_VOLUME_DOWN,
    SI468X_PI_MUTE,
    SI468X_PI_HOME,
    SI468X_PI_ACTION_COUNT   /* not an action */
};

struct si468x_pi_input_host {
    void* ctx;
    void (*push)(void* ctx, enum si468x_pi_action action);
    int (*stop_requested)(void* ctx);
};

extern const int si468x_pi_input_abi;
int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config);

#ifdef __cplusplus
}
#endif

#endif /* SI468X_PI_INPUT_PLUGIN_H */
