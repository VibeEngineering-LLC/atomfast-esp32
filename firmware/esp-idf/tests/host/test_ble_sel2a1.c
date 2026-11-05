#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include "ble_sel_cmd.h"

static ble_sel_t S;
static ble_acts_t O;

static int has(uint8_t kind) { return ble_acts_find(&O, kind) >= 0; }
static const ble_act_t *act(uint8_t kind) { int i = ble_acts_find(&O, kind); return i >= 0 ? &O.a[i] : NULL; }
static int eq6(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

__attribute__((unused))
static void mk_live_A(uint32_t t) {
    const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
    ble_sel_init(&S, A, 0, NULL, t, &O);
    ble_sel_on_open_ok(&S, 3, A, t + 10, &O);
    ble_sel_on_write_descr_ok(&S, t + 20, &O);
}

static void mk_scan_BC(uint32_t t) {
    ble_sel_init(&S, NULL, 0, NULL, t, &O);
    ble_sel_on_scan_start(&S, true, t, &O);
    const uint8_t B[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};
    ble_sel_on_scan_result(&S, B, 0, -50, "AtomFast ", true, t + 100, &O);
    const uint8_t C[6] = {0xA0, 0x6C, 0x5E, 0x13, 0x77, 0x0A};
    ble_sel_on_scan_result(&S, C, 0, -55, "AtomSwift", true, t + 200, &O);
}

static void sel_from_scan(uint8_t tok, uint32_t t) {
    ble_sel_cmd_select(&S, S.gen, tok, true, false, 0, t, &O);
    ble_sel_on_scan_end(&S, t + 10, &O);
}

static void T15_open_stale(void) {
    const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
    const uint8_t B[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};

    /* (a) */
    ble_sel_init(&S, A, 0, NULL, 0, &O);
    CHECK(S.state == BLE_ST_OPENING);

    CHECK(ble_sel_cmd_forget(&S, true, false, 100, &O) == BLE_DEFERRED);
    CHECK(S.open_stale == 1);
    CHECK(S.pending == BLE_PEND_FORGET);

    uint16_t id0 = S.conn_id;

    ble_sel_on_open_ok(&S, 9, A, 200, &O);
    {
        const ble_act_t *a = act(BLE_ACT_CLOSE_EVT_CONN);
        CHECK(a != NULL);
        CHECK(a != NULL && a->conn_id == 9);
    }
    CHECK(S.conn_id == id0);
    CHECK(S.connected == 0);
    CHECK(S.conn_valid == 0);
    CHECK(S.open_inflight == 0);

    ble_sel_on_disc(&S, 9, 300, &O);
    CHECK(S.state == BLE_ST_FORGETTING);
    {
        const ble_act_t *a = act(BLE_ACT_DEVSW);
        CHECK(a != NULL);
        CHECK(a != NULL && a->op == BLE_OP_FORGET);
    }

    /* (b) */
    ble_sel_init(&S, A, 0, NULL, 0, &O);
    CHECK(S.state == BLE_ST_OPENING);

    ble_sel_on_open_fail(&S, 100, &O);
    CHECK(S.state == BLE_ST_WAIT);

    ble_sel_on_open_ok(&S, 77, B, 200, &O);
    CHECK(O.n == 1);
    {
        const ble_act_t *a = act(BLE_ACT_CLOSE_EVT_CONN);
        CHECK(a != NULL);
        CHECK(a != NULL && a->conn_id == 77);
    }
    CHECK(S.conn_id == BLE_SEL_NO_CONN);
    CHECK(S.connected == 0);
    CHECK(S.state == BLE_ST_WAIT);
}

static void T16_deferred_cmds(void) {
    const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
    const uint8_t C[6] = {0xA0, 0x6C, 0x5E, 0x13, 0x77, 0x0A};

    /* (a) */
    ble_sel_init(&S, A, 0, NULL, 0, &O);
    CHECK(S.state == BLE_ST_OPENING);

    CHECK(ble_sel_cmd_rescan(&S, false, false, 100, &O) == BLE_DEFERRED);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_acts_reset(&O);
    CHECK(ble_sel_scan_begin(&S, 150, &O) == BLE_E_BUSY);
    CHECK(!has(BLE_ACT_SCAN_START));

    ble_sel_on_open_ok(&S, 9, A, 200, &O);
    CHECK(has(BLE_ACT_CLOSE_EVT_CONN));

    ble_sel_on_disc(&S, 9, 300, &O);
    CHECK(S.state == BLE_ST_SCAN);
    CHECK(has(BLE_ACT_SCAN_START));

    /* (b) */
    ble_sel_init(&S, A, 0, NULL, 0, &O);
    CHECK(S.state == BLE_ST_OPENING);

    CHECK(ble_sel_cmd_rescan(&S, false, false, 100, &O) == BLE_DEFERRED);

    ble_sel_on_open_fail(&S, 200, &O);
    CHECK(S.state == BLE_ST_SCAN);
    CHECK(has(BLE_ACT_SCAN_START));

    /* (c) */
    mk_scan_BC(1000);
    sel_from_scan(1, 1300);
    CHECK(S.state == BLE_ST_OPENING);

    CHECK(ble_sel_cmd_select(&S, S.gen, 2, true, false, 0, 1400, &O) == BLE_DEFERRED);

    ble_sel_on_open_fail(&S, 1500, &O);
    CHECK(has(BLE_ACT_OPEN));
    {
        const ble_act_t *a = act(BLE_ACT_OPEN);
        CHECK(a != NULL);
        CHECK(a != NULL && eq6(a->addr, C));
    }
    CHECK(S.state == BLE_ST_OPENING);
}

int main(void) {
    RUN(T15_open_stale);
    RUN(T16_deferred_cmds);
    TESTS_DONE();
    return 0;
}
