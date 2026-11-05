#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103

static inline const char *esp_err_to_name(esp_err_t e) {
    return (e == ESP_OK) ? "OK" : "ERR";
}

typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define xSemaphoreCreateMutex() ((SemaphoreHandle_t)1)
extern int g_inj_sem_fail;
extern void (*g_take_hook)(void); /* e16: хук на каждый xSemaphoreTake (имитация записей, пришедших в ходе операции) */
static inline void sim_take_hook(void) { void (*h)(void) = g_take_hook; if (h) { g_take_hook = NULL; h(); } }
#define xSemaphoreTake(m, t) ((void)(m), (void)(t), sim_take_hook(), (BaseType_t)(g_inj_sem_fail ? pdFALSE : pdTRUE))
#define xSemaphoreGive(m) ((void)(m), (BaseType_t)pdTRUE)

typedef struct { void *pxStackBase; } TaskStatus_t;

static inline UBaseType_t uxTaskGetSystemState(TaskStatus_t *a, UBaseType_t n, uint32_t *t) {
    (void)a;
    (void)n;
    (void)t;
    return 0;
}

#define esp_ptr_external_ram(p) ((void)(p), 0)
#define esp_ptr_internal(p) ((void)(p), 1)

extern int g_sim_verbose;

#define ESP_LOGI(tag, fmt, ...) do { if (g_sim_verbose) { printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } } while (0)
#define ESP_LOGW(tag, fmt, ...) do { if (g_sim_verbose) { printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } } while (0)
#define ESP_LOGE(tag, fmt, ...) do { if (g_sim_verbose) { printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } } while (0)
#define ESP_LOGD(tag, fmt, ...) do { if (g_sim_verbose) { printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } } while (0)

extern int64_t g_now_us;

static inline int64_t esp_timer_get_time(void) {
    return g_now_us;
}

#define MALLOC_CAP_SPIRAM 1

/* Инжекция отказов (#DBG-1): skip - сколько совпадений пропустить, count - сколько затем отказать. */
typedef struct { int skip; int count; } inj_t;
static inline int inj_hit(inj_t *i) {
    if (i->skip > 0) { i->skip--; return 0; }
    if (i->count > 0) { i->count--; return 1; }
    return 0;
}
extern inj_t g_inj_malloc;

static inline void *heap_caps_malloc(size_t n, uint32_t caps) {
    (void)caps;
    if (inj_hit(&g_inj_malloc)) return NULL;
    if (n == 0) return NULL;   /* как на плате: heap_caps_malloc(0) == NULL (IDF 5.4, heap_caps_base.c:97) */
    return malloc(n);
}

static inline void heap_caps_free(void *p) {
    free(p);
}

typedef struct {
    const char *base_path;
    const char *partition_label;
    bool format_if_mount_failed;
    bool dont_mount;
} esp_vfs_littlefs_conf_t;

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf);
esp_err_t esp_littlefs_info(const char *partition_label, size_t *total_bytes, size_t *used_bytes);

/* e16: дополнения для af_log.c в стенде */
#ifndef MALLOC_CAP_INTERNAL
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
#endif
