#ifndef AF_PUSH_H
#define AF_PUSH_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "cJSON.h"
#include "af_rec.h"
#include "af_env.h"
#include "af_hist_query.h"
#include "af_history_codec.h"
#include "af_ble_nvs.h"
#include "af_sens_req.h"

#define AF_PUSH_HOST_MAX 64
#define AF_PUSH_TOKEN_MAX 65
#define AF_PUSH_PORT_DEF 8480
#define AF_PUSH_INTERVAL_MIN 10
#define AF_PUSH_INTERVAL_MAX 3600
#define AF_PUSH_INTERVAL_DEF 60
#define AF_PUSH_BATCH_MIN 50
#define AF_PUSH_BATCH_MAX 2000
#define AF_PUSH_BATCH_DEF 500
#define AF_PUSH_TIMEOUT_MIN 2
#define AF_PUSH_TIMEOUT_MAX 30
#define AF_PUSH_TIMEOUT_DEF 8
#define AF_PUSH_RINGS 3
#define AF_PUSH_RING_ALL 7
#define AF_PUSH_BATCH_FLOOR 10
#define AF_PUSH_BODY_MAX 16384
#define AF_PUSH_ROW_MAX 160
#define AF_PUSH_CFG_BLOB 152
#define AF_PUSH_CUR_BLOB 48
#define AF_PUSH_CUR_SAVE_MS 30000u
#define AF_PUSH_BACKOFF_BASE_MS 2000u
#define AF_PUSH_BACKOFF_CAP_MS 300000u

#define AF_PUSH_E_BODY -1
#define AF_PUSH_E_JSON -2
#define AF_PUSH_E_KEY -3
#define AF_PUSH_E_VALUE -4
#define AF_PUSH_E_INCOMPLETE -5

typedef struct {
    uint8_t enabled;
    uint8_t rings;       /* маска: бит0 raw, бит1 agg, бит2 env */
    uint16_t port;
    uint16_t interval_s;
    uint16_t batch_rows;
    uint8_t timeout_s;
    char host[AF_PUSH_HOST_MAX];
    char token[AF_PUSH_TOKEN_MAX];
} af_push_cfg_t;

typedef struct {
    af_push_cfg_t cfg;       /* итоговые настройки после слияния */
    uint8_t token_changed;   /* токен сменён или очищен */
    uint8_t dest_changed;    /* host или port изменились относительно текущих */
} af_push_cmd_t;

static inline void af_push_cfg_default(af_push_cfg_t *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->rings = AF_PUSH_RING_ALL;
    c->port = AF_PUSH_PORT_DEF;
    c->interval_s = AF_PUSH_INTERVAL_DEF;
    c->batch_rows = AF_PUSH_BATCH_DEF;
    c->timeout_s = AF_PUSH_TIMEOUT_DEF;
}

static inline int af_push_ipv4(const char *s, uint8_t o[4]) {
    if (!s) return -1;
    size_t len = strlen(s);
    if (len < 7 || len > 15) return -1;
    if (s[len - 1] == '.') return -1;

    int parts = 0;
    size_t i = 0;
    while (i < len) {
        if (parts >= 4) return -1;
        if (s[i] < '0' || s[i] > '9') return -1;
        
        int val = 0;
        int digits = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            if (digits >= 3) return -1;
            val = val * 10 + (s[i] - '0');
            if (val > 255) return -1;
            digits++;
            i++;
        }
        
        if (digits > 1 && s[i - digits] == '0') return -1;
        
        o[parts++] = (uint8_t)val;
        
        if (i < len) {
            if (s[i] != '.') return -1;
            i++;
        }
    }
    return (parts == 4) ? 0 : -1;
}

static inline int af_push_host_ok(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len < 1 || len > 63) return 0;

    if (s[0] == '.' || s[len - 1] == '.') return 0;
    if ((s[0] < 'a' || s[0] > 'z') && (s[0] < 'A' || s[0] > 'Z') && (s[0] < '0' || s[0] > '9')) return 0;
    if ((s[len - 1] < 'a' || s[len - 1] > 'z') && (s[len - 1] < 'A' || s[len - 1] > 'Z') && (s[len - 1] < '0' || s[len - 1] > '9')) return 0;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-') continue;
        return 0;
    }

    for (size_t i = 0; i + 1 < len; i++) {
        if (s[i] == '.' && s[i + 1] == '.') return 0;
    }

    int is_ip_candidate = 1;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-') {
            is_ip_candidate = 0;
            break;
        }
    }

    if (is_ip_candidate) {
        uint8_t o[4];
        if (af_push_ipv4(s, o) != 0) return 0;
    }

    return 1;
}

