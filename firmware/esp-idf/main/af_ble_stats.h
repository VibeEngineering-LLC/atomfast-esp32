#ifndef AF_BLE_STATS_H
#define AF_BLE_STATS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define AF_BS_RING_N         32
#define AF_BS_SEC_N          10      /* корзины по 1 с, окно 10 с */
#define AF_BS_DEC_N          60      /* корзины по 10 с, окно 10 мин */
#define AF_BS_HOUR_N         24      /* корзины по 1 ч, окно 24 ч */
#define AF_BS_SEC_MS         1000u
#define AF_BS_DEC_MS         10000u
#define AF_BS_HOUR_MS        3600000u
#define AF_BS_NOMINAL_MS     2000u   /* номинальный интервал пакетов */
#define AF_BS_PAUSE_MUL      3u      /* пауза = интервал > 3 * номинал */
#define AF_BS_PKT_BYTES      13u
#define AF_BS_RSSI_BINS      8
#define AF_BS_RSSI_ND        127     /* RSSI «н/д» (так в спецификации BT) */
#define AF_BS_REASONS        6
#define AF_BS_NVS_PERIOD_MS  600000u /* запись NVS не чаще раза в 10 мин */
#define AF_BS_NVS_MIN_GAP_MS 30000u  /* при отключении - не чаще раза в 30 с (износ флеша) */
#define AF_BS_ND             (-1)    /* «н/д» для целых неотрицательных величин */

enum { AF_BS_R_OTHER = 0, AF_BS_R_TIMEOUT, AF_BS_R_REMOTE, AF_BS_R_LOCAL, AF_BS_R_ESTABLISH, AF_BS_R_L2CAP };

typedef struct {            /* запись журнала сессий (завершённая сессия), 28 байт */
    uint32_t end_utc;       /* UTC конца, 0 если время не известно */
    uint32_t dur_ms;
    uint32_t pkts;
    uint32_t bad;
    uint32_t setup_ms;      /* от начала попытки до connected, 0 = н/д */
    uint16_t reason;        /* код разрыва GAP/HCI */
    int8_t rssi_min, rssi_avg, rssi_max;   /* AF_BS_RSSI_ND если замеров не было */
    uint8_t pad[3];
} af_bs_sess_t;

typedef struct {            /* накопительные счётчики */
    uint32_t connects, disconnects, attempts, fails;
    uint32_t reason[AF_BS_REASONS];       /* по группам причин (разрывы и неудачные попытки) */
    uint64_t conn_ms;                     /* суммарное время на связи */
} af_bs_cnt_t;

typedef struct {            /* корзина 10 с */
    uint16_t pk;            /* пакетов (насыщение 65535) */
    uint16_t n;             /* интервалов между пакетами (насыщение) */
    uint16_t isum;          /* сумма интервалов, мс (насыщение 65535) */
    uint16_t imin;          /* минимум интервала, мс (0xFFFF = нет) */
    uint16_t imax;          /* максимум интервала, мс */
} af_bs_dec_t;

