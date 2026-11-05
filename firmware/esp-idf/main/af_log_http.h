#pragma once
/* e16: HTTP-обработчики журнала и очистки истории. CSRF и Basic auth проверяет вызывающий (web_server.c). */
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t af_log_http_get(httpd_req_t *req);        /* GET  /api/log?since=&limit=&level= (NDJSON) */
esp_err_t af_log_http_txt(httpd_req_t *req);        /* GET  /api/log.txt (скачивание файлом) */
esp_err_t af_log_http_clear(httpd_req_t *req);      /* POST /api/log/clear {"confirm":"CLEAR"} */
esp_err_t af_history_http_clear(httpd_req_t *req);  /* POST /api/history/clear {"ring":"raw|agg|env|all","confirm":"CLEAR"} */
