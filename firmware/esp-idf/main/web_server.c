#include "af_hostchk.h"
#include "af_hostreq.h"
#include "socket_budget.h"
#include "af_net.h"
#include "af_ble_http.h"
#include "http_activity_plan.h"
#include "net_time.h"
#include "boot_config.h"
#include "task_load_sampler.h"
#include "http_io_gate.h"
#include "debug_log_ring.h"
#include "ota_image_check.h"
#include "ota_github_client.h"
#include "ota_busy.h"
#include "ota_session.h"
#include "af_store.h"
#include "af_log_api.h"
#include "af_fsinfo.h"
#include "af_etag.h"
#include "af_log_http.h"
#include "af_settings_http.h"
#include "af_ble_sel_http.h"
#include "af_env_drv.h"
#include "af_sens_req.h"
#include "af_mqtt_drv.h"
#include "af_push_drv.h"
#include "af_meas_drv.h"
#include "af_meas_jrn_http.h"
#include "af_meas_http.h"
#include "ota_timeout_budget.h"
#include "ttl_plan.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>
#include <math.h>
#include <sys/time.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_littlefs.h"
#include "esp_random.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

static const char *TAG = "web";
/* Owned only by this HTTP task: session open and synchronous diag GET. */
static uint32_t s_http_nodelay_ok;
static uint32_t s_http_nodelay_fail;
#define SYS_SLOW_PERIOD_US (10LL * 1000000LL)

// Статические страницы: встроены в прошивку, отдаются без версии в пути.
// no-cache = обязательная ревалидация на каждый показ (после OTA).
#define EMBED_HTML_HANDLER(fn,sym) static esp_err_t fn(httpd_req_t *req){ \
    extern const uint8_t sym##_start[] asm("_binary_" #sym "_start"); \
    extern const uint8_t sym##_end[]   asm("_binary_" #sym "_end"); \
    httpd_resp_set_type(req,"text/html"); \
    httpd_resp_set_hdr(req,"Cache-Control","no-cache"); \
    httpd_resp_send(req,(const char *)sym##_start, sym##_end - sym##_start); \
    return ESP_OK; }

EMBED_HTML_HANDLER(handle_captive_page, captive_html)

// Страница истории (Э3d): gzip-ассет собирается scripts/build_history.py, отдаётся как есть.
static esp_err_t handle_history_page(httpd_req_t *req)
{
    extern const uint8_t history_html_gz_start[] asm("_binary_history_html_gz_start");
    extern const uint8_t history_html_gz_end[]   asm("_binary_history_html_gz_end");
    httpd_resp_set_type(req, "text/html");
    // e16e (perf-audit cpu-http F4): ETag = первые 8 байт SHA-256 ELF (как у common-time.js) -> при совпадении 304 без тела 115 КБ.
    char etag[20];
    const esp_app_desc_t *ad = esp_app_get_description();
    if (ad && af_etag_make(ad->app_elf_sha256, sizeof(ad->app_elf_sha256), etag, sizeof(etag)) == 18) {
        httpd_resp_set_hdr(req, "ETag", etag);
        char inm[96];
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && af_etag_match(inm, etag)) {
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            return httpd_resp_send(req, NULL, 0);
        }
    }
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)history_html_gz_start, history_html_gz_end - history_html_gz_start);
}

// Прототип: реализация в части 2
static const char *reset_reason_str(esp_reset_reason_t r);

