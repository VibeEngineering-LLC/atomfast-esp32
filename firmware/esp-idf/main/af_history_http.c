#include "af_store.h"
#include "af_rec.h"
#include "af_env.h"
#include "af_persist_fs.h"
#include "af_hist_query.h"
#include "af_history_codec.h"
#include "af_csvdisp.h"
#include "af_history_ds.h"
#include "af_history_hdr.h"
#include "af_net.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AF_CSV_YIELD_CHUNKS 32 /* e11: vTaskDelay(1) раз в столько чанков (было 8) */
#define AF_CSV_BUF_SIZE 2304
#define AF_REC_BUF_SIZE (32 * 28)

struct af_hist_ctx {
    uint8_t rec[AF_REC_BUF_SIZE];
    char csv[AF_CSV_BUF_SIZE];
    char cd[64];   /* Content-Disposition (живёт до конца ответа) */
};

esp_err_t af_history_csv_handler(httpd_req_t *req)
{
    char q[96];
    char v[16];
    struct af_hist_ctx *ctx = NULL;
    esp_err_t rc = ESP_OK;
    int qlen;
    int i;
    int n;
    int len;
    int csv_len = 0;
    int chunks = 0;
    int have_first = 0;
    int hdr_done = 0;
    int first_read = 1;
    uint32_t cur;
    uint32_t end;
    uint32_t sent = 0;
    uint32_t next = 0;
    uint32_t lost = 0;
    uint32_t lost_total = 0;
    uint32_t first = 0;
    uint32_t t_out;
    uint32_t want;
    uint32_t seq0;
    uint32_t ts;
    int pl;
    af_hq_params_t hp;
    af_hq_plan_t plan;
    uint32_t head = 0;
    uint32_t epoch = 0;
    int prc;
    char mac[AF_HQ_DEVICE_LEN];
    af_hq_hdrs_t hh;
    const char *keys[] = {"kind", "since", "limit", "from", "to", "ext", "pts"};
    const char *errs[] = {"bad kind", "bad since", "bad limit", "bad from", "bad to", "bad ext", "bad pts"};


    qlen = httpd_req_get_url_query_len(req);
    if (qlen < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
    }
    if (qlen > 95) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query too long");
    }
    q[0] = 0;
    if (qlen > 0 && httpd_req_get_url_query_str(req, q, qlen + 1) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
    }

    af_hq_init(&hp);
    for (i = 0; i < 7; i++) {
        esp_err_t kr = httpd_query_key_value(q, keys[i], v, sizeof(v));
        if (kr == ESP_ERR_NOT_FOUND) {
            continue;
        }
        if (kr != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        }
        if (af_hq_set(&hp, keys[i], v) != 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, errs[i]);
        }
    }
    if (af_hq_check(&hp) != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad range");
    }

    prc = af_store_plan(&hp, &plan, &head, &epoch);
    if (prc == -1) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
    }

    mac[0] = 0;
    (void)ble_gw_target_mac(mac, sizeof(mac));

    if (hp.pts > 0) {   /* e16e (T-34): прореживание по числу точек */
        if (hp.kind == AF_HQ_KIND_ENV) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pts raw/agg only");
        }
        return af_history_ds_run(req, &hp, &plan, head, epoch, mac);
    }
    ctx = (struct af_hist_ctx *)heap_caps_malloc(sizeof(struct af_hist_ctx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ctx == NULL) {
        ctx = (struct af_hist_ctx *)malloc(sizeof(struct af_hist_ctx));
    }
    if (ctx == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    if (af_history_csv_header(hp.kind, hp.ext, ctx->csv, sizeof(ctx->csv)) < 0) {
        rc = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hdr err");
        goto done;
    }
    csv_len = (int)strlen(ctx->csv);

    t_out = af_store_read_timeouts();
    cur = plan.start;
    end = plan.end;
    first = plan.start;

    for (;;) {
        want = af_hq_next_n(cur, end, hp.limit - sent, 32);
        if (want == 0) {
            break;
        }
        n = af_store_read(hp.kind, cur, ctx->rec, want, &next, &lost);
        if (first_read) {
            lost_total += lost;
            first_read = 0;
        } else {
            if (lost != 0) {
                rc = ESP_FAIL;
                goto done;
            }
        }
        if (n == 0) {
            break;
        }
        seq0 = next - (uint32_t)n;
        for (i = 0; i < n; i++) {
            af_history_row_t row;
            size_t rec_size = af_history_record_size(hp.kind);
            if (af_history_row_decode(hp.kind, ctx->rec + i * rec_size, &row) != 0) {
                continue;
            }
            ts = row.ts;
            if (!af_hq_ts_match(&hp, ts)) {
                continue;
            }
            pl = 0;
            if (hp.ext == 1) {
                pl = af_hq_ext_prefix(mac, seq0 + (uint32_t)i, epoch, ctx->csv + csv_len, sizeof(ctx->csv) - csv_len);
                if (pl < 0) {
                    rc = ESP_FAIL;
                    goto done;
                }
            }
            len = af_history_row_csv(hp.kind, &row, ctx->csv + csv_len + pl, sizeof(ctx->csv) - csv_len - pl);
            if (len < 0) {
                continue;
            }
            if (!have_first) {
                first = seq0 + (uint32_t)i;
                have_first = 1;
            }
            csv_len += pl + len;
            sent++;
            if (csv_len > 1900 || sent >= hp.limit) {
                if (!hdr_done) {
                    httpd_resp_set_type(req, "text/csv");
                    af_csv_disp_fill(ctx->cd, sizeof ctx->cd, (uint8_t)hp.kind);
                    af_hq_hdrs_fill(&hh, hp.kind, head, epoch, first, lost_total, mac);
                    if (af_hist_resp_hdrs(req, ctx->cd, &hh, NULL) != ESP_OK) {
                        rc = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hdr");
                        goto done;
                    }
                    hdr_done = 1;
                }
                if (httpd_resp_send_chunk(req, ctx->csv, csv_len) != ESP_OK) {
                    rc = ESP_FAIL;
                    goto done;
                }
                chunks++;
                if (chunks % AF_CSV_YIELD_CHUNKS == 0) {
                    vTaskDelay(1);
                }
                csv_len = 0;
            }
        }
        cur = next;
    }

    if (csv_len > 0) {
        if (!hdr_done) {
            httpd_resp_set_type(req, "text/csv");
            af_csv_disp_fill(ctx->cd, sizeof ctx->cd, (uint8_t)hp.kind);
            af_hq_hdrs_fill(&hh, hp.kind, head, epoch, first, lost_total, mac);
            if (af_hist_resp_hdrs(req, ctx->cd, &hh, NULL) != ESP_OK) {
                rc = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hdr");
                goto done;
            }
            hdr_done = 1;
        }
        if (httpd_resp_send_chunk(req, ctx->csv, csv_len) != ESP_OK) {
            rc = ESP_FAIL;
            goto done;
        }
    }

    if (af_store_read_timeouts() != t_out) {
        rc = ESP_FAIL;
        goto done;
    }

    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) {
        rc = ESP_FAIL;
        goto done;
    }

done:
    free(ctx);
    return rc;
}

