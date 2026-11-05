#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"
#define UTC0 1792859805u
#define RUN2(fn) do { t_setup(); RUN(fn); t_teardown(); } while (0)
#define R(id) af_meas_find(&C, (id))
#include <string.h>
#include <math.h>

static const uint16_t PUL[5] = {20, 22, 18, 21, 19};
static const float RAT[5] = {0.08f, 0.088f, 0.072f, 0.084f, 0.076f};

static void feed_series(uint64_t base, const uint16_t *pulses, const float *rates, int skip_k) {
    for (int k = 0; k < 5; k++) {
        if (k != skip_k) {
            t_feed(base + 20000ull * k + 1000, base + 20000ull * (k + 1), pulses[k], rates[k]);
        }
    }
}

static void t_summary(void) {
    af_meas_params_t p = t_params();
    p.n = 5;
    p.t_s = 20;
    p.retry_invalid = 0;

    CHECK(af_meas_start(&C, &p, T0, 0, 0, NULL) == 0);
    feed_series(T0, PUL, RAT, -1);
    af_meas_tick(&C, T0 + 100001, 0, 0);

    af_meas_run_t *r = R(1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);
    CHECK(r->state == AF_MEAS_ST_DONE);

    af_meas_sum_t s;
    memset(&s, 0, sizeof(s));
    af_meas_summary(r, &s);

    CHECK(s.rows == 5);
    CHECK(s.n_valid == 5);
    CHECK(s.n_invalid == 0);
    CHECK(s.counts == 1000);
    CHECK(near(s.live_s, 100.0, 1e-9));
    CHECK(near(s.mean_cps, 10.0, 1e-9));
    CHECK(near(s.sd_cps, 0.7905694, 1e-6));
    CHECK(near(s.sem_cps, 0.3535534, 1e-6));
    CHECK(near(s.min_cps, 9.0, 1e-9));
    CHECK(near(s.max_cps, 11.0, 1e-9));
    CHECK(near(s.pooled_cps, 10.0, 1e-9));
    CHECK(near(s.pooled_sigma, 0.3162278, 1e-6));
    CHECK(near(s.chi2_ndf, 1.2544017, 1e-5));
    CHECK(near(s.mean_usvh, 0.08, 1e-6));
    CHECK(near(s.dose_usv, 0.0022222, 1e-7));
}

static void t_summary_invalid(void) {
    af_meas_params_t p = t_params();
    p.n = 5;
    p.t_s = 20;
    p.retry_invalid = 0;

    CHECK(af_meas_start(&C, &p, T0, 0, 0, NULL) == 0);
    feed_series(T0, PUL, RAT, 2);
    af_meas_tick(&C, T0 + 100001, 0, 0);

    af_meas_run_t *r = R(1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);

    af_meas_sum_t s;
    memset(&s, 0, sizeof(s));
    af_meas_summary(r, &s);

    CHECK(s.rows == 5);
    CHECK(s.n_valid == 4);
    CHECK(s.n_invalid == 1);
    CHECK(s.counts == 820);
    CHECK(near(s.live_s, 80.0, 1e-9));
    CHECK(near(s.mean_cps, 10.25, 1e-9));
    CHECK(near(s.pooled_cps, 10.25, 1e-9));
}

