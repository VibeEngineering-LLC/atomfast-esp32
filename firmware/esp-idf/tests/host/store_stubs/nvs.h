#pragma once
/* e16i b29 (V-22): хост-заглушка nvs.h для сборки af_store.c в store_sim.
   NVS пуст: чтение (open READONLY) -> NOT_FOUND = значения по умолчанию, запись принимается и не сохраняется. */
#include <stdint.h>
#include "pfs_stubs.h"
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
typedef int nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h)
{
    (void)ns; *h = (int)m;
    return m == NVS_READONLY ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
static inline void nvs_close(nvs_handle_t h) { (void)h; }
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; (void)k; (void)v; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) { (void)h; (void)k; (void)v; return ESP_ERR_NVS_NOT_FOUND; }
