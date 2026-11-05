#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"

static void t_window_time(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 60;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r->mode == AF_MEAS_MODE_SINGLE);
    CHECK(r->state == AF_MEAS_ST_RUN);
    CHECK(r->phase == AF_MEAS_PH_MEASURING);

    t_feed(T0 + 1000, T0 + 60000, 35, 0.08f);
    af_meas_tick(&C, T0 + 60001, 0, 0);

    CHECK(C.active == -1);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(r->end_reason == AF_MEAS_SR_TIME);
    CHECK(r->end_mono_s == 1060);
    CHECK(r->done_i == 1);
    CHECK(af_meas_res_count(r) == 1);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->i == 1);
    CHECK(rec->attempt == 1);
    CHECK(rec->valid == 1);
    CHECK(rec->stop_reason == AF_MEAS_SR_TIME);
    CHECK(rec->t_ms == 60000);
    CHECK(rec->counts == 1050);
    CHECK(rec->npk == 30);
    CHECK(rec->lost == 0);
    CHECK((rec->flags & AF_MEAS_F_FINAL) != 0);
    CHECK((rec->flags & AF_MEAS_F_PARTIAL) == 0);
    CHECK(rec->t0_mono_s == 1000);
    CHECK(near(rec->cps, 17.5f, 1e-4f));
    CHECK(near(rec->usv_h, 0.08f, 1e-6f));

    af_meas_feed(&C, T0 + 61000, 35, 0.08f);
    CHECK(af_meas_res_count(r) == 1);
    t_teardown();
}

static void t_window_boundary(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 3;
    p.t_s = 10;
    p.pause_s = 0;
    p.retry_invalid = 0;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    for (int k = 0; k <= 15; k++) {
        af_meas_feed(&C, T0 + 2000 * k, (uint16_t)(k + 1), 0.1f);
    }

    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(C.active == -1);
    CHECK(r->done_i == 3);
    CHECK(af_meas_res_count(r) == 3);

    const af_meas_res_t *rec0 = t_row(id, 0);
    if (rec0 == NULL) { CHECK(rec0 != NULL); return; }
    const af_meas_res_t *rec1 = t_row(id, 1);
    if (rec1 == NULL) { CHECK(rec1 != NULL); return; }
    const af_meas_res_t *rec2 = t_row(id, 2);
    if (rec2 == NULL) { CHECK(rec2 != NULL); return; }

    CHECK(rec0->counts == 15);
    CHECK(rec1->counts == 40);
    CHECK(rec2->counts == 65);

    CHECK(rec0->npk == 5);
    CHECK(rec1->npk == 5);
    CHECK(rec2->npk == 5);

    CHECK(rec0->valid == 1);
    CHECK(rec1->valid == 1);
    CHECK(rec2->valid == 1);

    CHECK(rec0->t_ms == 10000);
    CHECK(rec1->t_ms == 10000);
    CHECK(rec2->t_ms == 10000);

    CHECK(rec0->t0_mono_s == 1000);
    CHECK(rec1->t0_mono_s == 1010);
    CHECK(rec2->t0_mono_s == 1020);

    CHECK(rec0->counts + rec1->counts + rec2->counts == 120);
    t_teardown();
}

static void t_pause_between(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 2;
    p.t_s = 10;
    p.pause_s = 6;
    p.retry_invalid = 0;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    t_feed(T0 + 1000, T0 + 10000, 10, 0.1f);
    af_meas_feed(&C, T0 + 11000, 10, 0.1f);
    af_meas_feed(&C, T0 + 13000, 10, 0.1f);

    CHECK(r->phase == AF_MEAS_PH_GAP);
    CHECK(r->next_ms == T0 + 16000);

    af_meas_feed(&C, T0 + 15000, 10, 0.1f);
    t_feed(T0 + 17000, T0 + 26000, 10, 0.1f);
    af_meas_tick(&C, T0 + 26001, 0, 0);

    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(af_meas_res_count(r) == 2);

    const af_meas_res_t *rec0 = t_row(id, 0);
    if (rec0 == NULL) { CHECK(rec0 != NULL); return; }
    const af_meas_res_t *rec1 = t_row(id, 1);
    if (rec1 == NULL) { CHECK(rec1 != NULL); return; }

    CHECK(rec0->counts == 50);
    CHECK(rec1->counts == 50);
    CHECK(rec0->npk == 5);
    CHECK(rec1->npk == 5);
    CHECK(rec0->lost == 0);
    CHECK(rec1->lost == 0);
    CHECK(rec0->t0_mono_s == 1000);
    CHECK(rec1->t0_mono_s == 1016);
    CHECK(r->done_i == 2);
    t_teardown();
}

