#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include "ble_devsw.h"

static const uint8_t A[6] = {0xA0,0x6C,0x3B,0x51,0x9D,0xF5};
static const uint8_t B[6] = {0xA0,0x6C,0x47,0x02,0xC8,0xE1};

typedef struct {
    uint8_t  nvs_has, nvs[6];
    uint8_t  ram_has, ram[6];
    uint8_t  init_has, init[6];
    uint32_t ring_n; uint8_t ring_author[6];
    int meas, cur, mounted;
    int clear_rc[8], clear_pers[8], clear_len, clear_calls;
    int save_fail_n, save_calls, tgt_clear_rc, tgt_clear_calls;
    int commit_calls, sleep_calls, sleep_last, set_valid_ret, violations;
    int stale_ev, stale, after;   /* B02: шаг, внутри которого задание устаревает; флаг; необратимых вызовов после */
    int save_noname; char saved_name[16], ram_name[16];   /* b29 B06: имя, записанное в NVS, и имя в ОЗУ */
} world_t;

static world_t W;

static void check(void)
{
    if (W.ring_n > 0) {
        if (!(W.nvs_has && memcmp(W.nvs, W.ring_author, 6) == 0))
            W.violations++;
    }
    bool ok = (W.ram_has == W.nvs_has && (!W.ram_has || memcmp(W.ram, W.nvs, 6) == 0))
           || (W.ram_has == W.init_has && memcmp(W.ram, W.init, 6) == 0);
    if (!ok)
        W.violations++;
}

static void reset_world(void)
{
    memset(&W, 0, sizeof(W));
    W.init_has = 1; memcpy(W.init, A, 6);
    W.nvs_has  = 1; memcpy(W.nvs,  A, 6);
    W.ram_has  = 1; memcpy(W.ram,  A, 6);
    W.ring_n = 5;   memcpy(W.ring_author, A, 6);
    W.cur = 1;
    W.mounted = 1;
    W.clear_rc[0] = 0;
    W.clear_pers[0] = 1;
    W.clear_len = 1;
    W.set_valid_ret = 1;
}

/* ---- stubs ---- */

enum { EV_NONE, EV_MEAS, EV_DROP, EV_CLEAR, EV_SAVE, EV_TCLR };
static void stale_mark(int ev) { if (W.stale_ev == ev) W.stale = 1; }
static void irrev_enter(void)  { if (W.stale) W.after++; }

static bool stub_meas_active(void *ctx)            { (void)ctx; stale_mark(EV_MEAS); return W.meas != 0; }
static bool stub_still_current(void *ctx, uint32_t seq) { (void)ctx; (void)seq; return W.cur != 0 && !W.stale; }
#ifdef BLE_DEVSW_HAS_OWNER
static bool stub_owner(void *ctx, uint32_t seq)    { (void)ctx; (void)seq; return !W.stale; }
#endif
static void stub_drop_state(void *ctx)             { (void)ctx; stale_mark(EV_DROP); }
static uint32_t stub_rings_count(void *ctx)        { (void)ctx; return W.ring_n; }

static int stub_rings_clear(void *ctx, int *persisted)
{
    (void)ctx;
    irrev_enter();
    int k = W.clear_calls;
    if (k >= W.clear_len) k = W.clear_len - 1;
    W.clear_calls++;
    *persisted = W.clear_pers[k];
    if (W.clear_rc[k] == 0) W.ring_n = 0;
    check();
    stale_mark(EV_CLEAR);
    return W.clear_rc[k];
}

static bool stub_pfs_mounted(void *ctx)            { (void)ctx; return W.mounted != 0; }

#ifdef BLE_DEVSW_HAS_NAME
static int stub_tgt_save(void *ctx, const uint8_t addr[6], uint8_t atype, const char *name)
#else
static int stub_tgt_save(void *ctx, const uint8_t addr[6], uint8_t atype)
#endif
{
    (void)ctx; (void)atype;
    irrev_enter();
    W.save_calls++;
    if (W.save_calls <= W.save_fail_n) return -1;
    memcpy(W.nvs, addr, 6);
    W.nvs_has = 1;
#ifdef BLE_DEVSW_HAS_NAME
    snprintf(W.saved_name, sizeof W.saved_name, "%s", name);
    if (W.save_noname) { W.saved_name[0] = '\0'; check(); stale_mark(EV_SAVE); return BLE_DEVSW_SAVE_NONAME; }
#endif
    check();
    stale_mark(EV_SAVE);
    return 0;
}

