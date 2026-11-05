#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define AF_DS_PTS_MIN 2
#define AF_DS_PTS_MAX 2000

typedef struct {
    uint32_t src;          /* число исходных записей в корзине */
    uint32_t w;            /* суммарный вес: для raw каждая запись весит 1, для agg - число измерений n записи */
    uint32_t ts_first;     /* время первой записи корзины, с (UTC) */
    uint32_t ts_last;      /* время последней записи корзины */
    uint32_t step_s;       /* длительность одной записи последней в корзине: raw = 2, agg = period_s */
    double rate_sum;       /* сумма (doserate * вес) */
    double pulses_sum;     /* сумма (pulses_2s * вес) */
    float rate_min, rate_max;
    float pulses_min, pulses_max;
    float dose_last;
    int8_t battery_last, temp_last;
    uint8_t flags_or;
} af_ds_acc_t;

static inline void af_ds_reset(af_ds_acc_t *a) {
    if (a) memset(a, 0, sizeof(*a));
}

static inline int af_ds_add(af_ds_acc_t *a, uint32_t ts, float dose_msv, float doserate, float pulses_2s, int8_t battery, int8_t temp_c, uint8_t flags, uint32_t weight, uint32_t step_s) {
    if (!a || weight == 0 || ts == 0) return -1;
    if (!isfinite(dose_msv) || !isfinite(doserate) || !isfinite(pulses_2s)) return -1;

    if (a->src == 0) {
        a->ts_first = ts;
        a->rate_min = a->rate_max = doserate;
        a->pulses_min = a->pulses_max = pulses_2s;
    } else {
        if (doserate < a->rate_min) a->rate_min = doserate;
        if (doserate > a->rate_max) a->rate_max = doserate;
        if (pulses_2s < a->pulses_min) a->pulses_min = pulses_2s;
        if (pulses_2s > a->pulses_max) a->pulses_max = pulses_2s;
    }

    a->src++;
    a->w += weight;
    a->ts_last = ts;
    a->step_s = step_s;
    a->rate_sum += (double)doserate * weight;
    a->pulses_sum += (double)pulses_2s * weight;
    a->dose_last = dose_msv;
    a->battery_last = battery;
    a->temp_last = temp_c;
    a->flags_or |= flags;
    return 0;
}

static inline uint32_t af_ds_bucket(uint32_t total, uint32_t pts) {
    if (total == 0 || pts == 0) return 1;
    uint64_t t = total;
    uint64_t p = pts;
    uint64_t res = (t + p - 1) / p;
    if (res < 1) res = 1;
    return (uint32_t)res;
}

static inline int af_ds_header(char *buf, size_t cap) {
    const char *hdr = "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n,doserate_min,doserate_max,cps_min,cps_max\n";
    size_t len = strlen(hdr);
    if (!buf) return -1;
    if (cap < len + 1) {
        if (cap > 0) buf[0] = 0;
        return -1;
    }
    memcpy(buf, hdr, len + 1);
    return (int)len;
}

static inline int af_ds_row(const af_ds_acc_t *a, char *buf, size_t cap) {
    if (!a || a->src == 0 || a->w == 0 || !buf || cap == 0) return -1;

    uint32_t span = (a->ts_last - a->ts_first) + a->step_s;
    double rate_avg = a->rate_sum / (double)a->w;
    double pulses_avg = a->pulses_sum / (double)a->w;
    double cps_avg = pulses_avg / 2.0;
    double cps_min = (double)a->pulses_min / 2.0;
    double cps_max = (double)a->pulses_max / 2.0;

    if (!isfinite(rate_avg) || !isfinite(pulses_avg) || !isfinite(cps_avg) || !isfinite(cps_min) || !isfinite(cps_max)) {
        if (cap > 0) buf[0] = 0;
        return -3;
    }

    int len = snprintf(buf, cap, "%lu,%lu,%.6g,%.6g,%.6g,%.6g,%d,%d,%lu,%lu,%.6g,%.6g,%.6g,%.6g\n",
        (unsigned long)a->ts_first,
        (unsigned long)span,
        (double)a->dose_last,
        rate_avg,
        pulses_avg,
        cps_avg,
        (int)a->battery_last,
        (int)a->temp_last,
        (unsigned long)a->flags_or,
        (unsigned long)a->w,
        (double)a->rate_min,
        (double)a->rate_max,
        cps_min,
        cps_max
    );

    if (len < 0 || (size_t)len >= cap) {
        if (cap > 0) buf[0] = 0;
        return -1;
    }
    return len;
}