static void t_sigma_stop(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.stop_mode = AF_MEAS_STOP_SIGMA;
    p.eps_x100 = 100;
    p.t_min_s = 10;
    p.t_max_s = 600;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    for (int k = 0; k < 99; k++) {
        af_meas_feed(&C, T0 + 1000 + 2000 * k, 100, 0.1f);
    }
    CHECK(r->state == AF_MEAS_ST_RUN);

    af_meas_feed(&C, T0 + 1000 + 2000 * 99, 100, 0.1f);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(af_meas_res_count(r) == 1);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->stop_reason == AF_MEAS_SR_SIGMA);
    CHECK(rec->counts == 10000);
    CHECK(rec->npk == 100);
    CHECK(rec->t_ms == 199000);
    CHECK(rec->valid == 1);
    CHECK((rec->flags & AF_MEAS_F_GOAL_MISS) == 0);
    CHECK(near(rec->eps_pct, 1.0f, 1e-4f));
    CHECK(near(rec->cps, 50.0f, 1e-3f));
    CHECK(r->end_reason == AF_MEAS_SR_SIGMA);
    t_teardown();
}

static void t_sigma_tmin(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.stop_mode = AF_MEAS_STOP_SIGMA;
    p.eps_x100 = 100;
    p.t_min_s = 300;
    p.t_max_s = 600;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    for (int k = 0; k < 150; k++) {
        af_meas_feed(&C, T0 + 1000 + 2000 * k, 100, 0.1f);
    }
    CHECK(r->state == AF_MEAS_ST_RUN);

    af_meas_feed(&C, T0 + 1000 + 2000 * 150, 100, 0.1f);
    CHECK(r->state == AF_MEAS_ST_DONE);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->stop_reason == AF_MEAS_SR_SIGMA);
    CHECK(rec->npk == 151);
    CHECK(rec->counts == 15100);
    CHECK(rec->t_ms == 301000);
    t_teardown();
}

static void t_sigma_limit(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.stop_mode = AF_MEAS_STOP_SIGMA;
    p.eps_x100 = 100;
    p.t_min_s = 10;
    p.t_max_s = 100;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    t_feed(T0 + 1000, T0 + 100000, 10, 0.1f);
    CHECK(r->state == AF_MEAS_ST_RUN);

    af_meas_tick(&C, T0 + 100001, 0, 0);
    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(r->end_reason == AF_MEAS_SR_LIMIT);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->stop_reason == AF_MEAS_SR_LIMIT);
    CHECK((rec->flags & AF_MEAS_F_GOAL_MISS) != 0);
    CHECK(rec->valid == 1);
    CHECK(rec->counts == 500);
    CHECK(rec->npk == 50);
    CHECK(rec->t_ms == 100000);
    CHECK(near(rec->eps_pct, 4.472136f, 1e-4f));
    t_teardown();
}

static void t_sigma_change(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.stop_mode = AF_MEAS_STOP_SIGMA;
    p.eps_x100 = 100;
    p.t_min_s = 10;
    p.t_max_s = 600;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    for (int k = 0; k < 50; k++) {
        af_meas_feed(&C, T0 + 1000 + 2000 * k, 10, 0.1f);
    }
    for (int k = 50; k < 97; k++) {
        af_meas_feed(&C, T0 + 1000 + 2000 * k, 200, 0.1f);
    }
    CHECK(r->state == AF_MEAS_ST_RUN);

    af_meas_feed(&C, T0 + 1000 + 2000 * 97, 200, 0.1f);
    CHECK(r->state == AF_MEAS_ST_DONE);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->stop_reason == AF_MEAS_SR_SIGMA);
    CHECK(rec->counts == 10100);
    CHECK(rec->npk == 98);
    CHECK(rec->t_ms == 195000);
    CHECK(near(rec->cps, 51.5306f, 1e-3f));
    t_teardown();
}

