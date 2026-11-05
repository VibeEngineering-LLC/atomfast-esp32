#pragma GCC diagnostic ignored "-Wunused-function"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
static int g_failures = 0;
#include "af_push.h"
#include "push_t_common.inc"

static store_t ST;
static fk_t FK;
static char BODY[16384];
static char OUT[8192];

static unsigned long h32(const void *p, size_t n) {
    return (unsigned long)af_bsn_crc32(p, n);
}

static void sec1_pack(void) {
    af_push_pack_t pk;
    int rc, k, s, i;
    unsigned long rows, first, last, scan, lost, len, crc;

    // Полное кольцо
    st_init(&ST, 1300, 400, 400);
    st_fill(&ST, 1200, 365, 291);

    // kind=0, 12 образцов
    for (i = 0; i < 12; i++) {
        s = i * 100;
        rc = af_push_pack(fk_read, &ST, 0, s, 100, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
        rows = (unsigned long)pk.rows;
        first = (unsigned long)pk.first_seq;
        last = (unsigned long)pk.last_seq;
        scan = (unsigned long)pk.scan_end;
        lost = (unsigned long)pk.lost;
        len = (unsigned long)pk.len;
        crc = h32(BODY, strlen(BODY));
        printf("pack k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
               0, s, rc, rows, first, last, scan, lost, len, crc);
    }

    // kind=1, 4 образца
    for (i = 0; i < 4; i++) {
        s = i * 100;
        rc = af_push_pack(fk_read, &ST, 1, s, 100, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
        rows = (unsigned long)pk.rows;
        first = (unsigned long)pk.first_seq;
        last = (unsigned long)pk.last_seq;
        scan = (unsigned long)pk.scan_end;
        lost = (unsigned long)pk.lost;
        len = (unsigned long)pk.len;
        crc = h32(BODY, strlen(BODY));
        printf("pack k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
               1, s, rc, rows, first, last, scan, lost, len, crc);
    }

    // kind=2, 3 образца
    for (i = 0; i < 3; i++) {
        s = i * 100;
        rc = af_push_pack(fk_read, &ST, 2, s, 100, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
        rows = (unsigned long)pk.rows;
        first = (unsigned long)pk.first_seq;
        last = (unsigned long)pk.last_seq;
        scan = (unsigned long)pk.scan_end;
        lost = (unsigned long)pk.lost;
        len = (unsigned long)pk.len;
        crc = h32(BODY, strlen(BODY));
        printf("pack k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
               2, s, rc, rows, first, last, scan, lost, len, crc);
    }

    // Сжатое кольцо (oldest=900)
    st_init(&ST, 300, 400, 400);
    st_fill(&ST, 1200, 0, 0);

    // lost=0
    rc = af_push_pack(fk_read, &ST, 0, 0, 100, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packe k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 0, rc, rows, first, last, scan, lost, len, crc);

    // lost=7
    rc = af_push_pack(fk_read, &ST, 0, 0, 100, D1_MAC, 21, 7, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packe k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 0, rc, rows, first, last, scan, lost, len, crc);

    // start=1100, max_rows=50
    rc = af_push_pack(fk_read, &ST, 0, 1100, 50, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packe k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 1100, rc, rows, first, last, scan, lost, len, crc);

    // Нечитаемые записи
    st_init(&ST, 1300, 400, 400);
    st_fill(&ST, 40, 0, 0);
    // Испортить CRC для seq=3 и seq=4
    ST.buf[0][3 * AF_REC_RAW_SIZE + 18] ^= 0xFF;
    ST.buf[0][4 * AF_REC_RAW_SIZE + 18] ^= 0xFF;

    // max_rows=10
    rc = af_push_pack(fk_read, &ST, 0, 0, 10, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packb k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 0, rc, rows, first, last, scan, lost, len, crc);

    // max_rows=40
    rc = af_push_pack(fk_read, &ST, 0, 0, 40, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packb k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 0, rc, rows, first, last, scan, lost, len, crc);

    // Малый буфер
    st_init(&ST, 416, 400, 400);
    st_fill(&ST, 1200, 0, 0);
    rc = af_push_pack(fk_read, &ST, 0, 0, 100, D1_MAC, 21, 0, BODY, sizeof BODY, &pk);
    rows = (unsigned long)pk.rows;
    first = (unsigned long)pk.first_seq;
    last = (unsigned long)pk.last_seq;
    scan = (unsigned long)pk.scan_end;
    lost = (unsigned long)pk.lost;
    len = (unsigned long)pk.len;
    crc = h32(BODY, strlen(BODY));
    printf("packs k=%d s=%d rc=%d rows=%lu first=%lu last=%lu scan=%lu lost=%lu len=%lu crc=%08lx\n",
           0, 0, rc, rows, first, last, scan, lost, len, crc);
}

static void sec2_plan(void) {
    af_push_cur_t cur;
    af_push_plan_t pl;
    int rc, k, i;
    unsigned long start, count, lost, pend;
    unsigned fl;

    // Кольца: raw (0,1200,21), agg (0,365,21), env (0,291,21), сжатое (900,1200,21)
    // Курсоры: fresh (valid=0), acked = 0, 99, 100, head/2, head-2, head-1, head, head+5
    // Для сжатого: acked=700 (вытеснен), epoch=20
    // batch=100

    // raw
    memset(&cur, 0, sizeof cur);
    cur.valid = 0;
    af_push_plan(&cur, 0, 1200, 21, 100, &pl);
    start = (unsigned long)pl.start;
    count = (unsigned long)pl.count;
    lost = (unsigned long)pl.lost;
    pend = (unsigned long)pl.pending;
    fl = (unsigned)pl.lost;
    printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 0, "fresh", start, count, lost, pend, fl);

    int acked_vals[] = {0, 99, 100, 600, 1198, 1199, 1200, 1205};
    for (i = 0; i < 8; i++) {
        memset(&cur, 0, sizeof cur);
        cur.valid = 1;
        cur.acked = (unsigned long)acked_vals[i];
        cur.epoch = 21;
        af_push_plan(&cur, 0, 1200, 21, 100, &pl);
        start = (unsigned long)pl.start;
        count = (unsigned long)pl.count;
        lost = (unsigned long)pl.lost;
        pend = (unsigned long)pl.pending;
        fl = (unsigned)pl.lost;
        printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 0, "acked", start, count, lost, pend, fl);
    }

    // agg
    memset(&cur, 0, sizeof cur);
    cur.valid = 0;
    af_push_plan(&cur, 1, 365, 21, 100, &pl);
    start = (unsigned long)pl.start;
    count = (unsigned long)pl.count;
    lost = (unsigned long)pl.lost;
    pend = (unsigned long)pl.pending;
    fl = (unsigned)pl.lost;
    printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 1, "fresh", start, count, lost, pend, fl);

    int acked_vals_agg[] = {0, 99, 100, 182, 363, 364, 365, 370};
    for (i = 0; i < 8; i++) {
        memset(&cur, 0, sizeof cur);
        cur.valid = 1;
        cur.acked = (unsigned long)acked_vals_agg[i];
        cur.epoch = 21;
        af_push_plan(&cur, 1, 365, 21, 100, &pl);
        start = (unsigned long)pl.start;
        count = (unsigned long)pl.count;
        lost = (unsigned long)pl.lost;
        pend = (unsigned long)pl.pending;
        fl = (unsigned)pl.lost;
        printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 1, "acked", start, count, lost, pend, fl);
    }

    // env
    memset(&cur, 0, sizeof cur);
    cur.valid = 0;
    af_push_plan(&cur, 2, 291, 21, 100, &pl);
    start = (unsigned long)pl.start;
    count = (unsigned long)pl.count;
    lost = (unsigned long)pl.lost;
    pend = (unsigned long)pl.pending;
    fl = (unsigned)pl.lost;
    printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 2, "fresh", start, count, lost, pend, fl);

    int acked_vals_env[] = {0, 99, 100, 145, 289, 290, 291, 296};
    for (i = 0; i < 8; i++) {
        memset(&cur, 0, sizeof cur);
        cur.valid = 1;
        cur.acked = (unsigned long)acked_vals_env[i];
        cur.epoch = 21;
        af_push_plan(&cur, 2, 291, 21, 100, &pl);
        start = (unsigned long)pl.start;
        count = (unsigned long)pl.count;
        lost = (unsigned long)pl.lost;
        pend = (unsigned long)pl.pending;
        fl = (unsigned)pl.lost;
        printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 2, "acked", start, count, lost, pend, fl);
    }

    // сжатое (oldest=900, head=1200)
    memset(&cur, 0, sizeof cur);
    cur.valid = 0;
    af_push_plan(&cur, 0, 1200, 21, 100, &pl);
    start = (unsigned long)pl.start;
    count = (unsigned long)pl.count;
    lost = (unsigned long)pl.lost;
    pend = (unsigned long)pl.pending;
    fl = (unsigned)pl.lost;
    printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 3, "fresh", start, count, lost, pend, fl);

    // acked=700 (вытеснен)
    memset(&cur, 0, sizeof cur);
    cur.valid = 1;
    cur.acked = 700;
    cur.epoch = 20;
    af_push_plan(&cur, 0, 1200, 21, 100, &pl);
    start = (unsigned long)pl.start;
    count = (unsigned long)pl.count;
    lost = (unsigned long)pl.lost;
    pend = (unsigned long)pl.pending;
    fl = (unsigned)pl.lost;
    printf("plan r=%d c=%s start=%lu count=%lu lost=%lu pend=%lu fl=%u\n", 3, "evicted", start, count, lost, pend, fl);
}

