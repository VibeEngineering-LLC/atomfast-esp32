#include "af_mqtt_drv.h"
#include "af_log_api.h"
#include "af_mqtt_cfg.h"
#include "af_env_drv.h"
#include "af_net.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

static const char *TAG = "af_mqtt";

static af_ha_dev_t s_dev = {
    .node_id = "atomfast_gw",
    .name = "AtomFast шлюз",
    .model = "AtomFast Plus2",
    .sw = NULL,
    .base = "atomfast"
};

static SemaphoreHandle_t s_mtx;
static af_mqtt_cfg_t s_cfg;
static af_mqtt_stat_t s_stat;
static uint32_t s_gen;

static volatile int s_conn;
static volatile int s_need_online;
static volatile int s_need_disc;

static char s_ha_topic[64];
static char s_prefix[AF_MQTT_PREFIX_MAX]; /* префикс discovery активного клиента */
static esp_mqtt_client_handle_t s_cli;
static char s_buf[768];
static int s_running; /* задача af_mqtt существует (под мьютексом) */

static void cfg_load(af_mqtt_cfg_t *c) {
    af_mqtt_cfg_default(c);
    nvs_handle_t h;
    if (nvs_open("mqtt", NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    uint8_t en;
    if (nvs_get_u8(h, "en", &en) == ESP_OK) {
        af_mqtt_cfg_set_enabled(c, en);
    }

    uint16_t port;
    if (nvs_get_u16(h, "port", &port) == ESP_OK) {
        af_mqtt_cfg_set_num(c, "port", (double)port);
    }

    uint16_t per;
    if (nvs_get_u16(h, "per", &per) == ESP_OK) {
        af_mqtt_cfg_set_num(c, "period_s", (double)per);
    }

    char buf[64];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, "host", buf, &len) == ESP_OK) {
        af_mqtt_cfg_set_str(c, "host", buf);
    }

    len = sizeof(buf);
    if (nvs_get_str(h, "user", buf, &len) == ESP_OK) {
        af_mqtt_cfg_set_str(c, "user", buf);
    }

    len = sizeof(buf);
    if (nvs_get_str(h, "pass", buf, &len) == ESP_OK) {
        af_mqtt_cfg_set_str(c, "pass", buf);
    }

    len = sizeof(buf);
    if (nvs_get_str(h, "pfx", buf, &len) == ESP_OK) {
        af_mqtt_cfg_set_str(c, "prefix", buf);
    }

    nvs_close(h);

    if (!af_mqtt_cfg_valid(c)) {
        af_mqtt_cfg_default(c);
    }
}

static int cfg_save(const af_mqtt_cfg_t *c) {
    nvs_handle_t h;
    if (nvs_open("mqtt", NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }

    esp_err_t rc = ESP_OK;
    rc = rc == ESP_OK ? nvs_set_u8(h, "en", c->enabled) : rc;
    rc = rc == ESP_OK ? nvs_set_u16(h, "port", c->port) : rc;
    rc = rc == ESP_OK ? nvs_set_u16(h, "per", c->period_s) : rc;
    rc = rc == ESP_OK ? nvs_set_str(h, "host", c->host) : rc;
    rc = rc == ESP_OK ? nvs_set_str(h, "user", c->user) : rc;
    rc = rc == ESP_OK ? nvs_set_str(h, "pass", c->pass) : rc;
    rc = rc == ESP_OK ? nvs_set_str(h, "pfx", c->prefix) : rc;

    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);

    return (rc == ESP_OK) ? 0 : -1;
}

static void stat_set_err(uint8_t e) {
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_stat.last_err = e;
    xSemaphoreGive(s_mtx);
}

static void stat_set_conn(int v) {
    if (s_mtx == NULL) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_stat.connected = v ? 1 : 0;
    xSemaphoreGive(s_mtx);
}