static inline int af_push_token_ok(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len < 1 || len > 64) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x21 || c > 0x7E) return 0;
        if (c == 0x5C || c == 0x22) return 0;
    }
    return 1;
}

static inline int af_push_addr_class(const char *host) {
    if (!host || host[0] == '\0') return 2;
    uint8_t o[4];
    if (af_push_ipv4(host, o) != 0) return 2;

    if (o[0] == 10) return 1;
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return 1;
    if (o[0] == 192 && o[1] == 168) return 1;
    if (o[0] == 127) return 1;
    if (o[0] == 169 && o[1] == 254) return 1;

    return 0;
}

static inline int af_push_cfg_valid(const af_push_cfg_t *c) {
    if (!c) return AF_PUSH_E_VALUE;
    if (c->enabled != 0 && c->enabled != 1) return AF_PUSH_E_VALUE;
    if (c->rings & ~AF_PUSH_RING_ALL) return AF_PUSH_E_VALUE;
    if (c->port == 0) return AF_PUSH_E_VALUE;
    if (c->interval_s < AF_PUSH_INTERVAL_MIN || c->interval_s > AF_PUSH_INTERVAL_MAX) return AF_PUSH_E_VALUE;
    if (c->batch_rows < AF_PUSH_BATCH_MIN || c->batch_rows > AF_PUSH_BATCH_MAX) return AF_PUSH_E_VALUE;
    if (c->timeout_s < AF_PUSH_TIMEOUT_MIN || c->timeout_s > AF_PUSH_TIMEOUT_MAX) return AF_PUSH_E_VALUE;

    if (c->host[0] != '\0') {
        char *nul = memchr(c->host, 0, AF_PUSH_HOST_MAX);
        if (!nul) return AF_PUSH_E_VALUE;
        for (size_t i = (size_t)(nul - c->host) + 1; i < AF_PUSH_HOST_MAX; i++) {
            if (c->host[i] != 0) return AF_PUSH_E_VALUE;
        }
        if (!af_push_host_ok(c->host)) return AF_PUSH_E_VALUE;
    } else {
        for (size_t i = 0; i < AF_PUSH_HOST_MAX; i++) {
            if (c->host[i] != 0) return AF_PUSH_E_VALUE;
        }
    }

    if (c->token[0] != '\0') {
        char *nul = memchr(c->token, 0, AF_PUSH_TOKEN_MAX);
        if (!nul) return AF_PUSH_E_VALUE;
        for (size_t i = (size_t)(nul - c->token) + 1; i < AF_PUSH_TOKEN_MAX; i++) {
            if (c->token[i] != 0) return AF_PUSH_E_VALUE;
        }
        if (!af_push_token_ok(c->token)) return AF_PUSH_E_VALUE;
    } else {
        for (size_t i = 0; i < AF_PUSH_TOKEN_MAX; i++) {
            if (c->token[i] != 0) return AF_PUSH_E_VALUE;
        }
    }

    return 0;
}

static inline int af_push_cfg_complete(const af_push_cfg_t *c) {
    if (!c) return 0;
    if (!c->enabled) return 1;
    if (c->host[0] == '\0') return 0;
    if (c->token[0] == '\0') return 0;
    if (c->rings == 0) return 0;
    return 1;
}

