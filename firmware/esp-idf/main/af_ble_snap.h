#ifndef AF_BLE_SNAP_H
#define AF_BLE_SNAP_H

#include "af_ble_stats.h"
#include <math.h>
#include <string.h>

typedef struct {
    bool connected;
    bool ext;        /* П.10: расширенная статистика включена (лежит в паддинге, размер структуры прежний) */
    uint32_t sess_dur_ms, sess_pkts, sess_bad;
    af_bs_cnt_t boot, life;
    uint32_t fail_streak;
    bool last_reason_valid;
    uint16_t last_reason;
    uint8_t last_reason_group;
    bool have_last;  uint32_t last_dur_ms;
    bool have_ext;   uint32_t longest_ms, shortest_ms;
    uint8_t ring_n;  uint32_t avg_ms, median_ms;
    float up_boot_pct, up_24h_pct;
    float pps_inst, pps_1m, pps_10m, pps_sess;
    float bps_inst, bps_1m, bps_10m, bps_sess;
    int32_t i_last_ms;
    int32_t i_min_ms, i_max_ms;
    float i_avg_ms, jitter_ms;
    uint32_t pauses, pause_max_ms;
    int32_t i10_min_ms, i10_max_ms;
    float i10_avg_ms;
    uint32_t pkts_boot, bad_boot;
    float bad_pct;
    bool have_bad;  uint32_t last_bad_age_ms;
    int32_t last_pkt_age_ms;
    float ci_ms;
    int32_t latency, timeout_ms, mtu, tx_eff;
    int32_t rssi_cur, rssi_min, rssi_avg, rssi_max;
    uint16_t rssi_hist[AF_BS_RSSI_BINS];
    int32_t setup_ms;
    uint32_t scan_n, scan_adv, scan_cand;
    int32_t scan_detect_ms;
    float scan_int_ms, scan_win_ms;
} af_bs_snap_t;

static inline uint64_t af_bs_win_ms(uint64_t now_ms, uint32_t k, uint32_t b) {
    if (k == 0) return 0;
    uint64_t full = (uint64_t)(k - 1) * b;
    uint64_t rem = now_ms % b;
    uint64_t w = full + rem;
    return (w < now_ms) ? w : now_ms;
}

