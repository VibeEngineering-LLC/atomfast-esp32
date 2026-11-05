#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "af_test.h"
#include "af_parse.h"
#include "af_rec.h"
#include "af_ring.h"
#include "af_persist.h"
#include "real_hist.h"

static uint8_t IMG[32 + 20*1000 + 64];
static uint8_t SEG[4][32 + 20*300 + 64];
static uint8_t RING[20*600];

static void mk_raw(unsigned i, uint8_t out[20]) {
    af_raw_rec_t r;
    r.ts = HRAW[i].ts;
    r.s.dose_msv = HRAW[i].dose;
    r.s.doserate_usvh = HRAW[i].rate;
    r.s.pulses_2s = (uint16_t)HRAW[i].p2s;
    r.s.cps = HRAW[i].p2s / 2.0f;
    r.s.battery = (int8_t)HRAW[i].bat;
    r.s.temp_c = (int8_t)HRAW[i].temp;
    r.s.flags = (uint8_t)HRAW[i].fl;
    af_raw_encode(&r, out);
}

static void mk_agg(unsigned i, uint8_t out[28]) {
    af_agg_rec_t r;
    r.ts = HAGG[i].ts;
    r.period_s = (uint16_t)HAGG[i].period;
    r.n = (uint16_t)HAGG[i].n;
    r.dose_msv = HAGG[i].dose;
    r.doserate_usvh = HAGG[i].rate;
    r.pulses_2s = HAGG[i].pulses;
    r.battery = (int8_t)HAGG[i].bat;
    r.temp_c = (int8_t)HAGG[i].temp;
    r.flags = (uint8_t)HAGG[i].fl;
    r.agg_flags = (HAGG[i].n < 30) ? 1 : 0;
    r.modes = 1;
    af_agg_encode(&r, out);
}

static size_t build_seg(uint8_t *img, int kind, uint32_t seg_idx, uint32_t first_seq, uint32_t epoch, uint32_t cap, unsigned from, unsigned cnt) {
    af_seg_hdr_t h;
    h.kind = (uint8_t)kind;
    h.rec_size = (kind == 0) ? 20 : 28;
    h.seg_idx = seg_idx;
    h.first_seq = first_seq;
    h.epoch = epoch;
    h.cap_recs = cap;
    af_seg_hdr_encode(&h, img);
    size_t off = 32;
    for (unsigned i = 0; i < cnt; i++) {
        unsigned idx = from + i;
        if (kind == 0) {
            if (idx >= HRAW_N) break;
            uint8_t buf[20];
            mk_raw(idx, buf);
            memcpy(img + off, buf, 20);
            off += 20;
        } else {
            if (idx >= HAGG_N) break;
            uint8_t buf[28];
            mk_agg(idx, buf);
            memcpy(img + off, buf, 28);
            off += 28;
        }
    }
    return off;
}

static void t_hdr_roundtrip(void) {
    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 1;
    h.first_seq = 100;
    h.epoch = 5;
    h.cap_recs = 1000;
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    CHECK(buf[0] == 'A');
    CHECK(buf[1] == 'F');
    CHECK(buf[2] == 'S');
    CHECK(buf[3] == '1');
    CHECK(buf[4] == 1);
    uint32_t magic_le = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    CHECK(magic_le == AF_SEG_MAGIC);
    af_seg_hdr_t h2;
    memset(&h2, 0, sizeof(h2));
    int ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == 0);
    CHECK(h2.kind == h.kind);
    CHECK(h2.rec_size == h.rec_size);
    CHECK(h2.seg_idx == h.seg_idx);
    CHECK(h2.first_seq == h.first_seq);
    CHECK(h2.epoch == h.epoch);
    CHECK(h2.cap_recs == h.cap_recs);
}

static void t_hdr_bad_magic(void) {
    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 1;
    h.first_seq = 100;
    h.epoch = 5;
    h.cap_recs = 1000;
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    buf[0] = 'X';
    af_seg_hdr_t h2;
    int ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -1);
}

static void t_hdr_bad_ver(void) {
    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 1;
    h.first_seq = 100;
    h.epoch = 5;
    h.cap_recs = 1000;
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    buf[4] = 2;
    uint16_t crc = af_crc16(buf, 30);
    buf[30] = (uint8_t)(crc & 0xFF);
    buf[31] = (uint8_t)((crc >> 8) & 0xFF);
    af_seg_hdr_t h2;
    int ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -2);
}

