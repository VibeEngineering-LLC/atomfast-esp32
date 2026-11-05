#include "af_test.h"
#include "af_hist_query.h"
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
        char *comma = strchr(buf, ',');
        if (comma) *comma = '\0';
        if (buf[0] == '\0') continue;
        unsigned long v = strtoul(buf, NULL, 10);
        if (TSN < 8192) {
            TS[TSN++] = (uint32_t)v;
        }
    }
    fclose(f);
    if (TSN == 0) {
        printf("FAIL load_ts %s\n", path);
        g_failures++;
        return 0;
    }
    return TSN;
}

typedef struct {
    const uint32_t *a;
    uint32_t n;
    uint32_t base;
    uint32_t calls;
    const unsigned char *bad;
} arr_ctx;

static int arr_ts(void *ctx, uint32_t seq, uint32_t *ts) {
    arr_ctx *c = (arr_ctx *)ctx;
    c->calls++;
    uint32_t idx = seq - c->base;
    if (idx >= c->n) return -1;
    if (c->bad != NULL && c->bad[idx] != 0) return -1;
    *ts = c->a[idx];
    return 0;
}

static uint32_t lin_lb(const uint32_t *a, uint32_t n, uint32_t t) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != 0 && a[i] >= t) return i;
    }
    return n;
}

static void t_parse_ok(void) {
    uint32_t out = 0;
    CHECK(af_hq_parse_u32("0", &out) == 0 && out == 0);
    CHECK(af_hq_parse_u32("7", &out) == 0 && out == 7);
    CHECK(af_hq_parse_u32("007", &out) == 0 && out == 7);
    CHECK(af_hq_parse_u32("0000000007", &out) == 0 && out == 7);
    CHECK(af_hq_parse_u32("4294967295", &out) == 0 && out == 4294967295u);
    CHECK(af_hq_parse_u32("1792864800", &out) == 0 && out == 1792864800u);
}

static void t_parse_bad(void) {
    const char *bads[] = {"", "-1", "+5", " 1", "1 ", "0x10", "4294967296", "99999999999", "00000000007", "12a", "1.5"};
    for (size_t i = 0; i < sizeof(bads)/sizeof(bads[0]); i++) {
        uint32_t out = 123;
        CHECK(af_hq_parse_u32(bads[i], &out) == -1 && out == 123);
    }
    uint32_t out = 123;
    CHECK(af_hq_parse_u32(NULL, &out) == -1 && out == 123);
    CHECK(af_hq_parse_u32("1", NULL) == -1);
}

static void t_set_kind(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(af_hq_set(&p, "kind", "raw") == 0 && p.kind == AF_HQ_KIND_RAW);
    CHECK(af_hq_set(&p, "kind", "agg") == 0 && p.kind == AF_HQ_KIND_AGG);
    CHECK(af_hq_set(&p, "kind", "env") == 0 && p.kind == AF_HQ_KIND_ENV);
    CHECK(af_hq_set(&p, "kind", "bogus") == -1 && p.kind == AF_HQ_KIND_ENV);
    CHECK(af_hq_set(&p, "kind", "RAW") == -1 && p.kind == AF_HQ_KIND_ENV);
    CHECK(af_hq_set(&p, "kind", "") == -1 && p.kind == AF_HQ_KIND_ENV);
}

static void t_set_since_limit(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(af_hq_set(&p, "since", "5") == 0 && p.since == 5 && p.has_since == 1);
    CHECK(af_hq_set(&p, "since", "x") == -1 && p.since == 5);
    
    af_hq_init(&p);
    CHECK(af_hq_set(&p, "since", "-1") == -1 && p.has_since == 0);
    
    af_hq_init(&p);
    CHECK(af_hq_set(&p, "limit", "0") == 0 && p.limit == AF_HQ_LIMIT_MAX);
    CHECK(af_hq_set(&p, "limit", "200001") == 0 && p.limit == AF_HQ_LIMIT_MAX);
    CHECK(af_hq_set(&p, "limit", "200000") == 0 && p.limit == 200000u);
    CHECK(af_hq_set(&p, "limit", "17") == 0 && p.limit == 17u);
    CHECK(af_hq_set(&p, "limit", "-3") == -1 && p.limit == 17u);
    CHECK(af_hq_set(&p, "limit", "4294967296") == -1 && p.limit == 17u);
}