static void t_summary_partial(void) {
    // (a) серия
    af_meas_params_t p = t_params();
    p.n = 3;
    p.t_s = 20;
    p.retry_invalid = 0;

    CHECK(af_meas_start(&C, &p, T0, 0, 0, NULL) == 0);
    t_feed(T0 + 1000, T0 + 40000, 20, 0.08f);
    t_feed(T0 + 41000, T0 + 50000, 20, 0.08f);
    CHECK(af_meas_stop(&C, 0, T0 + 50000) == 0);

    af_meas_run_t *r = R(1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);

    af_meas_sum_t s;
    memset(&s, 0, sizeof(s));
    af_meas_summary(r, &s);

    CHECK(s.rows == 3);
    CHECK(s.n_valid == 2);
    CHECK(s.n_invalid == 0);
    CHECK(s.counts == 400);

    // (b) разовое
    af_meas_params_t p2 = t_params();
    p2.n = 1;
    p2.t_s = 100;

    CHECK(af_meas_start(&C, &p2, T0 + 100000, 0, 0, NULL) == 0);
    t_feed(T0 + 101000, T0 + 110000, 40, 0.08f);
    CHECK(af_meas_stop(&C, 0, T0 + 110000) == 0);

    af_meas_run_t *r2 = R(2);
    if (r2 == NULL) { CHECK(r2 != NULL); return; }
    CHECK(r2 != NULL);

    af_meas_sum_t s2;
    memset(&s2, 0, sizeof(s2));
    af_meas_summary(r2, &s2);

    CHECK(s2.rows == 1);
    CHECK(s2.n_valid == 1);
    CHECK(s2.counts == 200);
    CHECK(near(s2.mean_cps, 20.0, 1e-9));
    CHECK(isnan(s2.sd_cps));
    CHECK(isnan(s2.sem_cps));
    CHECK(isnan(s2.chi2_ndf));
}

static void t_summary_empty(void) {
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 20;

    CHECK(af_meas_start(&C, &p, T0, 0, 0, NULL) == 0);

    af_meas_run_t *r = R(1);
    if (r == NULL) { CHECK(r != NULL); return; }
    CHECK(r != NULL);

    af_meas_sum_t s;
    memset(&s, 0, sizeof(s));
    af_meas_summary(r, &s);

    CHECK(s.rows == 0);
    CHECK(s.n_valid == 0);
    CHECK(s.n_invalid == 0);
    CHECK(s.counts == 0);
    CHECK(isnan(s.mean_cps));
    CHECK(isnan(s.sd_cps));
    CHECK(isnan(s.sem_cps));
    CHECK(isnan(s.min_cps));
    CHECK(isnan(s.max_cps));
    CHECK(isnan(s.pooled_cps));
    CHECK(isnan(s.pooled_sigma));
    CHECK(isnan(s.chi2_ndf));
    CHECK(isnan(s.mean_usvh));
    CHECK(isnan(s.dose_usv));

    af_meas_sum_t s_null;
    memset(&s_null, 0, sizeof(s_null));
    af_meas_summary(NULL, &s_null);
    CHECK(s_null.rows == 0);
    CHECK(isnan(s_null.mean_cps));
}

static void t_fmt_utc(void) {
    char o[32];
    CHECK(af_meas_fmt_utc(0, o, 32) == 0);
    CHECK(o[0] == 0);

    CHECK(af_meas_fmt_utc(1792859805u, o, 32) == 20);
    CHECK(strcmp(o, "2026-10-24T16:36:45Z") == 0);

    CHECK(af_meas_fmt_utc(1792859805u, o, 20) == -1);
}

static void t_fmt_csv(void) {
    char b[512];
    int len = af_meas_fmt_csv_header(b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "i,t0_utc,t_s,counts,cps,cps_sigma,usv_h,usv_h_sigma,valid,stop_reason,eps_achieved_pct,attempt,npk,lost_packets,partial,raw_seq0,raw_seq1\n") == 0);
    CHECK(af_meas_fmt_csv_header(b, 10) == -1);

    af_meas_res_t x;
    memset(&x, 0, sizeof(x));
    x.i = 3;
    x.attempt = 2;
    x.valid = 1;
    x.t0_utc = 1792859805u;
    x.t0_mono_s = 1000;
    x.t_ms = 60000;
    x.counts = 1050;
    x.npk = 30;
    x.lost = 0;
    x.cps = 17.5f;
    x.cps_sigma = 0.5f;
    x.usv_h = 0.08f;
    x.usv_h_sigma = NAN;
    x.eps_pct = 3.0f;
    x.stop_reason = AF_MEAS_SR_TIME;
    x.flags = AF_MEAS_F_FINAL;
    x.raw_seq0 = 100;
    x.raw_seq1 = 130;
    x.raw_epoch = 21;

    len = af_meas_fmt_csv_row(&x, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "3,2026-10-24T16:36:45Z,60.000,1050,17.5,0.5,0.0800,0.0024,1,time,3.0,2,30,0,0,100,130\n") == 0);

    af_meas_res_t x2 = x;
    x2.t0_utc = 0;
    x2.flags = AF_MEAS_F_PARTIAL;
    x2.cps = NAN;
    x2.eps_pct = NAN;
    x2.valid = 0;
    x2.stop_reason = AF_MEAS_SR_MANUAL;

    len = af_meas_fmt_csv_row(&x2, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "3,,60.000,1050,,0.5,0.08,,0,manual,,2,30,0,1,100,130\n") == 0);

    CHECK(af_meas_fmt_csv_row(&x, b, 20) == -1);
}

