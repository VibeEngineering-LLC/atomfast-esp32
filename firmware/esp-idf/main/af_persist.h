#ifndef AF_PERSIST_H
#define AF_PERSIST_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "af_rec.h"
#include "af_env.h"
#include "af_ring.h"

#define AF_SEG_MAGIC      0x31534641u
#define AF_SEG_VER        1
#define AF_SEG_HDR_SIZE   32
#define AF_SEG_KIND_RAW   0
#define AF_SEG_KIND_AGG   1
#define AF_SEG_KIND_ENV   2   /* e10: воздух (T, P), 14-байтные записи */
#define AF_SEG_RAW_RECS   1600u
#define AF_SEG_AGG_RECS   512u
#define AF_SEG_RAW_KEEP   116u
#define AF_SEG_AGG_KEEP   41u
#define AF_SEG_ENV_RECS   1024u
#define AF_SEG_ENV_KEEP   41u   /* 41*1024 = 41984 записей >= 14 сут при периоде 30 с (40320) */
#define AF_FLUSH_INTERVAL_S 300u
#define AF_FLUSH_PENDING_MAX 1200u

typedef struct {
    uint8_t kind;
    uint8_t rec_size;
    uint32_t seg_idx;
    uint32_t first_seq;
    uint32_t epoch;
    uint32_t cap_recs;
} af_seg_hdr_t;

static inline void af_seg_hdr_encode(const af_seg_hdr_t *h, uint8_t out[AF_SEG_HDR_SIZE]) {
    out[0] = (uint8_t)(AF_SEG_MAGIC & 0xFF);
    out[1] = (uint8_t)((AF_SEG_MAGIC >> 8) & 0xFF);
    out[2] = (uint8_t)((AF_SEG_MAGIC >> 16) & 0xFF);
    out[3] = (uint8_t)((AF_SEG_MAGIC >> 24) & 0xFF);
    out[4] = AF_SEG_VER;
    out[5] = h->kind;
    out[6] = h->rec_size;
    out[7] = 0;
    out[8] = (uint8_t)(h->seg_idx & 0xFF);
    out[9] = (uint8_t)((h->seg_idx >> 8) & 0xFF);
    out[10] = (uint8_t)((h->seg_idx >> 16) & 0xFF);
    out[11] = (uint8_t)((h->seg_idx >> 24) & 0xFF);
    out[12] = (uint8_t)(h->first_seq & 0xFF);
    out[13] = (uint8_t)((h->first_seq >> 8) & 0xFF);
    out[14] = (uint8_t)((h->first_seq >> 16) & 0xFF);
    out[15] = (uint8_t)((h->first_seq >> 24) & 0xFF);
    out[16] = (uint8_t)(h->epoch & 0xFF);
    out[17] = (uint8_t)((h->epoch >> 8) & 0xFF);
    out[18] = (uint8_t)((h->epoch >> 16) & 0xFF);
    out[19] = (uint8_t)((h->epoch >> 24) & 0xFF);
    out[20] = (uint8_t)(h->cap_recs & 0xFF);
    out[21] = (uint8_t)((h->cap_recs >> 8) & 0xFF);
    out[22] = (uint8_t)((h->cap_recs >> 16) & 0xFF);
    out[23] = (uint8_t)((h->cap_recs >> 24) & 0xFF);
    for (int i = 24; i < 30; i++) out[i] = 0;
    uint16_t crc = af_crc16(out, 30);
    out[30] = (uint8_t)(crc & 0xFF);
    out[31] = (uint8_t)((crc >> 8) & 0xFF);
}

