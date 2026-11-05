#ifndef BLE_SEL_H
#define BLE_SEL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ble_sel_cand.h"

#define BLE_SEL_SCAN_MS               20000u
#define BLE_SEL_STOP_DEADLINE_MS      3000u
#define BLE_SEL_STOP_RETRY_MS         1000u
#define BLE_SEL_STOP_TRIES            3u
#define BLE_SEL_COOLDOWN_MS           3000u
#define BLE_SEL_CHOOSE_MS             180000u
#define BLE_SEL_FIRST_PKT_MS          30000u
#define BLE_SEL_DEVSW_MAX_MS          90000u
#define BLE_SEL_SWITCH_CLOSE_RETRY_MS 5000u
#define BLE_SEL_CLOSE_RETRY_MAX       3u
#define BLE_SEL_OPEN_NOTE_MS          45000u
#define BLE_SEL_DELAY0_MS             600u
#define BLE_SEL_DELAY_MAX_MS          60000u
#define BLE_SEL_SCAN_DURATION_S       22u
#define BLE_SEL_NO_CONN               0xFFFFu

/* состояния */
#define BLE_ST_SCAN 0
#define BLE_ST_SCAN_STOPPING 1
#define BLE_ST_LIST 2
#define BLE_ST_OPENING 3
#define BLE_ST_CONFIRMING 4
#define BLE_ST_LIVE 5
#define BLE_ST_WAIT 6
#define BLE_ST_FORGETTING 7

/* отложенная команда */
#define BLE_PEND_NONE 0
#define BLE_PEND_RESCAN 1
#define BLE_PEND_SELECT 2
#define BLE_PEND_FORGET 3

/* действия для оболочки */
#define BLE_ACT_NONE 0
#define BLE_ACT_SCAN_START 1
#define BLE_ACT_SCAN_STOP 2
#define BLE_ACT_OPEN 3
#define BLE_ACT_CLOSE_EVT_CONN 4
#define BLE_ACT_CLOSE_CUR 5
#define BLE_ACT_FAST_CONN 6
#define BLE_ACT_DEVSW 7
#define BLE_ACT_SET_VALID_SAME 8
#define BLE_ACT_NOTE_ERR 9

#ifndef BLE_OP_CONFIRM_NEW
#define BLE_OP_CONFIRM_NEW 1
#define BLE_OP_FORGET 2
#endif

/* коды возврата */
#define BLE_OK 0
#define BLE_DEFERRED 1
#define BLE_E_NEED_REPLACE (-1)
#define BLE_E_NEED_CLEAR (-2)
#define BLE_E_MEAS_ACTIVE (-3)
#define BLE_E_STALE (-4)
#define BLE_E_COOLDOWN (-5)
#define BLE_E_BUSY (-6)
#define BLE_E_LIVE (-7)
#define BLE_E_NO_CAND (-8)
#define BLE_E_BAD (-9)
#define BLE_E_NOSPACE (-11)

/* причины для полей err/note */
#define BLE_NOTE_NONE 0
#define BLE_NOTE_DEFERRED 1
#define BLE_NOTE_INCOMPATIBLE 2
#define BLE_NOTE_CLEAR_FAILED 3
#define BLE_NOTE_TGT_WRITE_FAILED 4
#define BLE_NOTE_MEAS_ACTIVE 5
#define BLE_NOTE_ABORTED 6
#define BLE_NOTE_CLOSE_FAILED 7
#define BLE_NOTE_SCAN_START_FAILED 8
#define BLE_NOTE_NO_CAND 9
#define BLE_NOTE_NAME_UNSAVED 10   /* b29 B06: цель записана, имя прибора в NVS не записано */

