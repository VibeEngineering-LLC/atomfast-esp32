#include "ble_gw_stats.h"
#include "af_net.h"
#include "net_time.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Прототипы публичных функций */
esp_err_t af_ble_http_stats(httpd_req_t *req);
esp_err_t af_ble_http_sessions(httpd_req_t *req);
esp_err_t af_ble_http_sessions_csv(httpd_req_t *req);
void af_ble_http_status_add(cJSON *root);

/* Внутренние прототипы */
static uint32_t utc_now(void);
static esp_err_t send_json_or_err(httpd_req_t *req, char *buf, size_t len, const char *err_msg);

/* Bounds include the widest integer/null fields and the longest reason text.
 * test_af_ble_http exercises these limits using the production serializers. */
#define AF_BLE_STATS_CAP 10240
#define AF_BLE_SESSION_JSON_MAX 248 /* object plus separating comma */
#define AF_BLE_SESSION_CSV_MAX 116  /* fields, commas and newline */
static const char session_csv_header[] = "idx,start_utc,end_utc,dur_s,pkts,bad,reason_code,reason,rssi_min_dbm,rssi_avg_dbm,rssi_max_dbm,setup_ms\n";

static char *ble_http_alloc(size_t cap) {
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return buf != NULL ? buf : malloc(cap);
}

/* JSON Writer */
typedef struct {
    char *b;
    size_t cap;
    size_t pos;
    bool comma;
    bool ovf;
} jw_t;

static void jw_raw(jw_t *w, const char *s) {
    if (w->ovf) return;
    size_t len = strlen(s);
    if (w->pos + len + 1 > w->cap) {
        w->ovf = true;
        return;
    }
    memcpy(w->b + w->pos, s, len);
    w->pos += len;
    w->b[w->pos] = '\0';
}

static void jw_key(jw_t *w, const char *key) {
    if (w->ovf) return;
    if (w->comma) jw_raw(w, ",");
    jw_raw(w, "\"");
    jw_raw(w, key);
    jw_raw(w, "\":");
    w->comma = false;
}

static void jw_obj(jw_t *w, const char *key) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    jw_raw(w, "{");
    w->comma = false;
}

static void jw_obj_end(jw_t *w) {
    if (w->ovf) return;
    jw_raw(w, "}");
    w->comma = true;
}

static void jw_arr(jw_t *w, const char *key) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    jw_raw(w, "[");
    w->comma = false;
}

static void jw_arr_end(jw_t *w) {
    if (w->ovf) return;
    jw_raw(w, "]");
    w->comma = true;
}

static void jw_num(jw_t *w, const char *key, double v, int dec) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    
    if (isnan(v) || isinf(v)) {
        jw_raw(w, "null");
    } else {
        char tmp[32];
        if (dec == 0) {
            snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
        } else {
            snprintf(tmp, sizeof(tmp), "%.*f", dec, v);
        }
        jw_raw(w, tmp);
    }
    w->comma = true;
}

static void jw_u(jw_t *w, const char *key, uint64_t v) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
    jw_raw(w, tmp);
    w->comma = true;
}

static void jw_i_nd(jw_t *w, const char *key, int32_t v, int32_t nd) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    if (v == nd) {
        jw_raw(w, "null");
    } else {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%ld", (long)v);
        jw_raw(w, tmp);
    }
    w->comma = true;
}

static void jw_str(jw_t *w, const char *key, const char *val) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    if (val == NULL) {
        jw_raw(w, "null");
    } else {
        jw_raw(w, "\"");
        for (const char *p = val; *p; p++) {
            if (*p >= 0x20 && *p <= 0x7E && *p != '"' && *p != '\\') {
                char c[2] = {*p, 0};
                jw_raw(w, c);
            }
        }
        jw_raw(w, "\"");
    }
    w->comma = true;
}

static void jw_bool(jw_t *w, const char *key, bool v) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    jw_raw(w, v ? "true" : "false");
    w->comma = true;
}

static void jw_null(jw_t *w, const char *key) {
    if (w->ovf) return;
    if (key != NULL) {
        jw_key(w, key);
    } else {
        if (w->comma) jw_raw(w, ",");
    }
    jw_raw(w, "null");
    w->comma = true;
}

static uint32_t utc_now(void) {
    if (net_time_source() == TIME_SRC_NONE) {
        return 0;
    }
    return (uint32_t)time(NULL);
}

