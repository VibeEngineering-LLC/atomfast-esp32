#include "af_push_drv.h"
#include "af_store.h"
#include "af_net.h"
#include "ota_busy.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define PUSH_TASK_STACK 5120
#define PUSH_NVS_NS "afpush"
static const char *TAG = "af_push";

/* журнал событий: настоящий af_log_event (af_log.c) переопределяет эту слабую заглушку при линковке; сигнатура та же (variadic) */
__attribute__((weak)) void af_log_event(int level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }

static SemaphoreHandle_t s_mtx;
static af_push_cfg_t s_cfg;
static af_push_ring_t s_ring[3];
static uint8_t s_auth_stop;
static af_push_test_t s_test;
static uint8_t s_test_req;
static int s_running;
static volatile int s_sending;
static bool s_dirty;
static uint32_t s_last_save_ms;
static TaskHandle_t s_task;
static volatile uint32_t s_stack_hwm;
static uint8_t s_logged_err[3];
static uint32_t s_gen;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t now_ts(void) {
    time_t t = time(NULL);
    return (t < 1700000000) ? 0 : (uint32_t)t;
}

static void cfg_load(void) {
    nvs_handle_t h;
    if (nvs_open(PUSH_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        af_push_cfg_default(&s_cfg);
        for (int i = 0; i < 3; i++) af_push_ring_init(&s_ring[i], s_cfg.batch_rows);
        return;
    }

    uint8_t buf[160];
    size_t len = sizeof(buf);
    if (nvs_get_blob(h, "push_cfg", buf, &len) == ESP_OK && len == AF_PUSH_CFG_BLOB) {
        af_push_cfg_decode(&s_cfg, buf, len);
    } else {
        af_push_cfg_default(&s_cfg);
    }

    for (int i = 0; i < 3; i++) {
        af_push_ring_init(&s_ring[i], s_cfg.batch_rows);
    }

    len = sizeof(buf);
    if (nvs_get_blob(h, "push_cur", buf, &len) == ESP_OK && len == AF_PUSH_CUR_BLOB) {
        af_push_cur_t cur[3];
        if (af_push_curs_decode(cur, buf, len)) {
            for (int i = 0; i < 3; i++) {
                s_ring[i].cur = cur[i];
            }
        }
    }
    nvs_close(h);
}

static int cfg_save(const af_push_cfg_t *c) {
    uint8_t buf[160];
    size_t len = af_push_cfg_encode(c, buf, sizeof(buf));
    if (len == 0) return -1;

    nvs_handle_t h;
    if (nvs_open(PUSH_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_blob(h, "push_cfg", buf, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? 0 : -1;
}

static int curs_save_locked(void) {
    uint8_t buf[64];
    af_push_cur_t cur[3];
    for (int i = 0; i < 3; i++) cur[i] = s_ring[i].cur;
    size_t len = af_push_curs_encode(cur, buf, sizeof(buf));
    if (len == 0) return -1;

    nvs_handle_t h;
    if (nvs_open(PUSH_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_blob(h, "push_cur", buf, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        s_dirty = false;
        s_last_save_ms = now_ms();
        return 0;
    }
    return -1;
}

static int src_info(int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch) {
    af_hq_params_t hp;
    af_hq_init(&hp);
    hp.kind = kind;
    af_hq_plan_t pl;
    int rc = af_store_plan(&hp, &pl, head, epoch);
    if (rc == 0) *oldest = pl.start;
    return rc;
}

static uint32_t src_read(void *ctx, int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost) {
    (void)ctx;
    return af_store_read(kind, since, out, max, next, lost);
}

typedef struct {
    char buf[160];
    size_t n;
} resp_t;

static esp_err_t http_ev(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        resp_t *rs = (resp_t *)evt->user_data;
        if (rs && evt->data && evt->data_len > 0) {
            size_t space = sizeof(rs->buf) - 1 - rs->n;
            size_t copy = (evt->data_len < space) ? evt->data_len : space;
            if (copy > 0) {
                memcpy(rs->buf + rs->n, evt->data, copy);
                rs->n += copy;
                rs->buf[rs->n] = '\0';
            }
        }
    }
    return ESP_OK;
}

static esp_err_t do_post(const af_push_cfg_t *c, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int test, int *http, resp_t *rs) {
    rs->n = 0;
    rs->buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .host = c->host,
        .port = c->port,
        .path = af_push_path(kind),
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .method = HTTP_METHOD_POST,
        .timeout_ms = (int)c->timeout_s * 1000,
        .buffer_size = 512,
        .buffer_size_tx = 768,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
        .event_handler = http_ev,
        .user_data = rs
    };

    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (cl == NULL) return ESP_ERR_NO_MEM;

    char auth[80];
    snprintf(auth, sizeof(auth), "Bearer %s", c->token);
    esp_http_client_set_header(cl, "Content-Type", "text/csv");
    esp_http_client_set_header(cl, "Authorization", auth);

    af_hq_hdrs_t hh;
    af_hq_hdrs_fill(&hh, kind, head, epoch, first_seq, lost, mac);
    const char *hn[6];
    const char *hv[6];
    int hc = af_push_hdr_list(&hh, test, hn, hv);
    for (int i = 0; i < hc; i++) esp_http_client_set_header(cl, hn[i], hv[i]);

    esp_http_client_set_post_field(cl, body, (int)len);
    esp_err_t err = esp_http_client_perform(cl);
    *http = (err == ESP_OK) ? esp_http_client_get_status_code(cl) : 0;
    esp_http_client_cleanup(cl);
    rs->buf[rs->n] = '\0';
    return err;
}

static int cb_info(void *ctx, int kind, uint32_t *o, uint32_t *h, uint32_t *e) {
    (void)ctx;
    return src_info(kind, o, h, e);
}

static int cb_post(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap) {
    const af_push_cfg_t *c = (const af_push_cfg_t *)ctx;
    resp_t rs;
    s_sending = 1;
    esp_err_t err = do_post(c, kind, mac, head, epoch, first_seq, lost, body, len, 0, http, &rs);
    s_sending = 0;

    if (resp && resp_cap > 0) {
        size_t copy = (rs.n < resp_cap - 1) ? rs.n : resp_cap - 1;
        memcpy(resp, rs.buf, copy);
        resp[copy] = '\0';
    }
    return (err == ESP_OK) ? 0 : 1;
}

static uint32_t cb_now(void *ctx) {
    (void)ctx;
    return now_ts();
}

static const af_push_ops_t s_ops = {
    .info = cb_info,
    .read = src_read,
    .post = cb_post,
    .now_ts = cb_now
};

static int send_ring(int kind, const af_push_cfg_t *c, char *body, size_t body_cap, uint32_t *pending_left) {
    uint32_t gen0;
    af_push_ring_t r;
    uint8_t auth;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    gen0 = s_gen;
    r = s_ring[kind];
    auth = s_auth_stop;
    xSemaphoreGive(s_mtx);

    char mac[24] = {0};
    (void)ble_gw_target_mac(mac, sizeof(mac));

    int moved = 0;
    int res = af_push_send_ring(&s_ops, (void *)c, kind, c, mac, &r, &auth, body, body_cap, pending_left, &moved);

    uint8_t err_code = 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (gen0 == s_gen) {
        s_ring[kind] = r;
        if (auth) s_auth_stop = 1;
        if (moved) s_dirty = true;
        err_code = r.err;
    } else {
        *pending_left = 0;
        res = AF_PUSH_S_WAIT;
    }
    xSemaphoreGive(s_mtx);

    if (gen0 == s_gen && err_code != s_logged_err[kind]) {
        int level = (err_code == 0) ? 1 : 2;
        const char *msg = "ok";
        switch (err_code) {
            case AF_PUSH_ERR_AUTH: msg = "auth failed"; break;
            case AF_PUSH_ERR_BAD: msg = "batch rejected"; break;
            case AF_PUSH_ERR_NET: msg = "network error"; break;
            case AF_PUSH_ERR_SRV: msg = "server error"; break;
            case AF_PUSH_ERR_BIG: msg = "batch too big"; break;
            case AF_PUSH_ERR_PROTO: msg = "bad response"; break;
            case AF_PUSH_ERR_HTTP4: msg = "http error"; break;
            case AF_PUSH_ERR_NODEV:
            case AF_PUSH_ERR_NOSTORE:
            case AF_PUSH_ERR_OOM: msg = "internal error"; break;
            default: break;
        }
        af_log_event(level, "push", "%s", msg);
        s_logged_err[kind] = err_code;
    }

    return res;
}

static void run_test(void) {
    af_push_cfg_t c;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    c = s_cfg;
    xSemaphoreGive(s_mtx);

    char mac[24] = {0};
    (void)ble_gw_target_mac(mac, sizeof(mac));

    char body[200];
    int n = af_push_test_body(body, sizeof(body));
    if (n < 0 || mac[0] == 0) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_test.state = 2;
        s_test.err = (mac[0] == 0) ? AF_PUSH_ERR_NODEV : AF_PUSH_ERR_OOM;
        s_test.ts = now_ts();
        s_test_req = 0;
        xSemaphoreGive(s_mtx);
        return;
    }

    resp_t rs;
    int http = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = do_post(&c, AF_HQ_KIND_ENV, mac, 0, 0, 0, 0, body, (size_t)n, 1, &http, &rs);
    int64_t ms = (esp_timer_get_time() - t0) / 1000;

    int accepted = -1;
    int legacy = 0;
    int err_code = AF_PUSH_ERR_NET;

    if (err == ESP_OK) {
        if (http == 200) {
            uint32_t acc = 0;
            int64_t last_seq = 0;
            if (af_push_parse_resp(rs.buf, rs.n, &acc, &last_seq) == 0) {
                accepted = (int)acc;
                legacy = af_push_resp_test(rs.buf, rs.n) ? 0 : 1;
                err_code = AF_PUSH_ERR_NONE;
            } else {
                err_code = AF_PUSH_ERR_PROTO;
            }
        } else if (http == 401) {
            err_code = AF_PUSH_ERR_AUTH;
        } else if (http == 400) {
            err_code = AF_PUSH_ERR_BAD;
        } else if (http == 413) {
            err_code = AF_PUSH_ERR_BIG;
        } else if ((http >= 500 && http < 600) || http == 408 || http == 429) {
            err_code = AF_PUSH_ERR_SRV;
        } else if (http >= 400 && http < 500) {
            err_code = AF_PUSH_ERR_HTTP4;
        } else {
            err_code = AF_PUSH_ERR_PROTO;
        }
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_test.state = 2;
    s_test.http = http;
    s_test.ms = (uint32_t)ms;
    s_test.accepted = accepted;
    s_test.err = err_code;
    s_test.legacy = (uint8_t)legacy;
    s_test.ts = now_ts();
    s_test_req = 0;
    xSemaphoreGive(s_mtx);
}

static void push_task(void *arg) {
    (void)arg;
    size_t cap = AF_PUSH_BODY_MAX;
    char *body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        cap = 4096;
        body = malloc(cap);
    }

    if (!body) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (int i = 0; i < 3; i++) s_ring[i].err = AF_PUSH_ERR_OOM;
        s_running = 0;
        s_task = NULL;
        xSemaphoreGive(s_mtx);
        vTaskDelete(NULL);
    }

    for (;;) {
        af_push_cfg_t c;
        uint8_t treq;
        uint8_t auth;

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        c = s_cfg;
        treq = s_test_req;
        auth = s_auth_stop;

        if (!c.enabled && !treq) {
            if (s_dirty) curs_save_locked();
            s_running = 0;
            s_task = NULL;
            xSemaphoreGive(s_mtx);
            free(body);
            vTaskDelete(NULL);
        }
        xSemaphoreGive(s_mtx);

        s_stack_hwm = uxTaskGetStackHighWaterMark(NULL);

        if (treq) {
            run_test();
            continue;
        }

        if (!wifi_is_connected() || ota_busy_is_busy()) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
            continue;
        }

        if (auth) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }

        bool any_more = false;
        bool any_fail = false;
        bool any_stop = false;

        for (int kind = 0; kind < 3; kind++) {
            if (!(c.rings & (1 << kind))) continue;

            uint32_t left = 0;
            int rc = send_ring(kind, &c, body, cap, &left);

            if (left > 0) any_more = true;
            if (rc == AF_PUSH_S_WAIT) any_fail = true;
            if (rc == AF_PUSH_S_STOP) any_stop = true;

            xSemaphoreTake(s_mtx, portMAX_DELAY);
            bool stop_now = s_auth_stop;
            xSemaphoreGive(s_mtx);
            if (stop_now) break;

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        (void)any_stop;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (af_push_curs_due(s_dirty, now_ms(), s_last_save_ms)) {
            curs_save_locked();
        }
        xSemaphoreGive(s_mtx);

        uint8_t rnd = esp_random() & 0xFF;
        uint8_t max_fails = 0;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (int i = 0; i < 3; i++) {
            if (c.rings & (1 << i)) {
                if (s_ring[i].fails > max_fails) max_fails = s_ring[i].fails;
            }
        }
        xSemaphoreGive(s_mtx);

        uint32_t wait;
        if (max_fails > 0) {
            wait = af_push_backoff_ms(max_fails, rnd);
        } else if (any_fail) {
            wait = 3000;
        } else {
            wait = af_push_pause_ms(any_more ? 1 : 0, c.interval_s, rnd);
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

static void spawn_locked(void) {
    if (!s_running && (s_cfg.enabled || s_test_req)) {
        if (xTaskCreate(push_task, "af_push", PUSH_TASK_STACK, NULL, 2, &s_task) == pdPASS) {
            s_running = 1;
        } else {
            ESP_LOGE(TAG, "task create failed");
            s_test_req = 0;
            s_test.state = 2;
            s_test.err = AF_PUSH_ERR_OOM;
        }
    }
}

static void wake(void) {
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

void af_push_start(void) {
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }
    cfg_load();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    spawn_locked();
    xSemaphoreGive(s_mtx);
}

void af_push_get_cfg(af_push_cfg_t *out) {
    if (!out || !s_mtx) {
        if (out) af_push_cfg_default(out);
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mtx);
}

int af_push_apply(const af_push_cmd_t *cmd) {
    if (!cmd || !s_mtx) return -1;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg = cmd->cfg;
    s_gen++;
    for (int i = 0; i < 3; i++) {
        af_push_ring_clear_err(&s_ring[i], s_cfg.batch_rows);
    }
    if (cmd->dest_changed) {
        for (int i = 0; i < 3; i++) {
            af_push_ring_reset_cursor(&s_ring[i], s_cfg.batch_rows);
        }
        s_dirty = true;
    }
    s_auth_stop = 0;
    spawn_locked();
    wake();
    xSemaphoreGive(s_mtx);

    int rc = cfg_save(&cmd->cfg);
    if (rc != 0) return -2;

    if (cmd->dest_changed) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        curs_save_locked();
        xSemaphoreGive(s_mtx);
    }
    return 0;
}

int af_push_get_json(char *buf, size_t cap) {
    if (!s_mtx) return -1;

    af_push_view_t v;
    af_push_cfg_t cfg;
    memset(&v, 0, sizeof(v));

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    v.sending = s_sending;
    v.auth_stop = s_auth_stop;
    v.stack_hwm = s_stack_hwm;
    v.now_ts = now_ts();
    for (int i = 0; i < 3; i++) {
        v.ring[i] = s_ring[i];
    }
    v.test = s_test;
    cfg = s_cfg;
    xSemaphoreGive(s_mtx);

    for (int kind = 0; kind < 3; kind++) {
        uint32_t oldest, head, epoch;
        if (src_info(kind, &oldest, &head, &epoch) == 0) {
            v.info[kind].present = 1;
            v.info[kind].oldest = oldest;
            v.info[kind].head = head;
            v.info[kind].epoch = epoch;
        }
    }

    return af_push_json(buf, cap, &cfg, &v);
}

int af_push_test_start(void) {
    if (!s_mtx) return -3;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_test.state == 1) {
        xSemaphoreGive(s_mtx);
        return -1;
    }
    if (s_cfg.host[0] == 0 || s_cfg.token[0] == 0) {
        xSemaphoreGive(s_mtx);
        return -2;
    }
    memset(&s_test, 0, sizeof(s_test));
    s_test.state = 1;
    s_test.accepted = -1;
    s_test_req = 1;
    spawn_locked();
    wake();
    xSemaphoreGive(s_mtx);
    return 0;
}

int af_push_reset(void) {
    if (!s_mtx) return -1;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_gen++;
    for (int i = 0; i < 3; i++) {
        af_push_ring_reset_cursor(&s_ring[i], s_cfg.batch_rows);
    }
    s_auth_stop = 0;
    s_dirty = true;
    curs_save_locked();
    wake();
    xSemaphoreGive(s_mtx);
    return 0;
}
