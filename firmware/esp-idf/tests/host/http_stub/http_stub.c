#include "esp_http_server.h"
#include "af_meas_drv.h"
#include "af_meas_jrn_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Часть 1: заглушка HTTP */
stub_resp_t g_resp;

void stub_reset(void) {
    free(g_resp.body);
    memset(&g_resp, 0, sizeof g_resp);
    snprintf(g_resp.status, sizeof g_resp.status, "200 OK");
}

void stub_req(httpd_req_t *r, const char *uri, const char *body) {
    r->uri = uri;
    r->body = body;
    r->body_pos = 0;
    r->content_len = body ? (int)strlen(body) : 0;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) {
    (void)r;
    snprintf(g_resp.status, sizeof g_resp.status, "%s", s);
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) {
    (void)r;
    snprintf(g_resp.type, sizeof g_resp.type, "%s", t);
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *val) {
    (void)r;
    if (g_resp.nhdr < 4) {
        snprintf(g_resp.hdr_name[g_resp.nhdr], sizeof g_resp.hdr_name[0], "%s", name);
        snprintf(g_resp.hdr_val[g_resp.nhdr], sizeof g_resp.hdr_val[0], "%s", val);
        g_resp.nhdr++;
    }
    return ESP_OK;
}

static void append(const char *s, size_t n) {
    if (g_resp.len + n + 1 > g_resp.cap) {
        size_t new_cap = g_resp.cap ? g_resp.cap : 4096;
        while (new_cap < g_resp.len + n + 1) {
            new_cap *= 2;
        }
        char *tmp = realloc(g_resp.body, new_cap);
        if (!tmp) return;
        g_resp.body = tmp;
        g_resp.cap = new_cap;
    }
    memcpy(g_resp.body + g_resp.len, s, n);
    g_resp.len += n;
    g_resp.body[g_resp.len] = '\0';
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s) {
    (void)r;
    append(s, strlen(s));
    g_resp.finished = 1;
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, long len) {
    (void)r;
    if (buf == NULL && len == 0) {
        g_resp.finished = 1;
        return ESP_OK;
    }
    size_t n = len < 0 ? strlen(buf) : (size_t)len;
    append(buf, n);
    g_resp.chunks++;
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
    if (!r->body || r->body_pos >= r->content_len) return 0;
    size_t avail = (size_t)(r->content_len - r->body_pos);
    size_t to_copy = avail < len ? avail : len;
    memcpy(buf, r->body + r->body_pos, to_copy);
    r->body_pos += (int)to_copy;
    return (int)to_copy;
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len) {
    const char *q = strchr(r->uri, '?');
    if (!q) return ESP_FAIL;
    q++;
    size_t qlen = strlen(q);
    if (qlen + 1 > len) return ESP_FAIL;
    memcpy(buf, q, qlen);
    buf[qlen] = '\0';
    return ESP_OK;
}

/* Часть 2: хост-реализация драйвера */
af_meas_core_t g_core;
uint64_t g_now_ms;
int g_utc_ok;
uint32_t g_utc;

static void *h_alloc(size_t n) { return calloc(1, n); }
static void h_free(void *p) { free(p); }

void drv_host_init(void) {
    af_meas_init(&g_core, h_alloc, h_free, NULL, NULL);
    g_now_ms = 1000000;
    g_utc_ok = 1;
    g_utc = 1792859805u;
}

void drv_host_free(void) {
    for (int i = 0; i < AF_MEAS_MAX_RUNS; i++) {
        if (g_core.runs[i].id != 0) {
            am_free_run(&g_core, &g_core.runs[i]);
        }
    }
}


/* e16f (T-38): заглушка обработчика «замер из журнала»: считает вызовы, тело проверяется отдельным тестом af_mjrn */
int g_jsave_calls;
esp_err_t af_meas_http_journal_save(httpd_req_t *req, const char *body, size_t n) {
    (void)req; (void)body; (void)n;
    g_jsave_calls++;
    return ESP_OK;
}
void af_meas_drv_init(void) {
    drv_host_init();
}

int af_meas_drv_start(const af_meas_params_t *p, uint32_t *id) {
    return af_meas_start(&g_core, p, g_now_ms, g_utc_ok, g_utc, id);
}

int af_meas_drv_stop(uint32_t id) {
    return af_meas_stop(&g_core, id, g_now_ms);
}

int af_meas_drv_pause(uint32_t id) {
    return af_meas_pause(&g_core, id, g_now_ms);
}

int af_meas_drv_resume(uint32_t id) {
    return af_meas_resume(&g_core, id, g_now_ms);
}

int af_meas_drv_delete(uint32_t id) {
    return af_meas_delete(&g_core, id);
}

uint32_t af_meas_drv_active_id(void) {
    return g_core.active >= 0 ? g_core.runs[g_core.active].id : 0;
}

int af_meas_drv_time(uint32_t *utc) {
    if (utc) *utc = g_utc_ok ? g_utc : 0;
    return g_utc_ok;
}

static af_meas_run_t *pick(uint32_t id) {
    if (id != 0) {
        return af_meas_find(&g_core, id);
    }
    if (g_core.active >= 0) {
        return &g_core.runs[g_core.active];
    }
    af_meas_run_t *best = NULL;
    uint32_t max_id = 0;
    for (int i = 0; i < AF_MEAS_MAX_RUNS; i++) {
        if (g_core.runs[i].id != 0 && g_core.runs[i].id > max_id) {
            max_id = g_core.runs[i].id;
            best = &g_core.runs[i];
        }
    }
    return best;
}

int af_meas_drv_snapshot(uint32_t id, af_meas_run_t *run, af_meas_live_t *live, int *live_ok, af_meas_sum_t *sum) {
    af_meas_run_t *r = pick(id);
    if (!r) return -1;
    *run = *r;
    *live_ok = (af_meas_live(r, g_now_ms, live) == 0);
    af_meas_summary(r, sum);
    return 0;
}

int af_meas_drv_row(uint32_t id, uint32_t idx, af_meas_res_t *out) {
    af_meas_run_t *r = af_meas_find(&g_core, id);
    if (!r) return -1;
    const af_meas_res_t *row = af_meas_res_at(r, idx);
    if (!row) return -1;
    *out = *row;
    return 0;
}

uint32_t af_meas_drv_ids(uint32_t *ids, uint32_t cap) {
    uint32_t n = 0;
    for (int i = 0; i < AF_MEAS_MAX_RUNS && n < cap; i++) {
        if (g_core.runs[i].id != 0) {
            ids[n++] = g_core.runs[i].id;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            if (ids[j] > ids[i]) {
                uint32_t tmp = ids[i];
                ids[i] = ids[j];
                ids[j] = tmp;
            }
        }
    }
    return n;
}
