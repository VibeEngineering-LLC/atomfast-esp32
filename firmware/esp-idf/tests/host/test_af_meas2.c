#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"
#define UTC0 1792859805u

static void t_ring_overflow(void) {
    t_setup();
    g_alloc_max = 64 * sizeof(af_meas_res_t);
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 10;
    p.pause_s = 0;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    CHECK(r->res_cap == 64);
    CHECK(r->mode == AF_MEAS_MODE_ENDLESS);

    t_feed(T0 + 1000, T0 + 700000, 10, 0.1f);
    af_meas_tick(&C, T0 + 700001, 0, 0);

    CHECK(r->rows_total == 70);
    CHECK(r->res_n == 64);
    CHECK(r->dropped == 6);
    CHECK(af_meas_res_count(r) == 64);
    CHECK(af_meas_res_at(r, 0) != NULL);
    CHECK(af_meas_res_at(r, 0)->i == 7);
    CHECK(af_meas_res_at(r, 63) != NULL);
    CHECK(af_meas_res_at(r, 63)->i == 70);
    CHECK(af_meas_res_at(r, 64) == NULL);
    CHECK(af_meas_res_at(r, 0)->npk == 5);
    CHECK(af_meas_res_at(r, 0)->counts == 50);
    CHECK(r->state == AF_MEAS_ST_RUN);
    t_teardown();
}

static void t_nomem(void) {
    t_setup();
    g_alloc_fail_all = 1;
    af_meas_params_t p = t_params();
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == AF_MEAS_E_NOMEM);
    CHECK(C.active == -1);
    CHECK(C.runs[0].id == 0);

    g_alloc_fail_all = 0;
    g_alloc_max = 100;
    p.n = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == AF_MEAS_E_NOMEM);
    CHECK(C.active == -1);

    g_alloc_max = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(C.active == 0);
    t_teardown();
}

static void t_time_jump(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 60;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 1, UTC0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    CHECK(r->start_utc == UTC0);
    CHECK(r->cur_t0_utc == UTC0);

    t_feed(T0 + 1000, T0 + 30000, 10, 0.1f);
    af_meas_tick(&C, T0 + 30000, 1, UTC0 + 30 + 3600);
    t_feed(T0 + 31000, T0 + 60000, 10, 0.1f);
    af_meas_tick(&C, T0 + 60001, 1, UTC0 + 60 + 3600);

    CHECK(r->state == AF_MEAS_ST_DONE);
    const af_meas_res_t *row0 = af_meas_res_at(r, 0);
    if (row0 == NULL) { CHECK(row0 != NULL); return; }
    CHECK(row0 != NULL);
    CHECK(row0->valid == 1);
    CHECK(row0->t_ms == 60000);
    CHECK(row0->counts == 300);
    CHECK(row0->npk == 30);
    CHECK(row0->t0_utc == UTC0);
    CHECK((row0->flags & AF_MEAS_F_TIMEJUMP) != 0);
    CHECK((row0->flags & AF_MEAS_F_UTC_OK) != 0);

    uint64_t T1 = T0 + 100000;
    CHECK(af_meas_start(&C, &p, T1, 1, UTC0 + 100, &id) == 0);
    r = af_meas_find(&C, 2);
    CHECK(r != NULL);
    t_feed(T1 + 1000, T1 + 30000, 10, 0.1f);
    af_meas_tick(&C, T1 + 30000, 1, UTC0 + 100 + 30 + 5);
    t_feed(T1 + 31000, T1 + 60000, 10, 0.1f);
    af_meas_tick(&C, T1 + 60001, 1, UTC0 + 100 + 60 + 5);

    row0 = af_meas_res_at(r, 0);
    if (row0 == NULL) { CHECK(row0 != NULL); return; }
    CHECK(row0 != NULL);
    CHECK(row0->valid == 1);
    CHECK(row0->t_ms == 60000);
    CHECK((row0->flags & AF_MEAS_F_TIMEJUMP) == 0);
    t_teardown();
}

