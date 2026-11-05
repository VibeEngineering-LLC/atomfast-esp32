#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "af_parse.h"
#include "af_env.h"
#include "af_hist_query.h"

#define AF_STORE_TOTAL_BYTES (4u * 1024u * 1024u)
#define AF_STORE_RETENTION_S (14u * 24u * 3600u)
#define AF_STORE_WINDOW_S 60u
#define AF_STORE_QUEUE_LEN 16u
#define AF_STORE_ENV_RECS 40320u /* 14 сут при периоде 30 с */

typedef struct {
    uint32_t cap;
    uint32_t limit;
    uint32_t count;
    uint32_t seq;
    uint32_t oldest;
    uint32_t dropped;
    uint32_t rejected;
    uint32_t epoch;
} af_store_ring_info_t;

typedef struct {
    af_store_ring_info_t raw;
    af_store_ring_info_t agg;
    af_store_ring_info_t env;
    uint32_t window_s;
    uint8_t en_raw, en_agg, en_env;   /* e16i П.2: 1 = вид пишется */
    uint32_t samples;
    uint32_t ts0_samples;
    uint32_t win_rejected;
    uint32_t agg_closed;
    uint32_t q_dropped;
    uint32_t bytes;
    uint32_t now_ts;
    int ready;
} af_store_status_t;

esp_err_t af_store_init(void);
esp_err_t af_store_set_enabled(int raw, int agg, int env); /* e16i П.2: -1 не менять, 0 выкл, 1 вкл; сохраняет в NVS afset */
void af_store_get_enabled(int *raw, int *agg, int *env);
esp_err_t af_store_set_window(uint16_t period_s); /* e16i П.3: окно агрегатов в RAM (NVS и очистку делает вызывающий) */
void af_store_submit(const af_sample_t *s);
uint32_t af_store_gen(void);                                     /* b23 F05: текущее поколение ядра */
void af_store_submit_gen(const af_sample_t *s, uint32_t gen);    /* b23 F05: постановка со снятым поколением */
int af_store_submit_env(const af_env_rec_t *r); /* e10: 0 принято, -1 нет (не готов/таймаут/кольцо не создано) */
void af_store_get_status(af_store_status_t *out);
/* T-86: смена прибора - очистка raw+agg; 0 готово, -1/-2/-3 как af_pfs_clear; *persisted из результата */
int af_store_clear_for_device_change(int *persisted);
uint32_t af_store_read(int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost);
uint32_t af_store_read_timeouts(void);
/* e11: план выборки CSV под одним захватом мьютекса: 0 ок; -1 не готов/мьютекс занят; -2 кольца нет (plan, head, epoch = 0) */
int af_store_plan(const af_hq_params_t *p, af_hq_plan_t *plan, uint32_t *head, uint32_t *epoch);
esp_err_t af_history_csv_handler(httpd_req_t *req);
esp_err_t af_history_status_handler(httpd_req_t *req);
