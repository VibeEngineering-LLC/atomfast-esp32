#include "af_net.h"
#include "boot_config.h"
#include "ota_github_client.h"
#include "ota_busy.h"
#include "af_store.h"
#include "af_env_drv.h"
#include "af_mqtt_drv.h"
#include "af_log_api.h"
#include "af_push_drv.h"
#include "net_time.h"
#include "debug_log_ring.h"
#include "http_io_gate.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include <inttypes.h>
#include <sys/time.h>
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "ota_mark_valid_plan.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static int mark_app_valid(void *ctx)
{
    (void)ctx;
    return esp_ota_mark_app_valid_cancel_rollback();
}

// Колбэк синхронизации времени: фиксируем факт SNTP
static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    net_time_mark_sntp();
    af_log_event(1, "time", "sntp synced");
}

// Выделение памяти cJSON в PSRAM с фолбэком во внутреннюю RAM
static uint32_t s_cjson_spill, s_alloc_fail_n, s_alloc_fail_size, s_alloc_fail_caps;
static __thread bool t_cjson_try;

static void *cjson_psram_malloc(size_t sz)
{
    t_cjson_try = true;
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    t_cjson_try = false;
    if (!p) {
        __atomic_fetch_add(&s_cjson_spill, 1, __ATOMIC_RELAXED);
        p = heap_caps_malloc(sz, MALLOC_CAP_DEFAULT);
    }
    return p;
}

// Обработчик неудачных аллокаций
static void alloc_failed_cb(size_t size, uint32_t caps, const char *fn)
{
    (void)fn;
    if (t_cjson_try) return;
    __atomic_fetch_add(&s_alloc_fail_n, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&s_alloc_fail_size, (uint32_t)size, __ATOMIC_RELAXED);
    __atomic_store_n(&s_alloc_fail_caps, caps, __ATOMIC_RELAXED);
    af_log_note_alloc_fail(size, caps); /* e16: только атомарные счётчики, запись в журнал - в af_log_tick */
}

// Получение диагностической информации о памяти
void mem_diag_get(uint32_t *n, uint32_t *last_size, uint32_t *last_caps, uint32_t *cjson_spill)
{
    *n = __atomic_load_n(&s_alloc_fail_n, __ATOMIC_RELAXED);
    *last_size = __atomic_load_n(&s_alloc_fail_size, __ATOMIC_RELAXED);
    *last_caps = __atomic_load_n(&s_alloc_fail_caps, __ATOMIC_RELAXED);
    *cjson_spill = __atomic_load_n(&s_cjson_spill, __ATOMIC_RELAXED);
}

// Инициализация SNTP
static void init_sntp(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
}

void app_main(void)
{
    af_log_init(); /* e16: журнал событий - самым первым (кольцо в PSRAM, причина сброса) */
    ESP_LOGI(TAG, "atomfast_gw 0.1.0-e1 starting");
    
    cJSON_Hooks cj_hooks = { .malloc_fn = cjson_psram_malloc, .free_fn = free };
    cJSON_InitHooks(&cj_hooks);
    heap_caps_register_failed_alloc_callback(alloc_failed_cb);

    wifi_manager_init();

    uint32_t sess = boot_config_bump_session(0);
    if (sess == 0) {
        ESP_LOGE(TAG, "Boot counter unavailable");
    }

    debug_log_ring_boot();

    if (af_store_init() != ESP_OK) ESP_LOGE(TAG, "af_store_init failed");
    af_env_drv_start(); // e10: датчик воздуха (I2C); ошибки не мешают остальной работе
    af_mqtt_drv_start(); // e12: MQTT + HA discovery (по умолчанию выкл); ошибки не мешают остальной работе
    af_push_start(); // Э8: отправка истории на приёмник (по умолчанию выкл); ошибки не мешают остальной работе
    ble_gw_init();

    net_run_mode_t net_mode = wifi_manager_mode();
    if (net_mode == NET_MODE_SETUP) {
        while (1) {
            ESP_LOGI(TAG, "SETUP: connect SSID=atomfast-gw-setup -> http://192.168.4.1");
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    ota_busy_init();
    ota_gh_client_init();
    bool httpd_up = web_server_init();

    if (net_mode == NET_MODE_STA) {
        init_sntp();
    } else {
        ESP_LOGI(TAG, "SNTP disabled in field AP mode, time from browser (POST /api/time)");
    }

    ESP_LOGI(TAG, "All subsystems initialized");

    bool ota_valid_marked = false;
    uint32_t seconds_since_boot = 0;
    uint32_t tick_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        seconds_since_boot += 10;
        tick_count++;

        // Подтверждение валидности OTA образа
        bool can_mark = ota_mark_valid_should_fire(wifi_is_connected(), httpd_up, seconds_since_boot, 30);
        int mark_error = ESP_OK;
        if (ota_mark_valid_attempt(&ota_valid_marked, can_mark, mark_app_valid, NULL, &mark_error)) {
            if (mark_error == ESP_OK) {
                af_log_event(1, "ota", "image marked valid");
            } else {
                ESP_LOGW(TAG, "Failed to mark app valid: %s", esp_err_to_name(mark_error));
            }
        }

        // Периодическая попытка возврата в STA режим
        if (tick_count % 15 == 0) {
            wifi_manager_try_return_to_sta();
        }

        // Логирование статуса
        ESP_LOGI(TAG, "WiFi:%s BLE:%s", 
                 wifi_is_connected() ? "OK" : "--",
                 ble_gw_is_ready() ? "OK" : "--");
    }
}
