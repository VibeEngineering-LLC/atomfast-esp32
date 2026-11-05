#ifndef AF_REC_H
#define AF_REC_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_parse.h"
#include "af_crc16_tab.h"

#define AF_REC_RAW_SIZE 20
#define AF_REC_AGG_SIZE 28

#define AF_AGG_F_PARTIAL  0x01
#define AF_AGG_F_TIMEJUMP 0x02

#define AF_MODE_LAST_DOSE 0x01
#define AF_MODE_LAST_RATE 0x02
#define AF_MODE_LAST_PULSES 0x04
#define AF_MODE_LAST_BAT 0x08
#define AF_MODE_LAST_TEMP 0x10
#define AF_MODE_MASK 0x1F

typedef struct {
    uint32_t ts;
    af_sample_t s;
} af_raw_rec_t;

typedef struct {
    uint32_t ts;
    uint16_t period_s;
    uint16_t n;
    float dose_msv;
    float doserate_usvh;
    float pulses_2s;
    int8_t battery;
    int8_t temp_c;
    uint8_t flags;
    uint8_t agg_flags;
    uint8_t modes;
} af_agg_rec_t;

/* e16e (perf-audit flash P1-2): эталон - битовый цикл; боевая af_crc16 - табличная, тождественность проверяет test_af_crc16 */
static inline uint16_t af_crc16_bitwise(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

static inline uint16_t af_crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc = (uint16_t)((crc << 8) ^ af_crc16_tab[(uint8_t)((crc >> 8) ^ p[i])]);
    }
    return crc;
}

static inline void af_raw_encode(const af_raw_rec_t *r, uint8_t out[AF_REC_RAW_SIZE]) {
    out[0] = (uint8_t)(r->ts & 0xFF);
    out[1] = (uint8_t)((r->ts >> 8) & 0xFF);
    out[2] = (uint8_t)((r->ts >> 16) & 0xFF);
    out[3] = (uint8_t)((r->ts >> 24) & 0xFF);

    uint32_t bits;
    memcpy(&bits, &r->s.dose_msv, sizeof(bits));
    out[4] = (uint8_t)(bits & 0xFF);
    out[5] = (uint8_t)((bits >> 8) & 0xFF);
    out[6] = (uint8_t)((bits >> 16) & 0xFF);
    out[7] = (uint8_t)((bits >> 24) & 0xFF);

    memcpy(&bits, &r->s.doserate_usvh, sizeof(bits));
    out[8] = (uint8_t)(bits & 0xFF);
    out[9] = (uint8_t)((bits >> 8) & 0xFF);
    out[10] = (uint8_t)((bits >> 16) & 0xFF);
    out[11] = (uint8_t)((bits >> 24) & 0xFF);

    out[12] = (uint8_t)(r->s.pulses_2s & 0xFF);
    out[13] = (uint8_t)((r->s.pulses_2s >> 8) & 0xFF);

    out[14] = (uint8_t)r->s.battery;
    out[15] = (uint8_t)r->s.temp_c;
    out[16] = r->s.flags;
    out[17] = 0;

    uint16_t crc = af_crc16(out, 18);
    out[18] = (uint8_t)(crc & 0xFF);
    out[19] = (uint8_t)((crc >> 8) & 0xFF);
}

static inline int af_raw_decode(const uint8_t in[AF_REC_RAW_SIZE], af_raw_rec_t *r) {
    uint16_t crc_calc = af_crc16(in, 18);
    uint16_t crc_stored = (uint16_t)(in[18] | (in[19] << 8));
    if (crc_calc != crc_stored) return -1;
    if (in[17] != 0) return -2;

    r->ts = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);

    uint32_t bits;
    bits = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    memcpy(&r->s.dose_msv, &bits, sizeof(bits));

    bits = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    memcpy(&r->s.doserate_usvh, &bits, sizeof(bits));

    r->s.pulses_2s = (uint16_t)(in[12] | (in[13] << 8));
    r->s.battery = (int8_t)in[14];
    r->s.temp_c = (int8_t)in[15];
    r->s.flags = in[16];
    r->s.cps = r->s.pulses_2s / 2.0f;

    return 0;
}

static inline void af_agg_encode(const af_agg_rec_t *r, uint8_t out[AF_REC_AGG_SIZE]) {
    out[0] = (uint8_t)(r->ts & 0xFF);
    out[1] = (uint8_t)((r->ts >> 8) & 0xFF);
    out[2] = (uint8_t)((r->ts >> 16) & 0xFF);
    out[3] = (uint8_t)((r->ts >> 24) & 0xFF);

    out[4] = (uint8_t)(r->period_s & 0xFF);
    out[5] = (uint8_t)((r->period_s >> 8) & 0xFF);

    out[6] = (uint8_t)(r->n & 0xFF);
    out[7] = (uint8_t)((r->n >> 8) & 0xFF);

    uint32_t bits;
    memcpy(&bits, &r->dose_msv, sizeof(bits));
    out[8] = (uint8_t)(bits & 0xFF);
    out[9] = (uint8_t)((bits >> 8) & 0xFF);
    out[10] = (uint8_t)((bits >> 16) & 0xFF);
    out[11] = (uint8_t)((bits >> 24) & 0xFF);

    memcpy(&bits, &r->doserate_usvh, sizeof(bits));
    out[12] = (uint8_t)(bits & 0xFF);
    out[13] = (uint8_t)((bits >> 8) & 0xFF);
    out[14] = (uint8_t)((bits >> 16) & 0xFF);
    out[15] = (uint8_t)((bits >> 24) & 0xFF);

    memcpy(&bits, &r->pulses_2s, sizeof(bits));
    out[16] = (uint8_t)(bits & 0xFF);
    out[17] = (uint8_t)((bits >> 8) & 0xFF);
    out[18] = (uint8_t)((bits >> 16) & 0xFF);
    out[19] = (uint8_t)((bits >> 24) & 0xFF);

    out[20] = (uint8_t)r->battery;
    out[21] = (uint8_t)r->temp_c;
    out[22] = r->flags;
    out[23] = r->agg_flags;
    out[24] = r->modes;
    out[25] = 0;

    uint16_t crc = af_crc16(out, 26);
    out[26] = (uint8_t)(crc & 0xFF);
    out[27] = (uint8_t)((crc >> 8) & 0xFF);
}

