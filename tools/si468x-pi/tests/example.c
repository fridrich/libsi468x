/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Example si468x-pi plugin, input and display in one file, used by
 * input_plugin_tests and display_plugin_tests */
#include "si468x-pi/plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char si468x_pi_plugin_name[] = "example";
const char si468x_pi_plugin_description[] = "keys from the config, frames to a file";

const int si468x_pi_input_abi = SI468X_PI_INPUT_ABI;
const int si468x_pi_display_abi = SI468X_PI_DISPLAY_ABI;

/* config e.g. {"plugin":"example","keys":"d+hme"}: d = DOWN, e = ENTER,
 * + = VOLUME_UP, h = HOME, m = MUTE */
int si468x_pi_input_run(const struct si468x_pi_input_host* host, const char* config)
{
    const char* keys = strstr(config, "\"keys\":\"");
    if (keys) {
        for (keys += 8; *keys && *keys != '"'; keys++) {
            if (*keys == 'd') host->push(host->ctx, SI468X_PI_DOWN);
            else if (*keys == 'e') host->push(host->ctx, SI468X_PI_ENTER);
            else if (*keys == '+') host->push(host->ctx, SI468X_PI_VOLUME_UP);
            else if (*keys == 'h') host->push(host->ctx, SI468X_PI_HOME);
            else if (*keys == 'm') host->push(host->ctx, SI468X_PI_MUTE);
        }
    }
    while (!host->stop_requested(host->ctx))
        usleep(10000);
    return 0;
}

/* config e.g. {"plugin":"example","file":"/tmp/frames.txt"}: 16 x 2 */
void* si468x_pi_display_open(const char* config, int* cols, int* rows, unsigned* features)
{
    const char* key = strstr(config, "\"file\":\"");
    if (!key) {
        fprintf(stderr, "display example: \"file\" missing\n");
        return NULL;
    }
    key += 8;
    const char* end = strchr(key, '"');
    if (!end) return NULL;
    char path[4096];
    snprintf(path, sizeof(path), "%.*s", (int)(end - key), key);
    FILE* out = fopen(path, "w");
    if (!out) return NULL;
    *cols = 16;
    *rows = 2;
    *features = 0;      // plain text: the screens keep the > < markers
    return out;
}

void si468x_pi_display_present(void* handle, const char* const* lines, int cols, int rows, int highlight)
{
    FILE* out = handle;
    fprintf(out, "frame %d x %d", cols, rows);
    if (highlight >= 0) fprintf(out, ", highlighted row %d", highlight);
    fprintf(out, "\n");
    for (int i = 0; i < rows; i++) fprintf(out, "%s\n", lines[i]);
    fflush(out);
}

void si468x_pi_display_close(void* handle)
{
    FILE* out = handle;
    fprintf(out, "closed\n");
    fclose(out);
}