static void t_set_from_to_ext(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(p.kind == 0 && p.since == 0 && p.has_since == 0 && p.limit == AF_HQ_LIMIT_MAX && p.from == 0 && p.has_from == 0 && p.to == 0 && p.has_to == 0 && p.ext == 0);
    
    CHECK(af_hq_set(&p, "from", "1792864800") == 0 && p.from == 1792864800u && p.has_from == 1 && p.has_to == 0);
    CHECK(af_hq_set(&p, "to", "1792868400") == 0 && p.to == 1792868400u && p.has_to == 1);
    CHECK(af_hq_set(&p, "from", "abc") == -1 && p.from == 1792864800u);
    CHECK(af_hq_set(&p, "to", "") == -1 && p.to == 1792868400u);
    
    CHECK(af_hq_set(&p, "ext", "1") == 0 && p.ext == 1);
    CHECK(af_hq_set(&p, "ext", "0") == 0 && p.ext == 0);
    CHECK(af_hq_set(&p, "ext", "2") == -1 && p.ext == 0);
    CHECK(af_hq_set(&p, "ext", "yes") == -1 && p.ext == 0);
}

static void t_set_unknown(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(af_hq_set(&p, "foo", "1") == 1);
    CHECK(p.kind == 0 && p.since == 0 && p.has_since == 0 && p.limit == AF_HQ_LIMIT_MAX && p.from == 0 && p.has_from == 0 && p.to == 0 && p.has_to == 0 && p.ext == 0);
    
    CHECK(af_hq_set(&p, "KIND", "raw") == 1);
    CHECK(af_hq_set(NULL, "kind", "raw") == -1);
    CHECK(af_hq_set(&p, NULL, "raw") == -1);
    CHECK(af_hq_set(&p, "kind", NULL) == -1);
}

static void t_check(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    af_hq_set(&p, "from", "10");
    af_hq_set(&p, "to", "5");
    CHECK(af_hq_check(&p) == -1);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "5");
    af_hq_set(&p, "to", "5");
    CHECK(af_hq_check(&p) == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "10");
    CHECK(af_hq_check(&p) == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "to", "10");
    CHECK(af_hq_check(&p) == 0);
    
    af_hq_init(&p);
    CHECK(af_hq_check(&p) == 0);
    
    CHECK(af_hq_check(NULL) == -1);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "0");
    af_hq_set(&p, "to", "0");
    CHECK(af_hq_check(&p) == 0);
}

static void t_time_filtered_match(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(af_hq_time_filtered(&p) == 0);
    CHECK(af_hq_ts_match(&p, 0) == 1);
    CHECK(af_hq_ts_match(&p, 5) == 1);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "100");
    af_hq_set(&p, "to", "200");
    CHECK(af_hq_time_filtered(&p) == 1);
    CHECK(af_hq_ts_match(&p, 100) == 1);
    CHECK(af_hq_ts_match(&p, 99) == 0);
    CHECK(af_hq_ts_match(&p, 101) == 1);
    CHECK(af_hq_ts_match(&p, 199) == 1);
    CHECK(af_hq_ts_match(&p, 200) == 0);
    CHECK(af_hq_ts_match(&p, 201) == 0);
    CHECK(af_hq_ts_match(&p, 0) == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "100");
    CHECK(af_hq_ts_match(&p, 0) == 0);
    CHECK(af_hq_ts_match(&p, 99) == 0);
    CHECK(af_hq_ts_match(&p, 100) == 1);
    CHECK(af_hq_ts_match(&p, 4294967295u) == 1);
    
    af_hq_init(&p);
    af_hq_set(&p, "to", "200");
    CHECK(af_hq_ts_match(&p, 0) == 0);
    CHECK(af_hq_ts_match(&p, 1) == 1);
    CHECK(af_hq_ts_match(&p, 199) == 1);
    CHECK(af_hq_ts_match(&p, 200) == 0);
    
    CHECK(af_hq_ts_match(NULL, 0) == 1);
    CHECK(af_hq_time_filtered(NULL) == 0);
}

