#include "af_store.h"
#include "af_store_core.h"
#include "af_persist_fs.h"
#include "af_log_api.h"
#include "af_hist_core.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "af_store";

typedef struct {
    uint32_t ts;
    uint32_t gen; /* b22 F01: поколение кольцевого ядра на момент постановки в очередь */
    af_sample_t s;
} item_t;

static af_store_core_t s_core;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t s_q;
static uint8_t *s_raw_buf;
static uint8_t *s_agg_buf;
static uint8_t *s_env_buf;
static uint32_t s_q_dropped;
static uint32_t s_read_timeouts;
static uint32_t s_bytes;
static volatile int s_ready;

static uint32_t now_ts(void)
{
    time_t t = time(NULL);
    if (t < 1700000000) {
        return 0;
    }
    return (uint32_t)t;
}

static void store_free(void)
{
    if (s_raw_buf) {
        heap_caps_free(s_raw_buf);
        s_raw_buf = NULL;
    }
    if (s_agg_buf) {
        heap_caps_free(s_agg_buf);
        s_agg_buf = NULL;
    }
    if (s_env_buf) {
        heap_caps_free(s_env_buf);
        s_env_buf = NULL;
    }
    if (s_mtx) {
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
    }
    if (s_q) {
        vQueueDelete(s_q);
        s_q = NULL;
    }
    s_bytes = 0;
    s_q_dropped = 0;
    s_ready = 0;
}

static void store_shutdown(void)
{
    if (!s_ready) {
        return; /* e8 п.9: буферы уже освобождены или ещё не готовы */
    }
    /* e8 п.11: выборки, не успевшие из очереди в ядро, попадают в сброс */
    item_t it;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        while (xQueueReceive(s_q, &it, 0) == pdTRUE) {
            (void)af_core_ingest_g(&s_core, it.gen, it.ts, &it.s);
        }
        xSemaphoreGive(s_mtx);
    }
    af_pfs_flush_now();
}

static void store_task(void *arg)
{
    (void)arg;
    for (;;) {
        item_t it;
        if (xQueueReceive(s_q, &it, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
                (void)af_core_ingest_g(&s_core, it.gen, it.ts, &it.s);
                xSemaphoreGive(s_mtx);
            }
        }
        af_pfs_tick();
        af_log_tick(); /* e16: журнал событий - батч во флеш раз в минуту */
    }
}

/* Загрузка флагов «вид пишется» из NVS; вызывается один раз при инициализации */
static void store_load_enabled(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("afset", NVS_READONLY, &h);
    if (err != ESP_OK)
        return; /* пространства нет — оставляем умолчания (все = 1) */
    uint8_t v;
    if (nvs_get_u8(h, "st_raw", &v) == ESP_OK)
        s_core.en_raw = (v != 0);
    if (nvs_get_u8(h, "st_agg", &v) == ESP_OK)
        s_core.en_agg = (v != 0);
    if (nvs_get_u8(h, "st_env", &v) == ESP_OK)
        s_core.en_env = (v != 0);
    nvs_close(h);
}

/* Установка флагов записи; -1 = не менять, 0 = выкл, 1 = вкл.
 * b29 G (b26 F07/L2-04): сначала NVS, затем RAM - отказ NVS возвращает ошибку и RAM не меняет
 * (раньше RAM менялась до записи и не откатывалась, обработчик отвечал ok:true). */