static void sec3_res(void)
{
    struct {
        const char *id;
        int http;
        int terr;
        uint32_t start;
        uint32_t rows;
        const char *body;
        int eff_override;
    } cases[] = {
        {"b0", 200, 0, 0, 100, "{\"accepted\":100,\"last_seq\":99}", -1},
        {"b1", 200, 0, 100, 100, "{\"accepted\":100,\"last_seq\":199}", -1},
        {"b2", 200, 0, 200, 100, "{\"accepted\":100,\"last_seq\":299}", -1},
        {"b3", 200, 0, 300, 100, "{\"accepted\":100,\"last_seq\":399}", -1},
        {"b4", 200, 0, 400, 100, "{\"accepted\":100,\"last_seq\":499}", -1},
        {"b5", 200, 0, 500, 100, "{\"accepted\":100,\"last_seq\":599}", -1},
        {"b6", 200, 0, 600, 100, "{\"accepted\":100,\"last_seq\":699}", -1},
        {"b7", 200, 0, 700, 100, "{\"accepted\":100,\"last_seq\":799}", -1},
        {"b8", 200, 0, 800, 100, "{\"accepted\":100,\"last_seq\":899}", -1},
        {"b9", 200, 0, 900, 100, "{\"accepted\":100,\"last_seq\":999}", -1},
        {"b10", 200, 0, 1000, 100, "{\"accepted\":100,\"last_seq\":1099}", -1},
        {"b11", 200, 0, 1100, 100, "{\"accepted\":100,\"last_seq\":1199}", -1},
        {"idem", 200, 0, 0, 100, "{\"accepted\":0,\"last_seq\":1199}", -1},
        {"empty", 200, 0, 0, 0, "{\"accepted\":0,\"last_seq\":-1}", -1},
        {"bad_acc", 200, 0, 100, 100, "{\"accepted\":101,\"last_seq\":199}", -1},
        {"bad_low", 200, 0, 100, 100, "{\"accepted\":0,\"last_seq\":50}", -1},
        {"bad_high", 200, 0, 1100, 100, "{\"accepted\":100,\"last_seq\":1300}", -1},
        {"junk", 200, 0, 0, 0, "OK", -1},
        {"h400", 400, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h401", 401, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h404", 404, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h405", 405, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h408", 408, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h413", 413, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h429", 429, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h500", 500, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h502", 502, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h503", 503, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h599", 599, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h301", 301, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"h204", 204, 0, 100, 100, "{\"error\":\"x\"}", -1},
        {"net", 0, 1, 100, 100, "", -1},
        {"huge413", 413, 0, 100, 100, "{\"error\":\"x\"}", 10},
        {"eff0", 0, 1, 100, 100, "", 0}
    };

    size_t n = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < n; i++) {
        af_push_ring_t r;
        af_push_ring_init(&r, 500);
        r.cur.acked = 99;
        r.cur.epoch = 21;
        r.cur.valid = 1;
        uint8_t auth = 0;

        if (cases[i].eff_override != -1) {
            r.eff_batch = (uint16_t)cases[i].eff_override;
        }

        int act = af_push_on_result(
            &r, &auth,
            cases[i].http,
            cases[i].terr,
            (const uint8_t *)cases[i].body,
            strlen(cases[i].body),
            cases[i].start,
            cases[i].rows,
            1200,
            21,
            500,
            1792860000
        );

        printf("res id=%s act=%d acked=%lu v=%u ep=%lu err=%u stop=%u fails=%u http=%d eff=%u sent=%lu failt=%lu retr=%lu auth=%u\n",
               cases[i].id,
               act,
               (unsigned long)r.cur.acked,
               (unsigned)r.cur.valid,
               (unsigned long)r.cur.epoch,
               (unsigned)r.err,
               (unsigned)r.stopped,
               (unsigned)r.fails,
               (int)r.last_http,
               (unsigned)r.eff_batch,
               (unsigned long)r.sent_total,
               (unsigned long)r.fail_total,
               (unsigned long)r.retries,
               (unsigned)auth);
    }
}
static void sec4_cfg(void) {
    af_push_cfg_t cfg;
    af_push_cmd_t cmd;
    int rc, i;
    unsigned en, port, tok, intv, bat, tmo, rings, tchg, dchg;
    char host[256];

    // 16 образцов
    const char *bodies[] = {
        "{\"enabled\":true,\"host\":\"10.20.30.40\",\"port\":8480,\"token\":\"AbCdEf0123456789xyz\",\"interval_s\":60,\"batch_rows\":500,\"timeout_s\":8,\"rings\":[\"raw\",\"agg\",\"env\"]}",
        "{\"enabled\":false}",
        "{\"interval_s\":300}",
        "{\"host\":\"collector.lan\"}",
        "{\"port\":8481}",
        "{\"rings\":[\"env\"]}",
        "{\"clear_token\":true}",
        "{\"token\":\"\"}",
        "{\"port\":70000}",
        "{\"host\":\"bad host\"}",
        "{\"rings\":[\"raw\",\"raw\"]}",
        "{\"unknown_key\":1}",
        "{\"host\":\"back\\slash\"}",
        "{\"enabled\":true}",
        "{\"enabled\":true,\"token\":\"\"}",
        "{\"enabled\":true,\"host\":\"10.20.30.40\",\"port\":8480,\"token\":\"AbCdEf0123456789xyz\",\"interval_s\":60,\"batch_rows\":500,\"timeout_s\":8,\"rings\":[\"raw\",\"agg\",\"env\"]}"
    };

    for (i = 0; i < 16; i++) {
        af_push_cfg_default(&cfg);
        memset(&cmd, 0, sizeof cmd);
        rc = af_push_parse(bodies[i], strlen(bodies[i]), &cfg, &cmd);
        en = (unsigned)cfg.enabled;
        snprintf(host, sizeof host, "%s", cfg.host);
        port = (unsigned)cfg.port;
        tok = (unsigned)(cfg.token[0] != 0);
        intv = (unsigned)cfg.interval_s;
        bat = (unsigned)cfg.batch_rows;
        tmo = (unsigned)cfg.timeout_s;
        rings = (unsigned)cfg.rings;
        tchg = (unsigned)cmd.token_changed;
        dchg = (unsigned)cmd.dest_changed;
        printf("cfg id=%d rc=%d en=%u host=%s port=%u tok=%u int=%u bat=%u tmo=%u rings=%u tchg=%u dchg=%u\n",
               i, rc, en, host, port, tok, intv, bat, tmo, rings, tchg, dchg);
    }
}