static void t_retry(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 2;
    p.t_s = 20;
    p.pause_s = 0;
    p.retry_invalid = 1;
    p.lost_max = 2;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    t_feed(T0 + 1000, T0 + 6000, 10, 0.1f);
    af_meas_tick(&C, T0 + 11999, 0, 0);
    CHECK(af_meas_res_count(r) == 0);

    af_meas_tick(&C, T0 + 12000, 0, 0);
    CHECK(af_meas_res_count(r) == 1);
    const af_meas_res_t *rec0 = t_row(id, 0);
    if (rec0 == NULL) { CHECK(rec0 != NULL); return; }
    CHECK(rec0->i == 1);
    CHECK(rec0->attempt == 1);
    CHECK(rec0->valid == 0);
    CHECK(rec0->stop_reason == AF_MEAS_SR_ERROR);
    CHECK((rec0->flags & AF_MEAS_F_FINAL) == 0);
    CHECK(rec0->npk == 3);
    CHECK(rec0->counts == 30);
    CHECK(rec0->lost == 3);
    CHECK(rec0->t_ms == 12000);
    CHECK(r->cur_attempt == 2);
    CHECK(r->cur_i == 1);
    CHECK(r->phase == AF_MEAS_PH_MEASURING);

    t_feed(T0 + 13000, T0 + 32000, 20, 0.1f);
    t_feed(T0 + 33000, T0 + 52000, 20, 0.1f);
    af_meas_tick(&C, T0 + 52001, 0, 0);

    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(af_meas_res_count(r) == 3);

    const af_meas_res_t *rec1 = t_row(id, 1);
    if (rec1 == NULL) { CHECK(rec1 != NULL); return; }
    CHECK(rec1->i == 1);
    CHECK(rec1->attempt == 2);
    CHECK(rec1->valid == 1);
    CHECK(rec1->counts == 200);
    CHECK(rec1->npk == 10);
    CHECK(rec1->t0_mono_s == 1012);
    CHECK(rec1->t_ms == 20000);
    CHECK((rec1->flags & AF_MEAS_F_FINAL) != 0);

    const af_meas_res_t *rec2 = t_row(id, 2);
    if (rec2 == NULL) { CHECK(rec2 != NULL); return; }
    CHECK(rec2->i == 2);
    CHECK(rec2->attempt == 1);
    CHECK(rec2->valid == 1);
    CHECK(rec2->counts == 200);
    CHECK((rec2->flags & AF_MEAS_F_FINAL) != 0);

    CHECK(r->done_i == 2);

    int found = 0;
    for (int k = 0; k < g_log_n; k++) {
        if (g_log_lv[k] == 2 && strstr(g_log_msg[k], "invalid") != NULL) {
            found = 1;
            break;
        }
    }
    CHECK(found == 1);
    t_teardown();
}

static void t_retry_limit(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 2;
    p.t_s = 20;
    p.retry_invalid = 1;
    p.lost_max = 1;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    /* по одному пакету в окне: окна не пустые (пустые окна обрыва связи схлопываются), но потери выше порога */
    af_meas_feed(&C, T0 + 1000, 10, 0.1f);
    af_meas_feed(&C, T0 + 21000, 10, 0.1f);
    af_meas_feed(&C, T0 + 41000, 10, 0.1f);
    af_meas_tick(&C, T0 + 60001, 0, 0);

    CHECK(af_meas_res_count(r) == 3);
    const af_meas_res_t *rec0 = t_row(id, 0);
    if (rec0 == NULL) { CHECK(rec0 != NULL); return; }
    const af_meas_res_t *rec1 = t_row(id, 1);
    if (rec1 == NULL) { CHECK(rec1 != NULL); return; }
    const af_meas_res_t *rec2 = t_row(id, 2);
    if (rec2 == NULL) { CHECK(rec2 != NULL); return; }

    CHECK(rec0->i == 1);
    CHECK(rec1->i == 1);
    CHECK(rec2->i == 1);

    CHECK(rec0->valid == 0);
    CHECK(rec1->valid == 0);
    CHECK(rec2->valid == 0);

    CHECK(rec0->npk == 1);
    CHECK(rec1->npk == 1);
    CHECK(rec2->npk == 1);

    CHECK(rec0->attempt == 1);
    CHECK(rec1->attempt == 2);
    CHECK(rec2->attempt == 3);

    CHECK((rec0->flags & AF_MEAS_F_FINAL) == 0);
    CHECK((rec1->flags & AF_MEAS_F_FINAL) == 0);
    CHECK((rec2->flags & AF_MEAS_F_FINAL) != 0);

    CHECK(r->done_i == 1);
    CHECK(r->cur_i == 2);
    CHECK(r->cur_attempt == 1);
    CHECK(r->state == AF_MEAS_ST_RUN);

    CHECK(rec0->lost == 9);
    CHECK(rec1->lost == 9);
    CHECK(rec2->lost == 9);
    t_teardown();
}