static void t_hdr_bad_crc(void) {
    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 1;
    h.first_seq = 100;
    h.epoch = 5;
    h.cap_recs = 1000;
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    buf[16] ^= 0xFF;
    af_seg_hdr_t h2;
    int ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -3);
}

static void t_hdr_bad_fields(void) {
    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 1;
    h.first_seq = 100;
    h.epoch = 5;
    h.cap_recs = 1000;
    uint8_t buf[32];
    af_seg_hdr_encode(&h, buf);
    
    af_seg_hdr_t h2;
    memset(&h2, 0x5A, sizeof(h2));
    
    h.kind = 3;
    af_seg_hdr_encode(&h, buf);
    int ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -4);
    CHECK(h2.kind == 0x5A);
    CHECK(h2.rec_size == 0x5A);
    CHECK(h2.seg_idx == 0x5A5A5A5Au);
    CHECK(h2.first_seq == 0x5A5A5A5Au);
    CHECK(h2.epoch == 0x5A5A5A5Au);
    CHECK(h2.cap_recs == 0x5A5A5A5Au);

    h.kind = AF_SEG_KIND_AGG;
    h.rec_size = 20;
    af_seg_hdr_encode(&h, buf);
    memset(&h2, 0x5A, sizeof(h2));
    ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -4);
    CHECK(h2.kind == 0x5A);
    CHECK(h2.rec_size == 0x5A);
    CHECK(h2.seg_idx == 0x5A5A5A5Au);
    CHECK(h2.first_seq == 0x5A5A5A5Au);
    CHECK(h2.epoch == 0x5A5A5A5Au);
    CHECK(h2.cap_recs == 0x5A5A5A5Au);

    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.cap_recs = 0;
    af_seg_hdr_encode(&h, buf);
    memset(&h2, 0x5A, sizeof(h2));
    ret = af_seg_hdr_decode(buf, &h2);
    CHECK(ret == -4);
    CHECK(h2.kind == 0x5A);
    CHECK(h2.rec_size == 0x5A);
    CHECK(h2.seg_idx == 0x5A5A5A5Au);
    CHECK(h2.first_seq == 0x5A5A5A5Au);
    CHECK(h2.epoch == 0x5A5A5A5Au);
    CHECK(h2.cap_recs == 0x5A5A5A5Au);
}

static void t_scan_clean_raw(void) {
    size_t len = build_seg(IMG, 0, 3, 7000, 0, 1600, 0, 1000);
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, len, &scan);
    CHECK(ret == 0);
    CHECK(scan.valid == 1000);
    CHECK(scan.tail_bad == 0);
    CHECK(scan.good_bytes == 32 + 20*1000);
    CHECK(scan.hdr.first_seq == 7000);
}

static void t_scan_torn_tail(void) {
    size_t len = build_seg(IMG, 0, 3, 7000, 0, 1600, 0, 1000);
    size_t cut_len = 32 + 20*417 + 7;
    (void)len;
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, cut_len, &scan);
    CHECK(ret == 0);
    CHECK(scan.valid == 417);
    CHECK(scan.tail_bad == 1);
    CHECK(scan.good_bytes == 32 + 20*417);
}

static void t_scan_bad_crc_mid(void) {
    size_t len = build_seg(IMG, 0, 3, 7000, 0, 1600, 0, 1000);
    IMG[32 + 500*20 + 3] ^= 0xFF;
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, len, &scan);
    CHECK(ret == 0);
    CHECK(scan.valid == 500);
    CHECK(scan.tail_bad == 1);
}

static void t_scan_cap_limit(void) {
    size_t len = build_seg(IMG, 0, 3, 7000, 0, 300, 0, 400);
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, len, &scan);
    CHECK(ret == 0);
    CHECK(scan.valid == 300);
    CHECK(scan.tail_bad == 1);
}