static esp_err_t handle_status(httpd_req_t *req)
{
    bool include_load = true;
    bool measure_load = false;
    char query[16];
    char load_duration[24]; /* httpd keeps the header pointer until response send. */
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0 && query_len < sizeof(query) &&
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (strcmp(query, "load=0") == 0) {
            include_load = false;
            measure_load = true;
        } else if (strcmp(query, "load=1") == 0) {
            measure_load = true;
        }
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(root, "wifi_connected", wifi_is_connected());
    cJSON_AddBoolToObject(root, "ble_ready", ble_gw_is_ready());

    // Э2: состояние BLE-клиента AtomFast
    ble_gw_status_t bst;
    ble_gw_get_status(&bst);
    cJSON_AddBoolToObject(root, "ble_connected", bst.connected);
    cJSON_AddNumberToObject(root, "ble_reconnects", (double)bst.reconnects);
    cJSON_AddNumberToObject(root, "ble_rx_total", (double)bst.rx_total);
    cJSON_AddNumberToObject(root, "ble_bad_total", (double)bst.bad_total);
    cJSON_AddNumberToObject(root, "ble_errors", (double)bst.errors);
    af_ble_http_status_add(root);   /* e16: ble_* (сессии, скорость, RSSI, причина разрыва) */
    if (bst.has_rec) {
        cJSON *rec = cJSON_AddObjectToObject(root, "ble_last_rec");
        if (rec) {
            cJSON_AddNumberToObject(rec, "age_ms", (double)bst.last_rec_age_ms);
            cJSON_AddNumberToObject(rec, "flags", (double)bst.flags);
            cJSON_AddNumberToObject(rec, "dose_msv", (double)bst.dose_msv);
            cJSON_AddNumberToObject(rec, "doserate_usvh", (double)bst.doserate_usvh);
            cJSON_AddNumberToObject(rec, "pulses_2s", (double)bst.pulses_2s);
            cJSON_AddNumberToObject(rec, "cps", (double)bst.cps);
            cJSON_AddNumberToObject(rec, "battery", (double)bst.battery);
            cJSON_AddNumberToObject(rec, "temp_c", (double)bst.temp_c);
        }
    } else {
        cJSON_AddNullToObject(root, "ble_last_rec");
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "fw_version", app_desc ? app_desc->version : "?");

    const esp_partition_t *running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "running_partition", running ? running->label : "?");

    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "boot_count", (double)boot_config_get_boot_session());
    cJSON_AddNumberToObject(root, "session", (double)boot_config_get_session());

    char elf_sha[17];
    esp_app_get_elf_sha256(elf_sha, sizeof(elf_sha));
    cJSON_AddStringToObject(root, "elf_sha", elf_sha);

    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(esp_reset_reason()));
    cJSON_AddNumberToObject(root, "reset_reason_code", (double)esp_reset_reason());

    int64_t load_start_us = esp_timer_get_time();
    if (include_load) {
        cJSON *load = task_load_json();
        if (load) {
            cJSON_AddItemToObject(root, "load", load);
        }
    }
    if (measure_load) {
        snprintf(load_duration, sizeof(load_duration), "%" PRId64, esp_timer_get_time() - load_start_us);
        httpd_resp_set_hdr(req, "X-AF-Load-Us", load_duration);
    }

    cJSON_AddStringToObject(root, "time_source", net_time_source_str());
    cJSON_AddBoolToObject(root, "sntp_synced", net_time_sntp_synced());

    // e12: блок MQTT (пароль и хост не выводятся)
    af_mqtt_stat_t mst;
    char mbuf[160];
    af_mqtt_drv_get_stat(&mst);
    if (af_mqtt_stat_json(&mst, mbuf, sizeof(mbuf)) > 0) {
        cJSON_AddRawToObject(root, "mqtt", mbuf);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }
    httpd_resp_sendstr(req, json ? json : "{\"ok\":false,\"err\":\"oom\"}");
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_system(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "fw_version", app_desc ? app_desc->version : "?");
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());

    // Медленная часть: обходы кучи и стеки задач (каждые 10 с)
    static const char *const stk_names[] = { "httpd", "esp_timer", "sys_evt", "tiT", "wifi", "ble_gw", "af_mqtt", "mqtt_task" };
    enum { STK_N = sizeof(stk_names) / sizeof(stk_names[0]) };
    static uint32_t s_heap_walk_n;
    static int64_t  s_slow_at = 0;
    static size_t   s_psram_largest, s_int_largest, s_dflt_largest;
    static int64_t  s_walk_us;
    static int32_t  s_stk_min[STK_N];

    int64_t slow_now = esp_timer_get_time();
    if (ttl_expired(s_slow_at, slow_now, SYS_SLOW_PERIOD_US)) {
        s_heap_walk_n++;
        s_psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        s_int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        s_dflt_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT);
        s_walk_us = esp_timer_get_time() - slow_now;
        for (size_t i = 0; i < STK_N; i++) {
            TaskHandle_t th = xTaskGetHandle(stk_names[i]);
            s_stk_min[i] = th ? (int32_t)uxTaskGetStackHighWaterMark(th) : -1;
        }
        s_slow_at = slow_now;
    }

    size_t psram_largest = s_psram_largest, int_largest = s_int_largest, dflt_largest = s_dflt_largest;
    cJSON_AddNumberToObject(root, "heap_walk_us", (double)s_walk_us);
    cJSON_AddNumberToObject(root, "heap_walk_age_ms", (double)((slow_now - s_slow_at) / 1000));
    cJSON_AddNumberToObject(root, "psram_total", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "psram_largest", psram_largest);
    cJSON_AddNumberToObject(root, "int_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "int_largest", int_largest);
    cJSON_AddNumberToObject(root, "int_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "int_dflt_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT));
    cJSON_AddNumberToObject(root, "int_dflt_largest", dflt_largest);

    uint32_t af_n, af_size, af_caps, cj_spill;
    mem_diag_get(&af_n, &af_size, &af_caps, &cj_spill);
    cJSON_AddNumberToObject(root, "cjson_spill", cj_spill);
    cJSON_AddNumberToObject(root, "alloc_fail", af_n);
    cJSON_AddNumberToObject(root, "alloc_fail_last_size", af_size);
    cJSON_AddNumberToObject(root, "alloc_fail_last_caps", af_caps);

    cJSON *stk = cJSON_AddObjectToObject(root, "stack_min_free");
    for (size_t i = 0; stk && i < STK_N; i++) {
        if (s_stk_min[i] >= 0) {
            cJSON_AddNumberToObject(stk, stk_names[i], (double)s_stk_min[i]);
        }
    }

    cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_is_connected());

    // Информация о файловой системе: общий кэш 30 с (e16e: af_fsinfo, тот же, что у журнала/замеров/истории)
    size_t s_fs_total = 0, s_fs_used = 0;
    (void)af_fsinfo_get(&s_fs_total, &s_fs_used);
    cJSON_AddNumberToObject(root, "flash_total", (double)s_fs_total);
    cJSON_AddNumberToObject(root, "flash_used", (double)s_fs_used);

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
        cJSON_AddStringToObject(root, "ssid", (char *)ap.ssid);
    }

    switch (wifi_manager_mode()) {
        case NET_MODE_FIELD_AP: cJSON_AddStringToObject(root, "net_mode", "field_ap"); break;
        case NET_MODE_SETUP:    cJSON_AddStringToObject(root, "net_mode", "setup");    break;
        default:                cJSON_AddStringToObject(root, "net_mode", "sta");      break;
    }
    cJSON_AddStringToObject(root, "ap_ssid",        wifi_manager_ap_ssid());
    cJSON_AddNumberToObject(root, "ap_clients",     wifi_manager_ap_clients());
    cJSON_AddBoolToObject(root,   "ap_pass_default", wifi_manager_ap_pass_is_default());
    cJSON_AddBoolToObject(root,   "ap_forced",      wifi_manager_ap_forced());
    cJSON_AddStringToObject(root, "return_block_reason", wifi_manager_return_block_reason());

    cJSON_AddNumberToObject(root, "http_idle_s", (double)web_server_http_idle_s());
    cJSON_AddStringToObject(root, "time_source",    net_time_source_str());
    cJSON_AddBoolToObject(root,   "sntp_synced",    net_time_sntp_synced());

    cJSON_AddBoolToObject(root,   "http_heavy_busy", http_io_gate_busy());
    cJSON_AddNumberToObject(root, "http_heavy_rejects", (double)http_io_gate_reject_count());

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }
    httpd_resp_sendstr(req, json ? json : "{\"ok\":false,\"err\":\"oom\"}");
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_healthcheck(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    bool wifi_ok = wifi_is_connected();
    cJSON_AddStringToObject(root, "status", wifi_ok ? "ok" : "degraded");
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_ok);
    cJSON_AddBoolToObject(root, "ble_ready", ble_gw_is_ready());
    cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"oom\"}");
        cJSON_Delete(root);
        return ESP_OK;
    }

    if (wifi_ok) {
        httpd_resp_sendstr(req, json);
    } else {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, json);
    }
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Блок активности: метка последнего запроса
static http_activity_state_t s_activity;

void web_server_note_request_activity(const char *uri)
{
    http_activity_note_request(&s_activity, http_uri_is_activity(uri),
                                (uint32_t)(esp_timer_get_time() / 1000));
}

// N1: 404/405 за auth - без пароля 401 (не перечисляет пути); с валидным - обычный ответ.
static esp_err_t handle_404(httpd_req_t *req, httpd_err_code_t err)
{
    if (http_unrouted_needs_auth(req->uri) && !af_auth_check(req)) {
        return ESP_OK; // 401 уже отправлен af_auth_check
    }
    httpd_resp_send_err(req, err, err == HTTPD_405_METHOD_NOT_ALLOWED ? "Method Not Allowed" : "Not Found");
    return ESP_OK;
}

bool web_server_http_activity_quiet(uint32_t threshold_ms)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return http_activity_quiet(&s_activity, now_ms, threshold_ms);
}

uint32_t web_server_http_idle_s(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (now_ms - s_activity.last_user_event_ms) / 1000;
}

// Все URI под Basic auth (SAFETY-CORE п.5), кроме captive-проб ОС: они только редиректят
#define WEB_SERVER_URI_MAX 72
static esp_err_t (*s_wrap_handlers[WEB_SERVER_URI_MAX])(httpd_req_t *);

static esp_err_t activity_trampoline(httpd_req_t *req)
{
    size_t idx = (size_t)(intptr_t)req->user_ctx;
    if (!http_uri_needs_auth(req->uri)) {
        // Captive-проба: только фиксация активности и вызов обработчика
        web_server_note_request_activity(req->uri);
        return s_wrap_handlers[idx](req);
    }
    // Обычный запрос: проверка Basic auth
    if (!af_auth_check(req)) {
        // Ответ 401 уже отправлен af_auth_check
        return ESP_OK;
    }
    web_server_note_request_activity(req->uri);
    return s_wrap_handlers[idx](req);
}

// CSRF-токен: генерируется при старте, выдаётся по GET /api/csrf-token,
// требуется в заголовке X-CSRF-Token на всех мутирующих POST. Защищает
// открытый-в-LAN Web UI от drive-by cross-origin POST: сторонняя страница
// не может прочитать токен (same-origin policy) → не может подделать запрос.
static char s_csrf[33];

static void csrf_generate(void)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
        s_csrf[i] = hx[esp_random() & 0x0F];
    s_csrf[32] = '\0';
}

// P3-9: сравнение токена за постоянное время — не утекает позиция
// первого несовпавшего байта по времени ответа.
static bool csrf_ct_eq(const char *a, const char *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return d == 0;
}

