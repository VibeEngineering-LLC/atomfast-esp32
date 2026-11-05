#ifndef AF_SETTINGS_H
#define AF_SETTINGS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* --- Константы --- */
#define AF_SET_WIN_MIN_S   60u
#define AF_SET_WIN_MAX_S   1200u
#define AF_SET_WIN_STEP_S  60u
#define AF_SET_WIN_DEF_S   60u
#define AF_SET_SIGMA_DEF   1u      /* допустимо 1, 2, 3 */
#define AF_SET_FIELD_MAX   24      /* длина буфера имени поля, с нулём */

/* --- Коды возврата --- */
#define AF_SET_OK              0
#define AF_SET_NEED_CONFIRM    1   /* только af_settings_apply */
#define AF_SET_E_UNKNOWN_FIELD (-1)
#define AF_SET_E_BAD_VALUE     (-2)
#define AF_SET_E_SYNTAX        (-3)
#define AF_SET_E_CAP           (-4) /* буфер мал */
#define AF_SET_E_ARG           (-5) /* NULL-аргумент */

/* --- Биты маски санитизации --- */
#define AF_SET_R_RAW     0x01u
#define AF_SET_R_AGG     0x02u
#define AF_SET_R_ENV     0x04u
#define AF_SET_R_WINDOW  0x08u
#define AF_SET_R_SIGMA   0x10u
#define AF_SET_R_BLE_EXT 0x20u

/* --- Типы --- */
typedef struct {
    uint8_t  store_raw, store_agg, store_env;   /* 0/1 */
    uint16_t window_s;                          /* 60..1200 кратно 60 */
    uint8_t  sigma_k;                           /* 1, 2, 3 */
    uint8_t  ble_ext;                           /* 0/1 */
} af_settings_t;

typedef struct {            /* частичное тело POST: has_* = поле было в теле */
    uint8_t  has_raw, has_agg, has_env, has_window, has_sigma, has_ble_ext, has_confirm;
    uint8_t  store_raw, store_agg, store_env;
    uint16_t window_s;
    uint8_t  sigma_k, ble_ext;
    uint8_t  confirm_clear_agg;
    char     err_field[AF_SET_FIELD_MAX];
} af_settings_patch_t;

/* 1. Заполнить структуру значениями по умолчанию */
static inline void af_settings_defaults(af_settings_t *s)
{
    if (s == NULL) return;
    s->store_raw = 1;
    s->store_agg = 1;
    s->store_env = 1;
    s->window_s  = AF_SET_WIN_DEF_S;
    s->sigma_k   = AF_SET_SIGMA_DEF;
    s->ble_ext   = 0;
}

/* 2. Проверка допустимости окна (сек) */
static inline int af_settings_window_valid(uint32_t w)
{
    if (w < AF_SET_WIN_MIN_S)  return 0;
    if (w > AF_SET_WIN_MAX_S)  return 0;
    if (w % AF_SET_WIN_STEP_S != 0u) return 0;
    return 1;
}

/* 3. Проверка допустимости коэффициента сигмы */
static inline int af_settings_sigma_valid(uint32_t k)
{
    if (k == 1u || k == 2u || k == 3u) return 1;
    return 0;
}

/* 4. Санитизация: невалидные поля -> умолчания, вернуть маску исправлений */
static inline uint32_t af_settings_sanitize(af_settings_t *s)
{
    uint32_t mask = 0u;
    if (s == NULL) return 0u;

    if (s->store_raw > 1) { s->store_raw = 1; mask |= AF_SET_R_RAW; }
    if (s->store_agg > 1) { s->store_agg = 1; mask |= AF_SET_R_AGG; }
    if (s->store_env > 1) { s->store_env = 1; mask |= AF_SET_R_ENV; }

    if (!af_settings_window_valid((uint32_t)s->window_s)) {
        s->window_s = AF_SET_WIN_DEF_S;
        mask |= AF_SET_R_WINDOW;
    }
    if (!af_settings_sigma_valid((uint32_t)s->sigma_k)) {
        s->sigma_k = AF_SET_SIGMA_DEF;
        mask |= AF_SET_R_SIGMA;
    }
    if (s->ble_ext > 1) { s->ble_ext = 0; mask |= AF_SET_R_BLE_EXT; }

    return mask;
}

