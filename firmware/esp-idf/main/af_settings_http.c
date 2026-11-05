/* main/af_settings_http.c — HTTP-настройки AtomFast (ESP-IDF 5.4, C11) */

#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "af_http_body.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "af_settings.h"
#include "af_store.h"
#include "af_persist_fs.h"
#include "af_clear.h"
#include "af_settings_http.h"
#include "af_ble_sel_http.h" /* web_server_csrf_ok */
#include "ble_gw_stats.h"

/* ---------- локальные хелперы (аналоги lg_* из общего модуля) ---------- */

/* b29 G: ошибка с указанием шага POST /api/settings, на котором она произошла */
static esp_err_t st_send_err_step(httpd_req_t *req, const char *status, const char *code_str, const char *step)
{
    char body[128];
    int n = snprintf(body, sizeof(body), "{\"error\":\"%s\",\"step\":\"%s\"}", code_str, step);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, (size_t)n);
    return ESP_OK;
}

/* ---------- GET /api/settings ---------- */

esp_err_t af_settings_get_handler(httpd_req_t *req)
{
    af_settings_t s;
    af_settings_defaults(&s);

    int r = 0, a = 0, e = 0;
    af_store_get_enabled(&r, &a, &e);
    s.store_raw = r;
    s.store_agg = a;
    s.store_env = e;

    af_store_status_t st;
    af_store_get_status(&st);
    s.window_s = st.window_s;

    /* sigma_k / ble_ext из NVS afset; нет ключа — умолчание из af_settings_defaults */
    nvs_handle_t h;
    if (nvs_open("afset", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "sig_k", &v) == ESP_OK) s.sigma_k = v;
        if (nvs_get_u8(h, "ble_ext", &v) == ESP_OK) s.ble_ext = v;
        nvs_close(h);
    }

    af_settings_sanitize(&s);

    char buf[256];
    int n = af_settings_json_get(&s, buf, sizeof(buf));
    if (n < 0) {
        af_http_send_error(req, "500 Internal Server Error", "json_fail");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, (size_t)n);
    return ESP_OK;
}

/* ---------- POST /api/settings ---------- */

