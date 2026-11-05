#ifndef AF_SNS_JSON_H
#define AF_SNS_JSON_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_sns_reg.h"

typedef struct {
    uint8_t sensor; uint8_t model; uint8_t addr; uint8_t enabled;
    int16_t t_off10; int16_t p_off10; int16_t h_off10; uint16_t period_s;
    uint8_t state;
    uint8_t caps;
    float t_c; float p_mmhg; float h_pct;
    uint32_t n_ok; uint32_t n_err; uint32_t store_ok; uint32_t store_fail; uint32_t last_ok_ts;
    int32_t last_ok_age_s;
    const char *err;
} af_sns_view_t;

static inline int af_sns_json_list(const af_sns_view_t *v, int n, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) return -1;
    if (n < 0) return -1;
    if (n > 0 && v == NULL) return -1;

    size_t pos = 0;
    int r;

    r = snprintf(buf + pos, cap - pos, "[");
    if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
    pos += (size_t)r;

    for (int i = 0; i < n; i++) {
        const af_sns_view_t *s = &v[i];

        if (i > 0) {
            r = snprintf(buf + pos, cap - pos, ",");
            if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
            pos += (size_t)r;
        }

        r = snprintf(buf + pos, cap - pos, "{\"sensor\":%u,", (unsigned)s->sensor);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        const char *mkey = af_sns_key((af_sns_model_t)s->model);
        if (mkey == NULL) mkey = "none";
        r = snprintf(buf + pos, cap - pos, "\"model\":\"%s\",", mkey);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"addr\":\"0x%02X\",", (unsigned)s->addr);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"enabled\":%s,", s->enabled ? "true" : "false");
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        const char *status;
        if (s->enabled == 0) status = "off";
        else if (s->state == 1) status = "ok";
        else status = "err";

        r = snprintf(buf + pos, cap - pos, "\"status\":\"%s\",", status);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        const char *err_str = "";
        if (status[0] == 'e') {
            if (s->err == NULL || s->err[0] == '\0') {
                err_str = (s->state == 0) ? "init" : "error";
            } else {
                err_str = s->err;
            }
        }
        r = snprintf(buf + pos, cap - pos, "\"err\":\"%s\",", err_str);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"caps\":%u,\"temp\":%s,\"press\":%s,\"hum\":%s,", (unsigned)s->caps,
                     (s->caps & AF_CAP_TEMP) ? "true" : "false", (s->caps & AF_CAP_PRESS) ? "true" : "false",
                     (s->caps & AF_CAP_HUM) ? "true" : "false");
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        int show_t = (status[0] == 'o' && status[1] == 'k') && (s->caps & AF_CAP_TEMP) && !isnan(s->t_c);
        if (show_t) {
            r = snprintf(buf + pos, cap - pos, "\"t_c\":%.2f,", (double)s->t_c);
        } else {
            r = snprintf(buf + pos, cap - pos, "\"t_c\":null,");
        }
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        int show_h = (status[0] == 'o' && status[1] == 'k') && (s->caps & AF_CAP_HUM) && !isnan(s->h_pct);
        if (show_h) {
            r = snprintf(buf + pos, cap - pos, "\"h_pct\":%.2f,", (double)s->h_pct);
        } else {
            r = snprintf(buf + pos, cap - pos, "\"h_pct\":null,");
        }
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        int show_p = (status[0] == 'o' && status[1] == 'k') && (s->caps & AF_CAP_PRESS) && !isnan(s->p_mmhg);
        if (show_p) {
            r = snprintf(buf + pos, cap - pos, "\"p_mmhg\":%.2f,", (double)s->p_mmhg);
        } else {
            r = snprintf(buf + pos, cap - pos, "\"p_mmhg\":null,");
        }
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        double t_off = (s->t_off10 == 0) ? 0.0 : (double)s->t_off10 / 10.0;
        r = snprintf(buf + pos, cap - pos, "\"t_off\":%.1f,", t_off);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        double p_off = (s->p_off10 == 0) ? 0.0 : (double)s->p_off10 / 10.0;
        r = snprintf(buf + pos, cap - pos, "\"p_off\":%.1f,", p_off);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        double h_off = (s->h_off10 == 0) ? 0.0 : (double)s->h_off10 / 10.0;
        r = snprintf(buf + pos, cap - pos, "\"h_off\":%.1f,", h_off);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"period_s\":%u,", (unsigned)s->period_s);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"n_ok\":%lu,", (unsigned long)s->n_ok);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"n_err\":%lu,", (unsigned long)s->n_err);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"store_ok\":%lu,", (unsigned long)s->store_ok);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"store_fail\":%lu,", (unsigned long)s->store_fail);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"last_ok_ts\":%lu,", (unsigned long)s->last_ok_ts);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;

        r = snprintf(buf + pos, cap - pos, "\"last_ok_s_ago\":%ld}", (long)s->last_ok_age_s);
        if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
        pos += (size_t)r;
    }

    r = snprintf(buf + pos, cap - pos, "]");
    if (r < 0 || (size_t)r >= cap - pos) { buf[0] = '\0'; return -1; }
    pos += (size_t)r;

    return (int)pos;
}

#endif