static void sec5_blob(void) {
    af_push_cfg_t cfg;
    af_push_cur_t curs[3];
    unsigned char buf[256];
    int n, i;
    unsigned long crc;

    // 8 конфигов
    af_push_cfg_default(&cfg);
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 0, n, crc);

    af_push_cfg_default(&cfg);
    cfg.enabled = 1;
    snprintf(cfg.host, sizeof cfg.host, "10.20.30.40");
    snprintf(cfg.token, sizeof cfg.token, "AbCdEf0123456789xyz");
    cfg.port = 8480;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 1, n, crc);

    af_push_cfg_default(&cfg);
    cfg.port = 1;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 2, n, crc);

    af_push_cfg_default(&cfg);
    cfg.port = 65535;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 3, n, crc);

    af_push_cfg_default(&cfg);
    cfg.rings = 1;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 4, n, crc);

    af_push_cfg_default(&cfg);
    cfg.rings = 2;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 5, n, crc);

    af_push_cfg_default(&cfg);
    cfg.rings = 4;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 6, n, crc);

    af_push_cfg_default(&cfg);
    cfg.batch_rows = 50;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 7, n, crc);

    af_push_cfg_default(&cfg);
    cfg.batch_rows = 2000;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 8, n, crc);

    af_push_cfg_default(&cfg);
    cfg.interval_s = 10;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 9, n, crc);

    af_push_cfg_default(&cfg);
    cfg.interval_s = 3600;
    n = af_push_cfg_encode(&cfg, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("blob id=%d n=%d crc=%08lx\n", 10, n, crc);

    // 6 курсоров
    memset(curs, 0, sizeof curs);
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 0, n, crc);

    curs[0].acked = 1199;
    curs[0].epoch = 21;
    curs[0].valid = 1;
    curs[1].acked = 364;
    curs[1].epoch = 21;
    curs[1].valid = 1;
    curs[2].acked = 290;
    curs[2].epoch = 21;
    curs[2].valid = 1;
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 1, n, crc);

    curs[0].acked = 0xFFFFFFFF;
    curs[0].epoch = 4294967295;
    curs[0].valid = 1;
    curs[1].acked = 0xFFFFFFFF;
    curs[1].epoch = 4294967295;
    curs[1].valid = 1;
    curs[2].acked = 0xFFFFFFFF;
    curs[2].epoch = 4294967295;
    curs[2].valid = 1;
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 2, n, crc);

    memset(curs, 0, sizeof curs);
    curs[0].acked = 100;
    curs[0].epoch = 21;
    curs[0].valid = 1;
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 3, n, crc);

    memset(curs, 0, sizeof curs);
    curs[1].acked = 200;
    curs[1].epoch = 21;
    curs[1].valid = 1;
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 4, n, crc);

    memset(curs, 0, sizeof curs);
    curs[2].acked = 300;
    curs[2].epoch = 21;
    curs[2].valid = 1;
    n = af_push_curs_encode(curs, buf, sizeof buf);
    crc = h32(buf, 152);
    printf("curs id=%d n=%d crc=%08lx\n", 5, n, crc);
}

