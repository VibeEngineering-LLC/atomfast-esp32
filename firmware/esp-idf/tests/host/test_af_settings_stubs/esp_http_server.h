#pragma once
/* b29 G: заглушка esp_http_server.h для test_af_settings_http (тело запроса из строки, ответ в g_rsp_*) */
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include "esp_err.h"
#define HTTPD_SOCK_ERR_TIMEOUT (-3)
#define HTTPD_RESP_USE_STRLEN (-1)
typedef void *httpd_handle_t;
typedef enum { HTTP_GET = 1, HTTP_POST = 3 } httpd_method_t;
typedef struct httpd_req { size_t content_len; const char *body; size_t pos; } httpd_req_t;
typedef struct { const char *uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t *r); void *user_ctx; } httpd_uri_t;
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *n, const char *v);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len);
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u);