/* 5. Вспомогательный: перенос строки из tmp в buf с проверкой границ */
static inline int af_set_emit(const char *tmp, int n, char *buf, size_t cap)
{
    if (n < 0)       return AF_SET_E_CAP;
    if (n >= 96)     return AF_SET_E_CAP;
    if (cap < (size_t)n + 1u) return AF_SET_E_CAP;
    memcpy(buf, tmp, (size_t)n + 1u);
    return n;
}

/* 6. Сериализация настроек в JSON (одна строка, без пробелов) */
static inline int af_settings_json_get(const af_settings_t *s, char *buf, size_t cap)
{
    if (s == NULL || buf == NULL) return AF_SET_E_ARG;

    char tmp[96];
    int n = snprintf(tmp, sizeof(tmp),
        "{\"store\":{\"raw\":%u,\"agg\":%u,\"env\":%u},"
        "\"window_s\":%u,\"sigma_k\":%u,\"ble_ext\":%u}",
        (unsigned)s->store_raw, (unsigned)s->store_agg, (unsigned)s->store_env,
        (unsigned)s->window_s,  (unsigned)s->sigma_k,   (unsigned)s->ble_ext);

    return af_set_emit(tmp, n, buf, cap);
}

/* 7. JSON: требуется подтверждение (очищение агрегата) */
static inline int af_settings_json_need_confirm(uint32_t agg_count,
                                                uint16_t window_old,
                                                char *buf, size_t cap)
{
    if (buf == NULL) return AF_SET_E_ARG;

    char tmp[96];
    int n = snprintf(tmp, sizeof(tmp),
        "{\"error\":\"need_confirm\",\"agg_count\":%lu,\"window_s_old\":%lu}",
        (unsigned long)agg_count, (unsigned long)window_old);

    return af_set_emit(tmp, n, buf, cap);
}

/* 8. JSON: успех, число очищенных записей */
static inline int af_settings_json_ok(uint32_t agg_cleared, char *buf, size_t cap)
{
    if (buf == NULL) return AF_SET_E_ARG;

    char tmp[96];
    int n = snprintf(tmp, sizeof(tmp),
        "{\"ok\":true,\"agg_cleared\":%lu}",
        (unsigned long)agg_cleared);

    return af_set_emit(tmp, n, buf, cap);
}

/* 9. JSON: ошибка; поле экранируется (квот, обратный слэш, управляющие -> '_') */
static inline int af_settings_json_error(int code, const char *field,
                                         char *buf, size_t cap)
{
    if (buf == NULL) return AF_SET_E_ARG;

    /* Неизвестный код ошибки */
    if (code != AF_SET_E_UNKNOWN_FIELD &&
        code != AF_SET_E_BAD_VALUE   &&
        code != AF_SET_E_SYNTAX) {
        return AF_SET_E_ARG;
    }

    char tmp[96];
    int n;

    if (code == AF_SET_E_SYNTAX) {
        n = snprintf(tmp, sizeof(tmp), "{\"error\":\"syntax\"}");
    } else {
        /* Экранирование имени поля в локальный буфер */
        char field_buf[AF_SET_FIELD_MAX + 8]; /* 32 байта, включая нуль */
        size_t i = 0u;

        if (field != NULL) {
            const char *p = field;
            while (*p != '\0' && i < sizeof(field_buf) - 1u) {
                unsigned char c = (unsigned char)*p;
                if (c == '"' || c == '\\' || (unsigned)c < 32u) {
                    field_buf[i] = '_';
                } else {
                    field_buf[i] = (char)c;
                }
                i++;
                p++;
            }
        }
        field_buf[i] = '\0';

        const char *msg = (code == AF_SET_E_UNKNOWN_FIELD)
                             ? "unknown_field" : "bad_value";
        n = snprintf(tmp, sizeof(tmp),
                     "{\"error\":\"%s\",\"field\":\"%s\"}", msg, field_buf);
    }

    return af_set_emit(tmp, n, buf, cap);
}