static void t_retry_off(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 2;
    p.t_s = 20;
    p.retry_invalid = 0;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    af_meas_feed(&C, T0 + 1000, 10, 0.1f);
    af_meas_feed(&C, T0 + 21000, 10, 0.1f);
    af_meas_tick(&C, T0 + 40001, 0, 0);

    CHECK(r->state == AF_MEAS_ST_DONE);
    CHECK(af_meas_res_count(r) == 2);

    const af_meas_res_t *rec0 = t_row(id, 0);
    if (rec0 == NULL) { CHECK(rec0 != NULL); return; }
    const af_meas_res_t *rec1 = t_row(id, 1);
    if (rec1 == NULL) { CHECK(rec1 != NULL); return; }

    CHECK(rec0->i == 1);
    CHECK(rec1->i == 2);

    CHECK(rec0->attempt == 1);
    CHECK(rec1->attempt == 1);

    CHECK(rec0->valid == 0);
    CHECK(rec1->valid == 0);

    CHECK((rec0->flags & AF_MEAS_F_FINAL) != 0);
    CHECK((rec1->flags & AF_MEAS_F_FINAL) != 0);

    CHECK(r->done_i == 2);
    t_teardown();
}

static void t_loss_threshold(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 30;
    p.lost_max = 2;
    p.retry_invalid = 1;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    af_meas_feed(&C, T0 + 1000, 10, 0.1f);
    af_meas_feed(&C, T0 + 3000, 10, 0.1f);
    af_meas_feed(&C, T0 + 5000, 10, 0.1f);
    af_meas_feed(&C, T0 + 7000, 10, 0.1f);
    af_meas_feed(&C, T0 + 13800, 10, 0.1f);
    t_feed(T0 + 15800, T0 + 29000, 10, 0.1f);

    af_meas_tick(&C, T0 + 30001, 0, 0);

    CHECK(r->state == AF_MEAS_ST_DONE);
    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->valid == 1);
    CHECK(rec->lost == 2);
    CHECK(rec->npk == 12);
    CHECK(rec->counts == 120);
    CHECK(rec->stop_reason == AF_MEAS_SR_TIME);
    CHECK(near(rec->cps, 5.0f, 1e-6f));
    t_teardown();
}

static void t_loss_invalid(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 30;
    p.lost_max = 2;
    p.retry_invalid = 1;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    af_meas_run_t *r = af_meas_find(&C, id);
    if (r == NULL) { CHECK(r != NULL); return; }

    af_meas_feed(&C, T0 + 1000, 10, 0.1f);
    af_meas_feed(&C, T0 + 3000, 10, 0.1f);
    af_meas_feed(&C, T0 + 5000, 10, 0.1f);
    af_meas_feed(&C, T0 + 7000, 10, 0.1f);
    af_meas_feed(&C, T0 + 14000, 10, 0.1f);

    CHECK(af_meas_res_count(r) == 1);
    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->valid == 0);
    CHECK(rec->stop_reason == AF_MEAS_SR_ERROR);
    CHECK(rec->lost == 3);
    CHECK(rec->npk == 4);
    CHECK(rec->t_ms == 14000);
    CHECK((rec->flags & AF_MEAS_F_FINAL) == 0);
    CHECK(r->cur_attempt == 2);
    CHECK(r->phase == AF_MEAS_PH_MEASURING);
    CHECK(r->state == AF_MEAS_ST_RUN);
    t_teardown();
}

static void t_zero_pulses(void) {
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 20;
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);

    t_feed(T0 + 1000, T0 + 20000, 0, 0.0f);
    af_meas_tick(&C, T0 + 20001, 0, 0);

    const af_meas_res_t *rec = t_row(id, 0);
    if (rec == NULL) { CHECK(rec != NULL); return; }
    CHECK(rec->valid == 1);
    CHECK(rec->counts == 0);
    CHECK(rec->npk == 10);
    CHECK(near(rec->cps, 0.0f, 1e-9f));
    CHECK(near(rec->cps_sigma, 0.05f, 1e-6f));
    CHECK(isnan(rec->usv_h_sigma));
    CHECK(isnan(rec->eps_pct));
    CHECK(near(rec->usv_h, 0.0f, 1e-9f));
    t_teardown();
}

int main(void) {
    RUN(t_window_time);
    RUN(t_window_boundary);
    RUN(t_pause_between);
    RUN(t_sigma_stop);
    RUN(t_sigma_tmin);
    RUN(t_sigma_limit);
    RUN(t_sigma_change);
    RUN(t_retry);
    RUN(t_retry_limit);
    RUN(t_retry_off);
    RUN(t_loss_threshold);
    RUN(t_loss_invalid);
    RUN(t_zero_pulses);
    TESTS_DONE();
    return 0;
}
