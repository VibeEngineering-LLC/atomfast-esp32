#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RUN2(fn) do { t_setup(); RUN(fn); t_teardown(); } while (0)

static uint32_t TS[11000];
static uint16_t PU[11000];
static float RT[11000];
static int NP;
static uint64_t RX[11000];

static int load_data(void) {
    FILE *f = fopen("data/e16s_raw_d1.csv", "r");
    if (!f) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    NP = 0;
    while (NP < 11000 && fgets(line, sizeof(line), f)) {
        unsigned int ts;
        unsigned short pu;
        float rt;
        if (sscanf(line, "%u,%hu,%f", &ts, &pu, &rt) == 3) {
            TS[NP] = ts;
            PU[NP] = pu;
            RT[NP] = rt;
            NP++;
        }
    }
    fclose(f);
    if (NP == 0) return 0;
    for (int i = 0; i < NP; i++) {
        RX[i] = T0 + (uint64_t)(TS[i] - TS[0]) * 1000;
    }
    return 1;
}

typedef struct {
    uint32_t counts, npk, lost, t_ms, i, attempt;
    uint64_t t0;
    int valid;
} ref_row_t;

static int ref_series(uint64_t t0, uint32_t t_ms, uint32_t pause_ms, uint32_t lost_max, int retry, ref_row_t *out, int max_rows) {
    uint64_t start = t0;
    uint64_t prev = t0;
    uint32_t lost = 0;
    uint32_t cnt = 0;
    uint32_t npk = 0;
    uint32_t i = 1;
    uint32_t attempt = 1;
    int nref = 0;
    int dead_prev = 0;

    for (int k = 0; k < NP; k++) {
        uint64_t rx = RX[k];
        
        while (rx >= start + t_ms) {
            uint64_t end = start + t_ms;
            uint64_t tail = end > prev ? end - prev : 0;
            uint32_t add_lost = 0;
            if (tail > 6000) add_lost = (tail + 1000) / 2000 - 1;
            lost += add_lost;
            
            if (!(npk == 0 && dead_prev)) {   /* продолжение обрыва: пустое окно подряд не пишется */
            dead_prev = (npk == 0);
            int valid = (npk >= 1 && lost <= lost_max);
            uint32_t t_ms_val = (end >= start) ? (uint32_t)(end - start) : 0;
            
            if (nref < max_rows) {
                out[nref].counts = cnt;
                out[nref].npk = npk;
                out[nref].lost = lost;
                out[nref].t_ms = t_ms_val;
                out[nref].i = i;
                out[nref].attempt = attempt;
                out[nref].t0 = start;
                out[nref].valid = valid;
                nref++;
            }
            
            if (!valid && retry && attempt < 3) {
                attempt++;
            } else {
                i++;
                attempt = 1;
            }
            }
            
            start = end + pause_ms;
            prev = start;
            lost = 0;
            cnt = 0;
            npk = 0;
            
            if (nref >= max_rows) return nref;
        }
        
        if (rx < start) {
            continue;
        }
        
        uint64_t gap = rx - prev;
        uint32_t missed = gap > 6000 ? (gap + 1000) / 2000 - 1 : 0;
        
        if (lost + missed > lost_max) {
            lost += missed;
            prev = rx;
            
            uint64_t end = rx;
            uint64_t tail = end > prev ? end - prev : 0;
            uint32_t add_lost = 0;
            if (tail > 6000) add_lost = (tail + 1000) / 2000 - 1;
            lost += add_lost;
            
            if (!(npk == 0 && dead_prev)) {   /* продолжение обрыва: пустое окно подряд не пишется */
            dead_prev = (npk == 0);
            int valid = (npk >= 1 && lost <= lost_max);
            uint32_t t_ms_val = (end >= start) ? (uint32_t)(end - start) : 0;
            
            if (nref < max_rows) {
                out[nref].counts = cnt;
                out[nref].npk = npk;
                out[nref].lost = lost;
                out[nref].t_ms = t_ms_val;
                out[nref].i = i;
                out[nref].attempt = attempt;
                out[nref].t0 = start;
                out[nref].valid = valid;
                nref++;
            }
            
            if (!valid && retry && attempt < 3) {
                attempt++;
            } else {
                i++;
                attempt = 1;
            }
            }
            
            start = end + pause_ms;
            prev = start;
            lost = 0;
            cnt = 0;
            npk = 0;
            
            if (nref >= max_rows) return nref;
        } else {
            lost += missed;
            cnt += PU[k];
            npk++;
            prev = rx;
        }
    }
    return nref;
}

