#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_test.h"
#include "af_parse.h"
#include "af_rec.h"
#include "af_ring.h"
#include "af_window.h"
#include "af_store_core.h"
#include "real_samples.h"

#define T0 1760000040u

static uint8_t RB[20*100];
static uint8_t AB[28*10];
static uint8_t OUT[28*200];

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
    const real_t *r = &REAL[i % REAL_N];
    af_sample_t s;
    s.dose_msv = r->dose;
    s.doserate_usvh = r->rate;
    s.pulses_2s = r->p2s;
    s.cps = r->p2s / 2.0f;
    s.battery = r->bat;
    s.temp_c = r->temp;
    s.flags = 0;
    return s;
}

static void ing(af_store_core_t *c, uint32_t ts, af_sample_t s) {
    af_core_ingest(c, ts, &s);
}

static void ing_n(af_store_core_t *c, uint32_t ts0, unsigned n, unsigned step, int real) {
    for (unsigned i = 0; i < n; i++) {
        af_sample_t s = real ? smp_real(i) : smp(0.192f, 0.08f, 35, 28, 25, 0);
        ing(c, ts0 + i * step, s);
    }
}

static void mk(af_store_core_t *c, uint32_t raw_limit, uint32_t agg_limit) {
    int rc = af_core_init(c, RB, sizeof RB, AB, sizeof AB, raw_limit, agg_limit, 60, AF_MODE_LAST_DOSE);
    CHECK(rc == 0);
}

static void t_init_args(void) {
    af_store_core_t c;
    CHECK(af_core_init(NULL, RB, sizeof RB, AB, sizeof AB, 10, 10, 60, AF_MODE_LAST_DOSE) == -1);
    CHECK(af_core_init(&c, RB, sizeof RB, AB, sizeof AB, 10, 10, 59, AF_MODE_LAST_DOSE) == -1);
    CHECK(af_core_init(&c, RB, sizeof RB, AB, sizeof AB, 101, 10, 60, AF_MODE_LAST_DOSE) == -1);
    
    CHECK(af_core_init(&c, RB, sizeof RB, AB, sizeof AB, 10, 5, 60, AF_MODE_LAST_DOSE) == 0);
    CHECK(c.raw.limit == 10);
    CHECK(c.agg.limit == 5);
    
    CHECK(af_core_init(&c, RB, sizeof RB, AB, sizeof AB, 0, 0, 60, AF_MODE_LAST_DOSE) == 0);
    CHECK(c.raw.limit == 100);
}

static void t_raw_always(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    for (int i = 0; i < 5; i++) {
        ing(&c, 0, smp(0.1f, 0.01f, 10, 20, 25, 0));
    }
    CHECK(c.raw.count == 5);
    CHECK(c.ts0_samples == 5);
    CHECK(c.samples == 5);
    CHECK(c.agg.count == 0);
    CHECK(c.agg_closed == 0);
    CHECK(c.win_rejected == 0);
    
    uint32_t next = 0, lost = 0;
    uint32_t n = af_core_read(&c, 0, 0, OUT, 10, &next, &lost);
    CHECK(n == 5);
    for (int i = 0; i < 5; i++) {
        af_raw_rec_t r = {0};
        CHECK(af_raw_decode((const uint8_t*)OUT + i * AF_REC_RAW_SIZE, &r) == 0);
        CHECK(r.ts == 0);
    }
}

