#ifndef BLE_DEVSW_H
#define BLE_DEVSW_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef BLE_OP_CONFIRM_NEW
#define BLE_OP_CONFIRM_NEW 1
#define BLE_OP_FORGET 2
#endif

#define BLE_DEVSW_CLEAR_RETRIES  3u    /* повторов очистки при rc -1 (всего вызовов до 4) */
#define BLE_DEVSW_CLEAR_PAUSE_MS 500u
#define BLE_DEVSW_SAVE_TRIES     2u    /* всего попыток tgt_save */

/* причины (значения совпадают с BLE_NOTE_* в ble_sel.h) */
#define BLE_DSW_OK                 0
#define BLE_DSW_E_CLEAR_FAILED     3
#define BLE_DSW_E_TGT_WRITE_FAILED 4
#define BLE_DSW_E_MEAS_ACTIVE      5
#define BLE_DSW_E_ABORTED          6
#define BLE_DSW_W_NAME_UNSAVED     10   /* b29 B06: цель записана, имени в NVS нет; rc=0, имя в ОЗУ пустое */
#define BLE_DEVSW_SAVE_NONAME      1    /* ответ tgt_save: цель записана, имя нет */

typedef struct {
    uint8_t  op;          /* BLE_OP_* */
    uint32_t seq;
    uint16_t conn_id;
    uint8_t  addr[6];     /* адрес из события соединения (conn) */
    uint8_t  atype;
    char     name[16];
} ble_devsw_req_t;

typedef struct {
    int     rc;           /* 0 успех, -1 сбой */
    uint8_t err;          /* BLE_DSW_* */
    uint8_t step;         /* шаг, на котором остановились (1..7) */
    uint8_t valid;        /* шаг 7 выдал s_conn_valid=1 */
} ble_devsw_res_t;

typedef struct {
    void *ctx;
    bool     (*meas_active)(void *ctx);
    bool     (*still_current)(void *ctx, uint32_t seq);
    void     (*drop_state)(void *ctx);
    uint32_t (*rings_count)(void *ctx);
    int      (*rings_clear)(void *ctx, int *persisted);
    bool     (*pfs_mounted)(void *ctx);
    int      (*tgt_save)(void *ctx, const uint8_t addr[6], uint8_t atype, const char *name); /* b29: 0 / BLE_DEVSW_SAVE_NONAME / -1 */
    int      (*tgt_clear)(void *ctx);
    void     (*set_commit)(void *ctx);
    void     (*commit_ram)(void *ctx, const ble_devsw_req_t *req);
    bool     (*set_valid)(void *ctx, uint32_t seq);
    void     (*sleep_ms)(void *ctx, uint32_t ms);
    bool     (*owner)(void *ctx, uint32_t seq);  /* B02: задание ещё владеет сменой (не вытеснено) */
} ble_devsw_ops_t;

#define BLE_DEVSW_HAS_OWNER 1
#define BLE_DEVSW_HAS_NAME 1   /* b29 B06: имя для tgt_save — из задания */

/* B02: проверка перед КАЖДЫМ необратимым шагом. fresh=1 — до первой очистки (ничего ещё не сделано,
   нужна текущая связь); fresh=0 — после неё (задание не вытеснено новым соединением/заданием). */
static inline bool ble_devsw__gate(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req, ble_devsw_res_t *res, bool fresh) {
    bool ok = fresh ? ops->still_current(ops->ctx, req->seq) : ops->owner(ops->ctx, req->seq);
    if (!ok) res->err = BLE_DSW_E_ABORTED;
    return ok;
}

/* Предикат успешной очистки колец (шаг 4 / FORGET шаг 2) */
static inline bool ble_devsw_p_clear(int rc, int persisted, bool mounted) {
    return rc == 0 && (persisted == 1 || !mounted); /* @p_clear */
}

/* Шаг 1: измерение не активно */
static inline bool ble_devsw__meas_ok(const ble_devsw_ops_t *ops, ble_devsw_res_t *res) {
    if (ops->meas_active(ops->ctx)) { res->err = BLE_DSW_E_MEAS_ACTIVE; return false; } /* @step1 */
    return true;
}

