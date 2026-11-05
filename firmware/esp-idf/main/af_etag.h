#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline int af_etag_make(const uint8_t *sha, size_t sha_len, char *out, size_t cap) {
    if (sha == NULL || out == NULL || sha_len < 8 || cap < 19) {
        if (out != NULL && cap > 0) out[0] = 0;
        return -1;
    }
    static const char hex[] = "0123456789abcdef";
    out[0] = '"';
    for (size_t i = 0; i < 8; i++) {
        out[1 + i * 2] = hex[sha[i] >> 4];
        out[2 + i * 2] = hex[sha[i] & 0x0F];
    }
    out[17] = '"';
    out[18] = 0;
    return 18;
}

static inline int af_etag_match(const char *inm, const char *etag) {
    if (inm == NULL || etag == NULL || etag[0] == 0) return 0;
    size_t etag_len = strlen(etag);
    const char *p = inm;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) break;
        const char *start = p;
        while (*p && *p != ',') p++;
        size_t len = (size_t)(p - start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '	')) len--;   /* пробелы перед запятой */
        if (len > 0) {
            if (len == 1 && start[0] == '*') return 1;
            const char *s = start;
            if (len >= 2 && s[0] == 'W' && s[1] == '/') {
                s += 2;
                len -= 2;
            }
            if (len == etag_len && memcmp(s, etag, etag_len) == 0) return 1;
        }
        if (*p == ',') p++;
    }
    return 0;
}