typedef struct { uint8_t kind, op, addr[6], atype, prime, err; uint16_t conn_id; uint32_t seq; } ble_act_t;
#define BLE_ACTS_MAX 4
typedef struct { uint8_t n; ble_act_t a[BLE_ACTS_MAX]; } ble_acts_t;
typedef struct { uint8_t count_rx, count_bad, store, first; } ble_pkt_dec_t;
typedef struct {
    uint8_t  state, has_tgt, tgt_addr[6], tgt_atype;
    char     tgt_name[BLE_SEL_NAME_LEN];
    uint8_t  connected, conn_valid;
    uint16_t conn_id;
    uint8_t  conn_addr[6], conn_atype;
    uint8_t  open_inflight, open_req_addr[6], open_req_atype;
    char     open_req_name[BLE_SEL_NAME_LEN];
    uint8_t  intent_new, switching, open_stale;
    uint8_t  await_close;
    uint16_t await_conn_id;
    uint8_t  pending, pending_tok;
    uint32_t pending_gen;
    uint8_t  close_wait, close_tries, stop_tries;
    uint8_t  first_have, devsw_wait;
    uint8_t  devsw_commit;
    uint8_t  devsw_open, devsw_op, devsw_addr[6], devsw_atype; /* снимок выданного задания смены (V-01) */
    uint32_t devsw_seq;
    char     devsw_name[BLE_SEL_NAME_LEN];
    uint32_t seq, gen;
    ble_cands_t cands;
    uint32_t fail_streak, delay_ms, next_try_ms;
    uint32_t scan_t0_ms, stop_t0_ms, stop_last_ms, list_t0_ms, switch_t0_ms;
    uint32_t confirm_t0_ms, devsw_t0_ms, cooldown_until_ms, lost_t0_ms;
    uint32_t ident_mismatch;
    uint8_t  err, note;
} ble_sel_t;

/* --- помощники ble_acts_* --- */

static inline void ble_acts_reset(ble_acts_t *o) {
    o->n = 0;
}

static inline ble_act_t *ble_acts_push(ble_acts_t *o, uint8_t kind) {
    if (o->n >= BLE_ACTS_MAX) return NULL;
    ble_act_t *a = &o->a[o->n];
    memset(a, 0, sizeof *a);
    a->kind = kind;
    o->n++;
    return a;
}

static inline int ble_acts_find(const ble_acts_t *o, uint8_t kind) {
    for (uint8_t i = 0; i < o->n; i++) {
        if (o->a[i].kind == kind) return (int)i;
    }
    return -1;
}

static inline int ble_acts_count(const ble_acts_t *o, uint8_t kind) {
    int cnt = 0;
    for (uint8_t i = 0; i < o->n; i++) {
        if (o->a[i].kind == kind) cnt++;
    }
    return cnt;
}

/* --- ble__due --- */

static inline bool ble__due(uint32_t now_ms, uint32_t t_ms) {
    return (int32_t)(now_ms - t_ms) >= 0;
}

/* --- ble_sel__* (в порядке контракта) --- */

static inline void ble_sel__open(ble_sel_t *s, const uint8_t addr[6], uint8_t atype, uint8_t intent_new, uint32_t now_ms, ble_acts_t *out) {
    (void)now_ms;
    memcpy(s->open_req_addr, addr, 6);
    s->open_req_atype = atype;
    s->open_inflight = 1;
    s->intent_new = intent_new;
    s->open_stale = 0;
    s->state = BLE_ST_OPENING;
    ble_act_t *a = ble_acts_push(out, BLE_ACT_OPEN);
    if (a != NULL) { memcpy(a->addr, addr, 6); a->atype = atype; a->prime = 1; } /* @prime */
}

static inline void ble_sel__devsw(ble_sel_t *s, uint8_t op, uint32_t now_ms, ble_acts_t *out) {
    s->seq++;
    s->devsw_wait = 1;
    s->devsw_commit = 0;
    s->devsw_t0_ms = now_ms;
    s->devsw_open = 1; s->devsw_op = op; s->devsw_seq = s->seq; /* @devsw_snap */
    memcpy(s->devsw_addr, s->conn_addr, 6); s->devsw_atype = s->conn_atype;
    memcpy(s->devsw_name, s->open_req_name, BLE_SEL_NAME_LEN);
    ble_act_t *a = ble_acts_push(out, BLE_ACT_DEVSW);
    if (a != NULL) { a->op = op; a->seq = s->seq; }
    if (op == BLE_OP_FORGET) s->state = BLE_ST_FORGETTING;
}

