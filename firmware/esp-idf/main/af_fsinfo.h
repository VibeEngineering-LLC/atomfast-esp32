#pragma once
#include <stddef.h>
#include "esp_err.h"

/* e16e (perf-audit S2): общий кэш esp_littlefs_info("storage") на 30 с; обход всей ФС идёт под глобальной блокировкой LittleFS. */
#define AF_FSINFO_TTL_US (30LL * 1000000LL)

/* Как esp_littlefs_info("storage", total, used), но не чаще раза в AF_FSINFO_TTL_US. Ошибка не кэшируется и возвращается вызывающему. */
esp_err_t af_fsinfo_get(size_t *total, size_t *used);

/* Сбросить кэш (после форматирования/очистки хранилища). */
void af_fsinfo_invalidate(void);
