#ifndef AF_MEAS_H
#define AF_MEAS_H

#include "af_fmtval.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define AF_MEAS_MAX_RUNS     8
#define AF_MEAS_NAME_MAX     33      /* 32 байта UTF-8 + NUL */
#define AF_MEAS_PREFIX_MAX   17      /* 16 + NUL */
#define AF_MEAS_TPL_MAX      49      /* 48 + NUL */
#define AF_MEAS_FNAME_MAX    56      /* имя файла с .csv и суффиксом, с NUL */
#define AF_MEAS_N_MAX        1000u
#define AF_MEAS_T_MIN_S      10u
#define AF_MEAS_T_MAX_S      3600000u   /* 1000 ч */
#define AF_MEAS_PAUSE_MAX_S  3600000u
#define AF_MEAS_DELAY_MAX_S  604800u
#define AF_MEAS_AT_MAX_S     2678400u   /* start_at не дальше 31 сут от now */
#define AF_MEAS_PAST_S       5u         /* start_at допускается в прошлом не более чем на 5 с */
#define AF_MEAS_LOSTMAX_MAX  60u
#define AF_MEAS_ATTEMPTS     3u
#define AF_MEAS_PKT_MS       2000u      /* номинальный интервал пакетов дозиметра */
#define AF_MEAS_GAP_MS       6000u      /* интервал между пакетами > этого = пропуск */
#define AF_MEAS_RES_MIN      64u
#define AF_MEAS_RES_MAX      1000u
#define AF_MEAS_JUMP_S       5u         /* скачок UTC относительно монотонных часов больше этого = флаг */
#define AF_MEAS_EPS_MIN      10u        /* 0.10 % в сотых долях процента */
#define AF_MEAS_EPS_MAX      5000u      /* 50.00 % */
#define AF_MEAS_FILES_MAX    8u
#define AF_MEAS_FILES_BYTES  262144u
#define AF_MEAS_FREE_PCT_MIN 25u

enum { AF_MEAS_MODE_SINGLE = 0, AF_MEAS_MODE_SERIES = 1, AF_MEAS_MODE_ENDLESS = 2, AF_MEAS_MODE_JOURNAL = 3 };
enum { AF_MEAS_ST_WAIT = 0, AF_MEAS_ST_RUN = 1, AF_MEAS_ST_PAUSED = 2, AF_MEAS_ST_DONE = 3, AF_MEAS_ST_ABORTED = 4, AF_MEAS_ST_ERROR = 5 };
enum { AF_MEAS_PH_NONE = 0, AF_MEAS_PH_WAITSTART = 1, AF_MEAS_PH_MEASURING = 2, AF_MEAS_PH_GAP = 3 };
enum { AF_MEAS_SR_NONE = 0, AF_MEAS_SR_TIME = 1, AF_MEAS_SR_SIGMA = 2, AF_MEAS_SR_LIMIT = 3, AF_MEAS_SR_MANUAL = 4, AF_MEAS_SR_ERROR = 5 };
enum { AF_MEAS_STOP_TIME = 0, AF_MEAS_STOP_SIGMA = 1 };
enum { AF_MEAS_START_NOW = 0, AF_MEAS_START_AT = 1, AF_MEAS_START_DELAY = 2 };
/* флаги результата */
#define AF_MEAS_F_TIMEJUMP   0x01u   /* UTC скакнул относительно монотонных часов во время окна (длительность не затронута) */
#define AF_MEAS_F_UTC_OK     0x02u   /* t0_utc известен */
#define AF_MEAS_F_GOAL_MISS  0x04u   /* остановка по лимиту времени, цель по погрешности не достигнута */
#define AF_MEAS_F_PARTIAL    0x08u   /* окно прервано вручную, неполное */
#define AF_MEAS_F_FINAL      0x10u   /* итоговая запись измерения i (не промежуточная неудачная попытка) */
/* коды ошибок (отрицательные) */
#define AF_MEAS_E_BODY      (-1)
#define AF_MEAS_E_JSON      (-2)
#define AF_MEAS_E_KEY       (-3)
#define AF_MEAS_E_VALUE     (-4)     /* HTTP 400 bad_value */
#define AF_MEAS_E_SIGMA     (-5)     /* HTTP 400 sigma_only_single: stop_mode=sigma при n != 1 */
#define AF_MEAS_E_TIME      (-6)     /* HTTP 409 time_not_synced: start_mode=at без синхронного времени */
#define AF_MEAS_E_BUSY      (-7)     /* HTTP 409 busy: уже есть активный прогон */
#define AF_MEAS_E_NOTFOUND  (-8)     /* HTTP 404 */
#define AF_MEAS_E_STATE     (-9)     /* HTTP 409 bad_state: действие не подходит состоянию */
#define AF_MEAS_E_NOMEM     (-10)    /* HTTP 503 no_mem */

typedef struct {                     /* параметры запуска */
    char name[AF_MEAS_NAME_MAX];
    char prefix[AF_MEAS_PREFIX_MAX];
    char name_tpl[AF_MEAS_TPL_MAX];
    uint32_t n;                      /* 0 = бесконечно, иначе 1..AF_MEAS_N_MAX */
    uint32_t t_s;                    /* длительность окна, c */
    uint32_t pause_s;
    uint32_t t_min_s, t_max_s;       /* режим по погрешности */
    uint32_t start_at;               /* UTC, для START_AT */
    uint32_t delay_s;                /* для START_DELAY */
    uint16_t eps_x100;               /* цель по относительной погрешности, сотые доли процента */
    uint8_t retry_invalid, lost_max, stop_mode, start_mode, file_store, no_overwrite;
} af_meas_params_t;

typedef struct {                     /* результат одного окна, 60 байт */
    uint32_t i;                      /* номер измерения, с 1 */
    uint32_t t0_utc;                 /* 0 = время не было известно */
    uint32_t t0_mono_s;
    uint32_t t_ms;                   /* фактическая длительность окна по монотонным часам */
    uint32_t counts;
    uint32_t raw_seq0, raw_seq1, raw_epoch;
    uint16_t npk;                    /* принято пакетов */
    uint16_t lost;                   /* оценка пропущенных пакетов */
    float cps, cps_sigma, usv_h, usv_h_sigma, eps_pct;   /* NAN = нет значения */
    uint8_t attempt, valid, stop_reason, flags;
} af_meas_res_t;

typedef struct {                     /* прогон: серия, разовое или непрерывное измерение */
    uint32_t id;                     /* 0 = слот свободен */
    char name[AF_MEAS_NAME_MAX];
    char prefix[AF_MEAS_PREFIX_MAX];
    char tpl[AF_MEAS_TPL_MAX];
    uint8_t mode, state, phase, end_reason;
    uint8_t retry_invalid, lost_max, stop_mode, start_mode;
    uint8_t file_store, no_overwrite, cur_attempt, cur_flags;
    uint8_t dead_prev;               /* предыдущее окно закрыто без единого пакета (обрыв связи) */
    uint16_t eps_x100, res_cap;
    uint32_t n, t_ms, pause_ms, t_min_ms, t_max_ms;
    uint32_t start_utc, start_mono_s, end_mono_s;
    uint32_t done_i, cur_i;
    uint32_t rows_total, dropped, res_head, res_n;
    uint32_t cur_counts, cur_lost, cur_t0_utc, cur_raw_seq0, cur_raw_epoch;
    uint32_t cur_npk;
    uint64_t t0_ms, next_ms, last_rx_ms;
    int64_t cur_utc_off;             /* utc - mono_s на момент открытия окна */
    double cur_rate_sum;
    af_meas_res_t *res;
} af_meas_run_t;

typedef void *(*af_meas_alloc_fn)(size_t);
typedef void (*af_meas_free_fn)(void *);
typedef uint32_t (*af_meas_seq_fn)(uint32_t *epoch);        /* текущий seq кольца raw и его epoch; может быть NULL */
typedef void (*af_meas_log_fn)(int level, const char *msg); /* 1 info, 2 warn, 3 error; может быть NULL */

typedef struct {
    af_meas_run_t runs[AF_MEAS_MAX_RUNS];
    uint32_t next_id;
    int32_t active;                  /* индекс активного прогона в runs[] или -1 */
    int utc_ok;
    int64_t utc_off;                 /* последнее известное utc - mono_s */
    af_meas_alloc_fn alloc;
    af_meas_free_fn free_fn;
    af_meas_seq_fn seq_fn;
    af_meas_log_fn log_fn;
} af_meas_core_t;

typedef struct {                     /* живое состояние текущего окна */
    uint32_t i;
    uint8_t attempt, phase;
    uint16_t npk;
    uint32_t elapsed_ms, remaining_ms, counts, lost;
    float cps, cps_sigma, eps_pct, usv_h;   /* NAN = нет значения */
    uint64_t n_target;               /* для режима по погрешности, иначе 0 */
    int32_t eta_s;                   /* оценка времени до цели по погрешности, -1 = нет */
} af_meas_live_t;

