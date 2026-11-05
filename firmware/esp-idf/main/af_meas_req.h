#ifndef AF_MEAS_REQ_H
#define AF_MEAS_REQ_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "af_meas.h"

static inline int af_mreq_body_ok(const char *buf, size_t n) {
    if (buf == NULL || n < 1) return 0;
    for (size_t i = 0; i < n; ) {
        unsigned char c = (unsigned char)buf[i];
        if (c >= 0x80) {
            size_t len = 0;
            if (c >= 0xC2 && c <= 0xDF) len = 2;
            else if (c >= 0xE0 && c <= 0xEF) len = 3;
            else if (c >= 0xF0 && c <= 0xF4) len = 4;
            else return 0;
            if (i + len > n) return 0;
            if (af_meas_utf8_ok(buf + i, len) != 1) return 0;
            i += len;
        } else {
            if (c == 0x09 || c == 0x0A || c == 0x0D) {
                i++;
            } else if (c >= 0x20 && c <= 0x7E) {
                if (c == 0x5C) return 0;
                i++;
            } else {
                return 0;
            }
        }
    }
    return 1;
}

static inline int af_mreq_keys_ok(const cJSON *root, const char *const *allowed) {
    if (root == NULL || !cJSON_IsObject(root)) return 0;
    const cJSON *child = root->child;
    while (child != NULL) {
        const char *name = child->string;
        if (name == NULL) return 0;
        int found = 0;
        for (size_t i = 0; allowed[i] != NULL; i++) {
            if (strcmp(name, allowed[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
        const cJSON *sib = child->next;
        while (sib != NULL) {
            if (sib->string != NULL && strcmp(sib->string, name) == 0) return 0;
            sib = sib->next;
        }
        child = child->next;
    }
    return 1;
}

static inline int af_mreq_num(const cJSON *it, double lo, double hi, int integer, double *out) {
    if (it == NULL || !cJSON_IsNumber(it)) return -1;
    double v = it->valuedouble;
    if (v != v || v == INFINITY || v == -INFINITY) return -1;
    if (v < lo || v > hi) return -1;
    if (integer && v != floor(v)) return -1;
    if (out != NULL) *out = v;
    return 0;
}

static inline int af_mreq_str(const cJSON *it, size_t maxlen, char *dst, size_t dstcap) {
    if (it == NULL || !cJSON_IsString(it) || it->valuestring == NULL) return -1;
    size_t len = strlen(it->valuestring);
    if (len > maxlen || len >= dstcap) return -1;
    memcpy(dst, it->valuestring, len + 1);
    return 0;
}

static inline int af_mreq_parse_start(const char *body, size_t n, af_meas_params_t *p) {
    if (p == NULL) return AF_MEAS_E_BODY;
    if (!af_mreq_body_ok(body, n)) return AF_MEAS_E_BODY;
    
    cJSON *root = cJSON_ParseWithLength(body, n);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return AF_MEAS_E_JSON;
    }

    static const char *allowed[] = {
        "name", "n", "endless", "t_s", "pause_s", "t_min_s", "t_max_s",
        "delay_s", "start_at", "retry_invalid", "file_store", "no_overwrite",
        "lost_max", "stop_mode", "start_mode", "eps_pct", "prefix", "name_template", NULL
    };
    if (!af_mreq_keys_ok(root, allowed)) {
        cJSON_Delete(root);
        return AF_MEAS_E_KEY;
    }

    af_meas_params_default(p);
    int has_n = 0;
    int n_val = 0;
    int endless_true = 0;

    const cJSON *it = root->child;
    while (it != NULL) {
        const char *key = it->string;
        if (strcmp(key, "name") == 0) {
            if (af_mreq_str(it, AF_MEAS_NAME_MAX - 1, p->name, sizeof(p->name)) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
        } else if (strcmp(key, "n") == 0) {
            double v;
            if (af_mreq_num(it, 0, 1000, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->n = (uint32_t)v;
            has_n = 1;
            n_val = (int)v;
        } else if (strcmp(key, "endless") == 0) {
            if (!cJSON_IsBool(it)) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            if (cJSON_IsTrue(it)) {
                endless_true = 1;
                p->n = 0;
            }
        } else if (strcmp(key, "t_s") == 0) {
            double v;
            if (af_mreq_num(it, 1, 3600000, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->t_s = (uint32_t)v;
        } else if (strcmp(key, "pause_s") == 0) {
            double v;
            if (af_mreq_num(it, 0, 3600000, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->pause_s = (uint32_t)v;
        } else if (strcmp(key, "t_min_s") == 0) {
            double v;
            if (af_mreq_num(it, 0, 3600000, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->t_min_s = (uint32_t)v;
        } else if (strcmp(key, "t_max_s") == 0) {
            double v;
            if (af_mreq_num(it, 0, 3600000, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->t_max_s = (uint32_t)v;
        } else if (strcmp(key, "delay_s") == 0) {
            double v;
            if (af_mreq_num(it, 0, 604800, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->delay_s = (uint32_t)v;
        } else if (strcmp(key, "start_at") == 0) {
            double v;
            if (af_mreq_num(it, 0, 4294967295, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->start_at = (uint32_t)v;
        } else if (strcmp(key, "retry_invalid") == 0) {
            if (!cJSON_IsBool(it)) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->retry_invalid = cJSON_IsTrue(it) ? 1 : 0;
        } else if (strcmp(key, "file_store") == 0) {
            if (!cJSON_IsBool(it)) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->file_store = cJSON_IsTrue(it) ? 1 : 0;
        } else if (strcmp(key, "no_overwrite") == 0) {
            if (!cJSON_IsBool(it)) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->no_overwrite = cJSON_IsTrue(it) ? 1 : 0;
        } else if (strcmp(key, "lost_max") == 0) {
            double v;
            if (af_mreq_num(it, 0, 255, 1, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->lost_max = (uint8_t)v;
        } else if (strcmp(key, "stop_mode") == 0) {
            if (!cJSON_IsString(it) || it->valuestring == NULL) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            if (strcmp(it->valuestring, "time") == 0) p->stop_mode = AF_MEAS_STOP_TIME;
            else if (strcmp(it->valuestring, "sigma") == 0) p->stop_mode = AF_MEAS_STOP_SIGMA;
            else { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
        } else if (strcmp(key, "start_mode") == 0) {
            if (!cJSON_IsString(it) || it->valuestring == NULL) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            if (strcmp(it->valuestring, "now") == 0) p->start_mode = AF_MEAS_START_NOW;
            else if (strcmp(it->valuestring, "at") == 0) p->start_mode = AF_MEAS_START_AT;
            else if (strcmp(it->valuestring, "delay") == 0) p->start_mode = AF_MEAS_START_DELAY;
            else { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
        } else if (strcmp(key, "eps_pct") == 0) {
            double v;
            if (af_mreq_num(it, 0, 100, 0, &v) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            if (v <= 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            double v100 = v * 100.0;
            double r = floor(v100 + 0.5);
            if (fabs(v100 - r) > 1e-6) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            if (r < 1 || r > 10000) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
            p->eps_x100 = (uint16_t)r;
        } else if (strcmp(key, "prefix") == 0) {
            if (af_mreq_str(it, AF_MEAS_PREFIX_MAX - 1, p->prefix, sizeof(p->prefix)) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
        } else if (strcmp(key, "name_template") == 0) {
            if (af_mreq_str(it, AF_MEAS_TPL_MAX - 1, p->name_tpl, sizeof(p->name_tpl)) != 0) { cJSON_Delete(root); return AF_MEAS_E_VALUE; }
        }
        it = it->next;
    }

    if (endless_true && has_n && n_val != 0) {
        cJSON_Delete(root);
        return AF_MEAS_E_VALUE;
    }

    cJSON_Delete(root);
    return 0;
}

static inline int af_mreq_parse_id(const char *body, size_t n, int required, uint32_t *id) {
    if (id == NULL) return AF_MEAS_E_BODY;
    *id = 0;
    if (n == 0) {
        return required ? AF_MEAS_E_BODY : 0;
    }
    if (!af_mreq_body_ok(body, n)) return AF_MEAS_E_BODY;
    
    cJSON *root = cJSON_ParseWithLength(body, n);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return AF_MEAS_E_JSON;
    }

    static const char *allowed[] = { "id", NULL };
    if (!af_mreq_keys_ok(root, allowed)) {
        cJSON_Delete(root);
        return AF_MEAS_E_KEY;
    }

    const cJSON *it = root->child;
    if (it == NULL) {
        cJSON_Delete(root);
        return required ? AF_MEAS_E_VALUE : 0;
    }

    if (strcmp(it->string, "id") != 0) {
        cJSON_Delete(root);
        return AF_MEAS_E_KEY;
    }

    double v;
    if (af_mreq_num(it, 1, 4294967295, 1, &v) != 0) {
        cJSON_Delete(root);
        return AF_MEAS_E_VALUE;
    }
    *id = (uint32_t)v;
    cJSON_Delete(root);
    return 0;
}

static inline int af_mreq_query_u32(const char *query, const char *key, uint32_t *out) {
    if (out == NULL || key == NULL) return -1;
    if (query == NULL || query[0] == '\0') return 0;

    size_t key_len = strlen(key);
    const char *p = query;
    while (*p != '\0') {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            size_t len = 0;
            while (val[len] != '\0' && val[len] != '&') len++;
            
            if (len == 0 || len > 10) return -1;
            
            uint64_t v = 0;
            for (size_t i = 0; i < len; i++) {
                char c = val[i];
                if (c < '0' || c > '9') return -1;
                v = v * 10 + (uint64_t)(c - '0');
            }
            if (v > 0xFFFFFFFFull) return -1;
            *out = (uint32_t)v;
            return 1;
        }
        while (*p != '\0' && *p != '&') p++;
        if (*p == '&') p++;
    }
    return 0;
}

#endif
