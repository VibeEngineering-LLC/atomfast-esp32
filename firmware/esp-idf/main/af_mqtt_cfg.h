#ifndef AF_MQTT_CFG_H
#define AF_MQTT_CFG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_sensor.h"
#include "af_mqtt_ha.h"

#define AF_MQTT_HOST_MAX 64
#define AF_MQTT_USER_MAX 32
#define AF_MQTT_PASS_MAX 64
#define AF_MQTT_PREFIX_MAX 33
#define AF_MQTT_PORT_DEF 1883
#define AF_MQTT_PERIOD_MIN 10
#define AF_MQTT_PERIOD_MAX 3600
#define AF_MQTT_PERIOD_DEF 60
#define AF_MQTT_PREFIX_DEF "homeassistant"

typedef struct {
    uint8_t enabled;
    uint16_t port;
    uint16_t period_s;
    char host[AF_MQTT_HOST_MAX];
    char user[AF_MQTT_USER_MAX];
    char pass[AF_MQTT_PASS_MAX];
    char prefix[AF_MQTT_PREFIX_MAX];
} af_mqtt_cfg_t;

typedef enum {
    AF_MQTT_E_NONE = 0,
    AF_MQTT_E_NOHOST,
    AF_MQTT_E_TRANSPORT,
    AF_MQTT_E_AUTH,
    AF_MQTT_E_PUBLISH,
    AF_MQTT_E_INIT,
    AF_MQTT_E_COUNT
} af_mqtt_err_t;

static const char *const AF_MQTT_ERR_NAMES[AF_MQTT_E_COUNT] = {
    "", "no_host", "transport", "auth", "publish", "init"
};

typedef struct {
    uint8_t enabled;
    uint8_t connected;
    uint8_t last_err;
    uint32_t published;
    uint32_t failed;
} af_mqtt_stat_t;

static inline void af_mqtt_cfg_default(af_mqtt_cfg_t *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->enabled = 0;
    c->port = AF_MQTT_PORT_DEF;
    c->period_s = AF_MQTT_PERIOD_DEF;
    strcpy(c->prefix, AF_MQTT_PREFIX_DEF);
}

static inline int af_mqtt_cfg_valid(const af_mqtt_cfg_t *c) {
    if (!c) return 0;
    if (c->enabled > 1) return 0;
    if (c->port < 1) return 0;
    if (c->period_s < AF_MQTT_PERIOD_MIN || c->period_s > AF_MQTT_PERIOD_MAX) return 0;

    if (memchr(c->prefix, 0, AF_MQTT_PREFIX_MAX) == NULL) return 0;
    if (af_ha_valid_id(c->prefix, 1) != 1) return 0;

    if (memchr(c->host, 0, AF_MQTT_HOST_MAX) == NULL) return 0;
    size_t hlen = strlen(c->host);
    if (hlen > 0) {
        char first = c->host[0];
        if (first == '.' || first == '-') return 0;
        for (size_t i = 0; i < hlen; i++) {
            char ch = c->host[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) return 0;
        }
    }
    if (c->enabled == 1 && hlen == 0) return 0;

    if (memchr(c->user, 0, AF_MQTT_USER_MAX) == NULL) return 0;
    for (size_t i = 0; i < strlen(c->user); i++) {
        unsigned char ch = (unsigned char)c->user[i];
        if (ch < 0x20 || ch > 0x7E) return 0;
    }

    if (memchr(c->pass, 0, AF_MQTT_PASS_MAX) == NULL) return 0;
    for (size_t i = 0; i < strlen(c->pass); i++) {
        unsigned char ch = (unsigned char)c->pass[i];
        if (ch < 0x20 || ch > 0x7E) return 0;
    }

    return 1;
}

static inline int af_mqtt_cfg_set_enabled(af_mqtt_cfg_t *c, int v) {
    if (!c) return -1;
    if (v != 0 && v != 1) return -1;
    c->enabled = (uint8_t)v;
    return 0;
}