// T-57 (P2-3): без пароля токен выдаётся любому, поэтому Host/Origin проверяются (DNS rebinding: чужой домен -> IP платы).
static bool host_origin_ok(httpd_req_t *req)
{
    char host[64] = {0}, org[96] = {0};
    // e16i b29 (V-17): усечение (RESULT_TRUNC) и любая иная ошибка = отказ, не "заголовка нет".
    int hc = af_hdr_class(httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)), ESP_OK, ESP_ERR_NOT_FOUND);
    int oc = af_hdr_class(httpd_req_get_hdr_value_str(req, "Origin", org, sizeof(org)), ESP_OK, ESP_ERR_NOT_FOUND);
    return af_host_origin_req_ok(hc, host, oc, org) != 0;
}

// Проверяет X-CSRF-Token, ответа не шлёт (импорт сначала сливает тело, потом отвечает 403).
static bool csrf_ok(httpd_req_t *req)
{
    char hdr[40] = {0};
    if (!host_origin_ok(req)) return false;
    return httpd_req_get_hdr_value_str(req, "X-CSRF-Token", hdr, sizeof(hdr)) == ESP_OK
        && s_csrf[0] && strlen(hdr) == 32 && csrf_ct_eq(hdr, s_csrf, 32);
}
// Проверяет X-CSRF-Token. При несовпадении сам шлёт 403 и возвращает false.
static bool csrf_check(httpd_req_t *req)
{
    if (csrf_ok(req)) return true;
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Bad or missing CSRF token");
    return false;
}

/* T-86: для обработчиков вне web_server.c (af_ble_sel_http.c) - проверка без отправки ответа */
bool web_server_csrf_ok(httpd_req_t *req)
{
    return csrf_ok(req);
}


static esp_err_t handle_csrf_token(httpd_req_t *req)
{
    char buf[48];
    if (!host_origin_ok(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Bad Host/Origin");
        return ESP_OK;
    }
    int n = snprintf(buf, sizeof(buf), "{\"token\":\"%s\"}", s_csrf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}


// #AUD-DIAG-1 R1: причина последнего сброса. Только имена, что есть во всех IDF 5.x;
// прочее — "OTHER" + числовой reset_reason_code рядом.
static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_UNKNOWN:   return "UNKNOWN";
    default:                return "OTHER";
    }
}


// #FW-50: GET /api/debug/log/meta — JSON status of PSRAM log ring (no CSRF).
// Он же отвечает на GET /api/debug/log/config: тело ответа — один и тот же
// meta_json (настройки живут в тех же полях enabled/level), два отдельных
// обработчика с одинаковым текстом расходились бы при первой же правке.
static esp_err_t handle_debug_log_meta(httpd_req_t *req)
{
    char buf[512];
    debug_log_ring_meta_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

// #FW-50: GET /api/debug/log?since=N — text/plain dump.
// Требует X-CSRF-Token, хотя это GET: в дампе лога видно SSID, IP и URL выгрузки,
// а без заголовка любая открытая в браузере сторонняя страница может дёрнуть
// эндпоинт с адреса пользователя. Токен читается только с самой платы
// (GET /api/csrf-token), поэтому drive-by странице он недоступен.
static esp_err_t handle_debug_log_get(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    uint32_t since = 0;
    char q[48], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK)
        since = strtoul(v, NULL, 10);
    return debug_log_ring_http_dump(req, since);
}

// #FW-50: POST /api/debug/log/flush — мутирующий запрос, CSRF как у остальных.
// Optional JSON body: {"upto_seq":N,"gen":G}
static esp_err_t handle_debug_log_flush(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    uint32_t upto = 0, gen = 0;
    int total = req->content_len;
    bool parse_failed = false;

    // Если тело запроса больше 255 байт — не читаем, это ошибка.
    // Если тело отсутствует (total <= 0) — это допустимо, сбрасываем всё.
    if (total >= 256) {
        parse_failed = true;
    } else if (total > 0 && total < 256) {
        char body[256];
        int r = httpd_req_recv(req, body, total);
        if (r <= 0) {
            parse_failed = true;
        } else {
            body[r < 255 ? r : 255] = '\0';
            cJSON *root = cJSON_Parse(body);
            if (root) {
                cJSON *u = cJSON_GetObjectItem(root, "upto_seq");
                cJSON *g = cJSON_GetObjectItem(root, "gen");
                if (cJSON_IsNumber(u)) upto = (uint32_t)u->valuedouble;
                if (cJSON_IsNumber(g)) gen = (uint32_t)g->valuedouble;
                cJSON_Delete(root);
            } else {
                parse_failed = true;
            }
        }
    }
    // total <= 0 — тело отсутствует, это нормально, сбрасываем всё

    if (parse_failed) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad flush body");
        return ESP_FAIL;
    }

    esp_err_t e = debug_log_ring_flush(upto, gen);
    if (e == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"gen_mismatch\"}");
    }
    if (e != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "flush failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"gen\":%lu}",
             (unsigned long)debug_log_ring_gen());
    return httpd_resp_sendstr(req, buf);
}

// #FW-50: POST /api/debug/log/config — settings. CSRF like other mutating UI.
// GET того же URI обслуживает handle_debug_log_meta (см. выше).
static esp_err_t handle_debug_log_config_set(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    int total = req->content_len;
    if (total <= 0 || total >= 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    char body[256];
    int r = httpd_req_recv(req, body, total);
    if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
        return ESP_FAIL;
    }
    body[r < 255 ? r : 255] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
        return ESP_FAIL;
    }
    cJSON *en = cJSON_GetObjectItem(root, "enabled");
    cJSON *lv = cJSON_GetObjectItem(root, "level");
    bool enabled = debug_log_ring_enabled();
    dbglog_level_t level = debug_log_ring_level();
    if (cJSON_IsBool(en)) enabled = cJSON_IsTrue(en);
    if (cJSON_IsString(lv) && lv->valuestring) {
        if (!strcmp(lv->valuestring, "debug")) level = DBGLOG_LEVEL_DEBUG;
        else if (!strcmp(lv->valuestring, "detailed")) level = DBGLOG_LEVEL_DETAILED;
        else level = DBGLOG_LEVEL_STANDARD;
    }
    cJSON_Delete(root);
    esp_err_t e = debug_log_ring_set_config(enabled, level);
    if (e != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"alloc_or_nvs\"}");
    }
    char buf[512];
    debug_log_ring_meta_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}


