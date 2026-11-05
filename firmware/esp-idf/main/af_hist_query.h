#ifndef AF_HIST_QUERY_H
#define AF_HIST_QUERY_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define AF_HQ_KIND_RAW 0
#define AF_HQ_KIND_AGG 1
#define AF_HQ_KIND_ENV 2
#define AF_HQ_LIMIT_MAX 200000u
#define AF_HQ_PROBE_MAX 64u
#define AF_HQ_DEVICE_LEN 18

typedef struct {
    int kind;
    uint32_t since;
    int has_since;
    uint32_t limit;
    uint32_t from;
    int has_from;
    uint32_t to;
    int has_to;
    int ext;
    uint32_t pts; /* e16e (T-34): 0 - выкл.; иначе число точек прореживания 2..2000 */
} af_hq_params_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t lost;
} af_hq_plan_t;

typedef struct {
    char kind[4];
    char seq[11];
    char epoch[11];
    char first_seq[11];
    char lost[11];
    char device[AF_HQ_DEVICE_LEN];
} af_hq_hdrs_t;

typedef int (*af_hq_ts_fn)(void *ctx, uint32_t seq, uint32_t *ts);

static inline int af_hq_parse_u32(const char *s, uint32_t *out) {
    if (s == NULL || out == NULL) return -1;
    size_t len = strlen(s);
    if (len == 0 || len > 10) return -1;
    uint64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c < '0' || c > '9') return -1;
        val = val * 10 + (uint64_t)(c - '0');
        if (val > 4294967295ULL) return -1;
    }
    *out = (uint32_t)val;
    return 0;
}

static inline void af_hq_init(af_hq_params_t *p) {
    if (p == NULL) return;
    p->kind = AF_HQ_KIND_RAW;
    p->since = 0;
    p->has_since = 0;
    p->limit = AF_HQ_LIMIT_MAX;
    p->from = 0;
    p->has_from = 0;
    p->to = 0;
    p->has_to = 0;
    p->ext = 0;
    p->pts = 0;
}

static inline int af_hq_set(af_hq_params_t *p, const char *key, const char *val) {
    if (p == NULL || key == NULL || val == NULL) return -1;

    if (strcmp(key, "kind") == 0) {
        if (strcmp(val, "raw") == 0) { p->kind = AF_HQ_KIND_RAW; return 0; }
        if (strcmp(val, "agg") == 0) { p->kind = AF_HQ_KIND_AGG; return 0; }
        if (strcmp(val, "env") == 0) { p->kind = AF_HQ_KIND_ENV; return 0; }
        return -1;
    }

    if (strcmp(key, "since") == 0) {
        uint32_t v;
        if (af_hq_parse_u32(val, &v) != 0) return -1;
        p->since = v;
        p->has_since = 1;
        return 0;
    }

    if (strcmp(key, "limit") == 0) {
        uint32_t v;
        if (af_hq_parse_u32(val, &v) != 0) return -1;
        if (v == 0 || v > AF_HQ_LIMIT_MAX) {
            p->limit = AF_HQ_LIMIT_MAX;
        } else {
            p->limit = v;
        }
        return 0;
    }

    if (strcmp(key, "from") == 0) {
        uint32_t v;
        if (af_hq_parse_u32(val, &v) != 0) return -1;
        p->from = v;
        p->has_from = 1;
        return 0;
    }

    if (strcmp(key, "to") == 0) {
        uint32_t v;
        if (af_hq_parse_u32(val, &v) != 0) return -1;
        p->to = v;
        p->has_to = 1;
        return 0;
    }

    if (strcmp(key, "pts") == 0) {
        uint32_t v;
        if (af_hq_parse_u32(val, &v) != 0) return -1;
        if (v != 0 && (v < 2 || v > 2000)) return -1;
        p->pts = v;
        return 0;
    }

    if (strcmp(key, "ext") == 0) {
        if (strcmp(val, "0") == 0) { p->ext = 0; return 0; }
        if (strcmp(val, "1") == 0) { p->ext = 1; return 0; }
        return -1;
    }

    return 1;
}

static inline int af_hq_check(const af_hq_params_t *p) {
    if (p == NULL) return -1;
    if (p->has_from && p->has_to && p->from > p->to) return -1;
    return 0;
}

static inline int af_hq_time_filtered(const af_hq_params_t *p) {
    if (p == NULL) return 0;
    return (p->has_from || p->has_to) ? 1 : 0;
}

static inline int af_hq_ts_match(const af_hq_params_t *p, uint32_t ts) {
    if (p == NULL) return 1;
    if (!af_hq_time_filtered(p)) return 1;
    if (ts == 0) return 0;
    if (p->has_from && ts < p->from) return 0;
    if (p->has_to && ts >= p->to) return 0;
    return 1;
}

/* late == 0: результат НЕ позже точного (граница from, начало просмотра); late == 1: результат НЕ раньше точного (граница to, конец просмотра). */
static inline uint32_t af_hq_lower_bound(af_hq_ts_fn fn, void *ctx, uint32_t oldest, uint32_t head, uint32_t t, int late) {
    if (fn == NULL) return oldest;
    uint32_t hi = head - oldest;
    if (hi == 0) return oldest;

    uint32_t lo = 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t m = mid;
        int found = 0;
        uint32_t ts = 0;
        uint32_t steps = 0;

        while (m < hi && steps < AF_HQ_PROBE_MAX) {
            int rc = fn(ctx, oldest + m, &ts);
            if (rc == 0 && ts != 0) {
                found = 1;
                break;
            }
            m++;
            steps++;
        }

        if (found) {
            if (ts >= t) {
                hi = late ? m : mid;
            } else {
                lo = m + 1;
            }
        } else {
            if (late) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
    }
    return oldest + lo;
}

