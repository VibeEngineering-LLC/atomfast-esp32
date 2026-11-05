#include "af_meas_jrn_http.h"
#include "af_mjrn.h"
#include "af_meas_drv.h"
#include "af_meas.h"
#include "af_store.h"
#include "af_rec.h"
#include "af_hist_query.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JRN_E_STORE (-20)
#define JRN_E_NOMEM (-21)

static esp_err_t jerr(httpd_req_t *req, const char *status, const char *code)
{
    char buf[64];
    snprintf(buf, sizeof buf, "{\"ok\":false,\"err\":\"%s\"}", code);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

static esp_err_t jrc_err(httpd_req_t *req, int rc)
{
    const char *status = "400 Bad Request";
    const char *code = "bad_value";

    switch (rc) {
    case AF_MJRN_E_RANGE:
        status = "400 Bad Request";
        code = "bad_range";
        break;
    case AF_MJRN_E_NODATA:
        status = "409 Conflict";
        code = "no_data";
        break;
    case AF_MJRN_E_LONG:
        status = "400 Bad Request";
        code = "too_long";
        break;
    case JRN_E_STORE:
        status = "503 Service Unavailable";
        code = "store_busy";
        break;
    case JRN_E_NOMEM:
        status = "503 Service Unavailable";
        code = "no_mem";
        break;
    case AF_MEAS_E_BUSY:
        status = "409 Conflict";
        code = "busy";
        break;
    case AF_MEAS_E_NOMEM:
        status = "503 Service Unavailable";
        code = "no_mem";
        break;
    case AF_MEAS_E_VALUE:
        status = "400 Bad Request";
        code = "bad_value";
        break;
    default:
        status = "400 Bad Request";
        code = "bad_value";
        break;
    }

    return jerr(req, status, code);
}

int af_meas_journal_compute(uint32_t from, uint32_t to, af_meas_res_t *res, uint32_t *gaps)
{
    if (from == 0 || to <= from || (to - from) > AF_MJRN_MAX_S) {
        return AF_MJRN_E_RANGE;
    }

    af_hq_params_t hp;
    af_hq_init(&hp);
    hp.kind = AF_HQ_KIND_RAW;
    hp.from = from;
    hp.has_from = 1;
    hp.to = to;
    hp.has_to = 1;

    af_hq_plan_t plan;
    uint32_t head = 0, epoch = 0;
    int prc = af_store_plan(&hp, &plan, &head, &epoch);
    if (prc == -1) {
        return JRN_E_STORE;
    }
    if (prc == -2) {
        return AF_MJRN_E_NODATA;
    }

    uint8_t *rec = heap_caps_malloc(32 * AF_REC_RAW_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int from_psram = 1;
    if (rec == NULL) {
        rec = malloc(32 * AF_REC_RAW_SIZE);
        from_psram = 0;
    }
    if (rec == NULL) {
        return JRN_E_NOMEM;
    }

    af_mjrn_t acc;
    af_mjrn_init(&acc, from, to);
    uint32_t cur = plan.start;
    unsigned chunks = 0;

    while (1) {
        uint32_t want = af_hq_next_n(cur, plan.end, 0xFFFFFFFFu, 32);
        if (want == 0) {
            break;
        }

        uint32_t next = 0, lost = 0;
        uint32_t n = af_store_read(AF_HQ_KIND_RAW, cur, rec, want, &next, &lost);
        if (n == 0) {
            break;
        }

        for (uint32_t i = 0; i < n; i++) {
            af_raw_rec_t r;
            if (af_raw_decode(rec + i * AF_REC_RAW_SIZE, &r) != 0) {
                continue;
            }
            (void)af_mjrn_add(&acc, r.ts, r.s.pulses_2s, r.s.doserate_usvh);
        }

        if (next == cur) {
            break;
        }
        cur = next;

        if ((++chunks & 31u) == 0) {
            vTaskDelay(1);
        }
    }

    if (from_psram) {
        heap_caps_free(rec);
    } else {
        free(rec);
    }

    if (gaps != NULL) {
        *gaps = acc.gaps;
    }

    return af_mjrn_finish(&acc, res);
}

esp_err_t af_meas_http_journal(httpd_req_t *req)
{
    int qlen = httpd_req_get_url_query_len(req);
    if (qlen <= 0 || qlen >= 63) {
        return jerr(req, "400 Bad Request", "bad_query");
    }

    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK) {
        return jerr(req, "400 Bad Request", "bad_query");
    }

    char v[16];
    uint32_t from = 0, to = 0;

    if (httpd_query_key_value(q, "from", v, sizeof v) != ESP_OK ||
        af_hq_parse_u32(v, &from) != 0) {
        return jerr(req, "400 Bad Request", "bad_value");
    }

    if (httpd_query_key_value(q, "to", v, sizeof v) != ESP_OK ||
        af_hq_parse_u32(v, &to) != 0) {
        return jerr(req, "400 Bad Request", "bad_value");
    }

    af_meas_res_t res;
    uint32_t gaps = 0;
    int rc = af_meas_journal_compute(from, to, &res, &gaps);
    if (rc != 0) {
        return jrc_err(req, rc);
    }

    char js[768];
    int len = af_meas_fmt_res_json(&res, js, sizeof js);
    if (len < 0) {
        return jerr(req, "500 Internal Server Error", "fmt");
    }

    char out[896];
    snprintf(out, sizeof out,
             "{\"ok\":true,\"from\":%u,\"to\":%u,\"gaps\":%u,\"res\":%s}",
             (unsigned)from, (unsigned)to, (unsigned)gaps, js);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

esp_err_t af_meas_http_journal_save(httpd_req_t *req, const char *body, size_t n)
{
    cJSON *root = cJSON_ParseWithLength(body, n);
    if (root == NULL) {
        return jerr(req, "400 Bad Request", "json");
    }

    uint32_t from = 0, to = 0;
    char name[AF_MEAS_NAME_MAX] = "";

    cJSON *jfrom = cJSON_GetObjectItem(root, "from");
    if (jfrom == NULL || !cJSON_IsNumber(jfrom) ||
        jfrom->valuedouble < 1.0 || jfrom->valuedouble > 4294967295.0 ||
        jfrom->valuedouble != (double)(uint32_t)jfrom->valuedouble) {
        cJSON_Delete(root);
        return jerr(req, "400 Bad Request", "bad_value");
    }
    from = (uint32_t)jfrom->valuedouble;

    cJSON *jto = cJSON_GetObjectItem(root, "to");
    if (jto == NULL || !cJSON_IsNumber(jto) ||
        jto->valuedouble < 1.0 || jto->valuedouble > 4294967295.0 ||
        jto->valuedouble != (double)(uint32_t)jto->valuedouble) {
        cJSON_Delete(root);
        return jerr(req, "400 Bad Request", "bad_value");
    }
    to = (uint32_t)jto->valuedouble;

    cJSON *jname = cJSON_GetObjectItem(root, "name");
    if (jname != NULL) {
        if (!cJSON_IsString(jname) || jname->valuestring == NULL) {
            cJSON_Delete(root);
            return jerr(req, "400 Bad Request", "bad_value");
        }
        size_t nl = strlen(jname->valuestring);
        if (nl >= AF_MEAS_NAME_MAX) {
            cJSON_Delete(root);
            return jerr(req, "400 Bad Request", "bad_value");
        }
        memcpy(name, jname->valuestring, nl);
        name[nl] = '\0';
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (strcmp(item->string, "from") != 0 &&
            strcmp(item->string, "to") != 0 &&
            strcmp(item->string, "name") != 0) {
            cJSON_Delete(root);
            return jerr(req, "400 Bad Request", "unknown_key");
        }
    }

    cJSON_Delete(root);

    af_meas_res_t res;
    int rc = af_meas_journal_compute(from, to, &res, NULL);
    if (rc != 0) {
        return jrc_err(req, rc);
    }

    uint32_t id = 0;
    rc = af_meas_drv_add_journal(name, &res, from, &id);
    if (rc != 0) {
        return jrc_err(req, rc);
    }

    char out[48];
    snprintf(out, sizeof out, "{\"ok\":true,\"id\":%u}", (unsigned)id);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}
