#include "af_test.h"
#include "af_meas.h"
#include "af_mjrn.h"
#include "af_mjadd.h"
#include "meas_t_common.inc"

#include <math.h>
#include <string.h>

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void t_basic(void) {
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    for (int k = 0; k < 30; k++) {
        uint32_t ts = from + 2u * (uint32_t)k;
        CHECK(af_mjrn_add(&a, ts, 35, 0.08f) == 0);
    }
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == 0);
    CHECK(res.counts == 1050u);
    CHECK(res.npk == 30u);
    CHECK(res.lost == 0u);
    CHECK(near(res.cps, 17.5f, 1e-4f));
    CHECK(near(res.usv_h, 0.08f, 1e-6f));
    CHECK(res.t_ms == 60000u);
    CHECK(res.t0_utc == from);
    CHECK(res.valid == 1);
    CHECK(near(res.eps_pct, 100.0f / sqrtf(1050.0f), 1e-4f));
}

static void t_equiv_live(void) {
    for (int iter = 0; iter < 2; iter++) {
        t_setup();
        uint32_t seed = (iter == 0) ? 12345u : 777u;
        uint32_t s = seed;
        af_meas_params_t p = t_params();
        p.n = 1;
        p.t_s = 120;
        p.lost_max = 60;
        uint32_t id = 0;
        CHECK(af_meas_start(&C, &p, T0, 1, 1700000000u, &id) == 0);

        uint32_t from = 1700000000u;
        uint32_t to = from + 120u;
        af_mjrn_t a;
        af_mjrn_init(&a, from, to);

        uint64_t rx = T0 + 1000ull;
        while (rx < T0 + 120000ull) {
            uint32_t r = xs32(&s);
            uint32_t add = 0;
            if ((r & 3u) != 0) {
                add = 2000u * (1u + (r % 4u));
            }
            uint16_t pulses = (uint16_t)(20 + (r % 30));
            float rate = 0.05f + (float)(r % 100) / 1000.0f;
            af_meas_feed(&C, rx, pulses, rate);
            uint32_t ts = from + (uint32_t)((rx - T0) / 1000ull);
            af_mjrn_add(&a, ts, pulses, rate);
            rx += 2000ull + add;
        }
        af_meas_tick(&C, T0 + 120001ull, 1, 1700000000u + 120u);

        const af_meas_res_t *live = t_row(id, 0);
        CHECK(live != NULL);
        if (live == NULL) {
            t_teardown();
            return;
        }

        af_meas_res_t mj;
        CHECK(af_mjrn_finish(&a, &mj) == 0);

        CHECK(live->lost > 0u);   /* сценарий обязан содержать пропуски, иначе сверка потерь пуста */
        CHECK(live->counts == mj.counts);
        CHECK(live->npk == mj.npk);
        CHECK(live->lost == mj.lost);
        CHECK(fabsf(live->cps - mj.cps) <= 1e-5f * fmaxf(1.0f, fabsf(live->cps)));
        CHECK(fabsf(live->usv_h - mj.usv_h) <= 1e-5f * fmaxf(1.0f, fabsf(live->usv_h)));
        CHECK(fabsf(live->eps_pct - mj.eps_pct) <= 1e-5f * fmaxf(1.0f, fabsf(live->eps_pct)));
        CHECK(fabsf(live->cps_sigma - mj.cps_sigma) <= 1e-5f * fmaxf(1.0f, fabsf(live->cps_sigma)));
        CHECK(fabsf(live->usv_h_sigma - mj.usv_h_sigma) <= 1e-5f * fmaxf(1.0f, fabsf(live->usv_h_sigma)));
        t_teardown();
        CHECK(g_alloc_n == g_free_n);
    }
}

static void t_gaps(void) {
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    CHECK(af_mjrn_add(&a, from + 0u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, from + 2u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, from + 4u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, from + 20u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, from + 22u, 10, 0.1f) == 0);
    CHECK(a.gaps == 1u);
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == 0);
    CHECK(res.lost == 25u);
}

static void t_head_gap(void) {
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    CHECK(af_mjrn_add(&a, from + 10u, 10, 0.1f) == 0);
    CHECK(a.gaps == 1u);
    CHECK(a.lost == 4u);
    for (uint32_t ts = from + 12u; ts < to; ts += 2u) {
        af_mjrn_add(&a, ts, 10, 0.1f);
    }
}

