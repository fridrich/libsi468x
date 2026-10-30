/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SI468X_PI_PLUGIN_H
#define SI468X_PI_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Every plugin */
extern const char si468x_pi_plugin_name[];
extern const char si468x_pi_plugin_description[];

/* Input plugins */
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

/* Display plugins */
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

#endif /* SI468X_PI_PLUGIN_H */