static inline int ble_sel__defer(ble_sel_t *s, uint8_t pend, uint8_t tok, uint32_t gen) {
    s->open_stale = 1; s->pending = pend; /* @defer */
    s->pending_tok = tok;
    s->pending_gen = gen;
    s->note = BLE_NOTE_DEFERRED;
    return BLE_DEFERRED;
}

static inline void ble_sel__backoff(ble_sel_t *s, uint32_t now_ms) {
    s->next_try_ms = now_ms + s->delay_ms;
    s->delay_ms = (s->delay_ms * 2u > BLE_SEL_DELAY_MAX_MS) ? BLE_SEL_DELAY_MAX_MS : s->delay_ms * 2u; /* @backoff */
}

static inline void ble_sel__to_rest(ble_sel_t *s, uint32_t now_ms) {
    s->switching = 0;
    s->intent_new = 0;
    s->pending = BLE_PEND_NONE;
    if (s->has_tgt) {
        s->state = BLE_ST_WAIT;
        s->next_try_ms = now_ms;
    } else {
        s->state = BLE_ST_LIST;
        s->list_t0_ms = now_ms;
    }
}

static inline void ble_sel__open_failed(ble_sel_t *s, uint32_t now_ms) {
    s->fail_streak++;
    ble_sel__backoff(s, now_ms);
    if (s->intent_new) {
        s->state = BLE_ST_LIST;
        s->list_t0_ms = now_ms;
    } else {
        s->state = BLE_ST_WAIT;
    }
    s->intent_new = 0;
}

static inline void ble_sel__fast_conn(ble_acts_t *out, const uint8_t addr[6]) {
    ble_act_t *a = ble_acts_push(out, BLE_ACT_FAST_CONN);
    if (a != NULL) memcpy(a->addr, addr, 6);
}

static inline void ble_sel__close_evt(ble_acts_t *out, uint16_t conn_id) {
    ble_act_t *a = ble_acts_push(out, BLE_ACT_CLOSE_EVT_CONN);
    if (a != NULL) a->conn_id = conn_id;
}

static inline int ble_sel__meas_block(bool meas_active) {
    return meas_active ? BLE_E_MEAS_ACTIVE : BLE_OK; /* @meas */
}

static inline void ble_sel__select_idx(ble_sel_t *s, int idx, uint32_t now_ms, ble_acts_t *out) {
    ble_cand_t *c = &s->cands.c[idx];
    memcpy(s->open_req_name, c->name, BLE_SEL_NAME_LEN);
    ble_sel__open(s, c->addr, c->atype, 1, now_ms, out); /* @sel_open */
}

static inline void ble_sel__do_pending_select(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    int i = ble_cands_find_tok(&s->cands, s->pending_tok);
    if (s->pending_gen == s->gen && i >= 0) {
        ble_sel__select_idx(s, i, now_ms, out);
    } else {
        s->state = BLE_ST_LIST;
        s->list_t0_ms = now_ms;
        s->err = BLE_NOTE_NO_CAND;
    }
}

static inline int ble_sel_scan_begin(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    if (s->connected || s->open_inflight) return BLE_E_BUSY; /* @scan_guard */
    if (s->state == BLE_ST_CONFIRMING || s->state == BLE_ST_FORGETTING) return BLE_E_BUSY; /* @scan_guard2 */
    s->state = BLE_ST_SCAN;
    s->gen++;
    ble_cands_clear(&s->cands);
    s->pending = BLE_PEND_NONE;
    s->scan_t0_ms = now_ms;
    (void)ble_acts_push(out, BLE_ACT_SCAN_START);
    return BLE_OK;
}

static inline void ble_sel__scan_stop(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    s->state = BLE_ST_SCAN_STOPPING;
    s->stop_t0_ms = now_ms;
    s->stop_last_ms = now_ms;
    s->stop_tries = 1;
    (void)ble_acts_push(out, BLE_ACT_SCAN_STOP);
}

