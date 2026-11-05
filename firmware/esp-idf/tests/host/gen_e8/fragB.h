typedef struct {
    uint32_t acked;   /* последний подтверждённый seq */
    uint32_t epoch;   /* epoch кольца на момент подтверждения */
    uint8_t valid;    /* 0 - подтверждений ещё не было */
} af_push_cur_t;

#define AF_PUSH_PF_FRESH 1
#define AF_PUSH_PF_EPOCH 2
#define AF_PUSH_PF_ROLLBACK 4
#define AF_PUSH_PF_EVICTED 8

typedef struct {
    uint32_t start;    /* с какого seq слать */
    uint32_t count;    /* максимум строк в пачке (<= batch) */
    uint32_t lost;     /* сколько строк вытеснено до start (для X-AF-Lost) */
    uint32_t pending;  /* сколько строк ждёт отправки с start */
    uint8_t flags;
} af_push_plan_t;

#define AF_PUSH_ERR_NONE 0
#define AF_PUSH_ERR_NET 1
#define AF_PUSH_ERR_SRV 2
#define AF_PUSH_ERR_AUTH 3
#define AF_PUSH_ERR_BAD 4
#define AF_PUSH_ERR_BIG 5
#define AF_PUSH_ERR_PROTO 6
#define AF_PUSH_ERR_HTTP4 7
#define AF_PUSH_ERR_NODEV 8
#define AF_PUSH_ERR_NOSTORE 9
#define AF_PUSH_ERR_OOM 10
#define AF_PUSH_ERR_COUNT 11

#define AF_PUSH_A_OK 0
#define AF_PUSH_A_RETRY 1
#define AF_PUSH_A_STOP_AUTH 2
#define AF_PUSH_A_STOP_RING 3
#define AF_PUSH_A_SHRINK 4

typedef struct {
    af_push_cur_t cur;
    uint8_t err;          /* AF_PUSH_ERR_* последней ошибки (0 - нет) */
    uint8_t stopped;      /* 1 - кольцо остановлено, нужно вмешательство */
    uint8_t fails;        /* подряд идущих неудач (для паузы) */
    int16_t last_http;    /* 0 - ответа не было */
    uint16_t eff_batch;   /* текущий размер пачки (после 413 уменьшается) */
    uint16_t last_rows;   /* строк в последней успешной пачке */
    uint32_t sent_total;  /* принято приёмником (сумма accepted) */
    uint32_t fail_total;
    uint32_t retries;
    uint32_t last_ok_ts;
} af_push_ring_t;

static inline const char *af_push_err_name(uint8_t e)
{
    static const char *const names[AF_PUSH_ERR_COUNT] = {
        "", "net", "server", "auth", "bad_batch", "too_big",
        "proto", "http4xx", "no_device", "no_store", "oom"
    };
    return (e < AF_PUSH_ERR_COUNT) ? names[e] : "unknown";
}

static inline void af_push_plan(const af_push_cur_t *cur, uint32_t oldest, uint32_t head, uint32_t epoch, uint32_t batch, af_push_plan_t *p)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    if (!cur) return;

    if (!cur->valid) {
        p->start = oldest;
        p->flags = AF_PUSH_PF_FRESH;
    } else if (cur->epoch != epoch) {
        p->start = oldest;
        p->flags = AF_PUSH_PF_EPOCH;
    } else {
        uint32_t next = cur->acked + 1;
        if ((int32_t)(next - head) > 0) {
            p->start = oldest;
            p->flags = AF_PUSH_PF_ROLLBACK;
        } else if ((int32_t)(oldest - next) > 0) {
            p->start = oldest;
            p->lost = oldest - next;
            p->flags = AF_PUSH_PF_EVICTED;
        } else {
            p->start = next;
        }
    }

    uint32_t pending = ((int32_t)(head - p->start) > 0) ? (head - p->start) : 0;
    p->pending = pending;
    p->count = (pending < batch) ? pending : batch;
}

static inline size_t af_push_curs_encode(const af_push_cur_t cur[3], uint8_t *buf, size_t cap)
{
    if (!cur || !buf || cap < AF_PUSH_CUR_BLOB) return 0;
    memset(buf, 0, AF_PUSH_CUR_BLOB);
    buf[0] = 'A'; buf[1] = 'P'; buf[2] = 'C'; buf[3] = '1';
    buf[4] = 1; buf[5] = 0;
    buf[6] = 0x24; buf[7] = 0x00;

    for (int i = 0; i < 3; i++) {
        size_t off = 8 + 12 * i;
        buf[off + 0] = (uint8_t)(cur[i].acked);
        buf[off + 1] = (uint8_t)(cur[i].acked >> 8);
        buf[off + 2] = (uint8_t)(cur[i].acked >> 16);
        buf[off + 3] = (uint8_t)(cur[i].acked >> 24);
        buf[off + 4] = (uint8_t)(cur[i].epoch);
        buf[off + 5] = (uint8_t)(cur[i].epoch >> 8);
        buf[off + 6] = (uint8_t)(cur[i].epoch >> 16);
        buf[off + 7] = (uint8_t)(cur[i].epoch >> 24);
        buf[off + 8] = cur[i].valid;
        /* 9, 10, 11 уже нули от memset */
    }

    uint32_t crc = af_bsn_crc32(buf, 44);
    buf[44] = (uint8_t)(crc);
    buf[45] = (uint8_t)(crc >> 8);
    buf[46] = (uint8_t)(crc >> 16);
    buf[47] = (uint8_t)(crc >> 24);
    return AF_PUSH_CUR_BLOB;
}

