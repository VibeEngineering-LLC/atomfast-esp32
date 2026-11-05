#ifndef AF_BLE_NVS_H
#define AF_BLE_NVS_H

#include "af_ble_stats.h"

#define AF_BSN_LEN 64

static inline uint32_t af_bsn_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int j = 0; j < 8; j++) {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static inline size_t af_bsn_encode(const af_bs_t *s, uint8_t *buf, size_t cap) {
    if (buf == NULL || cap < AF_BSN_LEN) return 0;

    /* Магия */
    buf[0] = 0x41; buf[1] = 0x53; buf[2] = 0x42; buf[3] = 0x31;
    /* Версия */
    buf[4] = 0x01; buf[5] = 0x00;
    /* Длина полезной части */
    buf[6] = 0x38; buf[7] = 0x00;

    /* life.connects */
    buf[8]  = (uint8_t)(s->life.connects & 0xFF);
    buf[9]  = (uint8_t)((s->life.connects >> 8) & 0xFF);
    buf[10] = (uint8_t)((s->life.connects >> 16) & 0xFF);
    buf[11] = (uint8_t)((s->life.connects >> 24) & 0xFF);

    /* life.disconnects */
    buf[12] = (uint8_t)(s->life.disconnects & 0xFF);
    buf[13] = (uint8_t)((s->life.disconnects >> 8) & 0xFF);
    buf[14] = (uint8_t)((s->life.disconnects >> 16) & 0xFF);
    buf[15] = (uint8_t)((s->life.disconnects >> 24) & 0xFF);

    /* life.attempts */
    buf[16] = (uint8_t)(s->life.attempts & 0xFF);
    buf[17] = (uint8_t)((s->life.attempts >> 8) & 0xFF);
    buf[18] = (uint8_t)((s->life.attempts >> 16) & 0xFF);
    buf[19] = (uint8_t)((s->life.attempts >> 24) & 0xFF);

    /* life.fails */
    buf[20] = (uint8_t)(s->life.fails & 0xFF);
    buf[21] = (uint8_t)((s->life.fails >> 8) & 0xFF);
    buf[22] = (uint8_t)((s->life.fails >> 16) & 0xFF);
    buf[23] = (uint8_t)((s->life.fails >> 24) & 0xFF);

    /* life.reason[0..5] */
    for (int i = 0; i < AF_BS_REASONS; i++) {
        uint32_t v = s->life.reason[i];
        size_t off = 24 + (size_t)i * 4;
        buf[off]     = (uint8_t)(v & 0xFF);
        buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
        buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    }

    /* life.conn_ms */
    buf[48] = (uint8_t)(s->life.conn_ms & 0xFF);
    buf[49] = (uint8_t)((s->life.conn_ms >> 8) & 0xFF);
    buf[50] = (uint8_t)((s->life.conn_ms >> 16) & 0xFF);
    buf[51] = (uint8_t)((s->life.conn_ms >> 24) & 0xFF);
    buf[52] = (uint8_t)((s->life.conn_ms >> 32) & 0xFF);
    buf[53] = (uint8_t)((s->life.conn_ms >> 40) & 0xFF);
    buf[54] = (uint8_t)((s->life.conn_ms >> 48) & 0xFF);
    buf[55] = (uint8_t)((s->life.conn_ms >> 56) & 0xFF);

    /* Резерв */
    buf[56] = 0; buf[57] = 0; buf[58] = 0; buf[59] = 0;

    /* CRC32 по [0..59] */
    uint32_t crc = af_bsn_crc32(buf, 60);
    buf[60] = (uint8_t)(crc & 0xFF);
    buf[61] = (uint8_t)((crc >> 8) & 0xFF);
    buf[62] = (uint8_t)((crc >> 16) & 0xFF);
    buf[63] = (uint8_t)((crc >> 24) & 0xFF);

    return AF_BSN_LEN;
}

static inline bool af_bsn_decode(af_bs_t *s, const uint8_t *buf, size_t len) {
    if (buf == NULL || len != AF_BSN_LEN) {
        memset(&s->life, 0, sizeof(s->life));
        return false;
    }

    /* Магия */
    if (buf[0] != 0x41 || buf[1] != 0x53 || buf[2] != 0x42 || buf[3] != 0x31) {
        memset(&s->life, 0, sizeof(s->life));
        return false;
    }

    /* Версия */
    if (buf[4] != 0x01 || buf[5] != 0x00) {
        memset(&s->life, 0, sizeof(s->life));
        return false;
    }

    /* Длина полезной части */
    if (buf[6] != 0x38 || buf[7] != 0x00) {
        memset(&s->life, 0, sizeof(s->life));
        return false;
    }

    /* CRC */
    uint32_t crc_calc = af_bsn_crc32(buf, 60);
    uint32_t crc_stored = (uint32_t)buf[60] | ((uint32_t)buf[61] << 8) |
                          ((uint32_t)buf[62] << 16) | ((uint32_t)buf[63] << 24);
    if (crc_calc != crc_stored) {
        memset(&s->life, 0, sizeof(s->life));
        return false;
    }

    /* Чтение значений */
    s->life.connects = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                       ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    s->life.disconnects = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                          ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
    s->life.attempts = (uint32_t)buf[16] | ((uint32_t)buf[17] << 8) |
                       ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24);
    s->life.fails = (uint32_t)buf[20] | ((uint32_t)buf[21] << 8) |
                    ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);

    for (int i = 0; i < AF_BS_REASONS; i++) {
        size_t off = 24 + (size_t)i * 4;
        s->life.reason[i] = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                            ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
    }

    s->life.conn_ms = (uint64_t)buf[48] | ((uint64_t)buf[49] << 8) |
                      ((uint64_t)buf[50] << 16) | ((uint64_t)buf[51] << 24) |
                      ((uint64_t)buf[52] << 32) | ((uint64_t)buf[53] << 40) |
                      ((uint64_t)buf[54] << 48) | ((uint64_t)buf[55] << 56);

    return true;
}

static inline bool af_bsn_due(const af_bs_t *s, uint64_t now_ms) {
    if (!s->dirty) return false;
    if (now_ms < s->last_save_ms) return false;

    uint64_t elapsed = now_ms - s->last_save_ms;

    if (elapsed >= AF_BS_NVS_PERIOD_MS) return true;
    if (s->disc_pending && elapsed >= AF_BS_NVS_MIN_GAP_MS) return true;

    return false;
}

static inline void af_bsn_saved(af_bs_t *s, uint64_t now_ms) {
    s->dirty = false;
    s->disc_pending = false;
    s->last_save_ms = now_ms;
}

#endif