static void t_fmt_res_json(void) {
    af_meas_res_t x;
    memset(&x, 0, sizeof(x));
    x.i = 3;
    x.attempt = 2;
    x.valid = 1;
    x.t0_utc = 1792859805u;
    x.t0_mono_s = 1000;
    x.t_ms = 60000;
    x.counts = 1050;
    x.npk = 30;
    x.lost = 0;
    x.cps = 17.5f;
    x.cps_sigma = 0.5f;
    x.usv_h = 0.08f;
    x.usv_h_sigma = NAN;
    x.eps_pct = 3.0f;
    x.stop_reason = AF_MEAS_SR_TIME;
    x.flags = AF_MEAS_F_FINAL;
    x.raw_seq0 = 100;
    x.raw_seq1 = 130;
    x.raw_epoch = 21;

    char b[512];
    int len = af_meas_fmt_res_json(&x, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "{\"i\":3,\"attempt\":2,\"valid\":true,\"t0_utc\":1792859805,\"t0_mono_s\":1000,\"t_ms\":60000,\"counts\":1050,\"npk\":30,\"lost\":0,\"cps\":17.5,\"cps_sigma\":0.5,\"usv_h\":0.08,\"usv_h_sigma\":null,\"eps_pct\":3,\"stop_reason\":\"time\",\"partial\":false,\"goal_missed\":false,\"time_jump\":false,\"final\":true,\"raw_seq0\":100,\"raw_seq1\":130,\"raw_epoch\":21}") == 0);

    af_meas_res_t x3 = x;
    x3.t0_utc = 0;
    x3.valid = 0;
    x3.flags = AF_MEAS_F_PARTIAL | AF_MEAS_F_TIMEJUMP | AF_MEAS_F_GOAL_MISS;

    len = af_meas_fmt_res_json(&x3, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"valid\":false") != NULL);
    CHECK(strstr(b, "\"t0_utc\":null") != NULL);
    CHECK(strstr(b, "\"partial\":true") != NULL);
    CHECK(strstr(b, "\"goal_missed\":true") != NULL);
    CHECK(strstr(b, "\"time_jump\":true") != NULL);
    CHECK(strstr(b, "\"final\":false") != NULL);
}

