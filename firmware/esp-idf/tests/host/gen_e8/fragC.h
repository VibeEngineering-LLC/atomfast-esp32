#include <stdarg.h>

typedef int (*af_push_info_fn)(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch);
typedef uint32_t (*af_push_read_fn)(void *ctx, int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost);

typedef struct {
    uint32_t first_seq;
    uint32_t last_seq;
    uint32_t rows;
    uint32_t scan_end;
    uint32_t lost;
    size_t len;
} af_push_pack_t;

typedef struct {
    uint8_t state;
    int16_t http;
    uint32_t ms;
    int32_t accepted;
    uint8_t err;
    uint32_t ts;
    uint8_t legacy;      /* 1 - приёмник ответил 200 без "test":true (старая версия) */
} af_push_test_t;

typedef struct {
    uint8_t present;
    uint32_t oldest;
    uint32_t head;
    uint32_t epoch;
} af_push_ringinfo_t;

typedef struct {
    int sending;
    int auth_stop;
    uint32_t stack_hwm;
    uint32_t now_ts;
    af_push_ring_t ring[3];
    af_push_ringinfo_t info[3];
    af_push_test_t test;
} af_push_view_t;

static inline const char *af_push_kind_name(int kind) {
    switch (kind) {
        case 0: return "raw";
        case 1: return "agg";
        case 2: return "env";
        default: return "raw";
    }
}

static inline const char *af_push_path(int kind) {
    switch (kind) {
        case 0: return "/ingest/raw";
        case 1: return "/ingest/agg";
        case 2: return "/ingest/env";
        default: return "/ingest/raw";
    }
}

#define AF_PUSH_TEST_HDR "X-AF-Test"

/* список заголовков X-AF-* запроса (кроме Authorization/Content-Type); test=1 добавляет X-AF-Test: 1. Возврат: число пар (<=6) */
static inline int af_push_hdr_list(const af_hq_hdrs_t *hh, int test, const char *n[6], const char *v[6])
{
    int k = 0;
    if (!hh || !n || !v) return 0;
    if (hh->device[0] != 0) { n[k] = "X-AF-Device"; v[k++] = hh->device; }
    n[k] = "X-AF-Ring"; v[k++] = hh->kind;
    n[k] = "X-AF-Epoch"; v[k++] = hh->epoch;
    n[k] = "X-AF-First-Seq"; v[k++] = hh->first_seq;
    n[k] = "X-AF-Lost"; v[k++] = hh->lost;
    if (test) { n[k] = AF_PUSH_TEST_HDR; v[k++] = "1"; }
    return k;
}

static inline int af_push_test_body(char *buf, size_t cap) {
    char base[96];
    int n = af_env_csv_header(base, sizeof base);
    if (n < 0) return -1;
    return af_hq_ext_header(base, buf, cap);
}

static inline int af_push_pack(af_push_read_fn rd, void *ctx, int kind, uint32_t start, uint32_t max_rows, const char *mac, uint32_t epoch, uint32_t lost0, char *buf, size_t cap, af_push_pack_t *out) {
    if (!rd || !buf || !out || !mac || mac[0] == '\0' || (kind < 0 || kind > 2) || cap < (256 + AF_PUSH_ROW_MAX) || max_rows == 0) return -1;
    memset(out, 0, sizeof(*out));
    out->lost = lost0;
    out->scan_end = start;

    char base[96];
    int n;
    if (kind == AF_HQ_KIND_ENV) n = af_env_csv_header(base, sizeof base);
    else n = af_csv_header(0, base, sizeof base);
    if (n < 0) return -2;

    size_t len = (size_t)af_hq_ext_header(base, buf, cap);
    if (len == (size_t)-1) return -2;

    uint8_t rec[32 * 28];
    uint32_t cur = start;
    uint32_t rows = 0;
    int first_read = 1;
    int full = 0;

    while (rows < max_rows && !full) {
        if (cap - len < AF_PUSH_ROW_MAX) break;
        uint32_t chunk = (max_rows - rows < 32) ? (max_rows - rows) : 32;
        uint32_t next = 0, lost = 0;
        uint32_t cnt = rd(ctx, kind, cur, rec, chunk, &next, &lost);
        if (first_read) {
            out->lost += lost;
            first_read = 0;
        } else if (lost != 0) {
            break;
        }
        if (cnt == 0) break;
        uint32_t seq0 = next - cnt;
        size_t rec_sz = (kind == 0) ? AF_REC_RAW_SIZE : (kind == 1) ? AF_REC_AGG_SIZE : AF_REC_ENV_SIZE;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t seq = seq0 + i;
            if (rows == max_rows) { full = 1; break; }
            if (cap - len < AF_PUSH_ROW_MAX) { full = 1; break; }
            const uint8_t *p = rec + i * rec_sz;
            int dec_ok = 0;
            af_raw_rec_t r_raw = {0}; af_agg_rec_t r_agg = {0}; af_env_rec_t r_env = {0};
            if (kind == 0) dec_ok = (af_raw_decode(p, &r_raw) == 0);
            else if (kind == 1) dec_ok = (af_agg_decode(p, &r_agg) == 0);
            else dec_ok = (af_env_decode(p, &r_env) == 0);
            if (!dec_ok) { out->scan_end = seq + 1; continue; }
            int pl = af_hq_ext_prefix(mac, seq, epoch, buf + len, cap - len);
            if (pl == -2) return -3;
            if (pl < 0) { full = 1; break; }
            int rl = -1;
            if (kind == 0) rl = af_raw_csv(&r_raw, NULL, buf + len + pl, cap - len - pl);
            else if (kind == 1) rl = af_agg_csv(&r_agg, NULL, buf + len + pl, cap - len - pl);
            else rl = af_env_csv(&r_env, buf + len + pl, cap - len - pl);
            if (rl < 0) { buf[len] = 0; out->scan_end = seq + 1; continue; }
            len += (size_t)pl + (size_t)rl;
            if (rows == 0) out->first_seq = seq;
            rows++;
            out->last_seq = seq;
            out->scan_end = seq + 1;
        }
        if (!full) cur = next;
    }
    out->rows = rows;
    buf[len] = 0;
    out->len = len;
    return 0;
}

