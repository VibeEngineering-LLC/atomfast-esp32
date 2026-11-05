#include "af_test.h"
#include "af_hist_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t TS[8192];
static uint32_t TSN;

static int load_ts(const char *path) {
    TSN = 0; /* каждая загрузка начинает с нуля */
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL load_ts %s\n", path);
        g_failures++;
        return 0;
    }
    char buf[512];
    int first = 1;
    while (fgets(buf, sizeof(buf), f)) {
        if (first) {
            first = 0;
            continue;
        }
        if (buf[0] == '\n' || buf[0] == '\r' || buf[0] == '\0') continue;
        if (TSN < 8192) {
            TS[TSN++] = (uint32_t)strtoul(buf, NULL, 10);
        }
    }
    fclose(f);
    if (TSN == 0) {
        printf("FAIL load_ts %s\n", path);
        g_failures++;
        return 0;
    }
    return (int)TSN;
}

static uint32_t lin_lb(const uint32_t *a, uint32_t n, uint32_t t) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != 0 && a[i] >= t) return i;
    }
    return n;
}

static void push_raw(af_store_core_t *c, uint32_t ts) {
    af_raw_rec_t r;
    memset(&r, 0, sizeof(r));
    r.ts = ts;
    r.s.dose_msv = 0.19f;
    r.s.doserate_usvh = 0.08f;
    r.s.pulses_2s = 30;
    r.s.battery = 27;
    r.s.temp_c = 26;
    uint8_t buf[20];
    af_raw_encode(&r, buf);
    af_ring_push(&c->raw, buf);
}

static void push_agg(af_store_core_t *c, uint32_t ts) {
    af_agg_rec_t a;
    memset(&a, 0, sizeof(a));
    a.ts = ts;
    a.period_s = 60;
    a.n = 30;
    a.dose_msv = 0.19f;
    a.doserate_usvh = 0.08f;
    a.pulses_2s = 30.0f;
    a.battery = 27;
    a.temp_c = 26;
    a.modes = AF_MODE_LAST_DOSE;
    uint8_t buf[28];
    af_agg_encode(&a, buf);
    af_ring_push(&c->agg, buf);
}

static void push_env(af_store_core_t *c, uint32_t ts) {
    af_env_rec_t e = {.ts = ts, .t_c100 = 2650, .p_pa = 100218, .flags = AF_ENV_F_T | AF_ENV_F_P};
    af_core_env_push(c, &e);
}

static uint8_t RAWB[20 * 8192];
static uint8_t AGGB[28 * 512];
static uint8_t ENVB[14 * 128];
static af_store_core_t C;

static void mk_core(uint32_t raw_recs, int with_env) {
    memset(&C, 0, sizeof(C));
    CHECK(af_core_init(&C, RAWB, 20 * raw_recs, AGGB, sizeof(AGGB), 0, 0, 60, AF_MODE_LAST_DOSE) == 0);
    if (with_env) {
        CHECK(af_core_env_init(&C, ENVB, sizeof(ENVB), 0) == 0);
    }
}

static void mkp(af_hq_params_t *p, const char *from, const char *to, const char *since, int kind) {
    af_hq_init(p);
    p->kind = kind;
    if (from != NULL) af_hq_set(p, "from", from);
    if (to != NULL) af_hq_set(p, "to", to);
    if (since != NULL) af_hq_set(p, "since", since);
}

