#ifndef AF_ENV_H
#define AF_ENV_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_sensor.h"
#include "af_rec.h"

/* Константы */
#define AF_REC_ENV_SIZE 14
#define AF_ENV_F_T 0x01
#define AF_ENV_F_P 0x02
#define AF_ENV_F_CORR 0x04
#define AF_ENV_F_H 0x08
#define AF_ENV_F_MASK 0x0F
#define AF_ENV_PERIOD_DEF 300   /* e16e: 5 мин по умолчанию (оператор 2026-10-27); настраивается на /system, хранится в NVS */
#define AF_ENV_PERIOD_MIN 5
#define AF_ENV_PERIOD_MAX 600
#define AF_ENV_AGG_DEF 5
#define AF_ENV_AGG_MIN 1
#define AF_ENV_AGG_MAX 60
#define AF_ENV_OFF_T_LIM10 50
#define AF_ENV_OFF_P_LIM10 200
#define AF_ENV_OSRS_T 5
#define AF_ENV_OSRS_P 5
#define AF_BMP280_REG_CAL 0x88
#define AF_BMP280_REG_ID 0xD0
#define AF_BMP280_REG_STATUS 0xF3
#define AF_BMP280_REG_CTRL 0xF4
#define AF_BMP280_REG_CONFIG 0xF5
#define AF_BMP280_REG_DATA 0xF7
#define AF_BMP280_MODE_FORCED 1

static inline uint8_t af_env_model_code(af_sns_model_t m) {
    switch (m) {
        case AF_SNS_BMP280: return 0;
        case AF_SNS_BME280: return 1;
        case AF_SNS_SHT3X:  return 2;
        case AF_SNS_AHT2X:  return 3;
        default:            return 0xFF;
    }
}

static inline af_sns_model_t af_env_model_from_code(uint8_t c) {
    switch (c) {
        case 0: return AF_SNS_BMP280;
        case 1: return AF_SNS_BME280;
        case 2: return AF_SNS_SHT3X;
        case 3: return AF_SNS_AHT2X;
        default: return AF_SNS_NONE;
    }
}

static inline const char *af_sns_key(af_sns_model_t m) {
    switch (m) {
        case AF_SNS_BMP280: return "bmp280";
        case AF_SNS_BME280: return "bme280";
        case AF_SNS_SHT3X:  return "sht3x";
        case AF_SNS_AHT2X:  return "aht2x";
        default:            return NULL;
    }
}

static inline af_sns_model_t af_sns_from_key(const char *s) {
    if (!s) return AF_SNS_NONE;
    if (strcmp(s, "bmp280") == 0) return AF_SNS_BMP280;
    if (strcmp(s, "bme280") == 0) return AF_SNS_BME280;
    if (strcmp(s, "sht3x")  == 0) return AF_SNS_SHT3X;
    if (strcmp(s, "aht2x")  == 0) return AF_SNS_AHT2X;
    return AF_SNS_NONE;
}

typedef struct {
    uint32_t ts;
    int16_t t_c100;
    uint32_t p_pa;
    uint8_t flags;
    uint16_t h_c100;
    uint8_t sensor;
    uint8_t model;
} af_env_rec_t;