static inline int af_push_app(char *buf, size_t cap, int pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static inline int af_push_app(char *buf, size_t cap, int pos, const char *fmt, ...) {
    if (pos < 0 || (size_t)pos >= cap) return -1;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + pos, cap - (size_t)pos, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)(pos + r) >= cap) return -1;
    return pos + r;
}

static inline int af_push_json(char *buf, size_t cap, const af_push_cfg_t *c, const af_push_view_t *v) {
    if (!buf || !c || !v) return -1;
    int pos = 0;
    pos = af_push_app(buf, cap, pos, "{\"enabled\":%s", c->enabled ? "true" : "false");
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"host\":\"%s\"", c->host);
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"port\":%lu", (unsigned long)c->port);
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"token_set\":%s", c->token[0] ? "true" : "false");
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"interval_s\":%lu", (unsigned long)c->interval_s);
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"batch_rows\":%lu", (unsigned long)c->batch_rows);
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"timeout_s\":%lu", (unsigned long)c->timeout_s);
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"rings\":[");
    if (pos < 0) return -1;
    int first = 1;
    for (int i = 0; i < 3; i++) {
        if (c->rings & (1 << i)) {
            if (!first) pos = af_push_app(buf, cap, pos, ",");
            if (pos < 0) return -1;
            pos = af_push_app(buf, cap, pos, "\"%s\"", af_push_kind_name(i));
            if (pos < 0) return -1;
            first = 0;
        }
    }
    pos = af_push_app(buf, cap, pos, "],\"warn\":{\"no_tls\":true,\"public_addr\":%s}", af_push_addr_class(c->host) == 0 ? "true" : "false");
    if (pos < 0) return -1;
    int bad = af_push_bad(v->ring, c->rings, (uint8_t)v->auth_stop);
    pos = af_push_app(buf, cap, pos, ",\"state\":\"%s\"", af_push_state_name(c->enabled, v->sending, bad));
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"auth_stop\":%s", v->auth_stop ? "true" : "false");
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"stack_hwm\":%lu", (unsigned long)v->stack_hwm);
    if (pos < 0) return -1;
    const char *tstate = (v->test.state == 0) ? "idle" : (v->test.state == 1) ? "running" : "done";
    pos = af_push_app(buf, cap, pos, ",\"test\":{\"state\":\"%s\",\"http\":%ld,\"ms\":%lu,\"accepted\":%ld,\"err\":\"%s\",\"ts\":%lu,\"legacy\":%s}", tstate, (long)v->test.http, (unsigned long)v->test.ms, (long)v->test.accepted, af_push_err_name(v->test.err), (unsigned long)v->test.ts, v->test.legacy ? "true" : "false");
    if (pos < 0) return -1;
    pos = af_push_app(buf, cap, pos, ",\"ring\":{");
    if (pos < 0) return -1;
    for (int i = 0; i < 3; i++) {
        if (i > 0) pos = af_push_app(buf, cap, pos, ",");
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, "\"%s\":{", af_push_kind_name(i));
        if (pos < 0) return -1;
        int on = (c->rings & (1 << i)) ? 1 : 0;
        int present = v->info[i].present;
        pos = af_push_app(buf, cap, pos, "\"on\":%s,\"present\":%s", on ? "true" : "false", present ? "true" : "false");
        if (pos < 0) return -1;
        long acked = v->ring[i].cur.valid ? (long)v->ring[i].cur.acked : -1L;
        pos = af_push_app(buf, cap, pos, ",\"acked_seq\":%ld", acked);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"epoch\":%lu", (unsigned long)v->info[i].epoch);
        if (pos < 0) return -1;
        uint32_t pending = 0;
        if (present) {
            af_push_plan_t p;
            af_push_plan(&v->ring[i].cur, v->info[i].oldest, v->info[i].head, v->info[i].epoch, 0xFFFFFFFFu, &p);
            pending = p.pending;
        }
        pos = af_push_app(buf, cap, pos, ",\"pending\":%lu", (unsigned long)pending);
        if (pos < 0) return -1;
        uint32_t last_ok = v->ring[i].last_ok_ts;
        uint32_t lag = 0;
        if (pending > 0 && last_ok != 0 && v->now_ts >= last_ok) lag = v->now_ts - last_ok;
        pos = af_push_app(buf, cap, pos, ",\"last_ok_ts\":%lu", (unsigned long)last_ok);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"lag_s\":%lu", (unsigned long)lag);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"last_err\":{\"code\":%lu,\"text\":\"%s\"}", (unsigned long)v->ring[i].err, af_push_err_name(v->ring[i].err));
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"last_http\":%ld", (long)v->ring[i].last_http);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"sent_total\":%lu", (unsigned long)v->ring[i].sent_total);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"fail_total\":%lu", (unsigned long)v->ring[i].fail_total);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"retries\":%lu", (unsigned long)v->ring[i].retries);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"last_batch_rows\":%lu", (unsigned long)v->ring[i].last_rows);
        if (pos < 0) return -1;
        pos = af_push_app(buf, cap, pos, ",\"stopped\":%s}", v->ring[i].stopped ? "true" : "false");
        if (pos < 0) return -1;
    }
    pos = af_push_app(buf, cap, pos, "}}");
    if (pos < 0) return -1;
    return pos;
}

#endif