typedef struct {                     /* итоги прогона по действительным окнам */
    uint32_t rows, n_valid, n_invalid, counts;
    double live_s;
    double mean_cps, sd_cps, sem_cps, min_cps, max_cps, pooled_cps, pooled_sigma, chi2_ndf, mean_usvh, dose_usv;   /* NAN = нет значения */
} af_meas_sum_t;

/* strnlen не входит в строгий C11: своя версия, чтобы заголовок собирался и на хосте */
static inline size_t am_strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

static inline void af_meas_params_default(af_meas_params_t *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->n = 1;
    p->t_s = 600;
    p->pause_s = 0;
    p->retry_invalid = 1;
    p->lost_max = 2;
    p->stop_mode = AF_MEAS_STOP_TIME;
    p->start_mode = AF_MEAS_START_NOW;
    p->eps_x100 = 100;
    p->t_min_s = 10;
    p->t_max_s = 3600;
    p->file_store = 0;
    p->no_overwrite = 1;
    snprintf(p->prefix, sizeof(p->prefix), "AtomFast");
    snprintf(p->name_tpl, sizeof(p->name_tpl), "{prefix}_{n:000}");
}

static inline void af_meas_civil(uint32_t utc, int *y, int *mo, int *d, int *h, int *mi, int *s) {
    if (!y || !mo || !d || !h || !mi || !s) return;
    uint32_t days = utc / 86400u;
    uint32_t rem = utc % 86400u;
    *h = (int)(rem / 3600u);
    *mi = (int)((rem % 3600u) / 60u);
    *s = (int)(rem % 60u);
    
    uint64_t z = (uint64_t)days + 719468ull;
    uint64_t era = z / 146097ull;
    uint64_t doe = z - era * 146097ull;
    uint64_t yoe = (doe - doe / 1460ull + doe / 36524ull - doe / 146096ull) / 365ull;
    uint64_t yy = yoe + era * 400ull;
    uint64_t doy = doe - (365ull * yoe + yoe / 4ull - yoe / 100ull);
    uint64_t mp = (5ull * doy + 2ull) / 153ull;
    uint64_t dd = doy - (153ull * mp + 2ull) / 5ull + 1ull;
    uint64_t mm = (mp < 10ull) ? mp + 3ull : mp - 9ull;
    if (mm <= 2ull) yy += 1ull;
    
    *y = (int)yy;
    *mo = (int)mm;
    *d = (int)dd;
}

static inline int af_meas_utf8_ok(const char *s, size_t n) {
    if (n == 0) return 1;
    if (s == NULL) return 0;
    
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        
        if (c < 0x20u || c == 0x7Fu) return 0;
        
        if (c <= 0x7Eu) {
            i++;
            continue;
        }
        
        if (c >= 0xC2u && c <= 0xDFu) {
            if (i + 1 >= n) return 0;
            unsigned char c2 = (unsigned char)s[i+1];
            if (c2 < 0x80u || c2 > 0xBFu) return 0;
            i += 2;
            continue;
        }
        
        if (c >= 0xE0u && c <= 0xEFu) {
            if (i + 2 >= n) return 0;
            unsigned char c2 = (unsigned char)s[i+1];
            unsigned char c3 = (unsigned char)s[i+2];
            
            if (c2 < 0x80u || c2 > 0xBFu || c3 < 0x80u || c3 > 0xBFu) return 0;
            
            if (c == 0xE0u) {
                if (c2 < 0xA0u) return 0;
            } else if (c == 0xEDu) {
                if (c2 > 0x9Fu) return 0;
            }
            
            i += 3;
            continue;
        }
        
        if (c >= 0xF0u && c <= 0xF4u) {
            if (i + 3 >= n) return 0;
            unsigned char c2 = (unsigned char)s[i+1];
            unsigned char c3 = (unsigned char)s[i+2];
            unsigned char c4 = (unsigned char)s[i+3];
            
            if (c2 < 0x80u || c2 > 0xBFu || c3 < 0x80u || c3 > 0xBFu || c4 < 0x80u || c4 > 0xBFu) return 0;
            
            if (c == 0xF0u) {
                if (c2 < 0x90u) return 0;
            } else if (c == 0xF4u) {
                if (c2 > 0x8Fu) return 0;
            }
            
            i += 4;
            continue;
        }
        
        return 0;
    }
    return 1;
}

static inline int af_meas_ident_ok(const char *s, size_t maxlen, int allow_space) {
    if (s == NULL) return 0;
    size_t len = am_strnlen(s, maxlen + 1);
    if (len > maxlen) return 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            continue;
        }
        if (allow_space && c == ' ') {
            continue;
        }
        return 0;
    }
    return 1;
}

static inline int af_meas_tpl_ok(const char *tpl) {
    if (tpl == NULL) return 0;
    size_t len = am_strnlen(tpl, 49);
    if (len < 1 || len > 48) return 0;
    
    if (tpl[0] == '.' || tpl[0] == ' ') return 0;
    if (tpl[len - 1] == ' ') return 0;
    
    size_t i = 0;
    while (i < len) {
        char c = tpl[i];
        if (c == '{') {
            if (strncmp(tpl + i, "{prefix}", 8) == 0) {
                i += 8;
                continue;
            }
            if (strncmp(tpl + i, "{n:000}", 7) == 0) {
                i += 7;
                continue;
            }
            if (strncmp(tpl + i, "{date}", 6) == 0) {
                i += 6;
                continue;
            }
            if (strncmp(tpl + i, "{time}", 6) == 0) {
                i += 6;
                continue;
            }
            return 0;
        }
        
        if (c == '}') return 0;
        
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ' ') {
            i++;
            continue;
        }
        
        return 0;
    }
    return 1;
}

static inline int af_meas_tpl_expand(const char *tpl, const char *prefix, uint32_t n, uint32_t utc, char *out, size_t cap) {
    if (out == NULL || tpl == NULL || prefix == NULL) return -1;
    if (af_meas_tpl_ok(tpl) != 1) return -1;
    if (af_meas_ident_ok(prefix, 16, 0) != 1) return -1;
    
    size_t out_len = 0;
    size_t i = 0;
    size_t tpl_len = am_strnlen(tpl, 49);
    
    while (i < tpl_len) {
        if (out_len >= cap) return -1;
        
        if (tpl[i] == '{') {
            if (strncmp(tpl + i, "{prefix}", 8) == 0) {
                size_t p_len = am_strnlen(prefix, 17);
                if (out_len + p_len >= cap) return -1;
                memcpy(out + out_len, prefix, p_len);
                out_len += p_len;
                i += 8;
                continue;
            }
            
            if (strncmp(tpl + i, "{n:000}", 7) == 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%03u", (unsigned)n);
                if (len < 0) return -1;
                if (out_len + (size_t)len >= cap) return -1;
                memcpy(out + out_len, buf, (size_t)len);
                out_len += (size_t)len;
                i += 7;
                continue;
            }
            
            if (strncmp(tpl + i, "{date}", 6) == 0) {
                char buf[16];
                if (utc == 0) {
                    snprintf(buf, sizeof(buf), "00000000");
                } else {
                    int y, mo, d, h, mi, s;
                    af_meas_civil(utc, &y, &mo, &d, &h, &mi, &s);
                    snprintf(buf, sizeof(buf), "%04d%02d%02d", y, mo, d);
                }
                size_t b_len = am_strnlen(buf, 16);
                if (out_len + b_len >= cap) return -1;
                memcpy(out + out_len, buf, b_len);
                out_len += b_len;
                i += 6;
                continue;
            }
            
            if (strncmp(tpl + i, "{time}", 6) == 0) {
                char buf[16];
                if (utc == 0) {
                    snprintf(buf, sizeof(buf), "000000");
                } else {
                    int y, mo, d, h, mi, s;
                    af_meas_civil(utc, &y, &mo, &d, &h, &mi, &s);
                    snprintf(buf, sizeof(buf), "%02d%02d%02d", h, mi, s);
                }
                size_t b_len = am_strnlen(buf, 16);
                if (out_len + b_len >= cap) return -1;
                memcpy(out + out_len, buf, b_len);
                out_len += b_len;
                i += 6;
                continue;
            }
            
            return -1;
        }
        
        out[out_len++] = tpl[i];
        i++;
    }
    
    if (out_len > AF_MEAS_FNAME_MAX - 9) return -1;   /* запас под _NNN и .csv */
    if (out_len + 5 > cap) return -1;
    memcpy(out + out_len, ".csv", 5);   /* вместе с NUL */
    out_len += 4;
    
    return (int)out_len;
}

