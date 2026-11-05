/* b29 G: host-тест POST /api/settings (af_settings_http.c + af_store.c целиком): порядок шагов, откат окна,
 * отказ NVS -> ошибка, B01 (запись старого периода после смены окна). Отсчёты - боевые (real_samples.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "pfs_stubs.h"
#include "af_test.h"
#include "real_samples.h"

#ifndef REAL_N
#define REAL_N (sizeof(REAL) / sizeof(REAL[0]))
#endif

/* ── pfs_stubs globals ─────────────────────────────────────────────────────── */
int g_inj_sem_fail;
void (*g_take_hook)(void);
int g_sim_verbose;
int64_t g_now_us;
inj_t g_inj_malloc;

/* ── FreeRTOS / ESP-IDF shims (must precede af_store.c include) ────────────── */
#include "af_store.h"
#include "af_persist_fs.h"

#ifndef pdPASS
#define pdPASS 1
#endif
#ifndef pdFAIL
#define pdFAIL 0
#endif

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;

typedef struct { size_t isz; int count; uint8_t buf[16][128]; } sq_t;
static sq_t g_q;

static QueueHandle_t xQueueCreate(unsigned len, size_t isz) {
    (void)len;
    memset(&g_q, 0, sizeof(g_q));
    g_q.isz = isz;
    return &g_q;
}
static void vQueueDelete(QueueHandle_t q) { (void)q; }
static BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t) {
    (void)q; (void)t;
    if (g_q.count >= 16) return pdFAIL;
    memcpy(g_q.buf[g_q.count], it, g_q.isz);
    g_q.count++;
    return pdPASS;
}
static BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t t) {
    (void)q; (void)t;
    if (g_q.count == 0) return pdFAIL;
    memcpy(out, g_q.buf[0], g_q.isz);
    memmove(g_q.buf[0], g_q.buf[1], (size_t)(--g_q.count) * sizeof(g_q.buf[0]));
    return pdPASS;
}
static void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
static BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                              void *arg, unsigned prio, void *h) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)h;
    return pdPASS;
}

esp_err_t esp_register_shutdown_handler(void (*h)(void)) { (void)h; return ESP_OK; }
esp_err_t esp_unregister_shutdown_handler(void (*h)(void)) { (void)h; return ESP_OK; }
esp_err_t af_pfs_mount(void) { return ESP_OK; }
void af_pfs_attach(af_store_core_t *c, SemaphoreHandle_t m) { (void)c; (void)m; }
esp_err_t af_pfs_restore(void) { return ESP_OK; }
void af_pfs_tick(void) {}
void af_log_tick(void) {}
void af_pfs_flush_now(void) {}
int af_pfs_status_json(char *b, size_t c) { (void)b; (void)c; return 0; }

/* ── Include store under test ──────────────────────────────────────────────── */
#include "af_store.c"

/* Модель af_pfs_clear по af_persist_fs.c:801-922: -1 (занято, :807) и -3 (маркер, :845) - выход до изменений RAM;
 * иначе снимок seq под s_mtx (:824), мьютекс отпущен на запись флеша (:838-861) - здесь g_clr_flash_hook имитирует
 * store_task, который в это время продолжает ingest; затем удаляются записи с seq < снимка (:869-875), rc 0 или -2. */
static int g_clr_rc;
static int g_clr_calls;
static uint32_t g_clr_mask;
static void (*g_clr_flash_hook)(void);

int af_pfs_clear(uint32_t mask, af_pfs_clear_res_t *res)
{
    memset(res, 0, sizeof(*res));
    g_clr_calls++;
    g_clr_mask = mask;
    if (g_clr_rc == -1 || g_clr_rc == -3) return g_clr_rc;

    af_ring_t *ring = &s_core.agg;
    uint32_t S = ring->seq;

    if (g_clr_flash_hook) g_clr_flash_hook();

    uint32_t keep = ring->seq - S;
    if (keep > ring->count) keep = ring->count;
    uint32_t drop = ring->count - keep;

    res->removed[1] = drop;
    ring->tail = (ring->tail + drop) % ring->cap_max;
    ring->count = keep;
    ring->epoch = ring->epoch + 1;

    return g_clr_rc;
}