/* Очистка колец с ретраями; true если пусто или предикат P_CLEAR */
static inline bool ble_devsw__clear(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req, ble_devsw_res_t *res, bool fresh) {
    int persisted = 0;
    if (!ble_devsw__gate(ops, req, res, fresh)) return false; /* @pre_clear */
    int rc = ops->rings_clear(ops->ctx, &persisted);
    for (uint32_t i = 0; rc == -1 && i < BLE_DEVSW_CLEAR_RETRIES; i++) { /* @clear_retry */
        ops->sleep_ms(ops->ctx, BLE_DEVSW_CLEAR_PAUSE_MS);
        if (!ble_devsw__gate(ops, req, res, false)) return false; /* @pre_retry */
        persisted = 0;
        rc = ops->rings_clear(ops->ctx, &persisted);
    }
    if (!ble_devsw_p_clear(rc, persisted, ops->pfs_mounted(ops->ctx))) {
        res->err = BLE_DSW_E_CLEAR_FAILED;
        return false;
    }
    return true;
}

/* Шаг 5: commit + tgt_save (2 попытки) */
static inline bool ble_devsw__save(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req, ble_devsw_res_t *res) {
    res->step = 5;
    for (uint32_t i = 0; i < BLE_DEVSW_SAVE_TRIES; i++) {
        if (!ble_devsw__gate(ops, req, res, false)) return false; /* @pre_save */
        if (i == 0) ops->set_commit(ops->ctx);
        int rc = ops->tgt_save(ops->ctx, req->addr, req->atype, req->name); /* @save_name_req */
        if (rc == 0) return true;
        if (rc == BLE_DEVSW_SAVE_NONAME) { res->err = BLE_DSW_W_NAME_UNSAVED; return true; } /* @name_unsaved */
    }
    res->err = BLE_DSW_E_TGT_WRITE_FAILED;
    return false;
}

/* OP_CONFIRM_NEW: полная последовательность шагов 1..7 */
static inline ble_devsw_res_t ble_devsw__confirm(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req) {
    ble_devsw_res_t res = { -1, BLE_DSW_OK, 0, 0 };
    res.step = 1;
    if (!ble_devsw__meas_ok(ops, &res)) return res;
    res.step = 2;
    if (!ops->still_current(ops->ctx, req->seq)) { res.err = BLE_DSW_E_ABORTED; return res; } /* @step2 */
    res.step = 3;
    ops->drop_state(ops->ctx);
    res.step = 4;
    if (!ble_devsw__clear(ops, req, &res, true) || !ble_devsw__save(ops, req, &res)) return res; /* @step45 */
    res.step = 6;
    if (!ble_devsw__gate(ops, req, &res, false)) return res; /* @pre_commit */
    ble_devsw_req_t eff = *req;
    if (res.err == BLE_DSW_W_NAME_UNSAVED) eff.name[0] = '\0'; /* @ram_name_nvs: в ОЗУ то же имя, что в NVS */
    ops->commit_ram(ops->ctx, &eff); /* @step6 */
    res.step = 7;
    res.valid = ops->set_valid(ops->ctx, req->seq) ? 1 : 0;
    res.rc = 0;
    if (res.err != BLE_DSW_W_NAME_UNSAVED) res.err = BLE_DSW_OK;
    return res;
}

/* OP_FORGET: шаги 1..4 */
static inline ble_devsw_res_t ble_devsw__forget(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req) {
    ble_devsw_res_t res = { -1, BLE_DSW_OK, 0, 0 };
    res.step = 1;
    if (!ble_devsw__meas_ok(ops, &res)) return res;
    res.step = 2;
    if (!ble_devsw__clear(ops, req, &res, false)) return res; /* @f_step2 */
    res.step = 3;
    if (!ble_devsw__gate(ops, req, &res, false)) return res; /* @f_pre_tclr */
    if (ops->tgt_clear(ops->ctx) != 0) { res.err = BLE_DSW_E_TGT_WRITE_FAILED; return res; } /* @f_step3 */
    res.step = 4;
    if (!ble_devsw__gate(ops, req, &res, false)) return res; /* @f_pre_commit */
    ops->commit_ram(ops->ctx, req);
    res.rc = 0;
    res.err = BLE_DSW_OK;
    return res;
}

/* Диспетчер: выбирает последовательность по op */
static inline ble_devsw_res_t ble_devsw_run(const ble_devsw_ops_t *ops, const ble_devsw_req_t *req) {
    if (req->op == BLE_OP_CONFIRM_NEW) return ble_devsw__confirm(ops, req);
    if (req->op == BLE_OP_FORGET) return ble_devsw__forget(ops, req);
    return (ble_devsw_res_t){ -1, BLE_DSW_E_ABORTED, 0, 0 };
}

#endif
