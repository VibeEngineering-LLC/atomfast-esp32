#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint8_t bda[6]; uint16_t min_int, max_int, latency, timeout; } esp_ble_conn_update_params_t;
static inline esp_err_t esp_ble_gap_set_prefer_conn_params(uint8_t *bd, uint16_t a, uint16_t b, uint16_t c, uint16_t d) { (void)bd; (void)a; (void)b; (void)c; (void)d; return ESP_OK; }
static inline esp_err_t esp_ble_gap_update_conn_params(esp_ble_conn_update_params_t *p) { (void)p; return ESP_OK; }
