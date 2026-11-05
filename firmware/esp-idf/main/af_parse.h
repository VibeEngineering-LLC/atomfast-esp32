#ifndef AF_PARSE_H
#define AF_PARSE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

typedef struct {
    uint8_t flags;
    float dose_msv;
    float doserate_usvh;
    uint16_t pulses_2s;
    float cps;
    int8_t battery;
    int8_t temp_c;
} af_sample_t;

typedef enum {
    AF_OK = 0,
    AF_ERR_LEN = 1,
    AF_ERR_RATE = 2,
    AF_ERR_DOSE = 3
} af_status_t;

static inline af_status_t af_parse(const uint8_t *buf, size_t len, af_sample_t *out) {
    if (len < 13) {
        return AF_ERR_LEN;
    }

    out->flags = buf[0];

    /* Decode float32 LE: assemble uint32 from bytes, then memcpy to float */
    uint32_t dose_bits = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
    memcpy(&out->dose_msv, &dose_bits, sizeof(float));

    uint32_t rate_bits = (uint32_t)buf[5] | ((uint32_t)buf[6] << 8) | ((uint32_t)buf[7] << 16) | ((uint32_t)buf[8] << 24);
    memcpy(&out->doserate_usvh, &rate_bits, sizeof(float));

    out->pulses_2s = (uint16_t)buf[9] | ((uint16_t)buf[10] << 8);
    out->cps = (float)out->pulses_2s / 2.0f;

    out->battery = (int8_t)buf[11];
    out->temp_c = (int8_t)buf[12];

    /* Validate doserate */
    if (!isfinite(out->doserate_usvh) || out->doserate_usvh < 0.0f || out->doserate_usvh > 1000.0f) {
        return AF_ERR_RATE;
    }

    /* Validate dose */
    if (!isfinite(out->dose_msv) || out->dose_msv < 0.0f) {
        return AF_ERR_DOSE;
    }

    return AF_OK;
}

static inline int af_reread_config(const af_sample_t *s) {
    return (s->flags >> 3) & 1;
}

#endif /* AF_PARSE_H */