static inline void ble_sel__stop_done(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    s->cooldown_until_ms = now_ms + BLE_SEL_COOLDOWN_MS;
    uint8_t p = s->pending;
    s->pending = BLE_PEND_NONE;
    if (p == BLE_PEND_SELECT) {
        ble_sel__do_pending_select(s, now_ms, out);
    } else if (p == BLE_PEND_RESCAN) {
        (void)ble_sel_scan_begin(s, now_ms, out);
    } else if (p == BLE_PEND_FORGET) {
        ble_sel__devsw(s, BLE_OP_FORGET, now_ms, out);
    } else {
        s->state = BLE_ST_LIST; s->list_t0_ms = now_ms; /* @4a_none */
    }
}

static inline void ble_sel__run_pending(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    uint8_t p = s->pending; s->pending = BLE_PEND_NONE; /* @run_pending */
    if (p == BLE_PEND_RESCAN) {
        (void)ble_sel_scan_begin(s, now_ms, out);
    } else if (p == BLE_PEND_SELECT) {
        ble_sel__do_pending_select(s, now_ms, out);
    } else if (p == BLE_PEND_FORGET) {
        ble_sel__devsw(s, BLE_OP_FORGET, now_ms, out);
    } else {
        ble_sel__open_failed(s, now_ms);
    }
}

/* --- публичные функции --- */

static inline void ble_sel_init(ble_sel_t *s, const uint8_t *tgt_addr, uint8_t tgt_atype, const char *tgt_name, uint32_t now_ms, ble_acts_t *out) {
    memset(s, 0, sizeof *s);
    s->conn_id = BLE_SEL_NO_CONN;
    s->await_conn_id = BLE_SEL_NO_CONN;
    s->delay_ms = BLE_SEL_DELAY0_MS;
    s->cooldown_until_ms = now_ms;
    ble_acts_reset(out);
    if (tgt_addr != NULL) {
        s->has_tgt = 1;
        memcpy(s->tgt_addr, tgt_addr, 6);
        s->tgt_atype = tgt_atype;
        if (tgt_name != NULL) {
            size_t len = strlen(tgt_name);
            if (len >= BLE_SEL_NAME_LEN) len = BLE_SEL_NAME_LEN - 1;
            memcpy(s->tgt_name, tgt_name, len);
            s->tgt_name[len] = '\0';
        }
        memcpy(s->open_req_name, s->tgt_name, BLE_SEL_NAME_LEN);
        ble_sel__open(s, tgt_addr, tgt_atype, 0, now_ms, out);
    } else {
        (void)ble_sel_scan_begin(s, now_ms, out);
    }
}

static inline bool ble_sel_scan_may_start(const ble_sel_t *s) {
    return s->state == BLE_ST_SCAN && s->pending == BLE_PEND_NONE && !s->connected && !s->open_inflight; /* @may_start */
}

static inline int ble_sel_on_scan_start(ble_sel_t *s, bool ok, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_SCAN) return BLE_OK;
    if (!ok) { s->state = BLE_ST_LIST; s->list_t0_ms = now_ms; s->err = BLE_NOTE_SCAN_START_FAILED; return BLE_OK; } /* @scan_start_fail */
    s->scan_t0_ms = now_ms;
    return BLE_OK;
}

static inline int ble_sel_on_scan_result(ble_sel_t *s, const uint8_t addr[6], uint8_t atype, int8_t rssi, const char *name, bool has_uuid, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_SCAN) return BLE_OK; /* @E1_gate */
    (void)ble_cands_update(&s->cands, addr, atype, rssi, name, has_uuid, now_ms); /* @E1 */
    return BLE_OK;
}

static inline int ble_sel_on_scan_end(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state == BLE_ST_SCAN) {
        ble_sel__scan_stop(s, now_ms, out);
    } else if (s->state == BLE_ST_SCAN_STOPPING) {
        ble_sel__stop_done(s, now_ms, out);
    }
    return BLE_OK;
}