// AWF-5: GET /api/ota/github/check -- сверка последнего релиза GitHub.
// Ответ строит ota_gh_check() (main/ota_github_client.c).
static esp_err_t handle_ota_gh_check(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;   // S-01 (1.2.30): тяжёлый GET — токен обязателен (drive-by с чужой страницы)
    char resp[560];   // #AWF-6: +html_url (до 200 Б) поверх прежних полей; LK-07: +"state"
    ota_gh_check_async(resp, sizeof(resp));   // LK-07: проверка в фоне, httpd не ждёт GitHub
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// AWF-5: POST /api/ota/github/install -- запускает фоновую задачу
// (ota_github_client.c: install_task); httpd-worker не блокируется на минуты
// сетевого приёма/прошивки.
static esp_err_t handle_ota_gh_install(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    esp_err_t e = ota_gh_install_start();
    if (e != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "Install already running or OOM");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// AWF-5: GET /api/ota/github/progress -- опрос UI во время установки.
static esp_err_t handle_ota_gh_progress(httpd_req_t *req)
{
    ota_gh_progress_t p = ota_gh_get_progress();
    static const char *names[] = {"idle","checking","downloading","verifying",
                                   "installing","done","error"};
    char resp[192];
    snprintf(resp, sizeof(resp),
             "{\"state\":\"%s\",\"bytes\":%" PRIu32 ",\"total\":%" PRIu32 ",\"error\":\"%s\"}",
             names[p.state], p.bytes, p.total, p.error);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// AWF-5: GET/POST /api/ota/github/channel -- тумблер "получать предрелизы".
static esp_err_t handle_ota_gh_channel_get(httpd_req_t *req)
{
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"prerelease\":%s}",
             ota_gh_prerelease_channel_get() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t handle_ota_gh_channel_set(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    char body[64] = {0};
    int total = req->content_len;
    if (total > 0 && total < (int)sizeof(body)) httpd_req_recv(req, body, total);
    bool on = strstr(body, "\"prerelease\":true") != NULL;
    ota_gh_prerelease_channel_set(on);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// AWF-4: POST /api/ota — приём .bin потоком (буфер 4 КБ), запись в неактивный
// OTA-раздел. CSRF как у остальных мутирующих POST. Тело — application/octet-stream
// (не multipart): UI шлёт raw ArrayBuffer, curl — `--data-binary @file.bin`.
// D4 (P3): httpd v5.4 — один task на приём (см. verify-awf4-2026-09-27.md
// D5) — медленный клиент, вечно попадающий в HTTPD_SOCK_ERR_TIMEOUT, держал
// бы его неограниченно, блокируя ВСЕ остальные HTTP-запросы. Порог — число
// ПОДРЯД идущих таймаутов (сброс на каждый успешный recv), не время: точное
// значение recv-таймаута — О9 (release-gate-firmware-v1.2.28-code.md):
// ПЕРЕОПРЕДЕЛЁН этим проектом, `config.recv_wait_timeout = 3` (ниже,
// web_server_init) — 3 с, не дефолт esp_http_server; бюджет простоя httpd
// D4 = 30 × 3 с = 90 с. 30 подряд отказов приёма — явно не "штатная сеть
// подождала", а зависший/враждебный клиент.
#define OTA_MAX_CONSECUTIVE_TIMEOUTS 30u

// P1-фикс (verify-awf5-github-ota-2026-09-27.md разд.2.4): переименована в
// _locked, тело не тронуто -- ota_busy_acquire/release снаружи, одной точкой
// на функцию (иначе пришлось бы трогать все ~8 return-путей ниже).
static esp_err_t handle_ota_locked(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    int total = req->content_len;
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    if ((size_t)total > update->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image too large for partition");
        return ESP_FAIL;
    }

    ota_session_t ota = {0};
    // D1: OTA_SIZE_UNKNOWN стирает ВЕСЬ слот сразу (не только Content-Length
    // байт от клиента) — обрезанная запись не оставляет "хвост" старого
    // образа структурно смешанным со свежими байтами.
    esp_err_t err = ota_session_begin(&ota, update, OTA_SIZE_UNKNOWN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AWF-4: esp_ota_begin: %s", esp_err_to_name(err));
        af_log_event(3, "ota", "upload: ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin");
        return ESP_FAIL;
    }

    // Буфер в куче, не на стеке httpd-worker'а (config.stack_size=8192,
    // web_server_init) — тот же приём, что handle_devlog (malloc(9000)).
    const size_t bufsz = 4096;
    uint8_t *buf = malloc(bufsz);
    if (!buf) {
        ota_session_abort(&ota);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int remaining = total, received = 0;
    bool header_checked = false, image_ok = true;
    ota_image_walker_t walker;             // D1: структурная сверка сегментов
    ota_image_walker_init(&walker);
    uint32_t timeout_streak = 0;           // D4: подряд идущие таймауты
    while (remaining > 0) {
        int to_read = remaining < (int)bufsz ? remaining : (int)bufsz;
        int rd = httpd_req_recv(req, (char *)buf, to_read);
        if (rd == HTTPD_SOCK_ERR_TIMEOUT) {
            if (ota_timeout_budget_exceeded(++timeout_streak, OTA_MAX_CONSECUTIVE_TIMEOUTS)) {
                free(buf);
                ota_session_abort(&ota);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Timeout budget exceeded");
                return ESP_FAIL;
            }
            continue;   // повтор, не абор (задача 2)
        }
        timeout_streak = 0;
        if (rd <= 0) {
            free(buf);
            ota_session_abort(&ota);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv error");
            return ESP_FAIL;
        }
        if (!header_checked) {
            header_checked = true;
            // main/ota_image_check.h (host-тест tests/host/test_ota_image_check.c):
            // magic 0xE9 + chip_id ДО esp_ota_end() — не пропускаем битый/чужой образ.
            if (!ota_image_header_is_valid((const uint8_t *)buf, (size_t)rd, ESP_CHIP_ID_ESP32S3))
                image_ok = false;
        }
        if (!image_ok) break;
        // D1: подать те же байты, что уходят в esp_ota_write(), в структурный
        // walker — magic/segment_count/пройденность сегментов независимо от
        // Content-Length (см. main/ota_image_check.h).
        if (!ota_image_walker_feed(&walker, (const uint8_t *)buf, (size_t)rd)) {
            image_ok = false;
            break;
        }
        err = ota_session_write(&ota, buf, rd);
        if (err != ESP_OK) {
            free(buf);
            ota_session_abort(&ota);
            ESP_LOGE(TAG, "AWF-4: esp_ota_write: %s", esp_err_to_name(err));
            af_log_event(3, "ota", "upload: ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write");
            return ESP_FAIL;
        }
        remaining -= rd;
        received += rd;
    }
    free(buf);
    if (!image_ok) {
        ota_session_abort(&ota);
        af_log_event(3, "ota", "upload: bad image header");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad image header (magic/chip_id/segments)");
        return ESP_FAIL;
    }
    // D1 (verify-awf4-2026-09-27.md, дефект D1/T3): Content-Length клиента
    // может совпасть с фактически принятым числом байт — это НЕ доказывает,
    // что образ ЦЕЛ (оба числа под контролем клиента). Структурная сверка:
    // все сегменты, заявленные САМИМ образом, обязаны быть пройдены целиком.
    if (!ota_image_walker_is_complete(&walker)) {
        ota_session_abort(&ota);
        ESP_LOGW(TAG, "AWF-4: D1 guard: image incomplete (%d/%d bytes, walker state=%d)",
                 received, total, (int)walker.state);
        af_log_event(3, "ota", "upload: incomplete image %d/%d bytes", received, total);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete image (truncated)");
        return ESP_FAIL;
    }

    // esp_ota_end() сама проверяет образ (SHA-256/подпись при secure boot) и
    // отвергает мусор эффективнее нашего header-чека выше — тот отбраковывает
    // раньше (не тратит запись всего .bin), а не заменяет эту проверку.
    err = ota_session_end(&ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AWF-4: esp_ota_end (image validation failed): %s", esp_err_to_name(err));
        af_log_event(3, "ota", "upload: image validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ota_end: invalid image");
        return ESP_FAIL;
    }
    // #OTA-VR (ota_busy.h): образ уже прошёл полную проверку в esp_ota_end() выше; повторная проверка
    // внутри set_boot изредка ложно отказывает — до 3 попыток. Отказ esp_ota_end() остаётся окончательным.
    err = ota_set_boot_verified(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AWF-4: esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        af_log_event(3, "ota", "upload: set_boot failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot_partition");
        return ESP_FAIL;
    }

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"bytes\":%d,\"partition\":\"%s\"}",
             received, update->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    ESP_LOGW(TAG, "AWF-4: OTA written %d bytes to '%s', rebooting", received, update->label);
    af_log_event(1, "ota", "upload ok %d bytes, rebooting", received);
    // #AUD-F01 (P-016): не терять открытый сегмент. После ответа: httpd однопоточный,
    // пока идёт финализация, UI (system.html pollOtaReboot) ответа старой прошивки не получит.
    // Как handle_reboot_esp/handle_wifi_reset: ответ уже отдан httpd_resp_sendstr
    // (блокирующий send() успел уйти в TCP-буфер), задержка — дать WiFi/LWIP
    // время реально протолкнуть его в эфир до esp_restart().
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

// P1-фикс: общий замок с GitHub-install_task (main/ota_busy.h) -- занято ->
// 409, esp_ota_begin() (внутри _locked) вообще не вызывается.
static esp_err_t handle_ota(httpd_req_t *req)
{
    if (!ota_busy_acquire(OTA_BUSY_MANUAL)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "GitHub OTA install in progress");
        return ESP_FAIL;
    }
    esp_err_t r = handle_ota_locked(req);
    ota_busy_release(OTA_BUSY_MANUAL);   // на успехе _locked уже esp_restart() и сюда не дойдёт
    return r;
}


// #FIELD-5: общий JS авто-синхронизации времени (application/javascript, не text/html).
static esp_err_t handle_common_time_js(httpd_req_t *req)
{
    // b30 (H01): встроен gzip -9 (scripts/build_history.py), как history.html.gz
    extern const uint8_t common_time_js_start[] asm("_binary_common_time_js_gz_start");
    extern const uint8_t common_time_js_end[]   asm("_binary_common_time_js_gz_end");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    // 1.2.36 (риск аудита S1-1): скрипт вырос до ~17 КБ и уходил целиком на каждый показ страницы. ETag = первые 8 байт SHA-256 ELF
    // (app_elf_sha256: меняется с любой пересборкой, а не только с version.txt -> после OTA старый кеш не подходит) + no-cache:
    // браузер всегда ревалидирует, при совпадении получает 304 без тела. Версия в URL не нужна (страницы не меняются).
    char etag[20];
    const esp_app_desc_t *ad = esp_app_get_description();
    if (ad) {
        static const char hx[] = "0123456789abcdef";
        etag[0] = '"';
        for (int i = 0; i < 8; i++) {
            etag[1 + 2 * i] = hx[ad->app_elf_sha256[i] >> 4];
            etag[2 + 2 * i] = hx[ad->app_elf_sha256[i] & 15];
        }
        etag[17] = '"';
        etag[18] = 0;
        httpd_resp_set_hdr(req, "ETag", etag);
        char inm[24];
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && strcmp(inm, etag) == 0) {
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            return httpd_resp_send(req, NULL, 0);
        }
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, (const char *)common_time_js_start, common_time_js_end - common_time_js_start);
    return ESP_OK;
}


static esp_err_t handle_reboot_esp(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    af_log_event(1, "sys", "reboot requested via web");
    esp_restart();
    return ESP_OK;
}

static esp_err_t sens_reply_err(httpd_req_t *req, const char *status, const char *code)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":\"%s\"}", code);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

static esp_err_t sens_parse_fail(httpd_req_t *req, int pc)
{
    switch (pc) {
        case AF_SENS_E_BODY:    return sens_reply_err(req, "400 Bad Request", "bad_body");
        case AF_SENS_E_JSON:    return sens_reply_err(req, "400 Bad Request", "json");
        case AF_SENS_E_KEY:     return sens_reply_err(req, "400 Bad Request", "unknown_key");
        case AF_SENS_E_VALUE:   return sens_reply_err(req, "400 Bad Request", "bad_value");
        case AF_SENS_E_NOSENSOR:return sens_reply_err(req, "404 Not Found", "no_sensor");
        default:                return sens_reply_err(req, "400 Bad Request", "bad_value");
    }
}

static int sens_read_body(httpd_req_t *req, char *buf, size_t cap)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= cap) return -1;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) return -1;
        got += r;
    }
    return total;
}

