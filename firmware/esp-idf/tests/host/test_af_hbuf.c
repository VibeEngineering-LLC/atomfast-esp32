#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "af_hbuf.h"

static int g_fail = 0;
static int g_n = 0;

#define CHECK(cond) do { \
    g_n++; \
    if (!(cond)) { \
        g_fail++; \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
    } \
} while (0)

typedef struct {
    char out[8192];
    size_t total;
    size_t max_chunk;
    unsigned calls;
    int fail_at;
} rec_t;

static int rec_send(void *ctx, const char *p, size_t n) {
    rec_t *r = (rec_t *)ctx;
    r->calls++;
    if (r->fail_at > 0 && (int)r->calls == r->fail_at) {
        return -1;
    }
    if (r->total + n > sizeof(r->out)) {
        return -1;
    }
    memcpy(r->out + r->total, p, n);
    r->total += n;
    if (n > r->max_chunk) {
        r->max_chunk = n;
    }
    return 0;
}

static uint32_t xorshift32(uint32_t *x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

int main(void) {
    /* Сценарий 1: Склейка */
    {
        char buf[64];
        af_hbuf_t h;
        rec_t r = {0};
        af_hbuf_init(&h, buf, 64, rec_send, &r);
        for (int i = 0; i < 10; i++) {
            CHECK(af_hbuf_adds(&h, "0123456789") == 0);
        }
        CHECK(af_hbuf_end(&h) == 0);
        char expected[100];
        for (int i = 0; i < 10; i++) memcpy(expected + i * 10, "0123456789", 10);
        CHECK(r.total == 100);
        CHECK(memcmp(r.out, expected, 100) == 0);
        CHECK(r.calls == 2);
        CHECK(r.max_chunk <= 64);
        CHECK(h.sends == r.calls);
    }

    /* Сценарий 2: Большой фрагмент */
    {
        char buf[16];
        af_hbuf_t h;
        rec_t r = {0};
        af_hbuf_init(&h, buf, 16, rec_send, &r);
        CHECK(af_hbuf_adds(&h, "AAAA") == 0);
        char big[40];
        memset(big, 'B', 40);
        CHECK(af_hbuf_add(&h, big, 40) == 0);
        CHECK(af_hbuf_adds(&h, "CC") == 0);
        CHECK(af_hbuf_end(&h) == 0);
        CHECK(r.total == 46);
        CHECK(memcmp(r.out, "AAAA", 4) == 0);
        for (int i = 0; i < 40; i++) CHECK(r.out[4 + i] == 'B');
        CHECK(memcmp(r.out + 44, "CC", 2) == 0);
        CHECK(r.calls == 3);
        CHECK(r.max_chunk == 40);
    }

    /* Сценарий 3: Сквозной режим */
    {
        af_hbuf_t h;
        rec_t r = {0};
        af_hbuf_init(&h, NULL, 0, rec_send, &r);
        CHECK(af_hbuf_adds(&h, "a") == 0);
        CHECK(af_hbuf_adds(&h, "bb") == 0);
        CHECK(af_hbuf_adds(&h, "ccc") == 0);
        CHECK(af_hbuf_adds(&h, "") == 0);
        CHECK(af_hbuf_add(&h, NULL, 0) == 0);
        CHECK(af_hbuf_end(&h) == 0);
        CHECK(r.calls == 3);
        CHECK(r.total == 6);
        CHECK(memcmp(r.out, "abbccc", 6) == 0);
    }

    /* Сценарий 4: Ошибка на середине */
    {
        char buf[8];
        af_hbuf_t h;
        rec_t r = {0};
        r.fail_at = 2;
        af_hbuf_init(&h, buf, 8, rec_send, &r);
        int ret = 0;
        for (int i = 0; i < 10; i++) {
            ret = af_hbuf_adds(&h, "12345");
            if (ret == -1) break;
        }
        CHECK(ret == -1);
        CHECK(h.err == 1);
        unsigned calls_before = r.calls;
        CHECK(af_hbuf_adds(&h, "x") == -1);
        CHECK(af_hbuf_flush(&h) == -1);
        CHECK(af_hbuf_end(&h) == -1);
        CHECK(r.calls == calls_before);
    }

    /* Сценарий 5: Пустой end */
    {
        char buf[16];
        af_hbuf_t h;
        rec_t r = {0};
        af_hbuf_init(&h, buf, 16, rec_send, &r);
        CHECK(af_hbuf_end(&h) == 0);
        CHECK(r.calls == 0);
    }

    /* Сценарий 6: Ровно по границе */
    {
        char buf[10];
        af_hbuf_t h;
        rec_t r = {0};
        af_hbuf_init(&h, buf, 10, rec_send, &r);
        char ten[10];
        memset(ten, 'A', 10);
        CHECK(af_hbuf_add(&h, ten, 10) == 0);
        CHECK(r.calls == 1);
        char four[4] = "BBBB";
        CHECK(af_hbuf_add(&h, four, 4) == 0);
        char six[6] = "CCCCCC";
        CHECK(af_hbuf_add(&h, six, 6) == 0);
        CHECK(r.calls == 1);
        CHECK(af_hbuf_end(&h) == 0);
        CHECK(r.calls == 2);
        CHECK(r.total == 20);
        for (int i = 0; i < 10; i++) CHECK(r.out[i] == 'A');
        for (int i = 0; i < 4; i++) CHECK(r.out[10 + i] == 'B');
        for (int i = 0; i < 6; i++) CHECK(r.out[14 + i] == 'C');
    }

    /* Сценарий 7: Случайная сверка */
    {
        uint32_t seed = 12345;
        int caps[] = {0, 1, 7, 64, 512};
        for (int run = 0; run < 200; run++) {
            int cap = caps[xorshift32(&seed) % 5];
            int nfrags = 1 + (int)(xorshift32(&seed) % 30);
            char buf[512];
            af_hbuf_t h;
            rec_t r = {0};
            af_hbuf_init(&h, cap > 0 ? buf : NULL, cap, rec_send, &r);
            char expected[3000];
            size_t exp_len = 0;
            size_t max_frag = 0;
            for (int i = 0; i < nfrags; i++) {
                int len = (int)(xorshift32(&seed) % 101);
                if ((size_t)len > max_frag) max_frag = (size_t)len;
                for (int j = 0; j < len; j++) {
                    char c = 'a' + (char)(xorshift32(&seed) % 26);
                    if (exp_len + 1 > sizeof(expected)) {
                        CHECK(0);
                        break;
                    }
                    expected[exp_len++] = c;
                }
                if (len > 0) {
                    CHECK(af_hbuf_add(&h, expected + exp_len - len, len) == 0);
                }
            }
            CHECK(af_hbuf_end(&h) == 0);
            CHECK(r.total == exp_len);
            if (exp_len > 0) {
                CHECK(memcmp(r.out, expected, exp_len) == 0);
            }
            size_t limit = (size_t)cap > max_frag ? (size_t)cap : max_frag;
            if (limit == 0) limit = 1;
            CHECK(r.max_chunk <= limit);
        }
    }

    printf("TESTS %d FAIL %d\n", g_n, g_fail);
    return g_fail == 0 ? 0 : 1;
}