static int cmp_rows(const char *scen, uint32_t id, const ref_row_t *ref, int nref, int retry) {
    af_meas_run_t *res = af_meas_find(&C, id);
    if (!res) return -1;
    int nres = af_meas_res_count(res);
    if (nres != nref) {
        CHECK(0);
        return 1;
    }
    
    int mismatches = 0;
    for (int k = 0; k < nref; k++) {
        const af_meas_res_t *row = t_row(id, k);
        if (!row) {
            CHECK(0);
            mismatches++;
            continue;
        }
        
        int ok = 1;
        if (row->counts != ref[k].counts) ok = 0;
        if (row->npk != ref[k].npk) ok = 0;
        if (row->lost != ref[k].lost) ok = 0;
        if (row->t_ms != ref[k].t_ms) ok = 0;
        if (row->i != ref[k].i) ok = 0;
        if (row->attempt != ref[k].attempt) ok = 0;
        if (row->valid != ref[k].valid) ok = 0;
        
        uint32_t t0_mono_s = (uint32_t)(ref[k].t0 / 1000);
        if (row->t0_mono_s != t0_mono_s) ok = 0;
        
        int expected_final = (ref[k].valid || ref[k].attempt >= 3 || retry == 0) ? 1 : 0;
        int actual_final = (row->flags & AF_MEAS_F_FINAL) ? 1 : 0;
        if (expected_final != actual_final) ok = 0;
        
        if (!ok) {
            mismatches++;
            if (mismatches <= 5) {
                CHECK(0);
            }
        }
        
        if (getenv("AF_PROBE")) {
            printf("DATA %s %d %u %u %u %u %u %d %u %u\n", scen, k, row->i, row->attempt, row->counts, row->npk, row->lost, row->valid, row->t_ms, row->t0_mono_s);
        }
    }
    return mismatches;
}

static void feed_all(void) {
    for (int k = 0; k < NP; k++) {
        af_meas_feed(&C, RX[k], PU[k], RT[k]);
    }
}

static void t_real_boundaries(void) {
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.pause_s = 0;
    p.retry_invalid = 0;
    p.lost_max = 60;
    
    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    
    feed_all();
    
    ref_row_t *ref = malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    int nref = ref_series(T0, 60000, 0, 60, 0, ref, 2000);
    
    CHECK(nref >= 300);
    CHECK(cmp_rows("bound", 1, ref, nref, 0) == 0);
    
    int all_valid = 1;
    for (int k = 0; k < nref; k++) {
        if (!ref[k].valid) all_valid = 0;
    }
    CHECK(all_valid);
    
    af_meas_run_t *res = af_meas_find(&C, 1);
    CHECK(res != NULL);
    CHECK(res->dropped == 0);
    
    uint64_t sum_counts = 0;
    for (int k = 0; k < nref; k++) {
        sum_counts += ref[k].counts;
    }
    
    uint64_t sum_pu = 0;
    uint64_t limit = T0 + 60000ull * nref;
    for (int k = 0; k < NP; k++) {
        if (RX[k] < limit) {
            sum_pu += PU[k];
        }
    }
    
    CHECK(sum_counts == sum_pu);
    
    printf("INFO real_boundaries windows=%d packets=%d\n", nref, NP);
    free(ref);
}