static inline void af_bs_snapshot(const af_bs_t *s, uint64_t now_ms, af_bs_snap_t *o) {
    memset(o, 0, sizeof(*o));
    
    o->connected = s->connected;
    o->ext = s->ext;

    // Сессия
    if (s->connected) {
        uint64_t dur = (now_ms > s->start_ms) ? (now_ms - s->start_ms) : 0;
        o->sess_dur_ms = (dur > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)dur;
        o->sess_pkts = s->pkts;
        o->sess_bad = s->bad;
    }
    
    // Счётчики
    o->boot = s->boot;
    o->life = s->life;
    o->fail_streak = s->fail_streak;
    o->last_reason_valid = s->last_reason_valid;
    o->last_reason = s->last_reason;
    o->last_reason_group = s->last_reason_valid ? af_bs_reason_group(s->last_reason) : 0;
    
    // Сессии (последняя, экстремумы)
    o->have_last = s->has_dur;
    o->last_dur_ms = s->dur_last;
    o->have_ext = s->has_dur;
    o->longest_ms = s->dur_max;
    o->shortest_ms = s->dur_min;
    
    // Кольцо сессий
    uint8_t rn = (s->ring != NULL) ? s->ring_n : 0;
    o->ring_n = rn;
    if (rn > 0 && s->ring != NULL) {
        uint64_t sum = 0;
        uint32_t arr[AF_BS_RING_N];
        for (uint8_t i = 0; i < rn; i++) {
            uint8_t idx = (uint8_t)((s->ring_head + AF_BS_RING_N - rn + i) % AF_BS_RING_N);
            sum += s->ring[idx].dur_ms;
            arr[i] = s->ring[idx].dur_ms;
        }
        o->avg_ms = (uint32_t)(sum / rn);
        
        // Сортировка вставками
        for (uint8_t i = 1; i < rn; i++) {
            uint32_t key = arr[i];
            uint8_t j = i;
            while (j > 0 && arr[j-1] > key) {
                arr[j] = arr[j-1];
                j--;
            }
            arr[j] = key;
        }
        
        if (rn % 2 == 1) {
            o->median_ms = arr[rn / 2];
        } else {
            o->median_ms = (uint32_t)(((uint64_t)arr[rn/2 - 1] + arr[rn/2]) / 2);
        }
    }
    
    // Время на связи
    if (now_ms > 0) {
        float pct = 100.0f * (float)s->boot.conn_ms / (float)now_ms;
        o->up_boot_pct = (pct > 100.0f) ? 100.0f : pct;
    } else {
        o->up_boot_pct = 0.0f;
    }
    
    uint64_t win_24h = af_bs_win_ms(now_ms, AF_BS_HOUR_N, AF_BS_HOUR_MS);
    if (!s->ext) {
        o->up_24h_pct = NAN;   /* b29 B04: часовые корзины при ext=0 не ведутся - не число, а нет данных */
    } else if (win_24h > 0) {
        uint64_t sum_h = 0;
        for (uint8_t i = 0; i < AF_BS_HOUR_N; i++) sum_h += s->hour_ms[i];
        float pct = 100.0f * (float)sum_h / (float)win_24h;
        o->up_24h_pct = (pct > 100.0f) ? 100.0f : pct;
    } else {
        o->up_24h_pct = 0.0f;
    }
    
    // Скорости (PPS/BPS)
    if (s->win_init) {
        // pps_inst: 10 сек
        uint32_t sum_sec = 0;
        for (uint8_t i = 0; i < AF_BS_SEC_N; i++) sum_sec += s->sec[i];
        uint64_t w_inst = af_bs_win_ms(now_ms, AF_BS_SEC_N, AF_BS_SEC_MS);
        o->pps_inst = (w_inst > 0) ? (float)sum_sec * 1000.0f / (float)w_inst : NAN;
        o->bps_inst = o->pps_inst * AF_BS_PKT_BYTES;
        
        // pps_1m: 6 dec (последние 6)
        uint32_t sum_dec_1m = 0;
        uint64_t idx_dec = s->dec_idx;
        for (uint8_t i = 0; i < 6; i++) {
            uint8_t idx = (idx_dec + AF_BS_DEC_N - i) % AF_BS_DEC_N;
            sum_dec_1m += s->dec[idx].pk;
        }
        uint64_t w_1m = af_bs_win_ms(now_ms, 6, AF_BS_DEC_MS);
        o->pps_1m = (w_1m > 0) ? (float)sum_dec_1m * 1000.0f / (float)w_1m : NAN;
        o->bps_1m = o->pps_1m * AF_BS_PKT_BYTES;
        
        // pps_10m: 60 dec (все)
        uint32_t sum_dec_10m = 0;
        for (uint8_t i = 0; i < AF_BS_DEC_N; i++) sum_dec_10m += s->dec[i].pk;
        uint64_t w_10m = af_bs_win_ms(now_ms, AF_BS_DEC_N, AF_BS_DEC_MS);
        o->pps_10m = (w_10m > 0) ? (float)sum_dec_10m * 1000.0f / (float)w_10m : NAN;
        o->bps_10m = o->pps_10m * AF_BS_PKT_BYTES;
    } else {
        o->pps_inst = NAN; o->bps_inst = NAN;
        o->pps_1m = NAN; o->bps_1m = NAN;
        o->pps_10m = NAN; o->bps_10m = NAN;
    }
    
    // pps_sess
    if (s->connected && now_ms > s->start_ms) {
        uint64_t dur = now_ms - s->start_ms;
        o->pps_sess = (float)s->pkts * 1000.0f / (float)dur;
        o->bps_sess = o->pps_sess * AF_BS_PKT_BYTES;
    } else {
        o->pps_sess = NAN;
        o->bps_sess = NAN;
    }
    
    // Интервалы за сессию
    if (s->connected && s->i_n > 0) {
        o->i_last_ms = (int32_t)s->i_last;
        o->i_min_ms = (int32_t)s->i_min;
        o->i_max_ms = (int32_t)s->i_max;
        o->i_avg_ms = (float)s->i_sum / (float)s->i_n;
        o->pauses = s->pauses;
        o->pause_max_ms = s->i_max;
        
        if (s->i_n >= 2) {
            float mean = (float)s->i_sum / (float)s->i_n;
            float var = (float)s->i_sumsq / (float)s->i_n - mean * mean;
            if (var < 0.0f) var = 0.0f;
            o->jitter_ms = sqrtf(var);
        } else {
            o->jitter_ms = NAN;
        }
    } else {
        o->i_last_ms = AF_BS_ND;
        o->i_min_ms = AF_BS_ND;
        o->i_max_ms = AF_BS_ND;
        o->i_avg_ms = NAN;
        o->jitter_ms = NAN;
        o->pauses = 0;
        o->pause_max_ms = 0;
    }
    
    // Интервалы за 10 мин
    uint32_t n_10m = 0, s_10m = 0;
    uint16_t min_10m = 0xFFFF, max_10m = 0;
    bool has_10m = false;
    for (uint8_t i = 0; i < AF_BS_DEC_N; i++) {
        if (s->dec[i].n > 0) {
            has_10m = true;
            n_10m += s->dec[i].n;
            s_10m += s->dec[i].isum;
            if (s->dec[i].imin < min_10m) min_10m = s->dec[i].imin;
            if (s->dec[i].imax > max_10m) max_10m = s->dec[i].imax;
        }
    }
    if (has_10m && n_10m > 0) {
        o->i10_min_ms = (int32_t)min_10m;
        o->i10_max_ms = (int32_t)max_10m;
        o->i10_avg_ms = (float)s_10m / (float)n_10m;
    } else {
        o->i10_min_ms = AF_BS_ND;
        o->i10_max_ms = AF_BS_ND;
        o->i10_avg_ms = NAN;
    }
    
    // Плохие пакеты
    o->pkts_boot = s->pkts_boot;
    o->bad_boot = s->bad_boot;
    o->bad_pct = (s->pkts_boot > 0) ? (float)s->bad_boot * 100.0f / (float)s->pkts_boot : NAN;
    o->have_bad = s->has_bad;
    if (s->has_bad) {
        if (now_ms >= s->last_bad_ms) {
            uint64_t age = now_ms - s->last_bad_ms;
            o->last_bad_age_ms = (age > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)age;
        } else {
            o->last_bad_age_ms = 0;
        }
    } else {
        o->last_bad_age_ms = 0;
    }
    
    // Последний пакет
    if (s->connected && s->has_pkt) {
        if (now_ms >= s->last_pkt_ms) {
            uint64_t age = now_ms - s->last_pkt_ms;
            o->last_pkt_age_ms = (age > 0x7FFFFFFFULL) ? 0x7FFFFFFF : (int32_t)age;
        } else {
            o->last_pkt_age_ms = 0;
        }
    } else {
        o->last_pkt_age_ms = AF_BS_ND;
    }
    
    // Параметры соединения
    if (s->connected) {
        o->ci_ms = s->ci_x125 ? s->ci_x125 * 1.25f : NAN;
        o->latency = s->ci_x125 ? (int32_t)s->ci_latency : AF_BS_ND;
        o->timeout_ms = s->ci_timeout_x10 ? (int32_t)(s->ci_timeout_x10 * 10) : AF_BS_ND;
        o->mtu = s->mtu ? (int32_t)s->mtu : AF_BS_ND;
        o->tx_eff = (s->tx_eff >= 0) ? (int32_t)s->tx_eff : AF_BS_ND;
    } else {
        o->ci_ms = NAN;
        o->latency = AF_BS_ND;
        o->timeout_ms = AF_BS_ND;
        o->mtu = AF_BS_ND;
        o->tx_eff = AF_BS_ND;
    }
    
    // RSSI: текущий - базовый (есть и при ext=0); min/avg/max/hist - расширенные (rssi_n растёт только при ext)
    o->rssi_cur = (s->connected && s->rssi_cur != AF_BS_RSSI_ND) ? (int32_t)s->rssi_cur : AF_BS_RSSI_ND;
    if (s->connected && s->rssi_n > 0) {
        o->rssi_cur = (int32_t)s->rssi_cur;
        o->rssi_min = (int32_t)s->rssi_min;
        o->rssi_max = (int32_t)s->rssi_max;
        // Симметричное округление
        int32_t rn_ = (int32_t)s->rssi_n;
        int32_t avg = (s->rssi_sum >= 0) ? (s->rssi_sum + rn_ / 2) / rn_ : (s->rssi_sum - rn_ / 2) / rn_;
        o->rssi_avg = avg;
        memcpy(o->rssi_hist, s->rssi_hist, sizeof(o->rssi_hist));
    } else {
        o->rssi_min = AF_BS_RSSI_ND;
        o->rssi_max = AF_BS_RSSI_ND;
        o->rssi_avg = AF_BS_RSSI_ND;
        memset(o->rssi_hist, 0, sizeof(o->rssi_hist));
    }
    
    // Setup
    o->setup_ms = (s->setup_ms > 0) ? (int32_t)s->setup_ms : AF_BS_ND;
    
    // Скан
    o->scan_n = s->scan_n;
    o->scan_adv = s->scan_adv;
    o->scan_cand = s->scan_cand;
    o->scan_detect_ms = s->scan_hit ? (int32_t)s->scan_detect_ms : AF_BS_ND;
    o->scan_int_ms = s->scan_int_x625 ? s->scan_int_x625 * 0.625f : NAN;
    o->scan_win_ms = s->scan_win_x625 ? s->scan_win_x625 * 0.625f : NAN;
}

#endif