static void t_raw_ts_kept(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    float d0 = 0.1f, r0 = 0.01f;
    float d1 = 0.2f, r1 = 0.02f;
    float d2 = 0.3f, r2 = 0.03f;
    
    ing(&c, T0, smp(d0, r0, 10, 20, 25, 0));
    ing(&c, T0 + 2, smp(d1, r1, 20, 20, 25, 0));
    ing(&c, T0 + 4, smp(d2, r2, 30, 20, 25, 0));
    
    uint32_t next = 0, lost = 0;
    uint32_t n = af_core_read(&c, 0, 0, OUT, 10, &next, &lost);
    CHECK(n == 3);
    
    af_raw_rec_t r0_rec, r1_rec, r2_rec;
    CHECK(af_raw_decode((const uint8_t*)OUT + 0 * AF_REC_RAW_SIZE, &r0_rec) == 0);
    CHECK(af_raw_decode((const uint8_t*)OUT + 1 * AF_REC_RAW_SIZE, &r1_rec) == 0);
    CHECK(af_raw_decode((const uint8_t*)OUT + 2 * AF_REC_RAW_SIZE, &r2_rec) == 0);
    
    CHECK(r0_rec.ts == T0);
    CHECK(r1_rec.ts == T0 + 2);
    CHECK(r2_rec.ts == T0 + 4);
    
    CHECK(r0_rec.s.dose_msv == d0);
    CHECK(r0_rec.s.doserate_usvh == r0);
    CHECK(r1_rec.s.dose_msv == d1);
    CHECK(r1_rec.s.doserate_usvh == r1);
    CHECK(r2_rec.s.dose_msv == d2);
    CHECK(r2_rec.s.doserate_usvh == r2);
}

static void t_agg_closes_once_per_window(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    ing_n(&c, T0, 35, 2, 0);
    
    CHECK(c.agg_closed == 1);
    CHECK(c.agg.count == 1);
    CHECK(c.samples == 35);
    CHECK(c.win_rejected == 0);
    
    uint32_t next = 0, lost = 0;
    uint32_t n = af_core_read(&c, 1, 0, OUT, 10, &next, &lost);
    CHECK(n == 1);
    
    af_agg_rec_t a;
    CHECK(af_agg_decode((const uint8_t*)OUT, &a) == 0);
    CHECK(a.ts == T0);
    CHECK(a.period_s == 60);
    CHECK(a.n == 30);
}

static void t_open_window_not_in_agg(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    ing_n(&c, T0, 20, 2, 0);
    CHECK(c.agg.count == 0);
}

static void t_mixed_ts0_then_valid(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    for (int i = 0; i < 3; i++) {
        ing(&c, 0, smp(0.1f, 0.01f, 10, 20, 25, 0));
    }
    ing_n(&c, T0, 35, 2, 0);
    
    CHECK(c.raw.count == 38);
    CHECK(c.ts0_samples == 3);
    CHECK(c.agg_closed == 1);
    CHECK(c.win_rejected == 0);
}

static void t_window_rejected_counts(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    uint32_t samples_before = c.samples;
    uint32_t raw_before = c.raw.count;
    
    ing(&c, T0, smp(0.1f, 5000.0f, 10, 20, 25, 0));
    
    CHECK(c.win_rejected == 1);
    CHECK(c.raw.count == raw_before + 1);
    CHECK(c.samples == samples_before + 1);
}

static void t_raw_overwrite_dropped(void) {
    af_store_core_t c;
    mk(&c, 10, 10);
    ing_n(&c, T0, 25, 2, 0);
    
    CHECK(c.raw.count == 10);
    CHECK(c.raw.dropped == 15);
    
    uint32_t next = 0, lost = 0;
    uint32_t n = af_core_read(&c, 0, 0, OUT, 100, &next, &lost);
    CHECK(n == 10);
    CHECK(lost == 15);
}

static void t_read_since(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    ing_n(&c, T0, 10, 2, 0);
    
    uint32_t next = 0, lost = 0;
    uint32_t n1 = af_core_read(&c, 0, 0, OUT, 4, &next, &lost);
    CHECK(n1 == 4);
    CHECK(next == 4);
    
    uint32_t n2 = af_core_read(&c, 0, next, OUT, 100, &next, &lost);
    CHECK(n2 == 6);
    CHECK(next == 10);
    
    uint32_t n3 = af_core_read(&c, 0, 10, OUT, 100, &next, &lost);
    CHECK(n3 == 0);
    CHECK(lost == 0);
}

