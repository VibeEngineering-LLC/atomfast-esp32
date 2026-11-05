#include "af_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "af_ble_stats.h"
#include "af_ble_snap.h"
#include "af_ble_nvs.h"
#define af_bs_init(s, r) do { (af_bs_init)((s), (r)); af_bs_set_ext((s), true); } while (0) /* старые тесты - с ext=1; новые зовут (af_bs_init) */

static af_bs_t S;
static af_bs_sess_t ring[32];
static uint32_t TS[12000];

static int near(double a, double b, double eps) {
    return fabs(a - b) <= eps;
}

static af_bs_snap_t snap(uint64_t now) {
    af_bs_tick(&S, now);
    af_bs_snap_t s;
    af_bs_snapshot(&S, now, &s);
    return s;
}

static int load_ts(uint32_t *ts, int cap) {
    FILE *f = fopen("data/e16_raw_ts.csv", "r");
    if (!f) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    int n = 0;
    while (n < cap && fgets(line, sizeof(line), f)) {
        uint32_t ts_val;
        if (sscanf(line, "%*u,%u", &ts_val) == 1) {
            ts[n++] = ts_val;
        }
    }
    fclose(f);
    return n;
}

static void t_reason_table(void) {
    CHECK(af_bs_reason_group(0x08) == AF_BS_R_TIMEOUT);
    CHECK(af_bs_reason_group(0x22) == AF_BS_R_TIMEOUT);
    CHECK(af_bs_reason_group(0x13) == AF_BS_R_REMOTE);
    CHECK(af_bs_reason_group(0x14) == AF_BS_R_REMOTE);
    CHECK(af_bs_reason_group(0x15) == AF_BS_R_REMOTE);
    CHECK(af_bs_reason_group(0x16) == AF_BS_R_LOCAL);
    CHECK(af_bs_reason_group(0x100) == AF_BS_R_LOCAL);
    CHECK(af_bs_reason_group(0x3E) == AF_BS_R_ESTABLISH);
    CHECK(af_bs_reason_group(0x01) == AF_BS_R_L2CAP);
    CHECK(af_bs_reason_group(0x00) == AF_BS_R_OTHER);
    CHECK(af_bs_reason_group(0x99) == AF_BS_R_OTHER);
    CHECK(af_bs_reason_group(0x85) == AF_BS_R_OTHER);

    CHECK(strcmp(af_bs_reason_text(0x08), "supervision timeout") == 0);
    CHECK(strcmp(af_bs_reason_text(0x13), "remote terminated") == 0);
    CHECK(strcmp(af_bs_reason_text(0x16), "local terminated") == 0);
    CHECK(strcmp(af_bs_reason_text(0x3E), "conn failed to establish") == 0);
    CHECK(strcmp(af_bs_reason_text(0x22), "LL response timeout") == 0);
    CHECK(strcmp(af_bs_reason_text(0x99), "other") == 0);

    CHECK(strcmp(af_bs_group_text(0), "other") == 0);
    CHECK(strcmp(af_bs_group_text(1), "timeout") == 0);
    CHECK(strcmp(af_bs_group_text(2), "remote") == 0);
    CHECK(strcmp(af_bs_group_text(3), "local") == 0);
    CHECK(strcmp(af_bs_group_text(4), "establish") == 0);
    CHECK(strcmp(af_bs_group_text(5), "l2cap") == 0);
    CHECK(strcmp(af_bs_group_text(6), "other") == 0);
}

static void t_counts_flow(void) {
    af_bs_init(&S, ring);
    af_bs_on_attempt(&S, 1000);
    af_bs_on_connect(&S, 1850, 247);
    af_bs_on_packet(&S, 3850, true);
    af_bs_on_packet(&S, 5850, true);
    af_bs_on_disconnect(&S, 8000, 1700000000, 0x13);

    CHECK(S.boot.attempts == 1);
    CHECK(S.boot.connects == 1);
    CHECK(S.boot.disconnects == 1);
    CHECK(S.boot.fails == 0);
    CHECK(S.boot.reason[AF_BS_R_REMOTE] == 1);
    CHECK(S.life.attempts == 1);
    CHECK(S.life.connects == 1);
    CHECK(S.life.disconnects == 1);
    CHECK(S.life.fails == 0);
    CHECK(S.life.reason[AF_BS_R_REMOTE] == 1);
    CHECK(S.last_reason == 0x13);
    CHECK(S.last_reason_valid);

    const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);
    CHECK(rec != NULL);
    CHECK(rec->pkts == 2);
    CHECK(rec->bad == 0);
    CHECK(rec->reason == 0x13);
    CHECK(rec->end_utc == 1700000000);
    CHECK(rec->rssi_min == AF_BS_RSSI_ND);
    CHECK(rec->rssi_avg == AF_BS_RSSI_ND);
    CHECK(rec->rssi_max == AF_BS_RSSI_ND);
    CHECK(S.ring_n == 1);
    CHECK(S.connected == false);
}

static void t_setup_ms(void) {
    af_bs_init(&S, ring);
    af_bs_on_attempt(&S, 1000);
    af_bs_on_connect(&S, 1850, 23);
    af_bs_snap_t s = snap(1850);
    CHECK(s.setup_ms == 850);
    af_bs_on_disconnect(&S, 5000, 0, 0x13);
    const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);
    CHECK(rec != NULL);
    CHECK(rec->setup_ms == 850);

    af_bs_on_connect(&S, 9000, 23);
    s = snap(9000);
    CHECK(s.setup_ms == AF_BS_ND);
}

static void t_fail_streak(void) {
    af_bs_init(&S, ring);
    uint64_t t;
    for (t = 1000; t <= 3000; t += 1000) {
        af_bs_on_attempt(&S, t);
        af_bs_on_open_fail(&S, t + 100, 0x3E);
    }
    CHECK(S.boot.fails == 3);
    CHECK(S.life.fails == 3);
    CHECK(S.boot.attempts == 3);
    CHECK(S.fail_streak == 3);
    CHECK(S.boot.reason[AF_BS_R_ESTABLISH] == 3);
    CHECK(S.last_reason == 0x3E);

    af_bs_on_attempt(&S, 4000);
    af_bs_on_connect(&S, 4300, 23);
    CHECK(S.fail_streak == 0);
    CHECK(S.boot.connects == 1);
    CHECK(S.boot.fails == 3);
}

