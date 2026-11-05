#ifndef BLE_SEL_CAND_H
#define BLE_SEL_CAND_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define BLE_SEL_CAND_MAX   8
#define BLE_SEL_NAME_LEN   16   /* до 15 символов + '\0' */
#define BLE_KIND_FAMILY    0    /* допущен только по UUID сервиса */
#define BLE_KIND_ATOMFAST  1    /* имя начинается с "atomfast" без учёта регистра */
#define BLE_KIND_ATOMSWIFT 2    /* имя начинается с "atomswift" без учёта регистра */

typedef struct {
    uint8_t  addr[6];
    uint8_t  atype;
    int8_t   rssi;
    uint8_t  kind;          /* BLE_KIND_* */
    uint8_t  tok;           /* 1..255; 0 = нет */
    uint8_t  used;          /* 0 = слот свободен */
    char     name[BLE_SEL_NAME_LEN];
    uint32_t last_seen_ms;
} ble_cand_t;

typedef struct {
    ble_cand_t c[BLE_SEL_CAND_MAX];
    uint8_t    next_tok;    /* следующий выдаваемый tok */
} ble_cands_t;

/* приведение заглавной латинской буквы к строчной, остальное без изменений */
static inline int ble__lc(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

/* s начинается с p (p — строчными), без учёта регистра */
static inline bool ble__prefix_ci(const char *s, const char *p) {
    if (s == NULL) return false;
    for (size_t i = 0; p[i] != '\0'; i++) {
        if (s[i] == '\0') return false;
        if (ble__lc((unsigned char)s[i]) != (unsigned char)p[i]) return false; /* @ci */
    }
    return true;
}

/* сброс таблицы: все слоты свободны, next_tok = 1 */
static inline void ble_cands_clear(ble_cands_t *t) {
    memset(t->c, 0, sizeof(t->c));
    t->next_tok = 1;
}

/* тип устройства по имени: NULL/пустое — FAMILY */
static inline uint8_t ble_kind_of(const char *name) {
    if (name == NULL || name[0] == '\0') return BLE_KIND_FAMILY;
    if (ble__prefix_ci(name, "atomswift")) return BLE_KIND_ATOMSWIFT;
    if (ble__prefix_ci(name, "atomfast")) return BLE_KIND_ATOMFAST;
    return BLE_KIND_FAMILY;
}

/* копия имени (не более 15 символов + '\0') и назначение kind */
static inline void ble__set_name(ble_cand_t *c, const char *name) {
    if (name != NULL && name[0] != '\0') { /* @name_keep */
        size_t len = strlen(name);
        if (len > 15u) len = 15u;
        memcpy(c->name, name, len);
        c->name[len] = '\0';
        c->kind = ble_kind_of(c->name);
    }
}

/* обновление или добавление кандидата; возвращает индекс или −1 */
static inline int ble_cands_update(ble_cands_t *t, const uint8_t addr[6], uint8_t atype,
                                   int8_t rssi, const char *name, bool has_uuid, uint32_t now_ms) {
    /* поиск существующей записи по addr и atype */
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        if (t->c[i].used && memcmp(t->c[i].addr, addr, 6) == 0 && t->c[i].atype == atype) { /* @dedup */
            t->c[i].rssi = rssi;
            t->c[i].last_seen_ms = now_ms;
            ble__set_name(&t->c[i], name);
            return i;
        }
    }
    /* допуск: нужен UUID или не-FAMILY имя */
    if (!has_uuid && ble_kind_of(name) == BLE_KIND_FAMILY) return -1;
    /* выбор слота: первый свободный */
    int slot = -1;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        if (!t->c[i].used) { slot = i; break; }
    }
    /* все заняты — вытесняем запись с наименьшим last_seen_ms (при равенстве — меньший индекс) */
    if (slot < 0) {
        int victim = 0;
        for (int i = 1; i < BLE_SEL_CAND_MAX; i++) {
            if (t->c[i].last_seen_ms < t->c[victim].last_seen_ms) victim = i; /* @evict */
        }
        slot = victim;
    }
    /* заполнение новой записи */
    memcpy(t->c[slot].addr, addr, 6);
    t->c[slot].atype = atype;
    t->c[slot].rssi = rssi;
    t->c[slot].last_seen_ms = now_ms;
    t->c[slot].name[0] = '\0';
    t->c[slot].kind = BLE_KIND_FAMILY;
    ble__set_name(&t->c[slot], name);
    t->c[slot].tok = t->next_tok;
    t->next_tok = (t->next_tok >= 255) ? 1 : (uint8_t)(t->next_tok + 1);
    t->c[slot].used = 1;
    return slot;
}

/* индекс used-записи с заданным tok (tok ≠ 0), иначе −1 */
static inline int ble_cands_find_tok(const ble_cands_t *t, uint8_t tok) {
    if (tok == 0) return -1;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        if (t->c[i].used && t->c[i].tok == tok) return i;
    }
    return -1;
}

/* число занятых слотов */
static inline int ble_cands_count(const ble_cands_t *t) {
    int n = 0;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        if (t->c[i].used) n++;
    }
    return n;
}

/* есть ли другая used-запись (индекс ≠ idx) с теми же addr[0], addr[1], addr[5] */
static inline bool ble_cands_mask_clash(const ble_cands_t *t, int idx) {
    if (idx < 0 || idx >= BLE_SEL_CAND_MAX) return false;
    for (int i = 0; i < BLE_SEL_CAND_MAX; i++) {
        if (i == idx) continue;
        if (!t->c[i].used) continue;
        if (t->c[i].addr[0] == t->c[idx].addr[0] &&
            t->c[i].addr[1] == t->c[idx].addr[1] &&
            t->c[i].addr[5] == t->c[idx].addr[5]) {
            return true;
        }
    }
    return false;
}

/* маска адреса: 17 символов + '\0', заглавные hex */
static inline void ble_sel_mask(const uint8_t addr[6], bool wide, char out[18]) {
    if (wide) {
        snprintf(out, 18, "%02X:%02X:**:**:%02X:%02X", addr[0], addr[1], addr[4], addr[5]); /* @mask_wide */
    } else {
        snprintf(out, 18, "%02X:%02X:**:**:**:%02X", addr[0], addr[1], addr[5]); /* @mask_narrow */
    }
}

#endif