static int stub_tgt_clear(void *ctx)
{
    (void)ctx;
    irrev_enter();
    W.tgt_clear_calls++;
    if (W.tgt_clear_rc != 0) return W.tgt_clear_rc;
    W.nvs_has = 0;
    check();
    stale_mark(EV_TCLR);
    return 0;
}

static void stub_set_commit(void *ctx)             { (void)ctx; }

static void stub_commit_ram(void *ctx, const ble_devsw_req_t *req)
{
    (void)ctx;
    irrev_enter();
    W.commit_calls++;
    if (req->op == BLE_OP_CONFIRM_NEW) {
        memcpy(W.ram, req->addr, 6);
        W.ram_has = 1;
        snprintf(W.ram_name, sizeof W.ram_name, "%s", req->name);
    } else {
        W.ram_has = 0;
    }
    check();
}

static bool stub_set_valid(void *ctx, uint32_t seq) { (void)ctx; (void)seq; return W.set_valid_ret != 0; }
static void stub_sleep_ms(void *ctx, uint32_t ms)   { (void)ctx; W.sleep_calls++; W.sleep_last = (int)ms; }
/* ---- ops ---- */

static ble_devsw_ops_t ops = {
    .ctx          = (void *)&W,
    .meas_active  = stub_meas_active,
    .still_current= stub_still_current,
    .drop_state   = stub_drop_state,
    .rings_count  = stub_rings_count,
    .rings_clear  = stub_rings_clear,
    .pfs_mounted  = stub_pfs_mounted,
    .tgt_save     = stub_tgt_save,
    .tgt_clear    = stub_tgt_clear,
    .set_commit   = stub_set_commit,
    .commit_ram   = stub_commit_ram,
    .set_valid    = stub_set_valid,
    .sleep_ms     = stub_sleep_ms,
#ifdef BLE_DEVSW_HAS_OWNER
    .owner        = stub_owner,
#endif
};
/* ---- request helpers ---- */

static ble_devsw_req_t make_confirm(void)
{
    ble_devsw_req_t r;
    memset(&r, 0, sizeof(r));
    r.op = BLE_OP_CONFIRM_NEW;
    r.seq = 7;
    r.conn_id = 1;
    memcpy(r.addr, B, 6);
    r.atype = 0;
    strcpy(r.name, "AtomSwift");
    return r;
}

static ble_devsw_req_t make_forget(void)
{
    ble_devsw_req_t r;
    memset(&r, 0, sizeof(r));
    r.op = BLE_OP_FORGET;
    r.seq = 7;
    r.conn_id = 1;
    memcpy(r.addr, A, 6);
    r.atype = 0;
    strcpy(r.name, "AtomSwift");
    return r;
}

/* ---- tests ---- */

static void T21_clear_predicate(void)
{
    /* (1) predicate table */
    CHECK(ble_devsw_p_clear( 0, 1, true)  == true);
    CHECK(ble_devsw_p_clear( 0, 0, true)  == false);
    CHECK(ble_devsw_p_clear( 0, 0, false) == true);
    CHECK(ble_devsw_p_clear(-1, 1, true)  == false);
    CHECK(ble_devsw_p_clear(-2, 1, true)  == false);
    CHECK(ble_devsw_p_clear(-3, 1, true)  == false);

    /* (2) CONFIRM, every clear attempt returns -1 */
    reset_world();
    W.clear_rc[0] = -1;
    W.clear_len  = 1;
    ble_devsw_req_t req = make_confirm();
    ble_devsw_res_t res = ble_devsw_run(&ops, &req);
    CHECK(W.clear_calls == 4);
    CHECK(W.sleep_calls == 3);
    CHECK(W.sleep_last  == 500);
    CHECK(res.rc  == -1);
    CHECK(res.err == BLE_DSW_E_CLEAR_FAILED);

    /* (3) CONFIRM, two failures then success */
    reset_world();
    W.clear_rc[0] = -1; W.clear_rc[1] = -1; W.clear_rc[2] = 0;
    W.clear_pers[0] = 0; W.clear_pers[1] = 0; W.clear_pers[2] = 1;
    W.clear_len = 3;
    res = ble_devsw_run(&ops, &req);
    CHECK(res.rc == 0);
    CHECK(W.clear_calls == 3);
}