static esp_err_t send_json_or_err(httpd_req_t *req, char *buf, size_t len, const char *err_msg) {
    esp_err_t err;
    if (err_msg != NULL) {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        err = httpd_resp_send(req, buf, len);
    }
    free(buf);
    return err;
}

esp_err_t af_ble_http_stats(httpd_req_t *req) {
    char *buf = ble_http_alloc(AF_BLE_STATS_CAP);
    if (buf == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    af_bs_snap_t s;
    uint64_t now_ms;
    if (!ble_gw_stats_snapshot(&s, &now_ms)) {
        jw_t w = { .b = buf, .cap = AF_BLE_STATS_CAP, .pos = 0, .comma = false, .ovf = false };
        jw_obj(&w, NULL);
        jw_bool(&w, "ready", false);
        jw_obj_end(&w);
        esp_err_t err = send_json_or_err(req, buf, w.pos, w.ovf ? "too big" : NULL);
        return err;
    }

    jw_t w = { .b = buf, .cap = AF_BLE_STATS_CAP, .pos = 0, .comma = false, .ovf = false };
    jw_obj(&w, NULL);
    
    jw_bool(&w, "ready", true);
    jw_bool(&w, "connected", s.connected);
    jw_bool(&w, "ext", s.ext);
    jw_u(&w, "uptime_s", now_ms / 1000);

    /* Link */
    jw_obj(&w, "link");
    double sess_s = s.connected ? (double)s.sess_dur_ms / 1000.0 : 0.0;
    jw_num(&w, "session_s", sess_s, 3);
    
    uint32_t now_utc = utc_now();
    if (s.connected && now_utc > 0) {
        jw_u(&w, "session_start_utc", now_utc - (uint32_t)(s.sess_dur_ms / 1000));
    } else {
        jw_null(&w, "session_start_utc");
    }

    jw_num(&w, "up_boot_pct", s.up_boot_pct, 2);
    if (!isnan(s.up_24h_pct)) jw_num(&w, "up_24h_pct", s.up_24h_pct, 2); /* b29 B04: при ext=0 поле опускается */
    
    jw_u(&w, "connects", s.boot.connects);
    jw_u(&w, "disconnects", s.boot.disconnects);
    jw_u(&w, "attempts", s.boot.attempts);
    jw_u(&w, "fails", s.boot.fails);
    jw_u(&w, "fail_streak", s.fail_streak);
    
    if (s.ext) { /* b26 П.10: итоги за всё время - расширенная статистика */
    jw_u(&w, "connects_total", s.life.connects);
    jw_u(&w, "disconnects_total", s.life.disconnects);
    jw_u(&w, "attempts_total", s.life.attempts);
    jw_u(&w, "fails_total", s.life.fails);
    jw_u(&w, "conn_total_s", s.life.conn_ms / 1000);
    }

    if (s.last_reason_valid) {
        jw_u(&w, "last_reason_code", s.last_reason);
        jw_str(&w, "last_reason", af_bs_reason_text(s.last_reason));
        jw_str(&w, "last_reason_group", af_bs_group_text(s.last_reason_group));
    } else {
        jw_null(&w, "last_reason_code");
        jw_null(&w, "last_reason");
        jw_null(&w, "last_reason_group");
    }

    if (s.ext) { /* b26 П.10 */
    jw_obj(&w, "reasons");
    jw_u(&w, "other", s.boot.reason[0]);
    jw_u(&w, "timeout", s.boot.reason[1]);
    jw_u(&w, "remote", s.boot.reason[2]);
    jw_u(&w, "local", s.boot.reason[3]);
    jw_u(&w, "establish", s.boot.reason[4]);
    jw_u(&w, "l2cap", s.boot.reason[5]);
    jw_obj_end(&w);

    jw_obj(&w, "reasons_total");
    jw_u(&w, "other", s.life.reason[0]);
    jw_u(&w, "timeout", s.life.reason[1]);
    jw_u(&w, "remote", s.life.reason[2]);
    jw_u(&w, "local", s.life.reason[3]);
    jw_u(&w, "establish", s.life.reason[4]);
    jw_u(&w, "l2cap", s.life.reason[5]);
    jw_obj_end(&w);
    }

    if (s.ext) {
        if (s.have_last) jw_num(&w, "last_session_s", (double)s.last_dur_ms / 1000.0, 3);
        else jw_null(&w, "last_session_s");

        if (s.have_ext) jw_num(&w, "longest_session_s", (double)s.longest_ms / 1000.0, 3);
        else jw_null(&w, "longest_session_s");
        
        if (s.have_ext) jw_num(&w, "shortest_session_s", (double)s.shortest_ms / 1000.0, 3);
        else jw_null(&w, "shortest_session_s");

        if (s.ring_n > 0) jw_num(&w, "avg_session_s", (double)s.avg_ms / 1000.0, 3);
        else jw_null(&w, "avg_session_s");
        
        if (s.ring_n > 0) jw_num(&w, "median_session_s", (double)s.median_ms / 1000.0, 3);
        else jw_null(&w, "median_session_s");
        
        jw_u(&w, "ring_n", s.ring_n);
    }
    jw_obj_end(&w);

    /* Rate */
    jw_obj(&w, "rate");
    jw_num(&w, "pps_inst", s.pps_inst, 3);
    jw_num(&w, "pps_1m", s.pps_1m, 3);
    jw_num(&w, "pps_10m", s.pps_10m, 3);
    jw_num(&w, "pps_sess", s.pps_sess, 3);
    if (s.ext) { /* b26 П.10 */
    jw_num(&w, "bps_inst", s.bps_inst, 2);
    jw_num(&w, "bps_1m", s.bps_1m, 2);
    jw_num(&w, "bps_10m", s.bps_10m, 2);
    }
    jw_num(&w, "bps_sess", s.bps_sess, 2);
    
    jw_u(&w, "pkts_session", s.sess_pkts);
    jw_u(&w, "bad_session", s.sess_bad);
    jw_u(&w, "pkts_boot", s.pkts_boot);
    jw_u(&w, "bad_boot", s.bad_boot);
    jw_num(&w, "bad_pct", s.bad_pct, 3);
    
    if (s.have_bad) jw_num(&w, "last_bad_age_s", (double)s.last_bad_age_ms / 1000.0, 3);
    else jw_null(&w, "last_bad_age_s");
    
    jw_i_nd(&w, "last_pkt_age_ms", s.last_pkt_age_ms, AF_BS_ND);
    jw_u(&w, "nominal_ms", 2000);
    
    if (s.ext) {
        jw_i_nd(&w, "iv_last_ms", s.i_last_ms, AF_BS_ND);
        jw_i_nd(&w, "iv_min_ms", s.i_min_ms, AF_BS_ND);
        jw_i_nd(&w, "iv_max_ms", s.i_max_ms, AF_BS_ND);
        jw_num(&w, "iv_avg_ms", s.i_avg_ms, 1);
        jw_num(&w, "jitter_ms", s.jitter_ms, 2);
        jw_u(&w, "pauses", s.pauses);
        jw_u(&w, "pause_max_ms", s.pause_max_ms);
        
        jw_i_nd(&w, "iv10_min_ms", s.i10_min_ms, AF_BS_ND);
        jw_i_nd(&w, "iv10_max_ms", s.i10_max_ms, AF_BS_ND);
        jw_num(&w, "iv10_avg_ms", s.i10_avg_ms, 1);
    }
    jw_obj_end(&w);

    /* Params */
    jw_obj(&w, "params");
    jw_num(&w, "interval_ms", s.ci_ms, 2);
    jw_i_nd(&w, "latency", s.latency, AF_BS_ND);
    jw_i_nd(&w, "timeout_ms", s.timeout_ms, AF_BS_ND);
    jw_i_nd(&w, "mtu", s.mtu, AF_BS_ND);
    
    if (s.ext) { /* b26 П.10 */
    jw_null(&w, "phy_tx");
    jw_null(&w, "phy_rx");
    jw_str(&w, "phy_note", "n/a: BLE 5.0 API off in this build");
    }
    
    jw_u(&w, "tx_set_dbm", 20);
    jw_i_nd(&w, "tx_eff_level", s.tx_eff, AF_BS_ND);
    
    jw_i_nd(&w, "rssi_dbm", s.rssi_cur, AF_BS_RSSI_ND);
    if (s.ext) {
        jw_i_nd(&w, "rssi_min_dbm", s.rssi_min, AF_BS_RSSI_ND);
        jw_i_nd(&w, "rssi_avg_dbm", s.rssi_avg, AF_BS_RSSI_ND);
        jw_i_nd(&w, "rssi_max_dbm", s.rssi_max, AF_BS_RSSI_ND);
        
        jw_arr(&w, "rssi_hist");
        for (int i = 0; i < AF_BS_RSSI_BINS; i++) {
            jw_u(&w, NULL, s.rssi_hist[i]);
        }
        jw_arr_end(&w);
        
        jw_u(&w, "rssi_bin_db", 10);
    }
    jw_i_nd(&w, "setup_ms", s.setup_ms, AF_BS_ND);
    jw_obj_end(&w);

    if (s.ext) {
        /* Scan */
        jw_obj(&w, "scan");
        jw_num(&w, "interval_ms", s.scan_int_ms, 2);
        jw_num(&w, "window_ms", s.scan_win_ms, 2);
        jw_u(&w, "scans", s.scan_n);
        jw_u(&w, "adv_last", s.scan_adv);
        jw_u(&w, "found_last", s.scan_cand);
        jw_i_nd(&w, "detect_ms", s.scan_detect_ms, AF_BS_ND);
        jw_obj_end(&w);
    }

    /* Device */
    jw_obj(&w, "device");
    char addr[18] = {0};
    char name[32] = {0};
    int addr_type = -1;
    ble_gw_stats_device(addr, sizeof(addr), &addr_type, name, sizeof(name));
    
    if (addr[0] != '\0') jw_str(&w, "addr", addr);
    else jw_null(&w, "addr");
    
    if (addr_type == 0) jw_str(&w, "addr_type", "public");
    else if (addr_type == 1) jw_str(&w, "addr_type", "random");
    else jw_null(&w, "addr_type");
    
    if (name[0] != '\0') jw_str(&w, "name", name);
    else jw_null(&w, "name");
    jw_obj_end(&w);

    jw_obj_end(&w);

    esp_err_t err = send_json_or_err(req, buf, w.pos, w.ovf ? "too big" : NULL);
    return err;
}

esp_err_t af_ble_http_sessions(httpd_req_t *req) {
    int limit = 32;
    int qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        if (qlen > 63) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query too long");
        }
        char q[64];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        }
        /* Строгий парсинг: только limit=N */
        if (strncmp(q, "limit=", 6) != 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        }
        const char *val = q + 6;
        size_t vlen = strlen(val);
        if (vlen < 1 || vlen > 2) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad limit");
        }
        int v = 0;
        for (size_t i = 0; i < vlen; i++) {
            if (val[i] < '0' || val[i] > '9') {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad limit");
            }
            v = v * 10 + (val[i] - '0');
        }
        if (v < 1 || v > 32) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad limit");
        }
        limit = v;
    }

    size_t cap = 3 + (size_t)limit * AF_BLE_SESSION_JSON_MAX;
    char *buf = ble_http_alloc(cap);
    if (buf == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    jw_t w = { .b = buf, .cap = cap, .pos = 0, .comma = false, .ovf = false };
    jw_arr(&w, NULL);

    if (ble_gw_is_ready()) {
        for (int i = 0; i < limit; i++) {
            af_bs_sess_t r;
            if (!ble_gw_stats_session((uint8_t)i, &r)) break;
            
            jw_obj(&w, NULL);
            jw_u(&w, "idx", i);
            
            if (r.end_utc > 0) {
                jw_u(&w, "end_utc", r.end_utc);
                jw_u(&w, "start_utc", r.end_utc - (uint32_t)(r.dur_ms / 1000));
            } else {
                jw_null(&w, "end_utc");
                jw_null(&w, "start_utc");
            }
            
            jw_num(&w, "dur_s", (double)r.dur_ms / 1000.0, 3);
            jw_u(&w, "pkts", r.pkts);
            jw_u(&w, "bad", r.bad);
            jw_u(&w, "reason_code", r.reason);
            jw_str(&w, "reason", af_bs_reason_text(r.reason));
            
            jw_i_nd(&w, "rssi_min_dbm", r.rssi_min, AF_BS_RSSI_ND);
            jw_i_nd(&w, "rssi_avg_dbm", r.rssi_avg, AF_BS_RSSI_ND);
            jw_i_nd(&w, "rssi_max_dbm", r.rssi_max, AF_BS_RSSI_ND);
            
            if (r.setup_ms > 0) jw_u(&w, "setup_ms", r.setup_ms);
            else jw_null(&w, "setup_ms");
            
            jw_obj_end(&w);
        }
    }
    jw_arr_end(&w);

    esp_err_t err = send_json_or_err(req, buf, w.pos, w.ovf ? "too big" : NULL);
    return err;
}