static inline void af_hq_plan(const af_hq_params_t *p, uint32_t oldest, uint32_t head, uint32_t from_seq, uint32_t to_seq, af_hq_plan_t *out) {
    if (out == NULL || p == NULL) return;

    uint32_t count = head - oldest;
    uint32_t start = oldest;
    uint32_t lost = 0;

    if (p->has_since) {
        uint32_t dist = head - p->since;
        if (dist > 0x80000000u) {
            start = head;
            lost = 0;
        } else if (dist > count) {
            start = oldest;
            lost = dist - count;
        } else {
            start = p->since;
        }
    }

    if (p->has_from) {
        uint32_t from_off = from_seq - oldest;
        uint32_t start_off = start - oldest;
        if (from_off > start_off) {
            start = from_seq;
        }
    }

    if ((start - oldest) > count) {
        start = head;
    }

    uint32_t end = head;
    if (p->has_to) {
        end = to_seq;
        uint32_t to_off = to_seq - oldest;
        if (to_off > count) {
            end = head;
        }
    }

    if ((end - oldest) < (start - oldest)) {
        start = end;
    }

    out->start = start;
    out->end = end;
    out->lost = lost;
}

static inline uint32_t af_hq_next_n(uint32_t cur, uint32_t end, uint32_t limit_left, uint32_t max_chunk) {
    uint32_t d = end - cur;
    if (d > 0x80000000u || d == 0) return 0;

    uint32_t res = d;
    if (limit_left < res) res = limit_left;
    if (max_chunk < res) res = max_chunk;
    return res;
}

static inline int af_hq_fmt_mac(const uint8_t a[6], char *buf, size_t cap) {
    if (a == NULL || buf == NULL) return -1;
    if (cap < 18) {
        if (cap > 0) buf[0] = 0;
        return -1;
    }
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        if (i > 0) buf[pos++] = ':';
        pos += sprintf(&buf[pos], "%02x", a[i]);
    }
    buf[pos] = 0;
    return 17;
}

static inline void af_hq_hdrs_fill(af_hq_hdrs_t *h, int kind, uint32_t head_seq, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *mac) {
    if (h == NULL) return;
    memset(h, 0, sizeof *h);

    if (kind == AF_HQ_KIND_RAW) {
        strcpy(h->kind, "raw");
    } else if (kind == AF_HQ_KIND_AGG) {
        strcpy(h->kind, "agg");
    } else if (kind == AF_HQ_KIND_ENV) {
        strcpy(h->kind, "env");
    } else {
        strcpy(h->kind, "raw");
    }

    sprintf(h->seq, "%lu", (unsigned long)head_seq);
    sprintf(h->epoch, "%lu", (unsigned long)epoch);
    sprintf(h->first_seq, "%lu", (unsigned long)first_seq);
    sprintf(h->lost, "%lu", (unsigned long)lost);

    if (mac != NULL) {
        size_t len = strlen(mac);
        if (len > 0 && len < AF_HQ_DEVICE_LEN) {
            memcpy(h->device, mac, len);
            h->device[len] = 0;
        }
    }
}

static inline int af_hq_ext_header(const char *base_hdr, char *buf, size_t cap) {
    if (base_hdr == NULL || buf == NULL || cap == 0) return -1;
    const char *prefix = "mac,seq,epoch,";
    size_t prefix_len = strlen(prefix);
    size_t base_len = strlen(base_hdr);
    size_t total = prefix_len + base_len + 1;
    if (total > cap) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, base_hdr, base_len);
    buf[total - 1] = 0;
    return (int)(total - 1);
}

static inline int af_hq_ext_prefix(const char *mac, uint32_t seq, uint32_t epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) return -1;

    const char *m = (mac != NULL) ? mac : "";
    size_t mac_len = strlen(m);

    if (mac != NULL) {
        for (size_t i = 0; i < mac_len; i++) {
            if (m[i] == ',' || m[i] == '"' || m[i] == '\r' || m[i] == '\n') {
                return -2;
            }
        }
    }

    char seq_buf[11];
    char epoch_buf[11];
    int seq_len = sprintf(seq_buf, "%lu", (unsigned long)seq);
    int epoch_len = sprintf(epoch_buf, "%lu", (unsigned long)epoch);

    size_t total = mac_len + 1 + (size_t)seq_len + 1 + (size_t)epoch_len + 1 + 1;
    if (total > cap) {
        buf[0] = 0;
        return -1;
    }

    size_t pos = 0;
    memcpy(buf, m, mac_len);
    pos += mac_len;
    buf[pos++] = ',';
    memcpy(buf + pos, seq_buf, (size_t)seq_len);
    pos += (size_t)seq_len;
    buf[pos++] = ',';
    memcpy(buf + pos, epoch_buf, (size_t)epoch_len);
    pos += (size_t)epoch_len;
    buf[pos++] = ',';
    buf[pos] = 0;

    return (int)pos;
}

#endif