static inline int af_push_parse(const char *body, size_t n, const af_push_cfg_t *cur, af_push_cmd_t *cmd) {
    if (!cmd || !cur) {
        if (cmd) memset(cmd, 0, sizeof(*cmd));
        return AF_PUSH_E_BODY;
    }
    memset(cmd, 0, sizeof(*cmd));

    if (!af_sens_body_ok(body, n)) return AF_PUSH_E_BODY;

    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) return AF_PUSH_E_JSON;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return AF_PUSH_E_JSON;
    }

    const char *const allowed[] = {
        "enabled", "host", "port", "token", "clear_token",
        "interval_s", "batch_rows", "timeout_s", "rings", NULL
    };
    if (!af_sens_keys_ok(root, allowed)) {
        cJSON_Delete(root);
        return AF_PUSH_E_KEY;
    }

    cmd->cfg = *cur;

    cJSON *item;
    int ret = 0;

    item = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (item) {
        if (!cJSON_IsBool(item)) { ret = AF_PUSH_E_VALUE; goto out; }
        cmd->cfg.enabled = cJSON_IsTrue(item) ? 1 : 0;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "clear_token");
    int clear_tok = 0;
    if (item) {
        if (!cJSON_IsBool(item)) { ret = AF_PUSH_E_VALUE; goto out; }
        clear_tok = cJSON_IsTrue(item) ? 1 : 0;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "host");
    if (item) {
        if (!cJSON_IsString(item) || !item->valuestring) { ret = AF_PUSH_E_VALUE; goto out; }
        const char *h = item->valuestring;
        size_t hl = strlen(h);
        if (hl > AF_PUSH_HOST_MAX - 1) { ret = AF_PUSH_E_VALUE; goto out; }
        if (hl > 0 && !af_push_host_ok(h)) { ret = AF_PUSH_E_VALUE; goto out; }
        memset(cmd->cfg.host, 0, AF_PUSH_HOST_MAX);
        memcpy(cmd->cfg.host, h, hl);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "port");
    if (item) {
        double v;
        if (af_sens_num(item, 1, 65535, 1, &v) != 0) { ret = AF_PUSH_E_VALUE; goto out; }
        cmd->cfg.port = (uint16_t)v;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "interval_s");
    if (item) {
        double v;
        if (af_sens_num(item, AF_PUSH_INTERVAL_MIN, AF_PUSH_INTERVAL_MAX, 1, &v) != 0) { ret = AF_PUSH_E_VALUE; goto out; }
        cmd->cfg.interval_s = (uint16_t)v;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "batch_rows");
    if (item) {
        double v;
        if (af_sens_num(item, AF_PUSH_BATCH_MIN, AF_PUSH_BATCH_MAX, 1, &v) != 0) { ret = AF_PUSH_E_VALUE; goto out; }
        cmd->cfg.batch_rows = (uint16_t)v;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "timeout_s");
    if (item) {
        double v;
        if (af_sens_num(item, AF_PUSH_TIMEOUT_MIN, AF_PUSH_TIMEOUT_MAX, 1, &v) != 0) { ret = AF_PUSH_E_VALUE; goto out; }
        cmd->cfg.timeout_s = (uint8_t)v;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "token");
    int tok_present = 0;
    if (item) {
        if (!cJSON_IsString(item) || !item->valuestring) { ret = AF_PUSH_E_VALUE; goto out; }
        const char *t = item->valuestring;
        size_t tl = strlen(t);
        if (tl > 0) {
            if (tl > AF_PUSH_TOKEN_MAX - 1) { ret = AF_PUSH_E_VALUE; goto out; }
            if (!af_push_token_ok(t)) { ret = AF_PUSH_E_VALUE; goto out; }
            memset(cmd->cfg.token, 0, AF_PUSH_TOKEN_MAX);
            memcpy(cmd->cfg.token, t, tl);
            cmd->token_changed = 1;
            tok_present = 1;
        }
    }

    if (clear_tok) {
        if (tok_present) { ret = AF_PUSH_E_VALUE; goto out; }
        memset(cmd->cfg.token, 0, AF_PUSH_TOKEN_MAX);
        cmd->token_changed = 1;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "rings");
    if (item) {
        if (!cJSON_IsArray(item)) { ret = AF_PUSH_E_VALUE; goto out; }
        int count = cJSON_GetArraySize(item);
        if (count < 1 || count > 3) { ret = AF_PUSH_E_VALUE; goto out; }
        uint8_t mask = 0;
        for (int i = 0; i < count; i++) {
            cJSON *el = cJSON_GetArrayItem(item, i);
            if (!el || !cJSON_IsString(el) || !el->valuestring) { ret = AF_PUSH_E_VALUE; goto out; }
            const char *s = el->valuestring;
            if (strcmp(s, "raw") == 0) {
                if (mask & 1) { ret = AF_PUSH_E_VALUE; goto out; }
                mask |= 1;
            } else if (strcmp(s, "agg") == 0) {
                if (mask & 2) { ret = AF_PUSH_E_VALUE; goto out; }
                mask |= 2;
            } else if (strcmp(s, "env") == 0) {
                if (mask & 4) { ret = AF_PUSH_E_VALUE; goto out; }
                mask |= 4;
            } else {
                ret = AF_PUSH_E_VALUE;
                goto out;
            }
        }
        cmd->cfg.rings = mask;
    }

    cmd->dest_changed = (strcmp(cmd->cfg.host, cur->host) != 0) || (cmd->cfg.port != cur->port);

    if (af_push_cfg_valid(&cmd->cfg) != 0) { ret = AF_PUSH_E_VALUE; goto out; }
    if (!af_push_cfg_complete(&cmd->cfg)) { ret = AF_PUSH_E_INCOMPLETE; goto out; }

