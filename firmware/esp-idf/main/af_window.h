#ifndef AF_WINDOW_H
#define AF_WINDOW_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "af_parse.h"
#include "af_rec.h"

#define AF_WIN_MIN_S 60
#define AF_WIN_MAX_S 1200

typedef struct {
    uint16_t period_s;
    uint8_t modes;
    uint8_t open;
    uint8_t agg_flags;
    uint8_t flags_or;
    uint32_t start;
    uint32_t last_ts;
    uint32_t n;
    double sum_dose, sum_rate, sum_pulses;
    int32_t sum_bat, sum_temp;
    af_sample_t last;
} af_window_t;

static inline int af_window_init(af_window_t *w, uint16_t period_s, uint8_t modes) {
    if (!w || period_s < AF_WIN_MIN_S || period_s > AF_WIN_MAX_S || (period_s % 60) != 0 || (modes & ~AF_MODE_MASK) != 0) {
        return -1;
    }
    memset(w, 0, sizeof(af_window_t));
    w->period_s = period_s;
    w->modes = modes;
    return 0;
}

static inline void af_window_accept(af_window_t *w, uint32_t ts, const af_sample_t *s) {
    if (w->n == 65535) return;
    w->n++;
    w->sum_dose += s->dose_msv;
    w->sum_rate += s->doserate_usvh;
    w->sum_pulses += s->pulses_2s;
    w->sum_bat += s->battery;
    w->sum_temp += s->temp_c;
    w->last = *s;
    w->last_ts = ts;
    w->flags_or |= s->flags;
}

static inline void af_window_close(af_window_t *w, af_agg_rec_t *out) {
    out->ts = w->start;
    out->period_s = w->period_s;
    out->n = (uint16_t)w->n;
    out->modes = w->modes;
    out->flags = w->flags_or;
    out->agg_flags = w->agg_flags;

    if (w->modes & AF_MODE_LAST_DOSE) {
        out->dose_msv = w->last.dose_msv;
    } else {
        out->dose_msv = (float)(w->sum_dose / (double)w->n);
    }

    if (w->modes & AF_MODE_LAST_RATE) {
        out->doserate_usvh = w->last.doserate_usvh;
    } else {
        out->doserate_usvh = (float)(w->sum_rate / (double)w->n);
    }

    if (w->modes & AF_MODE_LAST_PULSES) {
        out->pulses_2s = (float)w->last.pulses_2s;
    } else {
        out->pulses_2s = (float)(w->sum_pulses / (double)w->n);
    }

    if (w->modes & AF_MODE_LAST_BAT) {
        out->battery = w->last.battery;
    } else {
        double v = (double)w->sum_bat / (double)w->n;
        double r = (v >= 0.0) ? floor(v + 0.5) : -floor(-v + 0.5);
        out->battery = (int8_t)r;
    }

    if (w->modes & AF_MODE_LAST_TEMP) {
        out->temp_c = w->last.temp_c;
    } else {
        double v = (double)w->sum_temp / (double)w->n;
        double r = (v >= 0.0) ? floor(v + 0.5) : -floor(-v + 0.5);
        out->temp_c = (int8_t)r;
    }

    w->open = 0;
    w->agg_flags = 0;
    w->flags_or = 0;
    w->start = 0;
    w->last_ts = 0;
    w->n = 0;
    w->sum_dose = 0.0;
    w->sum_rate = 0.0;
    w->sum_pulses = 0.0;
    w->sum_bat = 0;
    w->sum_temp = 0;
    memset(&w->last, 0, sizeof(af_sample_t));
}

static inline int af_window_add(af_window_t *w, uint32_t ts, const af_sample_t *s, af_agg_rec_t *out) {
    if (!w || !s || !out) return -1;
    if (ts == 0) return -2;
    if (!isfinite(s->dose_msv) || !isfinite(s->doserate_usvh) || s->dose_msv < 0.0f || s->doserate_usvh < 0.0f || s->doserate_usvh > 1000.0f) return -3;

    uint32_t s_start = ts - (ts % w->period_s);

    if (!w->open) {
        w->open = 1;
        w->start = s_start;
        af_window_accept(w, ts, s);
        return 0;
    }

    if ((uint64_t)ts >= (uint64_t)w->start + (uint64_t)w->period_s) {
        af_window_close(w, out);
        w->open = 1;
        w->start = s_start;
        af_window_accept(w, ts, s);
        return 1;
    }

    if (ts < w->start) {
        w->agg_flags |= AF_AGG_F_TIMEJUMP;
        af_window_close(w, out);
        w->open = 1;
        w->start = s_start;
        af_window_accept(w, ts, s);
        return 1;
    }

    if (ts < w->last_ts) {
        w->agg_flags |= AF_AGG_F_TIMEJUMP;
    }
    af_window_accept(w, ts, s);
    return 0;
}

static inline int af_window_flush(af_window_t *w, af_agg_rec_t *out) {
    if (!w || !out) return -1;
    if (!w->open) return 0;
    w->agg_flags |= AF_AGG_F_PARTIAL;
    af_window_close(w, out);
    return 1;
}

#endif