typedef struct {
    bool connected;                 /* идёт сессия */
    bool dirty, disc_pending;       /* NVS: есть что писать / было отключение */
    bool last_reason_valid;
    bool local_close;               /* разрыв инициирован платой (esp_ble_gattc_close), снимается при учёте разрыва */
    bool ext;                       /* расширенная статистика (П.10): по умолчанию false; ставить только af_bs_set_ext(); лежит в паддинге, sizeof не растёт */
    uint16_t last_reason;
    uint32_t fail_streak;           /* подряд неудачных попыток */
    uint64_t acc_ms;                /* момент последнего учёта времени на связи */
    uint64_t last_save_ms;          /* момент последней записи в NVS */
    bool attempt_on;
    uint64_t attempt_ms;            /* начало последней попытки подключения */
    /* текущая сессия (сбрасывается в on_connect) */
    uint64_t start_ms;
    uint32_t pkts, bad;
    bool has_pkt;
    uint64_t last_pkt_ms;
    uint32_t setup_ms;
    uint32_t i_last, i_min, i_max, i_n, pauses;
    uint64_t i_sum, i_sumsq;
    int32_t rssi_sum;
    uint32_t rssi_n;
    int8_t rssi_cur, rssi_min, rssi_max;
    uint16_t rssi_hist[AF_BS_RSSI_BINS];
    uint16_t ci_x125;               /* интервал соединения в единицах 1.25 мс, 0 = н/д */
    uint16_t ci_latency;
    uint16_t ci_timeout_x10;        /* supervision timeout в единицах 10 мс, 0 = н/д */
    uint16_t mtu;                   /* 0 = н/д */
    int8_t tx_eff;                  /* фактический уровень мощности (код), -1 = н/д */
    /* с загрузки */
    uint32_t pkts_boot, bad_boot;
    bool has_bad;
    uint64_t last_bad_ms;
    bool has_dur;
    uint32_t dur_last, dur_min, dur_max;
    af_bs_cnt_t boot;               /* с загрузки */
    af_bs_cnt_t life;               /* накопительные (база из NVS + события с загрузки) */
    /* сканирование (последний скан) */
    bool scan_on, scan_hit;
    uint64_t scan_start_ms;
    uint32_t scan_n, scan_adv, scan_cand;
    uint32_t scan_detect_ms;
    uint16_t scan_int_x625, scan_win_x625;   /* в единицах 0.625 мс, 0 = н/д */
    /* окна */
    bool win_init;
    uint64_t sec_idx, dec_idx, hour_idx;     /* now_ms/1000, now_ms/10000, now_ms/3600000 */
    uint8_t sec[AF_BS_SEC_N];                /* пакетов в секунду (насыщение 255) */
    af_bs_dec_t dec[AF_BS_DEC_N];
    uint32_t hour_ms[AF_BS_HOUR_N];          /* мс на связи в часовой корзине */
    /* кольцо завершённых сессий: память отдаёт вызывающий (PSRAM) */
    af_bs_sess_t *ring;
    uint8_t ring_n, ring_head;               /* ring_head - индекс СЛЕДУЮЩЕЙ записи */
} af_bs_t;

_Static_assert(sizeof(af_bs_t) <= 2048, "af_bs_t должен занимать не более 2 КБ внутренней RAM");

static inline uint8_t af_bs_reason_group(uint16_t code) {
    switch (code) {
        case 0x08: case 0x22: return AF_BS_R_TIMEOUT;
        case 0x13: case 0x14: case 0x15: return AF_BS_R_REMOTE;
        case 0x16: case 0x100: return AF_BS_R_LOCAL;
        case 0x3E: return AF_BS_R_ESTABLISH;
        case 0x01: return AF_BS_R_L2CAP;
        default: return AF_BS_R_OTHER;
    }
}

static inline const char *af_bs_reason_text(uint16_t code) {
    switch (code) {
        case 0x08: return "supervision timeout";
        case 0x13: return "remote terminated";
        case 0x14: return "remote low resources";
        case 0x15: return "remote power off";
        case 0x16: return "local terminated";
        case 0x22: return "LL response timeout";
        case 0x3E: return "conn failed to establish";
        case 0x100: return "connection cancelled";
        case 0x01: return "L2CAP failure";
        default: return "other";
    }
}

static inline const char *af_bs_group_text(uint8_t g) {
    if (g >= AF_BS_REASONS) return "other";
    switch (g) {
        case AF_BS_R_TIMEOUT: return "timeout";
        case AF_BS_R_REMOTE: return "remote";
        case AF_BS_R_LOCAL: return "local";
        case AF_BS_R_ESTABLISH: return "establish";
        case AF_BS_R_L2CAP: return "l2cap";
        default: return "other";
    }
}

static inline void af_bs_init(af_bs_t *s, af_bs_sess_t *ring_mem) {
    memset(s, 0, sizeof(*s));
    s->ring = ring_mem;
    s->tx_eff = -1;
    s->dur_min = 0xFFFFFFFFu;
    s->i_min = 0xFFFFFFFFu;
    s->rssi_cur = AF_BS_RSSI_ND;
    s->rssi_min = AF_BS_RSSI_ND;
    s->rssi_max = AF_BS_RSSI_ND;
    for (int k = 0; k < AF_BS_DEC_N; k++) {
        s->dec[k].imin = 0xFFFF;
    }
    if (ring_mem != NULL) {
        memset(ring_mem, 0, sizeof(af_bs_sess_t) * AF_BS_RING_N);
    }
}