static inline void af_env_encode(const af_env_rec_t *r, uint8_t out[AF_REC_ENV_SIZE]) {
    if (!r || !out) return;

    uint32_t ts = r->ts;
    out[0] = (uint8_t)(ts & 0xFF);
    out[1] = (uint8_t)((ts >> 8) & 0xFF);
    out[2] = (uint8_t)((ts >> 16) & 0xFF);
    out[3] = (uint8_t)((ts >> 24) & 0xFF);

    int16_t t = r->t_c100;
    out[4] = (uint8_t)(t & 0xFF);
    out[5] = (uint8_t)((t >> 8) & 0xFF);

    uint32_t p = r->p_pa;
    if (p > 131071u) p = 131071u;
    uint16_t h = r->h_c100;
    if (h > 10000u) h = 10000u;

    uint32_t w = (p & 0x1FFFFu) | ((uint32_t)h << 17);
    out[6] = (uint8_t)(w & 0xFF);
    out[7] = (uint8_t)((w >> 8) & 0xFF);
    out[8] = (uint8_t)((w >> 16) & 0xFF);
    out[9] = (uint8_t)((w >> 24) & 0xFF);

    out[10] = r->flags;

    uint8_t sensor = r->sensor;
    if (sensor < 1 || sensor > 8) sensor = 1;
    
    uint8_t m_code = af_env_model_code((af_sns_model_t)r->model);
    if (m_code == 0xFF) m_code = 0;

    uint8_t id_byte;
    if ((sensor == 1) && (m_code == 0)) {
        id_byte = 0;
    } else {
        id_byte = (uint8_t)((m_code << 4) | sensor);
    }
    out[11] = id_byte;

    uint16_t crc = af_crc16(out, 12);
    out[12] = (uint8_t)(crc & 0xFF);
    out[13] = (uint8_t)((crc >> 8) & 0xFF);
}

static inline int af_env_decode(const uint8_t in[AF_REC_ENV_SIZE], af_env_rec_t *r) {
    if (!in || !r) return -1;

    uint16_t crc_calc = af_crc16(in, 12);
    uint16_t crc_stored = (uint16_t)in[12] | ((uint16_t)in[13] << 8);
    if (crc_calc != crc_stored) return -1;

    uint8_t id_byte = in[11];
    uint8_t sensor;
    uint8_t m_code;

    if (id_byte == 0) {
        sensor = 1;
        m_code = 0;
    } else {
        sensor = id_byte & 0x0F;
        m_code = id_byte >> 4;
        
        if (sensor < 1 || sensor > 8) return -2;
        if (m_code > 3) return -2;
        if (sensor == 1 && m_code == 0) return -2;
    }

    uint8_t flags = in[10];
    if (flags & ~AF_ENV_F_MASK) return -3;
    if ((flags & (AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_H)) == 0) return -4;

    uint32_t w = (uint32_t)in[6] | ((uint32_t)in[7] << 8) | ((uint32_t)in[8] << 16) | ((uint32_t)in[9] << 24);
    if (w & 0x80000000u) return -5;

    int16_t t_c100 = (int16_t)((uint16_t)in[4] | ((uint16_t)in[5] << 8));
    uint32_t p_pa = w & 0x1FFFFu;
    uint16_t h_c100 = (uint16_t)((w >> 17) & 0x3FFF);

    if ((flags & AF_ENV_F_T) == 0 && t_c100 != 0) return -5;
    if ((flags & AF_ENV_F_P) == 0 && p_pa != 0) return -5;
    if ((flags & AF_ENV_F_H) == 0 && h_c100 != 0) return -5;
    if (h_c100 > 10000u) return -5;

    r->ts = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    r->t_c100 = t_c100;
    r->p_pa = p_pa;
    r->flags = flags;
    r->h_c100 = h_c100;
    r->sensor = sensor;
    r->model = (uint8_t)af_env_model_from_code(m_code);

    return 0;
}

static inline int af_env_csv_header(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    const char *hdr = "ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n";
    size_t len = strlen(hdr);
    if (cap < len + 1) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, hdr, len + 1);
    return (int)len;
}

