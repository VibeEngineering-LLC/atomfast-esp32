#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "af_hist_query.h"

/* e16e (T-34): GET /api/history.csv?...&pts=N - не более ~N строк (корзины: среднее/мин/макс). Вызывается из af_history_csv_handler. */
esp_err_t af_history_ds_run(httpd_req_t *req, const af_hq_params_t *hp, const af_hq_plan_t *plan, uint32_t head, uint32_t epoch, const char *mac);