static inline int af_meas_name_suffix(const char *fname, uint32_t k, char *out, size_t cap) {
    if (fname == NULL || out == NULL) return -1;
    if (k < 1 || k > 999) return -1;
    
    size_t len = am_strnlen(fname, AF_MEAS_FNAME_MAX);
    if (len <= 4) return -1;
    if (len < 4 || strcmp(fname + len - 4, ".csv") != 0) return -1;
    
    size_t base_len = len - 4;
    if (base_len + 1 + 3 + 4 >= cap) return -1;
    if (base_len + 1 + 3 + 4 >= AF_MEAS_FNAME_MAX) return -1;
    
    memcpy(out, fname, base_len);
    out[base_len] = '_';
    snprintf(out + base_len + 1, 4, "%03u", (unsigned)k);
    memcpy(out + base_len + 4, ".csv", 5);   /* вместе с NUL */
    
    return (int)(base_len + 4 + 4);
}

static inline int af_meas_evict_need(uint32_t nfiles, uint32_t total_bytes, uint32_t new_len, uint32_t free_pct) {
    if (nfiles >= AF_MEAS_FILES_MAX) return 1;
    if ((uint64_t)total_bytes + new_len > AF_MEAS_FILES_BYTES) return 1;
    if (free_pct < AF_MEAS_FREE_PCT_MIN) return 1;
    return 0;
}

static inline uint64_t af_meas_n_target(uint32_t eps_x100) {
    if (eps_x100 < AF_MEAS_EPS_MIN || eps_x100 > AF_MEAS_EPS_MAX) return 0;
    uint64_t e2 = (uint64_t)eps_x100 * eps_x100;
    return (100000000ull + e2 - 1) / e2;
}

static inline float af_meas_cps_sigma(uint32_t counts, double live_s) {
    if (live_s <= 0.0 || !isfinite(live_s)) return NAN;
    double sigma = (counts > 0) ? sqrt((double)counts) : 1.0;
    return (float)(sigma / live_s);
}

static inline float af_meas_eps_pct(uint32_t counts) {
    if (counts == 0) return NAN;
    return (float)(100.0 / sqrt((double)counts));
}

static inline int af_meas_params_check(const af_meas_params_t *p, int utc_ok, uint32_t utc_now) {
    if (p == NULL) return AF_MEAS_E_VALUE;
    
    if (am_strnlen(p->name, AF_MEAS_NAME_MAX) >= AF_MEAS_NAME_MAX) return AF_MEAS_E_VALUE;
    if (am_strnlen(p->prefix, AF_MEAS_PREFIX_MAX) >= AF_MEAS_PREFIX_MAX) return AF_MEAS_E_VALUE;
    if (am_strnlen(p->name_tpl, AF_MEAS_TPL_MAX) >= AF_MEAS_TPL_MAX) return AF_MEAS_E_VALUE;
    
    if (af_meas_utf8_ok(p->name, strlen(p->name)) != 1) return AF_MEAS_E_VALUE;
    
    if (p->n > AF_MEAS_N_MAX) return AF_MEAS_E_VALUE;
    if (p->retry_invalid > 1) return AF_MEAS_E_VALUE;
    if (p->file_store > 1) return AF_MEAS_E_VALUE;
    if (p->no_overwrite > 1) return AF_MEAS_E_VALUE;
    if (p->stop_mode > AF_MEAS_STOP_SIGMA) return AF_MEAS_E_VALUE;
    if (p->start_mode > AF_MEAS_START_DELAY) return AF_MEAS_E_VALUE;
    if (p->lost_max > AF_MEAS_LOSTMAX_MAX) return AF_MEAS_E_VALUE;
    if (p->pause_s > AF_MEAS_PAUSE_MAX_S) return AF_MEAS_E_VALUE;
    
    if (p->stop_mode == AF_MEAS_STOP_SIGMA) {
        if (p->n != 1) return AF_MEAS_E_SIGMA;
        if (p->eps_x100 < AF_MEAS_EPS_MIN || p->eps_x100 > AF_MEAS_EPS_MAX) return AF_MEAS_E_VALUE;
        if (p->t_min_s < 1) return AF_MEAS_E_VALUE;
        if (p->t_max_s > AF_MEAS_T_MAX_S) return AF_MEAS_E_VALUE;
        if (p->t_min_s > p->t_max_s) return AF_MEAS_E_VALUE;
    } else {
        if (p->t_s < AF_MEAS_T_MIN_S || p->t_s > AF_MEAS_T_MAX_S) return AF_MEAS_E_VALUE;
    }
    
    if (p->start_mode == AF_MEAS_START_DELAY) {
        if (p->delay_s < 1 || p->delay_s > AF_MEAS_DELAY_MAX_S) return AF_MEAS_E_VALUE;
    } else if (p->start_mode == AF_MEAS_START_AT) {
        if (!utc_ok) return AF_MEAS_E_TIME;
        if ((uint64_t)p->start_at + AF_MEAS_PAST_S < (uint64_t)utc_now) return AF_MEAS_E_VALUE;
        if ((uint64_t)p->start_at > (uint64_t)utc_now + AF_MEAS_AT_MAX_S) return AF_MEAS_E_VALUE;
    }
    
    if (p->file_store == 1) {
        if (af_meas_ident_ok(p->prefix, 16, 0) != 1) return AF_MEAS_E_VALUE;
        if (strlen(p->prefix) == 0) return AF_MEAS_E_VALUE;
        if (af_meas_tpl_ok(p->name_tpl) != 1) return AF_MEAS_E_VALUE;
        
        char tmp[AF_MEAS_FNAME_MAX];
        if (af_meas_tpl_expand(p->name_tpl, p->prefix, 1, 0, tmp, sizeof(tmp)) < 0) return AF_MEAS_E_VALUE;
    }
    
    return 0;
}

/* Статистика окна по счёту (общая для живого замера и для замера из журнала): cps, sigma, мощность дозы, погрешность */
static inline void af_meas_fill_stats(af_meas_res_t *x, uint32_t counts, uint32_t npk, double rate_sum) {
    double live_s = 2.0 * (double)npk;
    x->cps = (live_s > 0.0) ? (float)(counts / live_s) : 0.0f;
    x->cps_sigma = af_meas_cps_sigma(counts, live_s);
    x->usv_h = (npk > 0) ? (float)(rate_sum / (double)npk) : NAN;
    x->usv_h_sigma = (counts > 0 && npk > 0) ? (float)(x->usv_h / sqrt((double)counts)) : NAN;
    x->eps_pct = af_meas_eps_pct(counts);
}

static inline uint32_t af_meas_res_count(const af_meas_run_t *r) {
    return r ? r->res_n : 0;
}

static inline const af_meas_res_t *af_meas_res_at(const af_meas_run_t *r, uint32_t idx) {
    if (!r || !r->res || r->res_cap == 0 || idx >= r->res_n) return NULL;
    uint32_t base = (r->res_head + r->res_cap - r->res_n) % r->res_cap;
    return &r->res[(base + idx) % r->res_cap];
}

static inline void af_meas_init(af_meas_core_t *c, af_meas_alloc_fn a, af_meas_free_fn f, af_meas_seq_fn s, af_meas_log_fn l) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->alloc = a;
    c->free_fn = f;
    c->seq_fn = s;
    c->log_fn = l;
    c->active = -1;
}

static inline af_meas_run_t *af_meas_find(af_meas_core_t *c, uint32_t id) {
    if (!c || id == 0) return NULL;
    for (uint32_t i = 0; i < AF_MEAS_MAX_RUNS; i++) {
        if (c->runs[i].id == id) return &c->runs[i];
    }
    return NULL;
}

static inline void am_log(af_meas_core_t *c, int level, const char *msg) {
    if (c && c->log_fn) c->log_fn(level, msg);
}

static inline void am_free_run(af_meas_core_t *c, af_meas_run_t *r) {
    if (!r) return;
    if (r->res && c && c->free_fn) {
        c->free_fn(r->res);
    }
    memset(r, 0, sizeof(*r));
}

static inline void am_res_push(af_meas_run_t *r, const af_meas_res_t *x) {
    if (!r || !r->res || r->res_cap == 0) return;
    r->res[r->res_head] = *x;
    r->res_head = (r->res_head + 1) % r->res_cap;
    if (r->res_n < r->res_cap) {
        r->res_n++;
    } else {
        r->dropped++;
    }
    r->rows_total++;
}

static inline void am_open_window(af_meas_core_t *c, af_meas_run_t *r, uint64_t t0_ms) {
    if (!c || !r) return;
    r->phase = AF_MEAS_PH_MEASURING;
    r->t0_ms = t0_ms;
    r->last_rx_ms = t0_ms;
    r->cur_counts = 0;
    r->cur_npk = 0;
    r->cur_lost = 0;
    r->cur_rate_sum = 0.0;
    r->cur_flags = 0;
    r->cur_raw_seq0 = 0;
    r->cur_raw_epoch = 0;
    
    if (c->seq_fn) {
        uint32_t epoch_local = 0;
        r->cur_raw_seq0 = c->seq_fn(&epoch_local);
        r->cur_raw_epoch = epoch_local;
    }
    
    if (c->utc_ok) {
        r->cur_flags |= AF_MEAS_F_UTC_OK;
        r->cur_utc_off = c->utc_off;
        r->cur_t0_utc = (uint32_t)(c->utc_off + (int64_t)(t0_ms / 1000));
    } else {
        r->cur_utc_off = 0;
        r->cur_t0_utc = 0;
    }
}