static inline int af_mqtt_cfg_set_num(af_mqtt_cfg_t *c, const char *key, double v) {
    if (!c || !key) return -1;

    if (strcmp(key, "port") == 0) {
        if (!isfinite(v) || v != floor(v)) return -1;
        if (v < 1 || v > 65535) return -1;
        c->port = (uint16_t)v;
        return 0;
    }
    if (strcmp(key, "period_s") == 0) {
        if (!isfinite(v) || v != floor(v)) return -1;
        if (v < AF_MQTT_PERIOD_MIN || v > AF_MQTT_PERIOD_MAX) return -1;
        c->period_s = (uint16_t)v;
        return 0;
    }
    return -2;
}

static inline int af_mqtt_cfg_set_str(af_mqtt_cfg_t *c, const char *key, const char *val) {
    if (!c || !key || !val) return -1;

    size_t len = strlen(val);

    if (strcmp(key, "host") == 0) {
        if (len >= AF_MQTT_HOST_MAX) return -1;
        if (len > 0) {
            if (val[0] == '.' || val[0] == '-') return -1;
            for (size_t i = 0; i < len; i++) {
                char ch = val[i];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) return -1;
            }
        }
        strcpy(c->host, val);
        return 0;
    }

    if (strcmp(key, "user") == 0) {
        if (len >= AF_MQTT_USER_MAX) return -1;
        for (size_t i = 0; i < len; i++) {
            unsigned char ch = (unsigned char)val[i];
            if (ch < 0x20 || ch > 0x7E) return -1;
        }
        strcpy(c->user, val);
        return 0;
    }

    if (strcmp(key, "pass") == 0) {
        if (len >= AF_MQTT_PASS_MAX) return -1;
        for (size_t i = 0; i < len; i++) {
            unsigned char ch = (unsigned char)val[i];
            if (ch < 0x20 || ch > 0x7E) return -1;
        }
        strcpy(c->pass, val);
        return 0;
    }

    if (strcmp(key, "prefix") == 0) {
        if (len == 0) return -1;
        if (len >= AF_MQTT_PREFIX_MAX) return -1;
        if (af_ha_valid_id(val, 1) != 1) return -1;
        strcpy(c->prefix, val);
        return 0;
    }

    return -2;
}

