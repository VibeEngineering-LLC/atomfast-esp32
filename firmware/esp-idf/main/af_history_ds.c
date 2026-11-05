#include "af_history_ds.h"
#include "af_store.h"
#include "af_rec.h"
#include "af_parse.h"
#include "af_hist_query.h"
#include "af_csvdisp.h"
#include "af_ds.h"
#include "af_history_hdr.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

esp_err_t af_history_ds_run(httpd_req_t *req, const af_hq_params_t *hp, const af_hq_plan_t *plan, uint32_t head, uint32_t epoch, const char *mac)
{
    struct ds_ctx {
        uint8_t rec[32 * 28];
        char csv[2304];
        char cd[64];
    } *ctx = NULL;
    esp_err_t rc = ESP_OK;
    int kind = hp->kind;
    uint32_t total = plan->end - plan->start;
    uint32_t eff = (total < hp->limit) ? total : hp->limit;
    uint32_t bucket = af_ds_bucket(eff, hp->pts);
    uint32_t next_seq = plan->start + eff;
    uint32_t t_out = af_store_read_timeouts();
    af_ds_acc_t acc;
    af_ds_reset(&acc);
    uint32_t csv_len = 0;
    char hdr_ds[40];
    af_hq_hdrs_t hh;
    uint32_t cur, end, first_read, lost_total, reads;

    if (kind != AF_HQ_KIND_RAW && kind != AF_HQ_KIND_AGG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pts raw/agg only");
    }

    ctx = heap_caps_malloc(sizeof(struct ds_ctx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx) {
        ctx = malloc(sizeof(struct ds_ctx));
    }
    if (!ctx) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }

    httpd_resp_set_type(req, "text/csv");
    af_csv_disp_fill(ctx->cd, sizeof ctx->cd, (uint8_t)kind);
    af_hq_hdrs_fill(&hh, kind, head, epoch, plan->start, plan->lost, mac);
    snprintf(hdr_ds, sizeof(hdr_ds), "next=%u;bucket=%u", (unsigned)next_seq, (unsigned)bucket);
    /* httpd держит не более 8 заголовков: набор и проверка возвратов - в af_hist_resp_hdrs (<=7), отказ - 500, не тихая потеря */
    if (af_hist_resp_hdrs(req, ctx->cd, &hh, hdr_ds) != ESP_OK) {
        free(ctx);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hdr");
    }

    csv_len = af_ds_header(ctx->csv, sizeof(ctx->csv));

    cur = plan->start;
    end = plan->start + eff;
    first_read = 1;
    lost_total = 0;
    reads = 0;

    for (;;) {
        uint32_t want = af_hq_next_n(cur, end, end - cur, 32);
        if (want == 0) break;

        uint32_t next = 0, lost = 0;
        uint32_t n = af_store_read(kind, cur, ctx->rec, want, &next, &lost);

        if (first_read) {
            lost_total += lost;
            first_read = 0;
        } else {
            if (lost != 0) {
                rc = ESP_FAIL;
                goto done;
            }
        }

        if (n == 0) break;

        for (uint32_t i = 0; i < n; i++) {
            uint32_t ts = 0;
            float dose = 0.0f, rate = 0.0f, pulses = 0.0f;
            int8_t battery = 0, temp = 0;
            uint8_t flags = 0;
            uint32_t weight = 1;
            uint32_t step = 2;
            int ok = 0;

            if (kind == AF_HQ_KIND_RAW) {
                af_raw_rec_t r;
                if (af_raw_decode(ctx->rec + i * AF_REC_RAW_SIZE, &r) == 0) {
                    ts = r.ts;
                    dose = r.s.dose_msv;
                    rate = r.s.doserate_usvh;
                    pulses = (float)r.s.pulses_2s;
                    battery = r.s.battery;
                    temp = r.s.temp_c;
                    flags = r.s.flags;
                    weight = 1;
                    step = 2;
                    ok = 1;
                }
            } else {
                af_agg_rec_t a;
                if (af_agg_decode(ctx->rec + i * AF_REC_AGG_SIZE, &a) == 0) {
                    if (a.n == 0) continue;
                    ts = a.ts;
                    dose = a.dose_msv;
                    rate = a.doserate_usvh;
                    pulses = a.pulses_2s;
                    battery = a.battery;
                    temp = a.temp_c;
                    flags = a.flags;
                    weight = a.n;
                    step = a.period_s;
                    ok = 1;
                }
            }

            if (!ok) continue;
            if (!af_hq_ts_match(hp, ts)) continue;

            if (af_ds_add(&acc, ts, dose, rate, pulses, battery, temp, flags, weight, step) == 0) {
                if (acc.src >= bucket) {
                    int len = af_ds_row(&acc, ctx->csv + csv_len, sizeof(ctx->csv) - csv_len);
                    if (len > 0) csv_len += len;
                    af_ds_reset(&acc);

                    if (csv_len > 1900) {
                        if (httpd_resp_send_chunk(req, ctx->csv, csv_len) != ESP_OK) {
                            rc = ESP_FAIL;
                            goto done;
                        }
                        csv_len = 0;
                    }
                }
            }
        }

        cur = next;
        reads++;
        if ((reads & 0x7F) == 0) {
            vTaskDelay(1);
        }
    }

    if (acc.src > 0) {
        int len = af_ds_row(&acc, ctx->csv + csv_len, sizeof(ctx->csv) - csv_len);
        if (len > 0) csv_len += len;
        af_ds_reset(&acc);
    }

    if (csv_len > 0) {
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
    }

done:
    free(ctx);
    return rc;
}
