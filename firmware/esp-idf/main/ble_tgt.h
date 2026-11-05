#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_gap_ble_api.h"

static inline uint32_t af_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline void ble_tl(const char *what) {
    ESP_LOGI("ble_t", "t=%lu %s", (unsigned long)af_now_ms(), what);
}

#define AF_CI_MIN 6
#define AF_CI_MAX 12
#define AF_CI_LAT 0
#define AF_CI_TO 500

static inline void ble_fast_conn_prime(const uint8_t addr[6]) {
    esp_err_t rc = esp_ble_gap_set_prefer_conn_params((uint8_t *)addr, AF_CI_MIN, AF_CI_MAX, AF_CI_LAT, AF_CI_TO);
    if (rc != ESP_OK) {
        ESP_LOGW("ble_t", "prefer params fail %d", (int)rc);
    }
}

static inline void ble_fast_conn_now(const uint8_t addr[6]) {
    esp_ble_conn_update_params_t p;
    memset(&p, 0, sizeof(p));
    memcpy(p.bda, addr, 6);
    p.min_int = AF_CI_MIN;
    p.max_int = AF_CI_MAX;
    p.latency = AF_CI_LAT;
    p.timeout = AF_CI_TO;
    esp_err_t rc = esp_ble_gap_update_conn_params(&p);
    if (rc != ESP_OK) {
        ESP_LOGW("ble_t", "conn update fail %d", (int)rc);
    } else {
        ble_tl("conn update req");
    }
}

/* e16e (perf-audit tasks-ble P1-2): после CCCD пакет приходит раз в 2 с - интервал 45-100 мс (36..80 x 1.25 мс), латентность 0.
   Риск: джиттер метки rx_ms до интервала соединения (решение оператора 2026-10-27). */
#define AF_CI_RELAX_MIN 36
#define AF_CI_RELAX_MAX 80

static inline void ble_relax_conn_now(const uint8_t addr[6]) {
    esp_ble_conn_update_params_t p;
    memset(&p, 0, sizeof(p));
    memcpy(p.bda, addr, 6);
    p.min_int = AF_CI_RELAX_MIN;
    p.max_int = AF_CI_RELAX_MAX;
    p.latency = AF_CI_LAT;
    p.timeout = AF_CI_TO;
    esp_err_t rc = esp_ble_gap_update_conn_params(&p);
    if (rc != ESP_OK) {
        ESP_LOGW("ble_t", "relax update fail %d", (int)rc);
    } else {
        ble_tl("conn relax req");
    }
}

static inline esp_err_t ble_tgt_read(uint8_t addr[6], uint8_t *atype) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("afble", NVS_READONLY, &h);
    if (rc != ESP_OK) {
        return rc;
    }
    uint8_t b[7];
    size_t n = sizeof(b);
    rc = nvs_get_blob(h, "tgt", b, &n);
    nvs_close(h);
    if (rc != ESP_OK) {
        return rc;
    }
    if (n != 7) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(addr, b, 6);
    *atype = b[6];
    return ESP_OK;
}

static inline bool ble_tgt_load(uint8_t addr[6], uint8_t *atype) {
    bool ok = (ble_tgt_read(addr, atype) == ESP_OK);
    if (ok) {
        ble_fast_conn_prime(addr);
    }
    return ok;
}

static inline esp_err_t ble_tgt_save(const uint8_t addr[6], uint8_t atype) {
    ble_fast_conn_now(addr);

    uint8_t b[7];
    memcpy(b, addr, 6);
    b[6] = atype;

    uint8_t a0[6];
    uint8_t t0;
    if (ble_tgt_read(a0, &t0) == ESP_OK && memcmp(a0, addr, 6) == 0 && t0 == atype) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t rc = nvs_open("afble", NVS_READWRITE, &h);
    if (rc != ESP_OK) {
        return rc;
    }
    rc = nvs_set_blob(h, "tgt", b, 7);
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    if (rc != ESP_OK) {
        return rc;
    }

    uint8_t a_rb[6];
    uint8_t t_rb;
    rc = ble_tgt_read(a_rb, &t_rb);
    if (rc != ESP_OK) {
        return rc;
    }
    if (memcmp(a_rb, addr, 6) != 0 || t_rb != atype) {
        return ESP_FAIL;
    }

    ESP_LOGI("ble_t", "target saved to NVS");
    return ESP_OK;
}

