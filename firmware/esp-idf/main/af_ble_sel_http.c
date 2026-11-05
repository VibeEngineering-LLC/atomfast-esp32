/* main/af_ble_sel_http.c — HTTP-роутеры BLE-селекции AtomFast (ESP-IDF 5.4, C11) */

#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "af_http_body.h"
#include "cJSON.h"

#include "ble_sel.h"
#include "ble_gw_sel.h"
#include "ble_addr.h"
#include "af_ble_sel_http.h"

/* ---------- локальные хелперы ---------- */

static int csrf_check(httpd_req_t *req)
{
    return web_server_csrf_ok(req) ? 0 : -1;
}

/* ---------- отображение кода -> (HTTP статус, строка ошибки) ---------- */

static int ble_sel_http_map(int rc, const char **status, const char **err)
{
    switch (rc) {
    case BLE_OK:
        *status = "200 OK";
        *err    = NULL;
        return 200;
    case BLE_DEFERRED:
        *status = "202 Accepted";
        *err    = NULL;
        return 202;
    case BLE_E_NEED_REPLACE:
        *status = "409 Conflict";
        *err    = "need_replace";
        return 409;
    case BLE_E_NEED_CLEAR:
        *status = "409 Conflict";
        *err    = "need_clear";
        return 409;
    case BLE_E_MEAS_ACTIVE:
        *status = "409 Conflict";
        *err    = "meas_active";
        return 409;
    case BLE_E_STALE:
        *status = "409 Conflict";
        *err    = "stale";
        return 409;
    case BLE_E_COOLDOWN:
        *status = "409 Conflict";
        *err    = "cooldown";
        return 409;
    case BLE_E_BUSY:
        *status = "409 Conflict";
        *err    = "busy";
        return 409;
    case BLE_E_LIVE:
        *status = "409 Conflict";
        *err    = "live";
        return 409;
    case BLE_E_NO_CAND:
        *status = "404 Not Found";
        *err    = "no_cand";
        return 404;
    case BLE_E_BAD:
        *status = "400 Bad Request";
        *err    = "bad_body";
        return 400;
    default:
        *status = "500 Internal Server Error";
        *err    = "internal";
        return 500;
    }
}

/* отправка результата по коду ble_gw_sel_* */
static esp_err_t ble_sel_send(httpd_req_t *req, int rc, uint32_t records)
{
    const char *status, *err;
    int code = ble_sel_http_map(rc, &status, &err);

    if (code == 200) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (code == 202) {
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true,\"deferred\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (code == 409 && err && strcmp(err, "need_clear") == 0) {
        int cnt = (int)records;
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "{\"error\":\"need_clear\",\"records\":%d}", cnt);
        if (n < 0) n = 0;
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, buf, (size_t)n);
        return ESP_OK;
    }
    return af_http_send_error(req, status, err ? err : "internal");
}

/* ---------- GET /api/ble/cands ---------- */

esp_err_t af_ble_sel_cands_handler(httpd_req_t *req)
{
    char buf[2048];
    int n = ble_gw_sel_json(buf, sizeof(buf));
    if (n < 0) {
        af_http_send_error(req, "503 Service Unavailable", "not_ready");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, (size_t)n);
    return ESP_OK;
}

/* ---------- POST /api/ble/select ---------- */

esp_err_t af_ble_sel_select_handler(httpd_req_t *req)
{
    if (csrf_check(req) != 0) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int len = af_http_read_body(req, body, sizeof(body));
    if (len < 0) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *jgen  = cJSON_GetObjectItem(root, "gen");
    cJSON *jtok  = cJSON_GetObjectItem(root, "tok");
    cJSON *jcc   = cJSON_GetObjectItem(root, "confirm_clear");

    if (!cJSON_IsNumber(jgen) || !cJSON_IsNumber(jtok) || (jcc != NULL && !cJSON_IsBool(jcc))) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    double dg = jgen->valuedouble;
    double dt = jtok->valuedouble;
    if (dg < 0.0 || dg > 2147483647.0 || dg != (double)(int)dg || dt < 1.0 || dt > 255.0 || dt != (double)(int)dt) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int gen  = (int)dg;
    int tok  = (int)dt;
    int cc   = cJSON_IsTrue(jcc) ? 1 : 0;
    cJSON_Delete(root);

    uint32_t rings = 0;
    int rc = ble_gw_sel_select(gen, tok, cc, &rings);
    return ble_sel_send(req, rc, rings);
}