static void t_dur_mono(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 10000, 23);
    af_bs_on_disconnect(&S, 70000, 5, 0x16);
    const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);
    CHECK(rec != NULL);
    CHECK(rec->dur_ms == 60000);
    CHECK(rec->end_utc == 5);

    af_bs_on_connect(&S, 100000, 23);
    af_bs_on_disconnect(&S, 160000, 4000000000u, 0x16);
    rec = af_bs_ring_get(&S, 0);
    CHECK(rec != NULL);
    CHECK(rec->dur_ms == 60000);
    CHECK(rec->end_utc == 4000000000u);
    rec = af_bs_ring_get(&S, 1);
    CHECK(rec != NULL);
    CHECK(rec->dur_ms == 60000);

    af_bs_tick(&S, 50000);
    CHECK(S.boot.conn_ms == 120000);

    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_disconnect(&S, 5000, 0, 0x16);
    af_bs_on_connect(&S, 6000, 23);
    af_bs_on_disconnect(&S, 7000, 0, 0x16);
    af_bs_on_connect(&S, 8000, 23);
    af_bs_on_disconnect(&S, 17000, 0, 0x16);
    af_bs_snap_t s = snap(17000);
    CHECK(s.have_last);
    CHECK(s.last_dur_ms == 9000);
    CHECK(s.have_ext);
    CHECK(s.longest_ms == 9000);
    CHECK(s.shortest_ms == 1000);
}

static void inject(const uint32_t *d, int n) {
    af_bs_init(&S, ring);
    for (int i = 0; i < n; i++) {
        ring[i].dur_ms = d[i];
    }
    S.ring_n = (uint8_t)n;
    S.ring_head = (uint8_t)(n % 32);
}

static void t_ring_stats(void) {
    uint32_t d1[] = {10000, 20000, 30000, 40000, 100000};
    inject(d1, 5);
    af_bs_snap_t s = snap(100000);
    CHECK(s.ring_n == 5);
    CHECK(s.avg_ms == 40000);
    CHECK(s.median_ms == 30000);

    uint32_t d2[] = {10000, 20000, 30000, 40000};
    inject(d2, 4);
    s = snap(100000);
    CHECK(s.avg_ms == 25000);
    CHECK(s.median_ms == 25000);

    uint32_t d3[] = {1000, 2001};
    inject(d3, 2);
    s = snap(100000);
    CHECK(s.avg_ms == 1500);
    CHECK(s.median_ms == 1500);

    uint32_t d4[] = {7};
    inject(d4, 1);
    s = snap(100000);
    CHECK(s.avg_ms == 7);
    CHECK(s.median_ms == 7);

    uint32_t d5[] = {30000, 10000, 40000, 20000};
    inject(d5, 4);
    s = snap(100000);
    CHECK(s.median_ms == 25000);

    for (int i = 0; i < 32; i++) {
        ring[i].dur_ms = (uint32_t)(i + 1) * 1000;
    }
    S.ring_n = 32;
    S.ring_head = 0;
    s = snap(100000);
    CHECK(s.avg_ms == 16500);
    CHECK(s.median_ms == 16500);

    S.ring_head = 7;
    s = snap(100000);
    CHECK(s.avg_ms == 16500);
    CHECK(s.median_ms == 16500);
    CHECK(s.ring_n == 32);

    inject(NULL, 0);
    s = snap(100000);
    CHECK(s.ring_n == 0);
    CHECK(s.avg_ms == 0);
    CHECK(s.median_ms == 0);
}

static void t_ring_overflow(void) {
    af_bs_init(&S, ring);
    for (int i = 0; i < 40; i++) {
        uint64_t t = (uint64_t)i * 2000;
        af_bs_on_connect(&S, t, 23);
        af_bs_on_disconnect(&S, t + 1000, 1000 + i, 0x13);
    }
    CHECK(S.ring_n == 32);
    const af_bs_sess_t *r0 = af_bs_ring_get(&S, 0);
    CHECK(r0 != NULL);
    CHECK(r0->end_utc == 1039);
    const af_bs_sess_t *r31 = af_bs_ring_get(&S, 31);
    CHECK(r31 != NULL);
    CHECK(r31->end_utc == 1008);
    CHECK(af_bs_ring_get(&S, 32) == NULL);
    CHECK(S.boot.connects == 40);
    CHECK(S.boot.disconnects == 40);
    CHECK(S.life.connects == 40);
    CHECK(S.life.disconnects == 40);
    CHECK(S.boot.reason[AF_BS_R_REMOTE] == 40);
}

static void t_pps_ref(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 700000, 23);
    for (int k = 1; k <= 300; k++) {
        af_bs_on_packet(&S, 700000 + 2000 * k, true);
    }
    af_bs_snap_t s = snap(1300000);
    CHECK(near(s.pps_inst, 5.0 / 9.0, 1e-4));
    CHECK(near(s.pps_1m, 0.52, 1e-4));
    CHECK(near(s.pps_10m, 296.0 / 590.0, 1e-4));
    CHECK(near(s.pps_sess, 0.5, 1e-4));
    CHECK(near(s.bps_sess, 6.5, 1e-4));
    CHECK(near(s.bps_inst, (5.0 / 9.0) * 13, 1e-4));
}

static void t_pps_early(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_packet(&S, 2000, true);
    af_bs_on_packet(&S, 4000, true);
    af_bs_snap_t s = snap(4000);
    CHECK(near(s.pps_inst, 0.5, 1e-6));
    CHECK(near(s.pps_1m, 0.5, 1e-6));
    CHECK(near(s.pps_10m, 0.5, 1e-6));
    CHECK(near(s.pps_sess, 0.5, 1e-6));
}

static void t_idle_nd(void) {
    af_bs_init(&S, ring);
    af_bs_snap_t s;
    af_bs_snapshot(&S, 5000, &s);
    CHECK(s.connected == false);
    CHECK(isnan(s.pps_inst));
    CHECK(isnan(s.pps_1m));
    CHECK(isnan(s.pps_10m));
    CHECK(isnan(s.pps_sess));
    CHECK(isnan(s.bps_inst));
    CHECK(s.i_last_ms == AF_BS_ND);
    CHECK(s.i_min_ms == AF_BS_ND);
    CHECK(s.i_max_ms == AF_BS_ND);
    CHECK(s.i10_min_ms == AF_BS_ND);
    CHECK(s.i10_max_ms == AF_BS_ND);
    CHECK(isnan(s.i_avg_ms));
    CHECK(isnan(s.jitter_ms));
    CHECK(isnan(s.i10_avg_ms));
    CHECK(s.latency == AF_BS_ND);
    CHECK(s.timeout_ms == AF_BS_ND);
    CHECK(s.mtu == AF_BS_ND);
    CHECK(s.tx_eff == AF_BS_ND);
    CHECK(isnan(s.ci_ms));
    CHECK(s.rssi_cur == AF_BS_RSSI_ND);
    CHECK(s.rssi_min == AF_BS_RSSI_ND);
    CHECK(s.rssi_avg == AF_BS_RSSI_ND);
    CHECK(s.rssi_max == AF_BS_RSSI_ND);
    CHECK(s.last_pkt_age_ms == AF_BS_ND);
    CHECK(s.scan_detect_ms == AF_BS_ND);
    CHECK(s.setup_ms == AF_BS_ND);
    CHECK(s.ring_n == 0);
    CHECK(isnan(s.bad_pct));
    CHECK(isnan(s.scan_int_ms));
    CHECK(isnan(s.scan_win_ms));
    CHECK(near(s.up_boot_pct, 0.0, 1e-6));
}