static void T22_devsw_power_loss(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    ble_devsw_req_t rf_ = make_forget();
    ble_devsw_res_t res;

    /* (a) CONFIRM success */
    reset_world();
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.rc == 0);
    CHECK(memcmp(W.nvs, B, 6) == 0);
    CHECK(memcmp(W.ram, B, 6) == 0);
    CHECK(W.ring_n == 0);
    CHECK(res.valid == 1);
    CHECK(W.violations == 0);

    /* (b) CONFIRM, meas active */
    reset_world();
    W.meas = 1;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_MEAS_ACTIVE);
    CHECK(res.step == 1);
    CHECK(W.clear_calls == 0);
    CHECK(W.save_calls == 0);
    CHECK(W.violations == 0);

    /* (c) CONFIRM, not current */
    reset_world();
    W.cur = 0;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_ABORTED);
    CHECK(res.step == 2);
    CHECK(W.clear_calls == 0);
    CHECK(W.violations == 0);

    /* (d) CONFIRM, clear rc=-1 */
    reset_world();
    W.clear_rc[0] = -1;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_CLEAR_FAILED);
    CHECK(W.save_calls == 0);
    CHECK(memcmp(W.nvs, A, 6) == 0);
    CHECK(W.violations == 0);

    /* (e) CONFIRM, clear rc=-2 */
    reset_world();
    W.clear_rc[0] = -2;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_CLEAR_FAILED);
    CHECK(W.save_calls == 0);
    CHECK(memcmp(W.nvs, A, 6) == 0);
    CHECK(W.violations == 0);

    /* (f) CONFIRM, clear rc=-3 */
    reset_world();
    W.clear_rc[0] = -3;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_CLEAR_FAILED);
    CHECK(W.save_calls == 0);
    CHECK(memcmp(W.nvs, A, 6) == 0);
    CHECK(W.violations == 0);

    /* (g) CONFIRM, save fails both tries */
    reset_world();
    W.save_fail_n = 2;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_TGT_WRITE_FAILED);
    CHECK(res.step == 5);
    CHECK(W.commit_calls == 0);
    CHECK(memcmp(W.ram, A, 6) == 0);
    CHECK(W.violations == 0);

    /* (h) CONFIRM, set_valid returns 0 */
    reset_world();
    W.set_valid_ret = 0;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.rc == 0);
    CHECK(res.valid == 0);
    CHECK(W.violations == 0);

    /* (i) CONFIRM, rings already empty: b22 F02 - clear (и сброс окна) выполняется всё равно, иначе открытое окно A смешается с B */
    reset_world();
    W.ring_n = 0;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(W.clear_calls == 1);
    CHECK(res.rc == 0);
    CHECK(W.violations == 0);

    /* (j) FORGET success */
    reset_world();
    res = ble_devsw_run(&ops, &rf_);
    CHECK(res.rc == 0);
    CHECK(W.nvs_has == 0);
    CHECK(W.ram_has == 0);
    CHECK(W.ring_n == 0);
    CHECK(W.violations == 0);

    /* (k) FORGET, clear fails */
    reset_world();
    W.clear_rc[0] = -1;
    res = ble_devsw_run(&ops, &rf_);
    CHECK(res.err == BLE_DSW_E_CLEAR_FAILED);
    CHECK(res.step == 2);
    CHECK(W.tgt_clear_calls == 0);
    CHECK(W.nvs_has == 1);
    CHECK(W.violations == 0);

    /* (l) FORGET, tgt_clear fails */
    reset_world();
    W.tgt_clear_rc = -1;
    res = ble_devsw_run(&ops, &rf_);
    CHECK(res.err == BLE_DSW_E_TGT_WRITE_FAILED);
    CHECK(res.step == 3);
    CHECK(W.commit_calls == 0);
    CHECK(W.violations == 0);
}

static void T25_meas_active_blocks(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    ble_devsw_req_t rf_ = make_forget();
    ble_devsw_res_t res;

    reset_world();
    W.meas = 1;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(res.err == BLE_DSW_E_MEAS_ACTIVE);
    CHECK(W.clear_calls == 0);

    reset_world();
    W.meas = 1;
    res = ble_devsw_run(&ops, &rf_);
    CHECK(res.err == BLE_DSW_E_MEAS_ACTIVE);
    CHECK(W.clear_calls == 0);
}

/* B02: задание устарело (разрыв и новое соединение / новое задание) внутри шага ev.
   После этого не должно быть ни одного необратимого вызова: очистка, запись NVS, запись ОЗУ. */