static void t_read_bad_kind(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    ing_n(&c, T0, 70, 2, 0);
    
    uint32_t next = 77, lost = 77;
    uint32_t n1 = af_core_read(&c, 2, 0, OUT, 10, &next, &lost);
    CHECK(n1 == 0);
    CHECK(next == 0);
    CHECK(lost == 0);
    
    next = 77; lost = 77;
    uint32_t n2 = af_core_read(&c, -1, 0, OUT, 10, &next, &lost);
    CHECK(n2 == 0);
    CHECK(next == 0);
    CHECK(lost == 0);
}

static void t_ingest_null(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    uint32_t samples_before = c.samples;
    
    af_core_ingest(NULL, T0, NULL);
    af_core_ingest(&c, T0, NULL);
    
    CHECK(c.samples == samples_before);
}

static void t_real_stream(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    ing_n(&c, T0, 90, 2, 1);
    
    CHECK(c.agg_closed == 2);
    
    uint32_t next = 0, lost = 0;
    uint32_t n = af_core_read(&c, 1, 0, OUT, 10, &next, &lost);
    CHECK(n == 2);
    
    uint32_t sum_n = 0;
    for (int i = 0; i < 2; i++) {
        af_agg_rec_t a;
        CHECK(af_agg_decode((const uint8_t*)OUT + i * AF_REC_AGG_SIZE, &a) == 0);
        CHECK(a.ts % 60 == 0);
        CHECK(a.n == 30);
        sum_n += a.n;
        CHECK(a.dose_msv == 0.192f);
    }
    CHECK(sum_n == 60);
}

static void t_en_raw_off(void)
{
    af_store_core_t c;
    mk(&c, 0, 0);
    c.en_raw = 0;
    ing_n(&c, T0, 130, 2, 0);
    CHECK(c.raw.seq == 0);
    CHECK(c.agg_closed >= 3);
    CHECK(c.samples == 130);
}

static void t_en_agg_off(void)
{
    af_store_core_t c;
    mk(&c, 0, 0);
    c.en_agg = 0;
    ing_n(&c, T0, 130, 2, 0);
    CHECK(c.agg.seq == 0);
    CHECK(c.agg_closed == 0);
    CHECK(c.raw.seq == 130);
    CHECK(c.win.open == 0);
    CHECK(c.samples == 130);
}

static void t_en_env_off(void)
{
    af_store_core_t c;
    static uint8_t EB[40 * 20];
    af_env_rec_t r;
    memset(&r, 0, sizeof r);

    mk(&c, 0, 0);
    CHECK(af_core_env_init(&c, EB, sizeof EB, 0) == 0);
    CHECK(af_core_env_push(&c, &r) == 0);
    uint32_t s0 = c.env.seq;
    CHECK(s0 > 0);

    c.en_env = 0;
    CHECK(af_core_env_push(&c, &r) == 1);
    CHECK(c.env.seq == s0);

    c.en_env = 1;
    CHECK(af_core_env_push(&c, &r) == 0);
    CHECK(c.env.seq == s0 + 1);

    af_store_core_t c2;
    mk(&c2, 0, 0);
    c2.en_env = 0;
    CHECK(af_core_env_push(&c2, &r) == -1);
}

static void t_en_reenable_seq(void)
{
    af_store_core_t c;
    mk(&c, 0, 0);
    ing_n(&c, T0, 20, 2, 0);
    uint32_t sr = c.raw.seq;
    uint32_t ep = c.raw.epoch;

    c.en_raw = 0;
    ing_n(&c, T0 + 40, 20, 2, 0);
    CHECK(c.raw.seq == sr);

    c.en_raw = 1;
    ing_n(&c, T0 + 80, 20, 2, 0);
    CHECK(c.raw.seq == sr + 20);
    CHECK(c.raw.epoch == ep);
}

static void t_en_defaults(void)
{
    af_store_core_t c;
    mk(&c, 0, 0);
    CHECK(c.en_raw == 1);
    CHECK(c.en_agg == 1);
    CHECK(c.en_env == 1);
}