static inline size_t af_mqtt_cfg_json(const af_mqtt_cfg_t *c, char *out, size_t cap) {
    if (!c || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }

    char esc_host[256];
    char esc_user[256];
    char esc_prefix[256];

    if (c->host[0] != '\0') {
        if (af_json_escape(c->host, esc_host, sizeof(esc_host)) == 0) {
            out[0] = '\0';
            return 0;
        }
    } else {
        esc_host[0] = '\0';
    }

    if (c->user[0] != '\0') {
        if (af_json_escape(c->user, esc_user, sizeof(esc_user)) == 0) {
            out[0] = '\0';
            return 0;
        }
    } else {
        esc_user[0] = '\0';
    }

    if (c->prefix[0] != '\0') {
        if (af_json_escape(c->prefix, esc_prefix, sizeof(esc_prefix)) == 0) {
            out[0] = '\0';
            return 0;
        }
    } else {
        esc_prefix[0] = '\0';
    }

    int pass_set = (c->pass[0] != '\0') ? 1 : 0;

    int n = snprintf(out, cap,
        "{\"enabled\":%d,\"host\":\"%s\",\"port\":%u,\"user\":\"%s\",\"pass_set\":%d,\"period_s\":%u,\"prefix\":\"%s\"}",
        c->enabled ? 1 : 0,
        esc_host,
        (unsigned)c->port,
        esc_user,
        pass_set,
        (unsigned)c->period_s,
        esc_prefix
    );

    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_stat_json(const af_mqtt_stat_t *s, char *out, size_t cap) {
    if (!s || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }

    const char *err_name = "unknown";
    if (s->last_err < AF_MQTT_E_COUNT) {
        err_name = AF_MQTT_ERR_NAMES[s->last_err];
    }

    int n = snprintf(out, cap,
        "{\"enabled\":%d,\"connected\":%d,\"last_err\":\"%s\",\"published\":%u,\"failed\":%u}",
        s->enabled ? 1 : 0,
        s->connected ? 1 : 0,
        err_name,
        (unsigned)s->published,
        (unsigned)s->failed
    );

    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_config_topic_p(const char *prefix, const af_ha_dev_t *d, const af_ha_sensor_t *s, char *out, size_t cap) {
    if (!prefix || !d || !s || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }
    if (!d->node_id || !s->key) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(prefix, 1) != 1) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(d->node_id, 0) != 1) {
        out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap, "%s/sensor/%s/%s/config", prefix, d->node_id, s->key);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_ha_status_topic(const char *prefix, char *out, size_t cap) {
    if (!prefix || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(prefix, 1) != 1) {
        out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap, "%s/status", prefix);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_ble_topic(const af_ha_dev_t *d, char *out, size_t cap) {
    if (!d || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }
    if (!d->base || !d->node_id) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(d->base, 1) != 1) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(d->node_id, 0) != 1) {
        out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap, "%s/%s/ble", d->base, d->node_id);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_ble_config_topic(const char *prefix, const af_ha_dev_t *d, char *out, size_t cap) {
    if (!prefix || !d || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }
    if (!d->node_id) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(prefix, 1) != 1) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_valid_id(d->node_id, 0) != 1) {
        out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap, "%s/binary_sensor/%s/ble/config", prefix, d->node_id);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline size_t af_mqtt_ble_config_json(const af_ha_dev_t *d, char *out, size_t cap) {
    if (!d || !out || cap == 0) {
        if (out && cap > 0) out[0] = '\0';
        return 0;
    }
    if (!d->node_id || !d->base) {
        out[0] = '\0';
        return 0;
    }

    char ble_topic[128];
    char avail_topic[128];

    if (af_mqtt_ble_topic(d, ble_topic, sizeof(ble_topic)) == 0) {
        out[0] = '\0';
        return 0;
    }
    if (af_ha_avail_topic(d, avail_topic, sizeof(avail_topic)) == 0) {
        out[0] = '\0';
        return 0;
    }

    char esc_node[256];
    char esc_ble[256];
    char esc_avail[256];

    if (af_json_escape(d->node_id, esc_node, sizeof(esc_node)) == 0) {
        out[0] = '\0';
        return 0;
    }
    if (af_json_escape(ble_topic, esc_ble, sizeof(esc_ble)) == 0) {
        out[0] = '\0';
        return 0;
    }
    if (af_json_escape(avail_topic, esc_avail, sizeof(esc_avail)) == 0) {
        out[0] = '\0';
        return 0;
    }

    char uniq_id[256];
    int n_uniq = snprintf(uniq_id, sizeof(uniq_id), "%s_ble", d->node_id);
    if (n_uniq < 0 || (size_t)n_uniq >= sizeof(uniq_id)) {
        out[0] = '\0';
        return 0;
    }

    char esc_uniq[256];
    if (af_json_escape(uniq_id, esc_uniq, sizeof(esc_uniq)) == 0) {
        out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap,
        "{\"name\":\"BLE AtomFast\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"dev_cla\":\"connectivity\",\"dev\":{\"ids\":[\"%s\"]}}",
        esc_uniq,
        esc_ble,
        esc_avail,
        esc_node
    );

    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

static inline uint8_t af_mqtt_air_caps(uint8_t sensor_caps, float t, float p) {
    uint8_t mask = 0;
    if ((sensor_caps & AF_CAP_TEMP) && !isnan(t)) {
        mask |= 0x01;
    }
    if ((sensor_caps & AF_CAP_PRESS) && !isnan(p)) {
        mask |= 0x02;
    }
    return mask;
}

static inline int af_mqtt_due(uint32_t now_s, uint32_t last_s, uint32_t period_s, int have_last) {
    if (have_last == 0) return 1;
    if (now_s < last_s) return 1;
    if (now_s - last_s >= period_s) return 1;
    return 0;
}

static inline int af_mqtt_match(const char *s, int len, const char *lit) {
    if (!s || !lit) return 0;
    if (len < 0) return 0;
    if ((size_t)len != strlen(lit)) return 0;
    if (memcmp(s, lit, (size_t)len) != 0) return 0;
    return 1;
}


/* Ограничитель топиков (ACL на брокере нет - дисциплина прошивки) */
#define AF_MQTT_NODE_PREFIX "atomfast_"
#define AF_MQTT_BASE "atomfast"
#define AF_MQTT_TOPIC_MAX 127

/* Вспомогательная: проверка суффикса */
static inline int af_mqtt_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return 0;
    size_t ls = strlen(s);
    size_t lf = strlen(suffix);
    if (ls < lf) return 0;
    return (0 == memcmp(s + ls - lf, suffix, lf));
}

