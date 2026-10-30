/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "font8x8_basic.h"
#include "font8x8_ext_latin.h"
#include "si468x-pi/display_plugin.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

const char si468x_pi_plugin_name[] = "fb";
const char si468x_pi_plugin_description[] = "Linux framebuffer display (/dev/fb*)";
const int si468x_pi_display_abi = SI468X_PI_DISPLAY_ABI;

struct fb_ctx {
    int fd;
    uint8_t* fb_mem;
    size_t fb_size;
    int width;
    int height;
    int bpp;
    int stride;
    int cols;
    int rows;
    FILE* out_file;
};

static const uint8_t* get_glyph(uint32_t cp)
{
    static const uint8_t full_block[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (cp == 0x2588) { // U+2588 FULL BLOCK
        return full_block;
    }
    if (cp < 128) {
        return font8x8_basic[cp];
    }
    if (cp >= 0xA0 && cp <= 0xFF) {
        return font8x8_ext_latin[cp - 0xA0];
    }
    return font8x8_basic['?'];
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

void* si468x_pi_display_open(const char* config, int* cols, int* rows, unsigned* features)
{
    struct fb_ctx* ctx = (struct fb_ctx*)calloc(1, sizeof(struct fb_ctx));
    if (!ctx) {
        return NULL;
    }
    ctx->fd = -1;

    char file_path[1024] = {};
    char dev_path[1024] = "/dev/fb0";

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
        ctx->out_file = fopen(file_path, "wb");
        if (!ctx->out_file) {
            free(ctx);
            return NULL;
        }
        ctx->cols = 16;
        ctx->rows = 2;
        ctx->width = 128;
        ctx->height = 16;
        ctx->bpp = 16;
        ctx->stride = 128 * 2;
        ctx->fb_size = ctx->stride * ctx->height;
        ctx->fb_mem = (uint8_t*)calloc(1, ctx->fb_size);
    } else {
        ctx->fd = open(dev_path, O_RDWR);
        if (ctx->fd < 0) {
            free(ctx);
            return NULL;
        }

        struct fb_var_screeninfo vinfo = {};
        struct fb_fix_screeninfo finfo = {};
        if (ioctl(ctx->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
            ioctl(ctx->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
            close(ctx->fd);
            free(ctx);
            return NULL;
        }

        ctx->width = vinfo.xres;
        ctx->height = vinfo.yres;
        ctx->bpp = vinfo.bits_per_pixel;
        ctx->stride = finfo.line_length;
        ctx->fb_size = finfo.smem_len;

        if (ctx->width < 8 || ctx->height < 8) {
            close(ctx->fd);
            free(ctx);
            return NULL;
        }

        ctx->fb_mem = (uint8_t*)mmap(NULL, ctx->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, 0);
        if (ctx->fb_mem == MAP_FAILED) {
            close(ctx->fd);
            free(ctx);
            return NULL;
        }

        ctx->cols = ctx->width / 8;
        ctx->rows = ctx->height / 8;
    }

    *cols = ctx->cols;
    *rows = ctx->rows;
    *features = SI468X_PI_DISPLAY_HIGHLIGHT;
    return ctx;
}

void si468x_pi_display_present(void* handle, const char* const* lines, int cols, int rows, int highlight)
{
    struct fb_ctx* ctx = (struct fb_ctx*)handle;
    if (!ctx || !ctx->fb_mem) {
        return;
    }

    const uint32_t fg_color = 0xFFFFFFFF;
    const uint32_t bg_color = 0x00000000;

    for (int r = 0; r < rows && r < ctx->rows; ++r) {
        const bool is_hl = (r == highlight);
        const uint32_t char_fg = is_hl ? bg_color : fg_color;
        const uint32_t char_bg = is_hl ? fg_color : bg_color;

        const char* p = lines[r];
        for (int c = 0; c < cols && c < ctx->cols; ++c) {
            uint32_t cp = next_utf8(&p);
            if (!cp) {
                cp = ' ';
            }
            const uint8_t* glyph = get_glyph(cp);

            for (int y = 0; y < 8; ++y) {
                const uint8_t row_bits = glyph[y];
                const int py = r * 8 + y;
                uint8_t* row_ptr = ctx->fb_mem + py * ctx->stride;

                for (int x = 0; x < 8; ++x) {
                    const int px = c * 8 + x;
                    const bool lit = (row_bits & (1 << x)) != 0;
                    const uint32_t color = lit ? char_fg : char_bg;

                    if (ctx->bpp == 16) {
                        uint16_t* p16 = (uint16_t*)row_ptr + px;
                        *p16 = (color != 0) ? 0xFFFF : 0x0000;
                    } else {
                        uint32_t* p32 = (uint32_t*)row_ptr + px;
                        *p32 = color;
                    }
                }
            }
        }
    }

    if (ctx->out_file) {
        fseek(ctx->out_file, 0, SEEK_SET);
        fwrite(ctx->fb_mem, 1, ctx->fb_size, ctx->out_file);
        fflush(ctx->out_file);
    }
}

void si468x_pi_display_close(void* handle)
{
    struct fb_ctx* ctx = (struct fb_ctx*)handle;
    if (!ctx) {
        return;
    }
    if (ctx->out_file) {
        fclose(ctx->out_file);
        free(ctx->fb_mem);
    } else {
        if (ctx->fb_mem && ctx->fb_mem != MAP_FAILED) {
            munmap(ctx->fb_mem, ctx->fb_size);
        }
        if (ctx->fd >= 0) {
            close(ctx->fd);
        }
    }
    free(ctx);
}