static void t_intervals_basic(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_packet(&S, 1000, true);
    af_bs_snap_t s = snap(1000);
    CHECK(s.i_last_ms == AF_BS_ND);

    af_bs_on_packet(&S, 3000, true);
    af_bs_on_packet(&S, 9000, true);
    af_bs_on_packet(&S, 15001, true);
    af_bs_on_packet(&S, 45001, true);
    s = snap(45001);
    CHECK(s.i_last_ms == 30000);
    CHECK(s.pauses == 2);
    CHECK(s.pause_max_ms == 30000);
}

static void t_bad_pkts(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    for (int k = 1; k <= 10; k++) {
        bool ok = (k != 3 && k != 7);
        af_bs_on_packet(&S, 1000 * k, ok);
    }
    af_bs_snap_t s = snap(12000);
    CHECK(s.sess_pkts == 10);
    CHECK(s.sess_bad == 2);
    CHECK(s.pkts_boot == 10);
    CHECK(s.bad_boot == 2);
    CHECK(near(s.bad_pct, 20.0, 1e-4));
    CHECK(s.have_bad);
    CHECK(s.last_bad_age_ms == 5000);
}

static void t_last_pkt_age(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_snap_t s = snap(500);
    CHECK(s.last_pkt_age_ms == AF_BS_ND);

    af_bs_on_packet(&S, 1000, true);
    s = snap(4500);
    CHECK(s.last_pkt_age_ms == 3500);

    af_bs_on_disconnect(&S, 5000, 0, 0x13);
    s = snap(6000);
    CHECK(s.last_pkt_age_ms == AF_BS_ND);
}

static void t_rssi_hist(void) {
    af_bs_init(&S, ring);
    af_bs_on_rssi(&S, -50);
    af_bs_on_connect(&S, 0, 23);
    af_bs_snap_t s = snap(0);
    CHECK(s.rssi_cur == AF_BS_RSSI_ND);

    int8_t vals[] = {-35, -40, -49, -50, -99, -100, -120, 127, 50};
    for (int i = 0; i < 9; i++) {
        af_bs_on_rssi(&S, vals[i]);
    }
    s = snap(1000);
    CHECK(s.rssi_cur == -120);
    CHECK(s.rssi_min == -120);
    CHECK(s.rssi_max == -35);
    uint16_t exp_hist[] = {1, 2, 1, 0, 0, 0, 1, 2};
    for (int i = 0; i < AF_BS_RSSI_BINS; i++) {
        CHECK(s.rssi_hist[i] == exp_hist[i]);
    }

    af_bs_on_disconnect(&S, 2000, 0, 0x13);
    const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);
    CHECK(rec != NULL);
    CHECK(rec->rssi_min == -120);
    CHECK(rec->rssi_max == -35);
}

static void t_rssi_round(void) {
    struct { int8_t v[10]; int n; int8_t exp; } cases[] = {
        {{-1, -2}, 2, -2},
        {{3, 4}, 2, 4},
        {{-3, -4}, 2, -4},
        {{-1, -1, -2}, 3, -1},
        {{-70}, 1, -70},
        {{-35, -40, -49, -50, -99, -100, -120}, 7, -70}
    };
    for (int c = 0; c < 6; c++) {
        af_bs_init(&S, ring);
        af_bs_on_connect(&S, 0, 23);
        for (int i = 0; i < cases[c].n; i++) {
            af_bs_on_rssi(&S, cases[c].v[i]);
        }
        af_bs_snap_t s = snap(1000);
        CHECK(s.rssi_avg == cases[c].exp);
        af_bs_on_disconnect(&S, 2000, 0, 0x13);
        const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);
        CHECK(rec != NULL);
        CHECK(rec->rssi_avg == cases[c].exp);
    }
}

static void t_conn_params(void) {
    af_bs_init(&S, ring);
    af_bs_set_conn_params(&S, 6, 0, 500);
    af_bs_set_mtu(&S, 100);
    af_bs_set_tx(&S, 15);
    af_bs_on_connect(&S, 0, 23);
    af_bs_snap_t s = snap(10);
    CHECK(isnan(s.ci_ms));
    CHECK(s.latency == AF_BS_ND);
    CHECK(s.timeout_ms == AF_BS_ND);
    CHECK(s.mtu == 23);
    CHECK(s.tx_eff == AF_BS_ND);

    af_bs_set_conn_params(&S, 6, 0, 500);
    af_bs_set_mtu(&S, 247);
    af_bs_set_tx(&S, 15);
    s = snap(20);
    CHECK(near(s.ci_ms, 7.5, 1e-6));
    CHECK(s.latency == 0);
    CHECK(s.timeout_ms == 5000);
    CHECK(s.mtu == 247);
    CHECK(s.tx_eff == 15);

    af_bs_set_conn_params(&S, 24, 4, 400);
    s = snap(30);
    CHECK(near(s.ci_ms, 30.0, 1e-6));
    CHECK(s.latency == 4);
    CHECK(s.timeout_ms == 4000);

    af_bs_set_conn_params(&S, 0, 0, 0);
    s = snap(40);
    CHECK(isnan(s.ci_ms));
    CHECK(s.latency == AF_BS_ND);
    CHECK(s.timeout_ms == AF_BS_ND);

    af_bs_on_disconnect(&S, 100, 0, 0x16);
    s = snap(200);
    CHECK(isnan(s.ci_ms));
    CHECK(s.mtu == AF_BS_ND);
    CHECK(s.tx_eff == AF_BS_ND);
    CHECK(s.latency == AF_BS_ND);
    CHECK(s.timeout_ms == AF_BS_ND);
}