static void t_scan_agg(void) {
    size_t len = build_seg(IMG, 1, 0, 0, 0, 512, 0, 98);
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, len, &scan);
    CHECK(ret == 0);
    CHECK(scan.valid == 98);
    CHECK(scan.tail_bad == 0);
    CHECK(scan.good_bytes == 32 + 28*98);

    af_seg_hdr_t h;
    h.kind = AF_SEG_KIND_RAW;
    h.rec_size = 20;
    h.seg_idx = 0;
    h.first_seq = 0;
    h.epoch = 0;
    h.cap_recs = 512;
    af_seg_hdr_encode(&h, IMG);
    af_seg_scan_t scan2;
    int ret2 = af_seg_scan(IMG, len, &scan2);
    CHECK(ret2 == 0);
    CHECK(scan2.valid == 0);
    CHECK(scan2.tail_bad == 1);
}

static void t_scan_short_and_bad_hdr(void) {
    af_seg_scan_t scan;
    int ret = af_seg_scan(IMG, 10, &scan);
    CHECK(ret == -5);
    CHECK(scan.valid == 0);

    size_t len = build_seg(IMG, 0, 0, 0, 0, 100, 0, 10);
    IMG[0] = 'X';
    af_seg_scan_t scan2;
    int ret2 = af_seg_scan(IMG, len, &scan2);
    CHECK(ret2 == -1);
    CHECK(scan2.valid == 0);
    CHECK(scan2.good_bytes == 0);
}

static void t_plan_empty(void) {
    af_restore_plan_t plan;
    int ret = af_restore_plan(NULL, 0, 100, 100, &plan);
    CHECK(ret == 0);
    CHECK(plan.have_data == 0);
    CHECK(plan.next_seg_idx == 0);
    CHECK(plan.total_recs == 0);

    ret = af_restore_plan(NULL, 0, 100, 100, NULL);
    CHECK(ret == -1);

    ret = af_restore_plan(NULL, 0, 0, 100, &plan);
    CHECK(ret == -1);
}

static void t_plan_single_clean(void) {
    af_seg_info_t segs[1];
    segs[0].seg_idx = 5;
    segs[0].first_seq = 100;
    segs[0].epoch = 42;
    segs[0].valid = 400;
    segs[0].tail_bad = 0;
    segs[0].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 1, 181491, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 400);
    CHECK(plan.first_seq == 100);
    CHECK(plan.end_seq == 500);
    CHECK(plan.append_last == 1);
    CHECK(plan.load_from == 0);
    CHECK(plan.skip_recs == 0);
    CHECK(plan.next_seg_idx == 6);
    CHECK(plan.epoch == 43); /* e8 п.3: epoch +1 при каждой загрузке */
    CHECK(plan.dropped_gap == 0);
    CHECK(plan.keep_from_idx == 5);
}

static void t_plan_contiguous_multi(void) {
    af_seg_info_t segs[3];
    segs[0].seg_idx = 10; segs[0].first_seq = 0; segs[0].epoch = 1; segs[0].valid = 1600; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 11; segs[1].first_seq = 1600; segs[1].epoch = 1; segs[1].valid = 1600; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    segs[2].seg_idx = 12; segs[2].first_seq = 3200; segs[2].epoch = 1; segs[2].valid = 250; segs[2].tail_bad = 0; segs[2].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 3, 10000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 3450);
    CHECK(plan.end_seq == 3450);
    CHECK(plan.last_pos == 2);
    CHECK(plan.append_last == 1);
    CHECK(plan.dropped_gap == 0);
}

static void t_plan_unsorted_input(void) {
    af_seg_info_t segs[3];
    segs[0].seg_idx = 12; segs[0].first_seq = 3200; segs[0].epoch = 1; segs[0].valid = 250; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 10; segs[1].first_seq = 0; segs[1].epoch = 1; segs[1].valid = 1600; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    segs[2].seg_idx = 11; segs[2].first_seq = 1600; segs[2].epoch = 1; segs[2].valid = 1600; segs[2].tail_bad = 0; segs[2].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 3, 10000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 3450);
    CHECK(plan.end_seq == 3450);
    CHECK(plan.last_pos == 2);
    CHECK(plan.append_last == 1);
    CHECK(plan.dropped_gap == 0);
    CHECK(segs[0].seg_idx == 10);
    CHECK(segs[1].seg_idx == 11);
    CHECK(segs[2].seg_idx == 12);
}