static esp_err_t handle_sensors_get(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(4096);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int n = af_env_drv_json(buf, 4096);
    if (n < 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, buf, n);
    free(buf);
    return e;
}

static esp_err_t handle_sensors_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[320];
    int n = sens_read_body(req, body, sizeof(body));
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");

    af_sens_cmd_t cmd;
    int pc = af_sens_parse(body, (size_t)n, af_env_drv_primary(), &cmd);
    if (pc != 0) return sens_parse_fail(req, pc);
    af_log_event(1, "cfg", "sensors command kind=%d", (int)cmd.kind);

    char resp[96];
    int pers = 0;

    switch (cmd.kind) {
        case AF_SENS_CMD_SCAN: {
            int f = 0, c = 0;
            int rc = af_env_drv_scan(&f, &c);
            if (rc == 0) {
                snprintf(resp, sizeof(resp), "{\"ok\":true,\"found\":%d,\"changed\":%d}", f, c);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, resp);
                return ESP_OK;
            } else if (rc == -1) {
                return sens_reply_err(req, "503 Service Unavailable", "busy");
            } else {
                return sens_reply_err(req, "500 Internal Server Error", "no_driver");
            }
        }
        case AF_SENS_CMD_ADD: {
            int rc = af_env_drv_add(cmd.model, cmd.addr, cmd.enabled, &pers);
            if (rc >= 1) {
                snprintf(resp, sizeof(resp), "{\"ok\":true,\"sensor\":%d,\"persisted\":%s}", rc, pers ? "true" : "false");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, resp);
                return ESP_OK;
            } else if (rc == -1) {
                return sens_reply_err(req, "400 Bad Request", "bad_value");
            } else if (rc == -2) {
                return sens_reply_err(req, "409 Conflict", "addr_busy");
            } else if (rc == -3) {
                return sens_reply_err(req, "409 Conflict", "full");
            } else {
                return sens_reply_err(req, "500 Internal Server Error", "no_driver");
            }
        }
        case AF_SENS_CMD_REMOVE: {
            int rc = af_env_drv_remove(cmd.sensor, &pers);
            if (rc == 0) {
                snprintf(resp, sizeof(resp), "{\"ok\":true,\"persisted\":%s}", pers ? "true" : "false");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, resp);
                return ESP_OK;
            } else if (rc == -1) {
                return sens_reply_err(req, "404 Not Found", "no_sensor");
            } else {
                return sens_reply_err(req, "500 Internal Server Error", "no_driver");
            }
        }
        case AF_SENS_CMD_PATCH: {
            int pers_all = 1;
            if (cmd.has_patch) {
                int p1 = 0;
                int rc = af_env_drv_patch(cmd.sensor, &cmd.patch, &p1);
                if (rc == -1) return sens_reply_err(req, "404 Not Found", "no_sensor");
                if (rc == -2) return sens_reply_err(req, "400 Bad Request", "bad_value");
                if (rc == -3) return sens_reply_err(req, "500 Internal Server Error", "no_driver");
                pers_all &= p1;
            }
            if (cmd.has_agg) {
                int p2 = 0;
                int rc = af_env_drv_set_agg(cmd.agg_s, &p2);
                if (rc == -2) return sens_reply_err(req, "400 Bad Request", "bad_value");
                if (rc == -3) return sens_reply_err(req, "500 Internal Server Error", "no_driver");
                pers_all &= p2;
            }
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"persisted\":%s}", pers_all ? "true" : "false");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, resp);
            return ESP_OK;
        }
        default:
            return sens_reply_err(req, "400 Bad Request", "bad_value");
    }
}

static esp_err_t handle_sensors_model_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[160];
    int n = sens_read_body(req, body, sizeof(body));
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");

    uint8_t model = 0, en = 0;
    int pc = af_sens_parse_model(body, (size_t)n, &model, &en);
    if (pc != 0) return sens_parse_fail(req, pc);

    int pers = 0;
    af_log_event(1, "cfg", "sensor model=%u enabled=%u", (unsigned)model, (unsigned)en);
    int rc = af_env_drv_enable_model(model, en, &pers);
    if (rc >= 0) {
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"changed\":%d,\"persisted\":%s}", rc, pers ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    } else if (rc == -1 || rc == -2) {
        return sens_reply_err(req, "400 Bad Request", "bad_value");
    } else {
        return sens_reply_err(req, "500 Internal Server Error", "no_driver");
    }
}
static esp_err_t handle_mqtt_get(httpd_req_t *req)
{
    char *buf = malloc(512);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    af_mqtt_cfg_t c;
    af_mqtt_drv_get_cfg(&c);
    size_t n = af_mqtt_cfg_json(&c, buf, 512);
    if (n == 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, buf, n);
    free(buf);
    return err;
}

