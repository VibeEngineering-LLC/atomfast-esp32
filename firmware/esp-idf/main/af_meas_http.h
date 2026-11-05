#pragma once
#include "esp_http_server.h"

/* Обработчик GET запроса /api/meas для получения текущего состояния */
esp_err_t af_meas_http_get(httpd_req_t *req);

/* Обработчик GET запроса /api/meas/results?id=&limit= для получения результатов */
esp_err_t af_meas_http_results(httpd_req_t *req);

/* Обработчик GET запроса /api/meas/results.csv?id= для экспорта в CSV */
esp_err_t af_meas_http_csv(httpd_req_t *req);

/* Обработчик POST запросов /api/meas/{start,stop,pause,resume,delete}; CSRF проверяет вызывающий */
esp_err_t af_meas_http_post(httpd_req_t *req);
