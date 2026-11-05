#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_test.h"
#include "af_parse.h"
#include "af_rec.h"
#include "af_window.h"
#include "real_samples.h"

#define T0 1760000040u

static af_sample_t smp(float dose, float rate, uint16_t p, int8_t bat, int8_t t, uint8_t fl) {
    af_sample_t s;
    s.dose_msv = dose;
    s.doserate_usvh = rate;
    s.pulses_2s = p;
    s.cps = p / 2.0f;
    s.battery = bat;
    s.temp_c = t;
    s.flags = fl;
    return s;
}

static af_sample_t smp_real(unsigned i) {
    real_t r = REAL[i % REAL_N];
    af_sample_t s;
    s.dose_msv = r.dose;
    s.doserate_usvh = r.rate;
    s.pulses_2s = (uint16_t)r.p2s;
    s.cps = r.p2s / 2.0f;
    s.battery = (int8_t)r.bat;
    s.temp_c = (int8_t)r.temp;
    s.flags = 0;
    return s;
}

static void t_init(void) {
    af_window_t w;
    CHECK(af_window_init(&w, 59, 0) == -1);
    CHECK(af_window_init(&w, 61, 0) == -1);
    CHECK(af_window_init(&w, 0, 0) == -1);
    CHECK(af_window_init(&w, 1260, 0) == -1);
    CHECK(af_window_init(&w, 1201, 0) == -1);
    CHECK(af_window_init(&w, 60, 0x20) == -1);
    CHECK(af_window_init(NULL, 60, 0) == -1);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_init(&w, 600, 0x1F) == 0);
    CHECK(af_window_init(&w, 1200, 0) == 0);
}

static void t_window_30(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    CHECK(af_window_init(&w, 60, 0) == 0);
    for (unsigned i = 0; i < 30; i++) {
        s = smp_real(i);
        CHECK(af_window_add(&w, T0 + 2 * i, &s, &out) == 0);
    }
    s = smp_real(30);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.ts == T0);
    CHECK(out.period_s == 60);
    CHECK(out.n == 30);
}

static void t_avg_real(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    double sum_d = 0.0, sum_r = 0.0, sum_p = 0.0;
    CHECK(af_window_init(&w, 60, 0) == 0);
    for (unsigned i = 0; i < 30; i++) {
        s = smp_real(i);
        CHECK(af_window_add(&w, T0 + 2 * i, &s, &out) == 0);
        sum_d += REAL[i].dose;
        sum_r += REAL[i].rate;
        sum_p += REAL[i].p2s;
    }
    s = smp_real(30);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    
    double avg_d = sum_d / 30.0;
    double avg_r = sum_r / 30.0;
    double avg_p = sum_p / 30.0;
    
    CHECK(fabs((double)out.dose_msv - avg_d) <= 1e-6 * fmax(1.0, fabs(avg_d)));
    CHECK(fabs((double)out.doserate_usvh - avg_r) <= 1e-6 * fmax(1.0, fabs(avg_r)));
    CHECK(fabs((double)out.pulses_2s - avg_p) <= 1e-6 * fmax(1.0, fabs(avg_p)));
}

static void t_one_sample(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.192f, 0.078f, 33, 28, 25, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.n == 1);
    CHECK(out.dose_msv == 0.192f);
    CHECK(out.doserate_usvh == 0.078f);
    CHECK(out.pulses_2s == 33.0f);
    CHECK(out.battery == 28);
    CHECK(out.temp_c == 25);

    CHECK(af_window_init(&w, 60, 0x1F) == 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.n == 1);
    CHECK(out.dose_msv == 0.192f);
    CHECK(out.doserate_usvh == 0.078f);
    CHECK(out.pulses_2s == 33.0f);
    CHECK(out.battery == 28);
    CHECK(out.temp_c == 25);
}

static void t_modes_each(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t a, b;
    uint8_t modes[5] = {0x01, 0x02, 0x04, 0x08, 0x10};
    
    a = smp(1.0f, 2.0f, 10, 20, 30, 0);
    b = smp(3.0f, 6.0f, 30, 40, 50, 0);
    
    for (int i = 0; i < 5; i++) {
        CHECK(af_window_init(&w, 60, modes[i]) == 0);
        CHECK(af_window_add(&w, T0, &a, &out) == 0);
        CHECK(af_window_add(&w, T0 + 10, &b, &out) == 0);
        CHECK(af_window_add(&w, T0 + 60, &a, &out) == 1);
        
        if (modes[i] & AF_MODE_LAST_DOSE) CHECK(out.dose_msv == 3.0f); else CHECK(out.dose_msv == 2.0f);
        if (modes[i] & AF_MODE_LAST_RATE) CHECK(out.doserate_usvh == 6.0f); else CHECK(out.doserate_usvh == 4.0f);
        if (modes[i] & AF_MODE_LAST_PULSES) CHECK(out.pulses_2s == 30.0f); else CHECK(out.pulses_2s == 20.0f);
        if (modes[i] & AF_MODE_LAST_BAT) CHECK(out.battery == 40); else CHECK(out.battery == 30);
        if (modes[i] & AF_MODE_LAST_TEMP) CHECK(out.temp_c == 50); else CHECK(out.temp_c == 40);
    }
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &a, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &b, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &a, &out) == 1);
    CHECK(out.dose_msv == 2.0f);
    CHECK(out.doserate_usvh == 4.0f);
    CHECK(out.pulses_2s == 20.0f);
    CHECK(out.battery == 30);
    CHECK(out.temp_c == 40);

    CHECK(af_window_init(&w, 60, 0x1F) == 0);
    CHECK(af_window_add(&w, T0, &a, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &b, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &a, &out) == 1);
    CHECK(out.dose_msv == 3.0f);
    CHECK(out.doserate_usvh == 6.0f);
    CHECK(out.pulses_2s == 30.0f);
    CHECK(out.battery == 40);
    CHECK(out.temp_c == 50);
}

