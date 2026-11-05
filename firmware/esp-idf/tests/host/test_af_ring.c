#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "af_test.h"
#include "af_parse.h"
#include "af_rec.h"
#include "af_ring.h"
#include "real_samples.h"

static void mk_rec(uint32_t i, uint8_t out[20]) {
    af_raw_rec_t r = {0};
    r.ts = 1760000000u + 2u * i;
    r.s.dose_msv = REAL[i % REAL_N].dose;
    r.s.doserate_usvh = REAL[i % REAL_N].rate;
    r.s.pulses_2s = REAL[i % REAL_N].p2s;
    r.s.cps = REAL[i % REAL_N].p2s / 2.0f;
    r.s.battery = REAL[i % REAL_N].bat;
    r.s.temp_c = REAL[i % REAL_N].temp;
    r.s.flags = 0;
    af_raw_encode(&r, out);
}

static int is_rec(const uint8_t *got, uint32_t i) {
    uint8_t ref[20];
    mk_rec(i, ref);
    return memcmp(got, ref, 20) == 0;
}

static void fill(af_ring_t *r, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint8_t rec[20];
        mk_rec(i, rec);
        af_ring_push(r, rec);
    }
}

static void t_init_errors(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    CHECK(af_ring_init(NULL, st, 100, 20, 7, AF_RING_OVERWRITE) == -1);
    CHECK(af_ring_init(&r, NULL, 100, 20, 7, AF_RING_OVERWRITE) == -1);
    CHECK(af_ring_init(&r, st, 100, 0, 7, AF_RING_OVERWRITE) == -1);
    CHECK(af_ring_init(&r, st, 100, 65, 7, AF_RING_OVERWRITE) == -1);
    CHECK(af_ring_init(&r, st, 19, 20, 7, AF_RING_OVERWRITE) == -1);
    CHECK(af_ring_init(&r, st, 100, 20, 7, (af_ring_policy_t)5) == -1);
    CHECK(af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE) == 0);
    CHECK(r.cap_max == 5);
    CHECK(r.limit == 5);
    CHECK(r.count == 0);
    CHECK(r.epoch == 7);
}

static void t_empty(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    CHECK(af_ring_count(&r) == 0);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == -1);
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 0, out, 5, &next, &lost) == 0);
    CHECK(next == 0);
    CHECK(lost == 0);
}

static void t_one(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    uint8_t rec[20];
    mk_rec(0, rec);
    CHECK(af_ring_push(&r, rec) == 0);
    CHECK(af_ring_count(&r) == 1);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 0));
}

static void t_exact_full(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    CHECK(af_ring_count(&r) == 5);
    uint8_t out[20];
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(af_ring_get(&r, i, out) == 0);
        CHECK(is_rec(out, i));
    }
    CHECK(af_ring_get(&r, 5, out) == -1);
}

static void t_wrap(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    uint8_t rec[20];
    for (uint32_t i = 5; i < 8; i++) {
        mk_rec(i, rec);
        CHECK(af_ring_push(&r, rec) == 1);
    }
    CHECK(af_ring_count(&r) == 5);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 3));
    CHECK(af_ring_get(&r, 4, out) == 0);
    CHECK(is_rec(out, 7));
    CHECK(af_ring_get(&r, 5, out) == -1);
}

static void t_wrap_many(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 23);
    uint8_t out[20];
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(af_ring_get(&r, i, out) == 0);
        CHECK(is_rec(out, 18 + i));
    }
}

static void t_dropped(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 8);
    CHECK(r.dropped == 3);
    af_ring_t r2 = {0};
    af_ring_init(&r2, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r2, 5);
    CHECK(r2.dropped == 0);
}

static void t_rejected(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_REJECT);
    uint8_t rec[20];
    for (uint32_t i = 0; i < 5; i++) {
        mk_rec(i, rec);
        CHECK(af_ring_push(&r, rec) == 0);
    }
    mk_rec(5, rec);
    CHECK(af_ring_push(&r, rec) == -1);
    mk_rec(6, rec);
    CHECK(af_ring_push(&r, rec) == -1);
    CHECK(r.rejected == 2);
    CHECK(r.dropped == 0);
}

static void t_reject_keeps_content(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_REJECT);
    fill(&r, 7);
    CHECK(af_ring_count(&r) == 5);
    uint8_t out[20];
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(af_ring_get(&r, i, out) == 0);
        CHECK(is_rec(out, i));
    }
}

