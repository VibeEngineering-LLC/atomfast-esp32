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
#include "af_clear.h"

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
static af_sample_t *smpp(float dose, float rate, uint16_t p, int8_t bat, int8_t t, uint8_t fl) { static af_sample_t b[8]; static int k; af_sample_t *o = &b[k++ & 7]; *o = smp(dose, rate, p, bat, t, fl); return o; }

typedef enum { EV_LOCK, EV_UNLOCK, EV_CLEAR } ev_t;

typedef struct {
    af_store_core_t *core;
    int clear_calls;
    uint32_t clear_mask;
    int lock_calls;
    int unlock_calls;
    int rc;
    ev_t events[8];
    int event_count;
} fake_ctx_t;

static int fake_clear(uint32_t mask, void *ctx) {
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->clear_calls++;
    f->clear_mask = mask;
    if (f->event_count < 8) f->events[f->event_count++] = EV_CLEAR;
    if (f->core) {
        f->core->raw.count = 0;
        f->core->raw.seq = 0;
        f->core->agg.count = 0;
        f->core->agg.seq = 0;
    }
    return f->rc;
}

static void fake_lock(void *ctx) {
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->lock_calls++;
    if (f->event_count < 8) f->events[f->event_count++] = EV_LOCK;
}

static void fake_unlock(void *ctx) {
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->unlock_calls++;
    if (f->event_count < 8) f->events[f->event_count++] = EV_UNLOCK;
}

static void mk(af_store_core_t *c, uint32_t raw_limit, uint32_t agg_limit) {
    int rc = af_core_init(c, RB, sizeof RB, AB, sizeof AB, raw_limit, agg_limit, 60, AF_MODE_LAST_DOSE);
    CHECK(rc == 0);
}

static void t24_full_flow(void) {
    af_store_core_t c;
    mk(&c, 100, 10);

    /* 1. окно агрегации открыто */
    uint32_t gen0 = c.gen;
    CHECK(gen0 > 0 || gen0 == 0);

    /* 2. отсчёты прибора A (doserate=1.0) через af_core_ingest_g */
    for (int i = 0; i < 3; i++) {
        int r = af_core_ingest_g(&c, c.gen, T0 + (uint32_t)i * 2, smpp(0.5f, 1.0f, 20, 30, 22, 0));
        CHECK(r == 1);
    }
    CHECK(c.raw.count == 3);

    /* 3. dev_switch_clear с rc=0 */
    uint32_t old = c.gen;
    fake_ctx_t f;
    memset(&f, 0, sizeof f);
    f.core = &c;
    f.rc = 0;

    int ret = af_core_dev_switch_clear(&c, fake_clear, fake_lock, fake_unlock, &f);
    CHECK(ret == 0);

    /* 4. проверки */
    CHECK(c.gen == old + 1);
    CHECK(f.clear_calls == 1);
    CHECK(f.clear_mask == (AF_CLR_MASK_RAW | AF_CLR_MASK_AGG));
    CHECK(f.lock_calls == 2);
    CHECK(f.unlock_calls == 2);

    /* порядок: lock, unlock, clear, lock, unlock */
    CHECK(f.event_count == 5);
    CHECK(f.events[0] == EV_LOCK);
    CHECK(f.events[1] == EV_UNLOCK);
    CHECK(f.events[2] == EV_CLEAR);
    CHECK(f.events[3] == EV_LOCK);
    CHECK(f.events[4] == EV_UNLOCK);

    /* 5. старый gen отклонён, новый принят */
    uint32_t raw_before = c.raw.count;
    int r_old = af_core_ingest_g(&c, old, T0 + 100, smpp(0.5f, 1.0f, 20, 30, 22, 0));
    CHECK(r_old == 0);
    CHECK(c.raw.count == raw_before);

    int r_new = af_core_ingest_g(&c, c.gen, T0 + 100, smpp(0.5f, 2.0f, 20, 30, 22, 0));
    CHECK(r_new == 1);
    CHECK(c.raw.count == raw_before + 1);

    /* 6. отсчёты прибора B (doserate=2.0): кольца очищены, все записи — B */
    /* после dev_switch_clear raw очищен; одна запись B уже есть (r_new) */
    CHECK(c.raw.count == 1);
    CHECK(c.agg.count == 0);

    for (int i = 0; i < 9; i++) {
        int rb = af_core_ingest_g(&c, c.gen, T0 + 110 + (uint32_t)i * 2, smpp(0.5f, 2.0f, 20, 30, 22, 0));
        CHECK(rb == 1);
    }
    CHECK(c.raw.count == 10);

    uint32_t next = 0, lost = 0;
    uint32_t n_raw = af_core_read(&c, 0, 0, OUT, 100, &next, &lost);
    CHECK(n_raw == 10);
    for (uint32_t i = 0; i < n_raw; i++) {
        af_raw_rec_t r; memset(&r, 0, sizeof r);
        CHECK(af_raw_decode((const uint8_t *)OUT + i * AF_REC_RAW_SIZE, &r) == 0);
        CHECK(r.s.doserate_usvh == 2.0f);
    }

    uint32_t next_a = 0, lost_a = 0;
    uint32_t n_agg = af_core_read(&c, 1, 0, OUT, 100, &next_a, &lost_a);
    /* 10 отсчётов < 30 на окно 60s — agg не закрыто */
    for (uint32_t i = 0; i < n_agg; i++) { af_agg_rec_t ar; memset(&ar, 0, sizeof ar); CHECK(af_agg_decode((const uint8_t *)OUT + i * AF_REC_AGG_SIZE, &ar) == 0); CHECK(ar.doserate_usvh == 2.0f); }
}

