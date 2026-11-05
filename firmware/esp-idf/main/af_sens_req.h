#ifndef AF_SENS_REQ_H
#define AF_SENS_REQ_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "af_sns_reg.h"

#define AF_SENS_CMD_PATCH 0
#define AF_SENS_CMD_SCAN 1
#define AF_SENS_CMD_ADD 2
#define AF_SENS_CMD_REMOVE 3

#define AF_SENS_E_BODY -1
#define AF_SENS_E_JSON -2
#define AF_SENS_E_KEY -3
#define AF_SENS_E_VALUE -4
#define AF_SENS_E_NOSENSOR -5

typedef struct {
    int kind;
    uint8_t sensor;
    uint8_t model;
    uint8_t addr;
    uint8_t enabled;
    uint8_t has_agg;
    int32_t agg_s;
    uint8_t has_patch;
    af_sns_patch_t patch;
} af_sens_cmd_t;

static inline int af_sens_body_ok(const char *buf, size_t n) {
    if (!buf || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0x5C) return 0; /* обратная косая запрещена: NUL в строке cJSON молча обрезает значение */
        if (c == 0x09 || c == 0x0A || c == 0x0D || (c >= 0x20 && c <= 0x7E)) continue;
        return 0;
    }
    return 1;
}

static inline int af_sens_num(const cJSON *it, double lo, double hi, int integer, double *out) {
    if (!it || !cJSON_IsNumber(it)) return -1;
    double v = it->valuedouble;
    if (!isfinite(v) || v < lo || v > hi) return -1;
    if (integer && v != floor(v)) return -1;
    *out = v;
    return 0;
}

static inline int af_sens_addr(const cJSON *it, uint8_t *out) {
    if (!it || !out) return -1;
    if (cJSON_IsNumber(it)) {
        double v;
        if (af_sens_num(it, 0, 127, 1, &v) != 0) return -1;
        *out = (uint8_t)v;
        return 0;
    }
    if (cJSON_IsString(it) && it->valuestring) {
        const char *s = it->valuestring;
        size_t len = strlen(s);
        if ((len == 3 || len == 4) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            int val = 0;
            for (size_t i = 2; i < len; i++) {
                char c = s[i];
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return -1;
                val = val * 16 + d;
            }
            if (val > 127) return -1;
            *out = (uint8_t)val;
            return 0;
        }
    }
    return -1;
}