static void t_push_null(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    uint8_t rec[20];
    mk_rec(0, rec);
    CHECK(af_ring_push(NULL, rec) == -2);
    CHECK(af_ring_push(&r, NULL) == -2);
}

static void t_seq(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    CHECK(r.seq == 5);
    CHECK(af_ring_oldest_seq(&r) == 0);
    fill(&r, 3);
    CHECK(r.seq == 8);
    CHECK(af_ring_oldest_seq(&r) == 3);
    
    af_ring_t r2 = {0};
    af_ring_init(&r2, st, 100, 20, 7, AF_RING_REJECT);
    fill(&r2, 7);
    CHECK(r2.seq == 5);
}

static void t_seq_wrap(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    r.seq = 0xFFFFFFFEu;
    fill(&r, 3);
    CHECK(r.seq == 1);
    CHECK(af_ring_count(&r) == 3);
    CHECK(af_ring_oldest_seq(&r) == 0xFFFFFFFEu);
}

static void t_read_since_all(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    uint8_t out[100];
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 0, out, 5, &next, &lost) == 5);
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(is_rec(out + i * 20, i));
    }
    CHECK(next == 5);
    CHECK(lost == 0);
}

static void t_read_since_mid(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    uint8_t out[100];
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 3, out, 5, &next, &lost) == 2);
    CHECK(is_rec(out, 3));
    CHECK(is_rec(out + 20, 4));
    CHECK(next == 5);
    CHECK(lost == 0);
    
    CHECK(af_ring_read_since(&r, 5, out, 5, &next, &lost) == 0);
    CHECK(next == 5);
    CHECK(lost == 0);
}

static void t_read_since_lost(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 8);
    uint8_t out[100];
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 0, out, 5, &next, &lost) == 5);
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(is_rec(out + i * 20, 3 + i));
    }
    CHECK(lost == 3);
    CHECK(next == 8);
    
    CHECK(af_ring_read_since(&r, 1, out, 5, &next, &lost) == 5);
    CHECK(lost == 2);
    
    CHECK(af_ring_read_since(&r, 3, out, 5, &next, &lost) == 5);
    CHECK(lost == 0);
}

static void t_read_since_future(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 8);
    uint8_t out[100];
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 13, out, 5, &next, &lost) == 0);
    CHECK(lost == 0);
    CHECK(next == 8);
    
    CHECK(af_ring_read_since(&r, 0xFFFFFFF0u, out, 5, &next, &lost) == 5);
    CHECK(lost == 19);
    CHECK(next == 8);
}

static void t_read_since_max(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    uint8_t out[100];
    uint32_t next = 0, lost = 0;
    CHECK(af_ring_read_since(&r, 0, out, 2, &next, &lost) == 2);
    CHECK(is_rec(out, 0));
    CHECK(is_rec(out + 20, 1));
    CHECK(next == 2);
    
    CHECK(af_ring_read_since(&r, next, out, 10, &next, &lost) == 3);
    CHECK(is_rec(out, 2));
    CHECK(is_rec(out + 20, 3));
    CHECK(is_rec(out + 40, 4));
    CHECK(next == 5);
    
    CHECK(af_ring_read_since(&r, 1, NULL, 0, &next, &lost) == 0);
    CHECK(next == 1);
    CHECK(lost == 0);
}

static void t_read_since_nulls(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    uint8_t out[100];
    CHECK(af_ring_read_since(&r, 0, out, 5, NULL, NULL) == 5);
}

static void t_clear(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    CHECK(r.seq == 5);
    af_ring_clear(&r);
    CHECK(af_ring_count(&r) == 0);
    CHECK(r.epoch == 8);
    CHECK(r.seq == 5);
    CHECK(r.limit == 5);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == -1);
    
    uint8_t rec[20];
    mk_rec(9, rec);
    af_ring_push(&r, rec);
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 9));
    CHECK(af_ring_oldest_seq(&r) == 5);
}

static void t_limit_shrink(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    CHECK(af_ring_set_limit(&r, 3) == 0);
    CHECK(af_ring_count(&r) == 3);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 2));
    CHECK(af_ring_get(&r, 2, out) == 0);
    CHECK(is_rec(out, 4));
    CHECK(r.dropped == 2);
    CHECK(r.limit == 3);
}

static void t_limit_errors(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 5);
    CHECK(af_ring_set_limit(&r, 0) == -1);
    CHECK(af_ring_set_limit(&r, 6) == -1);
    CHECK(r.limit == 5);
    CHECK(af_ring_count(&r) == 5);
}

