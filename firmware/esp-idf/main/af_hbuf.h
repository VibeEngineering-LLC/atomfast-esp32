#pragma once
#include <stddef.h>
#include <string.h>

typedef int (*af_hbuf_send_fn)(void *ctx, const char *p, size_t n);   /* 0 = успех, иное = ошибка */
typedef struct {
    char *buf;            /* внешний буфер; NULL или cap==0 -> режим «сквозной» (без накопления) */
    size_t cap;
    size_t n;             /* сколько байт накоплено */
    af_hbuf_send_fn send;
    void *ctx;
    int err;              /* липкая ошибка: после первой неудачи все вызовы возвращают -1 */
    unsigned sends;       /* сколько раз реально вызвали send (для тестов и диагностики) */
} af_hbuf_t;

static inline void af_hbuf_init(af_hbuf_t *h, char *buf, size_t cap, af_hbuf_send_fn send, void *ctx) {
    h->buf = buf;
    h->cap = cap;
    h->n = 0;
    h->send = send;
    h->ctx = ctx;
    h->err = 0;
    h->sends = 0;
}

static inline int af_hbuf_flush(af_hbuf_t *h) {
    if (h->err) return -1;
    if (h->n > 0) {
        int r = h->send(h->ctx, h->buf, h->n);
        h->sends++;
        h->n = 0;
        if (r != 0) {
            h->err = 1;
            return -1;
        }
    }
    return 0;
}

static inline int af_hbuf_add(af_hbuf_t *h, const char *s, size_t len) {
    if (h->err) return -1;
    if (len == 0 || s == NULL) return 0;

    /* Режим «сквозной»: без накопления */
    if (h->buf == NULL || h->cap == 0) {
        int r = h->send(h->ctx, s, len);
        h->sends++;
        if (r != 0) {
            h->err = 1;
            return -1;
        }
        return 0;
    }

    /* Фрагмент не влезает целиком даже в пустой буфер */
    if (len >= h->cap) {
        if (af_hbuf_flush(h) != 0) return -1;
        int r = h->send(h->ctx, s, len);
        h->sends++;
        if (r != 0) {
            h->err = 1;
            return -1;
        }
        return 0;
    }

    /* Буфер переполнится — сбрасываем накопленное */
    if (h->n + len > h->cap) {
        if (af_hbuf_flush(h) != 0) return -1;
    }

    memcpy(h->buf + h->n, s, len);
    h->n += len;
    return 0;
}

static inline int af_hbuf_adds(af_hbuf_t *h, const char *s) {
    return af_hbuf_add(h, s, s ? strlen(s) : 0);
}

static inline int af_hbuf_end(af_hbuf_t *h) {
    return af_hbuf_flush(h);
}