esp_err_t af_history_status_handler(httpd_req_t *req)
{
    af_store_status_t st;
    char out[1600];
    char pj[800];
    int n;
    int pl;

    af_store_get_status(&st);
    pl = af_pfs_status_json(pj, sizeof(pj));
    if (pl < 0) {
        strcpy(pj, "null");
    }

    n = snprintf(out, sizeof(out),
                 "{\"ready\":%d,\"window_s\":%lu,\"samples\":%lu,\"ts0_samples\":%lu,"
                 "\"win_rejected\":%lu,\"agg_closed\":%lu,\"q_dropped\":%lu,\"bytes\":%lu,\"now_ts\":%lu,"
                 "\"raw\":{\"cap\":%lu,\"limit\":%lu,\"count\":%lu,\"seq\":%lu,\"oldest\":%lu,\"dropped\":%lu,\"rejected\":%lu,\"epoch\":%lu},"
                 "\"agg\":{\"cap\":%lu,\"limit\":%lu,\"count\":%lu,\"seq\":%lu,\"oldest\":%lu,\"dropped\":%lu,\"rejected\":%lu,\"epoch\":%lu},"
                 "\"env\":{\"cap\":%lu,\"limit\":%lu,\"count\":%lu,\"seq\":%lu,\"oldest\":%lu,\"dropped\":%lu,\"rejected\":%lu,\"epoch\":%lu},\"en\":{\"raw\":%d,\"agg\":%d,\"env\":%d},\"persist\":%s}",
                 st.ready,
                 (unsigned long)st.window_s,
                 (unsigned long)st.samples,
                 (unsigned long)st.ts0_samples,
                 (unsigned long)st.win_rejected,
                 (unsigned long)st.agg_closed,
                 (unsigned long)st.q_dropped,
                 (unsigned long)st.bytes,
                 (unsigned long)st.now_ts,
                 (unsigned long)st.raw.cap,
                 (unsigned long)st.raw.limit,
                 (unsigned long)st.raw.count,
                 (unsigned long)st.raw.seq,
                 (unsigned long)st.raw.oldest,
                 (unsigned long)st.raw.dropped,
                 (unsigned long)st.raw.rejected,
                 (unsigned long)st.raw.epoch,
                 (unsigned long)st.agg.cap,
                 (unsigned long)st.agg.limit,
                 (unsigned long)st.agg.count,
                 (unsigned long)st.agg.seq,
                 (unsigned long)st.agg.oldest,
                 (unsigned long)st.agg.dropped,
                 (unsigned long)st.agg.rejected,
                 (unsigned long)st.agg.epoch,
                 (unsigned long)st.env.cap,
                 (unsigned long)st.env.limit,
                 (unsigned long)st.env.count,
                 (unsigned long)st.env.seq,
                 (unsigned long)st.env.oldest,
                 (unsigned long)st.env.dropped,
                 (unsigned long)st.env.rejected,
                 (unsigned long)st.env.epoch,
                 (int)st.en_raw, (int)st.en_agg, (int)st.en_env,
                 pj);

    if (n < 0 || n >= (int)sizeof(out)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json err");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out, n);
}