static void t_scan(void) {
    af_bs_init(&S, ring);
    af_bs_scan_params(&S, 960, 960);
    af_bs_on_scan_start(&S, 1000);
    af_bs_on_scan_adv(&S);
    af_bs_on_scan_adv(&S);
    af_bs_on_scan_adv(&S);
    af_bs_on_scan_hit(&S, 1500);
    af_bs_on_scan_hit(&S, 1600);
    af_bs_on_scan_stop(&S);
    af_bs_on_scan_adv(&S);
    af_bs_snap_t s = snap(2000);
    CHECK(s.scan_n == 1);
    CHECK(s.scan_adv == 3);
    CHECK(s.scan_cand == 2);
    CHECK(s.scan_detect_ms == 500);
    CHECK(near(s.scan_int_ms, 600.0, 1e-6));
    CHECK(near(s.scan_win_ms, 600.0, 1e-6));

    af_bs_on_scan_start(&S, 5000);
    s = snap(5000);
    CHECK(s.scan_n == 2);
    CHECK(s.scan_adv == 0);
    CHECK(s.scan_cand == 0);
    CHECK(s.scan_detect_ms == AF_BS_ND);
}

static void t_uptime_pct(void) {
    af_bs_init(&S, ring);
    af_bs_tick(&S, 0);
    af_bs_on_connect(&S, 30000, 23);
    af_bs_on_disconnect(&S, 60000, 0, 0x13);
    af_bs_snap_t s = snap(100000);
    CHECK(near(s.up_boot_pct, 30.0, 1e-3));
    CHECK(near(s.up_24h_pct, 30.0, 1e-3));

    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_disconnect(&S, 7200000, 0, 0x13);
    s = snap(10800000);
    CHECK(near(s.up_boot_pct, 66.6667, 1e-3));
    CHECK(near(s.up_24h_pct, 66.6667, 1e-3));

    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    for (uint64_t t = 1800000; t <= 108000000; t += 1800000) {
        af_bs_tick(&S, t);
    }
    s = snap(108000000);
    CHECK(near(s.up_24h_pct, 100.0, 1e-3));
    CHECK(near(s.up_boot_pct, 100.0, 1e-3));

    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_disconnect(&S, 3600000, 0, 0x13);
    for (uint64_t t = 1800000; t <= 93600000; t += 1800000) {
        af_bs_tick(&S, t);
    }
    s = snap(93600000);
    CHECK(near(s.up_24h_pct, 0.0, 1e-3));
    CHECK(near(s.up_boot_pct, 3.84615, 1e-3));
}

static void t_up24_ext_off(void)
{
    /* b29 B04: при ext=0 часовые корзины не ведутся - up_24h_pct не число (NAN), при ext=1 - число */
    (af_bs_init)(&S, ring);
    af_bs_set_ext(&S, false);
    af_bs_tick(&S, 0);
    af_bs_on_connect(&S, 0, 23);
    for (uint64_t t = 1800000; t <= 108000000; t += 1800000) {
        af_bs_tick(&S, t);
    }
    af_bs_snap_t s = snap(108000000);
    CHECK(isnan(s.up_24h_pct));
    CHECK(near(s.up_boot_pct, 100.0, 1e-3));

    af_bs_init(&S, ring);   /* ext=1 */
    af_bs_on_connect(&S, 0, 23);
    for (uint64_t t = 1800000; t <= 108000000; t += 1800000) {
        af_bs_tick(&S, t);
    }
    s = snap(108000000);
    CHECK(!isnan(s.up_24h_pct));
    CHECK(near(s.up_24h_pct, 100.0, 1e-3));
}

static void t_window_rollover(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 100000, 23);
    af_bs_on_packet(&S, 101000, true);
    af_bs_on_packet(&S, 103000, true);
    af_bs_snap_t s = snap(120000);
    CHECK(near(s.pps_inst, 0.0, 1e-9));
    CHECK(near(s.pps_1m, 0.04, 1e-6));
    CHECK(near(s.pps_10m, 2000.0 / 120000.0, 1e-6));

    s = snap(170000);
    CHECK(near(s.pps_1m, 0.0, 1e-6));
    CHECK(near(s.pps_10m, 2000.0 / 170000.0, 1e-6));

    s = snap(800000);
    CHECK(near(s.pps_10m, 0.0, 1e-6));
    CHECK(s.i10_min_ms == AF_BS_ND);
    CHECK(s.i10_max_ms == AF_BS_ND);

    af_bs_tick(&S, 10);
    s = snap(800000);
    CHECK(near(s.pps_10m, 0.0, 1e-6));
}

static void t_idempotent(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 1000, 23);
    af_bs_on_connect(&S, 2000, 99);
    CHECK(S.boot.connects == 1);
    CHECK(S.start_ms == 1000);
    CHECK(S.mtu == 23);

    af_bs_on_packet(&S, 3000, true);
    af_bs_on_disconnect(&S, 5000, 0, 0x13);
    af_bs_on_disconnect(&S, 6000, 0, 0x16);
    CHECK(S.boot.disconnects == 1);
    CHECK(S.ring_n == 1);
    CHECK(S.last_reason == 0x13);

    af_bs_on_packet(&S, 7000, true);
    CHECK(S.pkts_boot == 1);

    af_bs_on_rssi(&S, -50);
    CHECK(S.rssi_n == 0);
}

static void t_ring_null(void) {
    af_bs_init(&S, NULL);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_disconnect(&S, 5000, 0, 0x13);
    CHECK(af_bs_ring_get(&S, 0) == NULL);
    af_bs_snap_t s = snap(6000);
    CHECK(s.ring_n == 0);
    CHECK(s.avg_ms == 0);
    CHECK(s.median_ms == 0);
    CHECK(S.boot.disconnects == 1);
}

static void t_nvs_roundtrip(void) {
    af_bs_init(&S, ring);
    S.life.connects = 1000;
    S.life.disconnects = 999;
    S.life.attempts = 1500;
    S.life.fails = 7;
    S.life.reason[0] = 1;
    S.life.reason[1] = 2;
    S.life.reason[2] = 3;
    S.life.reason[3] = 4;
    S.life.reason[4] = 5;
    S.life.reason[5] = 6;
    S.life.conn_ms = 0x0123456789ABCDEFull;

    uint8_t buf[100];
    size_t n = af_bsn_encode(&S, buf, 100);
    CHECK(n == 64);
    CHECK(buf[0] == 0x41);
    CHECK(buf[1] == 0x53);
    CHECK(buf[2] == 0x42);
    CHECK(buf[3] == 0x31);
    CHECK(buf[4] == 1);
    CHECK(buf[5] == 0);
    CHECK(buf[6] == 56);
    CHECK(buf[7] == 0);
    CHECK(buf[8] == 0xE8);
    CHECK(buf[9] == 0x03);
    CHECK(buf[48] == 0xEF);
    CHECK(buf[55] == 0x01);
    uint32_t crc = af_bsn_crc32(buf, 60);
    CHECK(buf[60] == (crc & 0xFF));
    CHECK(buf[61] == ((crc >> 8) & 0xFF));
    CHECK(buf[62] == ((crc >> 16) & 0xFF));
    CHECK(buf[63] == ((crc >> 24) & 0xFF));

    CHECK(af_bsn_encode(&S, buf, 63) == 0);
    CHECK(af_bsn_encode(&S, NULL, 100) == 0);
    CHECK(af_bsn_encode(&S, buf, 64) == 64);

    af_bs_t T;
    af_bs_init(&T, NULL);
    CHECK(af_bsn_decode(&T, buf, 64) == true);
    CHECK(T.life.connects == S.life.connects);
    CHECK(T.life.disconnects == S.life.disconnects);
    CHECK(T.life.attempts == S.life.attempts);
    CHECK(T.life.fails == S.life.fails);
    for (int i = 0; i < AF_BS_REASONS; i++) {
        CHECK(T.life.reason[i] == S.life.reason[i]);
    }
    CHECK(T.life.conn_ms == S.life.conn_ms);
    CHECK(T.boot.connects == 0);
}

