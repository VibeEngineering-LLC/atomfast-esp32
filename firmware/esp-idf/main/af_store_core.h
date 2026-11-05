#ifndef AF_STORE_CORE_H
#define AF_STORE_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "af_parse.h"
#include "af_rec.h"
#include "af_ring.h"
#include "af_window.h"
#include "af_env.h"
#include "af_clear.h"

typedef struct {
    af_ring_t raw;
    af_ring_t agg;
    af_ring_t env;   /* e10: воздух; buf==NULL - кольцо не создано */
    af_window_t win;
    uint32_t samples;
    uint32_t ts0_samples;
    uint32_t win_rejected;
    uint32_t agg_closed;
    uint32_t gen;
    uint8_t en_raw, en_agg, en_env;   /* e16i П.2: 1 = вид пишется (по умолчанию), 0 = выключен пользователем */
} af_store_core_t;

static inline int af_core_init(af_store_core_t *c, uint8_t *raw_buf, size_t raw_bytes,
                               uint8_t *agg_buf, size_t agg_bytes,
                               uint32_t raw_limit, uint32_t agg_limit,
                               uint16_t period_s, uint8_t modes) {
    if (!c || !raw_buf || !agg_buf) return -1;
    memset(c, 0, sizeof(*c));
    c->en_raw = c->en_agg = c->en_env = 1;

    if (af_ring_init(&c->raw, raw_buf, raw_bytes, AF_REC_RAW_SIZE, 0, AF_RING_OVERWRITE) != 0) return -1;
    if (af_ring_init(&c->agg, agg_buf, agg_bytes, AF_REC_AGG_SIZE, 0, AF_RING_OVERWRITE) != 0) return -1;

    if (raw_limit > 0) {
        if (af_ring_set_limit(&c->raw, raw_limit) != 0) return -1;
    }
    if (agg_limit > 0) {
        if (af_ring_set_limit(&c->agg, agg_limit) != 0) return -1;
    }

    if (af_window_init(&c->win, period_s, modes) != 0) return -1;

    return 0;
}

/* e10: третье кольцо (env). Не создано (buf==NULL) - push и чтение безопасны и ничего не делают. */
static inline int af_core_env_init(af_store_core_t *c, uint8_t *buf, size_t bytes, uint32_t limit) {
    if (!c || !buf) return -1;
    if (af_ring_init(&c->env, buf, bytes, AF_REC_ENV_SIZE, 0, AF_RING_OVERWRITE) != 0) return -1;
    if (limit > 0 && af_ring_set_limit(&c->env, limit) != 0) return -1;
    return 0;
}

static inline int af_core_env_push(af_store_core_t *c, const af_env_rec_t *r) {
    if (!c || !r || !c->env.buf) return -1;
    if (!c->en_env) return 1;   /* выключен пользователем: запись не делается (не ошибка) */
    uint8_t b[AF_REC_ENV_SIZE];
    af_env_encode(r, b);
    return af_ring_push(&c->env, b) < 0 ? -1 : 0;
}

static inline void af_core_ingest(af_store_core_t *c, uint32_t ts, const af_sample_t *s) {
    if (!c || !s) return;

    c->samples++;

    uint8_t buf[AF_REC_RAW_SIZE];
    af_raw_rec_t r;
    r.ts = ts;
    r.s = *s;
    af_raw_encode(&r, buf);
    if (c->en_raw) af_ring_push(&c->raw, buf);

    if (ts == 0) {
        c->ts0_samples++;
        return;
    }

    if (!c->en_agg) {   /* агрегаты выключены: открытое окно сбрасывается, чтобы при включении не склеить старое */
        if (c->win.open) (void)af_window_init(&c->win, c->win.period_s, c->win.modes);
        return;
    }

    af_agg_rec_t a;
    int rc = af_window_add(&c->win, ts, s, &a);

    if (rc < 0) {
        c->win_rejected++;
    } else if (rc == 1) {
        uint8_t abuf[AF_REC_AGG_SIZE];
        af_agg_encode(&a, abuf);
        af_ring_push(&c->agg, abuf);
        c->agg_closed++;
    }
}

/* Смена окна агрегатов: проверка аргументов, инициализация временного окна, установка лимита agg, применение. */
static inline int af_core_set_window(af_store_core_t *c, uint16_t period_s, uint32_t agg_limit)
{
    if (c == NULL || agg_limit == 0 || agg_limit > c->agg.cap_max)
        return -1;

    af_window_t t;
    if (af_window_init(&t, period_s, c->win.modes) != 0)
        return -1;

    if (af_ring_set_limit(&c->agg, agg_limit) != 0)
        return -1;

    c->win = t;
    return 0;
}

static inline uint32_t af_core_read(const af_store_core_t *c, int kind, uint32_t since,
                                    void *out, uint32_t max, uint32_t *next, uint32_t *lost) {
    if (!c) return 0;

    if (next) *next = 0;
    if (lost) *lost = 0;

    if (kind == 0) {
        return af_ring_read_since(&c->raw, since, out, max, next, lost);
    } else if (kind == 1) {
        return af_ring_read_since(&c->agg, since, out, max, next, lost);
    } else if (kind == 2 && c->env.buf) {
        return af_ring_read_since(&c->env, since, out, max, next, lost);
    }

    return 0;
}

/* Сброс текущего окна агрегации в состояние нового окна (как af_window_init), без записи agg. */
static inline void af_core_win_reset(af_store_core_t *c) {
    if (!c) return;
    (void)af_window_init(&c->win, c->win.period_s, c->win.modes);
}

/* Ингест с проверкой поколения: если item_gen не совпадает с c->gen — не делать ничего. */
static inline int af_core_ingest_g(af_store_core_t *c, uint32_t item_gen, uint32_t ts, const af_sample_t *s) {
    if (!c || !s) return 0;
    if (item_gen != c->gen) return 0;
    af_core_ingest(c, ts, s);
    return 1;
}

/*
 * Смена устройства: bump gen → clear колец → сброс окна.
 * Порядок: (а) lock; gen++; unlock; (б) clear вне lock; (в) lock; win_reset; unlock.
 * Возвращает rc от clear_fn; шаг (в) выполняется всегда (при валидных аргументах).
 */
static inline int af_core_dev_switch_clear(af_store_core_t *c,
                                           int (*clear_fn)(uint32_t mask, void *ctx),
                                           void (*lock_fn)(void *ctx),
                                           void (*unlock_fn)(void *ctx),
                                           void *ctx) {
    if (!c || !clear_fn || !lock_fn || !unlock_fn) return -1;

    /* (а) bump generation under lock */
    lock_fn(ctx);
    c->gen++;
    unlock_fn(ctx);

    /* (б) clear rings (raw + agg) outside lock */
    int rc = clear_fn(AF_CLR_MASK_RAW | AF_CLR_MASK_AGG, ctx);

    /* (в) reset aggregation window under lock (always executed) */
    lock_fn(ctx);
    af_core_win_reset(c);
    unlock_fn(ctx);

    return rc;
}

#endif