static void t_lb_battle_raw(void) {
    TSN = 0;
    int n = load_ts("data/e9_pre_raw.csv");
    CHECK(n == 6374);
    if (n != 6374) return;
    
    uint32_t bases[2] = {100000u, 4294967000u};
    for (int b = 0; b < 2; b++) {
        uint32_t oldest = bases[b];
        uint32_t head = oldest + n;
        arr_ctx ctx = {TS, (uint32_t)n, oldest, 0, NULL};
        
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792864800u, 0) == oldest + 2482);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792868400u, 0) == oldest + 4282);
        
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, TS[0]-1, 0) == oldest);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, TS[0], 0) == oldest);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, TS[n-1], 0) == oldest + 6373);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, TS[n-1] + 1, 0) == head);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 0, 0) == oldest);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 4294967295u, 0) == head);
        
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792866212u, 0) == oldest + 3187);
        
        for (uint32_t i = 0; i < (uint32_t)n; i++) {
            uint32_t ts_val = TS[i];
            uint32_t tests[3] = {ts_val - 1, ts_val, ts_val + 1};
            for (int k = 0; k < 3; k++) {
                uint32_t t = tests[k];
                uint32_t expected = oldest + lin_lb(TS, (uint32_t)n, t);
                CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t, 0) == expected);
            }
        }
        
        ctx.calls = 0;
        af_hq_lower_bound(arr_ts, &ctx, oldest, head, TS[3187], 0);
        CHECK(ctx.calls <= 16);
    }
}

static void t_lb_battle_agg(void) {
    TSN = 0;
    int n = load_ts("data/e9_pre_agg.csv");
    CHECK(n == 213);
    if (n != 213) return;
    
    uint32_t oldest = 5000u;
    uint32_t head = oldest + n;
    arr_ctx ctx = {TS, (uint32_t)n, oldest, 0, NULL};
    
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792864800u, 0) == oldest + 83);
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792868400u, 0) == oldest + 143);
    
    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        uint32_t ts_val = TS[i];
        uint32_t tests[3] = {ts_val - 1, ts_val, ts_val + 1};
        for (int k = 0; k < 3; k++) {
            uint32_t t = tests[k];
            uint32_t expected = oldest + lin_lb(TS, (uint32_t)n, t);
            CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t, 0) == expected);
        }
    }
}

static void t_lb_zeros(void) {
    uint32_t a[1000];
    for (int i = 0; i < 1000; i++) {
        a[i] = 1792000000u + 2u * (uint32_t)i;
        if (i >= 300 && i <= 399) a[i] = 0;
    }
    
    uint32_t oldest = 7000u;
    uint32_t head = 8000u;
    arr_ctx ctx = {a, 1000, oldest, 0, NULL};
    
    for (int i = 0; i < 1000; i++) {
        if (i >= 300 && i <= 399) continue;
        uint32_t t1 = a[i];
        uint32_t t2 = a[i] + 1;
        uint32_t e1 = lin_lb(a, 1000, t1);
        uint32_t e2 = lin_lb(a, 1000, t2);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t1, 0) <= oldest + e1);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t2, 0) <= oldest + e2);
    }
    
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, a[10], 0) == oldest + 10);
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, a[900], 0) == oldest + 900);
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, a[450], 0) == oldest + 450);
    
    uint32_t z[50];
    for (int i = 0; i < 50; i++) z[i] = 0;
    arr_ctx ctx_z = {z, 50, 0, 0, NULL};
    CHECK(af_hq_lower_bound(arr_ts, &ctx_z, 0, 50, 1792000000u, 0) == 0);
    
    uint32_t a2[1000];
    for (int i = 0; i < 1000; i++) {
        a2[i] = 1792000000u + 2u * (uint32_t)i;
        if (i >= 300 && i <= 499) a2[i] = 0;
    }
    arr_ctx ctx2 = {a2, 1000, 7000, 0, NULL};
    uint32_t e800 = lin_lb(a2, 1000, a2[800]);
    uint32_t e100 = lin_lb(a2, 1000, a2[100]);
    CHECK(af_hq_lower_bound(arr_ts, &ctx2, 7000, 8000, a2[800], 0) <= 7000 + e800);
    CHECK(af_hq_lower_bound(arr_ts, &ctx2, 7000, 8000, a2[100], 0) <= 7000 + e100);
}