static void sec6_bo(void) {
    int f, r, p, i;
    unsigned long v;

    // backoff
    int fails_vals[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 255};
    int rnd_vals[] = {0, 1, 128, 255};
    for (i = 0; i < 14; i++) {
        f = fails_vals[i];
        for (r = 0; r < 4; r++) {
            v = (unsigned long)af_push_backoff_ms(f, rnd_vals[r]);
            printf("backoff f=%d r=%d v=%lu\n", f, rnd_vals[r], v);
        }
    }

    // pause
    int pend_vals[] = {0, 1, 500};
    int int_vals[] = {10, 30, 60, 300, 3600};
    int rnd_vals2[] = {0, 128, 255};
    for (p = 0; p < 3; p++) {
        for (i = 0; i < 5; i++) {
            for (r = 0; r < 3; r++) {
                v = (unsigned long)af_push_pause_ms(pend_vals[p], int_vals[i], rnd_vals2[r]);
                printf("pause p=%d i=%d r=%d v=%lu\n", pend_vals[p], int_vals[i], rnd_vals2[r], v);
            }
        }
    }
}

static void sec7_json(void)
{
    af_push_cfg_t c_base;
    af_push_view_t v_base;

    af_push_cfg_default(&c_base);
    c_base.enabled = 1;
    strcpy(c_base.host, "10.20.30.40");
    strcpy(c_base.token, "AbCdEf0123456789xyz");
    c_base.port = 8480;
    c_base.interval_s = 60;
    c_base.batch_rows = 500;

    memset(&v_base, 0, sizeof v_base);
    v_base.now_ts = 1792860030;
    v_base.stack_hwm = 1800;
    for (int i = 0; i < 3; i++) {
        af_push_ring_init(&v_base.ring[i], 500);
    }
    v_base.info[0] = (af_push_ringinfo_t){1, 0, 1200, 21};
    v_base.info[1] = (af_push_ringinfo_t){1, 0, 365, 21};
    v_base.info[2] = (af_push_ringinfo_t){1, 0, 291, 21};

    int n;

    // 0. полная доставка
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=0 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 1. середина доставки
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){599, 21, 1};
        v.ring[1].cur = (af_push_cur_t){199, 21, 1};
        v.ring[2].cur = (af_push_cur_t){99, 21, 1};
        v.ring[0].sent_total = 600;
        v.ring[1].sent_total = 200;
        v.ring[2].sent_total = 100;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.sending = 1;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=1 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 2. ошибка 401
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.auth_stop = 1;
        v.ring[0].err = 3;
        v.ring[0].last_http = 401;
        v.ring[0].fail_total = 1;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=2 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 3. кольцо agg остановлено
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.ring[1].stopped = 1;
        v.ring[1].err = 4;
        v.ring[1].last_http = 400;
        v.ring[1].cur = (af_push_cur_t){99, 21, 1};
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=3 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 4. выключено
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        c.enabled = 0;
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=4 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }

    // 5. публичный адрес
    {
        af_push_cfg_t c = c_base;
        af_push_view_t v = v_base;
        strcpy(c.host, "8.8.8.8");
        v.ring[0].cur = (af_push_cur_t){1199, 21, 1};
        v.ring[1].cur = (af_push_cur_t){364, 21, 1};
        v.ring[2].cur = (af_push_cur_t){290, 21, 1};
        v.ring[0].sent_total = 1200;
        v.ring[1].sent_total = 365;
        v.ring[2].sent_total = 291;
        v.ring[0].last_ok_ts = 1792860000;
        v.ring[1].last_ok_ts = 1792860000;
        v.ring[2].last_ok_ts = 1792860000;
        v.ring[0].last_rows = 100;
        v.ring[1].last_rows = 100;
        v.ring[2].last_rows = 100;
        v.ring[0].last_http = 200;
        v.ring[1].last_http = 200;
        v.ring[2].last_http = 200;
        v.test = (af_push_test_t){2, 200, 15, 0, 0, 1792860020};
        n = af_push_json(OUT, sizeof OUT, &c, &v);
        printf("json id=5 n=%d crc=%08lx\n", n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);
    }
}
static int send_one(const char *tag, int kind, const af_push_cfg_t *c, af_push_ring_t *r, uint8_t *auth, int *calls_total) {
    uint32_t left = 0;
    int moved = 0;
    int rc = af_push_send_ring(&FK_OPS, &FK, kind, c, D1_MAC, r, auth, BODY, sizeof BODY, &left, &moved);
    printf("send %s k=%d rc=%d mv=%d left=%lu acked=%lu v=%u calls=%d first=%lu lost=%lu epoch=%lu len=%lu crc=%08lx err=%u stop=%u eff=%u\n", tag, kind, rc, moved, (unsigned long)left, (unsigned long)r->cur.acked, (unsigned)r->cur.valid, FK.calls, (unsigned long)FK.last_first_seq, (unsigned long)FK.last_lost, (unsigned long)FK.last_epoch, (unsigned long)FK.last_len, h32(FK.last_body, FK.last_len), (unsigned)r->err, (unsigned)r->stopped, (unsigned)r->eff_batch);
    (*calls_total)++;
    return rc;
}