static void t_real_loss(void) {
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.pause_s = 0;
    p.retry_invalid = 0;
    p.lost_max = 2;
    
    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    
    feed_all();
    
    ref_row_t *ref = malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    int nref = ref_series(T0, 60000, 0, 2, 0, ref, 2000);
    
    CHECK(cmp_rows("loss", 1, ref, nref, 0) == 0);
    
    int invalid_ref = 0;
    for (int k = 0; k < nref; k++) {
        if (!ref[k].valid) invalid_ref++;
    }
    
    int invalid_res = 0;
    af_meas_run_t *res = af_meas_find(&C, 1);
    int nres = af_meas_res_count(res);
    for (int k = 0; k < nres; k++) {
        const af_meas_res_t *row = t_row(1, k);
        if (!row->valid) invalid_res++;
    }
    
    CHECK(invalid_ref == invalid_res);
    CHECK(invalid_ref >= 1);
    
    printf("INFO real_loss windows=%d invalid=%d\n", nref, invalid_ref);
    free(ref);
}

static void t_real_pause(void) {
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 30;
    p.pause_s = 10;
    p.retry_invalid = 0;
    p.lost_max = 60;
    
    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    
    feed_all();
    
    ref_row_t *ref = malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    int nref = ref_series(T0, 30000, 10000, 60, 0, ref, 2000);
    
    CHECK(cmp_rows("pause", 1, ref, nref, 0) == 0);
    CHECK(nref >= 400);
    
    free(ref);
}

static void t_real_retry(void) {
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.pause_s = 0;
    p.retry_invalid = 1;
    p.lost_max = 2;
    
    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    
    feed_all();
    
    ref_row_t *ref = malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    int nref = ref_series(T0, 60000, 0, 2, 1, ref, 2000);
    
    CHECK(cmp_rows("retry", 1, ref, nref, 1) == 0);
    
    int has_attempt2 = 0;
    for (int k = 0; k < nref; k++) {
        if (ref[k].attempt == 2) {
            has_attempt2 = 1;
            break;
        }
    }
    CHECK(has_attempt2);
    
    free(ref);
}

static void t_real_sigma(void) {
    int skipped = 0;
    int trials = 22;
    
    for (int s = 0; s < 11000; s += 500) {
        t_setup();
        
        af_meas_params_t p = t_params();
        p.n = 1;
        p.stop_mode = AF_MEAS_STOP_SIGMA;
        p.eps_x100 = 500;
        p.t_min_s = 60;
        p.t_max_s = 600;
        
        uint32_t id;
        CHECK(af_meas_start(&C, &p, RX[s], 0, 0, &id) == 0);
        CHECK(id == 1);
        
        uint64_t t0 = RX[s];
        uint32_t cum = 0;
        uint32_t npk = 0;
        int stop_reason = 0;
        uint64_t stop_time = 0;
        int skip = 0;
        
        for (int k = s; k < NP; k++) {
            uint64_t rx = RX[k];
            uint64_t dt = rx - t0;
            
            if (dt >= 600000) {
                stop_reason = AF_MEAS_SR_LIMIT;
                stop_time = t0 + 600000;
                break;
            }
            
            if (k > s) {
                uint64_t gap = RX[k] - RX[k-1];
                if (gap > 6000) {
                    skip = 1;
                    break;
                }
            } else {
                if (RX[k] - t0 > 6000) {
                    skip = 1;
                    break;
                }
            }
            
            cum += PU[k];
            npk++;
            
            if (dt >= 60000 && cum >= 400) {
                stop_reason = AF_MEAS_SR_SIGMA;
                stop_time = rx;
                break;
            }
        }
        
        if (skip) {
            skipped++;
            if (getenv("AF_PROBE")) {
                printf("DATA sigma %d 0 0 0 0 0\n", s);
            }
            t_teardown();
            continue;
        }
        
        for (int k = s; k < NP; k++) {   /* подача потока в автомат до его самостоятельной остановки */
            af_meas_feed(&C, RX[k], PU[k], RT[k]);
            if (af_meas_find(&C, 1)->state == AF_MEAS_ST_DONE) break;
        }
        af_meas_run_t *res = af_meas_find(&C, 1);
        CHECK(res != NULL);
        CHECK(t_row(1, 0) != NULL && t_row(1, 0)->stop_reason == stop_reason);
        
        const af_meas_res_t *row = t_row(1, 0);
        CHECK(row != NULL);
        if (row == NULL) { t_teardown(); continue; }
        
        if (getenv("AF_PROBE")) {
            printf("DATA sigma %d %u %u %u %d %d\n", s, row->counts, row->npk, row->t_ms, row->stop_reason, row->valid);
        }
        
        CHECK(row->counts == cum);
        CHECK(row->npk == npk);
        CHECK(row->t_ms == (uint32_t)(stop_time - t0));
        CHECK(row->valid == 1);
        
        t_teardown();
    }
    
    printf("INFO real_sigma trials=%d skipped=%d\n", trials, skipped);
    CHECK(trials - skipped >= 5);
}