static void t_lb_cb_fail(void) {
    uint32_t a[1000];
    unsigned char bad[1000];
    for (int i = 0; i < 1000; i++) {
        a[i] = 1792000000u + 2u * (uint32_t)i;
        bad[i] = (i >= 200 && i <= 260) ? 1 : 0;
    }
    
    uint32_t oldest = 0;
    uint32_t head = 1000;
    arr_ctx ctx = {a, 1000, oldest, 0, bad};
    
    for (int i = 0; i < 1000; i++) {
        if (i >= 200 && i <= 260) continue;
        uint32_t t1 = a[i];
        uint32_t t2 = a[i] + 1;
        
        uint32_t e1 = 1000;
        for (int j = 0; j < 1000; j++) {
            if (j >= 200 && j <= 260) continue;
            if (a[j] >= t1) { e1 = j; break; }
        }
        uint32_t e2 = 1000;
        for (int j = 0; j < 1000; j++) {
            if (j >= 200 && j <= 260) continue;
            if (a[j] >= t2) { e2 = j; break; }
        }
        
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t1, 0) <= oldest + e1);
        CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, t2, 0) <= oldest + e2);
    }
}

static void t_lb_edges(void) {
    uint32_t oldest = 10;
    CHECK(af_hq_lower_bound(NULL, NULL, oldest, 20, 100, 0) == oldest);
    
    arr_ctx ctx = {NULL, 0, 500, 0, NULL};
    CHECK(af_hq_lower_bound(arr_ts, &ctx, 500, 500, 100, 0) == 500);
    CHECK(ctx.calls == 0);
    
    uint32_t a[1] = {1792000000u};
    arr_ctx ctx1 = {a, 1, 0, 0, NULL};
    CHECK(af_hq_lower_bound(arr_ts, &ctx1, 0, 1, 1792000000u, 0) == 0);
    CHECK(af_hq_lower_bound(arr_ts, &ctx1, 0, 1, 1792000001u, 0) == 1);
}

static void t_plan_since(void) {
    af_hq_params_t p;
    af_hq_plan_t out;
    uint32_t oldest = 1000, head = 1500;
    
    af_hq_init(&p);
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1000 && out.end == 1500 && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "1200");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1200 && out.end == 1500 && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "1000");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1000 && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "900");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1000 && out.lost == 100);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "0");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1000 && out.lost == 1000);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "1500");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1500 && out.end == 1500 && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "1600");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    CHECK(out.start == 1500 && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "4000000000");
    af_hq_plan(&p, oldest, head, 1000, 1500, &out);
    /* модульная арифметика: 1500 - 4000000000 = 294968796 (< 2^31) -> since "старше oldest", lost = 294968796 - 500 */
    CHECK(out.start == 1000 && out.lost == 294968296u);
}

static void t_plan_time(void) {
    af_hq_params_t p;
    af_hq_plan_t out;
    uint32_t oldest = 1000, head = 1500;
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1300");
    af_hq_plan(&p, oldest, head, 1300, 1500, &out);
    CHECK(out.start == 1300 && out.end == 1500);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1300");
    af_hq_set(&p, "since", "1400");
    af_hq_plan(&p, oldest, head, 1300, 1500, &out);
    CHECK(out.start == 1400);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1300");
    af_hq_set(&p, "since", "1200");
    af_hq_plan(&p, oldest, head, 1300, 1500, &out);
    CHECK(out.start == 1300);
    
    af_hq_init(&p);
    af_hq_set(&p, "to", "1450");
    af_hq_plan(&p, oldest, head, 1000, 1450, &out);
    CHECK(out.start == 1000 && out.end == 1450);
    
    af_hq_init(&p);
    af_hq_set(&p, "to", "1700");
    af_hq_plan(&p, oldest, head, 1000, 1700, &out);
    CHECK(out.end == 1500);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1300");
    af_hq_set(&p, "to", "1450");
    af_hq_plan(&p, oldest, head, 1300, 1450, &out);
    CHECK(out.start == 1300 && out.end == 1450);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1300");
    af_hq_set(&p, "to", "1100");
    af_hq_plan(&p, oldest, head, 1300, 1100, &out);
    CHECK(out.start == 1100 && out.end == 1100);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "1500");
    af_hq_set(&p, "to", "1500");
    af_hq_plan(&p, oldest, head, 1500, 1500, &out);
    CHECK(out.start == 1500 && out.end == 1500);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "900");
    af_hq_set(&p, "from", "1300");
    af_hq_plan(&p, oldest, head, 1300, 1500, &out);
    CHECK(out.start == 1300 && out.lost == 100);
}