static esp_err_t mqtt_bad_value(httpd_req_t *req)
{
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"bad_value\"}");
    return ESP_FAIL;
}

static esp_err_t handle_mqtt_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    int total = req->content_len;
    if (total <= 0 || total >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }

    char body[512];
    int r = httpd_req_recv(req, body, total);
    if (r <= 0 || r != total) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
        return ESP_FAIL;
    }
    body[r] = '\0';
    if (strstr(body, "\\u0000")) return mqtt_bad_value(req);  /* NUL внутри строки cJSON молча обрезает */

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
        return ESP_FAIL;
    }

    af_mqtt_cfg_t c;
    af_mqtt_drv_get_cfg(&c);

    // Обработка поля enabled
    cJSON *item = cJSON_GetObjectItem(root, "enabled");
    if (item) {
        if (!cJSON_IsBool(item) || af_mqtt_cfg_set_enabled(&c, cJSON_IsTrue(item) ? 1 : 0) != 0) {
            cJSON_Delete(root);
            return mqtt_bad_value(req);
        }
    }

    // Обработка числовых полей
    static const char *num_keys[] = {"port", "period_s"};
    for (size_t i = 0; i < sizeof(num_keys) / sizeof(num_keys[0]); i++) {
        item = cJSON_GetObjectItem(root, num_keys[i]);
        if (item) {
            if (!cJSON_IsNumber(item) || af_mqtt_cfg_set_num(&c, num_keys[i], item->valuedouble) != 0) {
                cJSON_Delete(root);
                return mqtt_bad_value(req);
            }
        }
    }

    // Обработка строковых полей
    static const char *str_keys[] = {"host", "user", "pass", "prefix"};
    for (size_t i = 0; i < sizeof(str_keys) / sizeof(str_keys[0]); i++) {
        item = cJSON_GetObjectItem(root, str_keys[i]);
        if (item) {
            if (!cJSON_IsString(item) || item->valuestring == NULL || af_mqtt_cfg_set_str(&c, str_keys[i], item->valuestring) != 0) {
                cJSON_Delete(root);
                return mqtt_bad_value(req);
            }
        }
    }

    cJSON_Delete(root);

    if (!af_mqtt_cfg_valid(&c)) {
        return mqtt_bad_value(req);
    }

    int rc = af_mqtt_drv_set_cfg(&c);
    if (rc != -1) af_log_event(1, "cfg", "mqtt settings changed persisted=%d", rc != -2);
    if (rc == -1) {
        return mqtt_bad_value(req);
    }

    httpd_resp_set_type(req, "application/json");
    if (rc == -2) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":false}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":true}");
    }
    return ESP_OK;
}
static esp_err_t handle_ble_reconnect(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    bool ok = ble_gw_reconnect();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}
static esp_err_t handle_wifi_reset(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    af_log_event(2, "cfg", "wifi credentials reset via web");
    esp_restart();
    return ESP_OK;
}

// ===================== #FIELD-4/5/7: полевой AP — время и пароль =====================

// Нижняя граница вменяемого epoch для /api/time (2023-11-14);
// локальная санитарная граница.
#define FIELD_MIN_EPOCH_S  1700000000LL
#define FIELD_MAX_EPOCH_S  4102444800LL   // 2100-01-01
#define FIELD_AP_URL       "http://192.168.4.1/"

// #FIELD-5 (A2): POST /api/time — установка времени платы от браузера телефона.
// Тело JSON: {"epoch_ms": <ms since 1970 UTC>, "manual": <bool, опц.>}.
// В Outdoor (полевой AP) SNTP не поднят → sntp_synced=false → время идёт от телефона.
// Guard-логика (net_time_should_accept) — в net_time.c (host-тестируема):
//   sntp_synced → отклонить (война источников; T1); manual → принять; auto → |Δ|>5 с.
// При приёме — settimeofday + пометка источника
static esp_err_t handle_time_set(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[128];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body"); return ESP_FAIL; }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jms  = cJSON_GetObjectItem(root, "epoch_ms");
    cJSON *jman = cJSON_GetObjectItem(root, "manual");
    if (!cJSON_IsNumber(jms)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "epoch_ms required");
        return ESP_FAIL;
    }
    int64_t epoch_ms = (int64_t)jms->valuedouble;
    bool manual = cJSON_IsTrue(jman);
    cJSON_Delete(root);

    if (epoch_ms < FIELD_MIN_EPOCH_S * 1000 || epoch_ms > FIELD_MAX_EPOCH_S * 1000) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "epoch out of range");
        return ESP_FAIL;
    }

    struct timeval now;
    gettimeofday(&now, NULL);
    int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
    int64_t dt_abs_sec = llabs(epoch_ms - now_ms) / 1000;

    bool sntp   = net_time_sntp_synced();
    bool accept = net_time_should_accept(sntp, manual, dt_abs_sec);

    if (accept) {
        struct timeval tv = { .tv_sec  = (time_t)(epoch_ms / 1000),
                              .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000) };
        settimeofday(&tv, NULL);
        af_log_event(1, "time", "set from %s", manual ? "manual" : "browser");
        net_time_set_source(manual ? TIME_SRC_MANUAL : TIME_SRC_BROWSER);
        ESP_LOGI(TAG, "FIELD-5: time set from %s (was off %lld s)",
                 manual ? "manual" : "browser", (long long)dt_abs_sec);
    }

    char out[96];
    int n = snprintf(out, sizeof(out),
        "{\"ok\":true,\"accepted\":%s,\"source\":\"%s\",\"sntp\":%s}",
        accept ? "true" : "false", net_time_source_str(), sntp ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, n);
    return ESP_OK;
}

// #FIELD-4: captive-portal probe endpoints. В полевом AP DNS-hijack (wifi_manager)
// заворачивает все имена на 192.168.4.1; OS-пробы связности (Android generate_204/
// gen_204, iOS/macOS hotspot-detect/library-test, Windows ncsi/connecttest) получают
// здесь 302 → всплывает «Вход в сеть», тап открывает Web UI. В STA (Indoor) плата —
// клиент, проба до неё не доходит; для чистоты отвечаем 204 (как настоящий generate_204).
static esp_err_t handle_captive_probe(httpd_req_t *req)
{
    // F1 (итоговое ревью 25.09): классификация теперь идёт ДО дispatch, в
    // общем трамплине (web_server_init) — здесь ничего откатывать не нужно.
    if (wifi_manager_is_ap_mode()) {
        // #FIELD-10/#FIELD-11: вместо 302 на тяжёлый index отдаём лёгкую captive-landing
        // прямо в мини-браузере ОС — крупный адрес 192.168.4.1 (виден сразу) + авто-редирект
        // на страницу прибора + кнопка-fallback. 200+HTML вместо 204 → OS считает «captive
        // portal», всплывает «Вход в сеть» и открывает нашу страницу.
        return handle_captive_page(req);
    }
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// #FIELD-7 (#SEC-2): POST /api/ap-pass — смена пароля полевого AP. Тело {"password":"..."}.
// Пустой → сброс к дефолту (следующий boot возьмёт AP_PASS_DEFAULT). Непустой обязан быть
// ≥8 символов (мин WPA2-PSK). Хранится в NVS wifi/ap_pass (тот же ключ, что читает
// load_ap_config); применяется после ребута (способ A4 — пересоздавать netif/httpd на
// лету рискованно). Пароль общий для всех (#SEC-2): оператор раздаёт SSID+пароль голосом.
static esp_err_t handle_ap_pass(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[128];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body"); return ESP_FAIL; }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jp = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(jp)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password required");
        return ESP_FAIL;
    }
    const char *pass = jp->valuestring;
    size_t plen = strlen(pass);
    if (plen > 0 && plen < 8) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password must be >=8 chars (WPA2)");
        return ESP_FAIL;
    }
    if (plen >= WIFI_PASS_MAX) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "password too long");
        return ESP_FAIL;
    }

    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        if (plen == 0) nvs_erase_key(nvs, "ap_pass");   // сброс к дефолту (not-found ок)
        else           nvs_set_str(nvs, "ap_pass", pass);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    af_log_event(2, "cfg", "ap password changed via web");
    esp_restart();
    return ESP_OK;
}