static inline void am_finish_run(af_meas_core_t *c, af_meas_run_t *r, uint64_t end_ms, uint8_t reason) {
    if (!c || !r) return;
    r->state = AF_MEAS_ST_DONE;
    r->phase = AF_MEAS_PH_NONE;
    r->end_reason = reason;
    r->end_mono_s = (uint32_t)(end_ms / 1000);
    
    if (c->active >= 0 && &c->runs[c->active] == r) {
        c->active = -1;
    }
    
    char msg[96];
    snprintf(msg, sizeof(msg), "m%u done rows=%u", (unsigned)r->id, (unsigned)r->rows_total);
    am_log(c, 1, msg);
}

static inline void am_schedule(af_meas_core_t *c, af_meas_run_t *r, uint64_t end_ms) {
    if (!c || !r) return;
    uint64_t nx = end_ms + r->pause_ms;
    if (r->pause_ms == 0) {
        am_open_window(c, r, nx);
    } else {
        r->phase = AF_MEAS_PH_GAP;
        r->next_ms = nx;
    }
}

static inline void am_close(af_meas_core_t *c, af_meas_run_t *r, uint64_t end_ms, uint8_t reason, int sched) {
    if (!c || !r) return;
    if (r->phase != AF_MEAS_PH_MEASURING) return;

    uint64_t el = (end_ms > r->t0_ms) ? (end_ms - r->t0_ms) : 0;
    uint64_t tail = (end_ms > r->last_rx_ms) ? (end_ms - r->last_rx_ms) : 0;
    if (tail > AF_MEAS_GAP_MS) {
        uint32_t add = (uint32_t)((tail + 1000) / 2000) - 1;
        r->cur_lost += add;
    }

    uint16_t npk = r->cur_npk;
    uint32_t counts = r->cur_counts;

    if (npk == 0 && r->dead_prev && sched) {   /* продолжение обрыва связи: строку не пишем, попытку не тратим */
        r->phase = AF_MEAS_PH_NONE;
        am_schedule(c, r, end_ms);
        return;
    }
    r->dead_prev = (npk == 0) ? 1 : 0;

    int valid = (npk >= 1 && r->cur_lost <= r->lost_max);
    int final = valid || !r->retry_invalid || r->cur_attempt >= AF_MEAS_ATTEMPTS || reason == AF_MEAS_SR_MANUAL;

    af_meas_res_t x;
    memset(&x, 0, sizeof(x));
    x.i = r->cur_i;
    x.attempt = r->cur_attempt;
    x.valid = valid ? 1 : 0;
    x.stop_reason = reason;
    x.t0_utc = r->cur_t0_utc;
    x.t0_mono_s = (uint32_t)(r->t0_ms / 1000);
    x.t_ms = (el > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)el;
    x.counts = counts;
    x.npk = npk;
    x.lost = (r->cur_lost > 65535u) ? 65535u : (uint16_t)r->cur_lost;
    x.raw_seq0 = r->cur_raw_seq0;
    x.raw_epoch = r->cur_raw_epoch;
    x.raw_seq1 = 0;
    if (c->seq_fn) {
        uint32_t ep = 0;
        x.raw_seq1 = c->seq_fn(&ep);
    }
    
    af_meas_fill_stats(&x, counts, npk, r->cur_rate_sum);

    uint8_t flags = r->cur_flags;
    if (final) flags |= AF_MEAS_F_FINAL;
    if (reason == AF_MEAS_SR_MANUAL) flags |= AF_MEAS_F_PARTIAL;
    if (reason == AF_MEAS_SR_LIMIT && r->stop_mode == AF_MEAS_STOP_SIGMA) flags |= AF_MEAS_F_GOAL_MISS;
    x.flags = flags;

    am_res_push(r, &x);

    if (!valid) {
        char msg[96];
        snprintf(msg, sizeof(msg), "m%u #%u try%u invalid lost=%u", (unsigned)r->id, (unsigned)r->cur_i, (unsigned)r->cur_attempt, (unsigned)r->cur_lost);
        am_log(c, 2, msg);
    }

    r->phase = AF_MEAS_PH_NONE;

    if (!sched) return;

    if (!valid && !final) {
        r->cur_attempt++;
        am_schedule(c, r, end_ms);
        return;
    }

    r->done_i++;
    if (r->n != 0 && r->done_i >= r->n) {
        am_finish_run(c, r, end_ms, reason);
        return;
    }

    r->cur_i++;
    r->cur_attempt = 1;
    am_schedule(c, r, end_ms);
}

static inline void am_advance(af_meas_core_t *c, af_meas_run_t *r, uint64_t now_ms) {
    if (!c || !r) return;
    
    for (int i = 0; i < 4096; i++) {
        if (r->state == AF_MEAS_ST_WAIT) {
            if (now_ms >= r->next_ms) {
                r->state = AF_MEAS_ST_RUN;
                am_open_window(c, r, r->next_ms);
                char msg[96];
                snprintf(msg, sizeof(msg), "m%u start", (unsigned)r->id);
                am_log(c, 1, msg);
                continue;
            } else {
                return;
            }
        }

        if (r->state != AF_MEAS_ST_RUN) {
            return;
        }

        if (r->phase == AF_MEAS_PH_GAP) {
            if (now_ms >= r->next_ms) {
                am_open_window(c, r, r->next_ms);
                continue;
            } else {
                return;
            }
        }

        if (r->phase == AF_MEAS_PH_MEASURING) {
            uint64_t end;
            if (r->stop_mode == AF_MEAS_STOP_TIME) {
                end = r->t0_ms + r->t_ms;
            } else {
                end = r->t0_ms + r->t_max_ms;
            }
            
            if (now_ms >= end) {
                uint8_t reason = (r->stop_mode == AF_MEAS_STOP_TIME) ? AF_MEAS_SR_TIME : AF_MEAS_SR_LIMIT;
                am_close(c, r, end, reason, 1);
                continue;
            } else {
                return;
            }
        }

        return;
    }
}

static inline int am_loss_check(af_meas_core_t *c, af_meas_run_t *r, uint64_t now_ms) {
    if (!c || !r) return 0;
    if (r->phase != AF_MEAS_PH_MEASURING) return 0;

    uint32_t pending = 0;
    if (now_ms > r->last_rx_ms && (now_ms - r->last_rx_ms) > AF_MEAS_GAP_MS) {
        pending = (uint32_t)((now_ms - r->last_rx_ms + 1000) / 2000) - 1;
    }

    if (r->cur_lost + pending > r->lost_max) {
        r->cur_lost += pending;
        r->last_rx_ms = now_ms;
        am_close(c, r, now_ms, AF_MEAS_SR_ERROR, 1);
        return 1;
    }
    return 0;
}

static inline void af_meas_feed(af_meas_core_t *c, uint64_t rx_ms, uint16_t pulses, float rate_usvh) {
    if (!c || c->active < 0) return;
    af_meas_run_t *r = &c->runs[c->active];
    
    am_advance(c, r, rx_ms);

    if (r->state != AF_MEAS_ST_RUN || r->phase != AF_MEAS_PH_MEASURING) return;
    if (rx_ms < r->t0_ms) return;

    uint64_t gap = rx_ms - r->last_rx_ms;
    uint32_t missed = (gap > AF_MEAS_GAP_MS) ? (uint32_t)((gap + 1000) / 2000) - 1 : 0;

    if (r->cur_lost + missed > r->lost_max) {
        r->cur_lost += missed;
        r->last_rx_ms = rx_ms;
        am_close(c, r, rx_ms, AF_MEAS_SR_ERROR, 1);
        return;
    }

    r->cur_lost += missed;
    
    if (r->cur_counts > 0xFFFFFFFFu - pulses) {
        r->cur_counts = 0xFFFFFFFFu;
    } else {
        r->cur_counts += pulses;
    }
    
    if (r->cur_npk < 65535u) {
        r->cur_npk++;
    }
    
    float rate = rate_usvh;
    if (!(rate >= 0.0f)) rate = 0.0f;
    r->cur_rate_sum += (double)rate;
    r->last_rx_ms = rx_ms;

    if (r->stop_mode == AF_MEAS_STOP_SIGMA) {
        if (rx_ms - r->t0_ms >= r->t_min_ms && r->cur_counts >= af_meas_n_target(r->eps_x100)) {
            am_close(c, r, rx_ms, AF_MEAS_SR_SIGMA, 1);
        }
    }
}