static void t_manual_stop(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 100;
    p.retry_invalid = 1;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    t_feed(T0 + 1000, T0 + 10000, 10, 0.1f);
    CHECK(af_meas_stop(&C, 0, T0 + 10000) == 0);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(r->end_reason == AF_MEAS_SR_MANUAL);
    CHECK(C.active == -1);
    CHECK(af_meas_res_count(r) == 1);
    const af_meas_res_t *row0 = af_meas_res_at(r, 0);
    if (row0 == NULL) { CHECK(row0 != NULL); return; }
    CHECK(row0 != NULL);
    CHECK(row0->valid == 1);
    CHECK(row0->stop_reason == AF_MEAS_SR_MANUAL);
    CHECK((row0->flags & AF_MEAS_F_PARTIAL) != 0);
    CHECK((row0->flags & AF_MEAS_F_FINAL) != 0);
    CHECK(row0->counts == 50);
    CHECK(row0->npk == 5);
    CHECK(row0->t_ms == 10000);
    CHECK(r->done_i == 1);

    CHECK(af_meas_stop(&C, 1, T0 + 11000) == AF_MEAS_E_STATE);
    CHECK(af_meas_stop(&C, 0, T0 + 11000) == AF_MEAS_E_STATE);
    CHECK(af_meas_stop(&C, 999, T0 + 11000) == AF_MEAS_E_NOTFOUND);

    CHECK(af_meas_start(&C, &p, T0 + 20000, 0, 0, &id) == 0);
    r = af_meas_find(&C, 2);
    CHECK(r != NULL);
    CHECK(af_meas_stop(&C, 0, T0 + 25000) == 0);
    CHECK(af_meas_res_count(r) == 0);
    CHECK(r->state == AF_MEAS_ST_DONE);

    p.start_mode = AF_MEAS_START_DELAY;
    p.delay_s = 30;
    CHECK(af_meas_start(&C, &p, T0 + 40000, 0, 0, &id) == 0);
    r = af_meas_find(&C, 3);
    CHECK(r != NULL);
    CHECK(r->state == AF_MEAS_ST_WAIT);
    CHECK(af_meas_stop(&C, 3, T0 + 41000) == 0);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(af_meas_res_count(r) == 0);
    t_teardown();
}

static void t_pause_resume(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 20;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    t_feed(T0 + 1000, T0 + 10000, 10, 0.1f);
    CHECK(af_meas_pause(&C, 0, T0 + 10000) == 0);
    CHECK(r->state == AF_MEAS_ST_PAUSED);
    CHECK(r->phase == AF_MEAS_PH_NONE);
    CHECK(C.active >= 0);

    uint32_t cur_counts_before = r->cur_counts;
    af_meas_feed(&C, T0 + 11000, 10, 0.1f);
    CHECK(r->cur_counts == cur_counts_before);

    af_meas_tick(&C, T0 + 30000, 0, 0);
    CHECK(r->state == AF_MEAS_ST_PAUSED);

    CHECK(af_meas_resume(&C, 0, T0 + 30000) == 0);
    CHECK(r->state == AF_MEAS_ST_RUN);
    CHECK(r->t0_ms == T0 + 30000);

    t_feed(T0 + 31000, T0 + 50000, 20, 0.1f);
    af_meas_tick(&C, T0 + 50001, 0, 0);
    CHECK(r->state == AF_MEAS_ST_DONE);
    const af_meas_res_t *row0 = af_meas_res_at(r, 0);
    if (row0 == NULL) { CHECK(row0 != NULL); return; }
    CHECK(row0 != NULL);
    CHECK(row0->counts == 200);
    CHECK(row0->npk == 10);
    CHECK(row0->t0_mono_s == 1030);
    CHECK(row0->valid == 1);

    CHECK(af_meas_pause(&C, 1, T0 + 60000) == AF_MEAS_E_STATE);
    CHECK(af_meas_resume(&C, 1, T0 + 60000) == AF_MEAS_E_STATE);
    t_teardown();
}

static void t_delete_busy(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 10;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == AF_MEAS_E_BUSY);
    CHECK(af_meas_delete(&C, 1) == AF_MEAS_E_STATE);

    af_meas_tick(&C, T0 + 10001, 0, 0);
    CHECK(af_meas_delete(&C, 1) == 0);
    CHECK(g_alloc_n == g_free_n);
    CHECK(af_meas_find(&C, 1) == NULL);
    CHECK(af_meas_delete(&C, 1) == AF_MEAS_E_NOTFOUND);
    CHECK(af_meas_delete(&C, 0) == AF_MEAS_E_NOTFOUND);

    for (int i = 0; i < 9; i++) {
        uint64_t t_start = T0 + 100000ull * (i + 1);
        CHECK(af_meas_start(&C, &p, t_start, 0, 0, &id) == 0);
        af_meas_tick(&C, t_start + 10001, 0, 0);
    }

    CHECK(af_meas_find(&C, 2) == NULL);
    CHECK(af_meas_find(&C, 3) != NULL);
    CHECK(af_meas_find(&C, 10) != NULL);
    CHECK(g_alloc_n - g_free_n == 8);
    t_teardown();
}