static void t_nvs_corrupt(void) {
    af_bs_init(&S, ring);
    S.life.connects = 5;
    S.life.conn_ms = 999;
    uint8_t buf[100];
    size_t n = af_bsn_encode(&S, buf, 100);
    CHECK(n == 64);

    int bad_indices[] = {0, 4, 6, 10, 57, 60};
    for (int i = 0; i < 6; i++) {
        uint8_t bad[64];
        memcpy(bad, buf, 64);
        bad[bad_indices[i]] ^= 0x01;
        if (bad_indices[i] < 8) {   /* заголовок: CRC пересчитан, отказ обязан дать сама проверка заголовка */
            uint32_t c = af_bsn_crc32(bad, 60);
            for (int b = 0; b < 4; b++) bad[60 + b] = (uint8_t)(c >> (8 * b));
        }
        af_bs_t T;
        af_bs_init(&T, NULL);
        T.life.connects = 77;
        T.life.conn_ms = 77;
        CHECK(af_bsn_decode(&T, bad, 64) == false);
        CHECK(T.life.connects == 0);
        CHECK(T.life.conn_ms == 0);
        for (int j = 0; j < AF_BS_REASONS; j++) {
            CHECK(T.life.reason[j] == 0);
        }
    }

    af_bs_t T;
    af_bs_init(&T, NULL);
    T.life.connects = 77;
    T.life.conn_ms = 77;
    CHECK(af_bsn_decode(&T, buf, 63) == false);
    CHECK(T.life.connects == 0);
    CHECK(T.life.conn_ms == 0);

    af_bs_init(&T, NULL);
    T.life.connects = 77;
    T.life.conn_ms = 77;
    CHECK(af_bsn_decode(&T, buf, 65) == false);
    CHECK(T.life.connects == 0);
    CHECK(T.life.conn_ms == 0);

    af_bs_init(&T, NULL);
    T.life.connects = 77;
    T.life.conn_ms = 77;
    CHECK(af_bsn_decode(&T, buf, 0) == false);
    CHECK(T.life.connects == 0);
    CHECK(T.life.conn_ms == 0);

    af_bs_init(&T, NULL);
    T.life.connects = 77;
    T.life.conn_ms = 77;
    CHECK(af_bsn_decode(&T, NULL, 64) == false);
    CHECK(T.life.connects == 0);
    CHECK(T.life.conn_ms == 0);

    uint8_t zeros[64];
    memset(zeros, 0, 64);
    af_bs_init(&T, NULL);
    T.life.connects = 77;
    T.life.conn_ms = 77;
    CHECK(af_bsn_decode(&T, zeros, 64) == false);
    CHECK(T.life.connects == 0);
    CHECK(T.life.conn_ms == 0);
}

static void t_crc_vector(void) {
    CHECK(af_bsn_crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);
    CHECK(af_bsn_crc32((const uint8_t*)"", 0) == 0);
}

static void t_nvs_due(void) {
    af_bs_init(&S, ring);
    CHECK(af_bsn_due(&S, 10000000) == false);

    af_bs_on_attempt(&S, 10);
    CHECK(af_bsn_due(&S, 599999) == false);
    CHECK(af_bsn_due(&S, 600000) == true);
    af_bsn_saved(&S, 700000);
    CHECK(S.dirty == false);
    CHECK(af_bsn_due(&S, 10000000) == false);

    af_bs_on_attempt(&S, 700001);
    CHECK(af_bsn_due(&S, 700000 + 599999) == false);
    CHECK(af_bsn_due(&S, 700000 + 600000) == true);
    af_bsn_saved(&S, 1000000);

    af_bs_on_connect(&S, 1000100, 23);
    af_bs_on_disconnect(&S, 1010000, 0, 0x13);
    CHECK(af_bsn_due(&S, 1029999) == false);
    CHECK(af_bsn_due(&S, 1030000) == true);
    af_bsn_saved(&S, 1030000);
    CHECK(S.disc_pending == false);

    af_bs_on_attempt(&S, 1030001);
    CHECK(af_bsn_due(&S, 1030000 + 30000) == false);
    CHECK(af_bsn_due(&S, 1030000 + 600000) == true);
    CHECK(af_bsn_due(&S, 1029999) == false);
}

static void t_life_accumulates(void) {
    af_bs_t T;
    af_bs_init(&T, NULL);
    T.life.connects = 100;
    T.life.disconnects = 99;
    T.life.conn_ms = 5000;
    uint8_t buf[100];
    size_t n = af_bsn_encode(&T, buf, 100);
    CHECK(n == 64);

    af_bs_init(&S, ring);
    CHECK(af_bsn_decode(&S, buf, 64) == true);
    af_bs_on_attempt(&S, 500);
    af_bs_on_connect(&S, 1000, 23);
    af_bs_on_disconnect(&S, 4000, 0, 0x16);

    CHECK(S.life.connects == 101);
    CHECK(S.life.disconnects == 100);
    CHECK(S.life.attempts == 1);
    CHECK(S.life.conn_ms == 8000);
    CHECK(S.boot.connects == 1);
    CHECK(S.boot.conn_ms == 3000);
    CHECK(S.life.reason[AF_BS_R_LOCAL] == 1);

    uint8_t buf2[100];
    n = af_bsn_encode(&S, buf2, 100);
    CHECK(n == 64);
    af_bs_t R;
    af_bs_init(&R, NULL);
    CHECK(af_bsn_decode(&R, buf2, 64) == true);
    CHECK(R.life.connects == 101);
    CHECK(R.life.conn_ms == 8000);
}