static inline int ble_sel_on_open_ok(ble_sel_t *s, uint16_t conn_id, const uint8_t addr[6], uint32_t now_ms, ble_acts_t *out) {
    (void)now_ms;
    ble_acts_reset(out); /* @E3_entry */
    if (s->state != BLE_ST_OPENING) { ble_sel__close_evt(out, conn_id); return BLE_OK; } /* @E3_notopen */
    if (s->open_stale || memcmp(addr, s->open_req_addr, 6) != 0) { /* @E3_check */
        if (memcmp(addr, s->open_req_addr, 6) != 0) s->ident_mismatch++;
        ble_sel__close_evt(out, conn_id);
        s->open_stale = 0;
        s->open_inflight = 0;
        s->await_close = 1;
        s->await_conn_id = conn_id;
        return BLE_OK;
    }
    s->conn_id = conn_id;
    memcpy(s->conn_addr, addr, 6); s->conn_atype = s->open_req_atype; /* @E3_conn */
    s->connected = 1;
    s->conn_valid = 0;
    s->open_inflight = 0;
    ble_sel__fast_conn(out, addr); /* @fast_conn */
    return BLE_OK;
}

static inline int ble_sel_on_open_fail(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_OPENING) return BLE_OK;
    s->open_inflight = 0;
    if (s->open_stale) {
        s->open_stale = 0;
        ble_sel__run_pending(s, now_ms, out);
    } else {
        ble_sel__open_failed(s, now_ms);
    }
    return BLE_OK;
}

static inline int ble_sel_on_sub_fail(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_OPENING || !s->connected) return BLE_OK;
    (void)ble_acts_push(out, BLE_ACT_CLOSE_CUR);
    if (s->intent_new) {
        s->state = BLE_ST_LIST;
        s->list_t0_ms = now_ms;
    } else {
        s->state = BLE_ST_WAIT;
        ble_sel__backoff(s, now_ms);
    }
    s->intent_new = 0;
    return BLE_OK;
}

static inline int ble_sel_on_disc(ble_sel_t *s, uint16_t conn_id, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->await_close && conn_id == s->await_conn_id) {
        s->await_close = 0;
        ble_sel__run_pending(s, now_ms, out);
        return BLE_OK;
    }
    if (!s->connected || conn_id != s->conn_id) return BLE_OK; /* @disc_idem */
    s->connected = 0;
    s->conn_valid = 0;
    s->close_wait = 0;
    s->lost_t0_ms = now_ms;
    if (s->state == BLE_ST_CONFIRMING) {
        s->seq++;
        s->devsw_wait = 0;
        s->first_have = 0;
    }
    if (s->switching && s->pending == BLE_PEND_FORGET) { s->pending = BLE_PEND_NONE; ble_sel__devsw(s, BLE_OP_FORGET, now_ms, out); } /* @disc_forget */
    else {
        uint8_t sw = s->switching;
        s->switching = 0; /* @disc_sw_clear: b29 — признак смены относится к этому разрыву и дальше не переносится */
        if (sw && ble_sel_scan_begin(s, now_ms, out) == BLE_OK) {
            /* @disc_switch */
        } else if (s->state == BLE_ST_LIVE || s->state == BLE_ST_OPENING || s->state == BLE_ST_CONFIRMING) { /* @disc_rest: b29 — без соединения автомат не остаётся в CONFIRMING */
            if (s->has_tgt) {
                s->state = BLE_ST_WAIT;
                s->next_try_ms = now_ms + s->delay_ms;
            } else {
                s->state = BLE_ST_LIST;
                s->list_t0_ms = now_ms;
            }
            s->intent_new = 0;
        }
    }
    return BLE_OK;
}

static inline int ble_sel_on_write_descr_ok(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_OPENING || !s->connected) return BLE_OK;
    if (s->has_tgt && memcmp(s->conn_addr, s->tgt_addr, 6) == 0) {
        s->conn_valid = 1; s->fail_streak = 0; s->delay_ms = BLE_SEL_DELAY0_MS; /* @live_reset */
        s->state = BLE_ST_LIVE;
        s->switching = 0;
        s->intent_new = 0;
        (void)ble_acts_push(out, BLE_ACT_SET_VALID_SAME);
    } else {
        s->state = BLE_ST_CONFIRMING; s->confirm_t0_ms = now_ms; /* @to_confirming */
        s->first_have = 0;
    }
    return BLE_OK;
}

static inline int ble_sel_on_first_pkt(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    if (s->state != BLE_ST_CONFIRMING || s->first_have) return BLE_OK;
    s->first_have = 1;
    ble_sel__devsw(s, BLE_OP_CONFIRM_NEW, now_ms, out);
    return BLE_OK;
}

