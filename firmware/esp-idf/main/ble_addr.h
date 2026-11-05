#ifndef BLE_ADDR_H
#define BLE_ADDR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static inline bool ble_addr_hexval(char c, uint8_t *v)
{
    if (c >= '0' && c <= '9') { *v = (uint8_t)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { *v = (uint8_t)(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { *v = (uint8_t)(c - 'A' + 10); return true; }
    return false;
}

static inline bool ble_addr_parse(const char *s, uint8_t out[6])
{
    if (s == NULL || out == NULL) return false;

    int len = 0;
    for (len = 0; len < 18; len++) {
        if (s[len] == '\0') break;
    }
    if (len >= 18) return false;
    if (len != 17 && len != 12) return false;

    uint8_t tmp[6];

    if (len == 17) {
        char sep = s[2];
        if (sep != ':' && sep != '-') return false;
        if (s[5] != sep) return false;
        if (s[8] != sep) return false;
        if (s[11] != sep) return false;
        if (s[14] != sep) return false;
        for (int i = 0; i < 6; i++) {
            uint8_t hi, lo;
            if (!ble_addr_hexval(s[3 * i], &hi)) return false;
            if (!ble_addr_hexval(s[3 * i + 1], &lo)) return false;
            tmp[i] = (uint8_t)((hi << 4) | lo);
        }
    } else {
        for (int i = 0; i < 6; i++) {
            uint8_t hi, lo;
            if (!ble_addr_hexval(s[2 * i], &hi)) return false;
            if (!ble_addr_hexval(s[2 * i + 1], &lo)) return false;
            tmp[i] = (uint8_t)((hi << 4) | lo);
        }
    }

    for (int i = 0; i < 6; i++) out[i] = tmp[i];
    return true;
}

#endif