static void t_round_pos(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s1, s2, s3;
    
    s1 = smp(0.0f, 0.0f, 0, 0, 25, 0);
    s2 = smp(0.0f, 0.0f, 0, 0, 26, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == 26);

    s3 = smp(0.0f, 0.0f, 0, 0, 25, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s3, &out) == 0);
    CHECK(af_window_add(&w, T0 + 20, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == 25);

    s1 = smp(0.0f, 0.0f, 0, 28, 0, 0);
    s2 = smp(0.0f, 0.0f, 0, 29, 0, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.battery == 29);
}

static void t_round_neg(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s1, s2, s3;
    
    s1 = smp(0.0f, 0.0f, 0, 0, -25, 0);
    s2 = smp(0.0f, 0.0f, 0, 0, -26, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == -26);

    s3 = smp(0.0f, 0.0f, 0, 0, -25, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s3, &out) == 0);
    CHECK(af_window_add(&w, T0 + 20, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == -25);

    s1 = smp(0.0f, 0.0f, 0, 0, -128, 0);
    s2 = smp(0.0f, 0.0f, 0, 0, -127, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == -128);

    s1 = smp(0.0f, 0.0f, 0, 0, 127, 0);
    s2 = smp(0.0f, 0.0f, 0, 0, 126, 0);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.temp_c == 127);
}

static void t_flags_or(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s1, s2;
    
    s1 = smp(0.0f, 0.0f, 0, 0, 0, 0x08);
    s2 = smp(0.0f, 0.0f, 0, 0, 0, 0x01);
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, T0, &s1, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s2, &out) == 0);
    s1 = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 60, &s1, &out) == 1);
    CHECK(out.flags == 0x09);
    CHECK(out.agg_flags == 0);

    s1 = smp(0.0f, 0.0f, 0, 0, 0, 0x04);
    CHECK(af_window_add(&w, T0 + 70, &s1, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.flags == 0x04);
}

static void t_alignment(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    uint32_t ts = 1760000123u;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, ts, &s, &out) == 0);
    CHECK(af_window_add(&w, ts + 60, &s, &out) == 1);
    CHECK(out.ts == 1760000100u);

    CHECK(af_window_init(&w, 1200, 0) == 0);
    CHECK(af_window_add(&w, ts, &s, &out) == 0);
    CHECK(af_window_add(&w, ts + 1200, &s, &out) == 1);
    CHECK(out.ts == ts - (ts % 1200u));
}

static void t_boundary(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 59, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
}

static void t_gap_no_empty(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 5 * 60 + 7, &s, &out) == 1);
    CHECK(out.ts == T0);
    CHECK(out.n == 1);
    CHECK(af_window_add(&w, T0 + 5 * 60 + 9, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.ts == T0 + 300);
    CHECK(out.n == 2);
}

static void t_timejump_inside(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 4, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.n == 2);
    CHECK((out.agg_flags & AF_AGG_F_TIMEJUMP) != 0);
    CHECK((out.agg_flags & AF_AGG_F_PARTIAL) == 0);
}

static void t_timejump_before(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    uint32_t ts_before = T0 - 100;
    uint32_t aligned_ts = ts_before - (ts_before % 60u);
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_add(&w, ts_before, &s, &out) == 1);
    CHECK(out.ts == T0);
    CHECK(out.n == 1);
    CHECK((out.agg_flags & AF_AGG_F_TIMEJUMP) != 0);
    
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.ts == aligned_ts);
    CHECK(out.n == 1);
    CHECK(out.agg_flags == AF_AGG_F_PARTIAL);
}

static void t_no_timejump(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.agg_flags == 0);
}

static void t_ts_zero(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, 0, &s, &out) == -2);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.n == 1);
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    CHECK(af_window_add(&w, 0, &s, &out) == -2);
    CHECK(af_window_flush(&w, &out) == 0);
}