#define BLE_SEL_HAS_LABEL_GATE 1

/* Результат смены прибора пришёл после смены seq автоматом (разрыв BLE / таймаут).
   rc==0: оболочка уже записала цель в NVS и ОЗУ — автомат принимает ту же цель (V-01). */
static inline void ble_sel__devsw_late(ble_sel_t *s, uint8_t err, uint32_t now_ms)
{
    s->devsw_open = 0;
    if (s->devsw_op != BLE_OP_CONFIRM_NEW)
        return;
    s->has_tgt = 1; memcpy(s->tgt_addr, s->devsw_addr, 6); s->tgt_atype = s->devsw_atype; memcpy(s->tgt_name, s->devsw_name, BLE_SEL_NAME_LEN); /* @late_adopt */
    if (err == BLE_NOTE_NAME_UNSAVED) { s->tgt_name[0] = '\0'; s->err = err; } /* @late_name: имя как в NVS */
    s->switching = 0; s->intent_new = 0;
    if ((s->state == BLE_ST_CONFIRMING || s->state == BLE_ST_WAIT) && !s->connected && !s->open_inflight)
        ble_sel__to_rest(s, now_ms); /* @late_rest */
}

/* Пакет пишется с меткой цели оболочки; допуск — только если метка совпадает с прибором текущего соединения (V-01). */
static inline bool ble_sel_label_ok(const ble_sel_t *s, bool have_label, const uint8_t label[6])
{
    return have_label && s->connected && memcmp(s->conn_addr, label, 6) == 0; /* @label_gate */
}
static inline void ble_sel__devsw_done(ble_sel_t *s, uint32_t seq, int rc, uint8_t err, bool valid, uint32_t now_ms, ble_acts_t *out) {
    if (rc == 0 && s->devsw_open && seq == s->devsw_seq && seq != s->seq) { ble_sel__devsw_late(s, err, now_ms); return; } /* @late_done */
    if (seq == s->devsw_seq) s->devsw_open = 0;
    if ((s->state != BLE_ST_CONFIRMING && s->state != BLE_ST_FORGETTING) || !s->devsw_wait || seq != s->seq) return;
    s->devsw_wait = 0;
    if (s->state == BLE_ST_CONFIRMING) {
        if (rc == 0) {
            s->has_tgt = 1;
            memcpy(s->tgt_addr, s->conn_addr, 6);
            s->tgt_atype = s->conn_atype;
            memcpy(s->tgt_name, s->devsw_name, BLE_SEL_NAME_LEN); /* @done_name: b29 B06 — имя задания */
            if (err == BLE_NOTE_NAME_UNSAVED) { s->tgt_name[0] = '\0'; s->err = err; } /* @done_name_unsaved */
            s->switching = 0;
            s->intent_new = 0;
            if (valid) {
                s->state = BLE_ST_LIVE;
                s->conn_valid = 1;
                s->fail_streak = 0;
                s->delay_ms = BLE_SEL_DELAY0_MS;
            } else {
                s->state = BLE_ST_WAIT;
                s->next_try_ms = now_ms;
            }
        } else {
            (void)ble_acts_push(out, BLE_ACT_CLOSE_CUR);
            s->err = err;
            ble_sel__to_rest(s, now_ms);
        }
    } else {
        if (rc == 0) {
            s->has_tgt = 0;
            memset(s->tgt_addr, 0, 6);
            s->tgt_name[0] = '\0';
            s->switching = 0;
            s->state = BLE_ST_LIST;
            (void)ble_sel_scan_begin(s, now_ms, out);
        } else {
            s->err = err;
            ble_sel__to_rest(s, now_ms);
        }
    }
}

/* b29: «забыть», принятое (HTTP OK) во время смены, переживает результат задания CONFIRM_NEW.
   Есть соединение — FORGET запустит его разрыв (@disc_forget); нет — FORGET выдаётся сразу. */