static void t_limit_push_full(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    CHECK(af_ring_set_limit(&r, 3) == 0);
    fill(&r, 7);
    CHECK(af_ring_count(&r) == 3);
    uint8_t out[20];
    for (uint32_t i = 0; i < 3; i++) {
        CHECK(af_ring_get(&r, i, out) == 0);
        CHECK(is_rec(out, 4 + i));
    }
    uint8_t rec[20];
    mk_rec(7, rec);
    CHECK(af_ring_push(&r, rec) == 1);
}

static void t_limit_grow_after_wrap(void) {
    uint8_t st[100];
    af_ring_t r = {0};
    af_ring_init(&r, st, 100, 20, 7, AF_RING_OVERWRITE);
    fill(&r, 8);
    CHECK(af_ring_set_limit(&r, 3) == 0);
    uint8_t out[20];
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 5));
    CHECK(af_ring_get(&r, 1, out) == 0);
    CHECK(is_rec(out, 6));
    CHECK(af_ring_get(&r, 2, out) == 0);
    CHECK(is_rec(out, 7));
    
    CHECK(af_ring_set_limit(&r, 5) == 0);
    CHECK(af_ring_count(&r) == 3);
    CHECK(af_ring_get(&r, 0, out) == 0);
    CHECK(is_rec(out, 5));
    
    uint8_t rec[20];
    mk_rec(8, rec);
    af_ring_push(&r, rec);
    mk_rec(9, rec);
    af_ring_push(&r, rec);
    CHECK(af_ring_count(&r) == 5);
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(af_ring_get(&r, i, out) == 0);
        CHECK(is_rec(out, 5 + i));
    }
}

static void t_quota_example(void) {
    af_quota_t q = {0};
    CHECK(af_quota_split(12451840, 20, 28, 1, 1, 60, 1209600, &q) == 0);
    CHECK(q.agg_limit == 20160);
    CHECK(q.atomic_limit == 594368);
}

static void t_quota_modes(void) {
    af_quota_t q = {0};
    CHECK(af_quota_split(1000, 20, 28, 1, 0, 60, 600, &q) == 0);
    CHECK(q.atomic_limit == 50);
    CHECK(q.agg_limit == 0);
    
    CHECK(af_quota_split(1000, 20, 28, 0, 1, 60, 600, &q) == 0);
    CHECK(q.agg_limit == 10);
    CHECK(q.atomic_limit == 0);
    
    CHECK(af_quota_split(1000, 20, 28, 0, 0, 60, 600, &q) == 0);
    CHECK(q.agg_limit == 0);
    CHECK(q.atomic_limit == 0);
}

static void t_quota_agg_first(void) {
    af_quota_t q = {0};
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 60, 600, &q) == 0);
    CHECK(q.agg_limit == 10);
    CHECK(q.atomic_limit == 36);
    
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 60, 100000, &q) == 0);
    CHECK(q.agg_limit == 35);
    CHECK(q.atomic_limit == 1);
    
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 60, 601, &q) == 0);
    CHECK(q.agg_limit == 11);
}

static void t_quota_errors(void) {
    af_quota_t q = {0};
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 60, 600, NULL) == -1);
    CHECK(af_quota_split(1000, 0, 28, 1, 1, 60, 600, &q) == -1);
    CHECK(af_quota_split(1000, 20, 0, 1, 1, 60, 600, &q) == -1);
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 0, 600, &q) == -1);
    CHECK(af_quota_split(1000, 20, 28, 1, 1, 60, 0, &q) == -1);
    CHECK(af_quota_split(1000, 20, 28, 0, 0, 0, 0, &q) == 0);
}

int main(void) {
    RUN(t_init_errors);
    RUN(t_empty);
    RUN(t_one);
    RUN(t_exact_full);
    RUN(t_wrap);
    RUN(t_wrap_many);
    RUN(t_dropped);
    RUN(t_rejected);
    RUN(t_reject_keeps_content);
    RUN(t_push_null);
    RUN(t_seq);
    RUN(t_seq_wrap);
    RUN(t_read_since_all);
    RUN(t_read_since_mid);
    RUN(t_read_since_lost);
    RUN(t_read_since_future);
    RUN(t_read_since_max);
    RUN(t_read_since_nulls);
    RUN(t_clear);
    RUN(t_limit_shrink);
    RUN(t_limit_errors);
    RUN(t_limit_push_full);
    RUN(t_limit_grow_after_wrap);
    RUN(t_quota_example);
    RUN(t_quota_modes);
    RUN(t_quota_agg_first);
    RUN(t_quota_errors);
    TESTS_DONE();
}