static void t_fmt_run_json(void) {
    af_meas_params_t p = t_params();
    p.n = 5;
    p.t_s = 60;
    p.pause_s = 10;
    // "Фон" в UTF-8: \xD0\xA4 \xD0\xBE \xD0\xBD
    char name[] = "\xD0\xA4\xD0\xBE\xD0\xBD";
    memcpy(p.name, name, sizeof(name));

    CHECK(af_meas_start(&C, &p, T0, 1, UTC0, NULL) == 0);

    char b[1024];
    int len = af_meas_fmt_run_json(R(1), b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "{\"id\":1,\"name\":\"Фон\",\"mode\":\"series\",\"state\":\"running\",\"phase\":\"measuring\",\"n\":5,\"done\":0,\"cur_i\":1,\"t_s\":60,\"pause_s\":10,\"retry_invalid\":true,\"lost_max\":2,\"stop_mode\":\"time\",\"eps_pct\":1,\"t_min_s\":10,\"t_max_s\":3600,\"start_mode\":\"now\",\"start_utc\":1792859805,\"start_mono_s\":1000,\"end_mono_s\":null,\"end_reason\":null,\"file_store\":false,\"no_overwrite\":true,\"prefix\":\"AtomFast\",\"name_template\":\"{prefix}_{n:000}\",\"rows\":0,\"rows_total\":0,\"dropped\":0,\"total_s\":350,\"expected_end_utc\":1792860155}") == 0);

    CHECK(af_meas_stop(&C, 0, T0 + 5000) == 0);
    len = af_meas_fmt_run_json(R(1), b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"state\":\"done\"") != NULL);
    CHECK(strstr(b, "\"end_reason\":\"manual\"") != NULL);
    CHECK(strstr(b, "\"cur_i\":null") != NULL);
    CHECK(strstr(b, "\"end_mono_s\":1005") != NULL);
    CHECK(strstr(b, "\"expected_end_utc\":1792860155") != NULL);

    af_meas_params_t p2 = t_params();
    p2.n = 0;
    CHECK(af_meas_start(&C, &p2, T0 + 100000, 0, 0, NULL) == 0);
    len = af_meas_fmt_run_json(R(2), b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"mode\":\"endless\"") != NULL);
    CHECK(strstr(b, "\"total_s\":null") != NULL);
    CHECK(strstr(b, "\"expected_end_utc\":null") != NULL);

    CHECK(af_meas_stop(&C, 0, T0 + 150000) == 0);
    af_meas_params_t p3 = t_params();
    p3.n = 1;
    p3.stop_mode = AF_MEAS_STOP_SIGMA;
    p3.eps_x100 = 100;
    p3.t_min_s = 10;
    p3.t_max_s = 600;
    CHECK(af_meas_start(&C, &p3, T0 + 200000, 0, 0, NULL) == 0);
    len = af_meas_fmt_run_json(R(3), b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"mode\":\"single\"") != NULL);
    CHECK(strstr(b, "\"stop_mode\":\"sigma\"") != NULL);
    CHECK(strstr(b, "\"total_s\":null") != NULL);
}

static void t_fmt_escape(void) {
    char nm[] = {'a', '"', 'b', '\\', 'c', '\x01', '\xD0', '\xA4', 0};
    /* имя с управляющим символом start отвергает (params_check), поэтому прогон собран вручную */
    af_meas_run_t fr;
    memset(&fr, 0, sizeof(fr));
    fr.id = 1;
    memcpy(fr.name, nm, sizeof(nm));

    char b[1024];
    int len = af_meas_fmt_run_json(&fr, b, sizeof(b));
    CHECK(len > 0);

    // Ожидаемая подстрока: "name":"a\"b\\c\u0001Ф"
    char expected[] = "\"name\":\"a\\\"b\\\\c\\u0001\xD0\xA4\"";
    CHECK(strstr(b, expected) != NULL);

    // Граничные размеры
    af_meas_res_t x;
    memset(&x, 0, sizeof(x));
    x.i = 3;
    x.attempt = 2;
    x.valid = 1;
    x.t0_utc = 1792859805u;
    x.t0_mono_s = 1000;
    x.t_ms = 60000;
    x.counts = 1050;
    x.npk = 30;
    x.lost = 0;
    x.cps = 17.5f;
    x.cps_sigma = 0.5f;
    x.usv_h = 0.08f;
    x.usv_h_sigma = NAN;
    x.eps_pct = 3.0f;
    x.stop_reason = AF_MEAS_SR_TIME;
    x.flags = AF_MEAS_F_FINAL;
    x.raw_seq0 = 100;
    x.raw_seq1 = 130;
    x.raw_epoch = 21;

    char big[1024];
    int L = af_meas_fmt_res_json(&x, big, sizeof(big));
    CHECK(L > 0);

    char buf[600];
    for (size_t cap = 0; cap <= (size_t)L + 1; cap++) {
        memset(buf, 'X', sizeof(buf));
        int ret = af_meas_fmt_res_json(&x, buf, cap);
        if (cap <= (size_t)L) {
            CHECK(ret == -1);
            if (cap > 0) {
                // Проверка NUL-терминации в пределах cap
                int found_nul = 0;
                for (size_t i = 0; i < cap; i++) {
                    if (buf[i] == 0) {
                        found_nul = 1;
                        break;
                    }
                }
                CHECK(found_nul == 1);
                // Проверка, что за cap не затронуто
                for (size_t i = cap; i < sizeof(buf); i++) {
                    CHECK(buf[i] == 'X');
                }
            } else {
                CHECK(buf[0] == 'X');
            }
        } else {
            CHECK(ret == L);
        }
    }
}

