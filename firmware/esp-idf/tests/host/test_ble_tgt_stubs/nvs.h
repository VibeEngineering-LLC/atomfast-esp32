#pragma once
/* b29 поток H: NVS-заглушка для test_ble_tgt.c. Запись попадает во «флеш» только на nvs_commit;
   сбои задаются счётчиками FN.fail_* (>0 — столько раз, -1 — всегда), FN.fail_erase_tnm_from — с какого вызова стирания tnm. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_err.h"

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

typedef struct { int has; uint8_t v[32]; size_t n; } fnvs_val_t;
typedef struct {
    fnvs_val_t tgt, tnm;          /* флеш */
    int p_tgt, p_tnm;             /* незакоммиченное: 0 нет, 1 запись, 2 стирание */
    fnvs_val_t w_tgt, w_tnm;      /* незакоммиченные значения */
    int fail_set_tnm, fail_get_tnm, fail_commit_tnm;
    int fail_erase_tnm_from, erase_tnm_calls, set_tnm_calls;
    int freeze_after_tgt, frozen; /* отказ питания/флеша сразу после записи цели: дальше запись не проходит */
} fnvs_t;
static fnvs_t FN;

static inline int fnvs__hit(int *cnt) {
    if (*cnt == 0) return 0;
    if (*cnt > 0) (*cnt)--;
    return 1;
}
static inline void fnvs_put(fnvs_val_t *d, const void *v, size_t n) {
    d->has = 1;
    d->n = n;
    memcpy(d->v, v, n);
}
static inline void fnvs__drop(void) { FN.p_tgt = 0; FN.p_tnm = 0; }

static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h) {
    (void)m;
    if (strcmp(ns, "afble") != 0) return ESP_ERR_INVALID_ARG;
    fnvs__drop();
    *h = 1;
    return ESP_OK;
}
static inline void nvs_close(nvs_handle_t h) { (void)h; fnvs__drop(); }

static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *n) {
    (void)h;
    if (strcmp(k, "tgt") != 0) return ESP_ERR_INVALID_ARG;
    if (!FN.tgt.has) return ESP_ERR_NVS_NOT_FOUND;
    if (*n < FN.tgt.n) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, FN.tgt.v, FN.tgt.n);
    *n = FN.tgt.n;
    return ESP_OK;
}
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t n) {
    (void)h;
    if (strcmp(k, "tgt") != 0 || n > sizeof FN.w_tgt.v) return ESP_ERR_INVALID_ARG;
    fnvs_put(&FN.w_tgt, v, n);
    FN.p_tgt = 1;
    return ESP_OK;
}
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *n) {
    (void)h;
    if (strcmp(k, "tnm") != 0) return ESP_ERR_INVALID_ARG;
    if (fnvs__hit(&FN.fail_get_tnm)) return ESP_FAIL;
    if (!FN.tnm.has) return ESP_ERR_NVS_NOT_FOUND;
    if (*n < FN.tnm.n) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, FN.tnm.v, FN.tnm.n);
    *n = FN.tnm.n;
    return ESP_OK;
}
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *s) {
    (void)h;
    size_t n = strlen(s) + 1;
    if (strcmp(k, "tnm") != 0 || n > sizeof FN.w_tnm.v) return ESP_ERR_INVALID_ARG;
    FN.set_tnm_calls++;
    if (fnvs__hit(&FN.fail_set_tnm)) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    fnvs_put(&FN.w_tnm, s, n);
    FN.p_tnm = 1;
    return ESP_OK;
}
static inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) {
    (void)h;
    if (strcmp(k, "tnm") == 0) {
        int i = FN.erase_tnm_calls++;
        if (FN.fail_erase_tnm_from >= 0 && i >= FN.fail_erase_tnm_from) return ESP_FAIL;
        if (!FN.tnm.has) return ESP_ERR_NVS_NOT_FOUND;
        FN.p_tnm = 2;
        return ESP_OK;
    }
    if (strcmp(k, "tgt") == 0) {
        if (!FN.tgt.has) return ESP_ERR_NVS_NOT_FOUND;
        FN.p_tgt = 2;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}
static inline esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    if (FN.frozen || (FN.p_tnm == 1 && fnvs__hit(&FN.fail_commit_tnm))) { fnvs__drop(); return ESP_FAIL; }
    if (FN.p_tgt == 1) { FN.tgt = FN.w_tgt; FN.frozen = FN.freeze_after_tgt; }
    if (FN.p_tgt == 2) FN.tgt.has = 0;
    if (FN.p_tnm == 1) FN.tnm = FN.w_tnm;
    if (FN.p_tnm == 2) FN.tnm.has = 0;
    fnvs__drop();
    return ESP_OK;
}