static void t_plan_gap(void) {
    af_seg_info_t segs[3];
    segs[0].seg_idx = 1; segs[0].first_seq = 0; segs[0].epoch = 4; segs[0].valid = 1600; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 2; segs[1].first_seq = 5000; segs[1].epoch = 4; segs[1].valid = 1600; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    segs[2].seg_idx = 3; segs[2].first_seq = 6600; segs[2].epoch = 4; segs[2].valid = 100; segs[2].tail_bad = 0; segs[2].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 3, 10000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 1700);
    CHECK(plan.first_seq == 5000);
    CHECK(plan.end_seq == 6700);
    CHECK(plan.dropped_gap == 1600);
    CHECK(plan.keep_from_idx == 2);
    CHECK(plan.epoch == 5);
}

static void t_plan_ring_trim(void) {
    af_seg_info_t segs[3];
    segs[0].seg_idx = 0; segs[0].first_seq = 0; segs[0].epoch = 1; segs[0].valid = 1000; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 1; segs[1].first_seq = 1000; segs[1].epoch = 1; segs[1].valid = 1000; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    segs[2].seg_idx = 2; segs[2].first_seq = 2000; segs[2].epoch = 1; segs[2].valid = 500; segs[2].tail_bad = 0; segs[2].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 3, 1200, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 1200);
    CHECK(plan.first_seq == 1300);
    CHECK(plan.end_seq == 2500);
    CHECK(plan.load_from == 1);
    CHECK(plan.skip_recs == 300);
}

static void t_plan_tail_bad(void) {
    af_seg_info_t segs[1];
    segs[0].seg_idx = 0; segs[0].first_seq = 0; segs[0].epoch = 10; segs[0].valid = 100; segs[0].tail_bad = 1; segs[0].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 1, 1000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.append_last == 0);
    CHECK(plan.tail_bad_any == 1);
    CHECK(plan.epoch == 11);
    CHECK(plan.end_seq == 100);
}

static void t_plan_full_last(void) {
    af_seg_info_t segs[1];
    segs[0].seg_idx = 0; segs[0].first_seq = 0; segs[0].epoch = 10; segs[0].valid = 1600; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 1, 10000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.append_last == 0);
    CHECK(plan.tail_bad_any == 0);
    CHECK(plan.epoch == 11);
}

/* e8 п.3: чистая загрузка (без разрыва и битого хвоста) тоже меняет epoch - иначе клиент с курсором впереди не увидит отката seq */
static void t_plan_epoch_every_boot(void) {
    af_seg_info_t segs[2];
    segs[0].seg_idx = 0; segs[0].first_seq = 0; segs[0].epoch = 3; segs[0].valid = 1600; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 1; segs[1].first_seq = 1600; segs[1].epoch = 3; segs[1].valid = 200; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    af_restore_plan_t plan;
    CHECK(af_restore_plan(segs, 2, 10000, 1600, &plan) == 0);
    CHECK(plan.dropped_gap == 0);
    CHECK(plan.tail_bad_any == 0);
    CHECK(plan.epoch == 4);
}

/* e8 п.3: epoch берётся по максимуму среди читаемых заголовков, а не только по последнему */
static void t_plan_epoch_max(void) {
    af_seg_info_t segs[2];
    segs[0].seg_idx = 0; segs[0].first_seq = 0; segs[0].epoch = 7; segs[0].valid = 100; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 1; segs[1].first_seq = 100; segs[1].epoch = 5; segs[1].valid = 100; segs[1].tail_bad = 0; segs[1].hdr_ok = 1;
    af_restore_plan_t plan;
    CHECK(af_restore_plan(segs, 2, 10000, 1600, &plan) == 0);
    CHECK(plan.epoch == 8);
}

static void t_plan_hdr_bad_skipped(void) {
    af_seg_info_t segs[3];
    segs[0].seg_idx = 1; segs[0].first_seq = 0; segs[0].epoch = 1; segs[0].valid = 100; segs[0].tail_bad = 0; segs[0].hdr_ok = 1;
    segs[1].seg_idx = 2; segs[1].first_seq = 100; segs[1].epoch = 1; segs[1].valid = 100; segs[1].tail_bad = 0; segs[1].hdr_ok = 0;
    segs[2].seg_idx = 3; segs[2].first_seq = 100; segs[2].epoch = 1; segs[2].valid = 50; segs[2].tail_bad = 0; segs[2].hdr_ok = 1;
    af_restore_plan_t plan;
    int ret = af_restore_plan(segs, 3, 1000, 1600, &plan);
    CHECK(ret == 0);
    CHECK(plan.total_recs == 150);
    CHECK(plan.end_seq == 150);
    CHECK(plan.next_seg_idx == 4);
    CHECK(plan.keep_from_idx == 1);
}