#define AF_SET_V_NUM   1
#define AF_SET_V_TRUE  2
#define AF_SET_V_FALSE 3
#define AF_SET_V_STR   4
#define AF_SET_V_OBJ   5

typedef struct { uint8_t type; uint32_t num; } af_set_val_t;

static inline void af_set_p_ws(const char *b, size_t len, size_t *i)
{
    /* пропустить пробелы, табы, переводы строк */
    while (*i < len) {
        char c = b[*i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            (*i)++;
        } else {
            break;
        }
    }
}

static inline int af_set_p_key(const char *b, size_t len, size_t *i, char *key, size_t kcap)
{
    /* на текущей позиции обязана стоять открывающая кавычка */
    if (*i >= len || b[*i] != '"') {
        return AF_SET_E_SYNTAX;
    }
    (*i)++; /* пропустить открывающую кавычку */

    size_t klen = 0;
    while (*i < len) {
        char c = b[*i];
        /* обратная косая внутри ключа запрещена */
        if (c == '\\') {
            return AF_SET_E_SYNTAX;
        }
        /* закрывающая кавычка — ключ прочитан */
        if (c == '"') {
            (*i)++;
            /* завершить строку нулём, уложившись в kcap */
            if (klen < kcap - 1) {
                key[klen] = '\0';
            } else {
                key[kcap - 1] = '\0';
            }
            return AF_SET_OK;
        }
        /* копировать символ (с усечением) */
        if (klen < kcap - 1) {
            key[klen] = c;
            klen++;
        }
        (*i)++;
    }
    /* конец данных до закрывающей кавычки */
    return AF_SET_E_SYNTAX;
}

static inline int af_set_p_val(const char *b, size_t len, size_t *i, af_set_val_t *v)
{
    if (*i >= len) {
        return AF_SET_E_SYNTAX;
    }
    char c = b[*i];

    /* начало вложенного объекта — символ '{' не потребляем */
    if (c == '{') {
        v->type = AF_SET_V_OBJ;
        return AF_SET_OK;
    }

    /* строка в двойных кавычках */
    if (c == '"') {
        (*i)++;
        while (*i < len) {
            char ch = b[*i];
            if (ch == '\\') {
                return AF_SET_E_SYNTAX;
            }
            if (ch == '"') {
                (*i)++;
                v->type = AF_SET_V_STR;
                return AF_SET_OK;
            }
            (*i)++;
        }
        return AF_SET_E_SYNTAX;
    }

    /* целое число без знака, до 5 цифр */
    if (c >= '0' && c <= '9') {
        size_t digits = 0;
        uint32_t num = 0;
        while (*i < len && b[*i] >= '0' && b[*i] <= '9') {
            digits++;
            if (digits > 5) {
                return AF_SET_E_BAD_VALUE;
            }
            num = num * 10u + (uint32_t)(b[*i] - '0');
            (*i)++;
        }
        /* дробная часть или экспонента не поддерживаются */
        if (*i < len) {
            char next = b[*i];
            if (next == '.' || next == 'e' || next == 'E') {
                return AF_SET_E_SYNTAX;
            }
        }
        v->type = AF_SET_V_NUM;
        v->num = num;
        return AF_SET_OK;
    }

    /* ключевое слово true */
    if (c == 't') {
        if (len - *i < 4) {
            return AF_SET_E_SYNTAX;
        }
        if (b[*i] != 't' || b[*i + 1] != 'r' || b[*i + 2] != 'u' || b[*i + 3] != 'e') {
            return AF_SET_E_SYNTAX;
        }
        size_t after = *i + 4;
        /* за словом не должно следовать буква, цифра или подчёркивание */
        if (after < len) {
            char nc = b[after];
            if ((nc >= 'a' && nc <= 'z') || (nc >= 'A' && nc <= 'Z')
                || (nc >= '0' && nc <= '9') || nc == '_') {
                return AF_SET_E_SYNTAX;
            }
        }
        *i = after;
        v->type = AF_SET_V_TRUE;
        return AF_SET_OK;
    }

    /* ключевое слово false */
    if (c == 'f') {
        if (len - *i < 5) {
            return AF_SET_E_SYNTAX;
        }
        if (b[*i] != 'f' || b[*i + 1] != 'a' || b[*i + 2] != 'l'
            || b[*i + 3] != 's' || b[*i + 4] != 'e') {
            return AF_SET_E_SYNTAX;
        }
        size_t after = *i + 5;
        if (after < len) {
            char nc = b[after];
            if ((nc >= 'a' && nc <= 'z') || (nc >= 'A' && nc <= 'Z')
                || (nc >= '0' && nc <= '9') || nc == '_') {
                return AF_SET_E_SYNTAX;
            }
        }
        *i = after;
        v->type = AF_SET_V_FALSE;
        return AF_SET_OK;
    }

    /* любой иной символ (минус, одинарная кавычка, прочие буквы) */
    return AF_SET_E_SYNTAX;
}

