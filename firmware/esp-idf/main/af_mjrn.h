#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "af_meas.h"

#define AF_MJRN_E_RANGE   (-1)
#define AF_MJRN_E_NODATA  (-2)
#define AF_MJRN_E_LONG    (-3)
#define AF_MJRN_MAX_S     (36u * 3600u)

typedef struct {
    uint32_t from, to;
    uint32_t last_ts;
    uint32_t npk;
    uint32_t counts;
    uint32_t lost;
    uint32_t gaps;
    double rate_sum;
    uint32_t bad;
    uint32_t lost_max;   /* T-57: допуск потерь (общее правило T-56: valid = npk>=1 && lost<=lost_max) */
} af_mjrn_t;

static inline void af_mjrn_init(af_mjrn_t *a, uint32_t from, uint32_t to) {
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->from = from;
    a->to = to;
    a->last_ts = from;
    a->lost_max = 2;
}

static inline int af_mjrn_add(af_mjrn_t *a, uint32_t ts, uint16_t pulses, float rate_usvh) {
    if (!a) return -1;
    
    if (ts < a->from || ts >= a->to || ts < a->last_ts || !isfinite(rate_usvh) || rate_usvh < 0.0f) {
        a->bad++;
        return -1;
    }

    uint64_t gap_ms = (uint64_t)(ts - a->last_ts) * 1000u;
    if (gap_ms > AF_MEAS_GAP_MS) {
        uint32_t missed = (uint32_t)((gap_ms + 1000u) / 2000u) - 1u;
        a->lost += missed;
        a->gaps++;
    }

    a->npk++;
    if (a->counts > 0xFFFFFFFFu - pulses) {
        a->counts = 0xFFFFFFFFu;
    } else {
        a->counts += pulses;
    }
    
    a->rate_sum += (double)rate_usvh;
    a->last_ts = ts;
    return 0;
}

static inline int af_mjrn_finish(const af_mjrn_t *a, af_meas_res_t *out) {
    if (!a || !out) return AF_MJRN_E_RANGE;
    if (a->from == 0 || a->to <= a->from || (a->to - a->from) > AF_MJRN_MAX_S) return AF_MJRN_E_RANGE;
    if (a->npk == 0) return AF_MJRN_E_NODATA;
    if (a->npk > 65535u) return AF_MJRN_E_LONG;

    memset(out, 0, sizeof(*out));
    
    uint32_t lost = a->lost;
    uint64_t tail_ms = (uint64_t)(a->to - a->last_ts) * 1000u;
    if (tail_ms > AF_MEAS_GAP_MS) {
        lost += (uint32_t)((tail_ms + 1000u) / 2000u) - 1u;
    }

    out->i = 1;
    out->attempt = 1;
    out->valid = (a->npk >= 1 && lost <= a->lost_max) ? 1 : 0;
    out->stop_reason = out->valid ? AF_MEAS_SR_TIME : AF_MEAS_SR_ERROR;
    out->t0_utc = a->from;
    out->t0_mono_s = 0;
    out->t_ms = (a->to - a->from) * 1000u;
    out->counts = a->counts;
    out->npk = (uint16_t)a->npk;
    out->lost = lost > 65535u ? 65535u : (uint16_t)lost;
    out->flags = AF_MEAS_F_UTC_OK | AF_MEAS_F_FINAL;

    af_meas_fill_stats(out, a->counts, a->npk, a->rate_sum);
    return 0;
}