static void sec8_send(void) {
    af_push_cfg_t c;
    af_push_ring_t r[3];
    uint8_t auth = 0;
    int calls_total = 0;
    int k, rc, i;

    // (a) полная отправка до IDLE
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 100; i++) {
        int all_idle = 1;
        for (k = 0; k < 3; k++) {
            rc = send_one("a", k, &c, &r[k], &auth, &calls_total);
            if (rc != 1) all_idle = 0;
        }
        if (all_idle) break;
    }

    // (b) с ошибками транспорта и HTTP
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    calls_total = 0;
    for (i = 0; i < 60; i++) {
        int all_idle = 1;
        for (k = 0; k < 3; k++) {
            if (calls_total + 1 == 3) { FK.http = 503; FK.transport = 0; }
            else if (calls_total + 1 == 5) { FK.http = 200; FK.transport = 1; }
            else if (calls_total + 1 == 8) { FK.http = 413; FK.transport = 0; }
            else { FK.http = 200; FK.transport = 0; }
            rc = send_one("b", k, &c, &r[k], &auth, &calls_total);
            if (rc != 1) all_idle = 0;
        }
        if (all_idle) break;
    }

    // (c) 401 и повтор
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    FK.http = 401;
    send_one("c", 0, &c, &r[0], &auth, &calls_total);
    FK.http = 200;
    send_one("c", 0, &c, &r[0], &auth, &calls_total);

    // (d) сброс курсора и повтор
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 40; i++) {
        rc = send_one("d", 0, &c, &r[0], &auth, &calls_total);
        if (rc == 1) break;
    }
    af_push_ring_reset_cursor(&r[0], 100);
    strcpy(FK.resp, "{\"accepted\":0,\"last_seq\":1199}");
    send_one("d", 0, &c, &r[0], &auth, &calls_total);
    FK.resp[0] = '\0';
    send_one("d", 0, &c, &r[0], &auth, &calls_total);

    // (e) поврежденные записи
    memset(&FK, 0, sizeof FK); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
    st_init(&ST, 1300, 400, 400); st_fill(&ST, 30, 0, 0);
    ST.buf[0][0*AF_REC_RAW_SIZE+18] ^= 0xFF;
    ST.buf[0][7*AF_REC_RAW_SIZE+18] ^= 0xFF;
    af_push_cfg_default(&c); c.enabled = 1; c.batch_rows = 100;
    for (k = 0; k < 3; k++) af_push_ring_init(&r[k], 100);
    auth = 0;
    for (i = 0; i < 10; i++) {
        rc = send_one("e", 0, &c, &r[0], &auth, &calls_total);
        if (rc == 1) break;
    }
}
static void sec9_more(void) {
    const char *hosts[] = {"10.20.30.40", "10.0.0.5", "172.20.5.9", "172.16.0.1", "172.31.9.9", "172.32.0.1", "collector.lan", "af-ingest.local", "pi5", "8.8.8.8", "256.1.1.1", "01.2.3.4", "1.2.3", "host name", "-bad", "bad-", "a..b", "10.20.30.40.", "12345", "10.20.30.040", "127.0.0.1", "169.254.7.7"};
    for (size_t i = 0; i < sizeof(hosts)/sizeof(hosts[0]); i++) printf("host s=%s ok=%d cls=%d\n", hosts[i], af_push_host_ok(hosts[i]), af_push_addr_class(hosts[i]));

    char tok64[65], tok65[66]; memset(tok64, 'k', 64); tok64[64] = 0; memset(tok65, 'k', 65); tok65[65] = 0;
    const char *toks[] = {"AbCdEf0123456789xyzQWERT", tok64, tok65, "has space", "back\\slash", "quo\"te", "", "tab\there"};
    for (size_t i = 0; i < sizeof(toks)/sizeof(toks[0]); i++) printf("tok id=%d ok=%d\n", (int)i, af_push_token_ok(toks[i]));

    af_push_cfg_t base; af_push_cfg_default(&base); base.enabled = 1; strcpy(base.host, "10.20.30.40"); strcpy(base.token, "AbCdEf0123456789xyzQWERT");
    int id = 0;
    int ports[] = {0, 1, 8480, 65535}; for (int i = 0; i < 4; i++) { af_push_cfg_t c = base; c.port = ports[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int ints[] = {9, 10, 60, 3600, 3601}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.interval_s = ints[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int bats[] = {49, 50, 500, 2000, 2001}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.batch_rows = bats[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int tos[] = {1, 2, 8, 30, 31}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.timeout_s = tos[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int rings[] = {0, 1, 5, 7, 8}; for (int i = 0; i < 5; i++) { af_push_cfg_t c = base; c.rings = rings[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    int ens[] = {0, 1, 2}; for (int i = 0; i < 3; i++) { af_push_cfg_t c = base; c.enabled = ens[i]; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    { af_push_cfg_t c = base; c.host[10] = 'X'; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }
    { af_push_cfg_t c = base; c.token[24] = 'X'; printf("valid id=%d rc=%d cmpl=%d\n", id++, af_push_cfg_valid(&c), af_push_cfg_complete(&c)); }

    af_push_cfg_t dc; af_push_cfg_default(&dc); dc.enabled = 1; strcpy(dc.host, "10.20.30.40"); strcpy(dc.token, "AbCdEf0123456789xyzQWERT"); dc.port = 8480; dc.interval_s = 60; dc.batch_rows = 500; dc.timeout_s = 8; dc.rings = 7;
    uint8_t blob[152]; size_t blen = af_push_cfg_encode(&dc, blob, 152);
    int ps[] = {0,3,4,5,6,7,8,9,10,12,14,16,17,18,30,81,82,100,146,147,148,151};
    for (int i = 0; i < 22; i++) {
        for (int fix = 0; fix < 2; fix++) {
            uint8_t b[152]; memcpy(b, blob, 152); b[ps[i]] ^= 0x01;
            if (fix) { uint32_t crc = af_bsn_crc32(b, 148); b[148] = crc & 0xFF; b[149] = (crc >> 8) & 0xFF; b[150] = (crc >> 16) & 0xFF; b[151] = (crc >> 24) & 0xFF; }
            af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, b, 152);
            printf("dec p=%d fix=%d ok=%d port=%u en=%u int=%u\n", ps[i], fix, ok, d.port, d.enabled, d.interval_s);
        }
    }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, blob, 0); printf("decl n=%d ok=%d\n", 0, ok); }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, blob, 151); printf("decl n=%d ok=%d\n", 151, ok); }
    { uint8_t b153[153]; memcpy(b153, blob, 152); b153[152] = 0; af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, b153, 153); printf("decl n=%d ok=%d\n", 153, ok); }
    { af_push_cfg_t d; bool ok = af_push_cfg_decode(&d, NULL, 152); printf("decl n=%d ok=%d\n", -1, ok); }

    af_push_cur_t curs[3] = {{1199,21,1},{364,21,1},{290,21,1}};
    uint8_t cblob[48]; size_t clen = af_push_curs_encode(curs, cblob, 48);
    int cps[] = {0,3,4,6,8,11,12,16,17,18,19,20,28,32,36,40,44,47};
    for (int i = 0; i < 18; i++) {
        for (int fix = 0; fix < 2; fix++) {
            uint8_t b[48]; memcpy(b, cblob, 48); b[cps[i]] ^= 0x01;
            if (fix) { uint32_t crc = af_bsn_crc32(b, 44); b[44] = crc & 0xFF; b[45] = (crc >> 8) & 0xFF; b[46] = (crc >> 16) & 0xFF; b[47] = (crc >> 24) & 0xFF; }
            af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, b, 48);
            printf("cdec p=%d fix=%d ok=%d a0=%lu v0=%u v1=%u v2=%u\n", cps[i], fix, ok, (unsigned long)d[0].acked, d[0].valid, d[1].valid, d[2].valid);
        }
    }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, cblob, 0); printf("cdec n=%d ok=%d\n", 0, ok); }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, cblob, 47); printf("cdec n=%d ok=%d\n", 47, ok); }
    { uint8_t b49[49]; memcpy(b49, cblob, 48); b49[48] = 0; af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, b49, 49); printf("cdec n=%d ok=%d\n", 49, ok); }
    { af_push_cur_t d[3]; bool ok = af_push_curs_decode(d, NULL, 48); printf("cdec n=%d ok=%d\n", -1, ok); }

    int dirty[] = {0, 1};
    uint32_t nows[] = {100000, 29999, 30000, 30001, 60000, 0x7000, 0x7F00, 5};
    uint32_t lasts[] = {0, 0, 0, 0, 0, 0xFFFFFF00, 0xFFFFFF00, 6};
    for (int i = 0; i < 8; i++) {
        for (int d = 0; d < 2; d++) {
            bool v = af_push_curs_due(d, nows[i], lasts[i]);
            printf("due d=%d now=%lu last=%lu v=%d\n", d, (unsigned long)nows[i], (unsigned long)lasts[i], v);
        }
    }

    int sf[5][2] = {{0,0},{1,0},{0,2},{0,3},{0,255}};
    int rgs[] = {7, 5, 3, 0};
    for (int i = 0; i < 5; i++) {
        for (int r = 0; r < 3; r++) {
            for (int rg = 0; rg < 4; rg++) {
                for (int a = 0; a < 2; a++) {
                    af_push_ring_t rr[3]; memset(rr, 0, sizeof(rr));
                    rr[r].stopped = sf[i][0]; rr[r].fails = sf[i][1];
                    int v = af_push_bad(rr, rgs[rg], a);
                    printf("bad k=%d s=%d f=%d rings=%d auth=%d v=%d\n", r, sf[i][0], sf[i][1], rgs[rg], a, v);
                }
            }
        }
    }
    for (int e = 0; e < 2; e++) for (int s = 0; s < 2; s++) for (int b = 0; b < 2; b++) printf("state e=%d s=%d b=%d n=%s\n", e, s, b, af_push_state_name(e, s, b));
    for (int e = 0; e <= 12; e++) printf("errn e=%d n=%s\n", e, af_push_err_name((uint8_t)e));
    printf("errn e=%d n=%s\n", 255, af_push_err_name(255));

    int effs[] = {10, 25, 50, 125, 250, 300, 500};
    for (int i = 0; i < 7; i++) {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.eff_batch = effs[i]; r.cur = (af_push_cur_t){99, 21, 1};
        uint8_t auth = 0;
        int act = af_push_on_result(&r, &auth, 200, 0, "{\"accepted\":0,\"last_seq\":99}", 28, 100, 0, 1200, 21, 500, 0);
        printf("grow eff=%d act=%d -> %u\n", effs[i], act, r.eff_batch);
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.eff_batch = 500;
        uint8_t auth = 0;
        for (int i = 0; i < 10; i++) {
            int act = af_push_on_result(&r, &auth, 413, 0, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("s413 i=%d act=%d eff=%u stop=%u retr=%lu failt=%lu\n", i, act, r.eff_batch, r.stopped, (unsigned long)r.retries, (unsigned long)r.fail_total);
        }
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.fails = 253;
        uint8_t auth = 0;
        for (int i = 0; i < 4; i++) {
            int act = af_push_on_result(&r, &auth, 503, 0, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("s503 i=%d act=%d fails=%u failt=%lu\n", i, act, r.fails, (unsigned long)r.fail_total);
        }
    }
    {
        af_push_ring_t r; af_push_ring_init(&r, 500); r.fails = 254;
        uint8_t auth = 0;
        for (int i = 0; i < 3; i++) {
            int act = af_push_on_result(&r, &auth, 0, 1, "", 0, 100, 100, 1200, 21, 500, 0);
            printf("snet i=%d fails=%u\n", i, r.fails);
        }
    }

    const char *bodies[] = {
        "{\"accepted\":5,\"last_seq\":104}",
        "{\"accepted\":0,\"last_seq\":-1}",
        "{\"accepted\":0,\"last_seq\":4294967295}",
        "{\"last_seq\":7,\"accepted\":3}",
        "{\"accepted\":1,\"last_seq\":-2}",
        "{\"accepted\":4294967296,\"last_seq\":0}",
        "{\"accepted\":1.5,\"last_seq\":2}",
        "{\"accepted\":1}",
        "{\"accepted\":1,\"last_seq\":2,\"x\":3}",
        "[]",
        "OK",
        "",
        "{\"accepted\":\"1\",\"last_seq\":2}",
        "{\"accepted\":-1,\"last_seq\":2}"
    };
    for (int i = 0; i < 14; i++) {
        uint32_t acc = 77; int64_t last = 77;
        int rc = af_push_parse_resp(bodies[i], strlen(bodies[i]), &acc, &last);
        printf("resp id=%d rc=%d acc=%lu last=%ld\n", i, rc, (unsigned long)acc, (long)last);
    }

    uint32_t olds[] = {0, 900, 0xFFFFFFF0};
    uint32_t heads[] = {1200, 1200, 0x10};
    for (int i = 0; i < 3; i++) {
        uint32_t o = olds[i], h = heads[i];
        uint32_t acks[] = {o-2, o-1, o, o+1, h-2, h-1, h, h+1};
        for (int j = 0; j < 8; j++) {
            af_push_cur_t cur = {acks[j], 21, 1};
            af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p);
            printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)acks[j], (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags);
        }
        { af_push_cur_t cur = {0, 21, 0}; af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p); printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)cur.acked, (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags); }
        { af_push_cur_t cur = {o, 20, 1}; af_push_plan_t p; af_push_plan(&cur, o, h, 21, 100, &p); printf("plan2 o=%lu h=%lu a=%lu start=%lu cnt=%lu lost=%lu pend=%lu fl=%u\n", (unsigned long)o, (unsigned long)h, (unsigned long)cur.acked, (unsigned long)p.start, (unsigned long)p.count, (unsigned long)p.lost, (unsigned long)p.pending, p.flags); }
    }

    af_push_cfg_t sc; af_push_cfg_default(&sc); sc.enabled = 1; sc.batch_rows = 100;
    af_push_ring_t sr; af_push_ring_init(&sr, 100);
    uint8_t sauth = 0;
    uint32_t sleft = 0; int smv = 0;
    int sids[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    for (int i = 0; i < 20; i++) {
        memset(&FK, 0, sizeof(FK)); FK.st = &ST; FK.http = 200; FK.auto_ok = 1; FK.now = 1792860000;
        st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
        memset(&sr, 0, sizeof(sr)); af_push_ring_init(&sr, 100); sauth = 0; sleft = 0; smv = 0;
        int kind = 0;
        switch (i) {
            case 0: st_init(&ST, 300, 400, 400); st_fill(&ST, 1200, 0, 0); sr.cur = (af_push_cur_t){49, 21, 1}; break;
            case 1: sr.cur = (af_push_cur_t){500, 20, 1}; break;
            case 2: sr.cur = (af_push_cur_t){5000, 21, 1}; break;
            case 3: ST.busy = 1; break;
            case 4: ST.rbusy = 1; break;
            case 5: break;
            case 6: break;
            case 7: st_init(&ST, 1300, 400, 400); st_fill(&ST, 5, 0, 0); for (int j = 0; j < 5; j++) ST.buf[0][j*AF_REC_RAW_SIZE+18] ^= 0xFF; break;
            case 8: st_init(&ST, 1300, 400, 400); st_fill(&ST, 20, 0, 0); ST.buf[0][18] ^= 0xFF; break;
            case 9: sr.eff_batch = 500; break;
            case 10: sr.stopped = 1; break;
            case 11: strcpy(FK.resp, "{\"accepted\":0,\"last_seq\":1199}"); sr.cur.valid = 0; break;
            case 12: strcpy(FK.resp, "zzz"); break;
            case 13: FK.http = 400; break;
            case 14: FK.http = 401; break;
            case 15: FK.http = 500; break;
            case 16: FK.http = 413; break;
            case 17: FK.transport = 1; break;
            case 18: kind = 1; break;
            case 19: kind = 2; break;
        }
        const char *mac = (i == 5) ? "" : (i == 6) ? NULL : D1_MAC;
        int rc = af_push_send_ring(&FK_OPS, &FK, kind, &sc, mac, &sr, &sauth, BODY, sizeof(BODY), &sleft, &smv);
        uint32_t crc = FK.last_len ? af_bsn_crc32((uint8_t*)FK.last_body, FK.last_len) : 0;
        printf("sendx id=%d rc=%d mv=%d left=%lu acked=%lu v=%u calls=%d first=%lu lost=%lu epoch=%lu len=%lu crc=%08lx err=%u stop=%u eff=%u auth=%u\n", i+1, rc, smv, (unsigned long)sleft, (unsigned long)sr.cur.acked, sr.cur.valid, FK.calls, (unsigned long)FK.last_first_seq, (unsigned long)FK.last_lost, (unsigned long)FK.last_epoch, (unsigned long)FK.last_len, crc, sr.err, sr.stopped, sr.eff_batch, sauth);
    }

    af_push_cfg_t jc; af_push_cfg_default(&jc); jc.enabled = 1; strcpy(jc.host, "10.20.30.40"); strcpy(jc.token, "AbCdEf0123456789xyzQWERT");
    af_push_view_t jv; memset(&jv, 0, sizeof(jv));
    jv.info[0] = (af_push_ringinfo_t){1, 0, 1200, 21}; jv.info[1] = (af_push_ringinfo_t){1, 0, 365, 21}; jv.info[2] = (af_push_ringinfo_t){1, 0, 291, 21};
    jv.ring[0].cur = (af_push_cur_t){599, 21, 1}; jv.ring[0].last_ok_ts = 1792860000; jv.now_ts = 1792860030;
    int jn = af_push_json(OUT, sizeof(OUT), &jc, &jv);
    int jleak = (strstr(OUT, "AbCdEf") != NULL);
    printf("jsonx len=%d leak=%d\n", jn, jleak);
    char OUT2[8192];
    int caps[] = {jn-1, jn, jn+1, jn+2, 10, 0};
    for (int i = 0; i < 6; i++) {
        if (caps[i] > 8192) caps[i] = 8192;
        if (caps[i] < 0) caps[i] = 0;
        printf("jsonx cap=%lu rc=%d\n", (unsigned long)caps[i], af_push_json(OUT2, caps[i], &jc, &jv));
    }

    st_init(&ST, 1300, 400, 400); st_fill(&ST, 1200, 365, 291);
    af_push_pack_t pk; memset(&pk, 0, sizeof(pk));
    int pids[] = {1,2,3,4,5,6,7,8};
    for (int i = 0; i < 8; i++) {
        int kind = 0; uint32_t maxr = 100; size_t cap = 416; const char *mac = D1_MAC;
        switch (i) {
            case 0: kind = 3; break;
            case 1: kind = -1; break;
            case 2: maxr = 0; break;
            case 3: cap = 415; break;
            case 4: cap = 416; break;
            case 5: mac = ""; break;
            case 6: mac = "a,b"; break;
            case 7: mac = NULL; break;
        }
        int rc = af_push_pack(fk_read, &ST, kind, 0, maxr, mac, 21, 0, BODY, cap, &pk);
        printf("packx id=%d rc=%d rows=%lu scan=%lu\n", i+1, rc, (unsigned long)pk.rows, (unsigned long)pk.scan_end);
    }
}
int main(void) {
    sec1_pack();
    sec2_plan();
    sec3_res();
    sec4_cfg();
    sec5_blob();
    sec6_bo();
    sec7_json();
    sec8_send();
    sec9_more();
    return 0;
}