static void t_fmt_sum_live(void) {
    af_meas_sum_t s;
    memset(&s, 0, sizeof(s));
    s.rows = 5;
    s.n_valid = 5;
    s.n_invalid = 0;
    s.counts = 1000;
    s.live_s = 100.0;
    s.mean_cps = 10.0;
    s.sd_cps = 0.7905694150420949;
    s.sem_cps = 0.3535533905932738;
    s.min_cps = 9.0;
    s.max_cps = 11.0;
    s.pooled_cps = 10.0;
    s.pooled_sigma = 0.31622776601683794;
    s.chi2_ndf = 1.2544017410491635;
    s.mean_usvh = 0.08;
    s.dose_usv = 0.0022222222;

    char b[512];
    int len = af_meas_fmt_sum_json(&s, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "{\"rows\":5,\"n_valid\":5,\"n_invalid\":0,\"counts\":1000,\"live_s\":100,\"mean_cps\":10,\"sd_cps\":0.790569,\"sem_cps\":0.353553,\"min_cps\":9,\"max_cps\":11,\"pooled_cps\":10,\"pooled_sigma\":0.316228,\"chi2_ndf\":1.2544,\"mean_usvh\":0.08,\"dose_usv\":0.00222222}") == 0);

    af_meas_sum_t s_nan;
    memset(&s_nan, 0, sizeof(s_nan));
    s_nan.mean_cps = NAN;
    s_nan.chi2_ndf = NAN;
    s_nan.dose_usv = NAN;
    len = af_meas_fmt_sum_json(&s_nan, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"mean_cps\":null") != NULL);
    CHECK(strstr(b, "\"chi2_ndf\":null") != NULL);
    CHECK(strstr(b, "\"dose_usv\":null") != NULL);

    af_meas_live_t lv;
    memset(&lv, 0, sizeof(lv));
    lv.i = 2;
    lv.attempt = 1;
    lv.phase = AF_MEAS_PH_MEASURING;
    lv.elapsed_ms = 10000;
    lv.remaining_ms = 90000;
    lv.counts = 200;
    lv.npk = 5;
    lv.lost = 0;
    lv.cps = 20.0f;
    lv.cps_sigma = 1.414214f;
    lv.eps_pct = 7.071068f;
    lv.usv_h = 0.1f;
    lv.n_target = 0;
    lv.eta_s = -1;

    len = af_meas_fmt_live_json(&lv, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strcmp(b, "{\"i\":2,\"attempt\":1,\"phase\":\"measuring\",\"elapsed_ms\":10000,\"remaining_ms\":90000,\"counts\":200,\"npk\":5,\"lost\":0,\"cps\":20,\"cps_sigma\":1.41421,\"eps_pct\":7.07107,\"usv_h\":0.1,\"n_target\":null,\"eta_s\":null}") == 0);

    lv.n_target = 10000;
    lv.eta_s = 490;
    len = af_meas_fmt_live_json(&lv, b, sizeof(b));
    CHECK(len > 0);
    CHECK(strstr(b, "\"n_target\":10000") != NULL);
    CHECK(strstr(b, "\"eta_s\":490") != NULL);
}

int main(void) {
    RUN2(t_summary);
    RUN2(t_summary_invalid);
    RUN2(t_summary_partial);
    RUN2(t_summary_empty);
    RUN2(t_fmt_utc);
    RUN2(t_fmt_csv);
    RUN2(t_fmt_res_json);
    RUN2(t_fmt_run_json);
    RUN2(t_fmt_escape);
    RUN2(t_fmt_sum_live);
    TESTS_DONE();
    return 0;
}