static inline int af_seg_hdr_decode(const uint8_t in[AF_SEG_HDR_SIZE], af_seg_hdr_t *h) {
    uint32_t magic = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    if (magic != AF_SEG_MAGIC) return -1;
    if (in[4] != AF_SEG_VER) return -2;
    uint16_t crc_calc = af_crc16(in, 30);
    uint16_t crc_stored = (uint16_t)in[30] | ((uint16_t)in[31] << 8);
    if (crc_calc != crc_stored) return -3;
    uint8_t kind = in[5];
    uint8_t rec_size = in[6];
    if (kind > 2) return -4;
    if (kind == 0 && rec_size != 20) return -4;
    if (kind == 1 && rec_size != 28) return -4;
    if (kind == 2 && rec_size != AF_REC_ENV_SIZE) return -4;
    uint32_t cap_recs = (uint32_t)in[20] | ((uint32_t)in[21] << 8) | ((uint32_t)in[22] << 16) | ((uint32_t)in[23] << 24);
    if (cap_recs == 0) return -4;
    h->kind = kind;
    h->rec_size = rec_size;
    h->seg_idx = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    h->first_seq = (uint32_t)in[12] | ((uint32_t)in[13] << 8) | ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);
    h->epoch = (uint32_t)in[16] | ((uint32_t)in[17] << 8) | ((uint32_t)in[18] << 16) | ((uint32_t)in[19] << 24);
    h->cap_recs = cap_recs;
    return 0;
}

typedef struct {
    af_seg_hdr_t hdr;
    uint32_t valid;
    uint32_t good_bytes;
    int tail_bad;
} af_seg_scan_t;

static inline int af_seg_scan(const uint8_t *img, size_t len, af_seg_scan_t *out) {
    if (len < AF_SEG_HDR_SIZE) {
        out->valid = 0;
        out->good_bytes = 0;
        out->tail_bad = 0;
        return -5;
    }
    af_seg_hdr_t hdr;
    int rc = af_seg_hdr_decode(img, &hdr);
    if (rc != 0) {
        out->valid = 0;
        out->good_bytes = 0;
        out->tail_bad = 0;
        return rc;
    }
    out->hdr = hdr;
    uint32_t valid = 0;
    size_t offset = AF_SEG_HDR_SIZE;
    while (valid < hdr.cap_recs) {
        if (offset + hdr.rec_size > len) break;
        int ok = 0;
        if (hdr.kind == AF_SEG_KIND_RAW) {
            af_raw_rec_t tmp;
            ok = (af_raw_decode(img + offset, &tmp) == 0);
        } else if (hdr.kind == AF_SEG_KIND_ENV) {
            af_env_rec_t tmp;
            ok = (af_env_decode(img + offset, &tmp) == 0);
        } else {
            af_agg_rec_t tmp;
            ok = (af_agg_decode(img + offset, &tmp) == 0);
        }
        if (!ok) break;
        valid++;
        offset += hdr.rec_size;
    }
    out->valid = valid;
    out->good_bytes = AF_SEG_HDR_SIZE + valid * hdr.rec_size;
    out->tail_bad = (len > out->good_bytes) ? 1 : 0;
    return 0;
}

typedef struct {
    uint32_t seg_idx;
    uint32_t first_seq;
    uint32_t epoch;
    uint32_t valid;
    uint8_t tail_bad;
    uint8_t hdr_ok;
    uint8_t io_err; /* e8 п.2: ошибка чтения (не битый заголовок): файл не удалять */
} af_seg_info_t;

typedef struct {
    uint32_t keep_from_idx;
    uint32_t load_from;
    uint32_t skip_recs;
    uint32_t total_recs;
    uint32_t first_seq;
    uint32_t end_seq;
    uint32_t epoch;
    uint32_t next_seg_idx;
    uint32_t dropped_gap;
    uint32_t last_pos;
    int append_last;
    int tail_bad_any;
    int have_data;
} af_restore_plan_t;

