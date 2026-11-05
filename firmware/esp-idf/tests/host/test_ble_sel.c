#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include "ble_sel_cmd.h"
#include "ble_devsw.h"

static ble_sel_t S;
static ble_acts_t O;

#ifndef BLE_SEL_HAS_LABEL_GATE
/* старая оболочка ble_gw.c: проверки метки нет, пакет пишется при dec.store */
static inline bool ble_sel_label_ok(const ble_sel_t *s, bool have, const uint8_t l[6]) { (void)s; (void)have; (void)l; return true; }
#endif

static const uint8_t A[6]  = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
static const uint8_t A2[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF4};
static const uint8_t B[6]  = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};
static const uint8_t C[6]  = {0xA0, 0x6C, 0x5E, 0x13, 0x77, 0x0A};

static int has(uint8_t kind) { return ble_acts_find(&O, kind) >= 0; }
static const ble_act_t *act(uint8_t kind) {
    int i = ble_acts_find(&O, kind);
    return i >= 0 ? &O.a[i] : NULL;
}
static int eq6(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

static void mk_live_A(uint32_t t) {
    ble_sel_init(&S, A, 0, "AtomFast ", t, &O);
    ble_sel_on_open_ok(&S, 3, A, t + 10, &O);
    ble_sel_on_write_descr_ok(&S, t + 20, &O);
}

static void mk_scan_BC(uint32_t t) {
    ble_sel_init(&S, NULL, 0, NULL, t, &O);
    ble_sel_on_scan_start(&S, true, t, &O);
    ble_sel_on_scan_result(&S, B, 0, -60, "AtomFast ", true, t + 100, &O);
    ble_sel_on_scan_result(&S, C, 0, -70, "AtomSwift", true, t + 200, &O);
}

static void sel_from_scan(uint8_t tok, uint32_t t) {
    ble_sel_cmd_select(&S, S.gen, tok, true, false, 0, t, &O);
    ble_sel_on_scan_end(&S, t + 10, &O);
}

/* ── T1 ─────────────────────────────────────────────────────────────────── */
static void T1_scan_match_live_keeps_mac(void) {
    uint8_t m[6];
    mk_live_A(0);

    ble_sel_on_scan_result(&S, B, 0, -60, "AtomFast ", true, 100, &O);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_sel_tick(&S, 25000, &O);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_sel_tick(&S, 200000, &O);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_sel_cmd_rescan(&S, false, false, 300000, &O);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_acts_reset(&O);
    ble_sel_scan_begin(&S, 300100, &O);
    CHECK(!has(BLE_ACT_SCAN_START));

    CHECK(eq6(S.conn_addr, A));
    CHECK(S.has_tgt);
    CHECK(eq6(S.tgt_addr, A));
    CHECK(ble_sel_data_mac(&S, m));
    CHECK(eq6(m, A));
}

/* ── T2 ─────────────────────────────────────────────────────────────────── */
static void T2_identity_from_open_event(void) {
    static const uint8_t zero6[6] = {0};

    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_on_scan_start(&S, true, 0, &O);
    ble_sel_on_scan_result(&S, A, 0, -60, "AtomFast ", true, 100, &O);

    sel_from_scan(1, 200);
    CHECK(has(BLE_ACT_OPEN));
    CHECK(act(BLE_ACT_OPEN) != NULL);
    CHECK(act(BLE_ACT_OPEN) != NULL && eq6(act(BLE_ACT_OPEN)->addr, A));

    ble_sel_on_open_ok(&S, 9, A2, 300, &O);
    CHECK(act(BLE_ACT_CLOSE_EVT_CONN) != NULL);
    CHECK(act(BLE_ACT_CLOSE_EVT_CONN) != NULL && act(BLE_ACT_CLOSE_EVT_CONN)->conn_id == 9);
    CHECK(S.ident_mismatch == 1);
    CHECK(S.connected == 0);
    CHECK(eq6(S.conn_addr, zero6));
}

/* ── T3 ─────────────────────────────────────────────────────────────────── */
static void T3_stale_tick(void) {
    /* (a) fail → wait → retry → live; ticks in live are no-ops */
    ble_sel_init(&S, A, 0, "AtomFast ", 1000, &O);
    ble_sel_on_open_fail(&S, 1100, &O);
    CHECK(S.state == BLE_ST_WAIT);

    ble_sel_tick(&S, S.next_try_ms, &O);
    CHECK(has(BLE_ACT_OPEN));

    ble_sel_on_open_ok(&S, 3, A, 1800, &O);
    ble_sel_on_write_descr_ok(&S, 1850, &O);
    CHECK(S.state == BLE_ST_LIVE);

    ble_sel_tick(&S, 2000, &O);
    CHECK(O.n == 0);
    ble_sel_tick(&S, 70000, &O);
    CHECK(O.n == 0);

    /* (b) high fail_streak in LIVE must not trigger scan */
    S.fail_streak = 9;
    ble_sel_tick(&S, 80000, &O);
    CHECK(O.n == 0);

    /* (c) sub_fail keeps connected; no retry until disc */
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    ble_sel_on_open_ok(&S, 3, A, 10, &O);
    ble_sel_on_sub_fail(&S, 20, &O);
    CHECK(S.state == BLE_ST_WAIT);
    CHECK(S.connected == 1);

    ble_sel_tick(&S, S.next_try_ms + 1, &O);
    CHECK(O.n == 0);

    ble_sel_on_disc(&S, 3, 30, &O);
    ble_sel_tick(&S, S.next_try_ms + 1, &O);
    CHECK(has(BLE_ACT_OPEN));
}

/* ── T4 ─────────────────────────────────────────────────────────────────── */
static void T4_no_autoconnect(void) {
    uint8_t m[6];

    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    CHECK(has(BLE_ACT_SCAN_START));
    CHECK(!has(BLE_ACT_OPEN));

    ble_sel_on_scan_start(&S, true, 0, &O);
    CHECK(!has(BLE_ACT_OPEN));

    ble_sel_on_scan_result(&S, B, 0, -60, "AtomFast ", true, 100, &O);
    CHECK(!has(BLE_ACT_OPEN));

    ble_sel_on_scan_result(&S, C, 0, -70, "AtomSwift", true, 200, &O);
    CHECK(!has(BLE_ACT_OPEN));

    ble_sel_on_scan_end(&S, 30000, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);
    CHECK(!has(BLE_ACT_OPEN));

    ble_sel_on_scan_end(&S, 30100, &O);
    CHECK(!has(BLE_ACT_OPEN));

    CHECK(S.has_tgt == 0);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(ble_sel_data_mac(&S, m) == false);
}

/* ── T5 ─────────────────────────────────────────────────────────────────── */
static void T5_select_by_token(void) {
    /* (a) select tok 2 → C */
    mk_scan_BC(0);
    CHECK(ble_sel_cmd_select(&S, S.gen, 2, true, false, 0, 300, &O) == BLE_OK);
    CHECK(has(BLE_ACT_SCAN_STOP));
    ble_sel_on_scan_end(&S, 310, &O);
    CHECK(has(BLE_ACT_OPEN));
    CHECK(act(BLE_ACT_OPEN) != NULL && eq6(act(BLE_ACT_OPEN)->addr, C));

    /* (b) stale gen and bad tok */
    mk_scan_BC(0);
    CHECK(ble_sel_cmd_select(&S, S.gen + 1, 1, true, false, 0, 300, &O) == BLE_E_STALE);
    CHECK(ble_sel_cmd_select(&S, S.gen, 77, true, false, 0, 300, &O) == BLE_E_NO_CAND);

    /* (c) 9 results, 9th evicts 1st */
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_on_scan_start(&S, true, 0, &O);
    for (int k = 1; k <= 9; k++) {
        uint8_t dk[6] = {0xA0, 0x6C, 0x10, 0x20, 0x30, (uint8_t)k};
        ble_sel_on_scan_result(&S, dk, 0, -60, "AtomFast ", true, 100u + (uint32_t)k, &O);
    }
    CHECK(ble_sel_cmd_select(&S, S.gen, 9, true, false, 0, 500, &O) == BLE_OK);
    ble_sel_on_scan_end(&S, 510, &O);
    CHECK(has(BLE_ACT_OPEN));
    {
        uint8_t d9[6] = {0xA0, 0x6C, 0x10, 0x20, 0x30, 9};
        CHECK(act(BLE_ACT_OPEN) != NULL && eq6(act(BLE_ACT_OPEN)->addr, d9));
    }

    /* (d) select while LIVE is rejected */
    mk_live_A(0);
    CHECK(ble_sel_cmd_select(&S, S.gen, 1, true, false, 0, 100, &O) == BLE_E_LIVE);
}

/* ── T7 ─────────────────────────────────────────────────────────────────── */
static void T7_scan_timing(void) {
    /* (a) scan deadline exactly at t0+20000 */
    ble_sel_init(&S, NULL, 0, NULL, 1000, &O);
    ble_sel_on_scan_start(&S, true, 1000, &O);

    ble_sel_tick(&S, 20999, &O);
    CHECK(S.state == BLE_ST_SCAN);
    CHECK(!has(BLE_ACT_SCAN_STOP));

    ble_sel_tick(&S, 21000, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);
    CHECK(has(BLE_ACT_SCAN_STOP));

    /* (b) cooldown after scan end */
    ble_sel_on_scan_end(&S, 21100, &O);
    CHECK(S.state == BLE_ST_LIST);

    CHECK(ble_sel_cmd_rescan(&S, false, false, 24099, &O) == BLE_E_COOLDOWN);
    CHECK(ble_sel_cmd_rescan(&S, false, false, 24100, &O) == BLE_OK);
    CHECK(has(BLE_ACT_SCAN_START));

    /* (c) scan-start failure */
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_on_scan_start(&S, false, 10, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(S.err == BLE_NOTE_SCAN_START_FAILED);
}

/* ── T8 ─────────────────────────────────────────────────────────────────── */
static void T8_reconnect_only_to_tgt(void) {
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);

    uint32_t t = 100;
    for (int k = 0; k <= 20; k++) {
        ble_sel_on_open_fail(&S, t, &O);
        CHECK(S.state == BLE_ST_WAIT);
        uint32_t d = S.next_try_ms - t;
        CHECK(d <= 60000u);
        uint32_t expected = (600u << (uint32_t)k) < 60000u ? (600u << (uint32_t)k) : 60000u;
        CHECK(d == expected);

        ble_sel_tick(&S, S.next_try_ms, &O);
        CHECK(O.n == 1);
        CHECK(has(BLE_ACT_OPEN));
        CHECK(act(BLE_ACT_OPEN) != NULL && eq6(act(BLE_ACT_OPEN)->addr, A));
        CHECK(!has(BLE_ACT_SCAN_START));

        t = S.next_try_ms + 10;
    }

    ble_sel_on_open_ok(&S, 3, A, t, &O);
    ble_sel_on_write_descr_ok(&S, t + 10, &O);
    CHECK(S.state == BLE_ST_LIVE);
    CHECK(S.delay_ms == 600);
    CHECK(S.fail_streak == 0);
}

/* ── T9 ─────────────────────────────────────────────────────────────────── */
static void T9_forget_flow(void) {
    /* (a) forget while LIVE with connection */
    mk_live_A(0);
    CHECK(ble_sel_cmd_forget(&S, true, false, 100, &O) == BLE_OK);
    CHECK(S.conn_valid == 0);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(!has(BLE_ACT_DEVSW));

    ble_sel_on_disc(&S, 3, 200, &O);
    CHECK(S.state == BLE_ST_FORGETTING);
    CHECK(act(BLE_ACT_DEVSW) != NULL);
    CHECK(act(BLE_ACT_DEVSW) != NULL && act(BLE_ACT_DEVSW)->op == BLE_OP_FORGET);
    CHECK(act(BLE_ACT_DEVSW) != NULL && act(BLE_ACT_DEVSW)->seq == S.seq);

    /* (b) repeat forget in FORGETTING is no-op */
    CHECK(ble_sel_cmd_forget(&S, true, false, 300, &O) == BLE_OK);
    CHECK(O.n == 0);

    /* (c) devsw done → SCAN */
    ble_sel_on_devsw_done(&S, S.seq, 0, 0, false, 400, &O);
    CHECK(S.state == BLE_ST_SCAN);
    CHECK(S.has_tgt == 0);
    CHECK(has(BLE_ACT_SCAN_START));

    /* (d) forget during SCAN → SCAN_STOPPING → FORGETTING */
    ble_sel_init(&S, NULL, 0, NULL, 1000, &O);
    CHECK(ble_sel_cmd_forget(&S, true, false, 1100, &O) == BLE_OK);
    CHECK(has(BLE_ACT_SCAN_STOP));
    CHECK(S.state == BLE_ST_SCAN_STOPPING);

    ble_sel_on_scan_end(&S, 1200, &O);
    CHECK(S.state == BLE_ST_FORGETTING);
    CHECK(act(BLE_ACT_DEVSW) != NULL);
    CHECK(act(BLE_ACT_DEVSW) != NULL && act(BLE_ACT_DEVSW)->op == BLE_OP_FORGET);

    /* (e) forget without confirm in LIVE → BLE_E_BAD */
    mk_live_A(2000);
    CHECK(ble_sel_cmd_forget(&S, false, false, 2100, &O) == BLE_E_BAD);
    CHECK(S.state == BLE_ST_LIVE);
    CHECK(O.n == 0);
}

/* ── T10 ────────────────────────────────────────────────────────────────── */
static void T10_switch_while_live(void) {
    mk_live_A(0);

    CHECK(ble_sel_cmd_rescan(&S, false, false, 100, &O) == BLE_E_NEED_REPLACE);
    CHECK(O.n == 0);

    CHECK(ble_sel_cmd_rescan(&S, true, false, 200, &O) == BLE_OK);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(S.switching == 1);

    ble_sel_on_disc(&S, 3, 300, &O);
    CHECK(S.state == BLE_ST_SCAN);
    CHECK(has(BLE_ACT_SCAN_START));
    CHECK(eq6(S.tgt_addr, A));
}

/* ── T11 ────────────────────────────────────────────────────────────────── */
static void T11_open_fail_new_selection(void) {
    /* (a) open fail → LIST, no target, no devsw */
    mk_scan_BC(0);
    sel_from_scan(1, 300);
    CHECK(has(BLE_ACT_OPEN));
    CHECK(act(BLE_ACT_OPEN) != NULL && eq6(act(BLE_ACT_OPEN)->addr, B));

    ble_sel_on_open_fail(&S, 400, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(S.has_tgt == 0);
    CHECK(!has(BLE_ACT_DEVSW));

    /* (b) open ok (wrong conn) + sub_fail → LIST */
    mk_scan_BC(1000);
    sel_from_scan(1, 1300);
    ble_sel_on_open_ok(&S, 9, B, 1400, &O);
    ble_sel_on_sub_fail(&S, 1500, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(S.has_tgt == 0);
    CHECK(!has(BLE_ACT_DEVSW));

    /* (c) write_descr ok → CONFIRMING; timeout → LIST, INCOMPATIBLE */
    mk_scan_BC(2000);
    sel_from_scan(1, 2300);
    ble_sel_on_open_ok(&S, 9, B, 2400, &O);
    ble_sel_on_write_descr_ok(&S, 2500, &O);
    CHECK(S.state == BLE_ST_CONFIRMING);
    CHECK(!has(BLE_ACT_DEVSW));

    ble_sel_tick(&S, 2500u + 30000u, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(S.has_tgt == 0);
    CHECK(!has(BLE_ACT_DEVSW));
    CHECK(S.err == BLE_NOTE_INCOMPATIBLE);
}

static struct {
    uint8_t gw[6], nvs[6]; int gw_have, conn, disc_at; uint16_t cid; uint32_t seq_cur, now;
    uint16_t job_cid; uint32_t job_seq;            /* исполняемое задание */
    int stale_wr, stale_valid, bad_first, first_n;  /* B02/B03: нарушения */
    uint8_t first_dev[4][6];                        /* прибор каждого записанного первого пакета */
    void (*hook)(void); int bypass, sel_rc;         /* событие внутри очистки колец */
    ble_act_t next; int next_have;                  /* задание, выданное во время исполнения текущего */
    uint8_t slot[6]; int slot_have;                 /* слот первого пакета: прибор-источник */
    char nvs_name[16], ram_name[16]; int name_fail, clear_fail;   /* b29: имя в NVS и в ОЗУ, сбой записи имени, сбой очистки */
} SH;
#ifdef BLE_SEL_HAS_DEVSW_OWNER
static ble_first_tag_t SH_TAG;
#endif

static void sh_disc(void) {
    ble_acts_t d;
    SH.conn = 0;
    ble_sel_on_disc(&S, SH.cid, SH.now, &d);
}

/* B02: запись задания (NVS/ОЗУ) после вытеснения: снимок автомата сменён либо автомат держит/открывает другой прибор */
static void sh_wr(const uint8_t *a) {
    if (!S.devsw_open || S.devsw_seq != SH.job_seq) { SH.stale_wr++; return; }
    if (a != NULL && (S.connected || S.open_inflight) && !eq6(S.connected ? S.conn_addr : S.open_req_addr, a)) SH.stale_wr++;
}

static bool sh_meas(void *c) { (void)c; return false; }
#ifdef BLE_SEL_HAS_DEVSW_OWNER
static bool sh_cur(void *c, uint32_t q) { (void)c; return SH.conn && ble_sel_devsw_fresh(&S, q, SH.job_cid); }
#else
static bool sh_cur(void *c, uint32_t q) { (void)c; return q == SH.seq_cur && SH.conn; }
#endif
#ifdef BLE_DEVSW_HAS_OWNER
static bool sh_own(void *c, uint32_t q) { (void)c; return ble_sel_devsw_owner(&S, q); }
#endif
static void sh_drop(void *c) { (void)c; }
static uint32_t sh_cnt(void *c) { (void)c; return 1520u; }
static int sh_clear(void *c, int *p) {
    (void)c;
    if (SH.disc_at == 4) sh_disc();
    if (SH.hook) { void (*h)(void) = SH.hook; SH.hook = NULL; h(); }
    if (SH.clear_fail) { *p = 0; return -1; }
    *p = 1;
    return 0;
}
static bool sh_mnt(void *c) { (void)c; return true; }
#ifdef BLE_DEVSW_HAS_NAME
static int sh_save(void *c, const uint8_t a[6], uint8_t t, const char *nm) {
    (void)c; (void)t; sh_wr(a); memcpy(SH.nvs, a, 6);
    if (SH.name_fail) { SH.nvs_name[0] = '\0'; return BLE_DEVSW_SAVE_NONAME; }
    snprintf(SH.nvs_name, sizeof SH.nvs_name, "%s", nm);
    return 0;
}
#else
/* модель ble_gw.c:936-941 на 80cce32: имя из общего снимка s_sel.devsw_name, ошибка записи имени не видна */
static int sh_save(void *c, const uint8_t a[6], uint8_t t) {
    (void)c; (void)t; sh_wr(a); memcpy(SH.nvs, a, 6);
    if (!SH.name_fail) snprintf(SH.nvs_name, sizeof SH.nvs_name, "%s", S.devsw_name);
    return 0;
}
#endif
static int sh_tclr(void *c) { (void)c; sh_wr(NULL); memset(SH.nvs, 0, 6); memset(SH.nvs_name, 0, sizeof SH.nvs_name); return 0; }
static void sh_commit(void *c) { (void)c; }
static void sh_ram(void *c, const ble_devsw_req_t *r) {
    (void)c;
    sh_wr(r->op == BLE_OP_FORGET ? NULL : r->addr);
    SH.gw_have = (r->op != BLE_OP_FORGET);
    memcpy(SH.gw, r->addr, 6);
    memset(SH.ram_name, 0, sizeof SH.ram_name);
    if (r->op != BLE_OP_FORGET) snprintf(SH.ram_name, sizeof SH.ram_name, "%s", r->name);
}
static bool sh_valid(void *c, uint32_t q) {
    (void)c;
#ifdef BLE_SEL_HAS_DEVSW_OWNER
    bool ok = SH.conn && ble_sel_devsw_fresh(&S, q, SH.job_cid);
#else
    bool ok = SH.conn && q == SH.seq_cur;
#endif
    if (ok && (q != S.seq || !S.connected || S.conn_id != SH.job_cid)) SH.stale_valid++;
    if (SH.disc_at == 7) sh_disc();
    return ok;
}
static void sh_sleep(void *c, uint32_t ms) { (void)c; (void)ms; }

static const ble_devsw_ops_t SH_OPS = { NULL, sh_meas, sh_cur, sh_drop, sh_cnt, sh_clear, sh_mnt, sh_save, sh_tclr, sh_commit, sh_ram, sh_valid, sh_sleep
#ifdef BLE_DEVSW_HAS_OWNER
    , sh_own
#endif
};

/* Модель оболочки: NOTIFY с dec.first (слот первого пакета + выдача задания). */
static void sh_first(const uint8_t dev[6], uint32_t t) {
#ifdef BLE_SEL_HAS_DEVSW_OWNER
    ble_sel_on_first_pkt(&S, t, &O);
    if (ble_first_put(&SH_TAG, &O)) memcpy(SH.slot, dev, 6);
#else
    memcpy(SH.slot, dev, 6);
    SH.slot_have = 1;
    ble_sel_on_first_pkt(&S, t, &O);
#endif
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    if (j != NULL) { SH.next = *j; SH.next_have = 1; }
}

/* B03: запись первого пакета в хранилище с меткой цели оболочки (SH.gw) */
static void sh_submit(const uint8_t dev[6]) {
    if (!SH.gw_have || !eq6(dev, SH.gw)) SH.bad_first++;
    if (SH.first_n < 4) memcpy(SH.first_dev[SH.first_n], dev, 6);
    SH.first_n++;
}

/* Модель оболочки: sel_task после ble_sel_on_devsw_done. */
static void sh_first_done(const ble_act_t *a, const ble_devsw_res_t *res) {
#ifdef BLE_SEL_HAS_DEVSW_OWNER
    if (ble_first_take(&SH_TAG, &S, a->seq, a->op == BLE_OP_CONFIRM_NEW && res->rc == 0 && res->valid != 0)) sh_submit(SH.slot);
#else
    if (a->op == BLE_OP_CONFIRM_NEW && res->rc == 0 && res->valid != 0 && SH.slot_have) sh_submit(SH.slot);
    SH.slot_have = 0;
#endif
}

/* Модель оболочки: ble_gw_sel_select (HTTP /select по адресу кандидата). */
static int sh_select(const uint8_t dev[6], uint32_t t) {
    int tok = 0;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++)
        if (S.cands.c[i].used && eq6(S.cands.c[i].addr, dev)) tok = S.cands.c[i].tok;
#ifdef BLE_SEL_HAS_DEVSW_OWNER
    if (ble_sel_select_blocked(&S)) return BLE_E_BUSY;
#endif
    return ble_sel_cmd_select(&S, S.gen, (uint8_t)tok, true, false, 1520u, t, &O);
}

static void sh_job(const ble_act_t *a, uint32_t t, int disc_at) {
    ble_devsw_req_t r;
    memset(&r, 0, sizeof r);
    r.op = a->op;
    r.seq = a->seq;
    r.conn_id = S.conn_id;
    memcpy(r.addr, S.conn_addr, 6);
    r.atype = S.conn_atype;
    memcpy(r.name, S.open_req_name, sizeof r.name - 1);   /* r обнулён, '\0' на месте */
    SH.seq_cur = (a->seq == S.seq && r.conn_id == S.conn_id) ? a->seq : (a->seq ^ 0x80000000u);
    SH.job_cid = r.conn_id;
    SH.job_seq = a->seq;
    SH.now = t;
    SH.disc_at = disc_at;
    ble_devsw_res_t res = ble_devsw_run(&SH_OPS, &r);
    SH.disc_at = 0;
    ble_sel_on_devsw_done(&S, a->seq, res.rc, res.err, res.valid != 0, t + 5, &O);
    if (!SH.next_have && act(BLE_ACT_DEVSW) != NULL) { SH.next = *act(BLE_ACT_DEVSW); SH.next_have = 1; }   /* b29: sel_exec ставит в очередь задание, выданное на результат */
    sh_first_done(a, &res);
}
static void sw_A_to_B(int disc_at) {
    mk_live_A(0);
    memset(&SH, 0, sizeof SH);
    memcpy(SH.gw, A, 6);
    memcpy(SH.nvs, A, 6);
    SH.gw_have = 1;
    SH.conn = 1;
    SH.cid = 3;
    ble_sel_cmd_rescan(&S, true, false, 100, &O);
    SH.conn = 0;
    ble_sel_on_disc(&S, 3, 200, &O);
    ble_sel_on_scan_start(&S, true, 200, &O);
    ble_sel_on_scan_result(&S, B, 0, -60, "AtomFast ", true, 300, &O);
    sel_from_scan(1, 400);
    ble_sel_on_open_ok(&S, 9, B, 500, &O);
    SH.conn = 1;
    SH.cid = 9;
    ble_sel_on_write_descr_ok(&S, 600, &O);
    ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
    CHECK(d.first == 1 && d.store == 0);
    ble_sel_on_first_pkt(&S, 700, &O);
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    CHECK(j != NULL);
    if (j == NULL) return;
    ble_act_t jc = *j;
    sh_job(&jc, 800, disc_at);
}

static void T12_disc_during_devsw_label(void) {
    /* V-01: разрыв BLE во время смены прибора (4 = в очистке колец, 7 = после set_valid) */
    static const int when[2] = {4, 7};
    for (int w = 0; w < 2; w++) {
        sw_A_to_B(when[w]);
        CHECK(S.has_tgt && SH.gw_have);
        CHECK(eq6(S.tgt_addr, SH.gw));
        CHECK(eq6(SH.nvs, SH.gw));
        uint32_t t = 900;
        int opened = 0;
        for (int k = 0; k < 40 && !opened; k++) {
            ble_sel_tick(&S, t, &O);
            if (has(BLE_ACT_OPEN)) opened = 1;
            else t += 1000;
        }
        CHECK(opened);
        if (!opened) continue;
        CHECK(t < 5000u);   /* к новой цели без 30-с простоя в CONFIRMING без соединения */
        uint8_t X[6];
        memcpy(X, act(BLE_ACT_OPEN)->addr, 6);
        CHECK(eq6(X, SH.gw));
        ble_sel_on_open_ok(&S, 11, X, t, &O);
        SH.conn = 1;
        SH.cid = 11;
        ble_sel_on_write_descr_ok(&S, t + 10, &O);
        ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
        int stored = d.store && ble_sel_label_ok(&S, SH.gw_have != 0, SH.gw);
        CHECK(!stored || eq6(S.conn_addr, SH.gw));
        CHECK(S.state == BLE_ST_LIVE && stored);
    }
}

static void T13_label_gate(void) {
    mk_live_A(0);
    CHECK(ble_sel_label_ok(&S, true, A));
    CHECK(!ble_sel_label_ok(&S, true, B));
    CHECK(!ble_sel_label_ok(&S, false, A));
    ble_sel_on_disc(&S, 3, 100, &O);
    CHECK(!ble_sel_label_ok(&S, true, A));
}

static void T14_boot_with_target_no_scan(void) {
    /* V-02: при сохранённой цели автомат не разрешает старт скана (предикат, которым оболочка гейтит SCAN_PARAM_SET_COMPLETE) */
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    CHECK(has(BLE_ACT_OPEN));
    CHECK(!has(BLE_ACT_SCAN_START));
    CHECK(!ble_sel_scan_may_start(&S));
    ble_sel_on_scan_start(&S, true, 50, &O);
    CHECK(S.state == BLE_ST_OPENING);
    ble_sel_on_open_ok(&S, 3, A, 100, &O);
    ble_sel_on_write_descr_ok(&S, 200, &O);
    CHECK(S.state == BLE_ST_LIVE);
    CHECK(!ble_sel_scan_may_start(&S));
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    CHECK(ble_sel_scan_may_start(&S));
}
/* ── b28 B02/B03: события во время исполнения смены прибора ──────────────── */

/* Загрузка без цели: скан находит B и C, пользователь выбирает B, связь, CCCD, первый пакет B — задание смены. */
static ble_act_t boot_to_confirm_B(void) {
    memset(&SH, 0, sizeof SH);
#ifdef BLE_SEL_HAS_DEVSW_OWNER
    memset(&SH_TAG, 0, sizeof SH_TAG);
#endif
    mk_scan_BC(0);
    ble_sel_tick(&S, 20000, &O);
    ble_sel_on_scan_end(&S, 20100, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(sh_select(B, 21000) == BLE_OK);
    ble_sel_on_open_ok(&S, 9, B, 21500, &O);
    SH.conn = 1;
    SH.cid = 9;
    ble_sel_on_write_descr_ok(&S, 21600, &O);
    ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
    CHECK(d.first == 1);
    sh_first(B, 23600);
    CHECK(SH.next_have);
    ble_act_t j = SH.next;
    SH.next_have = 0;
    return j;
}

/* Внутри очистки колец B: разрыв B, затем выбор C, связь C, CCCD, первый пакет C. */
static void hk_disc_select_C(void) {
    sh_disc();
    if (SH.bypass) {
        int idx = -1;
        for (int i = 0; i < BLE_SEL_CAND_MAX; i++)
            if (S.cands.c[i].used && eq6(S.cands.c[i].addr, C)) idx = i;
        if (idx >= 0) ble_sel__select_idx(&S, idx, SH.now + 50, &O);
        SH.sel_rc = BLE_OK;
    } else {
        SH.sel_rc = sh_select(C, SH.now + 50);
    }
    if (!has(BLE_ACT_OPEN)) return;
    ble_sel_on_open_ok(&S, 12, C, SH.now + 300, &O);
    SH.conn = 1;
    SH.cid = 12;
    ble_sel_on_write_descr_ok(&S, SH.now + 400, &O);
    ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
    if (d.first) sh_first(C, SH.now + 2400);
}

static void run_next(uint32_t t);
/* Внутри очистки колец B: разрыв B, выбор C в обход гейта, связь C и CCCD; первого пакета C ещё нет. */
static void hk_disc_open_C(void) {
    sh_disc();
    int idx = -1;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++)
        if (S.cands.c[i].used && eq6(S.cands.c[i].addr, C)) idx = i;
    if (idx >= 0) ble_sel__select_idx(&S, idx, SH.now + 50, &O);
    if (!has(BLE_ACT_OPEN)) return;
    ble_sel_on_open_ok(&S, 12, C, SH.now + 300, &O);
    SH.conn = 1;
    SH.cid = 12;
    ble_sel_on_write_descr_ok(&S, SH.now + 400, &O);
}

/* B02: соединение C открыто во время смены на B, задание C ещё не выдано:
   задание B не пишет NVS/ОЗУ и не валидирует C; затем смена на C проходит штатно. */
static void T17_new_conn_before_new_job(void) {
    ble_act_t j = boot_to_confirm_B();
    SH.hook = hk_disc_open_C;
    sh_job(&j, 24000, 0);
    CHECK(SH.stale_wr == 0);
    CHECK(SH.stale_valid == 0);
    CHECK(S.state == BLE_ST_CONFIRMING && S.connected && eq6(S.conn_addr, C));
    ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
    CHECK(d.first == 1);
    if (d.first) sh_first(C, 26400);
    run_next(27000);
    CHECK(SH.bad_first == 0);
    CHECK(SH.first_n == 1 && eq6(SH.first_dev[0], C));
    CHECK(S.state == BLE_ST_LIVE && eq6(S.tgt_addr, C) && eq6(SH.gw, C) && eq6(SH.nvs, C));
}
/* Внутри очистки колец B: разрыв B, затем «забыть» (HTTP /forget). */
static void hk_disc_forget(void) {
    sh_disc();
    SH.sel_rc = ble_sel_cmd_forget(&S, true, false, SH.now + 50, &O);
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    if (j != NULL) { SH.next = *j; SH.next_have = 1; }
}

static void run_next(uint32_t t) {
    if (!SH.next_have) return;
    ble_act_t n = SH.next;
    SH.next_have = 0;
    sh_job(&n, t, 0);
}

/* B02: выбор C (HTTP) во время смены на B после разрыва B — не принимается, пока задание B не вернуло результат. */
static void T15_disc_select_during_devsw(void) {
    ble_act_t j = boot_to_confirm_B();
    SH.hook = hk_disc_select_C;
    sh_job(&j, 24000, 0);
    CHECK(SH.sel_rc == BLE_E_BUSY);
    CHECK(SH.stale_wr == 0);
    CHECK(SH.stale_valid == 0);
    CHECK(SH.bad_first == 0);
    run_next(26000);
    CHECK(S.has_tgt == SH.gw_have && (!S.has_tgt || eq6(S.tgt_addr, SH.gw)));
    CHECK(eq6(SH.nvs, SH.gw));
    CHECK(sh_select(C, 27000) == BLE_OK);
}

/* B02/B03: новое соединение C всё же появилось во время смены на B (выбор в обход гейта):
   задание B не пишет NVS/ОЗУ, не валидирует C, не трогает слот C; первый пакет C записан один раз с меткой C. */
static void T16_new_conn_during_devsw(void) {
    ble_act_t j = boot_to_confirm_B();
    SH.bypass = 1;
    SH.hook = hk_disc_select_C;
    sh_job(&j, 24000, 0);
    CHECK(SH.stale_wr == 0);
    CHECK(SH.stale_valid == 0);
    CHECK(SH.next_have);
    run_next(26000);
    CHECK(SH.bad_first == 0);
    CHECK(SH.first_n == 1 && eq6(SH.first_dev[0], C));
    CHECK(S.state == BLE_ST_LIVE && eq6(S.tgt_addr, C) && eq6(SH.gw, C) && eq6(SH.nvs, C));
}

/* B02: «забыть» во время смены на B после разрыва B вытесняет задание B: оно не пишет B в NVS/ОЗУ. */
static void T18_forget_during_late_devsw(void) {
    ble_act_t j = boot_to_confirm_B();
    SH.hook = hk_disc_forget;
    sh_job(&j, 24000, 0);
    CHECK(SH.stale_wr == 0);
    CHECK(SH.next_have);
    run_next(26000);
    CHECK(!S.has_tgt && !SH.gw_have);
    CHECK(SH.stale_wr == 0);
}

/* B03: разрыв после set_valid — автомат не принял результат как текущий, первый пакет не пишется. */
static void T19_first_pkt_needs_accept(void) {
    ble_act_t j = boot_to_confirm_B();
    sh_job(&j, 24000, 7);
    CHECK(S.state != BLE_ST_LIVE);
    CHECK(SH.first_n == 0);
}
/* ── b29 поток H ─────────────────────────────────────────────────────────── */
#ifndef BLE_NOTE_NAME_UNSAVED
#define BLE_NOTE_NAME_UNSAVED 10   /* заголовки 80cce32: кода нет, проверка красит */
#endif
static const uint8_t Z6[6] = {0, 0, 0, 0, 0, 0};

/* HTTP /forget при живой связи (закрытие соединения ещё идёт) */
static void hk_forget_live(void) {
    SH.sel_rc = ble_sel_cmd_forget(&S, true, false, SH.now + 50, &O);
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    if (j != NULL) { SH.next = *j; SH.next_have = 1; }
}

/* разрыв, которым завершилось закрытие; задание, выданное автоматом на разрыв, — в SH.next */
static void sh_disc_acts(uint32_t t) {
    SH.conn = 0;
    ble_sel_on_disc(&S, SH.cid, t, &O);
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    if (j != NULL) { SH.next = *j; SH.next_have = 1; }
}

/* (1) «Забыть» во время смены на B при живой связи; задание B завершается раньше разрыва.
   HTTP ответил OK — после разрыва цель забыта, первый пакет не пишется. */
static void T20_forget_live_job_finishes_first(void) {
    ble_act_t j = boot_to_confirm_B();
    SH.hook = hk_forget_live;
    sh_job(&j, 24000, 0);
    CHECK(SH.sel_rc == BLE_OK);
    CHECK(!SH.next_have);
    sh_disc_acts(24200);
    CHECK(SH.next_have);
    run_next(24500);
    CHECK(!S.has_tgt && !SH.gw_have);
    CHECK(memcmp(SH.nvs, Z6, 6) == 0);
    CHECK(SH.first_n == 0);
    CHECK(SH.stale_wr == 0);
}

/* B06: имя не записалось — автомат и ОЗУ оболочки показывают то же, что NVS, и сообщают об этом.
   when: 0 — штатно, 4 — разрыв B в очистке (поздний результат). Последний проход — без сбоя. */
static void T21_name_unsaved_matches_nvs(void) {
    static const int when[3] = {0, 4, 0};
    for (int w = 0; w < 3; w++) {
        ble_act_t j = boot_to_confirm_B();
        SH.name_fail = (w < 2);
        sh_job(&j, 24000, when[w]);
        CHECK(S.has_tgt && eq6(S.tgt_addr, B) && eq6(SH.nvs, B));
        CHECK(strcmp(S.tgt_name, SH.nvs_name) == 0);
        CHECK(strcmp(SH.ram_name, SH.nvs_name) == 0);
        if (w < 2) CHECK(S.err == BLE_NOTE_NAME_UNSAVED);
        else CHECK(strcmp(SH.nvs_name, "AtomFast ") == 0 && S.err != BLE_NOTE_NAME_UNSAVED);
    }
}

/* Цель A, «заменить» (rescan), разрыв A, скан, выбор B, связь, CCCD, первый пакет B — задание смены. */
static ble_act_t a_to_b_job(void) {
    ble_act_t none;
    memset(&none, 0, sizeof none);
    mk_live_A(0);
    memset(&SH, 0, sizeof SH);
    memcpy(SH.gw, A, 6);
    memcpy(SH.nvs, A, 6);
    snprintf(SH.nvs_name, sizeof SH.nvs_name, "%s", "AtomFast ");
    SH.gw_have = 1;
    SH.conn = 1;
    SH.cid = 3;
    ble_sel_cmd_rescan(&S, true, false, 100, &O);
    SH.conn = 0;
    ble_sel_on_disc(&S, 3, 200, &O);
    ble_sel_on_scan_start(&S, true, 200, &O);
    ble_sel_on_scan_result(&S, B, 0, -60, "AtomFast ", true, 300, &O);
    sel_from_scan(1, 400);
    ble_sel_on_open_ok(&S, 9, B, 500, &O);
    SH.conn = 1;
    SH.cid = 9;
    ble_sel_on_write_descr_ok(&S, 600, &O);
    ble_sel_on_first_pkt(&S, 700, &O);
    const ble_act_t *j = act(BLE_ACT_DEVSW);
    CHECK(j != NULL);
    return j != NULL ? *j : none;
}

/* (1) Смена A→B после «заменить»: разрыв B в очистке колец, затем «забыть» (HTTP OK).
   Задание B не пишет B, «забыть» исполняется: цели нет ни в NVS, ни в ОЗУ. */
static void T22_forget_after_disc_in_switch(void) {
    ble_act_t j = a_to_b_job();
    if (j.kind != BLE_ACT_DEVSW) return;
    SH.hook = hk_disc_forget;
    sh_job(&j, 800, 0);
    CHECK(SH.sel_rc == BLE_OK);
    CHECK(SH.stale_wr == 0);
    CHECK(SH.next_have);
    run_next(1500);
    CHECK(!S.has_tgt && !SH.gw_have);
    CHECK(memcmp(SH.nvs, Z6, 6) == 0);
}

/* (1) Смена A→B после «заменить»: «забыть» при живой связи, очистка колец не удалась (задание B — отказ).
   После разрыва забыта прежняя цель A. */
static void T23_forget_live_job_fails(void) {
    ble_act_t j = a_to_b_job();
    if (j.kind != BLE_ACT_DEVSW) return;
    SH.clear_fail = 1;
    SH.hook = hk_forget_live;
    sh_job(&j, 800, 0);
    SH.clear_fail = 0;
    CHECK(SH.sel_rc == BLE_OK);
    sh_disc_acts(1000);
    CHECK(SH.next_have);
    run_next(1500);
    CHECK(!S.has_tgt && !SH.gw_have);
    CHECK(memcmp(SH.nvs, Z6, 6) == 0);
}
int main(void) {
    RUN(T1_scan_match_live_keeps_mac);
    RUN(T2_identity_from_open_event);
    RUN(T3_stale_tick);
    RUN(T4_no_autoconnect);
    RUN(T5_select_by_token);
    RUN(T7_scan_timing);
    RUN(T8_reconnect_only_to_tgt);
    RUN(T9_forget_flow);
    RUN(T10_switch_while_live);
    RUN(T11_open_fail_new_selection);
    RUN(T12_disc_during_devsw_label);
    RUN(T13_label_gate);
    RUN(T14_boot_with_target_no_scan);
    RUN(T15_disc_select_during_devsw);
    RUN(T16_new_conn_during_devsw);
    RUN(T17_new_conn_before_new_job);
    RUN(T18_forget_during_late_devsw);
    RUN(T19_first_pkt_needs_accept);
    RUN(T20_forget_live_job_finishes_first);
    RUN(T21_name_unsaved_matches_nvs);
    RUN(T22_forget_after_disc_in_switch);
    RUN(T23_forget_live_job_fails);
    TESTS_DONE();
}
