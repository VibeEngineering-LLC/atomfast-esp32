#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef struct { const char *query; int query_error; } httpd_req_t;
int httpd_req_get_url_query_len(httpd_req_t *req);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *req, char *buf, size_t cap);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *message);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, long len);