static inline void af_bs_ext_reset(af_bs_t *s)
{
    /* окна */
    s->win_init = false;
    s->sec_idx = 0;
    s->dec_idx = 0;
    s->hour_idx = 0;
    memset(s->sec, 0, sizeof(s->sec));
    memset(s->hour_ms, 0, sizeof(s->hour_ms));
    for (size_t k = 0; k < AF_BS_DEC_N; k++) {
        s->dec[k].pk = 0;
        s->dec[k].n = 0;
        s->dec[k].isum = 0;
        s->dec[k].imax = 0;
        s->dec[k].imin = 0xFFFF;
    }

    /* RSSI-агрегаты (rssi_cur — базовое, не трогаем) */
    s->rssi_sum = 0;
    s->rssi_n = 0;
    s->rssi_min = AF_BS_RSSI_ND;
    s->rssi_max = AF_BS_RSSI_ND;
    memset(s->rssi_hist, 0, sizeof(s->rssi_hist));

    /* интервалы */
    s->i_last = 0;
    s->i_min = 0xFFFFFFFFu;
    s->i_max = 0;
    s->i_n = 0;
    s->pauses = 0;
    s->i_sum = 0;
    s->i_sumsq = 0;

    /* длительность */
    s->has_dur = false;
    s->dur_last = 0;
    s->dur_min = 0xFFFFFFFFu;
    s->dur_max = 0;

    /* кольцевой буфер сессий */
    s->ring_n = 0;
    s->ring_head = 0;
    if (s->ring != NULL) {
        memset(s->ring, 0, sizeof(af_bs_sess_t) * AF_BS_RING_N);
    }

    /* сканирование (scan_int_x625 / scan_win_x625 — настройка, не трогаем) */
    s->scan_on = false;
    s->scan_hit = false;
    s->scan_start_ms = 0;
    s->scan_n = 0;
    s->scan_adv = 0;
    s->scan_cand = 0;
    s->scan_detect_ms = 0;
}

static inline void af_bs_set_ext(af_bs_t *s, bool on)
{
    if (on == s->ext) {
        return;
    }
    s->ext = on;
    af_bs_ext_reset(s);
}
static inline void af_bs_advance(af_bs_t *s, uint64_t now_ms) {
    if (!s->ext) return;    /* окна sec/dec/hour - расширенная часть */
    if (!s->win_init) {
        s->sec_idx = now_ms / AF_BS_SEC_MS;
        s->dec_idx = now_ms / AF_BS_DEC_MS;
        s->hour_idx = now_ms / AF_BS_HOUR_MS;
        s->win_init = true;
        return;
    }

    /* Секундные корзины */
    uint64_t ni_sec = now_ms / AF_BS_SEC_MS;
    if (ni_sec > s->sec_idx) {
        uint64_t delta = ni_sec - s->sec_idx;
        if (delta >= AF_BS_SEC_N) {
            memset(s->sec, 0, sizeof(s->sec));
        } else {
            for (uint64_t i = 1; i <= delta; i++) {
                s->sec[(s->sec_idx + i) % AF_BS_SEC_N] = 0;
            }
        }
        s->sec_idx = ni_sec;
    }

    /* Десятисекундные корзины */
    uint64_t ni_dec = now_ms / AF_BS_DEC_MS;
    if (ni_dec > s->dec_idx) {
        uint64_t delta = ni_dec - s->dec_idx;
        if (delta >= AF_BS_DEC_N) {
            for (int k = 0; k < AF_BS_DEC_N; k++) {
                s->dec[k].pk = 0;
                s->dec[k].n = 0;
                s->dec[k].isum = 0;
                s->dec[k].imin = 0xFFFF;
                s->dec[k].imax = 0;
            }
        } else {
            for (uint64_t i = 1; i <= delta; i++) {
                int idx = (int)((s->dec_idx + i) % AF_BS_DEC_N);
                s->dec[idx].pk = 0;
                s->dec[idx].n = 0;
                s->dec[idx].isum = 0;
                s->dec[idx].imin = 0xFFFF;
                s->dec[idx].imax = 0;
            }
        }
        s->dec_idx = ni_dec;
    }

    /* Часовые корзины */
    uint64_t ni_hour = now_ms / AF_BS_HOUR_MS;
    if (ni_hour > s->hour_idx) {
        uint64_t delta = ni_hour - s->hour_idx;
        if (delta >= AF_BS_HOUR_N) {
            memset(s->hour_ms, 0, sizeof(s->hour_ms));
        } else {
            for (uint64_t i = 1; i <= delta; i++) {
                s->hour_ms[(s->hour_idx + i) % AF_BS_HOUR_N] = 0;
            }
        }
        s->hour_idx = ni_hour;
    }
}

