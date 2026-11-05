#ifndef AF_HIST_CORE_H
#define AF_HIST_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "af_store_core.h"
#include "af_hist_query.h"

typedef struct {
    const af_store_core_t *c;
    int kind;
} af_hc_ctx_t;

static inline const af_ring_t *af_hc_ring(const af_store_core_t *c, int kind) {
    if (c == NULL) {
        return NULL;
    }
    if (kind == AF_HQ_KIND_RAW) {
        return &c->raw;
    }
    if (kind == AF_HQ_KIND_AGG) {
        return &c->agg;
    }
    if (kind == AF_HQ_KIND_ENV) {
        if (c->env.buf == NULL) {
            return NULL;
        }
        return &c->env;
    }
    return NULL;
}

static inline int af_hc_ts_at(void *ctx, uint32_t seq, uint32_t *ts) {
    if (ctx == NULL || ts == NULL) {
        return -1;
    }
    af_hc_ctx_t *c_ctx = (af_hc_ctx_t *)ctx;
    if (c_ctx->c == NULL) {
        return -1;
    }
    const af_ring_t *r = af_hc_ring(c_ctx->c, c_ctx->kind);
    if (r == NULL) {
        return -1;
    }
    uint32_t oldest = af_ring_oldest_seq(r);
    uint32_t off = seq - oldest;
    if (off >= r->count) {
        return -1;
    }
    uint8_t rec[AF_RING_MAX_REC];
    if (af_ring_get(r, off, rec) != 0) {
        return -1;
    }
    if (c_ctx->kind == AF_HQ_KIND_RAW) {
        af_raw_rec_t r_rec;
        if (af_raw_decode(rec, &r_rec) != 0) {
            return -1;
        }
        *ts = r_rec.ts;
    } else if (c_ctx->kind == AF_HQ_KIND_AGG) {
        af_agg_rec_t a_rec;
        if (af_agg_decode(rec, &a_rec) != 0) {
            return -1;
        }
        *ts = a_rec.ts;
    } else if (c_ctx->kind == AF_HQ_KIND_ENV) {
        af_env_rec_t e_rec;
        if (af_env_decode(rec, &e_rec) != 0) {
            return -1;
        }
        *ts = e_rec.ts;
    } else {
        return -1;
    }
    return 0;
}

static inline int af_core_plan(const af_store_core_t *c, const af_hq_params_t *p, af_hq_plan_t *plan, uint32_t *head, uint32_t *epoch) {
    if (c == NULL || p == NULL || plan == NULL) {
        return -1;
    }
    const af_ring_t *r = af_hc_ring(c, p->kind);
    if (r == NULL) {
        return -1;
    }
    uint32_t oldest = af_ring_oldest_seq(r);
    uint32_t hd = r->seq;
    uint32_t from_seq = oldest;
    uint32_t to_seq = hd;
    af_hc_ctx_t ctx = {c, p->kind};
    if (p->has_from) {
        from_seq = af_hq_lower_bound(af_hc_ts_at, &ctx, oldest, hd, p->from, 0);
    }
    if (p->has_to) {
        to_seq = af_hq_lower_bound(af_hc_ts_at, &ctx, oldest, hd, p->to, 1);
    }
    af_hq_plan(p, oldest, hd, from_seq, to_seq, plan);
    if (head != NULL) {
        *head = hd;
    }
    if (epoch != NULL) {
        *epoch = r->epoch;
    }
    return 0;
}

#endif
