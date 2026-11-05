#ifndef BLE_SEL_CMD_H
#define BLE_SEL_CMD_H
#include "ble_sel.h"
#include <string.h>

/* Выбор кандидата из списка (HTTP /select) */
static inline int ble_sel_cmd_select(ble_sel_t *s, uint32_t gen, uint8_t tok, bool confirm_clear, bool meas_active, uint32_t ring_count, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    if (s->state == BLE_ST_LIVE) return BLE_E_LIVE;
    if (s->state == BLE_ST_CONFIRMING) return BLE_E_BUSY;
    if (s->state == BLE_ST_FORGETTING) return BLE_E_BUSY;
    if (gen != s->gen) return BLE_E_STALE; /* @sel_gen */
    int i = ble_cands_find_tok(&s->cands, tok); /* @sel_tok */
    if (i < 0) return BLE_E_NO_CAND;
    const ble_cand_t *c = &s->cands.c[i];
    bool need = (!s->has_tgt || memcmp(c->addr, s->tgt_addr, 6) != 0) && ring_count > 0;
    if (need) {
        int r = ble_sel__meas_block(meas_active);
        if (r != BLE_OK) return r;
        if (!confirm_clear) return BLE_E_NEED_CLEAR;
    }
    if (s->state == BLE_ST_SCAN) {
        s->pending = BLE_PEND_SELECT;
        s->pending_tok = tok;
        s->pending_gen = gen;
        ble_sel__scan_stop(s, now_ms, out);
        return BLE_OK;
    }
    if (s->state == BLE_ST_SCAN_STOPPING) {
        s->pending = BLE_PEND_SELECT;
        s->pending_tok = tok;
        s->pending_gen = gen;
        return BLE_OK;
    }
    if (s->state == BLE_ST_OPENING) return ble_sel__defer(s, BLE_PEND_SELECT, tok, gen);
    if (s->state == BLE_ST_LIST) {
        ble_sel__select_idx(s, i, now_ms, out);
        return BLE_OK;
    }
    if (s->state == BLE_ST_WAIT) {
        ble_sel__select_idx(s, i, now_ms, out);
        return BLE_OK;
    }
    return BLE_E_BAD;
}

/* Перезапуск сканирования (HTTP /rescan) */
static inline int ble_sel_cmd_rescan(ble_sel_t *s, bool replace, bool meas_active, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    if (s->state == BLE_ST_LIVE) {
        if (!replace) return BLE_E_NEED_REPLACE; /* @rescan_replace */
        int r = ble_sel__meas_block(meas_active);
        if (r != BLE_OK) return r;
        s->conn_valid = 0;
        ble_acts_push(out, BLE_ACT_CLOSE_CUR);
        s->switching = 1;
        s->close_wait = 1;
        s->close_tries = 0;
        s->switch_t0_ms = now_ms;
        return BLE_OK;
    }
    if (s->state == BLE_ST_LIST || s->state == BLE_ST_WAIT) {
        if (!ble__due(now_ms, s->cooldown_until_ms)) return BLE_E_COOLDOWN;
        return ble_sel_scan_begin(s, now_ms, out);
    }
    if (s->state == BLE_ST_SCAN) return BLE_E_BUSY;
    if (s->state == BLE_ST_SCAN_STOPPING) {
        s->pending = BLE_PEND_RESCAN;
        return BLE_OK;
    }
    if (s->state == BLE_ST_OPENING) return ble_sel__defer(s, BLE_PEND_RESCAN, 0, 0); /* @rescan_opening */
    if (s->state == BLE_ST_CONFIRMING) return BLE_E_BUSY;
    if (s->state == BLE_ST_FORGETTING) return BLE_E_BUSY;
    return BLE_E_BAD;
}

/* Забыть текущую цель (HTTP /forget) */
static inline int ble_sel_cmd_forget(ble_sel_t *s, bool confirm, bool meas_active, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    if (!confirm) return BLE_E_BAD; /* @forget_confirm */
    if (s->state == BLE_ST_FORGETTING) return BLE_OK;
    int r = ble_sel__meas_block(meas_active);
    if (r != BLE_OK) return r;
    if (s->state == BLE_ST_LIVE || s->state == BLE_ST_CONFIRMING) {
        s->conn_valid = 0; /* @forget_valid */
        ble_acts_push(out, BLE_ACT_CLOSE_CUR); s->switching = 1; s->pending = BLE_PEND_FORGET; /* @forget_live */
        s->close_wait = 1;
        s->close_tries = 0;
        s->switch_t0_ms = now_ms;
        return BLE_OK;
    }
    if (s->state == BLE_ST_OPENING) return ble_sel__defer(s, BLE_PEND_FORGET, 0, 0);
    if (s->state == BLE_ST_SCAN) {
        s->pending = BLE_PEND_FORGET;
        ble_sel__scan_stop(s, now_ms, out);
        return BLE_OK;
    }
    if (s->state == BLE_ST_SCAN_STOPPING) {
        s->pending = BLE_PEND_FORGET;
        return BLE_OK;
    }
    if (s->state == BLE_ST_LIST || s->state == BLE_ST_WAIT) {
        ble_sel__devsw(s, BLE_OP_FORGET, now_ms, out);
        return BLE_OK;
    }
    return BLE_E_BAD;
}