static void t_real_summary(void) {
    af_meas_params_t p = t_params();
    p.n = 50;
    p.t_s = 60;
    p.retry_invalid = 0;
    p.lost_max = 60;
    
    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);
    CHECK(id == 1);
    
    feed_all();
    
    ref_row_t *ref = malloc(50 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    int nref = ref_series(T0, 60000, 0, 60, 0, ref, 50);
    
    CHECK(nref == 50);
    CHECK(cmp_rows("summary", 1, ref, nref, 0) == 0);
    
    af_meas_run_t *res = af_meas_find(&C, 1);
    af_meas_sum_t s;
    af_meas_summary(res, &s);
    
    CHECK(s.n_valid == 50);
    
    double sum_cps = 0.0;
    double sum_cps2 = 0.0;
    double min_cps = 1e18;
    double max_cps = -1e18;
    double sum_counts = 0.0;
    double sum_npks = 0.0;
    double chi2 = 0.0;
    
    for (int k = 0; k < nref; k++) {
        double cps_k = (double)ref[k].counts / (2.0 * (double)ref[k].npk);
        sum_cps += cps_k;
        sum_cps2 += cps_k * cps_k;
        if (cps_k < min_cps) min_cps = cps_k;
        if (cps_k > max_cps) max_cps = cps_k;
        sum_counts += (double)ref[k].counts;
        sum_npks += (double)ref[k].npk;
    }
    
    double mean_cps = sum_cps / nref;
    double var_cps = (sum_cps2 - nref * mean_cps * mean_cps) / (nref - 1);
    double sd_cps = sqrt(var_cps);
    double sem_cps = sd_cps / sqrt(nref);
    double pooled_cps = sum_counts / (2.0 * sum_npks);
    
    double sw = 0.0, swc = 0.0;   /* взвешенное среднее по пуассоновским весам */
    for (int k = 0; k < nref; k++) {
        double cps_k = (double)ref[k].counts / (2.0 * (double)ref[k].npk);
        double sigma_k = (ref[k].counts == 0 ? 1.0 : sqrt((double)ref[k].counts)) / (2.0 * (double)ref[k].npk);
        double w = 1.0 / (sigma_k * sigma_k);
        sw += w;
        swc += w * cps_k;
    }
    double mu = swc / sw;
    for (int k = 0; k < nref; k++) {
        double cps_k = (double)ref[k].counts / (2.0 * (double)ref[k].npk);
        double sigma_k = (ref[k].counts == 0 ? 1.0 : sqrt((double)ref[k].counts)) / (2.0 * (double)ref[k].npk);
        chi2 += (cps_k - mu) * (cps_k - mu) / (sigma_k * sigma_k);
    }
    double chi2_ndf = chi2 / (nref - 1);
    
    CHECK(near(s.mean_cps, mean_cps, 1e-5));
    CHECK(near(s.sd_cps, sd_cps, 1e-5));
    CHECK(near(s.sem_cps, sem_cps, 1e-5));
    CHECK(near(s.min_cps, min_cps, 1e-5));
    CHECK(near(s.max_cps, max_cps, 1e-5));
    CHECK(near(s.pooled_cps, pooled_cps, 1e-5));
    CHECK(near(s.chi2_ndf, chi2_ndf, 1e-5));
    
    if (getenv("AF_PROBE")) {
        printf("DATA summary %u %.5f %.5f %.5f %.5f %.6f\n", (unsigned)s.n_valid, s.mean_cps, s.sd_cps, s.sem_cps, s.chi2_ndf, s.dose_usv);
    }
    printf("INFO real_summary chi2_ndf=%.4f mean_cps=%.4f\n", s.chi2_ndf, s.mean_cps);
    free(ref);
}