static void t_plan_wrap(void) {
    af_hq_params_t p;
    af_hq_plan_t out;
    uint32_t oldest = 0xFFFFFF00u;
    uint32_t head = 0x00000100u;
    
    af_hq_init(&p);
    af_hq_plan(&p, oldest, head, 0, 0, &out);
    CHECK(out.start == 0xFFFFFF00u && out.end == 0x100u);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "4294967280");
    af_hq_plan(&p, oldest, head, 0, 0, &out);
    CHECK(out.start == 0xFFFFFFF0u && out.lost == 0);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "4294966784"); /* 0xFFFFFE00 */
    af_hq_plan(&p, oldest, head, 0, 0, &out);
    CHECK(out.start == 0xFFFFFF00u && out.lost == 256);
    
    af_hq_init(&p);
    af_hq_set(&p, "since", "80");
    af_hq_plan(&p, oldest, head, 0, 0, &out);
    CHECK(out.start == 0x50);
    
    af_hq_init(&p);
    af_hq_set(&p, "from", "16");
    af_hq_plan(&p, oldest, head, 0x10, 0, &out);
    CHECK(out.start == 0x10);
    
    af_hq_init(&p);
    af_hq_set(&p, "to", "4294967295");
    af_hq_plan(&p, oldest, head, 0, 0xFFFFFFFFu, &out);
    CHECK(out.end == 0xFFFFFFFFu);
}

static void t_plan_null(void) {
    af_hq_plan_t out;
    memset(&out, 0xAA, sizeof(out));
    af_hq_plan(NULL, 0, 10, 0, 10, &out);
    CHECK(out.start == 0xAAAAAAAA && out.end == 0xAAAAAAAA && out.lost == 0xAAAAAAAA);
    
    af_hq_params_t p;
    af_hq_init(&p);
    af_hq_plan(&p, 0, 10, 0, 10, NULL);
}

static void t_plan_from_outside(void) {
    af_hq_params_t p;
    af_hq_plan_t out;
    af_hq_init(&p);
    af_hq_set(&p, "from", "1");
    af_hq_plan(&p, 1000, 1500, 9999, 1500, &out); /* from_seq за кольцом: защитная ветка */
    CHECK(out.start == 1500 && out.end == 1500);
}

static void t_next_n(void) {
    CHECK(af_hq_next_n(10, 100, 1000, 32) == 32);
    CHECK(af_hq_next_n(90, 100, 1000, 32) == 10);
    CHECK(af_hq_next_n(10, 100, 5, 32) == 5);
    CHECK(af_hq_next_n(100, 100, 1000, 32) == 0);
    CHECK(af_hq_next_n(101, 100, 1000, 32) == 0);
    CHECK(af_hq_next_n(10, 100, 0, 32) == 0);
    CHECK(af_hq_next_n(0xFFFFFFF0u, 0x10u, 1000, 64) == 32);
    CHECK(af_hq_next_n(0xFFFFFFF0u, 0x10u, 1000, 16) == 16);
    CHECK(af_hq_next_n(0x10u, 0xFFFFFFF0u, 1000, 32) == 0);
}

static void t_mac(void) {
    uint8_t a[6] = {0x12, 0x34, 0xAB, 0xCD, 0xEF, 0x01};
    char buf[18];
    CHECK(af_hq_fmt_mac(a, buf, 18) == 17 && strcmp(buf, "12:34:ab:cd:ef:01") == 0);
    
    char buf17[17];
    CHECK(af_hq_fmt_mac(a, buf17, 17) == -1 && buf17[0] == 0);
    
    char buf0[1];
    CHECK(af_hq_fmt_mac(a, buf0, 0) == -1);
    
    CHECK(af_hq_fmt_mac(NULL, buf, 18) == -1);
    CHECK(af_hq_fmt_mac(a, NULL, 18) == -1);
    
    uint8_t z[6] = {0, 0, 0, 0, 0, 0xFF};
    CHECK(af_hq_fmt_mac(z, buf, 18) == 17 && strcmp(buf, "00:00:00:00:00:ff") == 0);
}