static void t_invalid_sample(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    
    s = smp(NAN, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(INFINITY, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(-0.001f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(0.0f, NAN, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(0.0f, -0.001f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(0.0f, 1000.5f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(0.0f, INFINITY, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == -3);
    
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    
    s = smp(0.0f, 1000.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    
    s = smp(NAN, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 20, &s, &out) == -3);
    CHECK(af_window_add(&w, T0 + 30, &s, &out) == -3);
    CHECK(af_window_add(&w, T0 + 40, &s, &out) == -3);
    
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.n == 2);
}

static void t_null_args(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    
    CHECK(af_window_add(NULL, T0, &s, &out) == -1);
    CHECK(af_window_add(&w, T0, NULL, &out) == -1);
    CHECK(af_window_add(&w, T0, &s, NULL) == -1);
    CHECK(af_window_flush(NULL, &out) == -1);
    CHECK(af_window_flush(&w, NULL) == -1);
    CHECK(af_window_add(&w, 0, NULL, &out) == -1);
}

static void t_flush_empty(void) {
    af_window_t w;
    af_agg_rec_t out;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    out.n = 777;
    CHECK(af_window_flush(&w, &out) == 0);
    CHECK(out.n == 777);
}

static void t_flush_partial(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 20, &s, &out) == 0);
    
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.n == 3);
    CHECK((out.agg_flags & AF_AGG_F_PARTIAL) != 0);
    
    CHECK(af_window_flush(&w, &out) == 0);
    
    CHECK(af_window_add(&w, T0 + 70, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 130, &s, &out) == 1);
    CHECK(out.n == 1);
}

static void t_period_change(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    uint32_t ts = T0 + 5;
    uint32_t aligned_ts = T0 - (T0 % 600u);
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.n == 2);
    
    CHECK(af_window_init(&w, 600, 0) == 0);
    CHECK(af_window_add(&w, ts, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.period_s == 600);
    CHECK(out.ts == aligned_ts);
}

static void t_ts_max(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    uint32_t ts = 0xFFFFFFFFu;
    uint32_t aligned_ts = ts - (ts % 60u);
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, ts, &s, &out) == 0);
    CHECK(af_window_add(&w, ts, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.n == 2);
    CHECK(out.ts == aligned_ts);
}

static void t_n_saturation(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    int ret;
    
    CHECK(af_window_init(&w, 1200, 0) == 0);
    s = smp(0.5f, 0.1f, 20, 28, 25, 0);
    
    for (int i = 0; i < 70000; i++) {
        ret = af_window_add(&w, T0, &s, &out);
        CHECK(ret != 1);
    }
    
    CHECK(af_window_add(&w, T0 + 1200, &s, &out) == 1);
    CHECK(out.n == 65535);
    CHECK(fabs((double)out.doserate_usvh - 0.1f) <= 1e-6 * fmax(1.0, fabs(0.1f)));
}

static void t_modes_survive(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0x15) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    
    CHECK(af_window_add(&w, T0 + 70, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 130, &s, &out) == 1);
    CHECK(out.modes == 0x15);
    CHECK(out.period_s == 60);
}

static void t_second_close(void) {
    af_window_t w;
    af_agg_rec_t out;
    af_sample_t s;
    
    CHECK(af_window_init(&w, 60, 0) == 0);
    s = smp(0.0f, 0.0f, 0, 0, 0, 0);
    
    CHECK(af_window_add(&w, T0, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 10, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 60, &s, &out) == 1);
    CHECK(out.ts == T0);
    CHECK(out.n == 2);
    
    CHECK(af_window_add(&w, T0 + 70, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 80, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 120, &s, &out) == 1);
    CHECK(out.ts == T0 + 60);
    CHECK(out.n == 3);
    
    CHECK(af_window_add(&w, T0 + 130, &s, &out) == 0);
    CHECK(af_window_add(&w, T0 + 140, &s, &out) == 0);
    CHECK(af_window_flush(&w, &out) == 1);
    CHECK(out.ts == T0 + 120);
    CHECK(out.n == 3);
}

int main(void) {
    RUN(t_init);
    RUN(t_window_30);
    RUN(t_avg_real);
    RUN(t_one_sample);
    RUN(t_modes_each);
    RUN(t_round_pos);
    RUN(t_round_neg);
    RUN(t_flags_or);
    RUN(t_alignment);
    RUN(t_boundary);
    RUN(t_gap_no_empty);
    RUN(t_timejump_inside);
    RUN(t_timejump_before);
    RUN(t_no_timejump);
    RUN(t_ts_zero);
    RUN(t_invalid_sample);
    RUN(t_null_args);
    RUN(t_flush_empty);
    RUN(t_flush_partial);
    RUN(t_period_change);
    RUN(t_ts_max);
    RUN(t_n_saturation);
    RUN(t_modes_survive);
    RUN(t_second_close);
    TESTS_DONE();
}
