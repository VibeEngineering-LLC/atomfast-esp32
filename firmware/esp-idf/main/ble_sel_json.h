#ifndef BLE_SEL_JSON_H
#define BLE_SEL_JSON_H
#include <stdarg.h>
#include "ble_sel.h"

/* Дописывает отформатированную строку в буфер через vsnprintf;
   при нехватке места устанавливает *ovf = 1 и завершает буфер. */
static inline void ble__jput(char *buf, size_t cap, size_t *pos, int *ovf, const char *fmt, ...) {
    if (*ovf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos) {
        *ovf = 1;
        *pos = cap - 1;
        buf[cap - 1] = '\0';
    } else {
        *pos += (size_t)n;
    }
}

/* Пищет JSON-строку с экранированием: кавычка, бэкслэш, управляющие (<0x20) → '?' */
static inline void ble__jstr(char *buf, size_t cap, size_t *pos, int *ovf, const char *str) {
    ble__jput(buf, cap, pos, ovf, "\"");
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        unsigned char ch = *p;
        if (ch == '"') {
            ble__jput(buf, cap, pos, ovf, "\\\"");
        } else if (ch == '\\') {
            ble__jput(buf, cap, pos, ovf, "\\\\");
        } else if (ch < 0x20) {
            ble__jput(buf, cap, pos, ovf, "?");
        } else {
            ble__jput(buf, cap, pos, ovf, "%c", (int)ch);
        }
    }
    ble__jput(buf, cap, pos, ovf, "\"");
}

/* Пишет объект устройства: {"addr":"<маска>","name":"<имя>","kind":"<тип>"} */
static inline void ble__jdev(char *buf, size_t cap, size_t *pos, int *ovf, const uint8_t addr[6], const char *name) {
    char mask[18];
    ble_sel_mask(addr, false, mask);
    const char *kind;
    uint8_t k = ble_kind_of(name);
    if (k == BLE_KIND_FAMILY) kind = "family";
    else if (k == BLE_KIND_ATOMFAST) kind = "atomfast";
    else kind = "atomswift";
    ble__jput(buf, cap, pos, ovf, "{\"addr\":\"%s\",\"name\":", mask);
    ble__jstr(buf, cap, pos, ovf, name);
    ble__jput(buf, cap, pos, ovf, ",\"kind\":\"%s\"}", kind);
}

/* Возвращает строковое имя кода заметки; NULL — для BLE_NOTE_NONE */
static inline const char *ble__jnote(uint8_t note) {
    switch (note) {
        case BLE_NOTE_NONE: return NULL;
        case 1:  return "deferred";
        case 2:  return "incompatible";
        case 3:  return "clear_failed";
        case 4:  return "tgt_write_failed";
        case 5:  return "meas_active";
        case 6:  return "aborted";
        case 7:  return "close_failed";
        case 8:  return "scan_start_failed";
        case 9:  return "no_cand";
        case 10: return "name_unsaved";
        default: return "unknown";
    }
}

/* Сериализует состояние выбора прибора в одну JSON-строку.
   Возвращает длину без '\0' либо BLE_E_NOSPACE, если буфер мал. */
static inline int ble_sel_json(const ble_sel_t *s, uint32_t now_ms, char *buf, size_t cap) {
    if (buf == NULL || cap < 2) return BLE_E_NOSPACE;
    size_t pos = 0;
    int ovf = 0;
    buf[0] = '\0';

    /* Названия состояний по индексу BLE_ST_* (0..7) */
    const char *st_names[8] = {
        "scan", "stopping", "list", "opening", "confirming", "live", "wait", "forgetting"
    };

    bool active  = (s->state == BLE_ST_SCAN);
    uint32_t elapsed = active ? (now_ms - s->scan_t0_ms) : 0u;

    /* Шапка: state, gen, max, scan */
    ble__jput(buf, cap, &pos, &ovf,
              "{\"state\":\"%s\",\"gen\":%u,\"max\":%d,"
              "\"scan\":{\"active\":%s,\"elapsed_ms\":%u,\"limit_ms\":%u},\"tgt\":",
              st_names[s->state], (unsigned)s->gen, (int)BLE_SEL_CAND_MAX,
              active ? "true" : "false",
              (unsigned)elapsed, (unsigned)BLE_SEL_SCAN_MS);

    /* tgt */
    if (s->has_tgt) {
        ble__jdev(buf, cap, &pos, &ovf, s->tgt_addr, s->tgt_name);
    } else {
        ble__jput(buf, cap, &pos, &ovf, "null");
    }
    ble__jput(buf, cap, &pos, &ovf, ",\"conn\":");

    /* conn */
    if (s->conn_valid) {
        ble__jdev(buf, cap, &pos, &ovf, s->conn_addr, s->open_req_name);
    } else {
        ble__jput(buf, cap, &pos, &ovf, "null");
    }
    ble__jput(buf, cap, &pos, &ovf, ",\"cands\":[");

    /* Список кандидатов (только used) */
    int first = 1;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        const ble_cand_t *c = &s->cands.c[i];
        if (!c->used) continue;
        if (!first) ble__jput(buf, cap, &pos, &ovf, ",");
        first = 0;

        char mask[18];
        bool wide = ble_cands_mask_clash(&s->cands, i);
        ble_sel_mask(c->addr, wide, mask);

        /* Определение совпадения с целью */
        int is_tgt = 0;
        if (s->has_tgt) {
            is_tgt = 1;
            for (int b = 0; b < 6; b++) {
                if (c->addr[b] != s->tgt_addr[b]) { is_tgt = 0; break; }
            }
        }

        /* Имя типа */
        const char *kind;
        uint8_t k = ble_kind_of(c->name);
        if (k == BLE_KIND_FAMILY) kind = "family";
        else if (k == BLE_KIND_ATOMFAST) kind = "atomfast";
        else kind = "atomswift";

        uint32_t age = now_ms - c->last_seen_ms;

        ble__jput(buf, cap, &pos, &ovf, "{\"tok\":%u,\"name\":", (unsigned)c->tok);
        ble__jstr(buf, cap, &pos, &ovf, c->name);
        ble__jput(buf, cap, &pos, &ovf,
                  ",\"kind\":\"%s\",\"rssi\":%d,\"addr\":\"%s\",\"age_ms\":%u,\"is_tgt\":%s}",
                  kind, (int)c->rssi, mask, (unsigned)age,
                  is_tgt ? "true" : "false");
    }

    ble__jput(buf, cap, &pos, &ovf, "],");

    /* err */
    const char *err_s = ble__jnote(s->err);
    if (err_s) {
        ble__jput(buf, cap, &pos, &ovf, "\"err\":\"%s\"", err_s);
    } else {
        ble__jput(buf, cap, &pos, &ovf, "\"err\":null");
    }
    ble__jput(buf, cap, &pos, &ovf, ",");

    /* note */
    const char *note_s = ble__jnote(s->note);
    if (note_s) {
        ble__jput(buf, cap, &pos, &ovf, "\"note\":\"%s\"", note_s);
    } else {
        ble__jput(buf, cap, &pos, &ovf, "\"note\":null");
    }
    ble__jput(buf, cap, &pos, &ovf, ",");

    /* lost_s — только в WAIT */
    uint32_t lost_s = (s->state == BLE_ST_WAIT) ? ((now_ms - s->lost_t0_ms) / 1000u) : 0u;
    ble__jput(buf, cap, &pos, &ovf, "\"lost_s\":%u}", (unsigned)lost_s);

    return ovf ? BLE_E_NOSPACE : (int)pos; /* @jbound */
}

#endif