// #FIELD-6 (T4/T5): переключатель Indoor/Outdoor со стартовой страницы. Тело
// {"mode":"indoor"|"outdoor"}. outdoor → NVS ap_mode=1 (липкий forced AP); indoor →
// снять ap_mode (ребут в STA). Применяется ребутом (способ A4). Тот же ключ ap_mode,
// что читает wifi_manager на старте (FIELD-2b) и ставит setup-кнопка (FIELD-2c).
static esp_err_t handle_net_mode(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    char body[64];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body"); return ESP_FAIL; }
    body[len] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }
    cJSON *jm = cJSON_GetObjectItem(root, "mode");
    const char *mode = cJSON_IsString(jm) ? jm->valuestring : NULL;
    bool outdoor;
    if (mode && strcmp(mode, "outdoor") == 0)      outdoor = true;
    else if (mode && strcmp(mode, "indoor") == 0)  outdoor = false;
    else { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode must be indoor|outdoor"); return ESP_FAIL; }
    cJSON_Delete(root);

    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        if (outdoor) nvs_set_u8(nvs, "ap_mode", 1);
        else         nvs_erase_key(nvs, "ap_mode");   // → Indoor (not-found ок)
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    af_log_event(2, "cfg", "net mode changed via web (%s)", outdoor ? "outdoor" : "indoor");
    esp_restart();
    return ESP_OK;
}


static esp_err_t handle_push_get(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    int n = af_push_get_json(buf, 2048);
    if (n < 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, buf, n);
    free(buf);
    return e;
}

static esp_err_t handle_push_diag(httpd_req_t *req)
{
    char buf[768];
    int n = af_push_get_diag(buf, sizeof(buf));
    if (n < 1 || (size_t)n >= sizeof(buf) || buf[n - 1] != '}')
        return sens_reply_err(req, "503 Service Unavailable", "diag_unavailable");
    size_t room = sizeof(buf) - (size_t)(n - 1);
    int extra = snprintf(buf + n - 1, room,
        ",\"http_nodelay_ok\":%" PRIu32 ",\"http_nodelay_fail\":%" PRIu32 "}",
        s_http_nodelay_ok, s_http_nodelay_fail);
    if (extra < 0 || (size_t)extra >= room)
        return sens_reply_err(req, "503 Service Unavailable", "diag_unavailable");
    n = n - 1 + extra;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t handle_push_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char body[384];
    int n = sens_read_body(req, body, sizeof body);
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");

    af_push_cfg_t cur;
    af_push_get_cfg(&cur);
    af_push_cmd_t cmd;
    int pc = af_push_parse(body, (size_t)n, &cur, &cmd);
    memset(body, 0, sizeof body);

    if (pc != 0) {
        memset(&cur, 0, sizeof cur);
        memset(&cmd, 0, sizeof cmd);
        if (pc == AF_PUSH_E_BODY) return sens_reply_err(req, "400 Bad Request", "bad_body");
        if (pc == AF_PUSH_E_JSON) return sens_reply_err(req, "400 Bad Request", "json");
        if (pc == AF_PUSH_E_KEY) return sens_reply_err(req, "400 Bad Request", "unknown_key");
        if (pc == AF_PUSH_E_VALUE) return sens_reply_err(req, "400 Bad Request", "bad_value");
        if (pc == AF_PUSH_E_INCOMPLETE) return sens_reply_err(req, "409 Conflict", "incomplete");
        return sens_reply_err(req, "400 Bad Request", "bad_value");
    }

    int rc = af_push_apply(&cmd);
    memset(&cur, 0, sizeof cur);
    memset(&cmd, 0, sizeof cmd);

    if (rc == -1) return sens_reply_err(req, "503 Service Unavailable", "not_ready");

    httpd_resp_set_type(req, "application/json");
    if (rc == 0) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true,\"persisted\":false}");
    }
    return ESP_OK;
}

static esp_err_t handle_push_test(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    if (req->content_len > 0) {
        char tb[64];
        int n = sens_read_body(req, tb, sizeof tb);
        if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");
        if (!af_sens_body_ok(tb, (size_t)n) || !(n == 2 && memcmp(tb, "{}", 2) == 0)) {
            return sens_reply_err(req, "400 Bad Request", "bad_body");
        }
    }

    int rc = af_push_test_start();
    if (rc == 0) {
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":true,\"started\":true}");
        return ESP_OK;
    }
    if (rc == -1) return sens_reply_err(req, "409 Conflict", "busy");
    if (rc == -2) return sens_reply_err(req, "409 Conflict", "incomplete");
    return sens_reply_err(req, "503 Service Unavailable", "not_ready");
}

static esp_err_t handle_push_reset(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;

    char rb[64];
    int n = sens_read_body(req, rb, sizeof rb);
    if (n < 0) return sens_reply_err(req, "400 Bad Request", "bad_body");
    if (!af_sens_body_ok(rb, (size_t)n)) return sens_reply_err(req, "400 Bad Request", "bad_body");

    cJSON *root = cJSON_ParseWithLength(rb, (size_t)n);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return sens_reply_err(req, "400 Bad Request", "json");
    }

    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "confirm");
    if (!(cJSON_GetArraySize(root) == 1 && cJSON_IsTrue(c))) {
        cJSON_Delete(root);
        return sens_reply_err(req, "400 Bad Request", "confirm_required");
    }

    cJSON_Delete(root);
    int rc = af_push_reset();
    if (rc != 0) return sens_reply_err(req, "503 Service Unavailable", "not_ready");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}
// e16: журнал событий и очистка истории; CSRF проверяется здесь, разбор тела и действие - в af_log_http.c
static esp_err_t handle_log_clear_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    return af_log_http_clear(req);
}

static esp_err_t handle_history_clear_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;
    return af_history_http_clear(req);
}

static esp_err_t handle_meas_post(httpd_req_t *req)
{
    if (!csrf_check(req)) return ESP_FAIL;   // замеры: все POST под CSRF, как у прочих
    return af_meas_http_post(req);
}
static esp_err_t httpd_session_open_nodelay(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    const int enabled = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0) {
        const int saved_errno = errno;
        if (s_http_nodelay_fail < UINT32_MAX) s_http_nodelay_fail++;
        ESP_LOGW(TAG, "TCP_NODELAY failed fd=%d errno=%d", sockfd, saved_errno);
    } else if (s_http_nodelay_ok < UINT32_MAX) {
        s_http_nodelay_ok++;
    }
    return ESP_OK;
}

