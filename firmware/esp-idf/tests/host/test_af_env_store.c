#include "af_test.h"
#include "af_store_core.h"
#include "af_persist.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static uint8_t RAWB[20*64];
static uint8_t AGGB[28*16];
static uint8_t ENVB[14*40];
static af_store_core_t C;
static uint8_t IMG[32 + 14*1024 + 64];

static af_env_rec_t mk_env(unsigned i) {
    af_env_rec_t r = {0};
    r.ts = 1761300000u + 30u * i;
    r.t_c100 = 2650 + (int16_t)i;
    r.p_pa = 100218u - 3u * i;
    r.flags = AF_ENV_F_T | AF_ENV_F_P;
    return r;
}

static void t_env_hdr_kind(void) {
    af_seg_hdr_t h = { .kind = AF_SEG_KIND_ENV, .rec_size = 14, .seg_idx = 7, .first_seq = 100, .epoch = 3, .cap_recs = AF_SEG_ENV_RECS };
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    af_seg_hdr_t h2;
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == 0);
    CHECK(h2.kind == AF_SEG_KIND_ENV);
    CHECK(h2.rec_size == 14);
    CHECK(h2.seg_idx == 7);
    CHECK(h2.first_seq == 100);
    CHECK(h2.epoch == 3);
    CHECK(h2.cap_recs == AF_SEG_ENV_RECS);

    af_seg_hdr_t bad = h;
    bad.rec_size = 20;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    bad.rec_size = 28;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    bad.kind = AF_SEG_KIND_RAW;
    bad.rec_size = 14;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    bad.kind = AF_SEG_KIND_AGG;
    bad.rec_size = 14;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    bad.kind = 3;
    bad.rec_size = 14;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    bad.kind = AF_SEG_KIND_ENV;
    bad.rec_size = 14;
    bad.cap_recs = 0;
    af_seg_hdr_encode(&bad, buf);
    memset(&h2, 0x5A, sizeof(h2));
    CHECK(af_seg_hdr_decode(buf, &h2) == -4);
    CHECK(h2.kind == 0x5A);

    af_seg_hdr_t ok_raw = { .kind = AF_SEG_KIND_RAW, .rec_size = 20, .seg_idx = 0, .first_seq = 0, .epoch = 0, .cap_recs = 10 };
    af_seg_hdr_encode(&ok_raw, buf);
    CHECK(af_seg_hdr_decode(buf, &h2) == 0);

    af_seg_hdr_t ok_agg = { .kind = AF_SEG_KIND_AGG, .rec_size = 28, .seg_idx = 0, .first_seq = 0, .epoch = 0, .cap_recs = 10 };
    af_seg_hdr_encode(&ok_agg, buf);
    CHECK(af_seg_hdr_decode(buf, &h2) == 0);
}

static void t_env_scan_clean(void) {
    af_seg_hdr_t h = { .kind = AF_SEG_KIND_ENV, .rec_size = 14, .seg_idx = 0, .first_seq = 0, .epoch = 1, .cap_recs = AF_SEG_ENV_RECS };
    af_seg_hdr_encode(&h, IMG);
    for (int i = 0; i < 10; i++) {
        af_env_rec_t r = mk_env(i);
        uint8_t buf[14];
        af_env_encode(&r, buf);
        memcpy(IMG + 32 + i * 14, buf, 14);
    }
    af_seg_scan_t scan;
    CHECK(af_seg_scan(IMG, 32 + 10 * 14, &scan) == 0);
    CHECK(scan.hdr.kind == AF_SEG_KIND_ENV);
    CHECK(scan.valid == 10);
    CHECK(scan.tail_bad == 0);
    CHECK(scan.good_bytes == 32 + 10 * 14);
}

static void t_env_scan_torn_and_bad(void) {
    af_seg_hdr_t h = { .kind = AF_SEG_KIND_ENV, .rec_size = 14, .seg_idx = 0, .first_seq = 0, .epoch = 1, .cap_recs = AF_SEG_ENV_RECS };
    af_seg_hdr_encode(&h, IMG);
    for (int i = 0; i < 10; i++) {
        af_env_rec_t r = mk_env(i);
        uint8_t buf[14];
        af_env_encode(&r, buf);
        memcpy(IMG + 32 + i * 14, buf, 14);
    }

    af_seg_scan_t scan;
    CHECK(af_seg_scan(IMG, 32 + 9 * 14 + 5, &scan) == 0);
    CHECK(scan.valid == 9);
    CHECK(scan.tail_bad == 1);

    af_env_rec_t r4 = mk_env(4);
    uint8_t buf4[14];
    af_env_encode(&r4, buf4);
    buf4[6] ^= 0xFF;
    memcpy(IMG + 32 + 4 * 14, buf4, 14);
    CHECK(af_seg_scan(IMG, 32 + 10 * 14, &scan) == 0);
    CHECK(scan.valid == 4);
    CHECK(scan.tail_bad == 1);

    af_seg_hdr_encode(&h, IMG);
    for (int i = 0; i < 10; i++) {
        af_env_rec_t r = mk_env(i);
        uint8_t buf[14];
        af_env_encode(&r, buf);
        memcpy(IMG + 32 + i * 14, buf, 14);
    }
    af_env_rec_t r2 = mk_env(2);
    r2.flags = 0;
    uint8_t buf2[14];
    af_env_encode(&r2, buf2);
    memcpy(IMG + 32 + 2 * 14, buf2, 14);
    CHECK(af_seg_scan(IMG, 32 + 10 * 14, &scan) == 0);
    CHECK(scan.valid == 2);

    af_seg_hdr_t h5 = h;
    h5.cap_recs = 5;
    af_seg_hdr_encode(&h5, IMG);
    for (int i = 0; i < 10; i++) {
        af_env_rec_t r = mk_env(i);
        uint8_t buf[14];
        af_env_encode(&r, buf);
        memcpy(IMG + 32 + i * 14, buf, 14);
    }
    CHECK(af_seg_scan(IMG, 32 + 10 * 14, &scan) == 0);
    CHECK(scan.valid == 5);
}