static inline void ble_sel__forget_keep(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out) {
    if (s->state == BLE_ST_FORGETTING) return;
    s->pending = BLE_PEND_FORGET; /* @fg_keep */
    if (s->state == BLE_ST_SCAN || s->state == BLE_ST_SCAN_STOPPING || s->open_inflight) return;
    if (s->connected) { s->switching = 1; s->conn_valid = 0; return; } /* @fg_wait_disc */
    s->pending = BLE_PEND_NONE;
    s->switching = 0;
    s->close_wait = 0;
    ble_sel__devsw(s, BLE_OP_FORGET, now_ms, out); /* @fg_run */
}

static inline int ble_sel_on_devsw_done(ble_sel_t *s, uint32_t seq, int rc, uint8_t err, bool valid, uint32_t now_ms, ble_acts_t *out) {
    ble_acts_reset(out);
    bool fg = (s->pending == BLE_PEND_FORGET); /* @fg_seen */
    ble_sel__devsw_done(s, seq, rc, err, valid, now_ms, out);
    if (fg) ble_sel__forget_keep(s, now_ms, out);
    return BLE_OK;
}

#define BLE_SEL_HAS_DEVSW_OWNER 1

/* B02: задание смены seq текущее — снимок открыт, seq автомата не сменён, связь та же (conn_id). */
static inline bool ble_sel_devsw_fresh(const ble_sel_t *s, uint32_t seq, uint16_t conn_id)
{
    return s->devsw_open && s->devsw_seq == seq && s->seq == seq && s->connected && s->conn_id == conn_id; /* @fresh */
}

/* B02: задание ещё владеет сменой — проверка перед каждым необратимым шагом после первой очистки.
   Вытеснено: новым заданием (снимок сменён) либо новым соединением или открытием после разрыва. */
static inline bool ble_sel_devsw_owner(const ble_sel_t *s, uint32_t seq)
{
    if (!s->devsw_open || s->devsw_seq != seq) return false; /* @own_snap */
    if (s->devsw_op == BLE_OP_CONFIRM_NEW && s->seq == seq) return s->connected != 0; /* @own_fresh */
    return !s->connected && !s->open_inflight; /* @own_idle */
}

/* B02: пока задание смены не вернуло результат, новый выбор прибора не принимается
   (закрывает окно между проверкой владения и записью в NVS). */
static inline bool ble_sel_select_blocked(const ble_sel_t *s)
{
    return s->devsw_open != 0; /* @sel_block */
}

/* B03: слот первого пакета помечен заданием смены, которое он подтверждает (seq задания). */
typedef struct { uint8_t have; uint32_t seq; } ble_first_tag_t;

/* После ble_sel_on_first_pkt: слот занимается, только если выдано задание CONFIRM_NEW; метка = его seq. */
static inline bool ble_first_put(ble_first_tag_t *t, const ble_acts_t *out)
{
    int i = ble_acts_find(out, BLE_ACT_DEVSW);
    if (i < 0 || out->a[i].op != BLE_OP_CONFIRM_NEW) return false; /* @first_put */
    t->have = 1;
    t->seq = out->a[i].seq;
    return true;
}

/* После ble_sel_on_devsw_done: слот освобождает только его владелец; запись — только если автомат принял результат. */
static inline bool ble_first_take(ble_first_tag_t *t, const ble_sel_t *s, uint32_t seq, bool job_ok)
{
    if (!t->have || t->seq != seq) return false; /* @first_owner */
    t->have = 0;
    return job_ok && s->seq == seq && s->state == BLE_ST_LIVE && s->conn_valid; /* @first_accept */
}
static inline bool ble_sel_data_mac(const ble_sel_t *s, uint8_t out[6]) {
    if (!s->has_tgt) return false;
    memcpy(out, s->tgt_addr, 6);
    return true;
}

static inline ble_pkt_dec_t ble_sel_accept_pkt(const ble_sel_t *s, bool parse_ok) {
    ble_pkt_dec_t d;
    memset(&d, 0, sizeof d);
    d.count_rx = 1; d.count_bad = parse_ok ? 0 : 1; /* @counters */
    if (!s->conn_valid) { /* @valid_gate */
        if (parse_ok && s->state == BLE_ST_CONFIRMING && !s->first_have) d.first = 1; /* @first_ok */
        return d;
    }
    d.store = parse_ok ? 1 : 0;
    return d;
}

#endif