static inline void af_bs_acc(af_bs_t *s, uint64_t now_ms) {
    if (now_ms <= s->acc_ms) {
        return;
    }
    if (s->connected) {
        uint64_t delta = now_ms - s->acc_ms;
        s->boot.conn_ms += delta;
        s->life.conn_ms += delta;
        if (!s->ext) {      /* часовые корзины hour_ms - расширенная часть */
            s->acc_ms = now_ms;
            return;
        }

        /* Разбиваем интервал [acc_ms, now_ms) по часовым корзинам */
        uint64_t start = s->acc_ms;
        uint64_t end = now_ms;
        
        /* Если delta больше 24 часов, учитываем только последние 24 часа */
        if (delta > (uint64_t)AF_BS_HOUR_N * AF_BS_HOUR_MS) {
            start = end - (uint64_t)AF_BS_HOUR_N * AF_BS_HOUR_MS;
        }

        {   /* не писать в часы старше окна 24 ч: их корзины уже заняты текущими часами */
            uint64_t oldest = (s->hour_idx >= AF_BS_HOUR_N - 1) ? (s->hour_idx - (AF_BS_HOUR_N - 1)) * AF_BS_HOUR_MS : 0;
            if (start < oldest) start = oldest;
        }
        uint64_t cur = start;
        while (cur < end) {
            uint64_t hour_idx = cur / AF_BS_HOUR_MS;
            uint64_t hour_end = (hour_idx + 1) * AF_BS_HOUR_MS;
            uint64_t chunk_end = (end < hour_end) ? end : hour_end;
            uint32_t chunk_ms = (uint32_t)(chunk_end - cur);
            
            uint8_t bin = (uint8_t)(hour_idx % AF_BS_HOUR_N);
            s->hour_ms[bin] += chunk_ms;
            
            cur = chunk_end;
        }
    }
    s->acc_ms = now_ms;
}

static inline void af_bs_tick(af_bs_t *s, uint64_t now_ms) {
    af_bs_advance(s, now_ms);
    af_bs_acc(s, now_ms);
}

static inline void af_bs_on_attempt(af_bs_t *s, uint64_t now_ms) {
    s->boot.attempts++;
    s->life.attempts++;
    s->attempt_on = true;
    s->attempt_ms = now_ms;
    s->dirty = true;
}

static inline void af_bs_on_open_fail(af_bs_t *s, uint64_t now_ms, uint16_t reason) {
    af_bs_tick(s, now_ms);
    s->boot.fails++;
    s->life.fails++;
    s->fail_streak++;
    s->attempt_on = false;
    s->last_reason = reason;
    s->last_reason_valid = true;
    uint8_t g = af_bs_reason_group(reason);
    s->boot.reason[g]++;
    s->life.reason[g]++;
    s->dirty = true;
}

static inline void af_bs_on_connect(af_bs_t *s, uint64_t now_ms, uint16_t mtu) {
    if (s->connected) return;
    
    af_bs_tick(s, now_ms);
    
    s->connected = true;
    s->start_ms = now_ms;
    
    /* Сброс полей текущей сессии */
    s->pkts = 0;
    s->bad = 0;
    s->has_pkt = false;
    s->last_pkt_ms = 0;
    s->i_last = 0;
    s->i_min = 0xFFFFFFFFu;
    s->i_max = 0;
    s->i_n = 0;
    s->pauses = 0;
    s->i_sum = 0;
    s->i_sumsq = 0;
    s->rssi_sum = 0;
    s->rssi_n = 0;
    s->rssi_cur = AF_BS_RSSI_ND;
    s->rssi_min = AF_BS_RSSI_ND;
    s->rssi_max = AF_BS_RSSI_ND;
    memset(s->rssi_hist, 0, sizeof(s->rssi_hist));
    s->ci_x125 = 0;
    s->ci_latency = 0;
    s->ci_timeout_x10 = 0;
    s->mtu = mtu;
    s->tx_eff = -1;
    
    s->setup_ms = s->attempt_on ? (uint32_t)(now_ms - s->attempt_ms) : 0;
    s->attempt_on = false;
    
    s->boot.connects++;
    s->life.connects++;
    s->fail_streak = 0;
    s->dirty = true;
}

static inline void af_bs_set_conn_params(af_bs_t *s, uint16_t interval_x125, uint16_t latency, uint16_t timeout_x10) {
    if (s->connected) {
        s->ci_x125 = interval_x125;
        s->ci_latency = latency;
        s->ci_timeout_x10 = timeout_x10;
    }
}

static inline void af_bs_set_mtu(af_bs_t *s, uint16_t mtu) {
    if (s->connected) {
        s->mtu = mtu;
    }
}