static void t_bounds(void) {
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    CHECK(af_mjrn_add(&a, from - 1u, 10, 0.1f) == -1);
    CHECK(af_mjrn_add(&a, from, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, to - 1u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, to, 10, 0.1f) == -1);
    CHECK(a.npk == 2u);
    CHECK(a.bad == 2u);
    af_mjrn_init(&a, from, to);
    CHECK(af_mjrn_add(&a, from + 10u, 10, 0.1f) == 0);
    CHECK(af_mjrn_add(&a, from + 8u, 10, 0.1f) == -1);
    CHECK(af_mjrn_add(&a, from + 12u, 10, NAN) == -1);
    CHECK(af_mjrn_add(&a, from + 14u, 10, -1.0f) == -1);
}

static void t_errors(void) {
    uint32_t from = 1700000000u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, from + 60u);
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_NODATA);
    af_mjrn_init(&a, 0u, 60u);
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_RANGE);
    af_mjrn_init(&a, from, from);
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_RANGE);
    af_mjrn_init(&a, from, from - 1u);
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_RANGE);
    af_mjrn_init(&a, from, from + AF_MJRN_MAX_S + 1u);
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_RANGE);
    af_mjrn_init(&a, from, from + AF_MJRN_MAX_S);
    CHECK(af_mjrn_add(&a, from, 10, 0.1f) == 0);
    CHECK(af_mjrn_finish(&a, &res) == 0);
    CHECK(af_mjrn_finish(NULL, &res) == AF_MJRN_E_RANGE);
    CHECK(af_mjrn_finish(&a, NULL) == AF_MJRN_E_RANGE);
}

static void t_long(void) {
    uint32_t from = 1700000000u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, from + 60u);
    CHECK(af_mjrn_add(&a, from, 10, 0.1f) == 0);
    a.npk = 65536u;
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == AF_MJRN_E_LONG);
    a.npk = 65535u;
    CHECK(af_mjrn_finish(&a, &res) == 0);
    CHECK(res.npk == 65535u);
}

static void t_saturate(void) {
    uint32_t from = 1700000000u;
    uint32_t to = from + 140000u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    for (int i = 0; i < 70000; i++) {
        uint32_t ts = from + (uint32_t)i * 2u;
        af_mjrn_add(&a, ts, 65535, 0.1f);
    }
    uint32_t c1 = a.counts;
    CHECK(c1 == 0xFFFFFFFFu);
    a.counts = 0xFFFFFFF0u;
    af_mjrn_add(&a, a.last_ts, 100, 0.1f);
    CHECK(a.counts == 0xFFFFFFFFu);
}

static void t_add_journal(void) {
    t_setup();
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    for (int k = 0; k < 30; k++) {
        af_mjrn_add(&a, from + 2u * (uint32_t)k, 35, 0.08f);
    }
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == 0);

    uint32_t id = 0;
    CHECK(af_meas_add_journal(&C, "Фон", &res, from, &id) == 0);
    CHECK(id == 1u);
    af_meas_run_t *r = af_meas_find(&C, id);
    CHECK(r != NULL);
    if (r == NULL) {
        t_teardown();
        return;
    }
    CHECK(r->mode == AF_MEAS_MODE_JOURNAL);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(r->done_i == 1u);
    CHECK(r->res_n == 1u);
    CHECK(r->start_utc == from);
    CHECK(strcmp(r->name, "Фон") == 0);
    CHECK(C.active == -1);
    const af_meas_res_t *row = t_row(id, 0);
    CHECK(row != NULL);
    if (row != NULL) CHECK(row->counts == 1050u);
    CHECK(g_alloc_n == 1u);

    uint32_t id2 = 0;
    CHECK(af_meas_add_journal(&C, NULL, &res, from, &id2) == 0);

    char bad[34];
    memset(bad, 'a', 33);
    bad[33] = '\0';
    CHECK(af_meas_add_journal(&C, bad, &res, from, &id2) == AF_MEAS_E_VALUE);
    CHECK(af_meas_add_journal(&C, "a\x01b", &res, from, &id2) == AF_MEAS_E_VALUE);
    CHECK(af_meas_add_journal(&C, "x", NULL, from, &id2) == AF_MEAS_E_VALUE);

    g_alloc_fail_all = 1;
    CHECK(af_meas_add_journal(&C, "y", &res, from, &id2) == AF_MEAS_E_NOMEM);
    g_alloc_fail_all = 0;
    t_teardown();
    CHECK(g_alloc_n == g_free_n);
}