static void t_hdrs(void) {
    af_hq_hdrs_t h;
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 6374, 12, 2482, 0, "12:34:ab:cd:ef:01");
    CHECK(strcmp(h.kind, "raw") == 0);
    CHECK(strcmp(h.seq, "6374") == 0);
    CHECK(strcmp(h.epoch, "12") == 0);
    CHECK(strcmp(h.first_seq, "2482") == 0);
    CHECK(strcmp(h.lost, "0") == 0);
    CHECK(strcmp(h.device, "12:34:ab:cd:ef:01") == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_AGG, 0, 0, 0, 0, NULL);
    CHECK(strcmp(h.kind, "agg") == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_ENV, 0, 0, 0, 0, NULL);
    CHECK(strcmp(h.kind, "env") == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, 7, 0, 0, 0, 0, NULL);
    CHECK(strcmp(h.kind, "raw") == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 4294967295u, 4294967295u, 4294967295u, 4294967295u, NULL);
    CHECK(strcmp(h.seq, "4294967295") == 0);
    CHECK(strcmp(h.epoch, "4294967295") == 0);
    CHECK(strcmp(h.first_seq, "4294967295") == 0);
    CHECK(strcmp(h.lost, "4294967295") == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 0, 0, 0, 0, NULL);
    CHECK(h.device[0] == 0);
    
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 0, 0, 0, 0, "");
    CHECK(h.device[0] == 0);
    
    char mac18[19];
    memset(mac18, 'a', 18);
    mac18[18] = 0;
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 0, 0, 0, 0, mac18);
    CHECK(h.device[0] == 0);
    
    char mac17[18];
    memset(mac17, 'a', 17);
    mac17[17] = 0;
    memset(&h, 0, sizeof(h));
    af_hq_hdrs_fill(&h, AF_HQ_KIND_RAW, 0, 0, 0, 0, mac17);
    CHECK(strcmp(h.device, mac17) == 0);
    
    af_hq_hdrs_fill(NULL, AF_HQ_KIND_RAW, 0, 0, 0, 0, NULL);
}

static void t_ext(void) {
    char buf[64];
    const char *base = "ts,kind,x\n";
    const char *expected = "mac,seq,epoch,ts,kind,x\n";
    int len = (int)strlen(expected);
    
    CHECK(af_hq_ext_header(base, buf, 64) == len && strcmp(buf, expected) == 0);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_header(base, buf, len) == -1 && buf[0] == 0);
    
    CHECK(af_hq_ext_header(base, buf, len + 1) == len && strcmp(buf, expected) == 0);
    
    CHECK(af_hq_ext_header(NULL, buf, 64) == -1);
    CHECK(af_hq_ext_header(base, buf, 0) == -1);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_prefix("12:34:ab:cd:ef:01", 2482, 12, buf, 64) == 26 && strcmp(buf, "12:34:ab:cd:ef:01,2482,12,") == 0);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_prefix(NULL, 2482, 12, buf, 64) == 9 && strcmp(buf, ",2482,12,") == 0);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_prefix("", 2482, 12, buf, 64) == 9 && strcmp(buf, ",2482,12,") == 0);
    
    CHECK(af_hq_ext_prefix("a,b", 0, 0, buf, 64) == -2);
    CHECK(af_hq_ext_prefix("a\"b", 0, 0, buf, 64) == -2);
    CHECK(af_hq_ext_prefix("a\nb", 0, 0, buf, 64) == -2);
    CHECK(af_hq_ext_prefix("a\rb", 0, 0, buf, 64) == -2);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_prefix("m", 4294967295u, 4294967295u, buf, 64) == 24 && strcmp(buf, "m,4294967295,4294967295,") == 0);
    
    memset(buf, 0, 64);
    CHECK(af_hq_ext_prefix("12:34:ab:cd:ef:01", 2482, 12, buf, 8) == -1 && buf[0] == 0);
    
    CHECK(af_hq_ext_prefix("12:34:ab:cd:ef:01", 2482, 12, buf, 0) == -1);
    CHECK(af_hq_ext_prefix("12:34:ab:cd:ef:01", 2482, 12, NULL, 64) == -1);
    
    FILE *f = fopen("data/e9_pre_raw.csv", "r");
    if (f) {
        char line[512];
        CHECK(fgets(line, 512, f) != NULL);
        CHECK(fgets(line, 512, f) != NULL);
        fclose(f);
        size_t line_len = strlen(line);
        if (line_len >= 2 && strcmp(line + line_len - 2, "\r\n") == 0) {
            line[line_len - 2] = '\n';
            line[line_len - 1] = '\0';
        }
        
        char prefix[64];
        af_hq_ext_prefix("12:34:ab:cd:ef:01", 0, 1, prefix, 64);
        
        char result[200];
        strcpy(result, prefix);
        strcat(result, line);
        
        const char *expected_line = "12:34:ab:cd:ef:01,0,1,1792859805,0,0.192411,0.0824327,35,17.5,27,26,0,1\n";
        CHECK(strcmp(result, expected_line) == 0);
    } else {
        CHECK(0);
    }
}