esp_err_t af_store_set_enabled(int raw, int agg, int env)
{
    if (raw != -1 && raw != 0 && raw != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (agg != -1 && agg != 0 && agg != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (env != -1 && env != 0 && env != 1) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (raw == -1 && agg == -1 && env == -1) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("afset", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (raw != -1) {
            err = nvs_set_u8(h, "st_raw", (uint8_t)raw);
        }
        if (err == ESP_OK && agg != -1) {
            err = nvs_set_u8(h, "st_agg", (uint8_t)agg);
        }
        if (err == ESP_OK && env != -1) {
            err = nvs_set_u8(h, "st_env", (uint8_t)env);
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }

    if (err != ESP_OK) {
        /* RAM не тронута; ключи, записанные до отказа, NVS мог сохранить (IDF пишет при set) - повтор POST перезапишет */
        ESP_LOGW(TAG, "NVS write failed: %s, flags unchanged", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (raw != -1) {
        s_core.en_raw = (raw != 0);
    }
    if (agg != -1) {
        s_core.en_agg = (agg != 0);
    }
    if (env != -1) {
        s_core.en_env = (env != 0);
    }
    xSemaphoreGive(s_mtx);

    return ESP_OK;
}

/* Чтение текущих флагов; NULL-указатели пропускаются */
void af_store_get_enabled(int *raw, int *agg, int *env)
{
    if (!s_ready) {
        /* до инициализации — всё «пишется» */
        if (raw) *raw = 1;
        if (agg) *agg = 1;
        if (env) *env = 1;
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (raw) *raw = s_core.en_raw;
    if (agg) *agg = s_core.en_agg;
    if (env) *env = s_core.en_env;
    xSemaphoreGive(s_mtx);
}
/* Смена окна агрегатов в ОЗУ; NVS и очистку файлов делает вызывающий. */
esp_err_t af_store_set_window(uint16_t period_s)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (period_s < 60u || period_s > 1200u || (period_s % 60u) != 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t limit = (AF_STORE_RETENTION_S + period_s - 1u) / period_s;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_core_set_window(&s_core, period_s, limit);
    xSemaphoreGive(s_mtx);

    if (rc != 0) {
        ESP_LOGW(TAG, "set_window: period=%u rejected (rc=%d)", (unsigned)period_s, rc);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
esp_err_t af_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    af_quota_t q;
    if (af_quota_split(AF_STORE_TOTAL_BYTES, AF_REC_RAW_SIZE, AF_REC_AGG_SIZE, 1, 1, AF_STORE_WINDOW_S, AF_STORE_RETENTION_S, &q) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t raw_size = q.atomic_limit * AF_REC_RAW_SIZE;
    size_t agg_size = q.agg_limit * AF_REC_AGG_SIZE;

    s_raw_buf = heap_caps_malloc(raw_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_raw_buf) {
        ESP_LOGE(TAG, "raw buf alloc fail");
        return ESP_ERR_NO_MEM;
    }

    s_agg_buf = heap_caps_malloc(agg_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_agg_buf) {
        ESP_LOGE(TAG, "agg buf alloc fail");
        heap_caps_free(s_raw_buf);
        s_raw_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_bytes = raw_size + agg_size;

    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        ESP_LOGE(TAG, "mutex create fail");
        store_free();
        return ESP_ERR_NO_MEM;
    }

    s_q = xQueueCreate(AF_STORE_QUEUE_LEN, sizeof(item_t));
    if (!s_q) {
        ESP_LOGE(TAG, "queue create fail");
        store_free();
        return ESP_ERR_NO_MEM;
    }

    if (af_core_init(&s_core, s_raw_buf, raw_size, s_agg_buf, agg_size, q.atomic_limit, q.agg_limit, AF_STORE_WINDOW_S, AF_MODE_LAST_DOSE) != 0) {
        ESP_LOGE(TAG, "core init fail");
        store_free();
        return ESP_ERR_INVALID_STATE;
    }

    store_load_enabled(); /* e16i П.2: флаги видов из NVS afset (нет ключей - всё включено) */

    /* e16i: окно агрегатов из NVS afset/win_s; кольца пусты, чистить нечего */
    {
        nvs_handle_t wh;
        uint16_t ws = 0;
        if (nvs_open("afset", NVS_READONLY, &wh) == ESP_OK) {
            if (nvs_get_u16(wh, "win_s", &ws) != ESP_OK)
                ws = 0;
            nvs_close(wh);
        }
        if (ws >= 60u && ws <= 1200u && (ws % 60u) == 0u && ws != AF_STORE_WINDOW_S) {
            if (af_core_set_window(&s_core, ws, (AF_STORE_RETENTION_S + ws - 1u) / ws) != 0)
                ESP_LOGW(TAG, "win_s=%u from NVS rejected", (unsigned)ws);
        }
    }

    /* e10: кольцо env; нет памяти - история воздуха отключена, остальное работает */
    s_env_buf = heap_caps_malloc((size_t)AF_STORE_ENV_RECS * AF_REC_ENV_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_env_buf || af_core_env_init(&s_core, s_env_buf, (size_t)AF_STORE_ENV_RECS * AF_REC_ENV_SIZE, AF_STORE_ENV_RECS) != 0) {
        ESP_LOGW(TAG, "env ring unavailable, air history disabled");
        if (s_env_buf) {
            heap_caps_free(s_env_buf);
            s_env_buf = NULL;
        }
    }

    af_pfs_attach(&s_core, s_mtx);
    if (af_pfs_mount() == ESP_OK) {
        if (af_pfs_restore() != ESP_OK) {
            ESP_LOGW(TAG, "persist restore failed: write disabled for the failed kind, files kept");
        }
        esp_register_shutdown_handler(store_shutdown);
    } else {
        ESP_LOGW(TAG, "persist off: LittleFS mount failed, history is RAM only");
    }

    /* xTaskCreate: стек задачи ВСЕГДА во внутренней RAM (запись во флеш гасит кэш PSRAM). 6144: LittleFS+VFS. */
    if (xTaskCreate(store_task, "af_store", 6144, NULL, 2, NULL) != pdPASS) {
        esp_unregister_shutdown_handler(store_shutdown); /* e8 п.9: иначе хук работает по освобождённым буферам */
        ESP_LOGE(TAG, "task create fail");
        store_free();
        return ESP_ERR_NO_MEM;
    }

    s_ready = 1;
    ESP_LOGI(TAG, "init ok: raw=%lu agg=%lu bytes=%lu", (unsigned long)q.atomic_limit, (unsigned long)q.agg_limit, (unsigned long)s_bytes);
    return ESP_OK;
}

uint32_t af_store_gen(void)
{
    return __atomic_load_n(&s_core.gen, __ATOMIC_RELAXED);
}

void af_store_submit(const af_sample_t *s)
{
    af_store_submit_gen(s, af_store_gen());
}

void af_store_submit_gen(const af_sample_t *s, uint32_t gen)
{
    if (!s_ready || s == NULL) {
        return;
    }

    item_t it;
    it.ts = now_ts();
    it.gen = gen;
    it.s = *s;

    if (xQueueSend(s_q, &it, 0) != pdTRUE) {
        __atomic_fetch_add(&s_q_dropped, 1, __ATOMIC_RELAXED);
    }
}

int af_store_submit_env(const af_env_rec_t *r)
{
    if (!s_ready || r == NULL || !s_env_buf) {
        return -1;
    }
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
        return -1;
    }
    int rc = af_core_env_push(&s_core, r);
    xSemaphoreGive(s_mtx);
    return rc;
}

void af_store_get_status(af_store_status_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

    if (!s_ready) {
        out->ready = 0;
        return;
    }

    if (xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
        out->en_raw = s_core.en_raw;
        out->en_agg = s_core.en_agg;
        out->en_env = s_core.en_env;
        out->raw.cap = s_core.raw.cap_max;
        out->raw.limit = s_core.raw.limit;
        out->raw.count = s_core.raw.count;
        out->raw.seq = s_core.raw.seq;
        out->raw.oldest = af_ring_oldest_seq(&s_core.raw);
        out->raw.dropped = s_core.raw.dropped;
        out->raw.rejected = s_core.raw.rejected;
        out->raw.epoch = s_core.raw.epoch;

        out->agg.cap = s_core.agg.cap_max;
        out->agg.limit = s_core.agg.limit;
        out->agg.count = s_core.agg.count;
        out->agg.seq = s_core.agg.seq;
        out->agg.oldest = af_ring_oldest_seq(&s_core.agg);
        out->agg.dropped = s_core.agg.dropped;
        out->agg.rejected = s_core.agg.rejected;
        out->agg.epoch = s_core.agg.epoch;

        if (s_core.env.buf) {
            out->env.cap = s_core.env.cap_max;
            out->env.limit = s_core.env.limit;
            out->env.count = s_core.env.count;
            out->env.seq = s_core.env.seq;
            out->env.oldest = af_ring_oldest_seq(&s_core.env);
            out->env.dropped = s_core.env.dropped;
            out->env.rejected = s_core.env.rejected;
            out->env.epoch = s_core.env.epoch;
        }

        out->window_s = s_core.win.period_s;
        out->samples = s_core.samples;
        out->ts0_samples = s_core.ts0_samples;
        out->win_rejected = s_core.win_rejected;
        out->agg_closed = s_core.agg_closed;

        xSemaphoreGive(s_mtx);
    }

    out->q_dropped = __atomic_load_n(&s_q_dropped, __ATOMIC_RELAXED);
    out->bytes = s_bytes;
    out->now_ts = now_ts();
    out->ready = 1;
}

/* e11, план выборки CSV под одним захватом мьютекса (-1 не готов или мьютекс занят, -2 кольца нет) */
int af_store_plan(const af_hq_params_t *p, af_hq_plan_t *plan, uint32_t *head, uint32_t *epoch)
{
    if (!s_ready || p == NULL || plan == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
        return -1;
    }

    int rc = af_core_plan(&s_core, p, plan, head, epoch);
    xSemaphoreGive(s_mtx);

    if (rc != 0) {
        memset(plan, 0, sizeof(*plan));
        if (head != NULL) {
            *head = 0;
        }
        if (epoch != NULL) {
            *epoch = 0;
        }
        return -2;
    }

    return 0;
}
uint32_t af_store_read_timeouts(void)
{
    return __atomic_load_n(&s_read_timeouts, __ATOMIC_RELAXED);
}

uint32_t af_store_read(int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost)
{
    if (!s_ready || out == NULL || max == 0) {
        if (next) *next = 0;
        if (lost) *lost = 0;
        return 0;
    }

    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
        /* e8 п.12: курсор клиента не сбрасывать; таймаут считается, обработчик CSV по счётчику отдаёт ошибку, а не усечённый успех */
        __atomic_fetch_add(&s_read_timeouts, 1, __ATOMIC_RELAXED);
        if (next) *next = since;
        if (lost) *lost = 0;
        return 0;
    }

    uint32_t res = af_core_read(&s_core, kind, since, out, max, next, lost);
    xSemaphoreGive(s_mtx);
    return res;
}

/* T-86: очистка raw+agg при смене прибора (gen++ -> af_pfs_clear -> сброс окна); persisted - из af_pfs_clear_res_t */
typedef struct { af_pfs_clear_res_t res; } dsw_ctx_t;
static int dsw_clear(uint32_t mask, void *ctx) { return af_pfs_clear(mask, &((dsw_ctx_t *)ctx)->res); }
static void dsw_lock(void *ctx) { (void)ctx; xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void dsw_unlock(void *ctx) { (void)ctx; xSemaphoreGive(s_mtx); }
int af_store_clear_for_device_change(int *persisted)
{
    dsw_ctx_t x;
    memset(&x, 0, sizeof x);
    int rc = af_core_dev_switch_clear(&s_core, dsw_clear, dsw_lock, dsw_unlock, &x);
    if (persisted != NULL) *persisted = x.res.persisted;
    return rc;
}
