#ifndef AF_MQTT_HA_H
#define AF_MQTT_HA_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define AF_HA_SENSOR_COUNT 9
#define AF_HA_ONLINE "online"
#define AF_HA_OFFLINE "offline"

typedef struct {
    const char *node_id;
    const char *name;
    const char *model;
    const char *sw;
    const char *base;
} af_ha_dev_t;

typedef struct {
    const char *key;
    const char *name;
    const char *unit;
    const char *dev_class;
    const char *state_class;
    uint8_t precision;
} af_ha_sensor_t;

static const af_ha_sensor_t AF_HA_SENSORS[AF_HA_SENSOR_COUNT] = {
    {"dose_rate", "Мощность дозы", "µSv/h", NULL, "measurement", 4},
    {"dose_rate_ur", "Мощность дозы (µR/h)", "µR/h", NULL, "measurement", 2},
    {"cps", "CPS", "cps", NULL, "measurement", 2},
    {"pulses_2s", "Импульсы за 2 с", NULL, NULL, "measurement", 0},
    {"dose", "Накопленная доза", "µSv", NULL, "total_increasing", 3},
    {"battery", "Батарея AtomFast", "%", "battery", "measurement", 0},
    {"temp_af", "Температура AtomFast", "°C", "temperature", "measurement", 0},
    {"air_temp", "Температура воздуха", "°C", "temperature", "measurement", 1},
    {"air_press", "Атмосферное давление", "mmHg", "pressure", "measurement", 1}
};

typedef struct {
    float dose_rate, cps, dose;
    int pulses_2s, battery, temp_af;
    float air_temp, air_press;
    uint32_t ts;
} af_ha_state_t;

static inline int af_ha_valid_id(const char *s, int allow_slash_dash) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len < 1 || len > 32) return 0;
    if (s[0] == '/' || s[len - 1] == '/') return 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
        if (allow_slash_dash && (c == '/' || c == '-')) continue;
        return 0;
    }
    return 1;
}

static inline size_t af_json_escape(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return 0;
    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"') {
            if (pos + 2 >= cap) { if (pos < cap) out[pos] = '\0'; return 0; }
            out[pos++] = '\\'; out[pos++] = '"';
        } else if (c == '\\') {
            if (pos + 2 >= cap) { if (pos < cap) out[pos] = '\0'; return 0; }
            out[pos++] = '\\'; out[pos++] = '\\';
        } else if (c < 0x20) {
            if (pos + 6 >= cap) { if (pos < cap) out[pos] = '\0'; return 0; }
            out[pos++] = '\\'; out[pos++] = 'u'; out[pos++] = '0'; out[pos++] = '0';
            char hi = (c >> 4) & 0x0F;
            char lo = c & 0x0F;
            out[pos++] = (hi < 10) ? ('0' + hi) : ('a' + hi - 10);
            out[pos++] = (lo < 10) ? ('0' + lo) : ('a' + lo - 10);
        } else {
            if (pos + 1 >= cap) { if (pos < cap) out[pos] = '\0'; return 0; }
            out[pos++] = (char)c;
        }
    }
    if (pos >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    out[pos] = '\0';
    return pos;
}

static inline size_t af_ha_state_topic(const af_ha_dev_t *d, char *out, size_t cap) {
    if (!d || !d->node_id || !d->base || !out || cap == 0) return 0;
    if (!af_ha_valid_id(d->node_id, 0) || !af_ha_valid_id(d->base, 1)) return 0;
    int n = snprintf(out, cap, "%s/%s/state", d->base, d->node_id);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    return (size_t)n;
}

static inline size_t af_ha_avail_topic(const af_ha_dev_t *d, char *out, size_t cap) {
    if (!d || !d->node_id || !d->base || !out || cap == 0) return 0;
    if (!af_ha_valid_id(d->node_id, 0) || !af_ha_valid_id(d->base, 1)) return 0;
    int n = snprintf(out, cap, "%s/%s/status", d->base, d->node_id);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    return (size_t)n;
}

static inline size_t af_ha_config_topic(const af_ha_dev_t *d, const af_ha_sensor_t *s, char *out, size_t cap) {
    if (!d || !d->node_id || !s || !s->key || !out || cap == 0) return 0;
    if (!af_ha_valid_id(d->node_id, 0)) return 0;
    int n = snprintf(out, cap, "homeassistant/sensor/%s/%s/config", d->node_id, s->key);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    return (size_t)n;
}