esp_err_t af_settings_post_handler(httpd_req_t *req)
{
    char body[256];

    if (!web_server_csrf_ok(req)) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        af_http_send_error(req, "413 Request Entity Too Large", "body too large");
        return ESP_OK;
    }

    int len = af_http_read_body(req, body, sizeof(body));
    if (len < 0) {
        af_http_send_error(req, "400 Bad Request", "syntax");
        return ESP_OK;
    }

    /* разбор */
    af_settings_patch_t patch;
    memset(&patch, 0, sizeof(patch));
    int code = af_settings_parse(body, (size_t)len, &patch);
    if (code < 0) {
        char jbuf[256];
        int jn = af_settings_json_error(code, patch.err_field, jbuf, sizeof(jbuf));
        if (jn < 0) {
            af_http_send_error(req, "400 Bad Request", "bad request");
            return ESP_OK;
        }
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, jbuf, (size_t)jn);
        return ESP_OK;
    }

    /* текущие настройки (аналогично GET) */
    af_settings_t cur;
    af_settings_defaults(&cur);

    int r = 0, a = 0, e = 0;
    af_store_get_enabled(&r, &a, &e);
    cur.store_raw = r;
    cur.store_agg = a;
    cur.store_env = e;

    af_store_status_t st;
    af_store_get_status(&st);
    cur.window_s = st.window_s;

    nvs_handle_t h;
    if (nvs_open("afset", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "sig_k", &v) == ESP_OK) cur.sigma_k = v;
        if (nvs_get_u8(h, "ble_ext", &v) == ESP_OK) cur.ble_ext = v;
        nvs_close(h);
    }
    af_settings_sanitize(&cur);

    /* применить патч */
    uint32_t agg_count = st.agg.count;
    uint32_t cleared = 0;
    af_settings_t out;
    int rc = af_settings_apply(&cur, &patch, agg_count, &out, &cleared);

    if (rc == AF_SET_NEED_CONFIRM) {
        char jbuf[256];
        int jn = af_settings_json_need_confirm(agg_count, cur.window_s, jbuf, sizeof(jbuf));
        if (jn < 0) {
            af_http_send_error(req, "500 Internal Server Error", "json_fail");
            return ESP_OK;
        }
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, jbuf, (size_t)jn);
        return ESP_OK;
    }

    if (rc < 0) {
        char jbuf[256];
        int jn = af_settings_json_error(rc, patch.err_field, jbuf, sizeof(jbuf));
        if (jn < 0) {
            af_http_send_error(req, "400 Bad Request", "apply_failed");
            return ESP_OK;
        }
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, jbuf, (size_t)jn);
        return ESP_OK;
    }

    /* --- применить изменения ---
     * b29 G (B01, P2 №9, №10): шаги идут от обратимых к необратимому; каждый шаг после ответа
     * оставляет RAM == NVS для своих параметров. Отказ шага -> ошибка с полем step, следующие шаги
     * не выполняются, уже выполненные остаются в силе (POST не транзакция; таблица - отчёт b29-stream-G).
     * 1) флаги хранения; 2) sigma_k/ble_ext; 3) окно: set_window -> clear agg -> NVS win_s, откат окна
     * в RAM при отказе очистки или NVS. Очистка последняя: отказ шагов 1-2 не стирает агрегаты. */

    /* 1) флаги хранения: только изменившиеся; отказ NVS -> ошибка, RAM прежняя (af_store.c) */
    int fr = (out.store_raw != cur.store_raw) ? (int)out.store_raw : -1;
    int fa = (out.store_agg != cur.store_agg) ? (int)out.store_agg : -1;
    int fe = (out.store_env != cur.store_env) ? (int)out.store_env : -1;
    if (fr != -1 || fa != -1 || fe != -1) {
        if (af_store_set_enabled(fr, fa, fe) != ESP_OK) {
            st_send_err_step(req, "500 Internal Server Error", "nvs_failed", "store");
            return ESP_OK;
        }
    }

    /* 2) sigma_k / ble_ext в NVS afset — только изменившиеся поля */
    int need_sigma = (out.sigma_k != cur.sigma_k);
    int need_ble = (out.ble_ext != cur.ble_ext);
    if (need_sigma || need_ble) {
        if (nvs_open("afset", NVS_READWRITE, &h) != ESP_OK) {
            st_send_err_step(req, "500 Internal Server Error", "nvs_failed", "afset");
            return ESP_OK;
        }
        esp_err_t nerr = ESP_OK;
        if (need_sigma) nerr = nvs_set_u8(h, "sig_k", out.sigma_k);
        if (nerr == ESP_OK && need_ble) nerr = nvs_set_u8(h, "ble_ext", out.ble_ext);
        if (nerr == ESP_OK) nerr = nvs_commit(h);
        nvs_close(h);
        if (nerr != ESP_OK) {
            st_send_err_step(req, "500 Internal Server Error", "nvs_failed", "afset");
            return ESP_OK;
        }
    }
    if (need_ble) {
        ble_gw_set_ext(out.ble_ext != 0); /* П.10: действует сразу */
    }

    /* 3) смена окна (спека П.3:76): (а) окно в RAM -> открытое окно старого периода сброшено, агрегат
     * старого периода закрыться больше не может; (б) очистка agg: её снимок seq берётся ПОСЛЕ (а), поэтому
     * удаляются все записи старого периода (b26 B01: при обратном порядке store_task закрывал окно, пока
     * af_pfs_clear пишет флеш без s_mtx, и запись старого периода оставалась при новом окне); (в) NVS win_s.
     * Отказ (б) или (в) -> окно в RAM возвращается к прежнему (RAM == NVS), ответ 5xx. */
    if (out.window_s != cur.window_s) {
        esp_err_t we = af_store_set_window(out.window_s);
        if (we != ESP_OK) {
            st_send_err_step(req, "500 Internal Server Error", "set_window_failed", "window");
            return ESP_OK;
        }
        af_pfs_clear_res_t pres;
        memset(&pres, 0, sizeof(pres));
        int prc = af_pfs_clear(AF_CLR_MASK_AGG, &pres);
        if (prc != 0) {
            /* -1 занято / -3 маркер: кольцо не тронуто; -2 поздний отказ: RAM-кольцо agg уже очищено, маркер clr.bin остаётся и дочищает флеш при загрузке (af_persist_fs.c:909-913) */
            (void)af_store_set_window((uint16_t)cur.window_s);
            if (prc == -1) {
                st_send_err_step(req, "503 Service Unavailable", "busy", "window");
            } else {
                st_send_err_step(req, "500 Internal Server Error", "clear_failed", "window");
            }
            return ESP_OK;
        }
        esp_err_t nrc = nvs_open("afset", NVS_READWRITE, &h);
        if (nrc == ESP_OK) {
            nrc = nvs_set_u16(h, "win_s", out.window_s);
            if (nrc == ESP_OK) nrc = nvs_commit(h);
            nvs_close(h);
        }
        if (nrc != ESP_OK) {
            /* агрегаты уже стёрты (пользователь подтвердил); окно в RAM -> прежнее, как в NVS: повтор того же POST снова пройдёт все шаги (b26 L2-03: раньше повтор отвечал ok без записи) */
            (void)af_store_set_window((uint16_t)cur.window_s);
            st_send_err_step(req, "500 Internal Server Error", "nvs_failed", "window");
            return ESP_OK;
        }
    }

    /* 4) успешный ответ */
    char jbuf[256];
    int jn = af_settings_json_ok(cleared, jbuf, sizeof(jbuf));
    if (jn < 0) {
        af_http_send_error(req, "500 Internal Server Error", "json_fail");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jbuf, (size_t)jn);
    return ESP_OK;
}