static int pub(const char *topic, const char *data, int qos, int retain) {
    if (s_cli == NULL) {
        return -1;
    }

    /* только свои топики; retain - лишь discovery-конфиги и статус (LWT) */
    if (!af_mqtt_pub_allowed(s_prefix, topic, retain)) { stat_set_err(AF_MQTT_E_PUBLISH); return -1; }

    int id = esp_mqtt_client_publish(s_cli, topic, data, 0, qos, retain);
    if (id >= 0) {
        if (s_mtx != NULL) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stat.published++;
            xSemaphoreGive(s_mtx);
        }
        return 0;
    } else {
        if (s_mtx != NULL) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stat.failed++;
            s_stat.last_err = AF_MQTT_E_PUBLISH;
            xSemaphoreGive(s_mtx);
        }
        return -1;
    }
}

static void mqtt_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;

    switch (id) {
        case MQTT_EVENT_CONNECTED:
            __atomic_store_n(&s_conn, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&s_need_online, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&s_need_disc, 1, __ATOMIC_RELAXED);
            stat_set_conn(1);
            stat_set_err(AF_MQTT_E_NONE);
            esp_mqtt_client_subscribe(ev->client, s_ha_topic, 0);
            ESP_LOGI(TAG, "Connected, subscribed to %s", s_ha_topic);
            af_log_event(1, "mqtt", "connected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            af_log_event(2, "mqtt", "disconnected");
            __atomic_store_n(&s_conn, 0, __ATOMIC_RELAXED);
            stat_set_conn(0);
            break;

        case MQTT_EVENT_DATA:
            if (ev->current_data_offset == 0 && ev->data_len == ev->total_data_len) {
                if (af_mqtt_match(ev->topic, ev->topic_len, s_ha_topic) &&
                    af_mqtt_match(ev->data, ev->data_len, AF_HA_ONLINE)) {
                    __atomic_store_n(&s_need_disc, 1, __ATOMIC_RELAXED);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            af_log_event(2, "mqtt", "error type=%d code=%d", ev->error_handle ? (int)ev->error_handle->error_type : -1, ev->error_handle ? (int)ev->error_handle->connect_return_code : -1);
            if (ev->error_handle != NULL) {
                if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    stat_set_err(AF_MQTT_E_AUTH);
                } else {
                    stat_set_err(AF_MQTT_E_TRANSPORT);
                }
            } else {
                stat_set_err(AF_MQTT_E_TRANSPORT);
            }
            break;

        default:
            break;
    }
}

static void client_destroy(void) {
    if (s_cli == NULL) {
        return;
    }

    if (__atomic_load_n(&s_conn, __ATOMIC_RELAXED)) {
        char topic[128];
        size_t len = af_ha_avail_topic(&s_dev, topic, sizeof(topic));
        if (len > 0) {
            pub(topic, AF_HA_OFFLINE, 1, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    esp_mqtt_client_stop(s_cli);
    esp_mqtt_client_destroy(s_cli);
    s_cli = NULL;
    __atomic_store_n(&s_conn, 0, __ATOMIC_RELAXED);
    stat_set_conn(0);
}

static int client_create(const af_mqtt_cfg_t *c) {
    char topic[128];
    size_t len = af_ha_avail_topic(&s_dev, topic, sizeof(topic));
    if (len == 0) {
        stat_set_err(AF_MQTT_E_INIT);
        return -1;
    }

    len = af_mqtt_ha_status_topic(c->prefix, s_ha_topic, sizeof(s_ha_topic));
    if (len == 0) {
        stat_set_err(AF_MQTT_E_INIT);
        return -1;
    }

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.hostname = c->host;
    cfg.broker.address.port = c->port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = "atomfast_gw";
    if (c->user[0]) {
        cfg.credentials.username = c->user;
    }
    if (c->pass[0]) {
        cfg.credentials.authentication.password = c->pass;
    }
    cfg.session.keepalive = 60;
    cfg.session.last_will.topic = topic;
    cfg.session.last_will.msg = AF_HA_OFFLINE;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.timeout_ms = 10000;
    cfg.network.reconnect_timeout_ms = 15000;
    cfg.buffer.size = 512;
    cfg.buffer.out_size = 512;
    cfg.task.stack_size = 4096; /* замер e14a: использовано 2284 из 5120 */
    cfg.task.priority = 3;

    snprintf(s_prefix, sizeof(s_prefix), "%s", c->prefix);
    if (!af_mqtt_sub_allowed(s_prefix, s_ha_topic)) {
        stat_set_err(AF_MQTT_E_INIT);
        return -1;
    }
    s_cli = esp_mqtt_client_init(&cfg);
    if (s_cli == NULL) {
        stat_set_err(AF_MQTT_E_INIT);
        return -1;
    }

    esp_mqtt_client_register_event(s_cli, ESP_EVENT_ANY_ID, mqtt_evt, NULL);
    if (esp_mqtt_client_start(s_cli) != ESP_OK) {
        esp_mqtt_client_destroy(s_cli);
        s_cli = NULL;
        stat_set_err(AF_MQTT_E_INIT);
        return -1;
    }

    return 0;
}

static int publish_discovery(const af_mqtt_cfg_t *c, uint8_t air_mask) {
    for (int i = 0; i < AF_HA_SENSOR_COUNT; i++) {
        const af_ha_sensor_t *s = &AF_HA_SENSORS[i];

        if (strcmp(s->key, "air_temp") == 0 && !(air_mask & 1)) {
            continue;
        }
        if (strcmp(s->key, "air_press") == 0 && !(air_mask & 2)) {
            continue;
        }

        char topic[128];
        size_t len = af_mqtt_config_topic_p(c->prefix, &s_dev, s, topic, sizeof(topic));
        if (len == 0) {
            return -1;
        }

        len = af_ha_config_json(&s_dev, s, s_buf, sizeof(s_buf));
        if (len == 0) {
            return -1;
        }

        if (pub(topic, s_buf, 1, 1) != 0) {
            return -1;
        }
    }

    char topic[128];
    size_t len = af_mqtt_ble_config_topic(c->prefix, &s_dev, topic, sizeof(topic));
    if (len == 0) {
        return -1;
    }

    len = af_mqtt_ble_config_json(&s_dev, s_buf, sizeof(s_buf));
    if (len == 0) {
        return -1;
    }

    if (pub(topic, s_buf, 1, 1) != 0) {
        return -1;
    }

    return 0;
}

static void publish_state(uint8_t *air_mask_out) {
    ble_gw_status_t b;
    ble_gw_get_status(&b);

    float t = NAN;
    float p = NAN;
    uint8_t caps = 0;
    (void)af_env_drv_get_air(&t, &p, &caps);

    uint8_t am = af_mqtt_air_caps(caps, t, p);
    *air_mask_out = am;

    char topic[128];
    size_t len = af_mqtt_ble_topic(&s_dev, topic, sizeof(topic));
    if (len > 0) {
        pub(topic, b.connected ? "ON" : "OFF", 0, 0);
    }

    if (!(b.connected && b.has_rec)) {
        return;
    }

    af_ha_state_t st;
    st.dose_rate = b.doserate_usvh;
    st.cps = b.cps;
    st.dose = b.dose_msv * 1000.0f;
    st.pulses_2s = b.pulses_2s;
    st.battery = b.battery;
    st.temp_af = b.temp_c;
    st.air_temp = t;
    st.air_press = p;
    st.ts = (uint32_t)time(NULL);

    len = af_ha_state_json(&st, am, s_buf, sizeof(s_buf));
    if (len == 0) {
        return;
    }

    len = af_ha_state_topic(&s_dev, topic, sizeof(topic));
    if (len > 0) {
        pub(topic, s_buf, 0, 0);
    }
}

static void mqtt_task(void *arg) {
    (void)arg;

    uint32_t gen_seen = 0;
    uint32_t last_pub_s = 0;
    int have_pub = 0;
    uint32_t next_disc_s = 0;
    uint32_t next_create_s = 0;
    uint8_t disc_mask = 0;
    uint8_t cur_mask = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);

        af_mqtt_cfg_t c;
        uint32_t gen;
        if (s_mtx != NULL) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            c = s_cfg;
            gen = s_gen;
            xSemaphoreGive(s_mtx);
        } else {
            continue;
        }

        if (gen != gen_seen) {
            client_destroy();
            gen_seen = gen;
            have_pub = 0;
            disc_mask = 0;
            next_disc_s = 0;
            next_create_s = 0;
        }

        if (s_mtx != NULL) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stat.enabled = c.enabled;
            xSemaphoreGive(s_mtx);
        }

        if (af_mqtt_task_action(c.enabled, 1, 1) == AF_MQTT_ACT_EXIT) {
            client_destroy();
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (af_mqtt_task_action(s_cfg.enabled, 1, 1) == AF_MQTT_ACT_EXIT) {
                s_running = 0;
                xSemaphoreGive(s_mtx);
                vTaskDelete(NULL); /* выключено: ни задачи, ни клиента, ни буферов */
            }
            xSemaphoreGive(s_mtx);
            continue;
        }
        if (c.host[0] == 0) {
            stat_set_err(AF_MQTT_E_NOHOST);
            continue;
        }

        if (s_cli == NULL) {
            if (!wifi_is_connected() || now_s < next_create_s) {
                continue;
            }
            if (client_create(&c) != 0) {
                next_create_s = now_s + 30;
            }
            continue;
        }

        if (!__atomic_load_n(&s_conn, __ATOMIC_RELAXED)) {
            continue;
        }

        if (__atomic_load_n(&s_need_online, __ATOMIC_RELAXED)) {
            char topic[128];
            size_t len = af_ha_avail_topic(&s_dev, topic, sizeof(topic));
            if (len > 0 && pub(topic, AF_HA_ONLINE, 1, 1) == 0) {
                __atomic_store_n(&s_need_online, 0, __ATOMIC_RELAXED);
            }
        }

        if ((cur_mask & ~disc_mask) != 0) {
            __atomic_store_n(&s_need_disc, 1, __ATOMIC_RELAXED);
        }

        if (__atomic_load_n(&s_need_disc, __ATOMIC_RELAXED) && now_s >= next_disc_s) {
            if (publish_discovery(&c, cur_mask) == 0) {
                disc_mask = cur_mask;
                __atomic_store_n(&s_need_disc, 0, __ATOMIC_RELAXED);
            } else {
                next_disc_s = now_s + 5;
            }
        }

        if (af_mqtt_due(now_s, last_pub_s, c.period_s, have_pub)) {
            publish_state(&cur_mask);
            last_pub_s = now_s;
            have_pub = 1;
        }
    }
}

