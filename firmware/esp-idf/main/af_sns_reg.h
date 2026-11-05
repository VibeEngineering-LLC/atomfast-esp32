#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "af_sensor.h"
#include "af_env.h"

#ifndef AF_SNS_REG_H
#define AF_SNS_REG_H

#define AF_SNS_MAX 8
#define AF_SNS_H_OFF_LIM10 100
#define AF_SNS_BLOB_SIZE 102
#define AF_SNS_BLOB_VER 1

typedef struct {
    uint8_t sensor;
    uint8_t model;
    uint8_t addr;
    uint8_t enabled;
    int16_t t_off10;
    int16_t p_off10;
    int16_t h_off10;
    uint16_t period_s;
} af_sns_cfg_t;

typedef struct {
    af_sns_cfg_t s[AF_SNS_MAX];
    uint8_t agg_s;
} af_sns_reg_t;

typedef struct {
    uint8_t addr;
    uint8_t model;
} af_sns_found_t;

typedef struct {
    uint8_t has_enabled;
    uint8_t enabled;
    uint8_t has_t_off;
    float t_off;
    uint8_t has_p_off;
    float p_off;
    uint8_t has_h_off;
    float h_off;
    uint8_t has_period;
    int32_t period_s;
} af_sns_patch_t;

typedef struct {
    void *ctx;
    int (*probe)(void *ctx, uint8_t addr);
    int (*xfer)(void *ctx, uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn);
} af_i2c_ops_t;

static inline int af_sns_model_ok(uint8_t model) {
    return (model == AF_SNS_BMP280 || model == AF_SNS_BME280 || model == AF_SNS_SHT3X || model == AF_SNS_AHT2X) ? 1 : 0;
}

static inline int af_sns_addr_ok(uint8_t model, uint8_t addr) {
    if (model == AF_SNS_BMP280 || model == AF_SNS_BME280) {
        return (addr == 0x76 || addr == 0x77) ? 1 : 0;
    }
    if (model == AF_SNS_SHT3X) {
        return (addr == 0x44 || addr == 0x45) ? 1 : 0;
    }
    if (model == AF_SNS_AHT2X) {
        return (addr == 0x38) ? 1 : 0;
    }
    return 0;
}

static inline void af_sns_reg_default(af_sns_reg_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->s[0].sensor = 1;
    r->s[0].model = AF_SNS_BMP280;
    r->s[0].addr = 0x76;
    r->s[0].enabled = 1;
    r->s[0].period_s = AF_ENV_PERIOD_DEF;
    r->agg_s = AF_ENV_AGG_DEF;
}

/* e16i П.7 (В4): пустой реестр для НОВОЙ платы (нет пространства envs): датчик пользователь добавляет сам */
static inline void af_sns_reg_empty(af_sns_reg_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->agg_s = AF_ENV_AGG_DEF;
}

static inline int af_sns_reg_valid(const af_sns_reg_t *r) {
    if (!r) return 0;
    if (r->agg_s < AF_ENV_AGG_MIN || r->agg_s > AF_ENV_AGG_MAX) return 0;

    for (int i = 0; i < AF_SNS_MAX; i++) {
        const af_sns_cfg_t *c = &r->s[i];
        if (c->sensor == 0) {
            if (c->model != 0 || c->addr != 0 || c->enabled != 0 || c->t_off10 != 0 || c->p_off10 != 0 || c->h_off10 != 0 || c->period_s != 0) {
                return 0;
            }
        } else {
            if (c->sensor != (uint8_t)(i + 1)) return 0;
            if (!af_sns_model_ok(c->model)) return 0;
            if (!af_sns_addr_ok(c->model, c->addr)) return 0;
            if (c->enabled > 1) return 0;

            if (c->t_off10 < -50 || c->t_off10 > 50) return 0;

            const af_sns_preset_t *p = af_sns_preset((af_sns_model_t)c->model);
            if (!p) return 0;

            if (p->caps & AF_CAP_PRESS) {
                if (c->p_off10 < -200 || c->p_off10 > 200) return 0;
            } else {
                if (c->p_off10 != 0) return 0;
            }

            if (p->caps & AF_CAP_HUM) {
                if (c->h_off10 < -100 || c->h_off10 > 100) return 0;
            } else {
                if (c->h_off10 != 0) return 0;
            }

            if (c->period_s < AF_ENV_PERIOD_MIN || c->period_s > AF_ENV_PERIOD_MAX) return 0;
        }
    }

    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor == 0) continue;
        for (int j = i + 1; j < AF_SNS_MAX; j++) {
            if (r->s[j].sensor == 0) continue;
            if (r->s[i].addr == r->s[j].addr) return 0;
        }
    }
    return 1;
}