/* Отмена выбора — возврат к целевому прибору */
static inline int ble_sel_cmd_cancel(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    if (s->state == BLE_ST_LIST && s->has_tgt) {
        ble_sel__to_rest(s, now_ms);
        return BLE_OK;
    }
    return BLE_E_BUSY;
}

/* Повторное подключение (кнопка reconnect) */
static inline int ble_sel_cmd_reconnect(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    if (s->state == BLE_ST_LIVE) {
        ble_acts_push(out, BLE_ACT_CLOSE_CUR);
        s->delay_ms = BLE_SEL_DELAY0_MS;
        s->fail_streak = 0;
        return BLE_OK;
    }
    if (s->state == BLE_ST_WAIT) {
        s->next_try_ms = now_ms;
        return BLE_OK;
    }
    return BLE_E_BUSY;
}

/* Периодический тик: таймауты и повторы закрытия */
static inline int ble_sel_tick(ble_sel_t *s, uint32_t now_ms, ble_acts_t *out)
{
    ble_acts_reset(out);
    /* (a) скан длится достаточно — останавливаем */
    if (s->state == BLE_ST_SCAN && (uint32_t)(now_ms - s->scan_t0_ms) >= BLE_SEL_SCAN_MS) { ble_sel__scan_stop(s, now_ms, out); return BLE_OK; } /* @scan_end */
    /* (b) дедлайн остановки скана */
    if (s->state == BLE_ST_SCAN_STOPPING && (uint32_t)(now_ms - s->stop_t0_ms) >= BLE_SEL_STOP_DEADLINE_MS) { ble_acts_push(out, BLE_ACT_SCAN_STOP); ble_sel__stop_done(s, now_ms, out); return BLE_OK; } /* @stop_deadline */
    /* (b) повтор запроса остановки */
    if (s->state == BLE_ST_SCAN_STOPPING && s->stop_tries < BLE_SEL_STOP_TRIES && (uint32_t)(now_ms - s->stop_last_ms) >= BLE_SEL_STOP_RETRY_MS) {
        ble_acts_push(out, BLE_ACT_SCAN_STOP);
        s->stop_tries++;
        s->stop_last_ms = now_ms;
        return BLE_OK;
    }
    /* (c) таймаут выбора в LIST — возврат к tgt */
    if (s->state == BLE_ST_LIST && s->has_tgt && (uint32_t)(now_ms - s->list_t0_ms) >= BLE_SEL_CHOOSE_MS) { ble_sel__to_rest(s, now_ms); return BLE_OK; } /* @list_return */
    /* (d) WAIT: время попробовать открыть tgt */
    if (s->state == BLE_ST_WAIT && s->has_tgt && !s->connected && !s->open_inflight && ble__due(now_ms, s->next_try_ms)) { /* @tick_open */
        memcpy(s->open_req_name, s->tgt_name, BLE_SEL_NAME_LEN);
        ble_sel__open(s, s->tgt_addr, s->tgt_atype, 0, now_ms, out);
        return BLE_OK;
    }
    /* (e) CONFIRMING: не получили первый пакет */
    if (s->state == BLE_ST_CONFIRMING && !s->first_have && (uint32_t)(now_ms - s->confirm_t0_ms) >= BLE_SEL_FIRST_PKT_MS) { ble_acts_push(out, BLE_ACT_CLOSE_CUR); /* @first_timeout */
        s->err = BLE_NOTE_INCOMPATIBLE;
        s->seq++;
        ble_sel__to_rest(s, now_ms);
        return BLE_OK;
    }
    /* (f) таймаут devsw в CONFIRMING/FORGETTING */
    if ((s->state == BLE_ST_CONFIRMING || s->state == BLE_ST_FORGETTING) && s->devsw_wait && !s->devsw_commit && (uint32_t)(now_ms - s->devsw_t0_ms) >= BLE_SEL_DEVSW_MAX_MS) {
        s->seq++;
        s->devsw_wait = 0;
        s->err = BLE_NOTE_ABORTED;
        if (s->state == BLE_ST_CONFIRMING) {
            ble_acts_push(out, BLE_ACT_CLOSE_CUR);
        }
        ble_sel__to_rest(s, now_ms);
        return BLE_OK;
    }
    /* (g) повтор закрытия при close_wait */
    if (s->close_wait && s->connected && (uint32_t)(now_ms - s->switch_t0_ms) >= BLE_SEL_SWITCH_CLOSE_RETRY_MS) {
        if (s->close_tries < BLE_SEL_CLOSE_RETRY_MAX) { ble_acts_push(out, BLE_ACT_CLOSE_CUR); s->close_tries++; s->switch_t0_ms = now_ms; return BLE_OK; } /* @close_retry */
        s->close_wait = 0;
        s->switching = 0;
        s->pending = BLE_PEND_NONE;
        s->err = BLE_NOTE_CLOSE_FAILED;
        { ble_act_t *na = ble_acts_push(out, BLE_ACT_NOTE_ERR); if (na) na->err = BLE_NOTE_CLOSE_FAILED; }
        if (s->state == BLE_ST_LIVE) {
            s->conn_valid = 1;
            ble_acts_push(out, BLE_ACT_SET_VALID_SAME);
        }
        return BLE_OK;
    }
    return BLE_OK;
}

#endif