static void t_sizeof(void) {
    CHECK(sizeof(af_bs_t) <= 2048);
    CHECK(sizeof(af_bs_sess_t) == 28);
}

static void t_real_intervals(void) {
    int n = load_ts(TS, 12000);
    CHECK(n == 10900);
    uint64_t tm[12000] = {0};
    for (int i = 0; i < n; i++) {
        tm[i] = 1000000 + (uint64_t)(TS[i] - TS[0]) * 1000;
    }

    af_bs_init(&S, ring);
    af_bs_on_connect(&S, tm[0] - 1000, 247);
    for (int i = 0; i < n; i++) {
        af_bs_on_packet(&S, tm[i], true);
    }
    af_bs_snap_t s = snap(tm[n - 1]);

    double imin = 1e18, imax = 0, sum = 0, sumsq = 0;
    for (int i = 1; i < n; i++) {
        double d = (double)(TS[i] - TS[i - 1]) * 1000.0;
        if (d < imin) imin = d;
        if (d > imax) imax = d;
        sum += d;
        sumsq += d * d;
    }
    double mean = sum / (n - 1);
    double var = sumsq / (n - 1) - mean * mean;
    if (var < 0) var = 0;
    double jit = sqrt(var);

    CHECK(s.i_min_ms == (int32_t)imin);
    CHECK(s.i_max_ms == (int32_t)imax);
    CHECK(near(s.i_avg_ms, mean, 1e-2));
    CHECK(near(s.jitter_ms, jit, 0.05));

    uint64_t dec_idx = tm[n - 1] / 10000;
    int n10 = 0;
    double sum10 = 0, min10 = 1e18, max10 = 0;
    for (int i = 1; i < n; i++) {
        if (tm[i] / 10000 >= dec_idx - 59) {
            double d = (double)(TS[i] - TS[i - 1]) * 1000.0;
            n10++;
            sum10 += d;
            if (d < min10) min10 = d;
            if (d > max10) max10 = d;
        }
    }
    CHECK(s.i10_min_ms == (int32_t)min10);
    CHECK(s.i10_max_ms == (int32_t)max10);
    CHECK(near(s.i10_avg_ms, sum10 / n10, 1e-2));
    CHECK(near(s.pps_sess, (double)n * 1000.0 / (double)(tm[n - 1] - (tm[0] - 1000)), 1e-4));
}

static void t_real_sessions(void) {
    int n = load_ts(TS, 12000);
    CHECK(n == 10900);
    uint64_t tm[12000] = {0};
    for (int i = 0; i < n; i++) {
        tm[i] = 1000000 + (uint64_t)(TS[i] - TS[0]) * 1000;
    }

    uint32_t K = 0;
    int starts[100], ends[100];
    int a = 0;
    for (int i = 1; i < n; i++) {
        if (TS[i] - TS[i - 1] >= 10) {
            starts[K] = a;
            ends[K] = i - 1;
            K++;
            a = i;
        }
    }
    starts[K] = a;
    ends[K] = n - 1;
    K++;

    CHECK(K == 24);

    af_bs_init(&S, ring);
    uint64_t ref_conn = 0;
    for (uint32_t k = 0; k < K; k++) {
        af_bs_on_connect(&S, tm[starts[k]] - 1000, 247);
        for (int i = starts[k]; i <= ends[k]; i++) {
            af_bs_on_packet(&S, tm[i], true);
        }
        af_bs_on_disconnect(&S, tm[ends[k]] + 2000, 100 + k, 0x08);
        ref_conn += (tm[ends[k]] - tm[starts[k]]) + 3000;
    }

    CHECK(S.boot.connects == K);
    CHECK(S.boot.disconnects == K);
    CHECK(S.boot.reason[AF_BS_R_TIMEOUT] == K);
    CHECK(S.pkts_boot == 10900);
    CHECK(S.ring_n == K);
    uint32_t sum_pkts = 0;
    for (uint32_t i = 0; i < K; i++) {
        const af_bs_sess_t *rec = af_bs_ring_get(&S, i);
        CHECK(rec != NULL);
        sum_pkts += rec->pkts;
    }
    CHECK(sum_pkts == 10900);
    const af_bs_sess_t *r0 = af_bs_ring_get(&S, 0);
    CHECK(r0 != NULL);
    CHECK(r0->end_utc == 100 + K - 1);
    CHECK(S.boot.conn_ms == ref_conn);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

static void t_real_ring_stats(void) {
    int n = load_ts(TS, 12000);
    CHECK(n == 10900);
    uint64_t tm[12000] = {0};
    for (int i = 0; i < n; i++) {
        tm[i] = 1000000 + (uint64_t)(TS[i] - TS[0]) * 1000;
    }

    uint32_t K = 0;
    int starts[100], ends[100];
    int a = 0;
    for (int i = 1; i < n; i++) {
        if (TS[i] - TS[i - 1] >= 10) {
            starts[K] = a;
            ends[K] = i - 1;
            K++;
            a = i;
        }
    }
    starts[K] = a;
    ends[K] = n - 1;
    K++;

    uint32_t durs[100];
    uint64_t sum = 0;
    for (uint32_t k = 0; k < K; k++) {
        durs[k] = (uint32_t)((tm[ends[k]] - tm[starts[k]]) + 3000);
        sum += durs[k];
    }

    af_bs_init(&S, ring);
    for (uint32_t i = 0; i < K; i++) {
        ring[i].dur_ms = durs[i];
    }
    S.ring_n = (uint8_t)K;
    S.ring_head = (uint8_t)(K % 32);

    uint32_t sorted[100];
    memcpy(sorted, durs, K * sizeof(uint32_t));
    qsort(sorted, K, sizeof(uint32_t), cmp_u32);

    uint32_t avg = sum / K;
    uint32_t median;
    if (K % 2 == 0) {
        median = (sorted[K / 2 - 1] + sorted[K / 2]) / 2;
    } else {
        median = sorted[K / 2];
    }

    af_bs_snap_t s = snap(1000000);
    CHECK(s.avg_ms == avg);
    CHECK(s.median_ms == median);
    CHECK(s.ring_n == K);
}

/* окна не на границе корзин: знаменатель = (K-1)*B + остаток от времени */
static void t_pps_phase(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 700000, 23);
    for (int k = 1; k <= 301; k++) af_bs_on_packet(&S, 700000 + 2000ull * k, true);
    af_bs_snap_t s = snap(1303500);
    CHECK(near(s.pps_inst, 5000.0 / 9500.0, 1e-4));
    CHECK(near(s.pps_1m, 27000.0 / 53500.0, 1e-4));
    CHECK(near(s.pps_10m, 297000.0 / 593500.0, 1e-4));
}