static inline af_sns_cfg_t *af_sns_reg_find(af_sns_reg_t *r, uint8_t sensor) {
    if (!r || sensor < 1 || sensor > AF_SNS_MAX) return NULL;
    int idx = (int)sensor - 1;
    if (r->s[idx].sensor == sensor) return &r->s[idx];
    return NULL;
}

static inline int af_sns_reg_count(const af_sns_reg_t *r) {
    if (!r) return 0;
    int cnt = 0;
    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor != 0) cnt++;
    }
    return cnt;
}

static inline int af_sns_reg_add(af_sns_reg_t *r, uint8_t model, uint8_t addr, uint8_t enabled) {
    if (!r || enabled > 1) return -4;
    if (!af_sns_model_ok(model) || !af_sns_addr_ok(model, addr)) return -1;

    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor != 0 && r->s[i].addr == addr) return -2;
    }

    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor == 0) {
            r->s[i].sensor = (uint8_t)(i + 1);
            r->s[i].model = model;
            r->s[i].addr = addr;
            r->s[i].enabled = enabled;
            r->s[i].t_off10 = 0;
            r->s[i].p_off10 = 0;
            r->s[i].h_off10 = 0;
            r->s[i].period_s = AF_ENV_PERIOD_DEF;
            return (int)(i + 1);
        }
    }
    return -3;
}

static inline int af_sns_reg_remove(af_sns_reg_t *r, uint8_t sensor) {
    if (!r) return -1;
    af_sns_cfg_t *c = af_sns_reg_find(r, sensor);
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    return 0;
}

static inline int af_sns_reg_order(const af_sns_reg_t *r, uint8_t out[AF_SNS_MAX]) {
    if (!r || !out) return 0;
    int cnt = 0;
    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor != 0) {
            out[cnt++] = r->s[i].sensor;
        }
    }

    for (int i = 1; i < cnt; i++) {
        uint8_t key = out[i];
        uint8_t key_model = r->s[key - 1].model;
        int j = i - 1;
        while (j >= 0) {
            uint8_t cur = out[j];
            uint8_t cur_model = r->s[cur - 1].model;
            uint8_t km = af_env_model_code((af_sns_model_t)key_model);
            uint8_t cm = af_env_model_code((af_sns_model_t)cur_model);
            if (cm > km || (cm == km && cur > key)) {
                out[j + 1] = out[j];
                j--;
            } else {
                break;
            }
        }
        out[j + 1] = key;
    }
    return cnt;
}

static inline int af_sns_reg_enable_model(af_sns_reg_t *r, uint8_t model, uint8_t enabled) {
    if (!r) return -1;
    if (enabled > 1) return -2;
    if (!af_sns_model_ok(model)) return -1;

    int cnt = 0;
    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (r->s[i].sensor != 0 && r->s[i].model == model) {
            r->s[i].enabled = enabled;
            cnt++;
        }
    }
    return cnt;
}

static inline int af_sns_reg_patch(af_sns_reg_t *r, uint8_t sensor, const af_sns_patch_t *p) {
    if (!r || !p) return -1;
    af_sns_cfg_t *c = af_sns_reg_find(r, sensor);
    if (!c) return -1;

    const af_sns_preset_t *preset = af_sns_preset((af_sns_model_t)c->model);
    if (!preset) return -2;

    uint8_t new_enabled = c->enabled;
    int16_t new_t = c->t_off10;
    int16_t new_p = c->p_off10;
    int16_t new_h = c->h_off10;
    uint16_t new_period = c->period_s;

    if (p->has_enabled) {
        if (p->enabled > 1) return -2;
        new_enabled = p->enabled;
    }

    if (p->has_t_off) {
        if (!isfinite(p->t_off) || fabsf(p->t_off) > 5.0f) return -2;
        new_t = (int16_t)lroundf(p->t_off * 10.0f);
    }

    if (p->has_p_off) {
        if (!isfinite(p->p_off) || fabsf(p->p_off) > 20.0f) return -2;
        if (!(preset->caps & AF_CAP_PRESS)) {
            if (p->p_off != 0.0f) return -2;
        }
        new_p = (int16_t)lroundf(p->p_off * 10.0f);
    }

    if (p->has_h_off) {
        if (!isfinite(p->h_off) || fabsf(p->h_off) > 10.0f) return -2;
        if (!(preset->caps & AF_CAP_HUM)) {
            if (p->h_off != 0.0f) return -2;
        }
        new_h = (int16_t)lroundf(p->h_off * 10.0f);
    }

    if (p->has_period) {
        if (p->period_s < AF_ENV_PERIOD_MIN || p->period_s > AF_ENV_PERIOD_MAX) return -2;
        new_period = (uint16_t)p->period_s;
    }

    c->enabled = new_enabled;
    c->t_off10 = new_t;
    c->p_off10 = new_p;
    c->h_off10 = new_h;
    c->period_s = new_period;
    return 0;
}