static void t_lb_late(void)
{
    uint32_t n = load_ts("data/e9_pre_raw.csv");
    if (n != 6374u) {
        CHECK(0);
        return;
    }

    uint32_t oldest = 100000u;
    uint32_t head = oldest + n;
    arr_ctx ctx = {TS, n, oldest, 0, NULL};

    for (uint32_t i = 0; i < n; i++) {
        uint32_t ts_vals[3] = {TS[i] - 1u, TS[i], TS[i] + 1u};
        for (int k = 0; k < 3; k++) {
            uint32_t T = ts_vals[k];
            uint32_t expected = oldest + lin_lb(TS, n, T);
            uint32_t res = af_hq_lower_bound(arr_ts, &ctx, oldest, head, T, 1);
            CHECK(res == expected);
        }
    }

    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 1792868400u, 1) == oldest + 4282u);
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 4294967295u, 1) == head);
    CHECK(af_hq_lower_bound(arr_ts, &ctx, oldest, head, 0u, 1) == oldest);

    uint32_t a1[1000];
    for (uint32_t i = 0; i < 1000u; i++) {
        a1[i] = 1792000000u + 2u * i;
        if (i >= 300u && i <= 399u) {
            a1[i] = 0u;
        }
    }
    uint32_t oldest1 = 7000u;
    uint32_t head1 = 8000u;
    arr_ctx ctx1 = {a1, 1000u, oldest1, 0, NULL};
    for (uint32_t i = 0; i < 1000u; i++) {
        if (i >= 300u && i <= 399u) continue;
        uint32_t ts_vals[2] = {a1[i], a1[i] + 1u};
        for (int k = 0; k < 2; k++) {
            uint32_t T = ts_vals[k];
            uint32_t exact = lin_lb(a1, 1000u, T);
            uint32_t res = af_hq_lower_bound(arr_ts, &ctx1, oldest1, head1, T, 1);
            CHECK(res >= oldest1 + exact);
            CHECK(res <= head1);
        }
    }

    uint32_t a2[800];
    for (uint32_t i = 0; i < 800u; i++) {
        a2[i] = 1792000000u + 2u * i;
        if (i >= 300u && i <= 499u) {
            a2[i] = 0u;
        }
    }
    uint32_t oldest2 = 0u;
    uint32_t head2 = 800u;
    arr_ctx ctx2 = {a2, 800u, oldest2, 0, NULL};
    uint32_t T2 = a2[600u];
    uint32_t early2 = af_hq_lower_bound(arr_ts, &ctx2, oldest2, head2, T2, 0);
    uint32_t late2 = af_hq_lower_bound(arr_ts, &ctx2, oldest2, head2, T2, 1);
    CHECK(early2 < 600u);
    CHECK(late2 == 600u);
    for (uint32_t i = 0; i < 800u; i++) {
        if (i >= 300u && i <= 499u) continue;
        uint32_t ts_vals[2] = {a2[i], a2[i] + 1u};
        for (int k = 0; k < 2; k++) {
            uint32_t T = ts_vals[k];
            uint32_t exact = lin_lb(a2, 800u, T);
            uint32_t res_early = af_hq_lower_bound(arr_ts, &ctx2, oldest2, head2, T, 0);
            uint32_t res_late = af_hq_lower_bound(arr_ts, &ctx2, oldest2, head2, T, 1);
            CHECK(res_early <= exact);
            CHECK(res_late >= exact);
        }
    }

    uint32_t a3[1000];
    unsigned char bad[1000] = {0};
    for (uint32_t i = 0; i < 1000u; i++) {
        a3[i] = 1792000000u + 2u * i;
        if (i >= 200u && i <= 340u) {
            bad[i] = 1;
        }
    }
    uint32_t oldest3 = 0u;
    uint32_t head3 = 1000u;
    arr_ctx ctx3 = {a3, 1000u, oldest3, 0, bad};
    for (uint32_t i = 0; i < 1000u; i++) {
        if (i >= 200u && i <= 340u) continue;
        uint32_t ts_vals[2] = {a3[i], a3[i] + 1u};
        for (int k = 0; k < 2; k++) {
            uint32_t T = ts_vals[k];
            uint32_t exact = 1000u;
            for (uint32_t j = 0; j < 1000u; j++) {
                if (j >= 200u && j <= 340u) continue;
                if (a3[j] >= T) {
                    exact = j;
                    break;
                }
            }
            uint32_t res_early = af_hq_lower_bound(arr_ts, &ctx3, oldest3, head3, T, 0);
            uint32_t res_late = af_hq_lower_bound(arr_ts, &ctx3, oldest3, head3, T, 1);
            CHECK(res_late >= exact);
            CHECK(res_early <= exact);
        }
    }

    uint32_t oldest4 = 10u;
    uint32_t head4 = 20u;
    CHECK(af_hq_lower_bound(NULL, NULL, oldest4, head4, 0u, 1) == oldest4);

    uint32_t oldest5 = 500u;
    uint32_t head5 = 500u;
    arr_ctx ctx5 = {NULL, 0u, oldest5, 0, NULL};
    uint32_t res5 = af_hq_lower_bound(arr_ts, &ctx5, oldest5, head5, 0u, 1);
    CHECK(res5 == oldest5);
    CHECK(ctx5.calls == 0u);

    uint32_t a6[50] = {0};
    uint32_t oldest6 = 0u;
    uint32_t head6 = 50u;
    arr_ctx ctx6 = {a6, 50u, oldest6, 0, NULL};
    uint32_t T6 = 1792000000u;
    CHECK(af_hq_lower_bound(arr_ts, &ctx6, oldest6, head6, T6, 1) == 50u);
    CHECK(af_hq_lower_bound(arr_ts, &ctx6, oldest6, head6, T6, 0) == 0u);
}
/* e16e (T-34): параметр pts - прореживание по числу точек */
static void t_pts(void) {
    af_hq_params_t p;
    af_hq_init(&p);
    CHECK(p.pts == 0);
    CHECK(af_hq_set(&p, "pts", "900") == 0 && p.pts == 900);
    CHECK(af_hq_set(&p, "pts", "0") == 0 && p.pts == 0);
    CHECK(af_hq_set(&p, "pts", "2") == 0 && p.pts == 2);
    CHECK(af_hq_set(&p, "pts", "2000") == 0 && p.pts == 2000);
    p.pts = 77;
    CHECK(af_hq_set(&p, "pts", "1") == -1 && p.pts == 77);
    CHECK(af_hq_set(&p, "pts", "2001") == -1 && p.pts == 77);
    CHECK(af_hq_set(&p, "pts", "-5") == -1);
    CHECK(af_hq_set(&p, "pts", "abc") == -1);
    CHECK(af_hq_set(&p, "pts", "") == -1);
}

int main(void) {
    RUN(t_parse_ok);
    RUN(t_parse_bad);
    RUN(t_set_kind);
    RUN(t_set_since_limit);
    RUN(t_set_from_to_ext);
    RUN(t_set_unknown);
    RUN(t_check);
    RUN(t_time_filtered_match);
    RUN(t_lb_battle_raw);
    RUN(t_lb_battle_agg);
    RUN(t_lb_zeros);
    RUN(t_lb_cb_fail);
    RUN(t_lb_edges);
    RUN(t_lb_late);
    RUN(t_plan_since);
    RUN(t_plan_time);
    RUN(t_plan_wrap);
    RUN(t_plan_null);
    RUN(t_plan_from_outside);
    RUN(t_next_n);
    RUN(t_mac);
    RUN(t_hdrs);
    RUN(t_ext);
    RUN(t_pts);
    TESTS_DONE();
    return 0;
}