static inline void af_meas_tick(af_meas_core_t *c, uint64_t now_ms, int utc_ok, uint32_t utc_now) {
    if (!c) return;
    
    c->utc_ok = utc_ok ? 1 : 0;
    if (utc_ok) {
        c->utc_off = (int64_t)utc_now - (int64_t)(now_ms / 1000);
    }

    if (c->active < 0) return;
    af_meas_run_t *r = &c->runs[c->active];
    
    am_advance(c, r, now_ms);

    if (r->state == AF_MEAS_ST_RUN && r->phase == AF_MEAS_PH_MEASURING) {
        if (am_loss_check(c, r, now_ms)) {
            return;
        }
        
        if (utc_ok && (r->cur_flags & AF_MEAS_F_UTC_OK)) {
            int64_t d = c->utc_off - r->cur_utc_off;
            if (d < 0) d = -d;
            if (d > AF_MEAS_JUMP_S) {
                r->cur_flags |= AF_MEAS_F_TIMEJUMP;
            }
        }
    }
}
static inline af_meas_run_t *am_pick(af_meas_core_t *c, uint32_t id) {
    if (c == NULL) return NULL;
    if (id == 0) {
        if (c->active < 0) return NULL;
        return &c->runs[c->active];
    }
    return af_meas_find(c, id);
}

/* Свободный слот прогона; если все заняты - вытесняется самый старый завершённый; -1 = нет места */
static inline int am_slot(af_meas_core_t *c) {
    int slot_idx = -1;
    for (int i = 0; i < AF_MEAS_MAX_RUNS; i++) {
        if (c->runs[i].id == 0) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx == -1) {
        uint32_t min_id = 0xFFFFFFFFu;
        for (int i = 0; i < AF_MEAS_MAX_RUNS; i++) {
            uint8_t st = c->runs[i].state;
            if ((st == AF_MEAS_ST_DONE || st == AF_MEAS_ST_ABORTED || st == AF_MEAS_ST_ERROR) && c->runs[i].id < min_id) {
                min_id = c->runs[i].id;
                slot_idx = i;
            }
        }
        if (slot_idx != -1) am_free_run(c, &c->runs[slot_idx]);
    }
    return slot_idx;
}

static inline int af_meas_start(af_meas_core_t *c, const af_meas_params_t *p, uint64_t now_ms, int utc_ok, uint32_t utc_now, uint32_t *id_out) {
    if (c == NULL || p == NULL) return AF_MEAS_E_VALUE;
    int rc = af_meas_params_check(p, utc_ok, utc_now);
    if (rc != 0) return rc;
    if (c->active >= 0) return AF_MEAS_E_BUSY;

    c->utc_ok = utc_ok ? 1 : 0;
    if (utc_ok) {
        c->utc_off = (int64_t)utc_now - (int64_t)(now_ms / 1000);
    }

    int slot_idx = am_slot(c);
    if (slot_idx < 0) return AF_MEAS_E_BUSY;

    if (c->alloc == NULL) return AF_MEAS_E_NOMEM;

    uint32_t want = (p->n == 0) ? AF_MEAS_RES_MAX : (p->retry_invalid ? p->n * AF_MEAS_ATTEMPTS : p->n);
    if (want < 1u) want = 1u;
    if (want > AF_MEAS_RES_MAX) want = AF_MEAS_RES_MAX;

    uint32_t cap = want;
    void *buf = NULL;
    while (1) {
        buf = c->alloc(cap * sizeof(af_meas_res_t));
        if (buf != NULL) break;
        if (cap <= AF_MEAS_RES_MIN) {
            return AF_MEAS_E_NOMEM;
        }
        cap = cap / 2;
        if (cap < AF_MEAS_RES_MIN) cap = AF_MEAS_RES_MIN;
    }
    memset(buf, 0, cap * sizeof(af_meas_res_t));

    af_meas_run_t *r = &c->runs[slot_idx];
    memset(r, 0, sizeof(af_meas_run_t));
    
    r->id = ++c->next_id;
    if (r->id == 0) r->id = 1;

    memcpy(r->name, p->name, am_strnlen(p->name, AF_MEAS_NAME_MAX - 1));   /* хвост нулевой после memset */
    memcpy(r->prefix, p->prefix, am_strnlen(p->prefix, AF_MEAS_PREFIX_MAX - 1));
    memcpy(r->tpl, p->name_tpl, am_strnlen(p->name_tpl, AF_MEAS_TPL_MAX - 1));

    if (p->n == 1) r->mode = AF_MEAS_MODE_SINGLE;
    else if (p->n == 0) r->mode = AF_MEAS_MODE_ENDLESS;
    else r->mode = AF_MEAS_MODE_SERIES;

    r->retry_invalid = p->retry_invalid;
    r->lost_max = p->lost_max;
    r->stop_mode = p->stop_mode;
    r->start_mode = p->start_mode;
    r->file_store = p->file_store;
    r->no_overwrite = p->no_overwrite;
    r->eps_x100 = p->eps_x100;
    r->res = (af_meas_res_t *)buf;
    r->res_cap = cap;
    r->n = p->n;
    r->t_ms = p->t_s * 1000u;
    r->pause_ms = p->pause_s * 1000u;
    r->t_min_ms = p->t_min_s * 1000u;
    r->t_max_ms = p->t_max_s * 1000u;
    r->cur_i = 1;
    r->cur_attempt = 1;

    uint64_t first = 0;
    if (p->start_mode == AF_MEAS_START_NOW) {
        first = now_ms;
    } else if (p->start_mode == AF_MEAS_START_DELAY) {
        first = now_ms + (uint64_t)p->delay_s * 1000u;
    } else { /* AF_MEAS_START_AT */
        if (p->start_at <= utc_now) {
            first = now_ms;
        } else {
            first = now_ms + (uint64_t)(p->start_at - utc_now) * 1000u;
        }
    }

    r->start_mono_s = (uint32_t)(first / 1000);
    if (utc_ok) {
        r->start_utc = (uint32_t)(c->utc_off + (int64_t)(first / 1000));
    } else {
        r->start_utc = 0;
    }

    if (first <= now_ms) {
        r->state = AF_MEAS_ST_RUN;
        am_open_window(c, r, now_ms);
    } else {
        r->state = AF_MEAS_ST_WAIT;
        r->phase = AF_MEAS_PH_WAITSTART;
        r->next_ms = first;
    }

    c->active = slot_idx;
    if (id_out != NULL) *id_out = r->id;

    char msg[96];
    snprintf(msg, sizeof(msg), "m%u n=%u T=%us started", (unsigned)r->id, (unsigned)r->n, (unsigned)(r->t_ms / 1000u));
    if (c->log_fn != NULL) c->log_fn(1, msg);

    return 0;
}

static inline int af_meas_stop(af_meas_core_t *c, uint32_t id, uint64_t now_ms) {
    af_meas_run_t *r = am_pick(c, id);
    if (r == NULL) {
        if (id != 0) return AF_MEAS_E_NOTFOUND;
        return AF_MEAS_E_STATE;
    }

    if (r->state != AF_MEAS_ST_WAIT && r->state != AF_MEAS_ST_RUN && r->state != AF_MEAS_ST_PAUSED) {
        return AF_MEAS_E_STATE;
    }

    if (r->state == AF_MEAS_ST_RUN) {
        am_advance(c, r, now_ms);
        if (r->state != AF_MEAS_ST_RUN) {
            return AF_MEAS_E_STATE;
        }
        if (r->phase == AF_MEAS_PH_MEASURING) {
            if (r->cur_npk >= 1) {
                am_close(c, r, now_ms, AF_MEAS_SR_MANUAL, 0);
                r->done_i++;
            } else {
                r->phase = AF_MEAS_PH_NONE;
            }
        }
    }

    am_finish_run(c, r, now_ms, AF_MEAS_SR_MANUAL);
    return 0;
}

static inline int af_meas_pause(af_meas_core_t *c, uint32_t id, uint64_t now_ms) {
    af_meas_run_t *r = am_pick(c, id);
    if (r == NULL) {
        if (id != 0) return AF_MEAS_E_NOTFOUND;
        return AF_MEAS_E_STATE;
    }

    if (r->state != AF_MEAS_ST_RUN && r->state != AF_MEAS_ST_WAIT) {
        return AF_MEAS_E_STATE;
    }

    if (r->state == AF_MEAS_ST_RUN) {
        am_advance(c, r, now_ms);
        if (r->state != AF_MEAS_ST_RUN) {
            return AF_MEAS_E_STATE;
        }
    }

    r->phase = AF_MEAS_PH_NONE;
    r->cur_counts = 0;
    r->cur_npk = 0;
    r->cur_lost = 0;
    r->cur_rate_sum = 0.0;
    r->state = AF_MEAS_ST_PAUSED;

    char msg[96];
    snprintf(msg, sizeof(msg), "m%u paused", (unsigned)r->id);
    if (c->log_fn != NULL) c->log_fn(1, msg);

    return 0;
}

