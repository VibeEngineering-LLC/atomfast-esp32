#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "af_log_http.h"
#include "af_log_api.h"
#include "af_clear.h"
#include "af_persist_fs.h"
#include "af_fsinfo.h"


static int lg_read_body(httpd_req_t *req, char *buf, size_t cap) {
    size_t total = req->content_len;
    if (total == 0 || total >= cap) return -1;
    size_t got = 0;
    int timeouts = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                if (++timeouts > 5) return -1;
                continue;
            }
            return -1;
        }
        got += (size_t)r;
    }
    return (int)total;
}

static void lg_send_json_err(httpd_req_t *req, const char *status, const char *msg) {
    char out[96];
    int n = snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", msg);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(out)) n = (int)sizeof(out) - 1;
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, n);
}

typedef struct {
    httpd_req_t *req;
    size_t n;
    int err;
    int text;
    char *buf;      /* e16e (perf-audit cpu-http F2): буфер чанка в PSRAM, 4 КБ вместо 640 Б на стеке */
    size_t cap;     /* 0 -> нет памяти, строки уходят по одной */
} lg_ctx_t;

#define LG_BUF_CAP 4096

static void lg_ctx_open(lg_ctx_t *c, httpd_req_t *req, int text) {
    memset(c, 0, sizeof(*c));
    c->req = req;
    c->text = text;
    c->buf = (char *)heap_caps_malloc(LG_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    c->cap = c->buf ? LG_BUF_CAP : 0;
}

static void lg_ctx_close(lg_ctx_t *c) {
    heap_caps_free(c->buf);
    c->buf = NULL;
    c->cap = 0;
}

static int lg_emit(void *vctx, const af_log_ent_t *e) {
    lg_ctx_t *ctx = (lg_ctx_t *)vctx;
    char line[384];
    int len = ctx->text ? af_log_fmt_text(e, line, sizeof(line)) : af_log_fmt_json(e, line, sizeof(line));
    if (len < 0) return 0;
    if (ctx->n + (size_t)len > ctx->cap) {
        if (ctx->n > 0 && httpd_resp_send_chunk(ctx->req, ctx->buf, ctx->n) != ESP_OK) {
            ctx->err = 1;
            return 1;
        }
        ctx->n = 0;
        if ((size_t)len > ctx->cap) {   /* нет буфера: строка уходит сама */
            if (httpd_resp_send_chunk(ctx->req, line, (size_t)len) != ESP_OK) {
                ctx->err = 1;
                return 1;
            }
            return 0;
        }
    }
    memcpy(ctx->buf + ctx->n, line, (size_t)len);
    ctx->n += (size_t)len;
    return 0;
}

static int lg_flush_ctx(lg_ctx_t *c) {
    if (c->n > 0) {
        if (httpd_resp_send_chunk(c->req, c->buf, c->n) != ESP_OK) {
            c->err = 1;
        }
        c->n = 0;
    }
    return c->err;
}

esp_err_t af_log_http_get(httpd_req_t *req) {
    char qs[96];
    qs[0] = 0;
    size_t ql = httpd_req_get_url_query_len(req);
    if (ql >= sizeof(qs)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        return ESP_OK;
    }
    if (ql > 0) {
        if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
            return ESP_OK;
        }
    }
    af_log_q_t q;
    if (af_log_query_parse(qs, &q) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        return ESP_OK;
    }
    af_log_stats_t st;
    af_log_stats(&st);
    httpd_resp_set_type(req, "application/x-ndjson");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char h1[16], h2[16], h3[16];
    snprintf(h1, sizeof(h1), "%u", (unsigned)st.next_seq);
    snprintf(h2, sizeof(h2), "%u", (unsigned)st.lost);
    snprintf(h3, sizeof(h3), "%u", (unsigned)st.suppressed);
    httpd_resp_set_hdr(req, "X-AF-Log-Next", h1);
    httpd_resp_set_hdr(req, "X-AF-Log-Lost", h2);
    httpd_resp_set_hdr(req, "X-AF-Log-Supp", h3);
    lg_ctx_t ctx;
    lg_ctx_open(&ctx, req, 0);
    af_log_iter(q.since, q.min_level, q.limit, lg_emit, &ctx);
    lg_flush_ctx(&ctx);
    lg_ctx_close(&ctx);
    httpd_resp_send_chunk(req, NULL, 0);
    if (ctx.err) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t af_log_http_txt(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"atomfast-log.txt\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    lg_ctx_t ctx;
    lg_ctx_open(&ctx, req, 1);
    const char *hdr = "# atomfast journal, newest last; x<N> = repeated N times\n";
    size_t hl = strlen(hdr);
    if (hl < ctx.cap) {
        memcpy(ctx.buf, hdr, hl);
        ctx.n = hl;
    }
    af_log_iter(0, 1, 0xFFFFFFFFu, lg_emit, &ctx);
    lg_flush_ctx(&ctx);
    lg_ctx_close(&ctx);
    httpd_resp_send_chunk(req, NULL, 0);
    if (ctx.err) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t af_log_http_clear(httpd_req_t *req) {
    char body[AF_CLR_BODY_MAX + 8];
    if (req->content_len == 0 || req->content_len > AF_CLR_BODY_MAX) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    int len = lg_read_body(req, body, sizeof(body));
    if (len < 0) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    uint32_t mask = 0;
    int rc = af_clr_req_parse(body, (size_t)len, 0, &mask);
    if (rc == -2) {
        lg_send_json_err(req, "400 Bad Request", "confirm");
        return ESP_OK;
    }
    if (rc != 0) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    uint32_t files = 0, bytes = 0;
    if (af_log_clear(&files, &bytes) != 0) {
        lg_send_json_err(req, "503 Service Unavailable", "busy");
        return ESP_OK;
    }
    af_fsinfo_invalidate();   /* e16e: после очистки занятость ФС читать заново */
    af_log_event(1, "log", "journal cleared files=%u bytes=%u", (unsigned)files, (unsigned)bytes);
    af_log_flush_now();
    char out[96];
    int n = snprintf(out, sizeof(out), "{\"ok\":true,\"files\":%u,\"bytes\":%u}", (unsigned)files, (unsigned)bytes);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(out)) n = (int)sizeof(out) - 1;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, n);
    return ESP_OK;
}
esp_err_t af_history_http_clear(httpd_req_t *req) {
    char body[AF_CLR_BODY_MAX + 8];
    if (req->content_len == 0 || req->content_len > AF_CLR_BODY_MAX) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    int len = lg_read_body(req, body, sizeof(body));
    if (len < 0) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    uint32_t mask = 0;
    int rc = af_clr_req_parse(body, (size_t)len, 1, &mask);
    if (rc == -2) {
        lg_send_json_err(req, "400 Bad Request", "confirm");
        return ESP_OK;
    }
    if (rc == -3) {
        lg_send_json_err(req, "400 Bad Request", "ring");
        return ESP_OK;
    }
    if (rc != 0) {
        lg_send_json_err(req, "400 Bad Request", "bad body");
        return ESP_OK;
    }
    af_pfs_clear_res_t res;
    memset(&res, 0, sizeof(res));
    int prc = af_pfs_clear(mask, &res);
    af_fsinfo_invalidate();
    if (prc == -1) {
        lg_send_json_err(req, "503 Service Unavailable", "busy");
        return ESP_OK;
    }
    if (prc == -3) {
        af_log_event(3, "hist", "history clear failed rc=-3");
        lg_send_json_err(req, "500 Internal Server Error", "marker");
        return ESP_OK;
    }
    char out[320];
    int n = af_clr_res_json(out, sizeof(out), mask, res.removed, res.seq, res.epoch, res.persisted);
    if (n < 0) {
        lg_send_json_err(req, "500 Internal Server Error", "json");
        return ESP_OK;
    }
    char msg[96];
    int mn = af_clr_log_msg(msg, sizeof(msg), mask, res.removed);
    if (mn < 0) mn = 0;
    if (mn >= (int)sizeof(msg)) mn = (int)sizeof(msg) - 1;
    af_log_event(prc == 0 ? 1 : 2, "hist", "%s%s", msg, prc == 0 ? "" : " (flash cleanup incomplete)");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, n);
    return ESP_OK;
}
