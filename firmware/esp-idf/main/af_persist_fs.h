#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "af_store_core.h"

/* Персистентность истории на LittleFS (раздел "storage"): журнальные сегменты, логика - af_persist.h. */

/* Подключить раздел (mount, при отказе - форматирование) и каталог /storage/h. */
esp_err_t af_pfs_mount(void);
/* Привязать ядро и его мьютекс (используются при сбросе). Вызывать до restore/tick. */
void af_pfs_attach(af_store_core_t *core, SemaphoreHandle_t mtx);
/* Восстановить оба кольца из сегментов (до старта задачи приёма). ESP_OK даже при пустом хранилище. */
esp_err_t af_pfs_restore(void);
/* Вызывать не реже раза в секунду из задачи af_store: сбрасывает пачку, если пора (af_flush_due). */
void af_pfs_tick(void);
/* Принудительный сброс (перед esp_restart); безопасен, если хранилище не смонтировано. */
void af_pfs_flush_now(void);
/* Фрагмент JSON без внешних скобок-обёртки: {"mounted":..,...} в buf; возврат длины или -1. */
int af_pfs_status_json(char *buf, size_t cap);

/* e16: очистка истории по кольцам (маска AF_CLR_MASK_*: raw 1, agg 2, env 4). seq НЕ сбрасывается, epoch растёт. */
typedef struct {
    uint32_t removed[3];
    uint32_t seq[3];
    uint32_t epoch[3];
    int persisted; /* 1 - флеш очищен полностью; 0 - только ОЗУ или очистка флеша неполная */
} af_pfs_clear_res_t;
/* 0 готово; -1 не готово/занято/неверные аргументы; -2 ОЗУ очищено, флеш неполно (маркер остаётся до перезагрузки); -3 маркер не записан, ничего не изменено */
int af_pfs_clear(uint32_t mask, af_pfs_clear_res_t *res);
/* T-86: 1 - хранилище смонтировано (нужно для предиката P_CLEAR). */
int af_pfs_mounted(void);
