/* Хост-стенд af_store.c (e8 пп. 9, 11, 12): заглушки FreeRTOS/ESP-IDF, боевой af_store.c включается как есть.
 * store_sim <сценарий>; результат строкой STORE <имя> RED|GREEN. Флаг -DBASELINE: оригинал без новых API. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "pfs_stubs.h"
#include "af_store.h"
#include "af_persist_fs.h"

/* e10: взятие мьютекса, которого нет (NULL), считается и отказывает - ловит обращение к хранилищу до init */
#undef xSemaphoreTake
static int g_null_take;
#define xSemaphoreTake(m, t) ((void)(t), (m) == NULL ? (g_null_take++, (BaseType_t)pdFALSE) : (BaseType_t)(g_inj_sem_fail ? pdFALSE : pdTRUE))

#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define pdFAIL 0
typedef void *QueueHandle_t;

int g_sim_verbose = 0;
int64_t g_now_us = 0;
int g_inj_sem_fail;
inj_t g_inj_malloc;

static int g_task_fail;
static int g_flush_calls;
static void (*g_handlers[8])(void);
static void (*g_last_hook)(void); /* e10: копия зарегистрированного хука (даже после снятия) для сценария p9b */
static int g_nhandlers;
static uint32_t g_clock = 1760000000u;
static int g_fail;

typedef struct { size_t isz; int count; uint8_t buf[16][128]; } sq_t;
static sq_t g_q;

static time_t sim_time(time_t *t) {
    g_clock += 2;
    if (t) *t = g_clock;
    return g_clock;
}
#define time(x) sim_time(x)

esp_err_t esp_register_shutdown_handler(void (*h)(void)) {
    g_last_hook = h;
    if (g_nhandlers < 8) g_handlers[g_nhandlers++] = h;
    return ESP_OK;
}

esp_err_t esp_unregister_shutdown_handler(void (*h)(void)) {
    for (int i = 0; i < g_nhandlers; i++) {
        if (g_handlers[i] != h) continue;
        g_handlers[i] = g_handlers[--g_nhandlers];
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

static QueueHandle_t xQueueCreate(unsigned len, size_t isz) {
    (void)len;
    memset(&g_q, 0, sizeof(g_q));
    g_q.isz = isz;
    return &g_q;
}

static void vQueueDelete(QueueHandle_t q) { (void)q; }

static BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t) {
    (void)q;
    (void)t;
    if (g_q.count >= 16) return pdFALSE;
    memcpy(g_q.buf[g_q.count++], it, g_q.isz);
    return pdTRUE;
}

static BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t t) {
    (void)q;
    (void)t;
    if (g_q.count == 0) return pdFALSE;
    memcpy(out, g_q.buf[0], g_q.isz);
    memmove(g_q.buf[0], g_q.buf[1], (size_t)(--g_q.count) * sizeof(g_q.buf[0]));
    return pdTRUE;
}

static void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }

static BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, unsigned prio, void *h) {
    (void)fn;
    (void)name;
    (void)stack;
    (void)arg;
    (void)prio;
    (void)h;
    return g_task_fail ? pdFAIL : pdPASS;
}

esp_err_t af_pfs_mount(void) { return ESP_OK; }
void af_pfs_attach(af_store_core_t *c, SemaphoreHandle_t m) { (void)c; (void)m; }
esp_err_t af_pfs_restore(void) { return ESP_OK; }
void af_pfs_tick(void) {}
void af_log_tick(void) {} /* e16: журнал событий в стенде af_store не участвует */
void af_pfs_flush_now(void) { g_flush_calls++; }
int af_pfs_status_json(char *b, size_t c) { (void)b; (void)c; return 0; }
/* e16i b29 (V-22): af_store.c:460 (очистка при смене прибора) - стенду достаточно "очищено без ошибок" */
int af_pfs_clear(uint32_t mask, af_pfs_clear_res_t *res) { (void)mask; if (res) memset(res, 0, sizeof *res); return 0; }

#include "af_store.c"

#define SCHECK(c) do { if (!(c)) { printf("STORE-FAIL %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

static af_sample_t mk_sample(void) {
    af_sample_t s;
    memset(&s, 0, sizeof(s));
    s.dose_msv = 1.0f;
    s.doserate_usvh = 0.1f;
    s.pulses_2s = 3;
    s.cps = 1.5f;
    s.battery = 80;
    s.temp_c = 20;
    return s;
}