static inline int af_env_csv(const af_env_rec_t *r, char *buf, size_t cap) {
    if (!r || !buf || cap == 0) return -1;

    int pos = 0;
    int n;

    #define APPEND_FMT(fmt, ...) do { \
        n = snprintf(buf + pos, cap - pos, fmt, ##__VA_ARGS__); \
        if (n < 0 || (size_t)n >= cap - pos) { buf[0] = 0; return -1; } \
        pos += n; \
    } while(0)

    #define APPEND_STR(s) do { \
        size_t slen = strlen(s); \
        if ((size_t)pos + slen >= cap) { buf[0] = 0; return -1; } \
        memcpy(buf + pos, s, slen); \
        pos += (int)slen; \
    } while(0)

    APPEND_FMT("%lu", (unsigned long)r->ts);
    APPEND_STR(",env,");
    
    uint8_t sensor = r->sensor ? r->sensor : 1;
    APPEND_FMT("%u", sensor);
    APPEND_STR(",");
    
    const char *mkey = af_sns_key((af_sns_model_t)r->model);
    if (!mkey) mkey = "bmp280";
    APPEND_STR(mkey);
    APPEND_STR(",");

    if (r->flags & AF_ENV_F_T) {
        APPEND_FMT("%.2f", (double)r->t_c100 / 100.0);
    } else {
        APPEND_STR("");
    }
    APPEND_STR(",");

    if (r->flags & AF_ENV_F_H) {
        APPEND_FMT("%.2f", (double)r->h_c100 / 100.0);
    } else {
        APPEND_STR("");
    }
    APPEND_STR(",");

    if (r->flags & AF_ENV_F_P) {
        APPEND_FMT("%.2f", (double)r->p_pa / 133.322387);
        APPEND_STR(",");
        APPEND_FMT("%lu", (unsigned long)r->p_pa);
    } else {
        APPEND_STR(",");
        APPEND_STR("");
    }
    APPEND_STR(",");

    APPEND_FMT("%u", r->flags);
    APPEND_STR("\n");

    if ((size_t)pos >= cap) {
        buf[0] = 0;
        return -1;
    }
    buf[pos] = 0;
    return pos;
}
#undef APPEND_FMT
#undef APPEND_STR
/* Настройки */
typedef struct {
    uint8_t enabled;
    int16_t t_off10;
    int16_t p_off10;
    uint8_t agg_s;
    uint16_t period_s;
} af_env_cfg_t;

/* Значения по умолчанию */
static inline void af_env_cfg_default(af_env_cfg_t *c) {
    if (c == NULL) return;
    c->enabled = 1;
    c->t_off10 = 0;
    c->p_off10 = 0;
    c->agg_s = AF_ENV_AGG_DEF;
    c->period_s = AF_ENV_PERIOD_DEF;
}

/* Проверка валидности настроек */
static inline int af_env_cfg_valid(const af_env_cfg_t *c) {
    if (c == NULL) return 0;
    if (c->enabled > 1) return 0;
    if (c->t_off10 < -AF_ENV_OFF_T_LIM10 || c->t_off10 > AF_ENV_OFF_T_LIM10) return 0;
    if (c->p_off10 < -AF_ENV_OFF_P_LIM10 || c->p_off10 > AF_ENV_OFF_P_LIM10) return 0;
    if (c->agg_s < AF_ENV_AGG_MIN || c->agg_s > AF_ENV_AGG_MAX) return 0;
    if (c->period_s < AF_ENV_PERIOD_MIN || c->period_s > AF_ENV_PERIOD_MAX) return 0;
    return 1;
}

/* Приведение настроек к допустимым значениям */
static inline void af_env_cfg_clamp(af_env_cfg_t *c) {
    if (c == NULL) return;
    c->enabled = c->enabled ? 1 : 0;
    if (c->t_off10 < -AF_ENV_OFF_T_LIM10) c->t_off10 = -AF_ENV_OFF_T_LIM10;
    if (c->t_off10 > AF_ENV_OFF_T_LIM10) c->t_off10 = AF_ENV_OFF_T_LIM10;
    if (c->p_off10 < -AF_ENV_OFF_P_LIM10) c->p_off10 = -AF_ENV_OFF_P_LIM10;
    if (c->p_off10 > AF_ENV_OFF_P_LIM10) c->p_off10 = AF_ENV_OFF_P_LIM10;
    if (c->agg_s < AF_ENV_AGG_MIN) c->agg_s = AF_ENV_AGG_MIN;
    if (c->agg_s > AF_ENV_AGG_MAX) c->agg_s = AF_ENV_AGG_MAX;
    if (c->period_s < AF_ENV_PERIOD_MIN) c->period_s = AF_ENV_PERIOD_MIN;
    if (c->period_s > AF_ENV_PERIOD_MAX) c->period_s = AF_ENV_PERIOD_MAX;
}