static inline int af_sens_keys_ok(const cJSON *root, const char *const *allowed) {
    if (!root || !cJSON_IsObject(root) || !allowed) return 0;
    cJSON *child = root->child;
    while (child) {
        if (!child->string) return 0;
        int found = 0;
        for (int i = 0; allowed[i]; i++) {
            if (strcmp(child->string, allowed[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
        cJSON *next = child->next;
        while (next) {
            if (next->string && strcmp(child->string, next->string) == 0) return 0;
            next = next->next;
        }
        child = child->next;
    }
    return 1;
}

static inline int af_sens_parse(const char *body, size_t n, uint8_t primary, af_sens_cmd_t *cmd) {
    if (!cmd) return AF_SENS_E_BODY;
    memset(cmd, 0, sizeof(*cmd));
    if (!af_sens_body_ok(body, n)) return AF_SENS_E_BODY;

    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) return AF_SENS_E_JSON;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return AF_SENS_E_JSON;
    }

    const char *const allowed[] = {
        "action", "model", "addr", "sensor", "enabled",
        "t_off", "p_off", "h_off", "period_s", "agg_s", NULL
    };
    if (!af_sens_keys_ok(root, allowed)) {
        cJSON_Delete(root);
        return AF_SENS_E_KEY;
    }

    cJSON *j_action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (j_action) {
        if (!cJSON_IsString(j_action) || !j_action->valuestring) {
            cJSON_Delete(root);
            return AF_SENS_E_VALUE;
        }
        const char *act = j_action->valuestring;
        if (strcmp(act, "scan") == 0) {
            if (cJSON_GetArraySize(root) != 1) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->kind = AF_SENS_CMD_SCAN;
        } else if (strcmp(act, "add") == 0) {
            cJSON *j_model = cJSON_GetObjectItemCaseSensitive(root, "model");
            cJSON *j_addr = cJSON_GetObjectItemCaseSensitive(root, "addr");
            cJSON *j_enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
            if (!j_model || !cJSON_IsString(j_model) || !j_model->valuestring) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            uint8_t m = af_sns_from_key(j_model->valuestring);
            if (m == AF_SNS_NONE) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            uint8_t a;
            if (af_sens_addr(j_addr, &a) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            if (!af_sns_model_ok(m) || !af_sns_addr_ok(m, a)) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            uint8_t en = 1;
            if (j_enabled) {
                if (!cJSON_IsBool(j_enabled)) {
                    cJSON_Delete(root);
                    return AF_SENS_E_VALUE;
                }
                en = cJSON_IsTrue(j_enabled) ? 1 : 0;
            }
            if (cJSON_GetArraySize(root) != (j_enabled ? 4 : 3)) { /* лишние ключи недопустимы */
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->kind = AF_SENS_CMD_ADD;
            cmd->model = m;
            cmd->addr = a;
            cmd->enabled = en;
        } else if (strcmp(act, "remove") == 0) {
            cJSON *j_sensor = cJSON_GetObjectItemCaseSensitive(root, "sensor");
            double v;
            if (!j_sensor || af_sens_num(j_sensor, 1, 8, 1, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            if (cJSON_GetArraySize(root) != 2) { /* лишние ключи недопустимы */
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->kind = AF_SENS_CMD_REMOVE;
            cmd->sensor = (uint8_t)v;
        } else {
            cJSON_Delete(root);
            return AF_SENS_E_VALUE;
        }
    } else {
        cmd->kind = AF_SENS_CMD_PATCH;
        cJSON *j_agg = cJSON_GetObjectItemCaseSensitive(root, "agg_s");
        if (j_agg) {
            double v;
            if (af_sens_num(j_agg, 1, 60, 1, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->has_agg = 1;
            cmd->agg_s = (int32_t)v;
        }

        cJSON *j_enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
        if (j_enabled) {
            if (!cJSON_IsBool(j_enabled)) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->patch.has_enabled = 1;
            cmd->patch.enabled = cJSON_IsTrue(j_enabled) ? 1 : 0;
        }

        cJSON *j_t_off = cJSON_GetObjectItemCaseSensitive(root, "t_off");
        if (j_t_off) {
            double v;
            if (af_sens_num(j_t_off, -5, 5, 0, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->patch.has_t_off = 1;
            cmd->patch.t_off = (float)v;
        }

        cJSON *j_p_off = cJSON_GetObjectItemCaseSensitive(root, "p_off");
        if (j_p_off) {
            double v;
            if (af_sens_num(j_p_off, -20, 20, 0, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->patch.has_p_off = 1;
            cmd->patch.p_off = (float)v;
        }

        cJSON *j_h_off = cJSON_GetObjectItemCaseSensitive(root, "h_off");
        if (j_h_off) {
            double v;
            if (af_sens_num(j_h_off, -10, 10, 0, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->patch.has_h_off = 1;
            cmd->patch.h_off = (float)v;
        }

        cJSON *j_period = cJSON_GetObjectItemCaseSensitive(root, "period_s");
        if (j_period) {
            double v;
            if (af_sens_num(j_period, 5, 600, 1, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->patch.has_period = 1;
            cmd->patch.period_s = (int32_t)v;
        }

        if (cJSON_GetObjectItemCaseSensitive(root, "model") || cJSON_GetObjectItemCaseSensitive(root, "addr")) {
            cJSON_Delete(root);
            return AF_SENS_E_VALUE;
        }

        cJSON *j_sensor = cJSON_GetObjectItemCaseSensitive(root, "sensor");
        if (j_sensor) {
            double v;
            if (af_sens_num(j_sensor, 1, 8, 1, &v) != 0) {
                cJSON_Delete(root);
                return AF_SENS_E_VALUE;
            }
            cmd->sensor = (uint8_t)v;
        } else {
            cmd->sensor = primary;
        }

        cmd->has_patch = (cmd->patch.has_enabled || cmd->patch.has_t_off || cmd->patch.has_p_off || cmd->patch.has_h_off || cmd->patch.has_period) ? 1 : 0;

        if (cmd->has_patch && cmd->sensor == 0) {
            cJSON_Delete(root);
            return AF_SENS_E_NOSENSOR;
        }

        if (!cmd->has_patch && !cmd->has_agg) {
            cJSON_Delete(root);
            return AF_SENS_E_VALUE;
        }
    }

    cJSON_Delete(root);
    return 0;
}

static inline int af_sens_parse_model(const char *body, size_t n, uint8_t *model, uint8_t *enabled) {
    if (!model || !enabled) return AF_SENS_E_BODY;
    if (!af_sens_body_ok(body, n)) return AF_SENS_E_BODY;

    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) return AF_SENS_E_JSON;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return AF_SENS_E_JSON;
    }

    const char *const allowed[] = { "model", "enabled", NULL };
    if (!af_sens_keys_ok(root, allowed)) {
        cJSON_Delete(root);
        return AF_SENS_E_KEY;
    }

    cJSON *j_model = cJSON_GetObjectItemCaseSensitive(root, "model");
    cJSON *j_enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");

    if (!j_model || !cJSON_IsString(j_model) || !j_model->valuestring) {
        cJSON_Delete(root);
        return AF_SENS_E_VALUE;
    }
    uint8_t m = af_sns_from_key(j_model->valuestring);
    if (m == AF_SNS_NONE) {
        cJSON_Delete(root);
        return AF_SENS_E_VALUE;
    }

    if (!j_enabled || !cJSON_IsBool(j_enabled)) {
        cJSON_Delete(root);
        return AF_SENS_E_VALUE;
    }

    *model = m;
    *enabled = cJSON_IsTrue(j_enabled) ? 1 : 0;
    cJSON_Delete(root);
    return 0;
}

#endif