bool web_server_init(void)
{
    csrf_generate();
    http_io_gate_init();
    af_meas_drv_init();   // замеры: ядро и мьютекс до регистрации маршрутов
    af_auth_init();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 1;
    config.max_uri_handlers = 72;
    config.stack_size = 8192;
    config.max_open_sockets = HTTPD_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.open_fn = httpd_session_open_nodelay;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }

    const httpd_uri_t uris[] = {
        {"/api/csrf-token",              HTTP_GET,  handle_csrf_token,       NULL},
        {"/api/status",                  HTTP_GET,  handle_status,           NULL},
        {"/api/system",                  HTTP_GET,  handle_system,           NULL},
        {"/api/debug/log/meta",          HTTP_GET,  handle_debug_log_meta,   NULL},
        {"/api/debug/log",               HTTP_GET,  handle_debug_log_get,    NULL},
        {"/api/debug/log/flush",         HTTP_POST, handle_debug_log_flush,  NULL},
        {"/api/debug/log/config",        HTTP_GET,  handle_debug_log_meta,   NULL},
        {"/api/debug/log/config",        HTTP_POST, handle_debug_log_config_set, NULL},
        {"/api/ota",                     HTTP_POST, handle_ota,              NULL},
        {"/api/ota/github/check",        HTTP_GET,  handle_ota_gh_check,     NULL},
        {"/api/ota/github/install",      HTTP_POST, handle_ota_gh_install,   NULL},
        {"/api/ota/github/progress",     HTTP_GET,  handle_ota_gh_progress,  NULL},
        {"/api/ota/github/channel",      HTTP_GET,  handle_ota_gh_channel_get, NULL},
        {"/api/ota/github/channel",      HTTP_POST, handle_ota_gh_channel_set, NULL},
        {"/api/wifi/reset",              HTTP_POST, handle_wifi_reset,       NULL},
        {"/api/reboot-esp",              HTTP_POST, handle_reboot_esp,       NULL},
        {"/api/time",                    HTTP_POST, handle_time_set,         NULL},
        {"/api/history.csv",             HTTP_GET,  af_history_csv_handler,  NULL},
        {"/api/history/status",          HTTP_GET,  af_history_status_handler, NULL},
        {"/api/settings",                HTTP_GET,  af_settings_get_handler,  NULL},
        {"/api/settings",                HTTP_POST, af_settings_post_handler, NULL},
        {"/api/sensors",                 HTTP_GET,  handle_sensors_get,      NULL},
        {"/api/sensors",                 HTTP_POST, handle_sensors_post,     NULL},
        {"/api/sensors/model",           HTTP_POST, handle_sensors_model_post, NULL},
        {"/api/mqtt",                    HTTP_GET,  handle_mqtt_get,         NULL},
        {"/api/mqtt",                    HTTP_POST, handle_mqtt_post,        NULL},
        {"/api/ble-reconnect",           HTTP_POST, handle_ble_reconnect,    NULL},
        {"/api/ap-pass",                 HTTP_POST, handle_ap_pass,          NULL},
        {"/api/net-mode",                HTTP_POST, handle_net_mode,         NULL},
        {"/generate_204",                HTTP_GET,  handle_captive_probe,    NULL},
        {"/gen_204",                     HTTP_GET,  handle_captive_probe,    NULL},
        {"/hotspot-detect.html",         HTTP_GET,  handle_captive_probe,    NULL},
        {"/library/test/success.html",   HTTP_GET,  handle_captive_probe,    NULL},
        {"/ncsi.txt",                    HTTP_GET,  handle_captive_probe,    NULL},
        {"/connecttest.txt",             HTTP_GET,  handle_captive_probe,    NULL},
        {"/captive",                     HTTP_GET,  handle_captive_page,     NULL},
        {"/healthcheck",                 HTTP_GET,  handle_healthcheck,      NULL},
        {"/common-time.js",              HTTP_GET,  handle_common_time_js,   NULL},
        {"/history",                     HTTP_GET,  handle_history_page,     NULL},
        {"/settings",                    HTTP_GET,  handle_history_page,     NULL},  // e13: тот же ассет, вид выбирает путь
        {"/readings",                    HTTP_GET,  handle_history_page,     NULL},  // оператор: вкладка «Показания» (алиас /settings)
        {"/system",                      HTTP_GET,  handle_history_page,     NULL},
        {"/measure",                     HTTP_GET,  handle_history_page,     NULL},  // e16d: вкладка «Измерения» (тот же ассет)
        {"/series",                      HTTP_GET,  handle_history_page,     NULL},  // e16d: вкладка «Агрегаты»
        {"/",                            HTTP_GET,  handle_history_page,     NULL},  // e13: Главная = график (тот же ассет, вид по пути)
        {"/api/ble",                     HTTP_GET,  af_ble_http_stats,        NULL},  // e16: подробная статистика BLE
        {"/api/ble/sessions",            HTTP_GET,  af_ble_http_sessions,     NULL},
        {"/api/ble/cands",               HTTP_GET,  af_ble_sel_cands_handler,  NULL},
        {"/api/ble/select",              HTTP_POST, af_ble_sel_select_handler, NULL},
        {"/api/ble/rescan",              HTTP_POST, af_ble_sel_rescan_handler, NULL},
        {"/api/ble/forget",              HTTP_POST, af_ble_sel_forget_handler, NULL},
        {"/api/ble/cancel",              HTTP_POST, af_ble_sel_cancel_handler, NULL},
        {"/api/ble/manual",              HTTP_POST, af_ble_sel_manual_handler, NULL},
        {"/api/ble/sessions.csv",        HTTP_GET,  af_ble_http_sessions_csv, NULL},
        {"/api/push",                    HTTP_GET,  handle_push_get,          NULL},  // Э8: отправка истории на приёмник
        {"/api/push/diag",               HTTP_GET,  handle_push_diag,         NULL},
        {"/api/push",                    HTTP_POST, handle_push_post,         NULL},
        {"/api/push/test",               HTTP_POST, handle_push_test,         NULL},
        {"/api/push/reset",              HTTP_POST, handle_push_reset,        NULL},
        {"/api/log",                     HTTP_GET,  af_log_http_get,          NULL},  // e16: журнал событий (NDJSON)
        {"/api/log.txt",                 HTTP_GET,  af_log_http_txt,          NULL},  // e16: скачивание журнала файлом
        {"/api/log/clear",               HTTP_POST, handle_log_clear_post,    NULL},  // e16: очистка журнала (CSRF + подтверждение)
        {"/api/history/clear",           HTTP_POST, handle_history_clear_post, NULL},  // e16: очистка истории по кольцам
        {"/api/meas",                    HTTP_GET,  af_meas_http_get,         NULL},  // замеры: состояние, журнал прогонов
        {"/api/meas/results",            HTTP_GET,  af_meas_http_results,     NULL},
        {"/api/meas/journal",            HTTP_GET,  af_meas_http_journal,     NULL},  // e16f (T-38): расчёт замера по журналу
        {"/api/meas/results.csv",        HTTP_GET,  af_meas_http_csv,         NULL},
        {"/api/meas/*",                  HTTP_POST, handle_meas_post,         NULL},  // start|stop|pause|resume|delete
    };

    _Static_assert(sizeof(uris) / sizeof(uris[0]) <= WEB_SERVER_URI_MAX,
                   "WEB_SERVER_URI_MAX меньше числа записей uris[]");

    bool ready = true;
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        s_wrap_handlers[i] = uris[i].handler;
        httpd_uri_t entry = uris[i];
        entry.handler = activity_trampoline;
        entry.user_ctx = (void *)(intptr_t)i;
        esp_err_t rerr = httpd_register_uri_handler(server, &entry);
        if (rerr != ESP_OK) {
            ESP_LOGE(TAG, "issue#52b: register '%s' failed: %s", uris[i].uri, esp_err_to_name(rerr));
            ready = false;
        }
    }

    if (httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handle_404) != ESP_OK) ready = false;
    if (httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, handle_404) != ESP_OK) ready = false;
    ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    return ready;
}