static inline size_t af_ha_config_json(const af_ha_dev_t *d, const af_ha_sensor_t *s, char *out, size_t cap) {
    if (!d || !d->node_id || !s || !s->key || !s->name || !out || cap == 0) return 0;
    if (!af_ha_valid_id(d->node_id, 0)) return 0;

    char state_topic[128], avail_topic[128];
    if (af_ha_state_topic(d, state_topic, sizeof(state_topic)) == 0) return 0;
    if (af_ha_avail_topic(d, avail_topic, sizeof(avail_topic)) == 0) return 0;

    char esc_name[128], esc_uniq[128], esc_state[128], esc_avail[128], esc_key[128];
    char esc_unit[128], esc_dev_cla[128], esc_stat_cla[128], esc_node[128], esc_dev_name[128], esc_model[128], esc_sw[128];

    if (af_json_escape(s->name, esc_name, sizeof(esc_name)) == 0) return 0;
    if (af_json_escape(d->node_id, esc_node, sizeof(esc_node)) == 0) return 0;
    if (af_json_escape(s->key, esc_key, sizeof(esc_key)) == 0) return 0;
    if (af_json_escape(state_topic, esc_state, sizeof(esc_state)) == 0) return 0;
    if (af_json_escape(avail_topic, esc_avail, sizeof(esc_avail)) == 0) return 0;

    char uniq_buf[128];
    snprintf(uniq_buf, sizeof(uniq_buf), "%s_%s", d->node_id, s->key);
    if (af_json_escape(uniq_buf, esc_uniq, sizeof(esc_uniq)) == 0) return 0;

    if (s->unit && af_json_escape(s->unit, esc_unit, sizeof(esc_unit)) == 0) return 0;
    if (s->dev_class && af_json_escape(s->dev_class, esc_dev_cla, sizeof(esc_dev_cla)) == 0) return 0;
    if (s->state_class && af_json_escape(s->state_class, esc_stat_cla, sizeof(esc_stat_cla)) == 0) return 0;
    if (d->name && af_json_escape(d->name, esc_dev_name, sizeof(esc_dev_name)) == 0) return 0;
    if (d->model && af_json_escape(d->model, esc_model, sizeof(esc_model)) == 0) return 0;
    if (d->sw && af_json_escape(d->sw, esc_sw, sizeof(esc_sw)) == 0) return 0;

    int n = snprintf(out, cap,
        "{\"name\":\"%s\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\",\"val_tpl\":\"{{ value_json.%s }}\"",
        esc_name, esc_uniq, esc_state, esc_avail, esc_key);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }

    if (s->unit) {
        n += snprintf(out + n, cap - n, ",\"unit_of_meas\":\"%s\"", esc_unit);
        if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    }
    if (s->dev_class) {
        n += snprintf(out + n, cap - n, ",\"dev_cla\":\"%s\"", esc_dev_cla);
        if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    }
    if (s->state_class) {
        n += snprintf(out + n, cap - n, ",\"stat_cla\":\"%s\"", esc_stat_cla);
        if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    }

    n += snprintf(out + n, cap - n, ",\"sug_dsp_prc\":%d", s->precision);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }

    n += snprintf(out + n, cap - n, ",\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mdl\":\"%s\",\"mf\":\"VibeEngineering\",\"sw\":\"%s\"}}",
        esc_node, esc_dev_name, esc_model, esc_sw);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }

    return (size_t)n;
}

static inline size_t af_ha_state_json(const af_ha_state_t *st, uint8_t air_caps, char *out, size_t cap) {
    if (!st || !out || cap == 0) return 0;
    if (isnan(st->dose_rate) || isinf(st->dose_rate) ||
        isnan(st->cps) || isinf(st->cps) ||
        isnan(st->dose) || isinf(st->dose)) {
        if (cap > 0) out[0] = '\0';
        return 0;
    }

    int n = snprintf(out, cap,
        "{\"ts\":%u,\"dose_rate\":%.4f,\"dose_rate_ur\":%.2f,\"cps\":%.2f,\"pulses_2s\":%d,\"dose\":%.3f,\"battery\":%d,\"temp_af\":%d",
        (unsigned)st->ts, st->dose_rate, st->dose_rate * 100.0f, st->cps, st->pulses_2s, st->dose, st->battery, st->temp_af);
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }

    if ((air_caps & 0x01) && !isnan(st->air_temp)) {
        n += snprintf(out + n, cap - n, ",\"air_temp\":%.1f", st->air_temp);
        if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    }
    if ((air_caps & 0x02) && !isnan(st->air_press)) {
        n += snprintf(out + n, cap - n, ",\"air_press\":%.1f", st->air_press);
        if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }
    }

    n += snprintf(out + n, cap - n, "}");
    if (n < 0 || (size_t)n >= cap) { if (cap > 0) out[0] = '\0'; return 0; }

    return (size_t)n;
}

#endif