/* интервал > 65535 мс обрезается до 16 бит, сумма корзины насыщается */
static void t_dec_clip(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_packet(&S, 1000, true);
    af_bs_on_packet(&S, 100000, true);
    af_bs_on_packet(&S, 102000, true);
    af_bs_on_packet(&S, 104000, true);
    af_bs_snap_t s = snap(104000);
    CHECK(s.i10_max_ms == 65535);
    CHECK(s.i10_min_ms == 2000);
    CHECK(near(s.i10_avg_ms, 65535.0 / 3.0, 1e-2));
}

/* время назад при открытой сессии не искажает время на связи и окна */
static void t_tick_back(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 1000, 23);
    af_bs_on_packet(&S, 2000, true);
    af_bs_on_packet(&S, 4000, true);
    af_bs_tick(&S, 5000);
    af_bs_tick(&S, 3000);
    CHECK(S.boot.conn_ms == 4000);
    af_bs_snap_t s = snap(6000);
    CHECK(S.boot.conn_ms == 5000);
    CHECK(near(s.pps_inst, 2000.0 / 6000.0, 1e-4));
}

static void t_local_close_code0(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_mark_local_close(&S);
    af_bs_on_disconnect(&S, 5000, 0, 0);
    CHECK(S.last_reason == 0x16);
    CHECK(S.boot.reason[AF_BS_R_LOCAL] == 1);
    CHECK(S.boot.reason[AF_BS_R_OTHER] == 0);
    CHECK(af_bs_ring_get(&S, 0)->reason == 0x16);
}

static void t_remote_code0_other(void) {
    af_bs_init(&S, ring);
    af_bs_on_connect(&S, 0, 23);
    af_bs_on_disconnect(&S, 5000, 0, 0);
    CHECK(S.last_reason == 0);
    CHECK(S.boot.reason[AF_BS_R_OTHER] == 1);
    CHECK(S.boot.reason[AF_BS_R_LOCAL] == 0);
    af_bs_on_connect(&S, 6000, 23);
    af_bs_mark_local_close(&S);
    af_bs_on_disconnect(&S, 7000, 0, 0x13);
    CHECK(S.last_reason == 0x13);
}

static void t_local_flag_reset(void) {
    af_bs_init(&S, ring);
    af_bs_mark_local_close(&S);
    CHECK(S.local_close == false);
    af_bs_on_connect(&S, 0, 23);
    af_bs_mark_local_close(&S);
    af_bs_on_disconnect(&S, 5000, 0, 0);
    CHECK(S.local_close == false);
    af_bs_on_connect(&S, 6000, 23);
    af_bs_mark_local_close(&S);
    af_bs_on_disconnect(&S, 9000, 0, 0x13);
    CHECK(S.local_close == false);
}

static const uint8_t NVS_GOLD[64] = {
0x41, 0x53, 0x42, 0x31, 0x01, 0x00, 0x38, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xe7, 0x03, 0x00, 0x00,
0xdc, 0x05, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00, 0x00, 0xe8, 0xe2, 0xa8, 0x4e,
};

static void ext_drive(af_bs_t *s)
{
    int i;

    af_bs_on_attempt(s, 500);
    af_bs_on_connect(s, 1000, 247);
    af_bs_on_scan_start(s, 600);
    af_bs_on_scan_adv(s);
    af_bs_on_scan_hit(s, 700);
    af_bs_on_scan_stop(s);
    for (i = 0; i < 1000; i++) {
        uint64_t now = 1000 + (uint64_t)i * 2000;
        af_bs_on_packet(s, now, (i % 10) != 9);
        af_bs_on_rssi(s, (int8_t)(-40 - (i * 7) % 70));
    }
    af_bs_tick(s, 1000 + 1000 * 2000 + 500);
    af_bs_on_disconnect(s, 1000 + 1000 * 2000 + 1000, 1761868800u, 0x08);
}

static int ext_empty(const af_bs_t *s)
{
    int i;
    int k;

    if (s->win_init)
        return 0;
    for (i = 0; i < 10; i++) {
        if (s->sec[i] != 0)
            return 0;
    }
    for (k = 0; k < 60; k++) {
        if (s->dec[k].pk != 0)
            return 0;
        if (s->dec[k].n != 0)
            return 0;
        if (s->dec[k].isum != 0)
            return 0;
        if (s->dec[k].imax != 0)
            return 0;
        if (s->dec[k].imin != 0xFFFF)
            return 0;
    }
    for (i = 0; i < 24; i++) {
        if (s->hour_ms[i] != 0)
            return 0;
    }
    for (i = 0; i < 8; i++) {
        if (s->rssi_hist[i] != 0)
            return 0;
    }
    if (s->rssi_n != 0)
        return 0;
    if (s->i_n != 0)
        return 0;
    if (s->pauses != 0)
        return 0;
    if (s->has_dur)
        return 0;
    if (s->ring_n != 0)
        return 0;
    if (s->scan_n != 0)
        return 0;
    if (s->scan_adv != 0)
        return 0;
    if (s->scan_cand != 0)
        return 0;
    if (s->ring != NULL) {
        const unsigned char *p = (const unsigned char *)s->ring;
        const unsigned char *end = p + sizeof(af_bs_sess_t) * 32u;
        while (p != end) {
            if (*p != 0)
                return 0;
            p++;
        }
    }
    return 1;
}

static void t_ext_default(void)
{
    (af_bs_init)(&S, ring);
    CHECK(S.ext == false);
    CHECK(ext_empty(&S));
}

