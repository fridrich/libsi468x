/* SPDX-FileCopyrightText: 2026 Fridrich Strba */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SI468X_PI_PLUGIN_UTIL_H
#define SI468X_PI_PLUGIN_UTIL_H

#include <stdio.h>
#include <string.h>

/* "key":"value" of the compact JSON config the host hands over; 0 if absent */
static inline int cfg_str(const char* cfg, const char* key, char* buf, size_t size)
{
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char* v = cfg ? strstr(cfg, pattern) : NULL;
    const char* end = v ? strchr(v += strlen(pattern), '"') : NULL;
    if (!end) {
        return 0;
    }
    snprintf(buf, size, "%.*s", (int)(end - v), v);
    return 1;
}

/* Next code point of UTF-8 *s, advancing *s; malformed input yields U+FFFD */
static inline unsigned utf8_next(const char** s)
{
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = *p++;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    unsigned cp = c < 0x80 ? c : c >= 0xc0 ? c & (0x3fu >> more) : 0xfffd;
    for (; more && (*p & 0xc0) == 0x80; --more) cp = cp << 6 | (*p++ & 0x3f);
    *s = (const char*)p;
    return more ? 0xfffd : cp;
}

#endif /* SI468X_PI_PLUGIN_UTIL_H */