/* ---------- POST /api/ble/rescan ---------- */

esp_err_t af_ble_sel_rescan_handler(httpd_req_t *req)
{
    if (csrf_check(req) != 0) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int len = af_http_read_body(req, body, sizeof(body));
    if (len < 0) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *jrep = cJSON_GetObjectItem(root, "replace");
    if (!cJSON_IsBool(jrep)) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    int rep = cJSON_IsTrue(jrep) ? 1 : 0;
    cJSON_Delete(root);

    uint32_t gen_out = 0;
    int rc = ble_gw_sel_rescan(rep, &gen_out);
    if (rc == BLE_OK) {
        int g = (int)gen_out;
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"gen\":%d}", g);
        if (n < 0) n = 0;
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, buf, (size_t)n);
        return ESP_OK;
    }
    return ble_sel_send(req, rc, 0);
}

/* ---------- POST /api/ble/forget ---------- */

esp_err_t af_ble_sel_forget_handler(httpd_req_t *req)
{
    if (csrf_check(req) != 0) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int len = af_http_read_body(req, body, sizeof(body));
    if (len < 0) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *jc = cJSON_GetObjectItem(root, "confirm");
    if (!cJSON_IsBool(jc)) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    int confirm = cJSON_IsTrue(jc) ? 1 : 0;
    cJSON_Delete(root);

    int rc = ble_gw_sel_forget(confirm);
    return ble_sel_send(req, rc, 0);
}

/* ---------- POST /api/ble/cancel ---------- */

esp_err_t af_ble_sel_cancel_handler(httpd_req_t *req)
{
    if (csrf_check(req) != 0) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    char body[64];
    if (req->content_len > 0 && req->content_len < sizeof(body)) {
        if (af_http_read_body(req, body, sizeof(body)) < 0) {
            af_http_send_error(req, "400 Bad Request", "bad_body");
            return ESP_OK;
        }
    }

    int rc = ble_gw_sel_cancel();
    return ble_sel_send(req, rc, 0);
}

/* ---------- POST /api/ble/manual ---------- */

esp_err_t af_ble_sel_manual_handler(httpd_req_t *req)
{
    if (csrf_check(req) != 0) {
        af_http_send_error(req, "403 Forbidden", "csrf");
        return ESP_OK;
    }

    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int len = af_http_read_body(req, body, sizeof(body));
    if (len < 0) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    cJSON *jaddr = cJSON_GetObjectItem(root, "addr");
    cJSON *jatyp = cJSON_GetObjectItem(root, "atype");
    cJSON *jcc   = cJSON_GetObjectItem(root, "confirm_clear");

    if (!cJSON_IsString(jaddr) || !cJSON_IsNumber(jatyp) || (jcc != NULL && !cJSON_IsBool(jcc))) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    /* b29 G (b26 F10): только целое 0 или 1 до приведения типа: 0.5, -0.5, 1e300, NaN -> 400
     * (раньше (int)0.5 == 0 принималось, слишком большое число было UB приведения) */
    double da = jatyp->valuedouble;
    if (!(da == 0.0 || da == 1.0)) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }
    int atype = (int)da;
    if (atype < 0 || atype > 1) {
        cJSON_Delete(root);
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    int cc = cJSON_IsTrue(jcc) ? 1 : 0;
    uint8_t addr[6];
    bool addr_ok = ble_addr_parse(jaddr->valuestring, addr);
    cJSON_Delete(root);
    if (!addr_ok) {
        af_http_send_error(req, "400 Bad Request", "bad_body");
        return ESP_OK;
    }

    uint32_t rings = 0;
    int rc = ble_gw_sel_manual(addr, atype, cc, &rings);
    return ble_sel_send(req, rc, rings);
}