static inline void af_bs_set_tx(af_bs_t *s, int8_t eff) {
    if (s->connected) {
        s->tx_eff = eff;
    }
}

static inline void af_bs_on_packet(af_bs_t *s, uint64_t now_ms, bool ok) {
    if (!s->connected) return;
    
    /* П.10: при ext=0 - только базовые счётчики */
    if (!s->ext) {
        s->pkts++;
        s->pkts_boot++;
        
        if (!ok) {
            s->bad++;
            s->bad_boot++;
            s->has_bad = true;
            s->last_bad_ms = now_ms;
        }
        
        s->has_pkt = true;
        s->last_pkt_ms = now_ms;
        return;
    }
    
    af_bs_advance(s, now_ms);
    
    s->pkts++;
    s->pkts_boot++;
    
    if (!ok) {
        s->bad++;
        s->bad_boot++;
        s->has_bad = true;
        s->last_bad_ms = now_ms;
    }
    
    /* Секундная корзина */
    uint8_t sec_bin = (uint8_t)((now_ms / AF_BS_SEC_MS) % AF_BS_SEC_N);
    if (s->sec[sec_bin] < 255) s->sec[sec_bin]++;
    
    /* Десятисекундная корзина */
    uint8_t dec_bin = (uint8_t)((now_ms / AF_BS_DEC_MS) % AF_BS_DEC_N);
    if (s->dec[dec_bin].pk < 0xFFFF) s->dec[dec_bin].pk++;
    
    /* Интервалы */
    if (s->has_pkt) {
        uint64_t iv64 = (now_ms > s->last_pkt_ms) ? (now_ms - s->last_pkt_ms) : 0;
        uint32_t iv = (iv64 > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)iv64;
        
        s->i_last = iv;
        s->i_n++;
        s->i_sum += iv;
        s->i_sumsq += (uint64_t)iv * iv;
        
        if (iv < s->i_min) s->i_min = iv;
        if (iv > s->i_max) s->i_max = iv;
        
        if (iv > AF_BS_PAUSE_MUL * AF_BS_NOMINAL_MS) {
            s->pauses++;
        }
        
        /* Корзина dec: интервалы */
        if (s->dec[dec_bin].n < 0xFFFF) s->dec[dec_bin].n++;
        
        uint16_t v16 = (iv > 0xFFFF) ? 0xFFFF : (uint16_t)iv;
        
        {
            uint32_t ns = (uint32_t)s->dec[dec_bin].isum + v16;
            s->dec[dec_bin].isum = (ns > 0xFFFFu) ? 0xFFFFu : (uint16_t)ns;
        }
        
        if (s->dec[dec_bin].imin == 0xFFFF || v16 < s->dec[dec_bin].imin) {
            s->dec[dec_bin].imin = v16;
        }
        if (v16 > s->dec[dec_bin].imax) {
            s->dec[dec_bin].imax = v16;
        }
    }
    
    s->has_pkt = true;
    s->last_pkt_ms = now_ms;
}
static inline void af_bs_on_rssi(af_bs_t *s, int8_t rssi) {
    if (!s->connected) return;
    if (rssi == AF_BS_RSSI_ND) return;
    if (rssi > 20) return;
    
    s->rssi_cur = rssi;
    if (!s->ext) return;    /* сумма, min/max и гистограмма - расширенная часть */
    s->rssi_sum += rssi;
    s->rssi_n++;
    
    if (s->rssi_n == 1 || rssi < s->rssi_min) {
        s->rssi_min = rssi;
    }
    if (s->rssi_n == 1 || rssi > s->rssi_max) {
        s->rssi_max = rssi;
    }
    
    /* Гистограмма: 8 корзин по 10 дБ, от -40 до -120 (и ниже) */
    uint8_t b;
    if (rssi > -40) {
        b = 0;
    } else {
        int32_t val = ((int32_t)(-rssi) - 40) / 10 + 1;
        if (val > 7) val = 7;
        b = (uint8_t)val;
    }
    
    if (s->rssi_hist[b] < 0xFFFF) s->rssi_hist[b]++;
}

/* Плата сама закрывает соединение: стек в этом случае отдаёт код 0, а не 0x16. Взводится только на живой сессии. */
static inline void af_bs_mark_local_close(af_bs_t *s) {
    if (s->connected) s->local_close = true;
}

