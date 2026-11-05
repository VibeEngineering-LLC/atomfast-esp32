#pragma once
/*
 * ESP-IDF httpd allows at most 8 response headers (Content-Type excluded).
 * Our budget is therefore 7 calls to httpd_resp_set_hdr().
 */
#include <stdio.h>
#include "esp_http_server.h"
#include "af_hist_query.h"

#define AF_HIST_HDR_BUDGET 7

static inline esp_err_t af_hist_resp_hdrs(httpd_req_t *req, const char *cd,
                                          const af_hq_hdrs_t *hh, const char *ds)
{
    char range[56];
    int n = 0;

    int rc = snprintf(range, sizeof(range), "epoch=%s;first=%s;lost=%s",
                      hh->epoch, hh->first_seq, hh->lost);
    if (rc < 0 || rc >= (int)sizeof(range))
        return ESP_FAIL;

#define AF_HH_SET(k, v) \
    do { \
        if (++n > AF_HIST_HDR_BUDGET || httpd_resp_set_hdr(req, (k), (v)) != ESP_OK) \
            return ESP_FAIL; \
    } while (0)

    AF_HH_SET("Cache-Control", "no-store");
    AF_HH_SET("Content-Disposition", cd);
    AF_HH_SET("X-AF-Kind", hh->kind);
    AF_HH_SET("X-AF-Seq", hh->seq);
    AF_HH_SET("X-AF-Range", range);
    if (hh->device[0] != 0)
        AF_HH_SET("X-AF-Device", hh->device);
    if (ds != NULL)
        AF_HH_SET("X-AF-Ds", ds);

#undef AF_HH_SET

    return ESP_OK;
}
