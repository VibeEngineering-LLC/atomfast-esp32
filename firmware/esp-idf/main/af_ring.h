#ifndef AF_RING_H
#define AF_RING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AF_RING_MAX_REC 64

typedef enum {
    AF_RING_OVERWRITE = 0,
    AF_RING_REJECT = 1
} af_ring_policy_t;

typedef struct {
    uint8_t *buf;
    uint32_t rec_size;
    uint32_t cap_max;
    uint32_t limit;
    uint32_t tail;
    uint32_t count;
    uint32_t seq;
    uint32_t dropped;
    uint32_t rejected;
    uint32_t epoch;
    af_ring_policy_t policy;
} af_ring_t;

typedef struct {
    uint32_t atomic_limit;
    uint32_t agg_limit;
} af_quota_t;

static inline int af_ring_init(af_ring_t *r, uint8_t *storage, size_t storage_bytes, uint32_t rec_size, uint32_t epoch, af_ring_policy_t policy) {
    if (!r || !storage || rec_size == 0 || rec_size > AF_RING_MAX_REC) {
        return -1;
    }
    if (storage_bytes / rec_size == 0) {
        return -1;
    }
    if (policy > AF_RING_REJECT) {
        return -1;
    }

    r->buf = storage;
    r->rec_size = rec_size;
    r->cap_max = (uint32_t)(storage_bytes / rec_size);
    r->limit = r->cap_max;
    r->tail = 0;
    r->count = 0;
    r->seq = 0;
    r->dropped = 0;
    r->rejected = 0;
    r->epoch = epoch;
    r->policy = policy;
    return 0;
}

static inline int af_ring_push(af_ring_t *r, const void *rec) {
    if (!r || !rec) {
        return -2;
    }

    if (r->count < r->limit) {
        uint32_t idx = (r->tail + r->count) % r->cap_max;
        uint8_t *dst = r->buf + (size_t)idx * r->rec_size;
        memcpy(dst, rec, r->rec_size);
        r->count++;
        r->seq++;
        return 0;
    }

    if (r->policy == AF_RING_OVERWRITE) {
        uint32_t idx = (r->tail + r->count) % r->cap_max;
        uint8_t *dst = r->buf + (size_t)idx * r->rec_size;
        memcpy(dst, rec, r->rec_size);
        r->tail = (r->tail + 1) % r->cap_max;
        r->dropped++;
        r->seq++;
        return 1;
    }

    r->rejected++;
    return -1;
}

static inline uint32_t af_ring_count(const af_ring_t *r) {
    if (!r) return 0;
    return r->count;
}

static inline int af_ring_get(const af_ring_t *r, uint32_t i, void *out) {
    if (!r || !out) return -1;
    if (i >= r->count) return -1;

    uint32_t idx = (r->tail + i) % r->cap_max;
    const uint8_t *src = r->buf + (size_t)idx * r->rec_size;
    memcpy(out, src, r->rec_size);
    return 0;
}

static inline uint32_t af_ring_oldest_seq(const af_ring_t *r) {
    if (!r) return 0;
    return r->seq - r->count;
}

static inline uint32_t af_ring_read_since(const af_ring_t *r, uint32_t since_seq, void *out, uint32_t max_recs, uint32_t *next_seq, uint32_t *lost) {
    if (!r) {
        if (next_seq) *next_seq = 0;
        if (lost) *lost = 0;
        return 0;
    }

    uint32_t oldest = r->seq - r->count;
    uint32_t n = 0;
    uint32_t l = 0;
    uint32_t first_seq = 0;

    if (since_seq == r->seq) {
        l = 0;
        first_seq = r->seq;
    } else {
        uint32_t dist = r->seq - since_seq;
        if (dist > r->count && dist <= 0x80000000u) {
            l = dist - r->count;
            first_seq = oldest;
        } else if (dist > 0x80000000u) {
            l = 0;
            first_seq = r->seq;
        } else {
            l = 0;
            first_seq = since_seq;
        }
    }

    if (next_seq) *next_seq = first_seq;
    if (lost) *lost = l;

    if (max_recs == 0 || !out) {
        return 0;
    }

    uint32_t start_idx = first_seq - oldest;
    if (start_idx > r->count) {
        return 0;
    }

    uint32_t available = r->count - start_idx;
    if (max_recs < available) {
        n = max_recs;
    } else {
        n = available;
    }

    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (r->tail + start_idx + i) % r->cap_max;
        const uint8_t *src = r->buf + (size_t)idx * r->rec_size;
        uint8_t *dst = out + (size_t)i * r->rec_size;
        memcpy(dst, src, r->rec_size);
    }

    if (next_seq) *next_seq = first_seq + n;
    return n;
}

static inline void af_ring_clear(af_ring_t *r) {
    if (!r) return;
    r->count = 0;
    r->tail = 0;
    r->epoch++;
}

static inline int af_ring_set_limit(af_ring_t *r, uint32_t new_limit) {
    if (!r) return -1;
    if (new_limit == 0 || new_limit > r->cap_max) {
        return -1;
    }

    if (r->count > new_limit) {
        uint32_t k = r->count - new_limit;
        r->tail = (r->tail + k) % r->cap_max;
        r->count = new_limit;
        r->dropped += k;
    }

    r->limit = new_limit;
    return 0;
}

static inline int af_quota_split(uint32_t total_bytes, uint32_t atomic_rec, uint32_t agg_rec, int atomic_on, int agg_on, uint32_t agg_period_s, uint32_t agg_retention_s, af_quota_t *out) {
    if (!out) return -1;
    if (atomic_rec == 0 || agg_rec == 0) return -1;
    if (agg_on && (agg_period_s == 0 || agg_retention_s == 0)) return -1;

    if (!atomic_on && !agg_on) {
        out->atomic_limit = 0;
        out->agg_limit = 0;
        return 0;
    }

    uint64_t agg_need = 0;
    if (agg_on) {
        agg_need = (uint64_t)agg_retention_s / agg_period_s;
        if (agg_retention_s % agg_period_s != 0) {
            agg_need++;
        }
        if (agg_need > UINT32_MAX) {
            agg_need = UINT32_MAX;
        }
    }

    if (agg_on && !atomic_on) {
        uint32_t max_by_bytes = total_bytes / agg_rec;
        uint32_t limit = (uint32_t)agg_need;
        if (limit > max_by_bytes) {
            limit = max_by_bytes;
        }
        out->agg_limit = limit;
        out->atomic_limit = 0;
        return 0;
    }

    if (atomic_on && !agg_on) {
        out->atomic_limit = total_bytes / atomic_rec;
        out->agg_limit = 0;
        return 0;
    }

    uint32_t max_by_bytes = total_bytes / agg_rec;
    uint32_t agg_limit = (uint32_t)agg_need;
    if (agg_limit > max_by_bytes) {
        agg_limit = max_by_bytes;
    }
    out->agg_limit = agg_limit;

    uint64_t agg_bytes = (uint64_t)agg_limit * agg_rec;
    if (agg_bytes > total_bytes) {
        out->atomic_limit = 0;
    } else {
        uint64_t remaining = total_bytes - agg_bytes;
        out->atomic_limit = (uint32_t)(remaining / atomic_rec);
    }

    return 0;
}

#endif
