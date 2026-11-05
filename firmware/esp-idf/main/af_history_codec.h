#ifndef AF_HISTORY_CODEC_H
#define AF_HISTORY_CODEC_H

#include "af_rec.h"
#include "af_env.h"
#include "af_hist_query.h"

/* The caller keeps the ring kind and transport cursor. */
typedef struct {
    uint32_t ts;
    union {
        af_raw_rec_t raw;
        af_agg_rec_t agg;
        af_env_rec_t env;
    } rec;
} af_history_row_t;

static inline size_t af_history_record_size(int kind) {
    switch (kind) {
        case AF_HQ_KIND_RAW: return AF_REC_RAW_SIZE;
        case AF_HQ_KIND_AGG: return AF_REC_AGG_SIZE;
        case AF_HQ_KIND_ENV: return AF_REC_ENV_SIZE;
        default: return 0;
    }
}

static inline int af_history_row_decode(int kind, const uint8_t *data, af_history_row_t *row) {
    if (!data || !row) return -1;
    int rc;
    switch (kind) {
        case AF_HQ_KIND_RAW:
            rc = af_raw_decode(data, &row->rec.raw);
            if (rc == 0) row->ts = row->rec.raw.ts;
            return rc;
        case AF_HQ_KIND_AGG:
            rc = af_agg_decode(data, &row->rec.agg);
            if (rc == 0) row->ts = row->rec.agg.ts;
            return rc;
        case AF_HQ_KIND_ENV:
            rc = af_env_decode(data, &row->rec.env);
            if (rc == 0) row->ts = row->rec.env.ts;
            return rc;
        default: return -1;
    }
}

/* Prefixing with device/sequence/epoch remains the caller's operation. */
static inline int af_history_row_csv(int kind, const af_history_row_t *row, char *buf, size_t cap) {
    if (!row) return -1;
    switch (kind) {
        case AF_HQ_KIND_RAW: return af_raw_csv(&row->rec.raw, NULL, buf, cap);
        case AF_HQ_KIND_AGG: return af_agg_csv(&row->rec.agg, NULL, buf, cap);
        case AF_HQ_KIND_ENV: return af_env_csv(&row->rec.env, buf, cap);
        default: return -1;
    }
}

static inline int af_history_csv_header(int kind, int ext, char *buf, size_t cap) {
    if (af_history_record_size(kind) == 0) return -1;
    if (ext != 1) {
        return kind == AF_HQ_KIND_ENV ? af_env_csv_header(buf, cap) : af_csv_header(0, buf, cap);
    }
    char base[96];
    int n = kind == AF_HQ_KIND_ENV ? af_env_csv_header(base, sizeof(base)) : af_csv_header(0, base, sizeof(base));
    if (n < 0) return -1;
    return af_hq_ext_header(base, buf, cap);
}

#endif
