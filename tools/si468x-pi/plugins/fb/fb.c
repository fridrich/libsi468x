/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "font8x8_basic.h"
#include "font8x8_ext_latin.h"
#include "../plugin_util.h"
#include "si468x-pi/plugin.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

const char si468x_pi_plugin_name[] = "fb";
const char si468x_pi_plugin_description[] = "Linux framebuffer display (/dev/fb*, 16 or 32 bpp)";
const int si468x_pi_display_abi = SI468X_PI_DISPLAY_ABI;

struct fb_ctx {
    int fd;
    uint8_t* fb_mem;
    size_t fb_size;
    unsigned stride;
    unsigned bpp;
    unsigned width;
    unsigned height;
    int cols;
    int rows;
    FILE* out_file;
};

/* U+2588 FULL BLOCK, the filled part of the volume bar (volumeLine() in
 * ui_manager.cpp); the ASCII fonts have no block */
static const uint8_t glyph_full_block[8] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* Glyph of the next code point of *s; '?' outside the font (U+0020-007F,
 * U+00A0-00FF and the block above) */
static const uint8_t* glyph(const char** s)
{
    unsigned cp = utf8_next(s);
    if (cp >= 0x20 && cp < 0x80) return font8x8_basic[cp - 0x20];
    if (cp >= 0xa0 && cp <= 0xff) return font8x8_ext_latin[cp - 0xa0];
    if (cp == 0x2588) return glyph_full_block;
    return font8x8_basic['?' - 0x20];
}

static void put_pixel(struct fb_ctx* fb, unsigned x, unsigned y, int on)
{
    uint8_t* p = fb->fb_mem + y * fb->stride + x * (fb->bpp / 8);
    if (fb->bpp == 32) *(uint32_t*)p = on ? 0xffffffff : 0;
    else *(uint16_t*)p = on ? 0xffff : 0;
}

void* si468x_pi_display_open(const char* config, int* cols, int* rows, unsigned* features)
{
    struct fb_ctx* ctx = (struct fb_ctx*)calloc(1, sizeof(struct fb_ctx));
    if (!ctx) {
        return NULL;
    }

    char file_path[1024] = {};
    char dev_path[1024] = "/dev/fb0";
    cfg_str(config, "file", file_path, sizeof file_path);   /* tests: frames to a file */
    cfg_str(config, "device", dev_path, sizeof dev_path);

    if (file_path[0]) {
        ctx->out_file = fopen(file_path, "wb");
        if (!ctx->out_file) {
            fprintf(stderr, "si468x-pi-fb: cannot open '%s': %s\n", file_path, strerror(errno));
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
        int fd = open(dev_path, O_RDWR);
        struct fb_var_screeninfo var;
        struct fb_fix_screeninfo fix;
        if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
            fprintf(stderr, "si468x-pi-fb: cannot open/ioctl '%s': %s\n", dev_path, strerror(errno));
            if (fd >= 0) close(fd);
            free(ctx);
            return NULL;
        }
        if (var.bits_per_pixel != 16 && var.bits_per_pixel != 32) {
            fprintf(stderr, "si468x-pi-fb: %s has %u bpp, only 16 and 32 supported\n", dev_path, var.bits_per_pixel);
            close(fd);
            free(ctx);
            return NULL;
        }

        /* Without fbcon nobody sets the mode: DRM fbdev emulation (ssd130x)
         * keeps the panel off until fb_set_par */
        var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
        (void)ioctl(fd, FBIOPUT_VSCREENINFO, &var);
        (void)ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);

        void* mem = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) {
            fprintf(stderr, "si468x-pi-fb: mmap on '%s' failed: %s\n", dev_path, strerror(errno));
            close(fd);
            free(ctx);
            return NULL;
        }

        ctx->fd = fd;
        ctx->fb_mem = (uint8_t*)mem;
        ctx->fb_size = fix.smem_len;
        ctx->stride = fix.line_length;
        ctx->bpp = var.bits_per_pixel;
        ctx->width = var.xres;
        ctx->height = var.yres;
        memset(ctx->fb_mem, 0, ctx->fb_size);
        ctx->cols = (int)(ctx->width / 8);
        ctx->rows = (int)(ctx->height / 8);
    }

    *cols = ctx->cols;
    *rows = ctx->rows;
    *features = SI468X_PI_DISPLAY_HIGHLIGHT;
    return ctx;
}

void si468x_pi_display_present(void* handle, const char* const* lines, int cols, int rows, int highlight)
{
    struct fb_ctx* fb = (struct fb_ctx*)handle;
    if (!fb || !fb->fb_mem) {
        return;
    }

    memset(fb->fb_mem, 0, fb->fb_size);
    for (int row = 0; row < rows && row < fb->rows; row++) {
        const int inverse = (row == highlight);
        if (inverse) {
            /* reverse video: the focused row in the foreground colour, its
             * glyphs in the background colour */
            for (unsigned y = 0; y < 8; y++) {
                for (int x = 0; x < cols * 8 && (unsigned)x < fb->width; x++) {
                    put_pixel(fb, x, row * 8 + y, 1);
                }
            }
        }
        const char* s = lines[row];
        for (int col = 0; col < cols && col < fb->cols && *s; col++) {
            const uint8_t* g = glyph(&s);
            for (unsigned y = 0; y < 8; y++) {
                for (unsigned x = 0; x < 8; x++) {
                    put_pixel(fb, col * 8 + x, row * 8 + y, (((g[y] >> x) & 1) ^ inverse));
                }
            }
        }
    }

    if (fb->fd >= 0 && fb->fb_mem) {
        msync(fb->fb_mem, fb->fb_size, MS_ASYNC);
        (void)pwrite(fb->fd, fb->fb_mem, fb->fb_size, 0);
    }

    if (fb->out_file) {
        fseek(fb->out_file, 0, SEEK_SET);
        fwrite(fb->fb_mem, 1, fb->fb_size, fb->out_file);
        fflush(fb->out_file);
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
        if (ctx->fd >= 0) {
            if (ctx->fb_mem && ctx->fb_mem != MAP_FAILED) {
                memset(ctx->fb_mem, 0, ctx->fb_size);
                (void)pwrite(ctx->fd, ctx->fb_mem, ctx->fb_size, 0);
                munmap(ctx->fb_mem, ctx->fb_size);
            }
            close(ctx->fd);
        }
    }
    free(ctx);
}
