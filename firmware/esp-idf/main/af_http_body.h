#pragma once

#include <limits.h>
#include <stdio.h>
#include "esp_http_server.h"

/* Settings and BLE commands allow three consecutive receive timeouts. */
static inline int af_http_read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (!req || !buf || cap == 0) return -1;
    size_t length = req->content_len;
    if (length >= cap || length > INT_MAX) return -1;
    size_t total = 0;
    int consecutive_timeouts = 0;
    while (total < length) {
        size_t remaining = length - total;
        int ret = httpd_req_recv(req, buf + total, remaining);
        if (ret > 0) {
            if ((size_t)ret > remaining) return -1;
            total += (size_t)ret;
            consecutive_timeouts = 0;
        } else if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts > 3) return -1;
        } else {
            return -1;
        }
    }
    buf[total] = '\0';
    return (int)total;
}

/* Codes are internal constants. Preserve the handlers' response convention. */
static inline esp_err_t af_http_send_error(httpd_req_t *req, const char *status, const char *code)
{
    char body[128];
    int n = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    if (n < 0 || (size_t)n >= sizeof(body)) return ESP_FAIL;
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, (size_t)n);
    return ESP_OK;
}