static void t_env_ring_push_read(void) {
    memset(&C, 0, sizeof(C));
    if (af_core_init(&C, RAWB, sizeof RAWB, AGGB, sizeof AGGB, 0, 0, 60, AF_MODE_LAST_DOSE) != 0) {
        CHECK(0);
        return;
    }
    if (af_core_env_init(&C, ENVB, sizeof ENVB, 0) != 0) {
        CHECK(0);
        return;
    }
    CHECK(C.env.cap_max == 40);
    CHECK(C.env.rec_size == 14);

    for (int i = 0; i < 5; i++) {
        af_env_rec_t r = mk_env(i);
        CHECK(af_core_env_push(&C, &r) == 0);
    }
    CHECK(C.env.count == 5);
    CHECK(C.env.seq == 5);

    uint8_t buf[10 * 14];
    uint32_t next = 0, lost = 0;
    CHECK(af_core_read(&C, 2, 0, buf, 10, &next, &lost) == 5);
    CHECK(next == 5);
    CHECK(lost == 0);

    for (int i = 0; i < 5; i++) {
        af_env_rec_t r = {0};
        CHECK(af_env_decode(buf + i * 14, &r) == 0);
        af_env_rec_t exp = mk_env(i);
        CHECK(r.ts == exp.ts);
        CHECK(r.t_c100 == exp.t_c100);
        CHECK(r.p_pa == exp.p_pa);
    }

    next = 0; lost = 0;
    CHECK(af_core_read(&C, 2, 3, buf, 10, &next, &lost) == 2);
    CHECK(next == 5);
    CHECK(lost == 0);
    af_env_rec_t r3, r4;
    CHECK(af_env_decode(buf, &r3) == 0);
    CHECK(af_env_decode(buf + 14, &r4) == 0);
    af_env_rec_t e3 = mk_env(3), e4 = mk_env(4);
    CHECK(r3.ts == e3.ts && r3.t_c100 == e3.t_c100 && r3.p_pa == e3.p_pa);
    CHECK(r4.ts == e4.ts && r4.t_c100 == e4.t_c100 && r4.p_pa == e4.p_pa);
}

static void t_env_ring_overwrite(void) {
    memset(&C, 0, sizeof(C));
    if (af_core_init(&C, RAWB, sizeof RAWB, AGGB, sizeof AGGB, 0, 0, 60, AF_MODE_LAST_DOSE) != 0) {
        CHECK(0);
        return;
    }
    if (af_core_env_init(&C, ENVB, sizeof ENVB, 0) != 0) {
        CHECK(0);
        return;
    }

    for (int i = 0; i < 50; i++) {
        af_env_rec_t r = mk_env(i);
        af_core_env_push(&C, &r);
    }
    CHECK(C.env.count == 40);
    CHECK(C.env.seq == 50);
    CHECK(C.env.dropped == 10);

    uint8_t buf[40 * 14];
    uint32_t next = 0, lost = 0;
    CHECK(af_core_read(&C, 2, 0, buf, 40, &next, &lost) == 40);
    CHECK(lost == 10);
    af_env_rec_t first, last;
    CHECK(af_env_decode(buf, &first) == 0);
    CHECK(af_env_decode(buf + 39 * 14, &last) == 0);
    af_env_rec_t e10 = mk_env(10), e49 = mk_env(49);
    CHECK(first.ts == e10.ts && first.t_c100 == e10.t_c100 && first.p_pa == e10.p_pa);
    CHECK(last.ts == e49.ts && last.t_c100 == e49.t_c100 && last.p_pa == e49.p_pa);
    CHECK(af_ring_oldest_seq(&C.env) == 10);

    memset(&C, 0, sizeof(C));
    if (af_core_init(&C, RAWB, sizeof RAWB, AGGB, sizeof AGGB, 0, 0, 60, AF_MODE_LAST_DOSE) != 0) {
        CHECK(0);
        return;
    }
    if (af_core_env_init(&C, ENVB, sizeof ENVB, 20) != 0) {
        CHECK(0);
        return;
    }
    for (int i = 0; i < 25; i++) {
        af_env_rec_t r = mk_env(i);
        af_core_env_push(&C, &r);
    }
    CHECK(C.env.count == 20);
    CHECK(C.env.dropped == 5);
}