static inline bool af_push_curs_decode(af_push_cur_t cur[3], const uint8_t *buf, size_t len)
{
    if (!cur) return false;
    if (!buf || len != AF_PUSH_CUR_BLOB) {
        memset(cur, 0, 3 * sizeof(af_push_cur_t));
        return false;
    }
    if (buf[0] != 'A' || buf[1] != 'P' || buf[2] != 'C' || buf[3] != '1') {
        memset(cur, 0, 3 * sizeof(af_push_cur_t));
        return false;
    }
    if (buf[4] != 1 || buf[5] != 0) {
        memset(cur, 0, 3 * sizeof(af_push_cur_t));
        return false;
    }
    if (buf[6] != 0x24 || buf[7] != 0x00) {
        memset(cur, 0, 3 * sizeof(af_push_cur_t));
        return false;
    }
    uint32_t crc_calc = af_bsn_crc32(buf, 44);
    uint32_t crc_stored = (uint32_t)buf[44] | ((uint32_t)buf[45] << 8) | ((uint32_t)buf[46] << 16) | ((uint32_t)buf[47] << 24);
    if (crc_calc != crc_stored) {
        memset(cur, 0, 3 * sizeof(af_push_cur_t));
        return false;
    }

    for (int i = 0; i < 3; i++) {
        size_t off = 8 + 12 * i;
        cur[i].acked = (uint32_t)buf[off + 0] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
        cur[i].epoch = (uint32_t)buf[off + 4] | ((uint32_t)buf[off + 5] << 8) | ((uint32_t)buf[off + 6] << 16) | ((uint32_t)buf[off + 7] << 24);
        cur[i].valid = buf[off + 8];
        if (cur[i].valid > 1 || buf[off + 9] != 0 || buf[off + 10] != 0 || buf[off + 11] != 0) {
            memset(cur, 0, 3 * sizeof(af_push_cur_t));
            return false;
        }
    }
    return true;
}

static inline bool af_push_curs_due(bool dirty, uint32_t now_ms, uint32_t last_save_ms)
{
    return dirty && ((uint32_t)(now_ms - last_save_ms) >= AF_PUSH_CUR_SAVE_MS);
}

static inline void af_push_ring_init(af_push_ring_t *r, uint16_t cfg_batch)
{
    memset(r, 0, sizeof(*r));
    r->eff_batch = cfg_batch;
}

static inline void af_push_ring_clear_err(af_push_ring_t *r, uint16_t cfg_batch)
{
    r->err = 0;
    r->stopped = 0;
    r->fails = 0;
    r->last_http = 0;
    r->eff_batch = cfg_batch;
}

static inline void af_push_ring_reset_cursor(af_push_ring_t *r, uint16_t cfg_batch)
{
    memset(&r->cur, 0, sizeof(r->cur));
    af_push_ring_clear_err(r, cfg_batch);
}

static inline void af_push_advance_local(af_push_ring_t *r, uint32_t scan_end, uint32_t epoch)
{
    r->cur.acked = scan_end - 1;
    r->cur.epoch = epoch;
    r->cur.valid = 1;
}

static inline int af_push_parse_resp(const char *body, size_t n, uint32_t *accepted, int64_t *last_seq)
{
    if (!body || !accepted || !last_seq || n == 0) return -1;

    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) return -1;

    int ret = -1;
    if (cJSON_IsObject(root)) {
        const char *const allowed[] = {"accepted", "last_seq", "test", NULL};
        int sz = cJSON_GetArraySize(root);
        const cJSON *tf = cJSON_GetObjectItemCaseSensitive(root, "test");
        if (af_sens_keys_ok(root, allowed) && (sz == 2 || (sz == 3 && tf && cJSON_IsBool(tf)))) {
            double acc_d = 0.0;
            double ls_d = 0.0;
            if (af_sens_num(cJSON_GetObjectItemCaseSensitive(root, "accepted"), 0.0, 4294967295.0, 1, &acc_d) == 0 &&
                af_sens_num(cJSON_GetObjectItemCaseSensitive(root, "last_seq"), -1.0, 4294967295.0, 1, &ls_d) == 0) {
                *accepted = (uint32_t)acc_d;
                *last_seq = (int64_t)ls_d;
                ret = 0;
            }
        }
    }
    cJSON_Delete(root);
    return ret;
}

/* 1 если ответ приёмника помечен "test":true (приёмник понимает X-AF-Test); иначе 0 */
static inline int af_push_resp_test(const char *body, size_t n)
{
    if (!body || n == 0) return 0;
    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) return 0;
    const cJSON *tf = cJSON_GetObjectItemCaseSensitive(root, "test");
    int r = (cJSON_IsObject(root) && tf && cJSON_IsTrue(tf)) ? 1 : 0;
    cJSON_Delete(root);
    return r;
}