static void t_set_window_limits(void)
{
    af_store_core_t c;
    static uint8_t RB2[20 * 100];
    static uint8_t AB2[28 * 20160];

    CHECK(af_core_init(&c, RB2, sizeof RB2, AB2, sizeof AB2, 0, 0, 60, AF_MODE_LAST_DOSE) == 0);

    CHECK(af_core_set_window(&c, 300, 4032) == 0);
    CHECK(c.win.period_s == 300);
    CHECK(c.agg.limit == 4032);

    CHECK(af_core_set_window(&c, 1200, 1008) == 0);
    CHECK(c.agg.limit == 1008);

    CHECK(af_core_set_window(&c, 60, 20160) == 0);
    CHECK(c.agg.limit == 20160);
}

static void t_set_window_bad(void)
{
    af_store_core_t c;
    mk(&c, 0, 5);
    uint16_t ps = c.win.period_s;
    uint32_t lim = c.agg.limit;

    CHECK(af_core_set_window(&c, 59, 5) == -1);
    CHECK(af_core_set_window(&c, 61, 5) == -1);
    CHECK(af_core_set_window(&c, 90, 5) == -1);
    CHECK(af_core_set_window(&c, 1260, 5) == -1);
    CHECK(af_core_set_window(&c, 0, 5) == -1);
    CHECK(af_core_set_window(&c, 300, 0) == -1);
    CHECK(af_core_set_window(&c, 300, 11) == -1);
    CHECK(af_core_set_window(NULL, 60, 5) == -1);

    CHECK(c.win.period_s == ps);
    CHECK(c.agg.limit == lim);
}

static void t_set_window_drops_open(void)
{
    af_store_core_t c;
    mk(&c, 0, 0);
    ing_n(&c, T0, 10, 2, 0);
    CHECK(c.win.open == 1);

    CHECK(af_core_set_window(&c, 120, 5) == 0);
    CHECK(c.win.open == 0);
    CHECK(c.agg_closed == 0);

    ing_n(&c, T0 + 20, 200, 2, 0);
    CHECK(c.agg_closed >= 2);
    CHECK(c.agg_closed <= 4);

    uint32_t n = af_core_read(&c, 1, 0, OUT, 10, NULL, NULL);
    CHECK(n >= 2);
}

static void t_en_agg_off_drops_open(void) {
    af_store_core_t c;
    mk(&c, 0, 0);
    ing_n(&c, T0, 10, 2, 0);
    CHECK(c.win.open == 1);          /* окно уже открыто до выключения */
    c.en_agg = 0;
    ing(&c, T0 + 20, smp(0.192f, 0.08f, 35, 28, 25, 0));
    CHECK(c.win.open == 0);          /* выключение сбрасывает открытое окно */
    c.en_agg = 1;
    ing_n(&c, T0 + 22, 40, 2, 0);
    CHECK(c.agg_closed <= 1);        /* старый хвост не склеен с новыми отсчётами */
}

int main(void) {
    RUN(t_init_args);
    RUN(t_raw_always);
    RUN(t_raw_ts_kept);
    RUN(t_agg_closes_once_per_window);
    RUN(t_open_window_not_in_agg);
    RUN(t_mixed_ts0_then_valid);
    RUN(t_window_rejected_counts);
    RUN(t_raw_overwrite_dropped);
    RUN(t_read_since);
    RUN(t_read_bad_kind);
    RUN(t_ingest_null);
    RUN(t_real_stream);
    RUN(t_en_raw_off);
    RUN(t_en_agg_off);
    RUN(t_en_env_off);
    RUN(t_en_reenable_seq);
    RUN(t_en_defaults);
    RUN(t_set_window_limits);
    RUN(t_set_window_bad);
    RUN(t_set_window_drops_open);
    RUN(t_en_agg_off_drops_open);
    TESTS_DONE();
}
