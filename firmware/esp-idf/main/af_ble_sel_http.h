#ifndef AF_BLE_SEL_HTTP_H
#define AF_BLE_SEL_HTTP_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

/* BLE-селекция: GET/POST по путям /api/ble/... Маршруты заданы в таблице web_server.c. */

/* Реализация в web_server.c: проверка X-CSRF-Token и Origin (true - запрос допустим) */
bool web_server_csrf_ok(httpd_req_t *req);
esp_err_t af_ble_sel_cands_handler(httpd_req_t *req);
esp_err_t af_ble_sel_select_handler(httpd_req_t *req);
esp_err_t af_ble_sel_rescan_handler(httpd_req_t *req);
esp_err_t af_ble_sel_forget_handler(httpd_req_t *req);
esp_err_t af_ble_sel_cancel_handler(httpd_req_t *req);
esp_err_t af_ble_sel_manual_handler(httpd_req_t *req);

#endif /* AF_BLE_SEL_HTTP_H */