esp_err_t af_ble_http_sessions_csv(httpd_req_t *req) {
    int qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
    }

    size_t cap = sizeof(session_csv_header) + 32 * AF_BLE_SESSION_CSV_MAX;
    char *buf = ble_http_alloc(cap);
    if (buf == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    size_t pos = 0;
    
    #define CSV_APPEND(...) do { \
        int n = snprintf(buf + pos, cap - pos, __VA_ARGS__); \
        if (n < 0 || (size_t)n >= cap - pos) { \
            free(buf); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "too big"); \
        } \
        pos += n; \
    } while(0)

    CSV_APPEND("%s", session_csv_header);

    if (ble_gw_is_ready()) {
        for (int i = 0; i < 32; i++) {
            af_bs_sess_t r;
            if (!ble_gw_stats_session((uint8_t)i, &r)) break;
            
            CSV_APPEND("%d,", i);
            
            if (r.end_utc > 0) {
                CSV_APPEND("%lu,", (unsigned long)(r.end_utc - (uint32_t)(r.dur_ms / 1000)));
                CSV_APPEND("%lu,", (unsigned long)r.end_utc);
            } else {
                CSV_APPEND(",");
                CSV_APPEND(",");
            }
            
            CSV_APPEND("%.3f,", (double)r.dur_ms / 1000.0);
            CSV_APPEND("%lu,", (unsigned long)r.pkts);
            CSV_APPEND("%lu,", (unsigned long)r.bad);
            CSV_APPEND("%u,", r.reason);
            
            const char *rt = af_bs_reason_text(r.reason);
            if (rt) {
                CSV_APPEND("%s,", rt);
            } else {
                CSV_APPEND(",");
            }
            
            if (r.rssi_min != AF_BS_RSSI_ND) CSV_APPEND("%d,", r.rssi_min);
            else CSV_APPEND(",");
            
            if (r.rssi_avg != AF_BS_RSSI_ND) CSV_APPEND("%d,", r.rssi_avg);
            else CSV_APPEND(",");
            
            if (r.rssi_max != AF_BS_RSSI_ND) CSV_APPEND("%d,", r.rssi_max);
            else CSV_APPEND(",");
            
            if (r.setup_ms > 0) CSV_APPEND("%lu\n", (unsigned long)r.setup_ms);
            else CSV_APPEND("\n");
        }
    }
    
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"ble_sessions.csv\"");
    esp_err_t err = httpd_resp_send(req, buf, pos);
    free(buf);
    return err;
}

