#pragma once
/* e16-BLE: HTTP-обработчики подробной статистики BLE (реализация в af_ble_http.c) */
#include "esp_http_server.h"
#include "cJSON.h"

esp_err_t af_ble_http_stats(httpd_req_t *req);          /* GET /api/ble */
esp_err_t af_ble_http_sessions(httpd_req_t *req);       /* GET /api/ble/sessions?limit=N */
esp_err_t af_ble_http_sessions_csv(httpd_req_t *req);   /* GET /api/ble/sessions.csv */
void af_ble_http_status_add(cJSON *root);               /* ключи ble_* для /api/status */
