#ifndef AF_SETTINGS_HTTP_H
#define AF_SETTINGS_HTTP_H
#include "esp_err.h"
#include "esp_http_server.h"
/* GET/POST /api/settings. Маршруты заданы в таблице web_server.c. */
esp_err_t af_settings_get_handler(httpd_req_t *req);
esp_err_t af_settings_post_handler(httpd_req_t *req);
#endif