/* п.9: после неудачи xTaskCreate хук завершения не должен остаться и трогать освобождённые буферы */
static void sc_p9(void) {
    g_task_fail = 1;
    SCHECK(af_store_init() != ESP_OK);
    SCHECK(g_nhandlers == 0);
    for (int i = 0; i < g_nhandlers; i++) g_handlers[i]();
    SCHECK(g_flush_calls == 0);
}

/* e10 (долг e8): страховка !s_ready в хуке. Хук, вызванный после освобождения буферов (init не удался), не должен ничего сбрасывать */
static void sc_p9b(void) {
    g_task_fail = 1;
    SCHECK(af_store_init() != ESP_OK);
    SCHECK(g_last_hook != NULL);
    if (g_last_hook) g_last_hook();
    SCHECK(g_flush_calls == 0);
}

/* e10: запись воздуха - до готовности и при таймауте мьютекса отказ без изменения кольца; в готовом хранилище принимается и читается */
static void sc_p13(void) {
    af_env_rec_t r = { 1761300000u, 2650, 100218u, AF_ENV_F_T | AF_ENV_F_P };
    SCHECK(af_store_submit_env(&r) == -1);
    SCHECK(g_null_take == 0);
    SCHECK(af_store_init() == ESP_OK);
    SCHECK(af_store_submit_env(NULL) == -1);
    SCHECK(af_store_submit_env(&r) == 0);
    g_inj_sem_fail = 1;
    SCHECK(af_store_submit_env(&r) == -1);
    g_inj_sem_fail = 0;
    af_store_status_t st;
    af_store_get_status(&st);
    SCHECK(st.env.seq == 1);
    SCHECK(st.env.count == 1);
    uint8_t buf[AF_REC_ENV_SIZE * 4];
    uint32_t next = 0, lost = 0;
    SCHECK(af_store_read(2, 0, buf, 4, &next, &lost) == 1);
    af_env_rec_t g = { 0, 0, 0, 0 };
    SCHECK(af_env_decode(buf, &g) == 0);
    SCHECK(g.ts == 1761300000u && g.t_c100 == 2650 && g.p_pa == 100218u);
}

/* п.11: хук завершения сначала разбирает очередь выборок в ядро, затем сбрасывает */
static void sc_p11(void) {
    SCHECK(af_store_init() == ESP_OK);
    af_sample_t s = mk_sample();
    for (int i = 0; i < 5; i++) af_store_submit(&s);
    for (int i = 0; i < g_nhandlers; i++) g_handlers[i]();
    af_store_status_t st;
    af_store_get_status(&st);
    SCHECK(st.samples == 5);
    SCHECK(g_flush_calls == 1);
}

/* п.12: таймаут мьютекса не должен сбрасывать курсор клиента в 0 и должен быть виден счётчиком */
static void sc_p12(void) {
    SCHECK(af_store_init() == ESP_OK);
    uint8_t buf[64 * 8];
    uint32_t next = 5, lost = 5;
    g_inj_sem_fail = 1;
    uint32_t n = af_store_read(0, 77, buf, 8, &next, &lost);
    g_inj_sem_fail = 0;
    SCHECK(n == 0);
    SCHECK(next == 77);
    SCHECK(lost == 0);
#ifndef BASELINE
    SCHECK(af_store_read_timeouts() == 1);
#endif
}

/* e16i b29 (V-22): очередь с gen. Выборка с чужим поколением (после смены прибора) не попадает в ядро; свои - попадают */
static void sc_p14(void) {
    SCHECK(af_store_init() == ESP_OK);
    af_sample_t s = mk_sample();
    uint32_t g = af_store_gen();
    for (int i = 0; i < 3; i++) af_store_submit_gen(&s, g);
    for (int i = 0; i < 2; i++) af_store_submit_gen(&s, g + 1);
    for (int i = 0; i < g_nhandlers; i++) g_handlers[i]();
    af_store_status_t st;
    af_store_get_status(&st);
    SCHECK(st.samples == 3);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (strcmp(argv[1], "p9_taskfail") == 0) sc_p9();
    else if (strcmp(argv[1], "p9b_hook_guard") == 0) sc_p9b();
    else if (strcmp(argv[1], "p13_env_submit") == 0) sc_p13();
    else if (strcmp(argv[1], "p11_drain") == 0) sc_p11();
    else if (strcmp(argv[1], "p12_timeout") == 0) sc_p12();
    else if (strcmp(argv[1], "p14_gen") == 0) sc_p14();
    else return 2;
    printf("STORE %s %s failures=%d\n", argv[1], g_fail ? "RED" : "GREEN", g_fail);
    return g_fail ? 1 : 0;
}
