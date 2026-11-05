#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_http_server.h"
#include "af_meas.h"

/* e16f (T-38): расчёт замера по сырому журналу за [from, to) (UTC, с); 0 или AF_MJRN_E_* / код платы (-20 журнал занят, -21 нет памяти) */
int af_meas_journal_compute(uint32_t from, uint32_t to, af_meas_res_t *res, uint32_t *gaps);

/* GET /api/meas/journal?from=&to= - расчёт без сохранения */
esp_err_t af_meas_http_journal(httpd_req_t *req);

/* POST /api/meas/journal-save {"name","from","to"} - расчёт на плате и запись в «Результаты» (CSRF проверил вызывающий) */
esp_err_t af_meas_http_journal_save(httpd_req_t *req, const char *body, size_t n);