static inline int af_agg_decode(const uint8_t in[AF_REC_AGG_SIZE], af_agg_rec_t *r) {
    uint16_t crc_calc = af_crc16(in, 26);
    uint16_t crc_stored = (uint16_t)(in[26] | (in[27] << 8));
    if (crc_calc != crc_stored) return -1;
    if (in[25] != 0) return -2;

    r->ts = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    r->period_s = (uint16_t)(in[4] | (in[5] << 8));
    r->n = (uint16_t)(in[6] | (in[7] << 8));

    uint32_t bits;
    bits = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    memcpy(&r->dose_msv, &bits, sizeof(bits));

    bits = (uint32_t)in[12] | ((uint32_t)in[13] << 8) | ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);
    memcpy(&r->doserate_usvh, &bits, sizeof(bits));

    bits = (uint32_t)in[16] | ((uint32_t)in[17] << 8) | ((uint32_t)in[18] << 16) | ((uint32_t)in[19] << 24);
    memcpy(&r->pulses_2s, &bits, sizeof(bits));

    r->battery = (int8_t)in[20];
    r->temp_c = (int8_t)in[21];
    r->flags = in[22];
    r->agg_flags = in[23];
    r->modes = in[24];

    return 0;
}

static inline int af_csv_header(int with_mac, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) return -1;
    const char *hdr = with_mac ? "mac,ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n"
                               : "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n";
    size_t len = strlen(hdr);
    if (len + 1 > cap) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, hdr, len + 1);
    return (int)len;
}

static inline int af_raw_csv(const af_raw_rec_t *r, const char *mac, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) return -1;
    if (mac != NULL) {
        for (const char *p = mac; *p; p++) {
            if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') return -2;
        }
    }
    if (!isfinite(r->s.dose_msv) || !isfinite(r->s.doserate_usvh)) return -3;

    double pulses = (double)r->s.pulses_2s;
    double cps = pulses / 2.0;

    int ret;
    if (mac != NULL) {
        ret = snprintf(buf, cap, "%s,%lu,0,%.6g,%.6g,%.6g,%.6g,%d,%d,%u,1\n",
                       mac, (unsigned long)r->ts, (double)r->s.dose_msv, (double)r->s.doserate_usvh,
                       pulses, cps, (int)r->s.battery, (int)r->s.temp_c, (unsigned)r->s.flags);
    } else {
        ret = snprintf(buf, cap, "%lu,0,%.6g,%.6g,%.6g,%.6g,%d,%d,%u,1\n",
                       (unsigned long)r->ts, (double)r->s.dose_msv, (double)r->s.doserate_usvh,
                       pulses, cps, (int)r->s.battery, (int)r->s.temp_c, (unsigned)r->s.flags);
    }
    if (ret < 0 || (size_t)ret >= cap) {
        buf[0] = 0;
        return -1;
    }
    return ret;
}

static inline int af_agg_csv(const af_agg_rec_t *r, const char *mac, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) return -1;
    if (mac != NULL) {
        for (const char *p = mac; *p; p++) {
            if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') return -2;
        }
    }
    if (!isfinite(r->dose_msv) || !isfinite(r->doserate_usvh) || !isfinite(r->pulses_2s)) return -3;

    double pulses = (double)r->pulses_2s;
    double cps = pulses / 2.0;

    int ret;
    if (mac != NULL) {
        ret = snprintf(buf, cap, "%s,%lu,%u,%.6g,%.6g,%.6g,%.6g,%d,%d,%u,%u\n",
                       mac, (unsigned long)r->ts, (unsigned)r->period_s, (double)r->dose_msv,
                       (double)r->doserate_usvh, pulses, cps, (int)r->battery, (int)r->temp_c,
                       (unsigned)r->flags, (unsigned)r->n);
    } else {
        ret = snprintf(buf, cap, "%lu,%u,%.6g,%.6g,%.6g,%.6g,%d,%d,%u,%u\n",
                       (unsigned long)r->ts, (unsigned)r->period_s, (double)r->dose_msv,
                       (double)r->doserate_usvh, pulses, cps, (int)r->battery, (int)r->temp_c,
                       (unsigned)r->flags, (unsigned)r->n);
    }
    if (ret < 0 || (size_t)ret >= cap) {
        buf[0] = 0;
        return -1;
    }
    return ret;
}

#endif