out:
    cJSON_Delete(root);
    return ret;
}

static inline size_t af_push_cfg_encode(const af_push_cfg_t *c, uint8_t *buf, size_t cap) {
    if (!c || !buf || cap < AF_PUSH_CFG_BLOB) return 0;
    memset(buf, 0, AF_PUSH_CFG_BLOB);

    buf[0] = 'A';
    buf[1] = 'P';
    buf[2] = 'S';
    buf[3] = '1';
    buf[4] = 1;
    buf[5] = 0;
    buf[6] = 0x8C;
    buf[7] = 0x00;

    buf[8] = c->enabled;
    buf[9] = c->rings;
    buf[10] = (uint8_t)(c->port & 0xFF);
    buf[11] = (uint8_t)((c->port >> 8) & 0xFF);
    buf[12] = (uint8_t)(c->interval_s & 0xFF);
    buf[13] = (uint8_t)((c->interval_s >> 8) & 0xFF);
    buf[14] = (uint8_t)(c->batch_rows & 0xFF);
    buf[15] = (uint8_t)((c->batch_rows >> 8) & 0xFF);
    buf[16] = c->timeout_s;
    buf[17] = 0;

    memcpy(&buf[18], c->host, AF_PUSH_HOST_MAX);
    memcpy(&buf[82], c->token, AF_PUSH_TOKEN_MAX);
    buf[147] = 0;

    uint32_t crc = af_bsn_crc32(buf, 148);
    buf[148] = (uint8_t)(crc & 0xFF);
    buf[149] = (uint8_t)((crc >> 8) & 0xFF);
    buf[150] = (uint8_t)((crc >> 16) & 0xFF);
    buf[151] = (uint8_t)((crc >> 24) & 0xFF);

    return AF_PUSH_CFG_BLOB;
}

static inline bool af_push_cfg_decode(af_push_cfg_t *c, const uint8_t *buf, size_t len) {
    if (!c) return false;
    af_push_cfg_default(c);
    if (!buf || len != AF_PUSH_CFG_BLOB) return false;

    if (buf[0] != 'A' || buf[1] != 'P' || buf[2] != 'S' || buf[3] != '1') return false;
    if (buf[4] != 1 || buf[5] != 0) return false;
    if (buf[6] != 0x8C || buf[7] != 0x00) return false;

    uint32_t crc_calc = af_bsn_crc32(buf, 148);
    uint32_t crc_stored = (uint32_t)buf[148] | ((uint32_t)buf[149] << 8) | ((uint32_t)buf[150] << 16) | ((uint32_t)buf[151] << 24);
    if (crc_calc != crc_stored) return false;

    if (buf[17] != 0 || buf[147] != 0) return false;

    c->enabled = buf[8];
    c->rings = buf[9];
    c->port = (uint16_t)(buf[10] | (buf[11] << 8));
    c->interval_s = (uint16_t)(buf[12] | (buf[13] << 8));
    c->batch_rows = (uint16_t)(buf[14] | (buf[15] << 8));
    c->timeout_s = buf[16];

    memcpy(c->host, &buf[18], AF_PUSH_HOST_MAX);
    memcpy(c->token, &buf[82], AF_PUSH_TOKEN_MAX);

    if (af_push_cfg_valid(c) != 0) {
        af_push_cfg_default(c);
        return false;
    }

    return true;
}

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
    return af_history_csv_header(AF_HQ_KIND_ENV, 1, buf, cap);
}