#define MQ_TASK_STACK 5120 /* замер e14a: использовано 4088 из 7168; запас >=1 КБ */

/* Создать задачу, если включено и ещё не запущена. Вызывать под мьютексом. */
static void task_spawn_locked(void) {
    if (af_mqtt_task_action(s_cfg.enabled, s_running, 0) != AF_MQTT_ACT_SPAWN) return;
    if (xTaskCreate(mqtt_task, "af_mqtt", MQ_TASK_STACK, NULL, 3, NULL) == pdPASS) {
        s_running = 1;
    } else {
        s_stat.last_err = AF_MQTT_E_INIT;
        ESP_LOGE(TAG, "Failed to create task");
    }
}
void af_mqtt_drv_start(void) {
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    s_dev.sw = (app_desc != NULL) ? app_desc->version : "?";

    cfg_load(&s_cfg);

    if (s_mtx != NULL) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_stat.enabled = s_cfg.enabled;
        xSemaphoreGive(s_mtx);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    task_spawn_locked();
    xSemaphoreGive(s_mtx);
}
int af_mqtt_drv_set_cfg(const af_mqtt_cfg_t *c) {
    if (s_mtx == NULL) {
        return -1;
    }
    if (!af_mqtt_cfg_valid(c)) {
        return -1;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg = *c;
    s_gen++;
    task_spawn_locked();
    xSemaphoreGive(s_mtx);

    int rc = cfg_save(c);
    return (rc == 0) ? 0 : -2;
}

void af_mqtt_drv_get_cfg(af_mqtt_cfg_t *c) {
    if (c == NULL) {
        return;
    }
    if (s_mtx == NULL) {
        af_mqtt_cfg_default(c);
        return;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *c = s_cfg;
    xSemaphoreGive(s_mtx);
}

void af_mqtt_drv_get_stat(af_mqtt_stat_t *s) {
    if (s == NULL) {
        return;
    }
    if (s_mtx == NULL) {
        memset(s, 0, sizeof(af_mqtt_stat_t));
        return;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *s = s_stat;
    xSemaphoreGive(s_mtx);
}