static inline int af_restore_plan(af_seg_info_t *segs, uint32_t n, uint32_t ring_cap, uint32_t seg_cap, af_restore_plan_t *out) {
    if (out == NULL || ring_cap == 0 || seg_cap == 0 || (segs == NULL && n > 0)) return -1;
    memset(out, 0, sizeof(*out));
    if (n == 0) {
        out->next_seg_idx = 0;
        return 0;
    }
    for (uint32_t i = 1; i < n; i++) {
        af_seg_info_t key = segs[i];
        uint32_t j = i;
        while (j > 0 && segs[j-1].seg_idx > key.seg_idx) {
            segs[j] = segs[j-1];
            j--;
        }
        segs[j] = key;
    }
    uint32_t max_idx = segs[n-1].seg_idx;
    out->next_seg_idx = max_idx + 1;
    int32_t last_pos = -1;
    for (int32_t i = (int32_t)n - 1; i >= 0; i--) {
        if (segs[i].hdr_ok) {
            last_pos = i;
            break;
        }
    }
    if (last_pos < 0) {
        out->have_data = 0;
        out->keep_from_idx = out->next_seg_idx;
        return 0;
    }
    uint32_t run_start = (uint32_t)last_pos;
    uint32_t cur_first = segs[last_pos].first_seq;
    for (int32_t i = last_pos - 1; i >= 0; i--) {
        if (!segs[i].hdr_ok) continue;
        if (segs[i].first_seq + segs[i].valid == cur_first) {
            run_start = (uint32_t)i;
            cur_first = segs[i].first_seq;
        } else {
            break;
        }
    }
    uint32_t dropped_gap = 0;
    for (uint32_t i = 0; i < run_start; i++) {
        if (segs[i].hdr_ok) dropped_gap += segs[i].valid;
    }
    uint32_t run_first_seq = segs[run_start].first_seq;
    uint32_t end_seq = segs[last_pos].first_seq + segs[last_pos].valid;
    uint32_t run_total = end_seq - run_first_seq;
    out->keep_from_idx = segs[run_start].seg_idx;
    out->dropped_gap = dropped_gap;
    uint32_t total_recs = (run_total > ring_cap) ? ring_cap : run_total;
    uint32_t first_seq = end_seq - total_recs;
    uint32_t load_from = run_start;
    uint32_t skip_recs = 0;
    uint32_t acc = 0;
    for (uint32_t i = run_start; i <= (uint32_t)last_pos; i++) {
        if (acc + segs[i].valid > first_seq - run_first_seq) {
            load_from = i;
            skip_recs = (first_seq - run_first_seq) - acc;
            break;
        }
        acc += segs[i].valid;
    }
    out->load_from = load_from;
    out->skip_recs = skip_recs;
    out->total_recs = total_recs;
    out->first_seq = first_seq;
    out->end_seq = end_seq;
    out->last_pos = (uint32_t)last_pos;
    out->append_last = (!segs[last_pos].tail_bad && segs[last_pos].valid < seg_cap) ? 1 : 0;
    int tail_bad_any = 0;
    for (uint32_t i = run_start; i <= (uint32_t)last_pos; i++) {
        if (segs[i].tail_bad) {
            tail_bad_any = 1;
            break;
        }
    }
    out->tail_bad_any = tail_bad_any;
    out->have_data = 1;
    /* e8 п.3: epoch +1 при КАЖДОЙ загрузке с данными: номера seq после обрыва питания переиспользуются,
       клиент с курсором впереди должен увидеть смену epoch. Максимум по читаемым заголовкам. */
    uint32_t max_epoch = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (segs[i].hdr_ok && segs[i].epoch > max_epoch) max_epoch = segs[i].epoch;
    }
    out->epoch = max_epoch + 1;
    return 0;
}

static inline uint32_t af_flush_chunk(uint32_t pending, uint32_t cur_valid, uint32_t seg_cap) {
    if (cur_valid >= seg_cap) return 0;
    uint32_t space = seg_cap - cur_valid;
    return (pending < space) ? pending : space;
}

static inline int af_flush_due(uint32_t now_s, uint32_t last_flush_s, uint32_t interval_s, uint32_t pending) {
    if (pending == 0) return 0;
    if (pending >= AF_FLUSH_PENDING_MAX) return 1;
    if (now_s < last_flush_s) return 1;
    if (now_s - last_flush_s >= interval_s) return 1;
    return 0;
}

static inline int af_persist_ring_finish(af_ring_t *r, uint32_t end_seq, uint32_t epoch) {
    if (r == NULL) return -1;
    if (r->count > end_seq) return -1;
    r->seq = end_seq;
    r->epoch = epoch;
    r->dropped = 0;
    return 0;
}

#endif
