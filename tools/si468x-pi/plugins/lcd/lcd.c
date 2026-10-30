/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "si468x-pi/display_plugin.h"

#include "hd44780_charset.h"

#include <errno.h>
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

#define OF_NODE "/sys/devices/platform/auxdisplay/of_node/"

struct lcd_ctx {
    int fd;
    FILE* out_file;
    int cols;
    int rows;
    char** shown;   /* per row; only changed rows are written */
};

/* device tree property: one big-endian u32, 0 if missing */
static unsigned read_of_u32(const char* path)
{
    unsigned char b[4];
    FILE* f = fopen(path, "rb");
    size_t n = f ? fread(b, 1, 4, f) : 0;
    if (f) fclose(f);
    return n == 4 ? (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3] : 0;
}

static int by_cp(const void* key, const void* entry)
{
    unsigned cp = *(const unsigned*)key, other = ((const struct hd44780_char*)entry)->cp;
    return cp < other ? -1 : cp > other;
}

/* UTF-8 line to ROM codes; '?' for characters the ROM lacks */
static size_t to_hd44780(const char* s, char* out, size_t size)
{
    size_t n = 0;
    while (*s && n + 2 < size) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x80) {
            out[n++] = c == '\\' ? '|' : c == '~' ? '-' : (char)c;   /* not in ROM A00 */
            continue;
        }
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        unsigned cp = c & (0x3f >> more);
        for (; more && (*s & 0xc0) == 0x80; --more) cp = cp << 6 | (*s++ & 0x3f);
        const struct hd44780_char* m = more ? NULL :
            (const struct hd44780_char*)bsearch(&cp, hd44780_charset, sizeof hd44780_charset / sizeof *hd44780_charset,
                                                sizeof *hd44780_charset, by_cp);
        /* U+2588, the block of the volume bar, is in no ROM: it is CGRAM
         * character 1, which define_block() puts there */
        for (const char* r = m ? m->rom : cp == 0x2588 ? "\x01" : "?"; *r; ++r) out[n++] = *r;
    }
    return n;
}

static void put(struct lcd_ctx* lcd, const char* s, size_t len)
{
    if (lcd->fd >= 0) {
        if (write(lcd->fd, s, len) < 0) {
            /* ignore or log */
        }
    }
}

/* The ROM has no block for the volume bar (U+2588 in ui_manager.cpp), but
 * the HD44780 has eight characters of its own: charlcd defines one with
 * "ESC [ L G <0-7> <8 rows of 5 dots, 16 hex digits>;", here character 1 as
 * a filled cell with a gap under it. A driver without CGRAM drops the
 * sequence. */
static void define_block(struct lcd_ctx* lcd)
{
    static const char escape[] = "\x1b[LG1" "1f1f1f1f1f1f1f00" ";";
    put(lcd, escape, sizeof escape - 1);
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

    char file_path[1024] = {};
    char dev_path[1024] = "/dev/lcd";
    bool has_cols = false;
    bool has_rows = false;

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
        const char* c = strstr(config, "\"cols\":");
        if (c) {
            ctx->cols = atoi(c + 7);
            has_cols = true;
        }
        const char* r = strstr(config, "\"rows\":");
        if (r) {
            ctx->rows = atoi(r + 7);
            has_rows = true;
        }
    }

    if (file_path[0]) {
        ctx->out_file = fopen(file_path, "w");
        if (!ctx->out_file) {
            fprintf(stderr, "si468x-pi-lcd: cannot open '%s': %s\n", file_path, strerror(errno));
            free(ctx);
            return NULL;
        }
    } else {
        if (!has_cols || !has_rows) {
            const int sys_w = (int)read_of_u32(OF_NODE "display-width-chars");
            const int sys_h = (int)read_of_u32(OF_NODE "display-height-chars");
            if (sys_w > 0 && sys_h > 0) {
                if (!has_cols) ctx->cols = sys_w;
                if (!has_rows) ctx->rows = sys_h;
            }
        }
        ctx->fd = open(dev_path, O_WRONLY);
        if (ctx->fd < 0) {
            fprintf(stderr, "si468x-pi-lcd: cannot open '%s': %s\n", dev_path, strerror(errno));
            free(ctx);
            return NULL;
        }
        put(ctx, "\x1b[2J\x1b[L+\x1b[Lc", 12);   /* clear, backlight on, cursor off */
        define_block(ctx);
    }

    ctx->shown = (char**)calloc((size_t)ctx->rows, sizeof(char*));

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
            char row_buf[256];
            size_t n = to_hd44780(lines[r], row_buf, sizeof(row_buf));
            while (n < (size_t)cols && n < sizeof(row_buf)) {
                row_buf[n++] = ' ';
            }
            fwrite(row_buf, 1, cols, ctx->out_file);
            fputc('\n', ctx->out_file);
        }
        fflush(ctx->out_file);
        return;
    }

    if (ctx->fd >= 0) {
        char buf[256];
        for (int row = 0; row < rows && row < ctx->rows; ++row) {
            if (ctx->shown && ctx->shown[row] && !strcmp(ctx->shown[row], lines[row])) {
                continue;
            }
            size_t n = (size_t)snprintf(buf, sizeof buf, "\x1b[Lx0y%d;", row);
            n += to_hd44780(lines[row], buf + n, sizeof buf - n);
            put(ctx, buf, n);
            if (ctx->shown) {
                free(ctx->shown[row]);
                ctx->shown[row] = strdup(lines[row]);
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
        put(ctx, "\x1b[2J\x1b[L-", 8);   /* clear, backlight off */
        close(ctx->fd);
    }
    if (ctx->shown) {
        for (int row = 0; row < ctx->rows; ++row) {
            free(ctx->shown[row]);
        }
        free(ctx->shown);
    }
    free(ctx);
}