static void t_flush_chunk(void) {
    CHECK(af_flush_chunk(100, 1500, 1600) == 100);
    CHECK(af_flush_chunk(300, 1500, 1600) == 100);
    CHECK(af_flush_chunk(50, 1600, 1600) == 0);
    CHECK(af_flush_chunk(0, 10, 1600) == 0);
    CHECK(af_flush_chunk(5000, 0, 1600) == 1600);
    CHECK(af_flush_chunk(50, 1700, 1600) == 0);
}

static void t_flush_due(void) {
    CHECK(af_flush_due(1000, 800, 300, 5) == 0);
    CHECK(af_flush_due(1100, 800, 300, 5) == 1);
    CHECK(af_flush_due(1099, 800, 300, 5) == 0);
    CHECK(af_flush_due(1100, 800, 300, 0) == 0);
    CHECK(af_flush_due(500, 800, 300, 5) == 1);
    CHECK(af_flush_due(801, 800, 300, AF_FLUSH_PENDING_MAX) == 1);
    CHECK(af_flush_due(801, 800, 300, AF_FLUSH_PENDING_MAX - 1) == 0);
}

static void t_ring_finish(void) {
    af_ring_t r;
    int ret = af_ring_init(&r, RING, sizeof(RING), 20, 0, AF_RING_OVERWRITE);
    CHECK(ret == 0);
    for (int i = 0; i < 100; i++) {
        uint8_t buf[20];
        mk_raw(i, buf);
        af_ring_push(&r, buf);
    }
    ret = af_persist_ring_finish(&r, 5000, 7);
    CHECK(ret == 0);
    CHECK(r.seq == 5000);
    CHECK(r.epoch == 7);
    CHECK(af_ring_oldest_seq(&r) == 4900);
    
    static uint8_t out[20 * 100];
    uint32_t next = 0, lost = 0;
    int n = af_ring_read_since(&r, 4950, out, 100, &next, &lost);
    CHECK(n == 50);
    uint8_t expected[20];
    mk_raw(50, expected);
    CHECK(memcmp(out, expected, 20) == 0);
    CHECK(next == 5000);
    CHECK(lost == 0);

    ret = af_persist_ring_finish(&r, 50, 1);
    CHECK(ret == -1);
    ret = af_persist_ring_finish(NULL, 50, 1);
    CHECK(ret == -1);
}

