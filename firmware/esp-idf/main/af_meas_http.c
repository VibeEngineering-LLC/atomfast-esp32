#include "af_meas_http.h"
#include "af_meas_drv.h"
#include "af_meas.h"
#include "af_meas_req.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "af_hbuf.h"
#include "af_meas_jrn_http.h"

static esp_err_t meas_err(httpd_req_t *req, const char *status, const char *code)
{
    char buf[64];
    snprintf(buf, sizeof buf, "{\"ok\":false,\"err\":\"%s\"}", code);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

static esp_err_t meas_rc_err(httpd_req_t *req, int rc)
{
    switch (rc) {
    case AF_MEAS_E_BODY:
        return meas_err(req, "400 Bad Request", "bad_body");
    case AF_MEAS_E_JSON:
        return meas_err(req, "400 Bad Request", "json");
    case AF_MEAS_E_KEY:
        return meas_err(req, "400 Bad Request", "unknown_key");
    case AF_MEAS_E_VALUE:
        return meas_err(req, "400 Bad Request", "bad_value");
    case AF_MEAS_E_SIGMA:
        return meas_err(req, "400 Bad Request", "sigma_only_single");
    case AF_MEAS_E_TIME:
        return meas_err(req, "409 Conflict", "time_not_synced");
    case AF_MEAS_E_BUSY:
        return meas_err(req, "409 Conflict", "busy");
    case AF_MEAS_E_NOTFOUND:
        return meas_err(req, "404 Not Found", "not_found");
    case AF_MEAS_E_STATE:
        return meas_err(req, "409 Conflict", "bad_state");
    case AF_MEAS_E_NOMEM:
        return meas_err(req, "503 Service Unavailable", "no_mem");
    default:
        return meas_err(req, "400 Bad Request", "bad_value");
    }
}

static int meas_read_body(httpd_req_t *req, char *buf, size_t cap)
{
    int total = req->content_len;
    if (total < 0 || (size_t)total >= cap) {
        return -1;
    }
    if (total == 0) {
        return 0;
    }
    size_t got = 0;
    while (got < (size_t)total) {
        int r = httpd_req_recv(req, buf + got, (size_t)total - got);
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    return total;
}

/* e16e (perf-audit cpu-http F2): фрагменты ответа склеиваются в 4 КБ-буфер (PSRAM) и уходят крупными кусками.
   Обработчики httpd выполняются в одной задаче -> одно общее состояние безопасно. Нет памяти -> сквозной режим как раньше. */
#define MEAS_HB_CAP 4096
static af_hbuf_t s_hb;
static char *s_hb_mem;

static int meas_hb_send(void *ctx, const char *p, size_t n)
{
    return httpd_resp_send_chunk((httpd_req_t *)ctx, p, (int)n) == ESP_OK ? 0 : -1;
}

static void meas_json_headers(httpd_req_t *req, const char *type)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!s_hb_mem) s_hb_mem = (char *)heap_caps_malloc(MEAS_HB_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    af_hbuf_init(&s_hb, s_hb_mem, s_hb_mem ? MEAS_HB_CAP : 0, meas_hb_send, req);
}

static int meas_send(httpd_req_t *req, const char *s)
{
    (void)req;
    return af_hbuf_adds(&s_hb, s) == 0 ? 0 : -1;
}

/* Конец ответа: сбросить остаток и закрыть chunked-поток. */
static esp_err_t meas_end(httpd_req_t *req)
{
    if (af_hbuf_end(&s_hb) != 0) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t af_meas_http_get(httpd_req_t *req)
{
    meas_json_headers(req, "application/json");

    uint32_t utc = 0;
    int tok = af_meas_drv_time(&utc);

    char nu[16];
    if (tok) snprintf(nu, sizeof nu, "%u", (unsigned)utc);
    else snprintf(nu, sizeof nu, "null");
    char head[320];
    int len = snprintf(head, sizeof head,
        "{\"ok\":true,\"time_ok\":%s,\"now_utc\":%s,\"active_id\":%u,"
        "\"limits\":{\"n_max\":%u,\"t_min_s\":%u,\"t_max_s\":%u,"
        "\"pause_max_s\":%u,\"delay_max_s\":%u,\"res_max\":%u,"
        "\"runs_max\":%u,\"files_max\":%u},"
        "\"runs\":[",
        tok ? "true" : "false",
        nu,
        (unsigned)af_meas_drv_active_id(),
        (unsigned)AF_MEAS_N_MAX,
        (unsigned)AF_MEAS_T_MIN_S,
        (unsigned)AF_MEAS_T_MAX_S,
        (unsigned)AF_MEAS_PAUSE_MAX_S,
        (unsigned)AF_MEAS_DELAY_MAX_S,
        (unsigned)AF_MEAS_RES_MAX,
        (unsigned)AF_MEAS_MAX_RUNS,
        (unsigned)AF_MEAS_FILES_MAX
    );
    if (len < 0 || len >= (int)sizeof head) {
        return ESP_FAIL;
    }
    if (meas_send(req, head) != 0) {
        return ESP_FAIL;
    }

    uint32_t ids[AF_MEAS_MAX_RUNS];
    int n = af_meas_drv_ids(ids, AF_MEAS_MAX_RUNS);

    int sent_runs = 0;
    for (int i = 0; i < n; i++) {
        af_meas_run_t run;
        af_meas_live_t live;
        af_meas_sum_t sum;
        int live_ok = 0;

        if (af_meas_drv_snapshot(ids[i], &run, &live, &live_ok, &sum) != 0) {
            continue;
        }

        char b1[1024];
        char b2[480];
        char b3[320];

        int l1 = af_meas_fmt_run_json(&run, b1, sizeof b1);
        int l2 = af_meas_fmt_sum_json(&sum, b2, sizeof b2);
        int l3 = live_ok ? af_meas_fmt_live_json(&live, b3, sizeof b3) : -1;

        if (l1 < 0 || l2 < 0) {
            continue;
        }

        if (sent_runs++ > 0) {
            if (meas_send(req, ",") != 0) {
                return ESP_FAIL;
            }
        }

        if (meas_send(req, "{\"run\":") != 0) {
            return ESP_FAIL;
        }
        if (meas_send(req, b1) != 0) {
            return ESP_FAIL;
        }
        if (meas_send(req, ",\"sum\":") != 0) {
            return ESP_FAIL;
        }
        if (meas_send(req, b2) != 0) {
            return ESP_FAIL;
        }
        if (meas_send(req, ",\"live\":") != 0) {
            return ESP_FAIL;
        }
        if (l3 >= 0) {
            if (meas_send(req, b3) != 0) {
                return ESP_FAIL;
            }
        } else {
            if (meas_send(req, "null") != 0) {
                return ESP_FAIL;
            }
        }
        if (meas_send(req, "}") != 0) {
            return ESP_FAIL;
        }
    }

    if (meas_send(req, "]}") != 0) {
        return ESP_FAIL;
    }
    if (meas_end(req) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t af_meas_http_results(httpd_req_t *req)
{
    char q[64];
    const char *query = (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) ? q : NULL;

    uint32_t id = 0;
    uint32_t limit = 100;

    int r = af_mreq_query_u32(query, "id", &id);
    if (r < 0) {
        return meas_rc_err(req, AF_MEAS_E_VALUE);
    }

    r = af_mreq_query_u32(query, "limit", &limit);
    if (r < 0 || (r == 1 && (limit < 1 || limit > AF_MEAS_RES_MAX))) {
        return meas_rc_err(req, AF_MEAS_E_VALUE);
    }

    af_meas_run_t run;
    af_meas_live_t live;
    af_meas_sum_t sum;
    int live_ok = 0;

    if (af_meas_drv_snapshot(id, &run, &live, &live_ok, &sum) != 0) {
        return meas_rc_err(req, AF_MEAS_E_NOTFOUND);
    }

    meas_json_headers(req, "application/json");

    char b1[1024];
    char b2[480];
    char b3[320];

    int l1 = af_meas_fmt_run_json(&run, b1, sizeof b1);
    int l2 = af_meas_fmt_sum_json(&sum, b2, sizeof b2);
    int l3 = live_ok ? af_meas_fmt_live_json(&live, b3, sizeof b3) : -1;

    if (l1 < 0 || l2 < 0) {
        return ESP_FAIL;
    }

    uint32_t from_idx = run.res_n > limit ? run.res_n - limit : 0;

    if (meas_send(req, "{\"ok\":true,\"run\":") != 0) {
        return ESP_FAIL;
    }
    if (meas_send(req, b1) != 0) {
        return ESP_FAIL;
    }
    if (meas_send(req, ",\"sum\":") != 0) {
        return ESP_FAIL;
    }
    if (meas_send(req, b2) != 0) {
        return ESP_FAIL;
    }
    if (meas_send(req, ",\"live\":") != 0) {
        return ESP_FAIL;
    }
    if (l3 >= 0) {
        if (meas_send(req, b3) != 0) {
            return ESP_FAIL;
        }
    } else {
        if (meas_send(req, "null") != 0) {
            return ESP_FAIL;
        }
    }

    char head2[64];
    int len2 = snprintf(head2, sizeof head2, ",\"from_idx\":%u,\"rows\":[", (unsigned)from_idx);
    if (len2 < 0 || len2 >= (int)sizeof head2) {
        return ESP_FAIL;
    }
    if (meas_send(req, head2) != 0) {
        return ESP_FAIL;
    }

    int sent_rows = 0;
    for (uint32_t idx = from_idx; idx < run.res_n; idx++) {
        af_meas_res_t row;
        if (af_meas_drv_row(run.id, idx, &row) != 0) {
            continue;
        }

        char b4[512];
        int l4 = af_meas_fmt_res_json(&row, b4, sizeof b4);
        if (l4 < 0) {
            continue;
        }

        if (sent_rows++ > 0) {
            if (meas_send(req, ",") != 0) {
                return ESP_FAIL;
            }
        }
        if (meas_send(req, b4) != 0) {
            return ESP_FAIL;
        }
    }

    if (meas_send(req, "]}") != 0) {
        return ESP_FAIL;
    }
    if (meas_end(req) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t af_meas_http_csv(httpd_req_t *req)
{
    char q[64];
    const char *query = (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK) ? q : NULL;

    uint32_t id = 0;
    int r = af_mreq_query_u32(query, "id", &id);
    if (r < 0) {
        return meas_rc_err(req, AF_MEAS_E_VALUE);
    }

    af_meas_run_t run;
    af_meas_live_t live;
    af_meas_sum_t sum;
    int live_ok = 0;

    if (af_meas_drv_snapshot(id, &run, &live, &live_ok, &sum) != 0) {
        return meas_rc_err(req, AF_MEAS_E_NOTFOUND);
    }

    meas_json_headers(req, "text/csv; charset=utf-8");

    char cd[96];
    snprintf(cd, sizeof cd, "attachment; filename=\"measure-%u.csv\"", (unsigned)run.id);
    httpd_resp_set_hdr(req, "Content-Disposition", cd);

    char b[512];
    int lh = af_meas_fmt_csv_header(b, sizeof b);
    if (lh < 0) {
        return ESP_FAIL;
    }
    if (meas_send(req, b) != 0) {
        return ESP_FAIL;
    }

    for (uint32_t idx = 0; idx < run.res_n; idx++) {
        af_meas_res_t row;
        if (af_meas_drv_row(run.id, idx, &row) != 0) {
            continue;
        }

        int lr = af_meas_fmt_csv_row(&row, b, sizeof b);
        if (lr < 0) {
            continue;
        }
        if (meas_send(req, b) != 0) {
            return ESP_FAIL;
        }
    }

    if (meas_end(req) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t af_meas_http_post(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *prefix = "/api/meas/";
    size_t plen = strlen(prefix);

    if (strncmp(uri, prefix, plen) != 0) {
        return meas_rc_err(req, AF_MEAS_E_NOTFOUND);
    }

    const char *a = uri + plen;
    size_t len = 0;
    while (a[len] != '\0' && a[len] != '?') {
        len++;
    }

    int is_start = 0, is_stop = 0, is_pause = 0, is_resume = 0, is_delete = 0, is_jsave = 0;
    if (len == 5 && strncmp(a, "start", 5) == 0) {
        is_start = 1;
    } else if (len == 4 && strncmp(a, "stop", 4) == 0) {
        is_stop = 1;
    } else if (len == 5 && strncmp(a, "pause", 5) == 0) {
        is_pause = 1;
    } else if (len == 6 && strncmp(a, "resume", 6) == 0) {
        is_resume = 1;
    } else if (len == 6 && strncmp(a, "delete", 6) == 0) {
        is_delete = 1;
    } else if (len == 12 && strncmp(a, "journal-save", 12) == 0) {
        is_jsave = 1;   /* e16f (T-38): замер из журнала */
    } else {
        return meas_rc_err(req, AF_MEAS_E_NOTFOUND);
    }

    char body[640];
    int n = meas_read_body(req, body, sizeof body);
    if (n < 0) {
        return meas_rc_err(req, AF_MEAS_E_BODY);
    }

    uint32_t id = 0;
    int rc = 0;

    if (is_jsave) {
        return n == 0 ? meas_rc_err(req, AF_MEAS_E_BODY) : af_meas_http_journal_save(req, body, (size_t)n);
    }

    if (is_start) {
        if (n == 0) {
            return meas_rc_err(req, AF_MEAS_E_BODY);
        }
        af_meas_params_t p;
        rc = af_mreq_parse_start(body, (size_t)n, &p);
        if (rc != 0) {
            return meas_rc_err(req, rc);
        }
        rc = af_meas_drv_start(&p, &id);
        if (rc != 0) {
            return meas_rc_err(req, rc);
        }
    } else {
        rc = af_mreq_parse_id(body, (size_t)n, is_delete ? 1 : 0, &id);
        if (rc != 0) {
            return meas_rc_err(req, rc);
        }
        uint32_t eff = id ? id : af_meas_drv_active_id();
        if (is_stop) {
            rc = af_meas_drv_stop(eff);
        } else if (is_pause) {
            rc = af_meas_drv_pause(eff);
        } else if (is_resume) {
            rc = af_meas_drv_resume(eff);
        } else if (is_delete) {
            rc = af_meas_drv_delete(eff);
        }
        if (rc != 0) {
            return meas_rc_err(req, rc);
        }
        id = eff;
    }

    char out[48];
    snprintf(out, sizeof out, "{\"ok\":true,\"id\":%u}", (unsigned)id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}
