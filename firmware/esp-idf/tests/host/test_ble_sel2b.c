#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include "ble_sel_cmd.h"

static const uint8_t A[6] = {0xA0,0x6C,0x3B,0x51,0x9D,0xF5};
static const uint8_t B[6] = {0xA0,0x6C,0x47,0x02,0xC8,0xE1};
static const uint8_t C[6] = {0xA0,0x6C,0x5E,0x13,0x77,0x0A};

static ble_sel_t S;
static ble_acts_t O;

static int has(uint8_t kind) { return ble_acts_find(&O, kind) >= 0; }
static const ble_act_t *act(uint8_t kind)
{
    int i = ble_acts_find(&O, kind);
    return i >= 0 ? &O.a[i] : NULL;
}
static int eq6(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

static void mk_live_A(uint32_t t)
{
    ble_sel_init(&S, A, 0, "AtomFast ", t, &O);
    ble_sel_on_open_ok(&S, 3, A, t + 10, &O);
    ble_sel_on_write_descr_ok(&S, t + 20, &O);
}

static void mk_scan_BC(uint32_t t)
{
    ble_sel_init(&S, NULL, 0, NULL, t, &O);
    ble_sel_on_scan_start(&S, true, t, &O);
    ble_sel_on_scan_result(&S, B, 0, -50, "AtomFast ", true, t + 100, &O);
    ble_sel_on_scan_result(&S, C, 0, -60, "AtomSwift", true, t + 200, &O);
}

static void sel_from_scan(uint8_t tok, uint32_t t)
{
    ble_sel_cmd_select(&S, S.gen, tok, true, false, 0, t, &O);
    ble_sel_on_scan_end(&S, t + 10, &O);
}

/* ---------- T14 ---------- */
static void T14_scan_stop_gates(void)
{
    /* (a) may_start gates */
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    CHECK(ble_sel_scan_may_start(&S));

    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    CHECK(!ble_sel_scan_may_start(&S));

    mk_live_A(0);
    CHECK(!ble_sel_scan_may_start(&S));

    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_tick(&S, 25000, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);
    CHECK(!ble_sel_scan_may_start(&S));

    /* (b) stop deadline ticks */
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_tick(&S, 25000, &O);
    CHECK(ble_acts_count(&O, BLE_ACT_SCAN_STOP) == 1);
    ble_sel_tick(&S, 26000, &O);
    CHECK(ble_acts_count(&O, BLE_ACT_SCAN_STOP) == 1);
    ble_sel_tick(&S, 27000, &O);
    CHECK(ble_acts_count(&O, BLE_ACT_SCAN_STOP) == 1);
    ble_sel_tick(&S, 28000, &O);
    CHECK(ble_acts_count(&O, BLE_ACT_SCAN_STOP) == 1);
    CHECK(S.state == BLE_ST_LIST);

    /* (c) select → STOPPING → OPENING via cooldown */
    ble_sel_init(&S, NULL, 0, NULL, 0, &O);
    ble_sel_on_scan_start(&S, true, 0, &O);
    ble_sel_on_scan_result(&S, B, 0, -50, "AtomFast ", true, 100, &O);
    ble_sel_cmd_select(&S, S.gen, 1, true, false, 0, 1000, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);
    ble_sel_tick(&S, 4000, &O);
    CHECK(has(BLE_ACT_OPEN));
    const ble_act_t *p = act(BLE_ACT_OPEN);
    CHECK(p != NULL && eq6(p->addr, B));
    CHECK(S.state == BLE_ST_OPENING);
}

/* ---------- T17 ---------- */
static void T17_list_returns(void)
{
    /* path 1: LIST timeout → WAIT → OPEN */
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    ble_sel_on_open_fail(&S, 100, &O);
    CHECK(S.state == BLE_ST_WAIT);

    CHECK(ble_sel_cmd_rescan(&S, false, false, 200, &O) == BLE_OK);

    ble_sel_tick(&S, 25200, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);

    ble_sel_on_scan_end(&S, 25300, &O);
    CHECK(S.state == BLE_ST_LIST);

    uint32_t L = 25300;
    ble_sel_tick(&S, L + 179999, &O);
    CHECK(S.state == BLE_ST_LIST);

    ble_sel_tick(&S, L + 180000, &O);
    CHECK(S.state == BLE_ST_WAIT);

    ble_sel_tick(&S, L + 180001, &O);
    CHECK(has(BLE_ACT_OPEN));
    const ble_act_t *p = act(BLE_ACT_OPEN);
    CHECK(p != NULL && eq6(p->addr, A));

    /* path 2: cancel from LIST */
    ble_sel_init(&S, A, 0, "AtomFast ", 1000000, &O);
    ble_sel_on_open_fail(&S, 1000100, &O);
    CHECK(S.state == BLE_ST_WAIT);
    CHECK(ble_sel_cmd_rescan(&S, false, false, 1000200, &O) == BLE_OK);
    ble_sel_tick(&S, 1025200, &O);
    CHECK(S.state == BLE_ST_SCAN_STOPPING);
    ble_sel_on_scan_end(&S, 1025300, &O);
    CHECK(S.state == BLE_ST_LIST);
    CHECK(ble_sel_cmd_cancel(&S, 1025400, &O) == BLE_OK);
    CHECK(S.state == BLE_ST_WAIT);

    /* LIVE → cancel is BUSY */
    mk_live_A(0);
    CHECK(ble_sel_cmd_cancel(&S, 100, &O) == BLE_E_BUSY);
}

/* ---------- T19 ---------- */
static void T19_fast_conn(void)
{
    /* (a) target A: init emits OPEN prime=1; open_ok → FAST_CONN */
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    const ble_act_t *p = act(BLE_ACT_OPEN);
    CHECK(p != NULL && p->prime == 1);

    ble_sel_on_open_ok(&S, 3, A, 10, &O);
    CHECK(has(BLE_ACT_FAST_CONN));
    p = act(BLE_ACT_FAST_CONN);
    CHECK(p != NULL && eq6(p->addr, A));

    /* (b) scan → select B → OPEN prime=1 → open_ok → FAST_CONN */
    mk_scan_BC(100);
    ble_sel_cmd_select(&S, S.gen, 1, true, false, 0, 400, &O);
    ble_sel_on_scan_end(&S, 410, &O);
    p = act(BLE_ACT_OPEN);
    CHECK(p != NULL && p->prime == 1);
    CHECK(p != NULL && eq6(p->addr, B));

    ble_sel_on_open_ok(&S, 9, B, 500, &O);
    CHECK(has(BLE_ACT_FAST_CONN));
    p = act(BLE_ACT_FAST_CONN);
    CHECK(p != NULL && eq6(p->addr, B));
}

/* ---------- T23 ---------- */
static void T23_valid_gate_gen(void)
{
    /* (a) live with target: store ok / bad */
    mk_live_A(0);
    ble_pkt_dec_t d = ble_sel_accept_pkt(&S, true);
    CHECK(d.store == 1);
    CHECK(d.count_rx == 1);

    d = ble_sel_accept_pkt(&S, false);
    CHECK(d.store == 0);
    CHECK(d.count_bad == 1);
    CHECK(d.count_rx == 1);

    /* (b) after forget: store==0 */
    ble_sel_cmd_forget(&S, true, false, 100, &O);
    d = ble_sel_accept_pkt(&S, true);
    CHECK(d.store == 0);
    CHECK(d.count_rx == 1);

    /* (c) new device (not original target): store==0 */
    mk_scan_BC(1000);
    sel_from_scan(1, 1300);
    ble_sel_on_open_ok(&S, 9, B, 1400, &O);
    ble_sel_on_write_descr_ok(&S, 1500, &O);
    d = ble_sel_accept_pkt(&S, true);
    CHECK(d.store == 0);
    CHECK(d.count_rx == 1);
}

/* ---------- T25 ---------- */
static void T25_meas_active_blocks(void)
{
    /* LIVE + meas_active blocks rescan and forget */
    mk_live_A(0);
    CHECK(ble_sel_cmd_rescan(&S, true, true, 100, &O) == BLE_E_MEAS_ACTIVE);
    CHECK(!has(BLE_ACT_CLOSE_CUR));

    CHECK(ble_sel_cmd_forget(&S, true, true, 200, &O) == BLE_E_MEAS_ACTIVE);
    CHECK(S.conn_valid == 1);

    /* From scratch: select with meas_active → E_MEAS_ACTIVE */
    ble_sel_init(&S, A, 0, "AtomFast ", 0, &O);
    ble_sel_on_open_fail(&S, 100, &O);
    CHECK(S.state == BLE_ST_WAIT);
    CHECK(ble_sel_cmd_rescan(&S, false, false, 200, &O) == BLE_OK);
    ble_sel_on_scan_result(&S, B, 0, -50, "AtomFast ", true, 300, &O);
    ble_sel_on_scan_result(&S, C, 0, -60, "AtomSwift", true, 400, &O);
    CHECK(ble_sel_cmd_select(&S, S.gen, 1, true, true, 5, 500, &O) == BLE_E_MEAS_ACTIVE);
}

/* ---------- T27 ---------- */
static void T27_switch_close_retry(void)
{
    mk_live_A(0);
    ble_sel_cmd_rescan(&S, true, false, 100, &O);
    CHECK(has(BLE_ACT_CLOSE_CUR));

    ble_sel_tick(&S, 5099, &O);
    CHECK(!has(BLE_ACT_CLOSE_CUR));

    ble_sel_tick(&S, 5100, &O);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(!has(BLE_ACT_NOTE_ERR));

    ble_sel_tick(&S, 10100, &O);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(!has(BLE_ACT_NOTE_ERR));

    ble_sel_tick(&S, 15100, &O);
    CHECK(has(BLE_ACT_CLOSE_CUR));
    CHECK(!has(BLE_ACT_NOTE_ERR));

    ble_sel_tick(&S, 20100, &O);
    const ble_act_t *p = act(BLE_ACT_NOTE_ERR);
    CHECK(p != NULL && p->err == BLE_NOTE_CLOSE_FAILED);
    CHECK(has(BLE_ACT_SET_VALID_SAME));
    CHECK(S.state == BLE_ST_LIVE);
    CHECK(S.conn_valid == 1);
    CHECK(S.switching == 0);
}

int main(void)
{
    RUN(T14_scan_stop_gates);
    RUN(T17_list_returns);
    RUN(T19_fast_conn);
    RUN(T23_valid_gate_gen);
    RUN(T25_meas_active_blocks);
    RUN(T27_switch_close_retry);
    TESTS_DONE();
}