static void t_restore_e2e(void) {
    size_t len0 = build_seg(SEG[0], 0, 0, 0, 0, 300, 0, 300);
    size_t len1 = build_seg(SEG[1], 0, 1, 300, 0, 300, 300, 300);
    size_t len2 = build_seg(SEG[2], 0, 2, 600, 0, 300, 600, 300);
    size_t len3 = build_seg(SEG[3], 0, 3, 900, 0, 300, 900, 100);
    size_t cut_len3 = 32 + 20*60 + 9;
    (void)len3;

    af_seg_info_t segs[4];
    af_seg_scan_t scan;
    int ret;

    ret = af_seg_scan(SEG[0], len0, &scan);
    CHECK(ret == 0);
    segs[0].seg_idx = scan.hdr.seg_idx;
    segs[0].first_seq = scan.hdr.first_seq;
    segs[0].epoch = scan.hdr.epoch;
    segs[0].valid = scan.valid;
    segs[0].tail_bad = scan.tail_bad;
    segs[0].hdr_ok = (ret == 0);

    ret = af_seg_scan(SEG[1], len1, &scan);
    CHECK(ret == 0);
    segs[1].seg_idx = scan.hdr.seg_idx;
    segs[1].first_seq = scan.hdr.first_seq;
    segs[1].epoch = scan.hdr.epoch;
    segs[1].valid = scan.valid;
    segs[1].tail_bad = scan.tail_bad;
    segs[1].hdr_ok = (ret == 0);

    ret = af_seg_scan(SEG[2], len2, &scan);
    CHECK(ret == 0);
    segs[2].seg_idx = scan.hdr.seg_idx;
    segs[2].first_seq = scan.hdr.first_seq;
    segs[2].epoch = scan.hdr.epoch;
    segs[2].valid = scan.valid;
    segs[2].tail_bad = scan.tail_bad;
    segs[2].hdr_ok = (ret == 0);

    ret = af_seg_scan(SEG[3], cut_len3, &scan);
    CHECK(ret == 0);
    segs[3].seg_idx = scan.hdr.seg_idx;
    segs[3].first_seq = scan.hdr.first_seq;
    segs[3].epoch = scan.hdr.epoch;
    segs[3].valid = scan.valid;
    segs[3].tail_bad = scan.tail_bad;
    segs[3].hdr_ok = (ret == 0);

    af_restore_plan_t plan;
    ret = af_restore_plan(segs, 4, 600, 300, &plan);
    CHECK(ret == 0);
    CHECK(plan.end_seq == 960);
    CHECK(plan.total_recs == 600);
    CHECK(plan.first_seq == 360);
    CHECK(plan.load_from == 1);
    CHECK(plan.skip_recs == 60);
    CHECK(plan.append_last == 0);
    CHECK(plan.tail_bad_any == 1);
    CHECK(plan.epoch == 1);
    CHECK(plan.next_seg_idx == 4);

    af_ring_t r;
    ret = af_ring_init(&r, RING, sizeof(RING), 20, 0, AF_RING_OVERWRITE);
    CHECK(ret == 0);

    uint32_t recs_to_load = plan.total_recs;
    uint32_t loaded = 0;
    for (uint32_t s = plan.load_from; s < 4 && loaded < recs_to_load; s++) {
        uint32_t skip = (s == plan.load_from) ? plan.skip_recs : 0;
        uint32_t valid = segs[s].valid;
        uint32_t to_load = (valid > skip) ? (valid - skip) : 0;
        if (to_load > recs_to_load - loaded) to_load = recs_to_load - loaded;
        for (uint32_t k = 0; k < to_load; k++) {
            uint32_t idx = skip + k;
            uint8_t *rec_ptr = SEG[s] + 32 + idx * 20;
            af_ring_push(&r, rec_ptr);
            loaded++;
        }
    }
    CHECK(loaded == recs_to_load);

    ret = af_persist_ring_finish(&r, plan.end_seq, plan.epoch);
    CHECK(ret == 0);
    CHECK(r.count == 600);
    CHECK(r.seq == 960);
    CHECK(af_ring_oldest_seq(&r) == 360);

    for (int i = 0; i < 600; i++) {
        uint8_t buf[20];
        af_ring_get(&r, i, buf);
        uint8_t expected[20];
        mk_raw(360 + i, expected);
        CHECK(memcmp(buf, expected, 20) == 0);
    }

    uint8_t new_rec[20];
    mk_raw(0, new_rec);
    af_ring_push(&r, new_rec);
    CHECK(r.seq == 961);
}

int main(void) {
    RUN(t_hdr_roundtrip);
    RUN(t_hdr_bad_magic);
    RUN(t_hdr_bad_ver);
    RUN(t_hdr_bad_crc);
    RUN(t_hdr_bad_fields);
    RUN(t_scan_clean_raw);
    RUN(t_scan_torn_tail);
    RUN(t_scan_bad_crc_mid);
    RUN(t_scan_cap_limit);
    RUN(t_scan_agg);
    RUN(t_scan_short_and_bad_hdr);
    RUN(t_plan_empty);
    RUN(t_plan_single_clean);
    RUN(t_plan_contiguous_multi);
    RUN(t_plan_unsorted_input);
    RUN(t_plan_gap);
    RUN(t_plan_ring_trim);
    RUN(t_plan_tail_bad);
    RUN(t_plan_full_last);
    RUN(t_plan_epoch_every_boot);
    RUN(t_plan_epoch_max);
    RUN(t_plan_hdr_bad_skipped);
    RUN(t_flush_chunk);
    RUN(t_flush_due);
    RUN(t_ring_finish);
    RUN(t_restore_e2e);
    TESTS_DONE();
    return 0;
}