static inline int af_meas_resume(af_meas_core_t *c, uint32_t id, uint64_t now_ms) {
    af_meas_run_t *r = am_pick(c, id);
    if (r == NULL) {
        if (id != 0) return AF_MEAS_E_NOTFOUND;
        return AF_MEAS_E_STATE;
    }

    if (r->state != AF_MEAS_ST_PAUSED) {
        return AF_MEAS_E_STATE;
    }

    r->state = AF_MEAS_ST_RUN;
    am_open_window(c, r, now_ms);

    char msg[96];
    snprintf(msg, sizeof(msg), "m%u resumed", (unsigned)r->id);
    if (c->log_fn != NULL) c->log_fn(1, msg);

    return 0;
}

static inline int af_meas_delete(af_meas_core_t *c, uint32_t id) {
    if (c == NULL || id == 0) return AF_MEAS_E_NOTFOUND;
    af_meas_run_t *r = af_meas_find(c, id);
    if (r == NULL) return AF_MEAS_E_NOTFOUND;

    if (r->state == AF_MEAS_ST_WAIT || r->state == AF_MEAS_ST_RUN || r->state == AF_MEAS_ST_PAUSED) {
        return AF_MEAS_E_STATE;
    }

    am_free_run(c, r);
    return 0;
}

static inline int af_meas_live(const af_meas_run_t *r, uint64_t now_ms, af_meas_live_t *out) {
    if (out == NULL) return -1;
    memset(out, 0, sizeof(af_meas_live_t));
    out->cps = NAN;
    out->cps_sigma = NAN;
    out->eps_pct = NAN;
    out->usv_h = NAN;
    out->eta_s = -1;

    if (r == NULL) return -1;
    if (r->state != AF_MEAS_ST_WAIT && r->state != AF_MEAS_ST_RUN && r->state != AF_MEAS_ST_PAUSED) {
        return -1;
    }

    out->i = r->cur_i;
    out->attempt = r->cur_attempt;
    out->phase = r->phase;

    if (r->state == AF_MEAS_ST_WAIT) {
        if (r->next_ms > now_ms) {
            uint64_t rem = r->next_ms - now_ms;
            out->remaining_ms = (rem > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)rem;
        } else {
            out->remaining_ms = 0;
        }
        return 0;
    }

    if (r->state == AF_MEAS_ST_PAUSED) {
        return 0;
    }

    if (r->phase == AF_MEAS_PH_GAP) {
        if (r->next_ms > now_ms) {
            uint64_t rem = r->next_ms - now_ms;
            out->remaining_ms = (rem > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)rem;
        } else {
            out->remaining_ms = 0;
        }
        return 0;
    }

    if (r->phase == AF_MEAS_PH_MEASURING) {
        uint64_t elapsed = (now_ms > r->t0_ms) ? (now_ms - r->t0_ms) : 0;
        if (elapsed > 0xFFFFFFFFull) elapsed = 0xFFFFFFFFull;
        out->elapsed_ms = (uint32_t)elapsed;
        out->counts = r->cur_counts;
        out->lost = r->cur_lost;
        out->npk = (uint16_t)r->cur_npk;

        double live_s = 2.0 * (double)r->cur_npk;
        if (live_s > 0.0) {
            out->cps = (float)((double)r->cur_counts / live_s);
            out->cps_sigma = af_meas_cps_sigma(r->cur_counts, live_s);
            out->eps_pct = af_meas_eps_pct(r->cur_counts);
            out->usv_h = (float)(r->cur_rate_sum / (double)r->cur_npk);
        }

        if (r->stop_mode == AF_MEAS_STOP_TIME) {
            if (r->t_ms > elapsed) {
                uint64_t rem = r->t_ms - elapsed;
                out->remaining_ms = (rem > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)rem;
            } else {
                out->remaining_ms = 0;
            }
        } else {
            if (r->t_max_ms > elapsed) {
                uint64_t rem = r->t_max_ms - elapsed;
                out->remaining_ms = (rem > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)rem;
            } else {
                out->remaining_ms = 0;
            }

            uint64_t n_target = af_meas_n_target(r->eps_x100);
            out->n_target = n_target;
            uint64_t tmin_left = (r->t_min_ms > elapsed) ? (r->t_min_ms - elapsed) : 0;
            
            if ((uint64_t)r->cur_counts >= n_target) {
                out->eta_s = (int32_t)((tmin_left + 999) / 1000);
            } else if (out->cps > 0.0f) {
                double need = ((double)n_target - (double)r->cur_counts) / (double)out->cps;
                int32_t eta_min = (int32_t)((tmin_left + 999) / 1000);
                int32_t eta_calc = (int32_t)ceil(need);
                if (eta_calc < eta_min) eta_calc = eta_min;
                if (eta_calc > 0x7FFFFFFF) eta_calc = 0x7FFFFFFF;
                out->eta_s = eta_calc;
            } else {
                out->eta_s = -1;
            }
        }
    }

    return 0;
}

static inline void af_meas_summary(const af_meas_run_t *r, af_meas_sum_t *s) {
    if (s == NULL) return;
    memset(s, 0, sizeof(af_meas_sum_t));
    s->mean_cps = NAN;
    s->sd_cps = NAN;
    s->sem_cps = NAN;
    s->min_cps = NAN;
    s->max_cps = NAN;
    s->pooled_cps = NAN;
    s->pooled_sigma = NAN;
    s->chi2_ndf = NAN;
    s->mean_usvh = NAN;
    s->dose_usv = NAN;

    if (r == NULL) return;

    uint32_t rows = af_meas_res_count(r);
    s->rows = rows;

    uint32_t n_valid = 0;
    uint32_t n_invalid = 0;
    uint32_t counts_sum = 0;
    double live_s = 0.0;
    double sum_cps = 0.0;
    double min_cps = 0.0;
    double max_cps = 0.0;
    int min_max_init = 0;
    double sum_usv = 0.0;
    uint32_t nu = 0;
    double dose_usv_acc = 0.0;

    for (uint32_t i = 0; i < rows; i++) {
        const af_meas_res_t *rec = af_meas_res_at(r, i);
        if (rec == NULL) continue;

        if (rec->valid == 0) {
            n_invalid++;
            continue;
        }

        int is_partial = (rec->flags & AF_MEAS_F_PARTIAL) != 0;
        if (is_partial && r->mode != AF_MEAS_MODE_SINGLE) {
            continue;
        }

        n_valid++;
        
        if (counts_sum > 0xFFFFFFFFu - rec->counts) {
            counts_sum = 0xFFFFFFFFu;
        } else {
            counts_sum += rec->counts;
        }

        double li = 2.0 * (double)rec->npk;
        live_s += li;

        if (!isnan(rec->cps)) {
            sum_cps += rec->cps;
            if (!min_max_init) {
                min_cps = rec->cps;
                max_cps = rec->cps;
                min_max_init = 1;
            } else {
                if (rec->cps < min_cps) min_cps = rec->cps;
                if (rec->cps > max_cps) max_cps = rec->cps;
            }
        }

        if (!isnan(rec->usv_h)) {
            sum_usv += rec->usv_h;
            nu++;
            dose_usv_acc += rec->usv_h * (li / 3600.0);
        }
    }

    s->n_valid = n_valid;
    s->n_invalid = n_invalid;
    s->counts = counts_sum;
    s->live_s = live_s;

    if (n_valid >= 1) {
        s->mean_cps = sum_cps / (double)n_valid;
        if (live_s > 0.0) {
            s->pooled_cps = (double)counts_sum / live_s;
            double sig = (counts_sum > 0) ? sqrt((double)counts_sum) : 1.0;
            s->pooled_sigma = sig / live_s;
        }
        if (nu > 0) {
            s->mean_usvh = sum_usv / (double)nu;
            s->dose_usv = dose_usv_acc;
        }
        if (min_max_init) {
            s->min_cps = min_cps;
            s->max_cps = max_cps;
        }
    }

    if (n_valid >= 2) {
        double ss = 0.0;
        double sw = 0.0;
        double swc = 0.0;

        for (uint32_t i = 0; i < rows; i++) {
            const af_meas_res_t *rec = af_meas_res_at(r, i);
            if (rec == NULL) continue;
            if (rec->valid == 0) continue;
            int is_partial = (rec->flags & AF_MEAS_F_PARTIAL) != 0;
            if (is_partial && r->mode != AF_MEAS_MODE_SINGLE) continue;
            if (isnan(rec->cps)) continue;

            double diff = rec->cps - s->mean_cps;
            ss += diff * diff;

            double li = 2.0 * (double)rec->npk;
            if (li <= 0.0) continue;
            double sig_i = (rec->counts > 0) ? sqrt((double)rec->counts) : 1.0;
            sig_i /= li;
            double w = 1.0 / (sig_i * sig_i);
            sw += w;
            swc += w * rec->cps;
        }

        s->sd_cps = sqrt(ss / (double)(n_valid - 1));
        s->sem_cps = s->sd_cps / sqrt((double)n_valid);

        if (sw > 0.0) {
            double mu = swc / sw;
            double chi2 = 0.0;
            for (uint32_t i = 0; i < rows; i++) {
                const af_meas_res_t *rec = af_meas_res_at(r, i);
                if (rec == NULL) continue;
                if (rec->valid == 0) continue;
                int is_partial = (rec->flags & AF_MEAS_F_PARTIAL) != 0;
                if (is_partial && r->mode != AF_MEAS_MODE_SINGLE) continue;
                if (isnan(rec->cps)) continue;

                double li = 2.0 * (double)rec->npk;
                if (li <= 0.0) continue;
                double sig_i = (rec->counts > 0) ? sqrt((double)rec->counts) : 1.0;
                sig_i /= li;
                double w = 1.0 / (sig_i * sig_i);
                double diff = rec->cps - mu;
                chi2 += w * diff * diff;
            }
            s->chi2_ndf = chi2 / (double)(n_valid - 1);
        }
    }
}
static inline const char *af_meas_state_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_ST_WAIT: return "wait";
        case AF_MEAS_ST_RUN: return "running";
        case AF_MEAS_ST_PAUSED: return "paused";
        case AF_MEAS_ST_DONE: return "done";
        case AF_MEAS_ST_ABORTED: return "aborted";
        case AF_MEAS_ST_ERROR: return "error";
        default: return "unknown";
    }
}