/* e16i b21: имя прибора (до 15 символов) хранится отдельным ключом tnm; ошибка чтения - имя пустое */
static inline esp_err_t ble_tgt_name_read(char *name, size_t cap) {
    nvs_handle_t h;
    if (cap == 0) return ESP_ERR_INVALID_ARG;
    name[0] = '\0';
    esp_err_t rc = nvs_open("afble", NVS_READONLY, &h);
    if (rc != ESP_OK) return rc;
    size_t n = cap;
    rc = nvs_get_str(h, "tnm", name, &n);
    nvs_close(h);
    if (rc != ESP_OK) name[0] = '\0';
    return rc;
}

static inline esp_err_t ble_tgt_name_save(const char *name) {
    char cur[16];
    if (name == NULL) return ESP_ERR_INVALID_ARG;
    if (ble_tgt_name_read(cur, sizeof cur) == ESP_OK && strcmp(cur, name) == 0) return ESP_OK;
    nvs_handle_t h;
    esp_err_t rc = nvs_open("afble", NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;
    rc = nvs_set_str(h, "tnm", name);
    if (rc == ESP_OK) rc = nvs_commit(h);
    nvs_close(h);
    return rc;
}
/* b29 (B06): стирание имени; отсутствие ключа — не ошибка */
static inline esp_err_t ble_tgt_name_erase(void) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("afble", NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;
    rc = nvs_erase_key(h, "tnm");
    if (rc == ESP_OK) rc = nvs_commit(h);
    else if (rc == ESP_ERR_NVS_NOT_FOUND) rc = ESP_OK;
    nvs_close(h);
    return rc;
}

/* b29 (B06): цель и имя одной операцией. Имя прежнего прибора стирается ДО записи цели, поэтому после
   любого сбоя в NVS не остаётся новой цели со старым именем. 0 — записаны цель и имя;
   BLE_TGT_SAVE_NONAME — цель записана, имени в NVS нет (имя в ОЗУ не показывать); -1 — цель не записана. */
#define BLE_TGT_SAVE_NONAME 1
#define BLE_TGT_NAME_TRIES 2
static inline int ble_tgt_save_named(const uint8_t addr[6], uint8_t atype, const char *name) {
    char rb[16];
    if (name == NULL) name = "";
    if (ble_tgt_name_erase() != ESP_OK) return -1; /* @tn_erase_first */
    if (ble_tgt_save(addr, atype) != ESP_OK) return -1;
    for (int i = 0; i < BLE_TGT_NAME_TRIES; i++) {
        if (ble_tgt_name_save(name) == ESP_OK && ble_tgt_name_read(rb, sizeof rb) == ESP_OK && strcmp(rb, name) == 0) return 0; /* @tn_check */
    }
    (void)ble_tgt_name_erase(); /* @tn_final_erase: в ОЗУ имени не будет — и в NVS тоже */
    return BLE_TGT_SAVE_NONAME;
}

static inline esp_err_t ble_tgt_clear(void) {
    nvs_handle_t h;
    esp_err_t rc = nvs_open("afble", NVS_READWRITE, &h);
    if (rc != ESP_OK) {
        return rc;
    }
    (void)nvs_erase_key(h, "tnm");
    rc = nvs_erase_key(h, "tgt");
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    } else if (rc == ESP_ERR_NVS_NOT_FOUND) {
        rc = ESP_OK;
    }
    nvs_close(h);
    return rc;
}