void af_ble_http_status_add(cJSON *root) {
    af_bs_snap_t s;
    uint64_t now_ms;
    if (!ble_gw_stats_snapshot(&s, &now_ms)) {
        return;
    }

    cJSON_AddNumberToObject(root, "ble_sess_s", (double)s.sess_dur_ms / 1000.0);
    cJSON_AddNumberToObject(root, "ble_connects_total", s.life.connects);
    cJSON_AddNumberToObject(root, "ble_disconnects_total", s.life.disconnects);
    if (!isnan(s.up_24h_pct)) cJSON_AddNumberToObject(root, "ble_uptime_24h_pct", s.up_24h_pct);
    
    if (isnan(s.pps_1m)) cJSON_AddNullToObject(root, "ble_pps");
    else cJSON_AddNumberToObject(root, "ble_pps", s.pps_1m);
    
    if (s.rssi_cur == AF_BS_RSSI_ND) cJSON_AddNullToObject(root, "ble_rssi_dbm");
    else cJSON_AddNumberToObject(root, "ble_rssi_dbm", s.rssi_cur);
    
    if (isnan(s.ci_ms)) cJSON_AddNullToObject(root, "ble_interval_ms");
    else cJSON_AddNumberToObject(root, "ble_interval_ms", s.ci_ms);
    
    if (s.mtu == AF_BS_ND) cJSON_AddNullToObject(root, "ble_mtu");
    else cJSON_AddNumberToObject(root, "ble_mtu", s.mtu);
    
    if (s.last_reason_valid) {
        cJSON_AddStringToObject(root, "ble_last_reason", af_bs_reason_text(s.last_reason));
    } else {
        cJSON_AddNullToObject(root, "ble_last_reason");
    }
    
    cJSON_AddNumberToObject(root, "ble_pkts_boot", s.pkts_boot);
    
    if (isnan(s.bad_pct)) cJSON_AddNullToObject(root, "ble_bad_pct");
    else cJSON_AddNumberToObject(root, "ble_bad_pct", s.bad_pct);
}