/* Расширенная часть закрытия сессии (длительности + кольцо); вызывается только при ext */
static inline void af_bs_sess_close(af_bs_t *s, uint32_t dur, uint32_t utc_s, uint16_t reason)
{
    s->dur_last = dur;
    s->has_dur = true;
    if (dur < s->dur_min) s->dur_min = dur;
    if (dur > s->dur_max) s->dur_max = dur;

    /* Формируем запись журнала */
    af_bs_sess_t rec;
    rec.end_utc = utc_s;
    rec.dur_ms = dur;
    rec.pkts = s->pkts;
    rec.bad = s->bad;
    rec.setup_ms = s->setup_ms;
    rec.reason = reason;

    if (s->rssi_n == 0) {
        rec.rssi_min = AF_BS_RSSI_ND;
        rec.rssi_avg = AF_BS_RSSI_ND;
        rec.rssi_max = AF_BS_RSSI_ND;
    } else {
        rec.rssi_min = s->rssi_min;
        rec.rssi_max = s->rssi_max;
        /* Округление к ближайшему целому (симметрично) */
        int32_t sum = s->rssi_sum;
        int32_t n = (int32_t)s->rssi_n;
        int32_t avg;
        if (sum >= 0) {
            avg = (sum + n / 2) / n;
        } else {
            avg = (sum - n / 2) / n;
        }
        rec.rssi_avg = (int8_t)avg;
    }
    rec.pad[0] = 0;
    rec.pad[1] = 0;
    rec.pad[2] = 0;

    /* Запись в кольцо */
    if (s->ring != NULL) {
        s->ring[s->ring_head] = rec;
        s->ring_head = (uint8_t)((s->ring_head + 1) % AF_BS_RING_N);
        if (s->ring_n < AF_BS_RING_N) {
            s->ring_n++;
        }
    }
}
static inline void af_bs_on_disconnect(af_bs_t *s, uint64_t now_ms, uint32_t utc_s, uint16_t reason) {
    if (!s->connected) return;
    if (s->local_close && reason == 0) reason = 0x16;
    s->local_close = false;
    
    af_bs_advance(s, now_ms);
    af_bs_acc(s, now_ms);
    
    uint64_t dur64 = (now_ms > s->start_ms) ? (now_ms - s->start_ms) : 0;
    uint32_t dur = (dur64 > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)dur64;
    
    s->connected = false;
    s->boot.disconnects++;
    s->life.disconnects++;
    
    s->last_reason = reason;
    s->last_reason_valid = true;
    
    uint8_t g = af_bs_reason_group(reason);
    s->boot.reason[g]++;
    s->life.reason[g]++;
    
    if (s->ext) af_bs_sess_close(s, dur, utc_s, reason);
    
    /* Очистка параметров соединения */
    s->ci_x125 = 0;
    s->ci_latency = 0;
    s->ci_timeout_x10 = 0;
    s->mtu = 0;
    s->tx_eff = -1;
    s->rssi_cur = AF_BS_RSSI_ND;
    
    s->dirty = true;
    s->disc_pending = true;
}

static inline const af_bs_sess_t *af_bs_ring_get(const af_bs_t *s, uint8_t i) {
    if (s->ring == NULL) return NULL;
    if (i >= s->ring_n) return NULL;
    uint8_t idx = (uint8_t)((s->ring_head + AF_BS_RING_N - 1 - i) % AF_BS_RING_N);
    return &s->ring[idx];
}

static inline void af_bs_scan_params(af_bs_t *s, uint16_t int_x625, uint16_t win_x625) {
    s->scan_int_x625 = int_x625;
    s->scan_win_x625 = win_x625;
}

static inline void af_bs_on_scan_start(af_bs_t *s, uint64_t now_ms) {
    if (!s->ext) return;    /* детали сканирования - расширенная часть; adv/hit/stop защищены scan_on */
    s->scan_n++;
    s->scan_on = true;
    s->scan_hit = false;
    s->scan_start_ms = now_ms;
    s->scan_adv = 0;
    s->scan_cand = 0;
    s->scan_detect_ms = 0;
}

static inline void af_bs_on_scan_adv(af_bs_t *s) {
    if (s->scan_on) {
        s->scan_adv++;
    }
}

static inline void af_bs_on_scan_hit(af_bs_t *s, uint64_t now_ms) {
    if (s->scan_on) {
        s->scan_cand++;
        if (!s->scan_hit) {
            s->scan_hit = true;
            s->scan_detect_ms = (uint32_t)(now_ms - s->scan_start_ms);
        }
    }
}

static inline void af_bs_on_scan_stop(af_bs_t *s) {
    s->scan_on = false;
}

#endif
