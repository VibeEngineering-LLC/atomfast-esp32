#include "af_fsinfo.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "ttl_plan.h"

static size_t s_total;
static size_t s_used;
static int64_t s_at_us;

// Гонки между задачами допустимы: записи 32-битные, худший случай — лишнее обновление кэша
esp_err_t af_fsinfo_get(size_t *total, size_t *used)
{
    int64_t now = esp_timer_get_time();

    if (ttl_expired(s_at_us, now, AF_FSINFO_TTL_US)) {
        size_t t = 0, u = 0;
        esp_err_t err = esp_littlefs_info("storage", &t, &u);
        if (err != ESP_OK) {
            return err;
        }
        s_total = t;
        s_used = u;
        s_at_us = (now == 0) ? 1 : now;
    }

    if (total != NULL) {
        *total = s_total;
    }
    if (used != NULL) {
        *used = s_used;
    }
    return ESP_OK;
}

void af_fsinfo_invalidate(void)
{
    s_at_us = 0;
}