static void t_ts_at_ok(void) {
    mk_core(64, 1);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < 100; i++) {
        push_raw(&C, TS[i]);
    }
    CHECK(C.raw.tail == 36);

    af_hc_ctx_t ctx = {&C, AF_HQ_KIND_RAW};
    uint32_t t;
    CHECK(af_hc_ts_at(&ctx, 35, &t) == -1);
    CHECK(af_hc_ts_at(&ctx, 36, &t) == 0);
    CHECK(t == TS[36]);
    CHECK(af_hc_ts_at(&ctx, 99, &t) == 0);
    CHECK(t == TS[99]);
    CHECK(af_hc_ts_at(&ctx, 100, &t) == -1);

    TSN = 0;
    n = load_ts("data/e9_pre_agg.csv");
    CHECK(n == 213);
    if (n != 213) return;

    for (uint32_t i = 0; i < 3; i++) {
        push_agg(&C, TS[i]);
    }
    ctx.kind = AF_HQ_KIND_AGG;
    CHECK(af_hc_ts_at(&ctx, 0, &t) == 0);
    CHECK(t == TS[0]);
    CHECK(af_hc_ts_at(&ctx, 1, &t) == 0);
    CHECK(t == TS[1]);
    CHECK(af_hc_ts_at(&ctx, 2, &t) == 0);
    CHECK(t == TS[2]);
    CHECK(af_hc_ts_at(&ctx, 3, &t) == -1);

    for (uint32_t i = 0; i < 5; i++) {
        push_env(&C, 1761300000u + 30u * i);
    }
    ctx.kind = AF_HQ_KIND_ENV;
    CHECK(af_hc_ts_at(&ctx, 4, &t) == 0);
    CHECK(t == 1761300120u);
    CHECK(af_hc_ts_at(&ctx, 5, &t) == -1);
}

static void t_ts_at_bad(void) {
    mk_core(64, 0);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < 100; i++) {
        push_raw(&C, TS[i]);
    }

    uint32_t idx = (C.raw.tail + (70 - 36)) % C.raw.cap_max;
    RAWB[idx * 20 + 5] ^= 0xFF;

    af_hc_ctx_t ctx = {&C, AF_HQ_KIND_RAW};
    uint32_t t;
    CHECK(af_hc_ts_at(&ctx, 70, &t) == -1);
    CHECK(af_hc_ts_at(&ctx, 69, &t) == 0);
    CHECK(t == TS[69]);
    CHECK(af_hc_ts_at(&ctx, 71, &t) == 0);
    CHECK(t == TS[71]);

    CHECK(af_hc_ts_at(NULL, 0, &t) == -1);
    CHECK(af_hc_ts_at(&ctx, 0, NULL) == -1);
    ctx.kind = 3;
    CHECK(af_hc_ts_at(&ctx, 0, &t) == -1);
    ctx.kind = AF_HQ_KIND_ENV;
    CHECK(af_hc_ts_at(&ctx, 0, &t) == -1);
    ctx.c = NULL;
    CHECK(af_hc_ts_at(&ctx, 0, &t) == -1);
}

static void t_plan_battle_raw(void) {
    mk_core(8192, 0);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < n; i++) {
        push_raw(&C, TS[i]);
    }
    C.raw.epoch = 7;

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    mkp(&p, "1792864800", "1792868400", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2482);
    CHECK(plan.end == 4282);
    CHECK(head == 6374);
    CHECK(epoch == 7);
    CHECK(plan.lost == 0);

    mkp(&p, "1792864800", NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2482);
    CHECK(plan.end == 6374);

    mkp(&p, NULL, "1792868400", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 0);
    CHECK(plan.end == 4282);

    mkp(&p, NULL, NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 0);
    CHECK(plan.end == 6374);

    mkp(&p, "1792859000", NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 0);
    CHECK(plan.end == 6374);

    mkp(&p, "1792873000", NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 6374);
    CHECK(plan.end == 6374);

    mkp(&p, NULL, "1792859805", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 0);
    CHECK(plan.end == 0);

    mkp(&p, "1792864800", "1792868400", "3000", AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 3000);
    CHECK(plan.end == 4282);

    mkp(&p, "1792864800", "1792868400", "2000", AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2482);
    CHECK(plan.end == 4282);

    mkp(&p, "1792864800", "1792868400", "5000", AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 4282);
    CHECK(plan.end == 4282);

    for (uint32_t i = 0; i < n; i += 17) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)TS[i]);
        mkp(&p, buf, NULL, NULL, AF_HQ_KIND_RAW);
        CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
        CHECK(plan.start == lin_lb(TS, n, TS[i]));
        CHECK(plan.end == 6374);
    }
}