static inline const char *af_meas_phase_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_PH_NONE: return "none";
        case AF_MEAS_PH_WAITSTART: return "waitstart";
        case AF_MEAS_PH_MEASURING: return "measuring";
        case AF_MEAS_PH_GAP: return "gap";
        default: return "unknown";
    }
}

static inline const char *af_meas_mode_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_MODE_SINGLE: return "single";
        case AF_MEAS_MODE_SERIES: return "series";
        case AF_MEAS_MODE_ENDLESS: return "endless";
        case AF_MEAS_MODE_JOURNAL: return "journal";
        default: return "unknown";
    }
}

static inline const char *af_meas_sr_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_SR_NONE: return "none";
        case AF_MEAS_SR_TIME: return "time";
        case AF_MEAS_SR_SIGMA: return "sigma";
        case AF_MEAS_SR_LIMIT: return "limit";
        case AF_MEAS_SR_MANUAL: return "manual";
        case AF_MEAS_SR_ERROR: return "error";
        default: return "unknown";
    }
}

static inline const char *af_meas_stopmode_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_STOP_TIME: return "time";
        case AF_MEAS_STOP_SIGMA: return "sigma";
        default: return "unknown";
    }
}

static inline const char *af_meas_startmode_str(uint8_t v) {
    switch (v) {
        case AF_MEAS_START_NOW: return "now";
        case AF_MEAS_START_AT: return "at";
        case AF_MEAS_START_DELAY: return "delay";
        default: return "unknown";
    }
}

typedef struct { char *b; size_t cap; size_t len; int err; int first; } am_w_t;

static inline void am_w_init(am_w_t *w, char *b, size_t cap) {
    w->b = b;
    w->cap = cap;
    w->len = 0;
    w->err = 0;
    w->first = 1;
    if (cap > 0) b[0] = '\0';
}

static inline void am_w_raw(am_w_t *w, const char *s) {
    if (w->err) return;
    size_t slen = strlen(s);
    if (w->len + slen + 1 > w->cap) {
        size_t avail = (w->cap > 0) ? (w->cap - 1 - w->len) : 0;
        if (avail > slen) avail = slen;
        if (avail > 0) memcpy(w->b + w->len, s, avail);
        w->len += avail;
        if (w->cap > 0) w->b[w->len] = '\0';
        w->err = 1;
    } else {
        memcpy(w->b + w->len, s, slen);
        w->len += slen;
        if (w->cap > 0) w->b[w->len] = '\0';
    }
}

static inline void am_w_sep(am_w_t *w) {
    if (!w->first) am_w_raw(w, ",");
    w->first = 0;
}

static inline void am_w_open(am_w_t *w) {
    am_w_raw(w, "{");
    w->first = 1;
}

static inline void am_w_close(am_w_t *w) {
    am_w_raw(w, "}");
    w->first = 0;
}

static inline void am_w_key(am_w_t *w, const char *key) {
    am_w_sep(w);
    am_w_raw(w, "\"");
    am_w_raw(w, key);
    am_w_raw(w, "\":");
}

static inline void am_w_u(am_w_t *w, const char *key, uint64_t v) {
    char t[32];
    snprintf(t, sizeof(t), "%llu", (unsigned long long)v);
    am_w_key(w, key);
    am_w_raw(w, t);
}

static inline void am_w_f(am_w_t *w, const char *key, double v) {
    char t[32];
    if (!isfinite(v)) {
        am_w_key(w, key);
        am_w_raw(w, "null");
    } else {
        snprintf(t, sizeof(t), "%.6g", v);
        am_w_key(w, key);
        am_w_raw(w, t);
    }
}

static inline void am_w_b(am_w_t *w, const char *key, int v) {
    am_w_key(w, key);
    am_w_raw(w, v ? "true" : "false");
}

static inline void am_w_null(am_w_t *w, const char *key) {
    am_w_key(w, key);
    am_w_raw(w, "null");
}

static inline void am_w_s(am_w_t *w, const char *key, const char *s) {
    am_w_key(w, key);
    am_w_raw(w, "\"");
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            if (*p == '"') am_w_raw(w, "\\\"");
            else if (*p == '\\') am_w_raw(w, "\\\\");
            else if (*p < 0x20) {
                char t[8];
                snprintf(t, sizeof(t), "\\u%04x", (unsigned int)*p);
                am_w_raw(w, t);
            } else {
                char c2[2] = { (char)*p, '\0' };
                am_w_raw(w, c2);
            }
        }
    }
    am_w_raw(w, "\"");
}

static inline void am_w_rawkey(am_w_t *w, const char *key, const char *json) {
    am_w_key(w, key);
    am_w_raw(w, json);
}

static inline int am_w_done(const am_w_t *w) {
    return w->err ? -1 : (int)w->len;
}

static inline int af_meas_fmt_utc(uint32_t utc, char *out, size_t cap) {
    if (utc == 0) {
        if (cap > 0) out[0] = '\0';
        return 0;
    }
    if (cap < 21) return -1;
    int y, mo, d, h, mi, s;
    af_meas_civil(utc, &y, &mo, &d, &h, &mi, &s);
    char t[64];
    snprintf(t, sizeof t, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, h, mi, s);
    memcpy(out, t, 21);   /* 20 символов и NUL */
    return 20;
}

static inline int af_meas_fmt_csv_header(char *out, size_t cap) {
    const char *h = "i,t0_utc,t_s,counts,cps,cps_sigma,usv_h,usv_h_sigma,valid,stop_reason,eps_achieved_pct,attempt,npk,lost_packets,partial,raw_seq0,raw_seq1\n";
    size_t len = strlen(h);
    if (cap < len + 1) return -1;
    memcpy(out, h, len);
    out[len] = '\0';
    return (int)len;
}

static inline void am_numtxt(char *buf, size_t cap, double v) {
    if (!isfinite(v)) {
        if (cap > 0) buf[0] = '\0';
    } else {
        snprintf(buf, cap, "%.6g", v);
    }
}