static inline int af_sns_reg_set_agg(af_sns_reg_t *r, int32_t agg_s) {
    if (!r) return -1;
    if (agg_s < AF_ENV_AGG_MIN || agg_s > AF_ENV_AGG_MAX) return -2;
    r->agg_s = (uint8_t)agg_s;
    return 0;
}

static inline size_t af_sns_reg_pack(const af_sns_reg_t *r, uint8_t out[AF_SNS_BLOB_SIZE]) {
    if (!r || !out) return 0;
    out[0] = AF_SNS_BLOB_VER;
    out[1] = (uint8_t)af_sns_reg_count(r);
    out[2] = r->agg_s;
    out[3] = 0;

    size_t off = 4;
    for (int i = 0; i < AF_SNS_MAX; i++) {
        const af_sns_cfg_t *c = &r->s[i];
        out[off++] = c->sensor;
        out[off++] = c->model;
        out[off++] = c->addr;
        out[off++] = c->enabled;
        out[off++] = (uint8_t)(c->t_off10 & 0xFF);
        out[off++] = (uint8_t)((c->t_off10 >> 8) & 0xFF);
        out[off++] = (uint8_t)(c->p_off10 & 0xFF);
        out[off++] = (uint8_t)((c->p_off10 >> 8) & 0xFF);
        out[off++] = (uint8_t)(c->h_off10 & 0xFF);
        out[off++] = (uint8_t)((c->h_off10 >> 8) & 0xFF);
        out[off++] = (uint8_t)(c->period_s & 0xFF);
        out[off++] = (uint8_t)((c->period_s >> 8) & 0xFF);
    }

    uint16_t crc = af_crc16(out, 100);
    out[100] = (uint8_t)(crc & 0xFF);
    out[101] = (uint8_t)((crc >> 8) & 0xFF);
    return AF_SNS_BLOB_SIZE;
}

static inline int af_sns_reg_unpack(const uint8_t *in, size_t n, af_sns_reg_t *r) {
    if (!in || !r || n != AF_SNS_BLOB_SIZE) return -1;

    uint16_t crc_calc = af_crc16(in, 100);
    uint16_t crc_stored = (uint16_t)(in[100] | (in[101] << 8));
    if (crc_calc != crc_stored) return -2;

    if (in[0] != AF_SNS_BLOB_VER) return -3;

    af_sns_reg_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.agg_s = in[2];

    int cnt = 0;
    for (int i = 0; i < AF_SNS_MAX; i++) {
        size_t off = 4 + i * 12;
        tmp.s[i].sensor = in[off];
        tmp.s[i].model = in[off + 1];
        tmp.s[i].addr = in[off + 2];
        tmp.s[i].enabled = in[off + 3];
        tmp.s[i].t_off10 = (int16_t)(in[off + 4] | (in[off + 5] << 8));
        tmp.s[i].p_off10 = (int16_t)(in[off + 6] | (in[off + 7] << 8));
        tmp.s[i].h_off10 = (int16_t)(in[off + 8] | (in[off + 9] << 8));
        tmp.s[i].period_s = (uint16_t)(in[off + 10] | (in[off + 11] << 8));
        if (tmp.s[i].sensor != 0) cnt++;
    }

    if (in[1] != (uint8_t)cnt) return -4;
    if (!af_sns_reg_valid(&tmp)) return -4;

    *r = tmp;
    return 0;
}

static inline void af_sns_reg_from_legacy(af_sns_reg_t *r, const af_env_cfg_t *c) {
    if (!r || !c) return;
    af_sns_reg_default(r);

    r->s[0].enabled = c->enabled ? 1 : 0;
    r->s[0].t_off10 = c->t_off10;
    r->s[0].p_off10 = c->p_off10;
    r->s[0].period_s = c->period_s;
    r->agg_s = c->agg_s;

    if (r->s[0].t_off10 < -50) r->s[0].t_off10 = -50;
    if (r->s[0].t_off10 > 50) r->s[0].t_off10 = 50;
    if (r->s[0].p_off10 < -200) r->s[0].p_off10 = -200;
    if (r->s[0].p_off10 > 200) r->s[0].p_off10 = 200;
    if (r->s[0].period_s < AF_ENV_PERIOD_MIN) r->s[0].period_s = AF_ENV_PERIOD_MIN;
    if (r->s[0].period_s > AF_ENV_PERIOD_MAX) r->s[0].period_s = AF_ENV_PERIOD_MAX;
    if (r->agg_s < AF_ENV_AGG_MIN) r->agg_s = AF_ENV_AGG_MIN;
    if (r->agg_s > AF_ENV_AGG_MAX) r->agg_s = AF_ENV_AGG_MAX;
}