static void t_plan_wrapped(void) {
    mk_core(4096, 0);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < n; i++) {
        push_raw(&C, TS[i]);
    }
    CHECK(C.raw.tail == 2278);
    CHECK(C.raw.count == 4096);

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    mkp(&p, "1792864800", "1792868400", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2482);
    CHECK(plan.end == 4282);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)TS[0]);
    mkp(&p, buf, NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2278);
    CHECK(plan.end == 6374);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)TS[100]);
    mkp(&p, NULL, buf, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2278);
    CHECK(plan.end == 2278);

    mkp(&p, NULL, NULL, "100", AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 2278);
    CHECK(plan.lost == 2178);
    CHECK(plan.end == 6374);
}

static void t_plan_agg(void) {
    mk_core(64, 0);
    uint32_t n = load_ts("data/e9_pre_agg.csv");
    CHECK(n == 213);
    if (n != 213) return;

    for (uint32_t i = 0; i < n; i++) {
        push_agg(&C, TS[i]);
    }

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    mkp(&p, "1792864800", "1792868400", NULL, AF_HQ_KIND_AGG);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 83);
    CHECK(plan.end == 143);
    CHECK(plan.end - plan.start == 60);
    CHECK(head == 213);
}

static void t_plan_env(void) {
    mk_core(64, 1);
    for (uint32_t i = 0; i < 100; i++) {
        push_env(&C, 1761300000u + 30u * i);
    }

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    mkp(&p, "1761300300", NULL, NULL, AF_HQ_KIND_ENV);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 10);
    CHECK(head == 100);

    mkp(&p, NULL, "1761300600", NULL, AF_HQ_KIND_ENV);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.end == 20);

    mkp(&p, "1761300300", "1761300600", NULL, AF_HQ_KIND_ENV);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start == 10);
    CHECK(plan.end == 20);

    mk_core(64, 0);
    mkp(&p, NULL, NULL, NULL, AF_HQ_KIND_ENV);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == -1);

    p.kind = 3;
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == -1);

    CHECK(af_core_plan(NULL, &p, &plan, &head, &epoch) == -1);
    CHECK(af_core_plan(&C, NULL, &plan, &head, &epoch) == -1);
    CHECK(af_core_plan(&C, &p, NULL, &head, &epoch) == -1);
}

static void t_plan_corrupt_early(void) {
    mk_core(8192, 0);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < n; i++) {
        push_raw(&C, TS[i]);
    }

    for (uint32_t i = 2400; i <= 2600; i++) {
        RAWB[i * 20 + 5] ^= 0xFF;
    }

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    mkp(&p, "1792864800", NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start <= 2482);

    mkp(&p, NULL, "1792868400", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.end == 4282);

    for (uint32_t i = 4200; i <= 4400; i++) {
        RAWB[i * 20 + 5] ^= 0xFF;
    }

    mkp(&p, NULL, "1792868400", NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.end >= 4282);
    CHECK(plan.end <= 6374);
}

static void t_plan_zero_ts(void) {
    mk_core(8192, 0);
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;

    for (uint32_t i = 0; i < 1000; i++) {
        push_raw(&C, TS[i]);
    }
    for (uint32_t i = 0; i < 30; i++) {
        push_raw(&C, 0);
    }
    for (uint32_t i = 1000; i < 3000; i++) {
        push_raw(&C, TS[i]);
    }

    af_hc_ctx_t ctx = {&C, AF_HQ_KIND_RAW};
    uint32_t t;
    CHECK(af_hc_ts_at(&ctx, 1000, &t) == 0);
    CHECK(t == 0);

    af_hq_params_t p;
    af_hq_plan_t plan;
    uint32_t head, epoch;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)TS[1500]);
    mkp(&p, buf, NULL, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.start <= 1530);
    CHECK(plan.start == 1530);

    mkp(&p, NULL, buf, NULL, AF_HQ_KIND_RAW);
    CHECK(af_core_plan(&C, &p, &plan, &head, &epoch) == 0);
    CHECK(plan.end >= 1530);
}

int main(void) {
    RUN(t_ts_at_ok);
    RUN(t_ts_at_bad);
    RUN(t_plan_battle_raw);
    RUN(t_plan_wrapped);
    RUN(t_plan_agg);
    RUN(t_plan_env);
    RUN(t_plan_corrupt_early);
    RUN(t_plan_zero_ts);
    TESTS_DONE();
}