/* Проверка разрешения публикации */
static inline int af_mqtt_pub_allowed(const char *prefix, const char *topic, int retain)
{
    if (!prefix || !topic) return 0;
    if (1 != af_ha_valid_id(prefix, 1)) return 0;

    size_t tl = strlen(topic);
    if (tl < 1 || tl > AF_MQTT_TOPIC_MAX) return 0;

    /* Запрет спецсимволов и "//" */
    if (strchr(topic, '+') || strchr(topic, '#') || strstr(topic, "//")) return 0;

    /* Формируем допустимые префиксы в буфер */
    char buf[96];
    int n;

    /* Вариант 1: <prefix>/sensor/atomfast_ */
    n = snprintf(buf, sizeof(buf), "%s/sensor/%s", prefix, AF_MQTT_NODE_PREFIX);
    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;
    size_t len1 = (size_t)n;
    if (tl > len1 && 0 == memcmp(topic, buf, len1)) {
        /* Проверка retain */
        if (retain != 0) {
            if (!(af_mqtt_ends_with(topic, "/config") || af_mqtt_ends_with(topic, "/status")))
                return 0;
        }
        return 1;
    }

    /* Вариант 2: <prefix>/binary_sensor/atomfast_ */
    n = snprintf(buf, sizeof(buf), "%s/binary_sensor/%s", prefix, AF_MQTT_NODE_PREFIX);
    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;
    size_t len2 = (size_t)n;
    if (tl > len2 && 0 == memcmp(topic, buf, len2)) {
        if (retain != 0) {
            if (!(af_mqtt_ends_with(topic, "/config") || af_mqtt_ends_with(topic, "/status")))
                return 0;
        }
        return 1;
    }

    /* Вариант 3: atomfast/atomfast_ */
    n = snprintf(buf, sizeof(buf), "%s/%s", AF_MQTT_BASE, AF_MQTT_NODE_PREFIX);
    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;
    size_t len3 = (size_t)n;
    if (tl > len3 && 0 == memcmp(topic, buf, len3)) {
        if (retain != 0) {
            if (!(af_mqtt_ends_with(topic, "/config") || af_mqtt_ends_with(topic, "/status")))
                return 0;
        }
        return 1;
    }

    return 0;
}

/* Проверка разрешения подписки */
static inline int af_mqtt_sub_allowed(const char *prefix, const char *topic)
{
    if (!prefix || !topic) return 0;
    if (1 != af_ha_valid_id(prefix, 1)) return 0;

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "%s/status", prefix);
    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;

    return (0 == strcmp(topic, buf));
}

/* Жизненный цикл задачи MQTT: задача и клиент существуют только при enabled.
   in_task=0 (старт/POST настроек): 1 = создать задачу (enabled и не запущена), иначе 0.
   in_task=1 (внутри задачи): 2 = освободить всё и завершиться (выключено), иначе 0. */
#define AF_MQTT_ACT_NONE 0
#define AF_MQTT_ACT_SPAWN 1
#define AF_MQTT_ACT_EXIT 2
static inline int af_mqtt_task_action(int enabled, int running, int in_task) {
    if (in_task) return enabled ? AF_MQTT_ACT_NONE : AF_MQTT_ACT_EXIT;
    return (enabled && !running) ? AF_MQTT_ACT_SPAWN : AF_MQTT_ACT_NONE;
}

#endif