static void T26_stale_before_first_clear(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    ble_devsw_req_t rf_ = make_forget();
    ble_devsw_res_t res;

    reset_world();
    W.stale_ev = EV_DROP;
    res = ble_devsw_run(&ops, &rc_);
    CHECK(W.clear_calls == 0);
    CHECK(W.after == 0);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED);
    CHECK(W.ring_n == 5 && memcmp(W.nvs, A, 6) == 0 && memcmp(W.ram, A, 6) == 0);

    reset_world();
    W.stale_ev = EV_MEAS;
    res = ble_devsw_run(&ops, &rf_);
    CHECK(W.clear_calls == 0);
    CHECK(W.after == 0);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED);
}

static void T27_stale_before_clear_retry(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    reset_world();
    W.clear_rc[0] = -1;
    W.clear_len = 1;
    W.stale_ev = EV_CLEAR;
    ble_devsw_res_t res = ble_devsw_run(&ops, &rc_);
    CHECK(W.clear_calls == 1);
    CHECK(W.after == 0);
    CHECK(W.save_calls == 0);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED);
}

static void T28_stale_before_save(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    reset_world();
    W.stale_ev = EV_CLEAR;
    ble_devsw_res_t res = ble_devsw_run(&ops, &rc_);
    CHECK(W.save_calls == 0);
    CHECK(W.commit_calls == 0);
    CHECK(W.after == 0);
    CHECK(memcmp(W.nvs, A, 6) == 0);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED && res.step == 5);
}

static void T29_stale_before_commit(void)
{
    ble_devsw_req_t rc_ = make_confirm();
    reset_world();
    W.stale_ev = EV_SAVE;
    ble_devsw_res_t res = ble_devsw_run(&ops, &rc_);
    CHECK(W.commit_calls == 0);
    CHECK(W.after == 0);
    CHECK(memcmp(W.ram, A, 6) == 0);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED && res.step == 6);
    CHECK(W.violations == 0);
}

static void T30_forget_stale_before_tgt_clear(void)
{
    ble_devsw_req_t rf_ = make_forget();
    reset_world();
    W.stale_ev = EV_CLEAR;
    ble_devsw_res_t res = ble_devsw_run(&ops, &rf_);
    CHECK(W.tgt_clear_calls == 0);
    CHECK(W.after == 0);
    CHECK(W.nvs_has == 1);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED && res.step == 3);
}

static void T31_forget_stale_before_commit(void)
{
    ble_devsw_req_t rf_ = make_forget();
    reset_world();
    W.stale_ev = EV_TCLR;
    ble_devsw_res_t res = ble_devsw_run(&ops, &rf_);
    CHECK(W.commit_calls == 0);
    CHECK(W.after == 0);
    CHECK(W.ram_has == 1);
    CHECK(res.rc == -1 && res.err == BLE_DSW_E_ABORTED && res.step == 4);
}
#ifdef BLE_DEVSW_HAS_NAME
/* b29 B06: имя в NVS — из задания (req.name); в ОЗУ — то же имя */
static void T32_name_from_job(void)
{
    ble_devsw_req_t r = make_confirm();
    reset_world();
    ble_devsw_res_t res = ble_devsw_run(&ops, &r);
    CHECK(res.rc == 0 && res.err == BLE_DSW_OK);
    CHECK(strcmp(W.saved_name, "AtomSwift") == 0);
    CHECK(strcmp(W.ram_name, "AtomSwift") == 0);
}

/* b29 B06: цель записана, имя нет — успех со статусом; в ОЗУ имени нет, как и в NVS; цель повторно не пишется */
static void T33_name_unsaved_reported(void)
{
    ble_devsw_req_t r = make_confirm();
    reset_world();
    W.save_noname = 1;
    ble_devsw_res_t res = ble_devsw_run(&ops, &r);
    CHECK(res.rc == 0 && res.err == BLE_DSW_W_NAME_UNSAVED);
    CHECK(W.save_calls == 1 && W.commit_calls == 1);
    CHECK(memcmp(W.nvs, B, 6) == 0 && memcmp(W.ram, B, 6) == 0);
    CHECK(W.ram_name[0] == '\0' && W.saved_name[0] == '\0');
    CHECK(W.violations == 0);
}
#endif
int main(void)
{
    RUN(T21_clear_predicate);
    RUN(T22_devsw_power_loss);
    RUN(T25_meas_active_blocks);
    RUN(T26_stale_before_first_clear);
    RUN(T27_stale_before_clear_retry);
    RUN(T28_stale_before_save);
    RUN(T29_stale_before_commit);
    RUN(T30_forget_stale_before_tgt_clear);
    RUN(T31_forget_stale_before_commit);
#ifdef BLE_DEVSW_HAS_NAME
    RUN(T32_name_from_job);
    RUN(T33_name_unsaved_reported);
#endif
    TESTS_DONE();
}