static void t_start_modes(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 20;
    p.retry_invalid = 0;
    p.start_mode = AF_MEAS_START_DELAY;
    p.delay_s = 30;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 1, UTC0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    CHECK(r->state == AF_MEAS_ST_WAIT);
    CHECK(r->phase == AF_MEAS_PH_WAITSTART);
    CHECK(r->next_ms == T0 + 30000);
    CHECK(r->start_mono_s == 1030);
    CHECK(r->start_utc == UTC0 + 30);

    af_meas_live_t lv;
    CHECK(af_meas_live(r, T0 + 10000, &lv) == 0);
    CHECK(lv.remaining_ms == 20000);
    CHECK(lv.phase == AF_MEAS_PH_WAITSTART);

    uint32_t cur_counts_before = r->cur_counts;
    af_meas_feed(&C, T0 + 5000, 10, 0.1f);
    CHECK(r->cur_counts == cur_counts_before);

    af_meas_tick(&C, T0 + 30000, 1, UTC0 + 30);
    CHECK(r->state == AF_MEAS_ST_RUN);
    CHECK(r->phase == AF_MEAS_PH_MEASURING);
    CHECK(r->t0_ms == T0 + 30000);

    af_meas_tick(&C, T0 + 50001, 1, UTC0 + 50);
    CHECK(r->state == AF_MEAS_ST_DONE);

    p.start_mode = AF_MEAS_START_AT;
    p.start_at = UTC0 + 100 + 45;
    CHECK(af_meas_start(&C, &p, T0 + 100000, 1, UTC0 + 100, &id) == 0);
    r = af_meas_find(&C, 2);
    CHECK(r != NULL);
    CHECK(r->state == AF_MEAS_ST_WAIT);
    CHECK(r->next_ms == T0 + 100000 + 45000);
    CHECK(r->start_utc == UTC0 + 145);
    t_teardown();
}

static void t_live(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 100;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    t_feed(T0 + 1000, T0 + 10000, 40, 0.1f);

    af_meas_live_t lv;
    CHECK(af_meas_live(r, T0 + 10000, &lv) == 0);
    CHECK(lv.i == 1);
    CHECK(lv.attempt == 1);
    CHECK(lv.phase == AF_MEAS_PH_MEASURING);
    CHECK(lv.elapsed_ms == 10000);
    CHECK(lv.remaining_ms == 90000);
    CHECK(lv.counts == 200);
    CHECK(lv.npk == 5);
    CHECK(near(lv.cps, 20.0, 1e-4));
    CHECK(near(lv.eps_pct, 7.071068, 1e-4));
    CHECK(lv.n_target == 0);
    CHECK(lv.eta_s == -1);

    CHECK(af_meas_stop(&C, 0, T0 + 10000) == 0);

    af_meas_params_t p2 = t_params();
    p2.n = 1;
    p2.stop_mode = AF_MEAS_STOP_SIGMA;
    p2.eps_x100 = 100;
    p2.t_min_s = 10;
    p2.t_max_s = 600;
    CHECK(af_meas_start(&C, &p2, T0 + 20000, 0, 0, &id) == 0);
    r = af_meas_find(&C, 2);
    CHECK(r != NULL);
    t_feed(T0 + 21000, T0 + 30000, 40, 0.1f);

    CHECK(af_meas_live(r, T0 + 30000, &lv) == 0);
    CHECK(lv.elapsed_ms == 10000);
    CHECK(lv.remaining_ms == 590000);
    CHECK(lv.counts == 200);
    CHECK(lv.n_target == 10000);
    CHECK(lv.eta_s == 490);

    CHECK(af_meas_stop(&C, 0, T0 + 31000) == 0);
    CHECK(af_meas_live(r, T0 + 32000, &lv) == -1);
    t_teardown();
}

static void t_seq_binding(void) {
    t_setup_seq();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 10;
    p.retry_invalid = 0;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    af_meas_tick(&C, T0 + 10001, 0, 0);

    const af_meas_res_t *row0 = af_meas_res_at(r, 0);
    if (row0 == NULL) { CHECK(row0 != NULL); return; }
    CHECK(row0 != NULL);
    CHECK(row0->raw_seq0 == 1);
    CHECK(row0->raw_seq1 == 2);
    CHECK(row0->raw_epoch == 7);
    t_teardown();
}

static void t_mode(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, 1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    CHECK(r->mode == AF_MEAS_MODE_SINGLE);
    CHECK(af_meas_stop(&C, 0, T0) == 0);

    p.n = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    r = af_meas_find(&C, 2);
    CHECK(r != NULL);
    CHECK(r->mode == AF_MEAS_MODE_ENDLESS);
    CHECK(af_meas_stop(&C, 0, T0) == 0);

    p.n = 5;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    r = af_meas_find(&C, 3);
    CHECK(r != NULL);
    CHECK(r->mode == AF_MEAS_MODE_SERIES);
    t_teardown();
}

int main(void) {
    RUN(t_ring_overflow);
    RUN(t_nomem);
    RUN(t_time_jump);
    RUN(t_manual_stop);
    RUN(t_pause_resume);
    RUN(t_delete_busy);
    RUN(t_start_modes);
    RUN(t_live);
    RUN(t_seq_binding);
    RUN(t_mode);
    TESTS_DONE();
    return 0;
}