static void t_ext_off_basic(void)
{
    static af_bs_t A, B;
    static af_bs_sess_t ra[32], rb[32];

    (af_bs_init)(&A, ra);
    af_bs_set_ext(&A, false);
    (af_bs_init)(&B, rb);
    af_bs_set_ext(&B, true);

    ext_drive(&A);
    ext_drive(&B);

    CHECK(ext_empty(&A));

    CHECK(A.pkts_boot == 1000);
    CHECK(A.bad_boot == 100);
    CHECK(A.has_bad);
    CHECK(A.boot.connects == 1);
    CHECK(A.boot.disconnects == 1);
    CHECK(A.boot.attempts == 1);
    CHECK(A.life.connects == 1);
    CHECK(A.last_reason == 0x08);
    CHECK(A.last_reason_valid);
    CHECK(A.boot.reason[AF_BS_R_TIMEOUT] == 1);
    CHECK(A.boot.conn_ms == 1000 + 1000 * 2000 + 1000 - 1000);

    CHECK(ext_empty(&B) == 0);
    CHECK(B.rssi_n == 1000);
    CHECK(B.ring_n == 1);

    CHECK(A.pkts_boot == B.pkts_boot);
    CHECK(A.bad_boot == B.bad_boot);
    CHECK(A.last_bad_ms == B.last_bad_ms);
    CHECK(A.last_reason == B.last_reason);
    CHECK(A.rssi_cur == B.rssi_cur);
    CHECK(A.mtu == B.mtu);
    CHECK(A.acc_ms == B.acc_ms);
    CHECK(memcmp(&A.boot, &B.boot, sizeof A.boot) == 0);
    CHECK(memcmp(&A.life, &B.life, sizeof A.life) == 0);

    (af_bs_init)(&A, ra);
    af_bs_set_ext(&A, false);
    af_bs_on_connect(&A, 1000, 100);
    af_bs_on_rssi(&A, -77);
    CHECK(A.rssi_cur == -77);
    CHECK(A.mtu == 100);
    CHECK(A.rssi_n == 0);
}

static void t_ext_toggle(void)
{
    static af_bs_t A;
    static af_bs_sess_t ra[32];

    (af_bs_init)(&A, ra);
    af_bs_set_ext(&A, true);
    ext_drive(&A);
    CHECK(!ext_empty(&A));

    uint32_t life_conn = A.life.connects;
    uint32_t life_disc = A.life.disconnects;
    uint32_t life_att  = A.life.attempts;
    uint32_t rs        = A.life.reason[AF_BS_R_TIMEOUT];
    uint64_t cms       = A.life.conn_ms;
    uint32_t pb        = A.pkts_boot;

    af_bs_set_ext(&A, false);
    CHECK(ext_empty(&A));
    CHECK(A.ext == false);
    CHECK(A.life.connects == life_conn);
    CHECK(A.life.disconnects == life_disc);
    CHECK(A.life.attempts == life_att);
    CHECK(A.life.reason[AF_BS_R_TIMEOUT] == rs);
    CHECK(A.life.conn_ms == cms);
    CHECK(A.pkts_boot == pb);

    af_bs_set_ext(&A, true);
    CHECK(A.ext == true);

    af_bs_on_connect(&A, 10000000u, 247);
    af_bs_on_packet(&A, 10000100u, true);
    af_bs_on_packet(&A, 10002100u, true);
    af_bs_set_ext(&A, true);
    CHECK(A.i_n == 1);

    af_bs_set_ext(&A, false);
    CHECK(ext_empty(&A));
    CHECK(A.boot.connects == 2);
    CHECK(A.life.connects == life_conn + 1);
}

static void t_ext_snapshot_flag(void)
{
    static af_bs_t A;
    static af_bs_sess_t ra[32];
    af_bs_snap_t o;

    (af_bs_init)(&A, ra);
    af_bs_snapshot(&A, 5000, &o);
    CHECK(o.ext == false);

    af_bs_set_ext(&A, true);
    af_bs_snapshot(&A, 5000, &o);
    CHECK(o.ext == true);

    af_bs_set_ext(&A, false);
    af_bs_snapshot(&A, 5000, &o);
    CHECK(o.ext == false);
}

static void t_ext_size(void)
{
    printf("INFO x86-64, \xd0\xbd\xd0\xb5 \xd0\xbf\xd0\xbb\xd0\xb0\xd1\x82\xd0\xb0: sizeof(af_bs_t)=%zu sizeof(af_bs_snap_t)=%zu\n",
           sizeof(af_bs_t), sizeof(af_bs_snap_t));
    CHECK(sizeof(af_bs_t) <= 2048);
}

static void t_ext_nvs_blob(void)
{
    static af_bs_t A;
    (af_bs_init)(&A, NULL);

    A.life.connects    = 1000;
    A.life.disconnects = 999;
    A.life.attempts    = 1500;
    A.life.fails       = 7;
    A.life.reason[0] = 1;
    A.life.reason[1] = 2;
    A.life.reason[2] = 3;
    A.life.reason[3] = 4;
    A.life.reason[4] = 5;
    A.life.reason[5] = 6;
    A.life.conn_ms    = 0x0123456789ABCDEFull;

    uint8_t b0[64], b1[64];

    CHECK(af_bsn_encode(&A, b0, 64) == 64);

    af_bs_set_ext(&A, true);
    CHECK(af_bsn_encode(&A, b1, 64) == 64);

    CHECK(memcmp(b0, b1, 64) == 0);
    CHECK(memcmp(b0, NVS_GOLD, 64) == 0);

    af_bs_t T;
    (af_bs_init)(&T, NULL);
    CHECK(af_bsn_decode(&T, b0, 64));
    CHECK(T.life.connects == 1000);
    CHECK(T.life.conn_ms == 0x0123456789ABCDEFull);
}

int main(void) {
    printf("INFO sizeof(af_bs_t)=%zu sizeof(af_bs_sess_t)=%zu\n", sizeof(af_bs_t), sizeof(af_bs_sess_t));
    RUN(t_reason_table);
    RUN(t_counts_flow);
    RUN(t_setup_ms);
    RUN(t_fail_streak);
    RUN(t_dur_mono);
    RUN(t_ring_stats);
    RUN(t_ring_overflow);
    RUN(t_pps_ref);
    RUN(t_pps_early);
    RUN(t_idle_nd);
    RUN(t_intervals_basic);
    RUN(t_bad_pkts);
    RUN(t_last_pkt_age);
    RUN(t_rssi_hist);
    RUN(t_rssi_round);
    RUN(t_conn_params);
    RUN(t_scan);
    RUN(t_uptime_pct);
    RUN(t_window_rollover);
    RUN(t_idempotent);
    RUN(t_ring_null);
    RUN(t_nvs_roundtrip);
    RUN(t_nvs_corrupt);
    RUN(t_crc_vector);
    RUN(t_nvs_due);
    RUN(t_life_accumulates);
    RUN(t_sizeof);
    RUN(t_pps_phase);
    RUN(t_dec_clip);
    RUN(t_tick_back);
    RUN(t_real_intervals);
    RUN(t_real_sessions);
    RUN(t_real_ring_stats);
    RUN(t_local_close_code0);
    RUN(t_remote_code0_other);
    RUN(t_local_flag_reset);
    RUN(t_up24_ext_off);
    RUN(t_ext_default);
    RUN(t_ext_off_basic);
    RUN(t_ext_toggle);
    RUN(t_ext_snapshot_flag);
    RUN(t_ext_size);
    RUN(t_ext_nvs_blob);
    TESTS_DONE();
    return 0;
}
