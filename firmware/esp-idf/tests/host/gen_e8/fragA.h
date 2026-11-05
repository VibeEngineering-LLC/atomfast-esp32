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