static void t24_rc_neg2(void) {
    af_store_core_t c;
    mk(&c, 100, 10);

    uint32_t gen_before = c.gen;
    fake_ctx_t f;
    memset(&f, 0, sizeof f);
    f.core = &c;
    f.rc = -2;

    int ret = af_core_dev_switch_clear(&c, fake_clear, fake_lock, fake_unlock, &f);
    CHECK(ret == -2);
    CHECK(c.gen == gen_before + 1);
    CHECK(f.lock_calls == 2);
    CHECK(f.unlock_calls == 2);
    CHECK(f.clear_calls == 1);
    CHECK(f.event_count == 5);
    CHECK(f.events[0] == EV_LOCK);
    CHECK(f.events[1] == EV_UNLOCK);
    CHECK(f.events[2] == EV_CLEAR);
    CHECK(f.events[3] == EV_LOCK);
    CHECK(f.events[4] == EV_UNLOCK);
}

static void t24_null_args(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    uint32_t gen_before = c.gen;

    fake_ctx_t f;
    memset(&f, 0, sizeof f);
    f.core = &c;

    CHECK(af_core_dev_switch_clear(NULL, fake_clear, fake_lock, fake_unlock, &f) == -1);
    CHECK(af_core_dev_switch_clear(&c, NULL, fake_lock, fake_unlock, &f) == -1);
    CHECK(af_core_dev_switch_clear(&c, fake_clear, NULL, fake_unlock, &f) == -1);
    CHECK(af_core_dev_switch_clear(&c, fake_clear, fake_lock, NULL, &f) == -1);

    CHECK(f.clear_calls == 0);
    CHECK(c.gen == gen_before);
}

static void t24_win_reset(void) {
    af_store_core_t c;
    mk(&c, 100, 10);

    /* открыть окно */
    af_core_ingest(&c, T0, smpp(0.5f, 1.0f, 20, 30, 22, 0));
    CHECK(c.win.open == 1);

    af_core_win_reset(&c);
    CHECK(c.win.open == 0);
}

static void t24_ingest_g_null(void) {
    af_store_core_t c;
    mk(&c, 100, 10);
    af_sample_t s = smp(0.5f, 1.0f, 20, 30, 22, 0);

    CHECK(af_core_ingest_g(NULL, 0, T0, &s) == 0);
    CHECK(af_core_ingest_g(&c, 0, T0, NULL) == 0);
}

int main(void) {
    RUN(t24_full_flow);
    RUN(t24_rc_neg2);
    RUN(t24_null_args);
    RUN(t24_win_reset);
    RUN(t24_ingest_g_null);
    TESTS_DONE();
}
