#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "af_meas.h"

static inline int af_meas_add_journal(af_meas_core_t *c, const char *name, const af_meas_res_t *res, uint32_t from, uint32_t *id_out) {
    if (c == NULL || res == NULL) return AF_MEAS_E_VALUE;
    if (name == NULL) name = "";
    if (am_strnlen(name, AF_MEAS_NAME_MAX) >= AF_MEAS_NAME_MAX || af_meas_utf8_ok(name, strlen(name)) != 1) return AF_MEAS_E_VALUE;
    if (c->alloc == NULL) return AF_MEAS_E_NOMEM;
    int slot = am_slot(c);
    if (slot < 0) return AF_MEAS_E_BUSY;
    void *buf = c->alloc(sizeof(af_meas_res_t));
    if (buf == NULL) return AF_MEAS_E_NOMEM;
    memset(buf, 0, sizeof(af_meas_res_t));
    af_meas_run_t *r = &c->runs[slot];
    memset(r, 0, sizeof *r);
    r->id = ++c->next_id;
    if (r->id == 0) r->id = ++c->next_id;
    memcpy(r->name, name, am_strnlen(name, AF_MEAS_NAME_MAX - 1));
    r->mode = AF_MEAS_MODE_JOURNAL;
    r->state = AF_MEAS_ST_DONE;
    r->phase = AF_MEAS_PH_NONE;
    r->end_reason = AF_MEAS_SR_TIME;
    r->stop_mode = AF_MEAS_STOP_TIME;
    r->start_mode = AF_MEAS_START_NOW;
    r->n = 1;
    r->done_i = 1;
    r->cur_i = 1;
    r->t_ms = res->t_ms;
    r->start_utc = from;
    r->res = (af_meas_res_t *)buf;
    r->res_cap = 1;
    am_res_push(r, res);
    char msg[96];
    snprintf(msg, sizeof msg, "m%u journal npk=%u", (unsigned)r->id, (unsigned)res->npk);
    am_log(c, 1, msg);
    if (id_out != NULL) *id_out = r->id;
    return 0;
}