/* ── Other stubs used by af_settings_http.c ────────────────────────────────── */
bool web_server_csrf_ok(httpd_req_t *req) { (void)req; return true; }

static int g_ble_ext_calls;
void ble_gw_set_ext(bool on) { (void)on; g_ble_ext_calls++; }

/* HTTP response capture */
static char g_status[64];
static char g_body[512];

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) {
    (void)r;
    snprintf(g_status, sizeof(g_status), "%s", s);
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) {
    (void)r; (void)t;
    return ESP_OK;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *n, const char *v) {
    (void)r; (void)n; (void)v;
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
    (void)r;
    if (len < 0) len = 0;
    size_t n = (size_t)len;
    if (n > 511) n = 511;
    memcpy(g_body, buf, n);
    g_body[n] = '\0';
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len) {
    size_t rem = r->content_len - r->pos;
    size_t n = (len < rem) ? len : rem;
    memcpy(buf, r->body + r->pos, n);
    r->pos += n;
    return (int)n;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u) {
    (void)h; (void)u;
    return ESP_OK;
}

/* ── NVS in-memory stub ────────────────────────────────────────────────────── */
static struct { char key[16]; uint32_t val; int used; } g_nvs[16];
static const char *g_nvs_fail_set;
static const char *g_nvs_fail_commit;
static int g_nvs_sess_hit;
static int g_nvs_commits;

static int nvs_has(const char *k, uint32_t *v)
{
    for (int i = 0; i < 16; i++) {
        if (g_nvs[i].used && strcmp(g_nvs[i].key, k) == 0) {
            if (v) *v = g_nvs[i].val;
            return 1;
        }
    }
    return 0;
}

static void nvs_reset(void)
{
    memset(g_nvs, 0, sizeof(g_nvs));
    g_nvs_fail_set = NULL;
    g_nvs_fail_commit = NULL;
    g_nvs_sess_hit = 0;
    g_nvs_commits = 0;
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    (void)ns; (void)mode;
    *h = 1;
    g_nvs_sess_hit = 0;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }

esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *val)
{
    (void)h;
    uint32_t v;
    if (nvs_has(key, &v)) { *val = (uint8_t)v; return ESP_OK; }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *val)
{
    (void)h;
    uint32_t v;
    if (nvs_has(key, &v)) { *val = (uint16_t)v; return ESP_OK; }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t val)
{
    (void)h;
    if (g_nvs_fail_set && strcmp(g_nvs_fail_set, key) == 0) return ESP_FAIL;
    for (int i = 0; i < 16; i++) {
        if (g_nvs[i].used && strcmp(g_nvs[i].key, key) == 0) {
            g_nvs[i].val = val;
            if (g_nvs_fail_commit && strcmp(g_nvs_fail_commit, key) == 0) g_nvs_sess_hit = 1;
            return ESP_OK;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!g_nvs[i].used) {
            snprintf(g_nvs[i].key, sizeof(g_nvs[i].key), "%s", key);
            g_nvs[i].val = val;
            g_nvs[i].used = 1;
            if (g_nvs_fail_commit && strcmp(g_nvs_fail_commit, key) == 0) g_nvs_sess_hit = 1;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}
esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t val)
{
    (void)h;
    if (g_nvs_fail_set && strcmp(g_nvs_fail_set, key) == 0) return ESP_FAIL;
    for (int i = 0; i < 16; i++) {
        if (g_nvs[i].used && strcmp(g_nvs[i].key, key) == 0) {
            g_nvs[i].val = val;
            if (g_nvs_fail_commit && strcmp(g_nvs_fail_commit, key) == 0) g_nvs_sess_hit = 1;
            return ESP_OK;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!g_nvs[i].used) {
            snprintf(g_nvs[i].key, sizeof(g_nvs[i].key), "%s", key);
            g_nvs[i].val = val;
            g_nvs[i].used = 1;
            if (g_nvs_fail_commit && strcmp(g_nvs_fail_commit, key) == 0) g_nvs_sess_hit = 1;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (g_nvs_fail_commit && g_nvs_sess_hit) return ESP_FAIL;
    g_nvs_commits++;
    return ESP_OK;
}

/* ── Include handler under test ────────────────────────────────────────────── */
#include "af_settings_http.c"

/* ── Test helpers ──────────────────────────────────────────────────────────── */
#define T0 1793606400u   /* 2026-11-02 08:00:00 UTC, кратно 60, 120, 300 и 1200 с */

static unsigned g_si;

static void feed(uint32_t ts)
{
    const real_t *r = &REAL[g_si++ % REAL_N];
    af_sample_t s;
    memset(&s, 0, sizeof(s));
    s.dose_msv = r->dose;
    s.doserate_usvh = r->rate;
    s.pulses_2s = (uint16_t)r->p2s;
    s.cps = r->p2s / 2.0f;
    s.battery = (int8_t)r->bat;
    s.temp_c = (int8_t)r->temp;
    s.flags = 0;
    (void)af_core_ingest_g(&s_core, s_core.gen, ts, &s);
}

static void feed_range(uint32_t ts_from, uint32_t ts_to)
{
    for (uint32_t t = ts_from; t <= ts_to; t += 2) feed(t);
}

static void setup(void)
{
    if (s_ready) store_free();
    nvs_reset();
    g_clr_rc = 0;
    g_clr_calls = 0;
    g_clr_mask = 0;
    g_clr_flash_hook = NULL;
    g_ble_ext_calls = 0;
    g_si = 0;
    CHECK(af_store_init() == ESP_OK);
    feed_range(T0, T0 + 1800 + 58);
}

static void post(const char *body)
{
    httpd_req_t r;
    memset(&r, 0, sizeof(r));
    r.body = body;
    r.content_len = strlen(body);
    r.pos = 0;
    g_status[0] = 0;
    g_body[0] = 0;
    (void)af_settings_post_handler(&r);
}

static int is200(void)
{
    return g_status[0] == 0 || strncmp(g_status, "200", 3) == 0;
}

static uint32_t agg_count(void)
{
    af_store_status_t st;
    af_store_get_status(&st);
    return st.agg.count;
}

static uint32_t win_s(void)
{
    af_store_status_t st;
    af_store_get_status(&st);
    return st.window_s;
}

static void hook_cross_boundary(void)
{
    feed(T0 + 1860);
}

/* ── Tests ─────────────────────────────────────────────────────────────────── */

static void t_b01_no_old_period_after_change(void)
{
    setup();
    uint32_t n0 = agg_count();
    CHECK(n0 >= 25);
    g_clr_flash_hook = hook_cross_boundary;
    post("{\"window_s\":120,\"confirm_clear_agg\":true}");
    CHECK(is200());
    CHECK(strstr(g_body, "\"ok\":true") != NULL);
    CHECK(win_s() == 120);
    CHECK(agg_count() == 0);
    uint32_t v = 0;
    CHECK(nvs_has("win_s", &v) && v == 120);
    CHECK(g_clr_mask == AF_CLR_MASK_AGG);
}

static void t_win_nvs_fail_rolls_back_and_retry_writes(void)
{
    setup();
    g_nvs_fail_commit = "win_s";
    post("{\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(strncmp(g_status, "500", 3) == 0);
    CHECK(strstr(g_body, "nvs_failed") != NULL);
    CHECK(strstr(g_body, "\"step\":\"window\"") != NULL);
    CHECK(win_s() == 60);
    g_nvs_fail_commit = NULL;
    post("{\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(is200());
    CHECK(win_s() == 300);
    uint32_t v = 0;
    CHECK(nvs_has("win_s", &v) && v == 300);
}

static void t_flags_nvs_fail_is_error_ram_unchanged(void)
{
    setup();
    g_nvs_fail_set = "st_agg";
    post("{\"store\":{\"agg\":false}}");
    CHECK(!is200());
    CHECK(strstr(g_body, "\"ok\":true") == NULL);
    CHECK(strstr(g_body, "nvs_failed") != NULL);
    int r = -1, a = -1, e = -1;
    af_store_get_enabled(&r, &a, &e);
    CHECK(a == 1);
}

static void t_flags_fail_keeps_aggregates(void)
{
    setup();
    uint32_t n0 = agg_count();
    g_nvs_fail_set = "st_raw";
    post("{\"store\":{\"raw\":false},\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(!is200());
    CHECK(agg_count() == n0);
    CHECK(win_s() == 60);
    CHECK(g_clr_calls == 0);
}

static void t_clear_busy_keeps_state(void)
{
    setup();
    uint32_t n0 = agg_count();
    g_clr_rc = -1;
    post("{\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(strncmp(g_status, "503", 3) == 0);
    CHECK(strstr(g_body, "busy") != NULL);
    CHECK(win_s() == 60);
    CHECK(agg_count() == n0);
    uint32_t v = 0;
    CHECK(!nvs_has("win_s", &v));
}

static void t_clear_marker_fail_keeps_state(void)
{
    setup();
    uint32_t n0 = agg_count();
    g_clr_rc = -3;
    post("{\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(strncmp(g_status, "500", 3) == 0);
    CHECK(strstr(g_body, "clear_failed") != NULL);
    CHECK(win_s() == 60);
    CHECK(agg_count() == n0);
    uint32_t v = 0;
    CHECK(!nvs_has("win_s", &v));
}

static void t_clear_late_fail_window_old(void)
{
    setup();
    g_clr_rc = -2;
    post("{\"window_s\":300,\"confirm_clear_agg\":true}");
    CHECK(strncmp(g_status, "500", 3) == 0);
    CHECK(strstr(g_body, "clear_failed") != NULL);
    CHECK(win_s() == 60);
    CHECK(agg_count() == 0);
    uint32_t v = 0;
    CHECK(!nvs_has("win_s", &v));
}

static void t_full_success_1200_and_sigma(void)
{
    setup();
    post("{\"window_s\":1200,\"sigma_k\":2,\"confirm_clear_agg\":true}");
    CHECK(is200());
    CHECK(win_s() == 1200);
    CHECK(agg_count() == 0);
    uint32_t v = 0;
    CHECK(nvs_has("win_s", &v) && v == 1200);
    CHECK(nvs_has("sig_k", &v) && v == 2);
    feed_range(T0 + 1860, T0 + 2400 + 10);
    CHECK(agg_count() == 1);
    uint8_t rec[AF_REC_AGG_SIZE];
    uint32_t nx = 0, lost = 0;
    CHECK(af_store_read(1, 0, rec, 1, &nx, &lost) == 1);
    af_agg_rec_t a;
    CHECK(af_agg_decode(rec, &a) == 0);
    CHECK(a.period_s == 1200);
}

static void t_sigma_nvs_fail_names_step(void)
{
    setup();
    g_nvs_fail_set = "sig_k";
    post("{\"sigma_k\":3}");
    CHECK(strncmp(g_status, "500", 3) == 0);
    CHECK(strstr(g_body, "\"step\":\"afset\"") != NULL);
}

static void t_need_confirm_untouched(void)
{
    setup();
    uint32_t n0 = agg_count();
    post("{\"window_s\":300}");
    CHECK(strncmp(g_status, "409", 3) == 0);
    CHECK(win_s() == 60);
    CHECK(agg_count() == n0);
    CHECK(g_clr_calls == 0);
}

/* ── main ──────────────────────────────────────────────────────────────────── */
int main(void)
{
    RUN(t_b01_no_old_period_after_change);
    RUN(t_win_nvs_fail_rolls_back_and_retry_writes);
    RUN(t_flags_nvs_fail_is_error_ram_unchanged);
    RUN(t_flags_fail_keeps_aggregates);
    RUN(t_clear_busy_keeps_state);
    RUN(t_clear_marker_fail_keeps_state);
    RUN(t_clear_late_fail_window_old);
    RUN(t_full_success_1200_and_sigma);
    RUN(t_sigma_nvs_fail_names_step);
    RUN(t_need_confirm_untouched);
    if (s_ready) store_free();
    TESTS_DONE();
}