/* Построение записи из измерения */
static inline int af_env_make_rec(const af_env_cfg_t *c, uint8_t caps, float t_c, float p_pa, uint32_t ts, af_env_rec_t *out, float *t_shown, float *p_shown_mmhg) {
    if (c == NULL || out == NULL) return -4;
    
    if (c->enabled == 0) {
        if (t_shown) *t_shown = NAN;
        if (p_shown_mmhg) *p_shown_mmhg = NAN;
        return -1;
    }
    
    if (!(caps & AF_CAP_TEMP)) t_c = NAN;
    if (!(caps & AF_CAP_PRESS)) p_pa = NAN;
    
    float p_mm_raw = af_pa_to_mmhg(p_pa);
    if (!af_sns_plausible(t_c, p_mm_raw)) {
        if (t_shown) *t_shown = NAN;
        if (p_shown_mmhg) *p_shown_mmhg = NAN;
        return -2;
    }
    
    float t_corr = af_apply_offset(t_c, c->t_off10 / 10.0f, AF_TEMP_OFF_LIM);
    float p_corr = af_apply_offset(p_mm_raw, c->p_off10 / 10.0f, AF_PRESS_OFF_LIM_MMHG);
    
    if (t_shown) *t_shown = t_corr;
    if (p_shown_mmhg) *p_shown_mmhg = p_corr;
    
    if (ts == 0) return -3;
    
    out->ts = ts;
    uint8_t flags = 0;
    if (!isnan(t_corr)) flags |= AF_ENV_F_T;
    if (!isnan(p_corr)) flags |= AF_ENV_F_P;
    if (c->t_off10 != 0 || c->p_off10 != 0) flags |= AF_ENV_F_CORR;
    out->flags = flags;
    
    out->t_c100 = isnan(t_corr) ? 0 : (int16_t)lroundf(t_corr * 100.0f);
    out->p_pa = isnan(p_corr) ? 0 : (uint32_t)lroundf(p_corr * AF_PA_PER_MMHG);
    
    return 0;
}

/* e15: построение записи с номером датчика и влажностью */
typedef struct {
    uint8_t sensor;
    uint8_t model;
    uint8_t enabled;
    int16_t t_off10;
    int16_t p_off10;
    int16_t h_off10;
    uint8_t caps;
    float t_c;
    float p_pa;
    float h_pct;
    uint32_t ts;
} af_env_in_t;

static inline int af_env_make_rec_ex(const af_env_in_t *in, af_env_rec_t *out, float *t_shown, float *p_shown_mmhg, float *h_shown) {
    if (!in || !out) return -4;

    if (t_shown) *t_shown = NAN;
    if (p_shown_mmhg) *p_shown_mmhg = NAN;
    if (h_shown) *h_shown = NAN;

    af_env_cfg_t c;
    c.enabled = in->enabled;
    c.t_off10 = in->t_off10;
    c.p_off10 = in->p_off10;
    c.agg_s = 5;
    c.period_s = 30;

    int rc = af_env_make_rec(&c, in->caps, in->t_c, in->p_pa, in->ts, out, t_shown, p_shown_mmhg);

    float h = in->h_pct;
    if (!(in->caps & AF_CAP_HUM) || !isfinite(h) || h < 0.0f || h > 100.5f) {
        h = NAN;
    } else {
        h = af_apply_offset(h, in->h_off10 / 10.0f, 10.0f);
        if (h < 0.0f) h = 0.0f;
        if (h > 100.0f) h = 100.0f;
    }

    if ((rc == 0 || rc == -3) && in->enabled) {
        if (h_shown) *h_shown = h;
    }

    if (rc == 0) {
        out->sensor = in->sensor;
        out->model = in->model;
        
        if (isnan(h)) {
            out->h_c100 = 0;
        } else {
            out->h_c100 = (uint16_t)lroundf(h * 100.0f);
            out->flags |= AF_ENV_F_H;
            if (in->h_off10 != 0) {
                out->flags |= AF_ENV_F_CORR;
            }
        }
    }

    return rc;
}
/* Преобразование сырых данных BMP280 */
static inline int af_env_bmp280_convert(const af_bmp280_cal_t *cal, const uint8_t raw[6], float *t_c, float *p_pa) {
    if (cal == NULL || raw == NULL || t_c == NULL || p_pa == NULL) return -1;
    
    int32_t adc_p = af_bmp280_adc20(raw);
    int32_t adc_t = af_bmp280_adc20(raw + 3);
    int32_t t_fine;
    float t = af_bmp280_temp_c(cal, adc_t, &t_fine);
    
    if (isnan(t)) return -2;
    
    *t_c = t;
    *p_pa = af_bmp280_press_pa(cal, adc_p, t_fine);
    return 0;
}