static inline int af_push_on_result(af_push_ring_t *r, uint8_t *auth_stop, int http, int transport_err, const char *body, size_t n, uint32_t start, uint32_t rows, uint32_t head, uint32_t epoch, uint16_t cfg_batch, uint32_t now_ts)
{
    r->last_http = transport_err ? 0 : ((http >= 0 && http <= 999) ? (uint16_t)http : 0);
    if (r->eff_batch == 0) r->eff_batch = cfg_batch;

    if (transport_err != 0) {
        r->err = AF_PUSH_ERR_NET;
        if (r->fails < 255) r->fails++;
        r->fail_total++;
        r->retries++;
        return AF_PUSH_A_RETRY;
    }

    if (http == 200) {
        uint32_t acc = 0;
        int64_t last = 0;
        if (af_push_parse_resp(body, n, &acc, &last) != 0 ||
            acc > rows ||
            (int64_t)(last + 1) < (int64_t)start ||
            (int64_t)(last + 1) > (int64_t)head) {
            r->err = AF_PUSH_ERR_PROTO;
            r->stopped = 1;
            r->fail_total++;
            return AF_PUSH_A_STOP_RING;
        }

        if (last == -1) {
            r->cur.valid = 0;
            r->cur.acked = 0;
            r->cur.epoch = epoch;
        } else {
            r->cur.acked = (uint32_t)last;
            r->cur.epoch = epoch;
            r->cur.valid = 1;
        }

        r->sent_total += acc;
        r->last_rows = (uint16_t)rows;
        r->last_ok_ts = now_ts;
        r->err = 0;
        r->stopped = 0;
        r->fails = 0;
        if (r->eff_batch < cfg_batch) {
            uint16_t next = (uint16_t)(r->eff_batch * 2);
            r->eff_batch = (next < cfg_batch) ? next : cfg_batch;
        }
        return AF_PUSH_A_OK;
    }

    if (http == 401) {
        r->err = AF_PUSH_ERR_AUTH;
        if (auth_stop) *auth_stop = 1;
        r->fail_total++;
        return AF_PUSH_A_STOP_AUTH;
    }

    if (http == 400) {
        r->err = AF_PUSH_ERR_BAD;
        r->stopped = 1;
        r->fail_total++;
        return AF_PUSH_A_STOP_RING;
    }

    if (http == 413) {
        r->err = AF_PUSH_ERR_BIG;
        r->fail_total++;
        if (r->eff_batch > AF_PUSH_BATCH_FLOOR) {
            uint16_t half = (uint16_t)(r->eff_batch / 2);
            r->eff_batch = (half < AF_PUSH_BATCH_FLOOR) ? AF_PUSH_BATCH_FLOOR : half;
            r->retries++;
            return AF_PUSH_A_SHRINK;
        } else {
            r->stopped = 1;
            return AF_PUSH_A_STOP_RING;
        }
    }

    if (http == 408 || http == 429 || (http >= 500 && http <= 599)) {
        r->err = AF_PUSH_ERR_SRV;
        if (r->fails < 255) r->fails++;
        r->fail_total++;
        r->retries++;
        return AF_PUSH_A_RETRY;
    }

    if (http >= 400 && http <= 499) {
        r->err = AF_PUSH_ERR_HTTP4;
        r->stopped = 1;
        r->fail_total++;
        return AF_PUSH_A_STOP_RING;
    }

    r->err = AF_PUSH_ERR_PROTO;
    r->stopped = 1;
    r->fail_total++;
    return AF_PUSH_A_STOP_RING;
}

static inline uint32_t af_push_backoff_ms(uint8_t fails, uint8_t rnd)
{
    if (fails == 0) return 0;
    uint8_t shift = (fails - 1 < 8) ? (fails - 1) : 8;
    uint64_t d = (uint64_t)AF_PUSH_BACKOFF_BASE_MS << shift;
    if (d > AF_PUSH_BACKOFF_CAP_MS) d = AF_PUSH_BACKOFF_CAP_MS;
    uint64_t jitter = (d / 8) * rnd / 255;
    return (uint32_t)(d + jitter);
}

static inline uint32_t af_push_pause_ms(uint32_t pending_left, uint16_t interval_s, uint8_t rnd)
{
    if (pending_left > 0) {
        return 1000 + (250 * rnd) / 255;
    }
    uint64_t base = (uint64_t)interval_s * 1000;
    uint64_t jitter = ((uint64_t)interval_s * 100) * rnd / 255;
    return (uint32_t)(base + jitter);
}

static inline int af_push_bad(const af_push_ring_t r[3], uint8_t rings, uint8_t auth_stop)
{
    if (auth_stop) return 1;
    for (int i = 0; i < 3; i++) {
        if ((rings >> i) & 1) {
            if (r[i].stopped || r[i].fails >= 3) return 1;
        }
    }
    return 0;
}

static inline const char *af_push_state_name(int enabled, int sending, int bad)
{
    if (!enabled) return "off";
    if (sending) return "sending";
    if (bad) return "error";
    return "idle";
}