static inline int af_set_p_fld_flag(const char *b, size_t len, size_t *i, uint8_t *out)
{
    af_set_val_t v;
    int rc = af_set_p_val(b, len, i, &v);
    if (rc != AF_SET_OK)
        return rc;
    if (v.type == AF_SET_V_NUM && (v.num == 0u || v.num == 1u)) {
        *out = (uint8_t)v.num;
        return AF_SET_OK;
    }
    if (v.type == AF_SET_V_TRUE) {
        *out = 1u;
        return AF_SET_OK;
    }
    if (v.type == AF_SET_V_FALSE) {
        *out = 0u;
        return AF_SET_OK;
    }
    return AF_SET_E_BAD_VALUE;
}

static inline int af_settings_parse(const char *body, size_t len, af_settings_patch_t *p)
{
    if (body == NULL || p == NULL)
        return AF_SET_E_ARG;
    memset(p, 0, sizeof(*p));

    size_t i = 0;
    af_set_p_ws(body, len, &i);

    /* Ожидаем открывающую фигурную скобку */
    if (i >= len || body[i] != '{')
        return AF_SET_E_SYNTAX;
    i++;

    af_set_p_ws(body, len, &i);

    /* Пустой объект {} — корректно */
    if (i < len && body[i] == '}') {
        i++;
        af_set_p_ws(body, len, &i);
        if (i != len)
            return AF_SET_E_SYNTAX;
        return AF_SET_OK;
    }

    for (;;) {
        char key[AF_SET_FIELD_MAX];
        int rc = af_set_p_key(body, len, &i, key, sizeof(key));
        if (rc != AF_SET_OK)
            return rc;

        af_set_p_ws(body, len, &i);
        if (i >= len || body[i] != ':')
            return AF_SET_E_SYNTAX;
        i++;
        af_set_p_ws(body, len, &i);

        if (strcmp(key, "store") == 0) {
            /* store — значение обязано быть объектом */
            if (i >= len || body[i] != '{') {
                const char *ef = "store";
                size_t el = 0;
                while (ef[el] && el < AF_SET_FIELD_MAX - 1)
                    { p->err_field[el] = ef[el]; el++; }
                p->err_field[el] = '\0';
                return AF_SET_E_BAD_VALUE;
            }
            i++; /* потребляем '{' */
            af_set_p_ws(body, len, &i);

            if (i < len && body[i] == '}') {
                /* Пустой объект внутри store */
                i++;
            } else {
                for (;;) {
                    char skey[AF_SET_FIELD_MAX];
                    rc = af_set_p_key(body, len, &i, skey, sizeof(skey));
                    if (rc != AF_SET_OK)
                        return rc;

                    af_set_p_ws(body, len, &i);
                    if (i >= len || body[i] != ':')
                        return AF_SET_E_SYNTAX;
                    i++;
                    af_set_p_ws(body, len, &i);

                    uint8_t fval = 0;
                    rc = af_set_p_fld_flag(body, len, &i, &fval);
                    if (rc == AF_SET_E_BAD_VALUE) {
                        /* err_field = "store." + skey */
                        size_t el = 0;
                        const char *pre = "store.";
                        size_t pi = 0;
                        while (pre[pi] && el < AF_SET_FIELD_MAX - 1)
                            p->err_field[el++] = pre[pi++];
                        size_t si = 0;
                        while (skey[si] && el < AF_SET_FIELD_MAX - 1)
                            p->err_field[el++] = skey[si++];
                        p->err_field[el] = '\0';
                        return AF_SET_E_BAD_VALUE;
                    }
                    if (rc != AF_SET_OK)
                        return rc;

                    if (strcmp(skey, "raw") == 0) {
                        p->store_raw = fval;
                        p->has_raw = 1;
                    } else if (strcmp(skey, "agg") == 0) {
                        p->store_agg = fval;
                        p->has_agg = 1;
                    } else if (strcmp(skey, "env") == 0) {
                        p->store_env = fval;
                        p->has_env = 1;
                    } else {
                        /* Неизвестный ключ внутри store */
                        size_t el = 0;
                        const char *pre = "store.";
                        size_t pi = 0;
                        while (pre[pi] && el < AF_SET_FIELD_MAX - 1)
                            p->err_field[el++] = pre[pi++];
                        size_t si = 0;
                        while (skey[si] && el < AF_SET_FIELD_MAX - 1)
                            p->err_field[el++] = skey[si++];
                        p->err_field[el] = '\0';
                        return AF_SET_E_UNKNOWN_FIELD;
                    }

                    af_set_p_ws(body, len, &i);
                    if (i < len && body[i] == ',') {
                        i++;
                        af_set_p_ws(body, len, &i);
                        continue;
                    }
                    if (i < len && body[i] == '}') {
                        i++;
                        break;
                    }
                    return AF_SET_E_SYNTAX;
                }
            }

        } else if (strcmp(key, "window_s") == 0) {
            af_set_val_t v;
            rc = af_set_p_val(body, len, &i, &v);
            if (rc != AF_SET_OK && rc != AF_SET_E_BAD_VALUE)
                return rc;
            if (rc != AF_SET_OK || v.type != AF_SET_V_NUM || !af_settings_window_valid(v.num)) {
                const char *ef = "window_s";
                size_t el = 0;
                while (ef[el] && el < AF_SET_FIELD_MAX - 1)
                    { p->err_field[el] = ef[el]; el++; }
                p->err_field[el] = '\0';
                return AF_SET_E_BAD_VALUE;
            }
            p->window_s = (uint16_t)v.num;
            p->has_window = 1;

        } else if (strcmp(key, "sigma_k") == 0) {
            af_set_val_t v;
            rc = af_set_p_val(body, len, &i, &v);
            if (rc != AF_SET_OK && rc != AF_SET_E_BAD_VALUE)
                return rc;
            if (rc != AF_SET_OK || v.type != AF_SET_V_NUM || !af_settings_sigma_valid(v.num)) {
                const char *ef = "sigma_k";
                size_t el = 0;
                while (ef[el] && el < AF_SET_FIELD_MAX - 1)
                    { p->err_field[el] = ef[el]; el++; }
                p->err_field[el] = '\0';
                return AF_SET_E_BAD_VALUE;
            }
            p->sigma_k = (uint8_t)v.num;
            p->has_sigma = 1;

        } else if (strcmp(key, "ble_ext") == 0) {
            uint8_t fval = 0;
            rc = af_set_p_fld_flag(body, len, &i, &fval);
            if (rc == AF_SET_E_BAD_VALUE) {
                const char *ef = "ble_ext";
                size_t el = 0;
                while (ef[el] && el < AF_SET_FIELD_MAX - 1)
                    { p->err_field[el] = ef[el]; el++; }
                p->err_field[el] = '\0';
                return AF_SET_E_BAD_VALUE;
            }
            if (rc != AF_SET_OK)
                return rc;
            p->ble_ext = fval;
            p->has_ble_ext = 1;

        } else if (strcmp(key, "confirm_clear_agg") == 0) {
            uint8_t fval = 0;
            rc = af_set_p_fld_flag(body, len, &i, &fval);
            if (rc == AF_SET_E_BAD_VALUE) {
                const char *ef = "confirm_clear_agg";
                size_t el = 0;
                while (ef[el] && el < AF_SET_FIELD_MAX - 1)
                    { p->err_field[el] = ef[el]; el++; }
                p->err_field[el] = '\0';
                return AF_SET_E_BAD_VALUE;
            }
            if (rc != AF_SET_OK)
                return rc;
            p->confirm_clear_agg = fval;
            p->has_confirm = 1;

        } else {
            /* Неизвестное поле верхнего уровня */
            size_t el = 0;
            while (key[el] && el < AF_SET_FIELD_MAX - 1)
                { p->err_field[el] = key[el]; el++; }
            p->err_field[el] = '\0';
            return AF_SET_E_UNKNOWN_FIELD;
        }

        /* После пары — запятая (ещё пара) или '}' (конец) */
        af_set_p_ws(body, len, &i);
        if (i < len && body[i] == ',') {
            i++;
            af_set_p_ws(body, len, &i);
            continue;
        }
        if (i < len && body[i] == '}') {
            i++;
            af_set_p_ws(body, len, &i);
            if (i != len)
                return AF_SET_E_SYNTAX;
            return AF_SET_OK;
        }
        return AF_SET_E_SYNTAX;
    }
}