static void t_real_loss_scan(void) {
    const uint32_t utc0 = 1792859805u;
    uint32_t lms[] = {2, 3, 4, 5, 6, 10};
    int n_lms = 6;
    for (int i = 0; i < n_lms; i++) {
        t_teardown();
        t_setup();
        af_meas_params_t p = t_params();
        p.n = 0;
        p.t_s = 60;
        p.retry_invalid = 0;
        p.lost_max = lms[i];
        
        uint32_t id = 0;
        CHECK(af_meas_start(&C, &p, RX[0], 1, utc0, &id) == 0);
        feed_all();
        
        ref_row_t *ref = (ref_row_t *)malloc(2000 * sizeof(ref_row_t));
        CHECK(ref != NULL);
        if (!ref) return;
        
        int nref = ref_series(RX[0], 60000, 0, lms[i], 0, ref, 2000);
        CHECK(cmp_rows("lscan", 1, ref, nref, 0) == 0);
        
        free(ref);
    }
    printf("INFO real_loss_scan lost_max_values=6\n");
}

static void t_real_loss_summary(void) {
    const uint32_t utc0 = 1792859805u;
    t_teardown();
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.retry_invalid = 0;
    p.lost_max = 2;
    
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, RX[0], 1, utc0, &id) == 0);
    feed_all();
    
    ref_row_t *ref = (ref_row_t *)malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    if (!ref) return;
    
    int nref = ref_series(RX[0], 60000, 0, 2, 0, ref, 2000);
    
    af_meas_run_t *run = af_meas_find(&C, 1);
    CHECK(run != NULL);
    if (!run) { free(ref); return; }
    
    af_meas_sum_t s;
    af_meas_summary(run, &s);
    
    int ref_valid = 0;
    int ref_invalid = 0;
    double sum_cps = 0.0;
    for (int i = 0; i < nref; i++) {
        if (ref[i].valid) {
            ref_valid++;
            // cps_k = counts / (2 * npk)
            if (ref[i].npk > 0) {
                sum_cps += (double)ref[i].counts / (2.0 * (double)ref[i].npk);
            }
        } else {
            ref_invalid++;
        }
    }
    
    CHECK((int)s.n_valid == ref_valid);
    CHECK((int)s.n_invalid == ref_invalid);
    
    if (ref_valid > 0) {
        double mean_cps_ref = sum_cps / (double)ref_valid;
        CHECK(near(s.mean_cps, mean_cps_ref, 1e-5));
    }
    
    if (getenv("AF_PROBE")) {
        printf("DATA lsum %u %u %.4f %.4f\n", s.n_valid, s.n_invalid, s.mean_cps, s.sd_cps);
    }
    
    free(ref);
}