static void t_slots(void) {
    t_setup();
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    for (int k = 0; k < 30; k++) {
        af_mjrn_add(&a, from + 2u * (uint32_t)k, 35, 0.08f);
    }
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == 0);

    for (int i = 0; i < 8; i++) {
        uint32_t id = 0;
        CHECK(af_meas_add_journal(&C, "j", &res, from, &id) == 0);
    }
    uint32_t id9 = 0;
    CHECK(af_meas_add_journal(&C, "j9", &res, from, &id9) == 0);
    CHECK(af_meas_find(&C, 1u) == NULL);
    CHECK(af_meas_find(&C, 9u) != NULL);

    t_teardown();
    CHECK(g_alloc_n == g_free_n);
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 60;
    uint32_t live_id = 0;
    CHECK(af_meas_start(&C, &p, T0, 1, 1700000000u, &live_id) == 0);
    CHECK(C.active >= 0);
    for (int i = 0; i < 7; i++) {
        uint32_t id = 0;
        CHECK(af_meas_add_journal(&C, "j", &res, from, &id) == 0);
    }
    uint32_t id10 = 0;
    CHECK(af_meas_add_journal(&C, "j10", &res, from, &id10) == 0);
    CHECK(C.active >= 0);
    CHECK(af_meas_find(&C, live_id) != NULL);
    t_teardown();
    CHECK(g_alloc_n == g_free_n);
}

static void t_summary(void) {
    t_setup();
    uint32_t from = 1700000000u;
    uint32_t to = from + 60u;
    af_mjrn_t a;
    af_mjrn_init(&a, from, to);
    for (int k = 0; k < 30; k++) {
        af_mjrn_add(&a, from + 2u * (uint32_t)k, 35, 0.08f);
    }
    af_meas_res_t res;
    CHECK(af_mjrn_finish(&a, &res) == 0);
    uint32_t id = 0;
    CHECK(af_meas_add_journal(&C, "s", &res, from, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    CHECK(r != NULL);
    if (r == NULL) {
        t_teardown();
        return;
    }
    af_meas_sum_t s;
    af_meas_summary(r, &s);
    CHECK(s.n_valid == 1u);
    CHECK(s.counts == 1050u);
    CHECK(near((float)s.mean_usvh, 0.08f, 1e-6f));
    t_teardown();
    CHECK(g_alloc_n == g_free_n);
}

/* T-57 P2-4: допуск потерь по общему правилу T-56 (valid = npk>=1 && lost<=lost_max, по умолчанию 2) */
static void t_valid_rule(void) {
    for (uint32_t L = 2; L <= 3; L++) {                /* граница допуска: lost == lost_max действителен, +1 нет */
        af_mjrn_t g; af_meas_res_t r2;
        af_mjrn_init(&g, 1700000000u, 1700000060u);
        af_mjrn_add(&g, 1700000000u, 30, 0.08f); g.lost = L; g.last_ts = 1700000058u;
        CHECK(af_mjrn_finish(&g, &r2) == 0);
        CHECK(r2.valid == (L == 2u ? 1 : 0));
    }
    uint32_t from = 1700000000u;
    for (uint32_t gap = 4; gap <= 10; gap += 2) {    /* разрыв gap с (порог AF_MEAS_GAP_MS = 6 с) */
        af_mjrn_t a;
        af_mjrn_init(&a, from, from + 60u);
        uint32_t ts = from;
        for (int k = 0; k < 20; k++) { af_mjrn_add(&a, ts, 30, 0.08f); ts += (k == 9) ? gap : 2u; }
        a.to = ts;                                   /* хвост без потерь */
        af_meas_res_t res;
        CHECK(af_mjrn_finish(&a, &res) == 0);
        uint32_t want_lost = gap > 6u ? (gap * 1000u + 1000u) / 2000u - 1u : 0u;
        CHECK(res.lost == want_lost);
        CHECK(res.valid == (want_lost <= 2u ? 1 : 0));
        CHECK(res.stop_reason == (want_lost <= 2u ? AF_MEAS_SR_TIME : AF_MEAS_SR_ERROR));
    }
}

int main(void) {
    RUN(t_basic);
    RUN(t_valid_rule);
    RUN(t_equiv_live);
    RUN(t_gaps);
    RUN(t_head_gap);
    RUN(t_bounds);
    RUN(t_errors);
    RUN(t_long);
    RUN(t_saturate);
    RUN(t_add_journal);
    RUN(t_slots);
    RUN(t_summary);
    TESTS_DONE();
}