/* Формирование регистра управления */
static inline uint8_t af_bmp280_ctrl(uint8_t osrs_t, uint8_t osrs_p, uint8_t mode) {
    return ((osrs_t & 7) << 5) | ((osrs_p & 7) << 2) | (mode & 3);
}

/* Расчет времени измерения в мс */
static inline uint32_t af_bmp280_meas_ms(uint8_t osrs_t, uint8_t osrs_p) {
    double ft = 0.0, fp = 0.0;
    if (osrs_t >= 1 && osrs_t <= 7) {
        if (osrs_t == 1) ft = 1.0;
        else if (osrs_t == 2) ft = 2.0;
        else if (osrs_t == 3) ft = 4.0;
        else if (osrs_t == 4) ft = 8.0;
        else ft = 16.0;
    }
    if (osrs_p >= 1 && osrs_p <= 7) {
        if (osrs_p == 1) fp = 1.0;
        else if (osrs_p == 2) fp = 2.0;
        else if (osrs_p == 3) fp = 4.0;
        else if (osrs_p == 4) fp = 8.0;
        else fp = 16.0;
    }
    
    double ms = 1.25 + 2.3 * ft;
    if (fp > 0) ms += 2.3 * fp + 0.575;
    
    return (uint32_t)ceil(ms);
}

/* Живое среднее */
typedef struct {
    double cps_sum, rate_sum;
    uint32_t n;
    uint32_t last_emit_s;
    int has_last_emit;
    float cps_avg, rate_avg;
    uint32_t avg_n;
    int valid;
} af_liveavg_t;

/* Инициализация живого среднего */
static inline void af_liveavg_init(af_liveavg_t *a) {
    if (a == NULL) return;
    memset(a, 0, sizeof(af_liveavg_t));
}

/* Добавление отсчета */
static inline int af_liveavg_add(af_liveavg_t *a, float cps, float rate_usvh) {
    if (a == NULL) return 0;
    if (!isfinite(cps) || !isfinite(rate_usvh)) return 0;
    if (cps < 0.0f || rate_usvh < 0.0f || rate_usvh >= 1000.0f) return 0;
    
    a->cps_sum += (double)cps;
    a->rate_sum += (double)rate_usvh;
    a->n++;
    return 1;
}

/* Тик живого среднего */
static inline int af_liveavg_tick(af_liveavg_t *a, uint32_t now_s, uint32_t window_s) {
    if (a == NULL) return 0;
    
    if (window_s == 0) window_s = 1;
    if (window_s > 60) window_s = 60;
    
    if (!a->has_last_emit) {
        a->has_last_emit = 1;
        a->last_emit_s = now_s;
        return 0;
    }
    
    if (now_s < a->last_emit_s) {
        a->last_emit_s = now_s;
        a->cps_sum = 0.0;
        a->rate_sum = 0.0;
        a->n = 0;
        return 0;
    }
    
    if (now_s - a->last_emit_s >= window_s) {
        a->last_emit_s = now_s;
        if (a->n > 0) {
            a->cps_avg = (float)(a->cps_sum / (double)a->n);
            a->rate_avg = (float)(a->rate_sum / (double)a->n);
            a->avg_n = a->n;
            a->valid = 1;
            a->cps_sum = 0.0;
            a->rate_sum = 0.0;
            a->n = 0;
            return 1;
        } else {
            a->cps_sum = 0.0;
            a->rate_sum = 0.0;
            a->n = 0;
            return 0;
        }
    }
    
    return 0;
}

#endif