static void t_real_limit(void) {
    const uint32_t utc0 = 1792859805u;
    int trials = 0;
    int skipped = 0;
    
    for (int s = 0; s < NP; s += 700) {
        t_teardown();
        t_setup();
        af_meas_params_t p = t_params();
        p.n = 1;
        p.stop_mode = AF_MEAS_STOP_SIGMA;
        p.eps_x100 = 10;
        p.t_min_s = 10;
        p.t_max_s = 300;
        
        uint32_t id = 0;
        CHECK(af_meas_start(&C, &p, RX[s], 1, utc0, &id) == 0);
        
        // Эталонный расчет
        uint64_t t0 = RX[s];
        uint32_t cum = 0;
        uint32_t npk = 0;
        int stop_at_limit = 0;
        int gap_found = 0;
        uint64_t prev_rx = t0;
        
        for (int k = s; k < NP; k++) {
            // Проверка разрыва до обработки пакета k
            if (RX[k] - prev_rx > 6000) {
                gap_found = 1;
            }
            prev_rx = RX[k];
            
            if (RX[k] - t0 >= 300000) {
                stop_at_limit = 1;
                break;
            }
            
            cum += PU[k];
            npk++;
        }
        
        // Если поток закончился раньше 300с, пробу пропускаем
        if (!stop_at_limit) {
            skipped++;
            continue;
        }
        
        // Если был разрыв > 6000 мс, пробу пропускаем
        if (gap_found) {
            skipped++;
            continue;
        }
        
        trials++;
        
        // Кормим ядро
        af_meas_run_t *run = af_meas_find(&C, 1);
        CHECK(run != NULL);
        if (!run) continue;
        
        for (int k = s; k < NP; k++) {
            af_meas_feed(&C, RX[k], PU[k], RT[k]);
            if (run->state == AF_MEAS_ST_DONE) break;
        }
        
        CHECK(run->state == AF_MEAS_ST_DONE);
        
        const af_meas_res_t *row = t_row(1, 0);
        CHECK(row != NULL);
        if (!row) continue;
        
        CHECK(row->stop_reason == AF_MEAS_SR_LIMIT);
        CHECK(row->flags & AF_MEAS_F_GOAL_MISS);
        CHECK(row->valid == 1);
        CHECK(row->counts == cum);
        CHECK(row->npk == npk);
        CHECK(row->t_ms == 300000);
        
        if (getenv("AF_PROBE")) {
            printf("DATA limit %d %u %u %u %d\n", s, row->counts, row->npk, row->t_ms, row->stop_reason);
        }
    }
    
    printf("INFO real_limit trials=%d skipped=%d\n", trials, skipped);
    CHECK(trials - skipped >= 5);
}

static void t_real_jump(void) {
    const uint32_t utc0 = 1792859805u;
    t_teardown();
    t_setup_seq();
    
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.retry_invalid = 0;
    p.lost_max = 60;
    
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, RX[0], 1, utc0, &id) == 0);
    
    for (int k = 0; k < NP; k++) {
        uint32_t utc_now = utc0 + (uint32_t)((RX[k] - RX[0]) / 1000);
        if (k >= 5000) {
            utc_now += 3600u;
        }
        af_meas_tick(&C, RX[k], 1, utc_now);
        af_meas_feed(&C, RX[k], PU[k], RT[k]);
    }
    
    ref_row_t *ref = (ref_row_t *)malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    if (!ref) return;
    
    int nref = ref_series(RX[0], 60000, 0, 60, 0, ref, 2000);
    CHECK(cmp_rows("jump", 1, ref, nref, 0) == 0);
    
    af_meas_run_t *run = af_meas_find(&C, 1);
    CHECK(run != NULL);
    if (!run) { free(ref); return; }
    
    int n_rows = af_meas_res_count(run);
    int jump_count = 0;
    int jump_idx = -1;
    
    for (int i = 0; i < n_rows; i++) {
        const af_meas_res_t *row = af_meas_res_at(run, i);
        CHECK(row != NULL);
        if (!row) continue;
        
        // Проверка флага UTC_OK
        CHECK(row->flags & AF_MEAS_F_UTC_OK);
        
        // Проверка флага TIMEJUMP
        if (row->flags & AF_MEAS_F_TIMEJUMP) {
            jump_count++;
            jump_idx = i;
        }
        
        // Проверка raw_seq
        CHECK(row->raw_seq1 > row->raw_seq0);
        CHECK(row->raw_epoch == 7);
        
        if (getenv("AF_PROBE")) {
            printf("DATA jump %d %u %u %u %u\n", i, row->t0_utc, row->flags, row->raw_seq0, row->raw_seq1);
        }
    }
    
    CHECK(jump_count == 1);
    
    // Проверка, что окно со скачком содержит RX[5000]
    if (jump_idx >= 0 && jump_idx < nref) {
        const af_meas_res_t *row = af_meas_res_at(run, jump_idx);
        CHECK(row != NULL);
        if (row) {
            uint64_t t0_mono = row->t0_mono_s * 1000;
            uint64_t t_end_mono = t0_mono + row->t_ms;
            // RX[5000] должен быть в пределах [t0_mono, t_end_mono) относительно RX[0]
            // t0_mono_s это секунды от начала прогона? Или абсолютные?
            // В условии: t0_mono_s. Обычно это монохронное время в секундах.
            // RX[k] - RX[0] это смещение в мс.
            uint64_t offset_5000 = RX[5000];
            CHECK(offset_5000 + 999 >= t0_mono && offset_5000 < t_end_mono + 999);
        }
    }
    
    // Проверка последней записи
    if (nref > 0 && n_rows > 0) {
        const af_meas_res_t *last_row = af_meas_res_at(run, n_rows - 1);
        CHECK(last_row != NULL);
        if (last_row) {
            uint32_t expected_utc = utc0 + 3600 + (ref[nref-1].t0 - RX[0]) / 1000;
            CHECK(last_row->t0_utc == expected_utc);
        }
    }
    
    // Проверка последовательности raw_seq0
    for (int i = 1; i < n_rows; i++) {
        const af_meas_res_t *prev = af_meas_res_at(run, i - 1);
        const af_meas_res_t *curr = af_meas_res_at(run, i);
        CHECK(prev != NULL && curr != NULL);
        if (prev && curr) {
            CHECK(curr->raw_seq0 > prev->raw_seq1);
        }
    }
    
    free(ref);
}

