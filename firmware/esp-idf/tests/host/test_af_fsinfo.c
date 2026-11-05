#include "pfs_stubs.h"
#include "af_fsinfo.h"
#include <stdio.h>

int64_t g_now_us;
int g_inj_sem_fail;
void (*g_take_hook)(void);
int g_sim_verbose;
inj_t g_inj_malloc;

static int g_calls;
static const char *g_last_label;
static size_t g_fake_used;
static esp_err_t g_fake_rc;

esp_err_t esp_littlefs_info(const char *label, size_t *total, size_t *used) {
    g_calls++;
    g_last_label = label;
    if (total) *total = 12451840;
    if (used) *used = g_fake_used;
    return g_fake_rc;
}

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c) {
    (void)c;
    return ESP_OK;
}

static int g_fail;
static int g_checks;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

int main(void) {
    size_t total, used;
    esp_err_t rc;

    // Сброс состояния перед каждым сценарием
    #define RESET() do { \
        af_fsinfo_invalidate(); \
        g_calls = 0; \
        g_fake_rc = ESP_OK; \
        g_fake_used = 700000; \
    } while(0)

    // Сценарий 1: Первый вызов
    RESET();
    g_now_us = 1000000;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);
    CHECK(total == 12451840);
    CHECK(used == 700000);
    CHECK(g_last_label != NULL && g_last_label[0] == 's'); // "storage"

    // Сценарий 2: Кэш внутри TTL
    g_now_us = 1000000 + 29 * 1000000;
    g_fake_used = 900000;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);
    CHECK(used == 700000);

    // Сценарий 3: Кэш истёк
    g_now_us = 1000000 + 31 * 1000000;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 2);
    CHECK(used == 900000);

    // Сценарий 4: Invalidate
    af_fsinfo_invalidate();
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 3);

    // Сценарий 5: Ошибка не кэшируется
    af_fsinfo_invalidate();
    g_fake_rc = ESP_FAIL;
    total = 777; used = 888;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_FAIL);
    CHECK(total == 777);
    CHECK(used == 888);
    CHECK(g_calls == 4);
    
    g_fake_rc = ESP_OK;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 5);

    // Сценарий 6: NULL-указатели
    af_fsinfo_invalidate();
    g_now_us = 1000000;
    rc = af_fsinfo_get(NULL, NULL);
    CHECK(rc == ESP_OK);
    
    total = 0;
    rc = af_fsinfo_get(&total, NULL);
    CHECK(rc == ESP_OK);
    
    used = 0;
    rc = af_fsinfo_get(NULL, &used);
    CHECK(rc == ESP_OK);

    // Сценарий 7: g_now_us = 0
    RESET();
    g_now_us = 0;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);
    
    // Второй вызов при том же времени не должен дёргать esp_littlefs_info
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);

    // Сценарий 8: Граница TTL
    RESET();
    int64_t t0 = 5000000;
    g_now_us = t0;
    total = 0; used = 0;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);
    
    // Ровно TTL (30s) - кэш ещё жив (строгое >)
    g_now_us = t0 + 30000000;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 1);
    
    // TTL + 1us - кэш истёк
    g_now_us = t0 + 30000001;
    rc = af_fsinfo_get(&total, &used);
    CHECK(rc == ESP_OK);
    CHECK(g_calls == 2);

    printf("TESTS %d FAIL %d\n", g_checks, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