static inline int af_push_pack(af_push_read_fn rd, void *ctx, int kind, uint32_t start, uint32_t max_rows, const char *mac, uint32_t epoch, uint32_t lost0, char *buf, size_t cap, af_push_pack_t *out) {
    if (!rd || !buf || !out || !mac || mac[0] == '\0' || (kind < 0 || kind > 2) || cap < (256 + AF_PUSH_ROW_MAX) || max_rows == 0) return -1;
    memset(out, 0, sizeof(*out));
    out->lost = lost0;
    out->scan_end = start;

    int header_len = af_history_csv_header(kind, 1, buf, cap);
    if (header_len < 0) return -2;
    size_t len = (size_t)header_len;

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
        size_t rec_sz = af_history_record_size(kind);
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t seq = seq0 + i;
            if (rows == max_rows) { full = 1; break; }
            if (cap - len < AF_PUSH_ROW_MAX) { full = 1; break; }
            const uint8_t *p = rec + i * rec_sz;
            af_history_row_t row;
            if (af_history_row_decode(kind, p, &row) != 0) { out->scan_end = seq + 1; continue; }
            int pl = af_hq_ext_prefix(mac, seq, epoch, buf + len, cap - len);
            if (pl == -2) return -3;
            if (pl < 0) { full = 1; break; }
            int rl = af_history_row_csv(kind, &row, buf + len + pl, cap - len - pl);
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


#define AF_PUSH_S_OK 0     /* пачка принята (или локально пропущены непригодные записи): можно сразу повторить */
#define AF_PUSH_S_IDLE 1   /* слать нечего */
#define AF_PUSH_S_WAIT 2   /* нужна пауза: ошибка, занято, нет данных для упаковки */
#define AF_PUSH_S_STOP 3   /* кольцо остановлено или остановлен доступ (401) */

typedef struct {
    int (*info)(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch);   /* 0 ок, -1 занято, -2 кольца нет */
    af_push_read_fn read;
    int (*post)(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap);   /* 0 - ответ получен (код в *http), иначе ошибка транспорта; resp - тело ответа, завершить нулём */
    uint32_t (*now_ts)(void *ctx);   /* unix-время или 0 */
} af_push_ops_t;

static inline int af_push_send_ring(const af_push_ops_t *ops, void *ctx, int kind, const af_push_cfg_t *c, const char *mac, af_push_ring_t *r, uint8_t *auth_stop, char *body, size_t cap, uint32_t *pending_left, int *cursor_moved) {
    if (!ops || !ops->info || !ops->read || !ops->post || !c || !r || !body || !pending_left || !cursor_moved || kind < 0 || kind > 2) {
        if (pending_left) *pending_left = 0;
        if (cursor_moved) *cursor_moved = 0;
        return AF_PUSH_S_WAIT;
    }
    *pending_left = 0;
    *cursor_moved = 0;
    if (r->stopped) return AF_PUSH_S_STOP;
    if (!mac || mac[0] == 0) {
        r->err = AF_PUSH_ERR_NODEV;
        return AF_PUSH_S_WAIT;
    }
    uint32_t oldest = 0, head = 0, epoch = 0;
    int ir = ops->info(ctx, kind, &oldest, &head, &epoch);
    if (ir == -1) return AF_PUSH_S_WAIT;
    if (ir != 0) return AF_PUSH_S_IDLE;
    uint32_t batch = r->eff_batch ? r->eff_batch : c->batch_rows;
    if (batch > c->batch_rows) batch = c->batch_rows;
    af_push_plan_t pl;
    af_push_plan(&r->cur, oldest, head, epoch, batch, &pl);
    if (pl.count == 0) return AF_PUSH_S_IDLE;
    af_push_pack_t pk;
    int pr = af_push_pack(ops->read, ctx, kind, pl.start, pl.count, mac, epoch, pl.lost, body, cap, &pk);
    if (pr == -3) {
        r->err = AF_PUSH_ERR_NODEV;
        return AF_PUSH_S_WAIT;
    }
    if (pr != 0) {
        r->err = AF_PUSH_ERR_NOSTORE;
        return AF_PUSH_S_WAIT;
    }
    if (pk.rows == 0) {
        if (pk.scan_end != pl.start) {
            af_push_advance_local(r, pk.scan_end, epoch);
            *cursor_moved = 1;
            return AF_PUSH_S_OK;
        }
        return AF_PUSH_S_WAIT;
    }
    char resp[160];
    resp[0] = 0;
    int http = 0;
    int post_res = ops->post(ctx, kind, mac, head, epoch, pk.first_seq, pk.lost, body, pk.len, &http, resp, sizeof resp);
    resp[sizeof resp - 1] = 0;
    uint32_t ts = ops->now_ts ? ops->now_ts(ctx) : 0;
    int act = af_push_on_result(r, auth_stop, http, post_res != 0, resp, strlen(resp), pk.first_seq, pk.rows, head, epoch, c->batch_rows, ts);
    if (act == AF_PUSH_A_OK) {
        *cursor_moved = 1;
        af_push_plan_t p2;
        af_push_plan(&r->cur, oldest, head, epoch, 0xFFFFFFFFu, &p2);
        *pending_left = p2.pending;
        return AF_PUSH_S_OK;
    }
    if (act == AF_PUSH_A_SHRINK || act == AF_PUSH_A_RETRY) return AF_PUSH_S_WAIT;
    if (act == AF_PUSH_A_STOP_AUTH || act == AF_PUSH_A_STOP_RING) return AF_PUSH_S_STOP;
    return AF_PUSH_S_WAIT;
}

#endif
