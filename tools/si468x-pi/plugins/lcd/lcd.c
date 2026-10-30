/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "si468x-pi/display_plugin.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char si468x_pi_plugin_name[] = "lcd";
const char si468x_pi_plugin_description[] = "Linux charlcd HD44780 driver (/dev/lcd)";
const int si468x_pi_display_abi = SI468X_PI_DISPLAY_ABI;

struct lcd_ctx {
    int fd;
    FILE* out_file;
    int cols;
    int rows;
};

static uint8_t map_hd44780(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7D) {
        return (uint8_t)cp;
    }
    switch (cp) {
    case 0x00B0: return 0xDF; // degree sign °
    case 0x00B5: return 0xE4; // micro sign µ
    case 0x00B7: return 0xA5; // middle dot ·
    case 0x00DF: return 0xE2; // sharp s ß
    case 0x00E0: // à
    case 0x00E1: // á
    case 0x00E2: // â
    case 0x00E3: return 'a';
    case 0x00E4: return 0xE1; // ä
    case 0x00E7: return 'c';  // ç
    case 0x00E8: // è
    case 0x00E9: // é
    case 0x00EA: // ê
    case 0x00EB: return 'e';
    case 0x00EC: // ì
    case 0x00ED: // í
    case 0x00EE: // î
    case 0x00EF: return 'i';
    case 0x00F1: return 0xEE; // ñ
    case 0x00F2: // ò
    case 0x00F3: // ó
    case 0x00F4: // ô
    case 0x00F5: return 'o';
    case 0x00F6: return 0xEF; // ö
    case 0x00F9: // ù
    case 0x00FA: // ú
    case 0x00FB: return 'u';
    case 0x00FC: return 0xF5; // ü
    default:     return '?';
    }
}

static uint32_t next_utf8(const char** p)
{
    const uint8_t* s = (const uint8_t*)*p;
    if (!*s) {
        return 0;
    }
    uint32_t cp = 0;
    if (*s < 0x80) {
        cp = *s++;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = (*s++ & 0x1F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
    } else if ((*s & 0xF0) == 0xE0) {
        cp = (*s++ & 0x0F) << 12;
        if (*s) cp |= (*s++ & 0x3F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
    } else if ((*s & 0xF8) == 0xF0) {
        cp = (*s++ & 0x07) << 18;
        if (*s) cp |= (*s++ & 0x3F) << 12;
        if (*s) cp |= (*s++ & 0x3F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
    } else {
        cp = '?';
        s++;
    }
    *p = (const char*)s;
    return cp;
}

static int read_sysfs_u32_be(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    uint8_t buf[4] = {};
    size_t n = fread(buf, 1, 4, f);
    fclose(f);
    if (n != 4) {
        return -1;
    }
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

void* si468x_pi_display_open(const char* config, int* cols, int* rows, unsigned* features)
{
    struct lcd_ctx* ctx = (struct lcd_ctx*)calloc(1, sizeof(struct lcd_ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->fd = -1;
    ctx->cols = 16;
    ctx->rows = 2;

    const int sys_w = read_sysfs_u32_be("/sys/devices/platform/auxdisplay/of_node/display-width-chars");
    const int sys_h = read_sysfs_u32_be("/sys/devices/platform/auxdisplay/of_node/display-height-chars");
    if (sys_w > 0 && sys_h > 0) {
        ctx->cols = sys_w;
        ctx->rows = sys_h;
    }

    char file_path[1024] = {};
    char dev_path[1024] = "/dev/lcd";

    if (config) {
        const char* f = strstr(config, "\"file\":\"");
        if (f) {
            f += 8;
            const char* end = strchr(f, '"');
            if (end) {
                snprintf(file_path, sizeof(file_path), "%.*s", (int)(end - f), f);
            }
        }
        const char* d = strstr(config, "\"device\":\"");
        if (d) {
            d += 10;
            const char* end = strchr(d, '"');
            if (end) {
                snprintf(dev_path, sizeof(dev_path), "%.*s", (int)(end - d), d);
            }
        }
    }

    if (file_path[0]) {
        ctx->out_file = fopen(file_path, "w");
        if (!ctx->out_file) {
            free(ctx);
            return NULL;
        }
    } else {
        ctx->fd = open(dev_path, O_WRONLY);
        if (ctx->fd < 0) {
            free(ctx);
            return NULL;
        }
    }

    *cols = ctx->cols;
    *rows = ctx->rows;
    *features = 0; // plain text only
    return ctx;
}

void si468x_pi_display_present(void* handle, const char* const* lines, int cols, int rows, int highlight)
{
    (void)highlight;
    struct lcd_ctx* ctx = (struct lcd_ctx*)handle;
    if (!ctx) {
        return;
    }

    if (ctx->out_file) {
        fseek(ctx->out_file, 0, SEEK_SET);
        fprintf(ctx->out_file, "frame %d x %d\n", cols, rows);
        for (int r = 0; r < rows; ++r) {
            const char* p = lines[r];
            for (int c = 0; c < cols; ++c) {
                uint32_t cp = next_utf8(&p);
                uint8_t b = cp ? map_hd44780(cp) : ' ';
                fputc(b, ctx->out_file);
            }
            fputc('\n', ctx->out_file);
        }
        fflush(ctx->out_file);
    } else if (ctx->fd >= 0) {
        // charlcd driver escape: \x1b[H = home
        const char home[] = "\x1b[H";
        write(ctx->fd, home, sizeof(home) - 1);

        for (int r = 0; r < rows && r < ctx->rows; ++r) {
            uint8_t row_buf[128] = {};
            const char* p = lines[r];
            for (int c = 0; c < cols && c < ctx->cols; ++c) {
                uint32_t cp = next_utf8(&p);
                row_buf[c] = cp ? map_hd44780(cp) : ' ';
            }
            write(ctx->fd, row_buf, ctx->cols);
            if (r + 1 < rows) {
                write(ctx->fd, "\n", 1);
            }
        }
    }
}

void si468x_pi_display_close(void* handle)
{
    struct lcd_ctx* ctx = (struct lcd_ctx*)handle;
    if (!ctx) {
        return;
    }
    if (ctx->out_file) {
        fclose(ctx->out_file);
    }
    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    free(ctx);
}
