#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ble_sel_cmd.h"

static const uint8_t A6[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
static const uint8_t B6[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};
static const uint8_t C6[6] = {0xA0, 0x6C, 0x5E, 0x13, 0x77, 0x0A};

static ble_sel_t S;
static ble_acts_t O;

static int has(uint8_t kind) { return ble_acts_find(&O, kind) >= 0; }
static const ble_act_t *act(uint8_t kind)
{
    int i = ble_acts_find(&O, kind);
    return i >= 0 ? &O.a[i] : NULL;
}
static int __attribute__((unused)) eq6(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static void mk_live_A(uint32_t t)
{
    ble_sel_init(&S, A6, 0, "AtomFast ", t, &O);
    ble_sel_on_open_ok(&S, 3, A6, t + 10, &O);
    ble_sel_on_write_descr_ok(&S, t + 20, &O);
}

static void mk_scan_BC(uint32_t t)
{
    ble_sel_init(&S, NULL, 0, NULL, t, &O);
    ble_sel_on_scan_start(&S, true, t, &O);
    ble_sel_on_scan_result(&S, B6, 0, -50, "AtomSwift", true, t + 100, &O);
    ble_sel_on_scan_result(&S, C6, 0, -50, "AtomSwift", true, t + 200, &O);
}

static void sel_from_scan(uint8_t tok, uint32_t t)
{
    ble_sel_cmd_select(&S, S.gen, tok, true, false, 0, t, &O);
    ble_sel_on_scan_end(&S, t + 10, &O);
}

/* ---- T26 snapshot helpers ---- */

typedef struct {
    uint8_t state, has_tgt, connected, conn_valid,
            open_inflight, pending, switching, err;
    uint8_t tgt_addr[6], conn_addr[6];
    uint16_t conn_id;
    uint32_t seq, fail_streak, delay_ms, next_try_ms, gen;
} snap_t;

static snap_t take_snap(void)
{
    snap_t r;
    r.state         = S.state;
    r.has_tgt       = S.has_tgt;
    memcpy(r.tgt_addr, S.tgt_addr, 6);
    r.connected     = S.connected;
    r.conn_valid    = S.conn_valid;
    r.conn_id       = S.conn_id;
    memcpy(r.conn_addr, S.conn_addr, 6);
    r.open_inflight = S.open_inflight;
    r.pending       = S.pending;
    r.seq           = S.seq;
    r.fail_streak   = S.fail_streak;
    r.delay_ms      = S.delay_ms;
    r.next_try_ms   = S.next_try_ms;
    r.switching     = S.switching;
    r.gen           = S.gen;
    r.err           = S.err;
    return r;
}

static int chk_snap(const snap_t *sn)
{
    return S.state == sn->state
        && S.has_tgt == sn->has_tgt
        && memcmp(S.tgt_addr, sn->tgt_addr, 6) == 0
        && S.connected == sn->connected
        && S.conn_valid == sn->conn_valid
        && S.conn_id == sn->conn_id
        && memcmp(S.conn_addr, sn->conn_addr, 6) == 0
        && S.open_inflight == sn->open_inflight
        && S.pending == sn->pending
        && S.seq == sn->seq
        && S.fail_streak == sn->fail_streak
        && S.delay_ms == sn->delay_ms
        && S.next_try_ms == sn->next_try_ms
        && S.switching == sn->switching
        && S.gen == sn->gen
        && S.err == sn->err;
}

static void build_state(int which)
{
    switch (which) {
    case 0: /* SCAN */
        ble_sel_init(&S, NULL, 0, NULL, 0, &O);
        break;
    case 1: /* SCAN_STOPPING */
        ble_sel_init(&S, NULL, 0, NULL, 0, &O);
        ble_sel_tick(&S, 25000, &O);
        break;
    case 2: /* LIST */
        ble_sel_init(&S, NULL, 0, NULL, 0, &O);
        ble_sel_on_scan_end(&S, 100, &O);
        ble_sel_on_scan_end(&S, 100, &O);
        break;
    case 3: /* OPENING */
        ble_sel_init(&S, A6, 0, "AtomFast ", 0, &O);
        break;
    case 4: /* CONFIRMING */
        mk_scan_BC(0);
        sel_from_scan(1, 300);
        ble_sel_on_open_ok(&S, 3, B6, 400, &O);
        ble_sel_on_write_descr_ok(&S, 500, &O);
        break;
    case 5: /* LIVE */
        mk_live_A(0);
        break;
    case 6: /* WAIT */
        ble_sel_init(&S, A6, 0, "AtomFast ", 0, &O);
        ble_sel_on_open_fail(&S, 100, &O);
        break;
    case 7: /* FORGETTING */
        ble_sel_init(&S, NULL, 0, NULL, 0, &O);
        ble_sel_cmd_forget(&S, true, false, 100, &O);
        ble_sel_on_scan_end(&S, 200, &O);
        break;
    }
}

/* ---- Tests ---- */

static void T18_first_packet_confirm(void)
{
    /* (a) CONFIRMING timeout without valid packet */
    mk_scan_BC(0);
    sel_from_scan(1, 300);
    ble_sel_on_open_ok(&S, 9, B6, 400, &O);
    ble_sel_on_write_descr_ok(&S, 500, &O);
    CHECK(S.state == BLE_ST_CONFIRMING);
    CHECK(S.has_tgt == 0);
    CHECK(!has(BLE_ACT_DEVSW));

    for (int i = 0; i < 5; i++) {
        ble_pkt_dec_t d = ble_sel_accept_pkt(&S, false);
        CHECK(d.first == 0);
        CHECK(d.store == 0);
    }

    ble_sel_tick(&S, 500u + 29999u, &O);
    CHECK(S.state == BLE_ST_CONFIRMING);

    ble_sel_tick(&S, 500u + 30000u, &O);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(S.err == BLE_NOTE_INCOMPATIBLE);
    CHECK(!has(BLE_ACT_DEVSW));
    CHECK(S.has_tgt == 0);

    /* (b) valid first packet → DEVSW confirm_new */
    mk_scan_BC(0);
    sel_from_scan(1, 300);
    ble_sel_on_open_ok(&S, 9, B6, 400, &O);
    ble_sel_on_write_descr_ok(&S, 500, &O);

    {
        ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
        CHECK(d.first == 1);
    }

    ble_sel_on_first_pkt(&S, 600, &O);
    const ble_act_t *da = act(BLE_ACT_DEVSW);
    CHECK(da != NULL);
    CHECK(da != NULL && da->op == BLE_OP_CONFIRM_NEW);
    CHECK(da != NULL && da->seq == S.seq);

    /* (c) re-init with same target → SET_VALID_SAME */
    mk_live_A(0);
    CHECK(S.state == BLE_ST_LIVE);
    CHECK(S.conn_valid == 1);

    ble_sel_init(&S, A6, 0, "AtomFast ", 0, &O);
    ble_sel_on_open_ok(&S, 3, A6, 10, &O);
    ble_sel_on_write_descr_ok(&S, 20, &O);
    CHECK(has(BLE_ACT_SET_VALID_SAME));
}

static void T26_event_state_matrix(void)
{
    const uint32_t t = 900000u;

    for (int st = 0; st < 8; st++) {

        /* E1: on_scan_result — all except SCAN */
        if (st != 0) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_scan_result(&S, C6, 0, -50, "AtomSwift", true, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }

        /* E2: on_scan_end — LIST, OPENING, CONFIRMING, LIVE, WAIT, FORGETTING */
        if (st >= 2) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_scan_end(&S, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }

        /* E3: on_open_ok(77,C) — all except OPENING */
        if (st != 3) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_open_ok(&S, 77, C6, t, &O);
            CHECK(O.n == 1);
            const ble_act_t *a = act(BLE_ACT_CLOSE_EVT_CONN);
            CHECK(a != NULL);
            CHECK(a != NULL && a->conn_id == 77);
            CHECK(chk_snap(&sn));
        }

        /* E4: on_open_fail — all except OPENING */
        if (st != 3) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_open_fail(&S, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }

        /* E6: on_write_descr_ok — all except OPENING */
        if (st != 3) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_write_descr_ok(&S, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }

        /* E7: on_first_pkt — all except CONFIRMING */
        if (st != 4) {
            build_state(st);
            snap_t sn = take_snap();
            ble_sel_on_first_pkt(&S, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }

        /* E8: on_devsw_done — all except CONFIRMING and FORGETTING */
        if (st != 4 && st != 7) {
            build_state(st);
            snap_t sn = take_snap();
            uint32_t seq = S.seq;
            ble_sel_on_devsw_done(&S, seq, 0, 0, true, t, &O);
            CHECK(O.n == 0);
            CHECK(chk_snap(&sn));
        }
    }

    /* E5: disc idempotency */
    mk_live_A(0);
    ble_sel_on_disc(&S, 3, 100, &O);
    CHECK(S.state == BLE_ST_WAIT);
    snap_t sn5 = take_snap();
    ble_sel_on_disc(&S, 3, 200, &O);
    CHECK(O.n == 0);
    CHECK(chk_snap(&sn5));
}

int main(void)
{
    RUN(T18_first_packet_confirm);
    RUN(T26_event_state_matrix);
    TESTS_DONE();
    return 0;
}