static inline int af_settings_apply(const af_settings_t *cur, const af_settings_patch_t *p,
                                    uint32_t agg_count, af_settings_t *out, uint32_t *agg_cleared)
{
    /* Проверка указателей */
    if (cur == NULL || p == NULL || out == NULL || agg_cleared == NULL)
        return AF_SET_E_ARG;

    /* Результат инициализируется как копия текущих настроек */
    *out = *cur;
    *agg_cleared = 0u;

    /* Валидация предложенного окна */
    if (p->has_window && !af_settings_window_valid(p->window_s))
        return AF_SET_E_BAD_VALUE;

    /* Валидация предложенного сигма-коэффициента */
    if (p->has_sigma && !af_settings_sigma_valid(p->sigma_k))
        return AF_SET_E_BAD_VALUE;

    /* Смена окна: новое значение отличается от текущего */
    int window_changed = (p->has_window && p->window_s != cur->window_s) ? 1 : 0;

    /* Смена окна при наличии агрегатов требует явного подтверждения */
    if (window_changed && agg_count > 0u && p->confirm_clear_agg == 0u) {
        /* Патч не применяется, состояние не меняется */
        *out = *cur;
        *agg_cleared = 0u;
        return AF_SET_NEED_CONFIRM;
    }

    /* Поле за полем применяем только те элементы, что отмечены в патче */
    if (p->has_raw)
        out->store_raw = p->store_raw;
    if (p->has_agg)
        out->store_agg = p->store_agg;
    if (p->has_env)
        out->store_env = p->store_env;
    if (p->has_window)
        out->window_s = p->window_s;
    if (p->has_sigma)
        out->sigma_k = p->sigma_k;
    if (p->has_ble_ext)
        out->ble_ext = p->ble_ext;

    /* Окно сменилось, агрегаты были — фиксируем их количество как очищенное */
    if (window_changed && agg_count > 0u)
        *agg_cleared = agg_count;

    return AF_SET_OK;
}

#endif