static void t_real_ring(void) {
    const uint32_t utc0 = 1792859805u;
    t_teardown();
    t_setup();
    g_alloc_max = 64 * sizeof(af_meas_res_t);
    
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 60;
    p.retry_invalid = 0;
    p.lost_max = 60;
    
    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, RX[0], 1, utc0, &id) == 0);
    feed_all();
    
    af_meas_run_t *run = af_meas_find(&C, 1);
    CHECK(run != NULL);
    if (!run) { g_alloc_max = 0; return; }
    
    CHECK(run->res_cap == 64);
    
    ref_row_t *ref = (ref_row_t *)malloc(2000 * sizeof(ref_row_t));
    CHECK(ref != NULL);
    if (!ref) { g_alloc_max = 0; return; }
    
    int nref = ref_series(RX[0], 60000, 0, 60, 0, ref, 2000);
    
    CHECK((int)run->rows_total == nref);
    CHECK((int)run->dropped == nref - 64);
    CHECK(run->res_n == 64);
    
    // Сравнение последних 64 записей
    for (int i = 0; i < 64; i++) {
        int run_idx = i;
        int ref_idx = nref - 64 + i;
        
        const af_meas_res_t *row = af_meas_res_at(run, run_idx);
        CHECK(row != NULL);
        if (!row) continue;
        
        CHECK(row->i == ref[ref_idx].i);
        CHECK(row->counts == ref[ref_idx].counts);
        CHECK(row->npk == ref[ref_idx].npk);
        CHECK(row->t0_mono_s == ref[ref_idx].t0 / 1000); // t0 в эталоне в мс, t0_mono_s в секундах
    }
    
    if (getenv("AF_PROBE")) {
        const af_meas_res_t *first_row = t_row(1, 0);
        CHECK(first_row != NULL);
        if (first_row) {
            printf("DATA ring %u %u %u\n", run->rows_total, run->dropped, first_row->i);
        }
    }
    
    free(ref);
    g_alloc_max = 0;
}
int main(void) {
    if (!load_data()) {
        printf("FAIL load_data\n");
        return 2;
    }
    
    RUN2(t_real_boundaries);
    RUN2(t_real_loss);
    RUN2(t_real_pause);
    RUN2(t_real_retry);
    RUN2(t_real_sigma);
    RUN2(t_real_summary);
    RUN2(t_real_loss_scan);
    RUN2(t_real_loss_summary);
    RUN2(t_real_limit);
    RUN2(t_real_jump);
    RUN2(t_real_ring);
    
    TESTS_DONE();
    return 0;
}