static inline int af_meas_fmt_csv_row(const af_meas_res_t *x, char *out, size_t cap) {
    char t_utc[32], t_ts[32], t_cps[32], t_cps_sig[32], t_usv[32], t_usv_sig[32], t_eps[32];
    af_meas_fmt_utc(x->t0_utc, t_utc, sizeof(t_utc));
    snprintf(t_ts, sizeof(t_ts), "%.3f", (double)x->t_ms / 1000.0);
    am_numtxt(t_cps, sizeof(t_cps), x->cps);
    am_numtxt(t_cps_sig, sizeof(t_cps_sig), x->cps_sigma);
    af_fmt_rate(t_usv, sizeof(t_usv), t_usv_sig, sizeof(t_usv_sig), t_eps, sizeof(t_eps), x->usv_h, x->eps_pct);
    int partial = (x->flags & AF_MEAS_F_PARTIAL) ? 1 : 0;
    int n = snprintf(out, cap, "%u,%s,%s,%u,%s,%s,%s,%s,%u,%s,%s,%u,%u,%u,%u,%u,%u\n",
        (unsigned)x->i, t_utc, t_ts, (unsigned)x->counts, t_cps, t_cps_sig, t_usv, t_usv_sig,
        x->valid, af_meas_sr_str(x->stop_reason), t_eps,
        x->attempt, x->npk, x->lost, partial, (unsigned)x->raw_seq0, (unsigned)x->raw_seq1);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

static inline int af_meas_fmt_res_json(const af_meas_res_t *x, char *out, size_t cap) {
    am_w_t w;
    am_w_init(&w, out, cap);
    am_w_open(&w);
    am_w_u(&w, "i", x->i);
    am_w_u(&w, "attempt", x->attempt);
    am_w_b(&w, "valid", x->valid);
    if (x->t0_utc == 0) am_w_null(&w, "t0_utc");
    else am_w_u(&w, "t0_utc", x->t0_utc);
    am_w_u(&w, "t0_mono_s", x->t0_mono_s);
    am_w_u(&w, "t_ms", x->t_ms);
    am_w_u(&w, "counts", x->counts);
    am_w_u(&w, "npk", x->npk);
    am_w_u(&w, "lost", x->lost);
    am_w_f(&w, "cps", x->cps);
    am_w_f(&w, "cps_sigma", x->cps_sigma);
    am_w_f(&w, "usv_h", x->usv_h);
    am_w_f(&w, "usv_h_sigma", x->usv_h_sigma);
    am_w_f(&w, "eps_pct", x->eps_pct);
    am_w_s(&w, "stop_reason", af_meas_sr_str(x->stop_reason));
    am_w_b(&w, "partial", (x->flags & AF_MEAS_F_PARTIAL) ? 1 : 0);
    am_w_b(&w, "goal_missed", (x->flags & AF_MEAS_F_GOAL_MISS) ? 1 : 0);
    am_w_b(&w, "time_jump", (x->flags & AF_MEAS_F_TIMEJUMP) ? 1 : 0);
    am_w_b(&w, "final", (x->flags & AF_MEAS_F_FINAL) ? 1 : 0);
    am_w_u(&w, "raw_seq0", x->raw_seq0);
    am_w_u(&w, "raw_seq1", x->raw_seq1);
    am_w_u(&w, "raw_epoch", x->raw_epoch);
    am_w_close(&w);
    return am_w_done(&w);
}

static inline int af_meas_fmt_run_json(const af_meas_run_t *r, char *out, size_t cap) {
    if (!r) return -1;
    am_w_t w;
    am_w_init(&w, out, cap);
    am_w_open(&w);
    am_w_u(&w, "id", r->id);
    am_w_s(&w, "name", r->name);
    am_w_s(&w, "mode", af_meas_mode_str(r->mode));
    am_w_s(&w, "state", af_meas_state_str(r->state));
    am_w_s(&w, "phase", af_meas_phase_str(r->phase));
    am_w_u(&w, "n", r->n);
    am_w_u(&w, "done", r->done_i);
    if (r->state == AF_MEAS_ST_WAIT || r->state == AF_MEAS_ST_RUN || r->state == AF_MEAS_ST_PAUSED)
        am_w_u(&w, "cur_i", r->cur_i);
    else
        am_w_null(&w, "cur_i");
    am_w_u(&w, "t_s", r->t_ms / 1000);
    am_w_u(&w, "pause_s", r->pause_ms / 1000);
    am_w_b(&w, "retry_invalid", r->retry_invalid);
    am_w_u(&w, "lost_max", r->lost_max);
    am_w_s(&w, "stop_mode", af_meas_stopmode_str(r->stop_mode));
    am_w_f(&w, "eps_pct", (double)r->eps_x100 / 100.0);
    am_w_u(&w, "t_min_s", r->t_min_ms / 1000);
    am_w_u(&w, "t_max_s", r->t_max_ms / 1000);
    am_w_s(&w, "start_mode", af_meas_startmode_str(r->start_mode));
    if (r->start_utc == 0) am_w_null(&w, "start_utc");
    else am_w_u(&w, "start_utc", r->start_utc);
    am_w_u(&w, "start_mono_s", r->start_mono_s);
    if (r->state == AF_MEAS_ST_DONE || r->state == AF_MEAS_ST_ABORTED || r->state == AF_MEAS_ST_ERROR)
        am_w_u(&w, "end_mono_s", r->end_mono_s);
    else
        am_w_null(&w, "end_mono_s");
    if (r->state == AF_MEAS_ST_DONE || r->state == AF_MEAS_ST_ABORTED || r->state == AF_MEAS_ST_ERROR)
        am_w_s(&w, "end_reason", af_meas_sr_str(r->end_reason));
    else
        am_w_null(&w, "end_reason");
    am_w_b(&w, "file_store", r->file_store);
    am_w_b(&w, "no_overwrite", r->no_overwrite);
    am_w_s(&w, "prefix", r->prefix);
    am_w_s(&w, "name_template", r->tpl);
    am_w_u(&w, "rows", r->res_n);
    am_w_u(&w, "rows_total", r->rows_total);
    am_w_u(&w, "dropped", r->dropped);
    uint64_t total_s = 0;
    int has_total = 0;
    if (r->n != 0 && r->stop_mode != AF_MEAS_STOP_SIGMA) {
        total_s = (uint64_t)r->n * ((uint64_t)r->t_ms + (uint64_t)r->pause_ms) / 1000;
        has_total = 1;
    }
    if (has_total) am_w_u(&w, "total_s", total_s);
    else am_w_null(&w, "total_s");
    if (r->start_utc != 0 && has_total)
        am_w_u(&w, "expected_end_utc", (uint64_t)r->start_utc + total_s);
    else
        am_w_null(&w, "expected_end_utc");
    am_w_close(&w);
    return am_w_done(&w);
}

static inline int af_meas_fmt_sum_json(const af_meas_sum_t *s, char *out, size_t cap) {
    am_w_t w;
    am_w_init(&w, out, cap);
    am_w_open(&w);
    am_w_u(&w, "rows", s->rows);
    am_w_u(&w, "n_valid", s->n_valid);
    am_w_u(&w, "n_invalid", s->n_invalid);
    am_w_u(&w, "counts", s->counts);
    am_w_f(&w, "live_s", s->live_s);
    am_w_f(&w, "mean_cps", s->mean_cps);
    am_w_f(&w, "sd_cps", s->sd_cps);
    am_w_f(&w, "sem_cps", s->sem_cps);
    am_w_f(&w, "min_cps", s->min_cps);
    am_w_f(&w, "max_cps", s->max_cps);
    am_w_f(&w, "pooled_cps", s->pooled_cps);
    am_w_f(&w, "pooled_sigma", s->pooled_sigma);
    am_w_f(&w, "chi2_ndf", s->chi2_ndf);
    am_w_f(&w, "mean_usvh", s->mean_usvh);
    am_w_f(&w, "dose_usv", s->dose_usv);
    am_w_close(&w);
    return am_w_done(&w);
}

static inline int af_meas_fmt_live_json(const af_meas_live_t *l, char *out, size_t cap) {
    am_w_t w;
    am_w_init(&w, out, cap);
    am_w_open(&w);
    am_w_u(&w, "i", l->i);
    am_w_u(&w, "attempt", l->attempt);
    am_w_s(&w, "phase", af_meas_phase_str(l->phase));
    am_w_u(&w, "elapsed_ms", l->elapsed_ms);
    am_w_u(&w, "remaining_ms", l->remaining_ms);
    am_w_u(&w, "counts", l->counts);
    am_w_u(&w, "npk", l->npk);
    am_w_u(&w, "lost", l->lost);
    am_w_f(&w, "cps", l->cps);
    am_w_f(&w, "cps_sigma", l->cps_sigma);
    am_w_f(&w, "eps_pct", l->eps_pct);
    am_w_f(&w, "usv_h", l->usv_h);
    if (l->n_target == 0) am_w_null(&w, "n_target");
    else am_w_u(&w, "n_target", l->n_target);
    if (l->eta_s < 0) am_w_null(&w, "eta_s");
    else am_w_u(&w, "eta_s", (uint64_t)l->eta_s);
    am_w_close(&w);
    return am_w_done(&w);
}
typedef struct { uint32_t last_total; int init; } af_meas_poll_t;

static inline int af_meas_poll(af_meas_poll_t *s, uint32_t good_total, uint32_t age_ms, uint64_t now_ms, uint32_t *missed, uint64_t *rx_ms) {
    if (s == NULL || missed == NULL || rx_ms == NULL) return 0;
    if (s->init == 0) {
        s->last_total = good_total;
        s->init = 1;
        *missed = 0;
        *rx_ms = 0;
        return 0;
    }
    uint32_t delta = good_total - s->last_total;
    if (delta == 0) {
        *missed = 0;
        *rx_ms = 0;
        return 0;
    }
    s->last_total = good_total;
    *missed = delta - 1;
    *rx_ms = (now_ms > (uint64_t)age_ms) ? (now_ms - (uint64_t)age_ms) : 0;
    return 1;
}

static inline void af_meas_note_missed(af_meas_core_t *c, uint32_t n, uint64_t now_ms) {
    if (c == NULL || n == 0 || c->active < 0) return;
    af_meas_run_t *r = &c->runs[c->active];
    if (r->state != AF_MEAS_ST_RUN || r->phase != AF_MEAS_PH_MEASURING) return;
    r->cur_lost += n;
    if (r->cur_lost > r->lost_max) {
        r->last_rx_ms = now_ms;
        am_close(c, r, now_ms, AF_MEAS_SR_ERROR, 1);
    }
}
#endif /* AF_MEAS_H */