static inline int af_sns_reg_merge_scan(af_sns_reg_t *r, const af_sns_found_t *f, int n) {
    if (!r || !f || n <= 0) return 0;
    int changes = 0;

    for (int i = 0; i < n; i++) {
        uint8_t model = f[i].model;
        uint8_t addr = f[i].addr;
        if (!af_sns_model_ok(model) || !af_sns_addr_ok(model, addr)) continue;

        int found_same = 0;
        int found_diff = 0;
        int idx_diff = -1;

        for (int j = 0; j < AF_SNS_MAX; j++) {
            if (r->s[j].sensor == 0) continue;
            if (r->s[j].addr == addr) {
                if (r->s[j].model == model) {
                    found_same = 1;
                } else {
                    found_diff = 1;
                    idx_diff = j;
                }
            }
        }

        if (found_same) {
            continue;
        }

        if (found_diff) {
            r->s[idx_diff].model = model;
            r->s[idx_diff].enabled = 0;
            r->s[idx_diff].t_off10 = 0;
            r->s[idx_diff].p_off10 = 0;
            r->s[idx_diff].h_off10 = 0;
            changes++;
            continue;
        }

        int res = af_sns_reg_add(r, model, addr, 0);
        if (res > 0) {
            changes++;
        } else if (res == -3) {
            break;
        }
    }
    return changes;
}

static inline int af_sns_reg_adopt_addr(af_sns_reg_t *r, const af_sns_found_t *f, int n) {
    if (!r || !f || n <= 0) return 0;
    if (r->s[0].sensor == 0 || r->s[0].model != AF_SNS_BMP280) return 0;

    uint8_t cur_addr = r->s[0].addr;
    int has_cur = 0;
    uint8_t new_addr = 0;
    int found_new = 0;

    for (int i = 0; i < n; i++) {
        if (f[i].model == AF_SNS_BMP280) {
            if (f[i].addr == cur_addr) {
                has_cur = 1;
            } else if (af_sns_addr_ok(AF_SNS_BMP280, f[i].addr)) {
                int occupied = 0;
                for (int j = 0; j < AF_SNS_MAX; j++) {
                    if (r->s[j].sensor != 0 && r->s[j].addr == f[i].addr) {
                        occupied = 1;
                        break;
                    }
                }
                if (!occupied) {
                    new_addr = f[i].addr;
                    found_new = 1;
                }
            }
        }
    }

    if (!has_cur && found_new) {
        r->s[0].addr = new_addr;
        return 1;
    }
    return 0;
}

static inline uint8_t af_sns_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static inline af_sns_model_t af_sns_identify(const af_i2c_ops_t *o, uint8_t addr) {
    if (!o) return AF_SNS_NONE;
    if (o->probe(o->ctx, addr) != 0) return AF_SNS_NONE;

    if (addr == 0x76 || addr == 0x77) {
        uint8_t w[1] = {0xD0};
        uint8_t r[1] = {0};
        if (o->xfer(o->ctx, addr, w, 1, r, 1) != 0) return AF_SNS_NONE;
        if (r[0] == 0x56 || r[0] == 0x57 || r[0] == 0x58) return AF_SNS_BMP280;
        if (r[0] == 0x60) return AF_SNS_BME280;
        return AF_SNS_NONE;
    }

    if (addr == 0x44 || addr == 0x45) {
        uint8_t w[2] = {0xF3, 0x2D};
        uint8_t r[3] = {0};
        if (o->xfer(o->ctx, addr, w, 2, r, 3) != 0) return AF_SNS_NONE;
        uint8_t crc = af_sns_crc8(r, 2);
        if (crc == r[2]) return AF_SNS_SHT3X;
        return AF_SNS_NONE;
    }

    if (addr == 0x38) {
        uint8_t w[1] = {0x71};
        uint8_t r[1] = {0};
        if (o->xfer(o->ctx, addr, w, 1, r, 1) != 0) return AF_SNS_NONE;
        if (r[0] == 0xFF) return AF_SNS_NONE;
        return AF_SNS_AHT2X;
    }

    return AF_SNS_NONE;
}

static inline int af_sns_scan(const af_i2c_ops_t *o, af_sns_found_t *out, int max) {
    if (!o || !out || max <= 0) return 0;

    const uint8_t addrs[] = {0x38, 0x44, 0x45, 0x76, 0x77};
    int cnt = 0;

    for (int i = 0; i < 5 && cnt < max; i++) {
        af_sns_model_t m = af_sns_identify(o, addrs[i]);
        if (m != AF_SNS_NONE) {
            out[cnt].addr = addrs[i];
            out[cnt].model = (uint8_t)m;
            cnt++;
        }
    }
    return cnt;
}

#endif