static void t_env_ring_absent(void) {
    memset(&C, 0, sizeof(C));
    if (af_core_init(&C, RAWB, sizeof RAWB, AGGB, sizeof AGGB, 0, 0, 60, AF_MODE_LAST_DOSE) != 0) {
        CHECK(0);
        return;
    }
    af_env_rec_t r = mk_env(0);
    CHECK(af_core_env_push(&C, &r) == -1);
    CHECK(C.env.seq == 0);

    uint8_t buf[4 * 14];
    uint32_t next = 0, lost = 0;
    CHECK(af_core_read(&C, 2, 0, buf, 4, &next, &lost) == 0);

    CHECK(af_core_env_init(NULL, ENVB, sizeof ENVB, 0) == -1);
    CHECK(af_core_env_init(&C, NULL, 10, 0) == -1);
    CHECK(af_core_env_push(NULL, &r) == -1);

    if (af_core_env_init(&C, ENVB, sizeof ENVB, 0) != 0) {
        CHECK(0);
        return;
    }
    CHECK(af_core_env_push(&C, NULL) == -1);
}

static void t_env_isolated_from_raw_agg(void) {
    memset(&C, 0, sizeof(C));
    if (af_core_init(&C, RAWB, sizeof RAWB, AGGB, sizeof AGGB, 0, 0, 60, AF_MODE_LAST_DOSE) != 0) {
        CHECK(0);
        return;
    }
    if (af_core_env_init(&C, ENVB, sizeof ENVB, 0) != 0) {
        CHECK(0);
        return;
    }

    af_sample_t s = { .flags = 0, .dose_msv = 0.0012f, .doserate_usvh = 0.11f, .pulses_2s = 1, .cps = 0.5f, .battery = 80, .temp_c = 21 };
    for (int i = 0; i < 3; i++) {
        af_core_ingest(&C, 1761300000u + i * 2, &s);
    }
    for (int i = 0; i < 3; i++) {
        af_env_rec_t r = mk_env(i);
        af_core_env_push(&C, &r);
    }

    CHECK(C.raw.seq == 3);
    CHECK(C.env.seq == 3);

    uint8_t buf_raw[3 * 20];
    uint32_t next = 0, lost = 0;
    CHECK(af_core_read(&C, 0, 0, buf_raw, 3, &next, &lost) == 3);

    uint8_t buf_env[3 * 14];
    next = 0; lost = 0;
    CHECK(af_core_read(&C, 2, 0, buf_env, 3, &next, &lost) == 3);

    CHECK(C.raw.count == 3);
    CHECK(C.agg.count == 0);

    for (int i = 0; i < 100; i++) {
        af_env_rec_t r = mk_env(i);
        af_core_env_push(&C, &r);
    }
    CHECK(C.raw.count == 3);
}

static void t_env_restore_plan(void) {
    af_seg_info_t segs[3] = {
        { .seg_idx = 0, .first_seq = 0, .epoch = 5, .valid = 4, .tail_bad = 0, .hdr_ok = 1, .io_err = 0 },
        { .seg_idx = 1, .first_seq = 4, .epoch = 5, .valid = 4, .tail_bad = 0, .hdr_ok = 1, .io_err = 0 },
        { .seg_idx = 2, .first_seq = 8, .epoch = 5, .valid = 2, .tail_bad = 0, .hdr_ok = 1, .io_err = 0 }
    };
    af_restore_plan_t p;
    CHECK(af_restore_plan(segs, 3, 40, 4, &p) == 0);
    CHECK(p.have_data == 1);
    CHECK(p.total_recs == 10);
    CHECK(p.end_seq == 10);
    CHECK(p.first_seq == 0);
    CHECK(p.next_seg_idx == 3);
    CHECK(p.epoch == 6);
    CHECK(p.append_last == 1);
    CHECK(p.dropped_gap == 0);
}

/* долг #SA-3 (мутант E04): хранение env на флеше не меньше 14 суток при периоде 30 с */
static void t_env_retention(void) {
    CHECK((uint64_t)AF_SEG_ENV_KEEP * AF_SEG_ENV_RECS >= 14u * 24u * 3600u / 30u);
    CHECK(AF_SEG_ENV_RECS * AF_REC_ENV_SIZE + AF_SEG_HDR_SIZE <= 16384u); /* сегмент укладывается в 4 блока LittleFS */
}

int main(void) {
    RUN(t_env_hdr_kind);
    RUN(t_env_scan_clean);
    RUN(t_env_scan_torn_and_bad);
    RUN(t_env_ring_push_read);
    RUN(t_env_ring_overwrite);
    RUN(t_env_ring_absent);
    RUN(t_env_isolated_from_raw_agg);
    RUN(t_env_restore_plan);
    RUN(t_env_retention);
    TESTS_DONE();
    return 0;
}
