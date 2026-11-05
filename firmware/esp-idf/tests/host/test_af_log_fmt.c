#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "af_test.h"
#include "af_log.h"

static af_log_ent_t mk(uint32_t seq, uint32_t mono, uint32_t last, uint32_t utc, uint16_t cnt, uint8_t lvl, const char *tag, const char *msg) {
    af_log_ent_t e;
    memset(&e, 0, sizeof(e));
    e.seq = seq;
    e.mono_s = mono;
    e.last_s = last;
    e.utc = utc;
    e.count = cnt;
    e.level = lvl;
    if (tag) {
        strncpy(e.tag, tag, sizeof(e.tag) - 1);
        e.tag[sizeof(e.tag) - 1] = '\0';
    }
    if (msg) {
        strncpy(e.msg, msg, sizeof(e.msg) - 1);
        e.msg[sizeof(e.msg) - 1] = '\0';
    }
    return e;
}

static const char *real_value(const char *file, const char *key, char *out, size_t cap) {
    const char *dir = getenv("AF_DUMP_DIR");
    if (!dir) dir = "/mnt/c/Temp/af_integration_dump/d1";
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *hit = strstr(line, key);
        if (hit) {
            char *p = hit + strlen(key);
            while (*p == ' ' || *p == '\t') p++;
            size_t len = strlen(p);
            if (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r')) p[len-1] = '\0';
            strncpy(out, p, cap - 1);
            out[cap-1] = '\0';
            fclose(f);
            return out;
        }
    }
    fclose(f);
    return NULL;
}

static uint32_t real_now_ts(void) {
    char val[64];
    if (real_value("status.txt", "\"now_ts\":", val, sizeof(val))) {
        return (uint32_t)strtoul(val, NULL, 10);
    }
    return 0;
}

static int g_real = 0;

static void t_level_name_parse(void) {
    CHECK(strcmp(af_log_level_name(1), "info") == 0);
    CHECK(strcmp(af_log_level_name(2), "warn") == 0);
    CHECK(strcmp(af_log_level_name(3), "error") == 0);
    CHECK(strcmp(af_log_level_name(0), "info") == 0);
    CHECK(strcmp(af_log_level_name(7), "info") == 0);

    CHECK(af_log_level_parse("info") == 1);
    CHECK(af_log_level_parse("1") == 1);
    CHECK(af_log_level_parse("warn") == 2);
    CHECK(af_log_level_parse("2") == 2);
    CHECK(af_log_level_parse("error") == 3);
    CHECK(af_log_level_parse("3") == 3);
    CHECK(af_log_level_parse("INFO") == 0);
    CHECK(af_log_level_parse("") == 0);
    CHECK(af_log_level_parse("4") == 0);
    CHECK(af_log_level_parse("0") == 0);
    CHECK(af_log_level_parse("warning") == 0);
    CHECK(af_log_level_parse(NULL) == 0);
}

static void t_store_roundtrip(void) {
    af_log_ent_t e = mk(12345, 77, 99, 1792881998u, 7, 2, "ble", "connect failed");
    char b[256];
    int len = af_log_fmt_store(&e, b, sizeof(b));
    CHECK(len > 0);
    const char *exp = "12345\t77\t99\t1792881998\tW\tble\t7\tconnect failed\n";
    CHECK(len == (int)strlen(exp));
    CHECK(memcmp(b, exp, len) == 0);

    af_log_ent_t e2;
    CHECK(af_log_parse_store(b, len, &e2) == 0);
    CHECK(e2.seq == 12345);
    CHECK(e2.mono_s == 77);
    CHECK(e2.last_s == 99);
    CHECK(e2.utc == 1792881998u);
    CHECK(e2.count == 7);
    CHECK(e2.level == 2);
    CHECK(strcmp(e2.tag, "ble") == 0);
    CHECK(strcmp(e2.msg, "connect failed") == 0);

    CHECK(af_log_parse_store(b, len - 1, &e2) == 0);

    // Level 1
    af_log_ent_t e3 = mk(1, 1, 1, 1, 1, 1, "t", "m");
    char b3[256];
    int l3 = af_log_fmt_store(&e3, b3, sizeof(b3));
    CHECK(l3 > 0);
    CHECK(b3[0] == '1');
    CHECK(b3[8] == 'I');
    af_log_ent_t e3p;
    CHECK(af_log_parse_store(b3, l3, &e3p) == 0);
    CHECK(e3p.level == 1);

    // Level 3
    af_log_ent_t e4 = mk(1, 1, 1, 1, 1, 3, "t", "m");
    char b4[256];
    int l4 = af_log_fmt_store(&e4, b4, sizeof(b4));
    CHECK(l4 > 0);
    CHECK(b4[8] == 'E');
    af_log_ent_t e4p;
    CHECK(af_log_parse_store(b4, l4, &e4p) == 0);
    CHECK(e4p.level == 3);

    // Max count
    af_log_ent_t e5 = mk(1, 1, 1, 1, 65535, 1, "t", "m");
    char b5[256];
    int l5 = af_log_fmt_store(&e5, b5, sizeof(b5));
    CHECK(l5 > 0);
    af_log_ent_t e5p;
    CHECK(af_log_parse_store(b5, l5, &e5p) == 0);
    CHECK(e5p.count == 65535);

    // Max seq
    af_log_ent_t e6 = mk(4294967295u, 1, 1, 1, 1, 1, "t", "m");
    char b6[256];
    int l6 = af_log_fmt_store(&e6, b6, sizeof(b6));
    CHECK(l6 > 0);
    af_log_ent_t e6p;
    CHECK(af_log_parse_store(b6, l6, &e6p) == 0);
    CHECK(e6p.seq == 4294967295u);

    // Empty msg
    af_log_ent_t e7 = mk(1, 1, 1, 1, 1, 1, "t", "");
    char b7[256];
    int l7 = af_log_fmt_store(&e7, b7, sizeof(b7));
    CHECK(l7 > 0);
    af_log_ent_t e7p;
    CHECK(af_log_parse_store(b7, l7, &e7p) == 0);
    CHECK(e7p.msg[0] == '\0');

    // 95 char msg
    char msg95[96];
    memset(msg95, 'a', 95);
    msg95[95] = '\0';
    af_log_ent_t e8 = mk(1, 1, 1, 1, 1, 1, "t", msg95);
    char b8[256];
    int l8 = af_log_fmt_store(&e8, b8, sizeof(b8));
    CHECK(l8 > 0);
    af_log_ent_t e8p;
    CHECK(af_log_parse_store(b8, l8, &e8p) == 0);
    CHECK(strlen(e8p.msg) == 95);

    // 7 char tag
    af_log_ent_t e9 = mk(1, 1, 1, 1, 1, 1, "1234567", "m");
    char b9[256];
    int l9 = af_log_fmt_store(&e9, b9, sizeof(b9));
    CHECK(l9 > 0);
    af_log_ent_t e9p;
    CHECK(af_log_parse_store(b9, l9, &e9p) == 0);
    CHECK(strcmp(e9p.tag, "1234567") == 0);

    // Special chars in msg
    const char *special = "a\"b\\c";
    af_log_ent_t e10 = mk(1, 1, 1, 1, 1, 1, "t", special);
    char b10[256];
    int l10 = af_log_fmt_store(&e10, b10, sizeof(b10));
    CHECK(l10 > 0);
    af_log_ent_t e10p;
    CHECK(af_log_parse_store(b10, l10, &e10p) == 0);
    CHECK(strcmp(e10p.msg, special) == 0);

    // Cyrillic
    const char *cyr = "\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0";
    af_log_ent_t e11 = mk(1, 1, 1, 1, 1, 1, "t", cyr);
    char b11[256];
    int l11 = af_log_fmt_store(&e11, b11, sizeof(b11));
    CHECK(l11 > 0);
    af_log_ent_t e11p;
    CHECK(af_log_parse_store(b11, l11, &e11p) == 0);
    CHECK(strcmp(e11p.msg, cyr) == 0);
}

static void t_store_cap(void) {
    af_log_ent_t e = mk(12345, 77, 99, 1792881998u, 7, 2, "ble", "connect failed");
    char b[256];
    int len = af_log_fmt_store(&e, b, sizeof(b));
    CHECK(len > 0);

    char b2[256];
    CHECK(af_log_fmt_store(&e, b2, len + 1) == len);
    CHECK(af_log_fmt_store(&e, b2, len) == -1);
    CHECK(af_log_fmt_store(&e, b2, 0) == -1);
    CHECK(af_log_fmt_store(NULL, b2, 256) == -1);
    CHECK(af_log_fmt_store(&e, NULL, 256) == -1);

    // Worst case fit
    char msg95[96];
    memset(msg95, 'a', 95);
    msg95[95] = '\0';
    af_log_ent_t ew = mk(4294967295u, 4294967295u, 4294967295u, 4294967295u, 65535, 3, "1234567", msg95);
    char bw[256];
    int lw = af_log_fmt_store(&ew, bw, sizeof(bw));
    CHECK(lw > 0);
    CHECK(lw + 1 <= AF_LOG_LINE_MAX);
}

static void t_parse_reject(void) {
    af_log_ent_t e;
    CHECK(af_log_parse_store("", strlen(""), &e) == -1);
    CHECK(af_log_parse_store("\n", strlen("\n"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t1", strlen("1\t2\t3\t4\tI\ttag\t1"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t1\tmsg\textra", strlen("1\t2\t3\t4\tI\ttag\t1\tmsg\textra"), &e) == -1);
    CHECK(af_log_parse_store("0\t2\t3\t4\tI\ttag\t1\tm", strlen("0\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tX\ttag\t1\tm", strlen("1\t2\t3\t4\tX\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tII\ttag\t1\tm", strlen("1\t2\t3\t4\tII\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\ti\ttag\t1\tm", strlen("1\t2\t3\t4\ti\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t0\tm", strlen("1\t2\t3\t4\tI\ttag\t0\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t65536\tm", strlen("1\t2\t3\t4\tI\ttag\t65536\tm"), &e) == -1);
    CHECK(af_log_parse_store("4294967296\t2\t3\t4\tI\ttag\t1\tm", strlen("4294967296\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("12345678901\t2\t3\t4\tI\ttag\t1\tm", strlen("12345678901\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("\t2\t3\t4\tI\ttag\t1\tm", strlen("\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("-1\t2\t3\t4\tI\ttag\t1\tm", strlen("-1\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1a\t2\t3\t4\tI\ttag\t1\tm", strlen("1a\t2\t3\t4\tI\ttag\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\t\t1\tm", strlen("1\t2\t3\t4\tI\t\t1\tm"), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\t12345678\t1\tm", strlen("1\t2\t3\t4\tI\t12345678\t1\tm"), &e) == -1);
    char msg96[97];
    memset(msg96, 'a', 96);
    msg96[96] = '\0';
    char line96[256];
    snprintf(line96, sizeof(line96), "1\t2\t3\t4\tI\ttag\t1\t%s", msg96);
    CHECK(af_log_parse_store(line96, strlen(line96), &e) == -1);
    char ctrl[256];
    snprintf(ctrl, sizeof(ctrl), "1\t2\t3\t4\tI\ttag\t1\t\x01m");
    CHECK(af_log_parse_store(ctrl, strlen(ctrl), &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t1\tm\n\n", strlen("1\t2\t3\t4\tI\ttag\t1\tm\n\n"), &e) == -1);
    char cr[256];
    snprintf(cr, sizeof(cr), "1\t2\t3\t4\tI\ttag\t1\tm\r");
    CHECK(af_log_parse_store(cr, strlen(cr), &e) == -1);
    CHECK(af_log_parse_store(NULL, 10, &e) == -1);
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t1\tm", strlen("1\t2\t3\t4\tI\ttag\t1\tm"), NULL) == -1);

    // Random binary
    char garbage[201];
    for (int i = 0; i < 200; i++) garbage[i] = (char)(i * 37 + 1);
    garbage[200] = '\0';
    CHECK(af_log_parse_store(garbage, 200, &e) == -1);

    // Valid empty msg
    CHECK(af_log_parse_store("1\t2\t3\t4\tI\ttag\t1\t", strlen("1\t2\t3\t4\tI\ttag\t1\t"), &e) == 0);
}

static void t_json_exact(void) {
    af_log_ent_t e = mk(5, 10, 20, 1792881998u, 3, 3, "mqtt", "err \"x\" \\ end");
    char b[256];
    int len = af_log_fmt_json(&e, b, sizeof(b));
    CHECK(len > 0);
    const char *exp = "{\"seq\":5,\"t\":10,\"t2\":20,\"utc\":1792881998,\"lvl\":\"error\",\"tag\":\"mqtt\",\"n\":3,\"msg\":\"err \\\"x\\\" \\\\ end\"}\n";
    CHECK(len == (int)strlen(exp));
    CHECK(memcmp(b, exp, len) == 0);

    // Control char
    af_log_ent_t e2 = mk(1, 1, 1, 1, 1, 1, "t", "\x01");
    char b2[256];
    int l2 = af_log_fmt_json(&e2, b2, sizeof(b2));
    CHECK(l2 > 0);
    CHECK(strstr(b2, "\\u0001") != NULL);

    // Tab
    af_log_ent_t e3 = mk(1, 1, 1, 1, 1, 1, "t", "\t");
    char b3[256];
    int l3 = af_log_fmt_json(&e3, b3, sizeof(b3));
    CHECK(l3 > 0);
    CHECK(strstr(b3, "\\u0009") != NULL);

    // UTF-8 raw
    af_log_ent_t e4 = mk(1, 1, 1, 1, 1, 1, "t", "\xD0\x9E");
    char b4[256];
    int l4 = af_log_fmt_json(&e4, b4, sizeof(b4));
    CHECK(l4 > 0);
    CHECK(strstr(b4, "\xD0\x9E") != NULL);

    // Cap too small
    char b5[256];
    int l5 = af_log_fmt_json(&e, b5, sizeof(b5));
    CHECK(af_log_fmt_json(&e, b5, l5) == -1);
}

static void t_json_esc(void) {
    char dst[64];
    int n = af_log_json_esc(dst, sizeof(dst), "a\"b");
    CHECK(n == 4);
    CHECK(strcmp(dst, "a\\\"b") == 0);

    n = af_log_json_esc(dst, sizeof(dst), "\\");
    CHECK(n == 2);
    CHECK(strcmp(dst, "\\\\") == 0);

    n = af_log_json_esc(dst, sizeof(dst), "\x1f");
    CHECK(n == 6);
    CHECK(strcmp(dst, "\\u001f") == 0);

    n = af_log_json_esc(dst, sizeof(dst), "");
    CHECK(n == 0);
    CHECK(dst[0] == '\0');

    // Cap exact
    char d2[5];
    n = af_log_json_esc(d2, 5, "a\"b");
    CHECK(n == 4);
    CHECK(d2[4] == '\0');

    // Cap too small
    char d3[4];
    n = af_log_json_esc(d3, 4, "a\"b");
    CHECK(n == -1);
}

static void t_iso(void) {
    char out[21];
    CHECK(af_log_iso(0, out) == 1);
    CHECK(strcmp(out, "-") == 0);

    CHECK(af_log_iso(1700000000u, out) == 20);
    CHECK(strcmp(out, "2023-11-14T22:13:20Z") == 0);

    CHECK(af_log_iso(86400u, out) == 20);
    CHECK(strcmp(out, "1970-01-02T00:00:00Z") == 0);

    CHECK(af_log_iso(951782400u, out) == 20);
    CHECK(strcmp(out, "2000-02-29T00:00:00Z") == 0);

    CHECK(af_log_iso(4102444800u, out) == 20);
    CHECK(strcmp(out, "2100-01-01T00:00:00Z") == 0);

    CHECK(af_log_iso(1792881998u, out) == 20);
    CHECK(strcmp(out, "2026-10-24T22:46:38Z") == 0);

    uint32_t rts = real_now_ts();
    if (rts != 0) {
        char ro[21];
        if (af_log_iso(rts, ro) == 20) {
            CHECK(strncmp(ro, "2026-10-", 8) == 0);
            g_real++;
        }
    }

    CHECK(af_log_iso(4294967295u, out) == 20);
    CHECK(strcmp(out, "2106-02-07T06:28:15Z") == 0);

    CHECK(af_log_iso(1709164799u, out) == 20);
    CHECK(strcmp(out, "2024-02-28T23:59:59Z") == 0);

    CHECK(af_log_iso(1709164800u, out) == 20);
    CHECK(strcmp(out, "2024-02-29T00:00:00Z") == 0);

    CHECK(af_log_iso(1709251200u, out) == 20);
    CHECK(strcmp(out, "2024-03-01T00:00:00Z") == 0);
}

static void t_text(void) {
    af_log_ent_t e = mk(7, 123, 130, 1700000000u, 1, 2, "ble", "connected");
    char b[256];
    int len = af_log_fmt_text(&e, b, sizeof(b));
    CHECK(len > 0);
    const char *exp = "2023-11-14T22:13:20Z +123s WARN  ble connected\n";
    CHECK(len == (int)strlen(exp));
    CHECK(memcmp(b, exp, len) == 0);

    af_log_ent_t e2 = mk(7, 123, 130, 1700000000u, 1, 1, "ble", "connected");
    char b2[256];
    int l2 = af_log_fmt_text(&e2, b2, sizeof(b2));
    CHECK(l2 > 0);
    CHECK(strstr(b2, "INFO  ") != NULL);

    af_log_ent_t e3 = mk(7, 123, 130, 1700000000u, 1, 3, "ble", "connected");
    char b3[256];
    int l3 = af_log_fmt_text(&e3, b3, sizeof(b3));
    CHECK(l3 > 0);
    CHECK(strstr(b3, "ERROR ") != NULL);

    af_log_ent_t e4 = mk(7, 123, 130, 1700000000u, 7, 2, "ble", "connect failed");
    char b4[256];
    int l4 = af_log_fmt_text(&e4, b4, sizeof(b4));
    CHECK(l4 > 0);
    CHECK(strstr(b4, "x7 connect failed") != NULL);

    af_log_ent_t e5 = mk(7, 123, 130, 0, 1, 2, "ble", "connected");
    char b5[256];
    int l5 = af_log_fmt_text(&e5, b5, sizeof(b5));
    CHECK(l5 > 0);
    CHECK(strncmp(b5, "- +123s", 7) == 0);

    char b6[10];
    CHECK(af_log_fmt_text(&e, b6, sizeof(b6)) == -1);
}

static void t_query(void) {
    af_log_q_t q;
    CHECK(af_log_query_parse(NULL, &q) == 0);
    CHECK(q.since == 0);
    CHECK(q.limit == AF_LOG_Q_DEF_LIMIT);
    CHECK(q.min_level == 1);

    CHECK(af_log_query_parse("", &q) == 0);
    CHECK(q.since == 0);
    CHECK(q.limit == AF_LOG_Q_DEF_LIMIT);
    CHECK(q.min_level == 1);

    CHECK(af_log_query_parse("since=5&limit=20&level=warn", &q) == 0);
    CHECK(q.since == 5);
    CHECK(q.limit == 20);
    CHECK(q.min_level == 2);

    CHECK(af_log_query_parse("level=3", &q) == 0);
    CHECK(q.min_level == 3);

    CHECK(af_log_query_parse("limit=9999", &q) == 0);
    CHECK(q.limit == AF_LOG_Q_MAX_LIMIT);

    CHECK(af_log_query_parse("limit=500", &q) == 0);
    CHECK(q.limit == 500);

    CHECK(af_log_query_parse("limit=0", &q) == -1);

    CHECK(af_log_query_parse("since=4294967295", &q) == 0);
    CHECK(q.since == 4294967295u);

    CHECK(af_log_query_parse("since=4294967296", &q) == -1);
    CHECK(af_log_query_parse("since=abc", &q) == -1);
    CHECK(af_log_query_parse("since=", &q) == -1);
    CHECK(af_log_query_parse("since=-1", &q) == -1);
    CHECK(af_log_query_parse("since=1&since=2", &q) == -1);
    CHECK(af_log_query_parse("level=bad", &q) == -1);
    CHECK(af_log_query_parse("level=", &q) == -1);
    CHECK(af_log_query_parse("foo", &q) == -1);

    CHECK(af_log_query_parse("foo=bar&since=3", &q) == 0);
    CHECK(q.since == 3);

    char long_val[32];
    memset(long_val, '1', 17);
    long_val[17] = '\0';
    char qs[64];
    snprintf(qs, sizeof(qs), "since=%s", long_val);
    CHECK(af_log_query_parse(qs, &q) == -1);

    CHECK(af_log_query_parse("since=1", NULL) == -1);
}

static void t_seg_name(void) {
    char buf[32];
    CHECK(af_log_seg_name(0xDEADBEEF, buf, sizeof(buf)) == 13);
    CHECK(strcmp(buf, "gdeadbeef.log") == 0);

    CHECK(af_log_seg_name(1, buf, sizeof(buf)) == 13);
    CHECK(strcmp(buf, "g00000001.log") == 0);

    char buf13[13];
    CHECK(af_log_seg_name(1, buf13, sizeof(buf13)) == -1);
    char buf14[14];
    CHECK(af_log_seg_name(1, buf14, sizeof(buf14)) == 13);

    uint32_t seq;
    CHECK(af_log_seg_parse("g00000001.log", &seq) == 0);
    CHECK(seq == 1);

    CHECK(af_log_seg_parse("gdeadbeef.log", &seq) == 0);
    CHECK(seq == 0xDEADBEEF);

    CHECK(af_log_seg_parse("gDEADBEEF.log", &seq) == -1);
    CHECK(af_log_seg_parse("g0000001.log", &seq) == -1);
    CHECK(af_log_seg_parse("g000000001.log", &seq) == -1);
    CHECK(af_log_seg_parse("x00000001.log", &seq) == -1);
    CHECK(af_log_seg_parse("g0000000g.log", &seq) == -1);
    CHECK(af_log_seg_parse("g00000001.txt", &seq) == -1);
    CHECK(af_log_seg_parse("g00000001.log\n", &seq) == -1);
    CHECK(af_log_seg_parse("", &seq) == -1);
    CHECK(af_log_seg_parse(NULL, &seq) == -1);

    uint32_t vals[20] = {1, 2, 3, 100, 1000, 65535, 65536, 1000000, 0xDEADBEEF, 0xFFFFFFFF, 0, 42, 43, 44, 45, 46, 47, 48, 49, 50};
    for (int i = 0; i < 20; i++) {
        char nb[32];
        CHECK(af_log_seg_name(vals[i], nb, sizeof(nb)) == 13);
        uint32_t ps = 0;
        CHECK(af_log_seg_parse(nb, &ps) == 0);
        CHECK(ps == vals[i]);
    }
}

static void t_last_seq(void) {
    char buf[1024];
    int off = 0;
    for (uint32_t i = 10; i <= 14; i++) {
        af_log_ent_t e = mk(i, 1, 1, 1, 1, 1, "t", "m");
        int l = af_log_fmt_store(&e, buf + off, sizeof(buf) - off);
        CHECK(l > 0);
        off += l;
    }
    uint32_t seq;
    CHECK(af_log_last_seq(buf, off, 1, &seq) == 0);
    CHECK(seq == 14);

    // Cut last line
    int cut = off - 1; // remove trailing \n
    CHECK(af_log_last_seq(buf, cut, 1, &seq) == 0);
    CHECK(seq == 13);

    // at_file_start = 0, drop first 5 bytes
    CHECK(af_log_last_seq(buf + 5, off - 5, 0, &seq) == 0);
    CHECK(seq == 14);

    // Garbage
    char garbage[10] = {1,2,3,4,5,6,7,8,9,10};
    CHECK(af_log_last_seq(garbage, 10, 1, &seq) == -1);

    // Empty
    CHECK(af_log_last_seq(buf, 0, 1, &seq) == -1);

    // Corrupted line in middle
    char buf2[1024];
    int off2 = 0;
    for (uint32_t i = 1; i <= 3; i++) {
        af_log_ent_t e = mk(i, 1, 1, 1, 1, 1, "t", "m");
        int l = af_log_fmt_store(&e, buf2 + off2, sizeof(buf2) - off2);
        off2 += l;
    }
    // Corrupt one tab
    buf2[off2 - 10] = 'X';
    af_log_ent_t e4 = mk(4, 1, 1, 1, 1, 1, "t", "m");
    int l4 = af_log_fmt_store(&e4, buf2 + off2, sizeof(buf2) - off2);
    off2 += l4;
    CHECK(af_log_last_seq(buf2, off2, 1, &seq) == 0);
    CHECK(seq == 4);

    // Real sample
    char val[64];
    if (real_value("status.txt", "\"now_ts\":", val, sizeof(val))) {
        uint32_t ts = (uint32_t)strtoul(val, NULL, 10);
        if (ts != 0) {
            char buf3[4096];
            int off3 = 0;
            for (int i = 0; i < 40; i++) {
                af_log_ent_t e = mk(100 + i, 1, 1, ts, 1, 1, "t", "m");
                int l = af_log_fmt_store(&e, buf3 + off3, sizeof(buf3) - off3);
                if (l > 0) off3 += l;
            }
            CHECK(af_log_last_seq(buf3, off3, 1, &seq) == 0);
            CHECK(seq == 139);
            g_real++;
        }
    }
}

static void t_scan_start(void) {
    uint32_t fs[4] = {1, 100, 200, 300};
    CHECK(af_log_scan_start(fs, 4, 0) == 0);
    CHECK(af_log_scan_start(fs, 4, 98) == 0);
    CHECK(af_log_scan_start(fs, 4, 99) == 1);
    CHECK(af_log_scan_start(fs, 4, 150) == 1);
    CHECK(af_log_scan_start(fs, 4, 199) == 2);
    CHECK(af_log_scan_start(fs, 4, 299) == 3);
    CHECK(af_log_scan_start(fs, 4, 5000) == 3);
    CHECK(af_log_scan_start(fs, 4, 4294967295u) == 3);
    CHECK(af_log_scan_start(fs, 0, 0) == 0);
    uint32_t fs1[1] = {1};
    CHECK(af_log_scan_start(fs1, 1, 0) == 0);
    CHECK(af_log_scan_start(fs1, 1, 100) == 0);
}

static void t_rot_basic(void) {
    af_log_rot_in_t in;
    uint32_t del_n; int new_seg;

    in.fs_total = 12476416;
    in.fs_used = 5000000;
    in.add_bytes = 500;
    in.nfiles = 0;
    in.cur_bytes = 0;
    CHECK(af_log_rot_plan(&in, NULL, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n == 0);

    uint32_t fb1[1] = {1000};
    in.nfiles = 1;
    in.cur_bytes = 1000;
    CHECK(af_log_rot_plan(&in, fb1, &del_n, &new_seg) == 0);
    CHECK(new_seg == 0);
    CHECK(del_n == 0);

    in.cur_bytes = 16000;
    CHECK(af_log_rot_plan(&in, fb1, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n == 0);

    CHECK(af_log_rot_plan(NULL, NULL, &del_n, &new_seg) == -1);

    in.nfiles = 3;
    CHECK(af_log_rot_plan(&in, NULL, &del_n, &new_seg) == -1);
}

static void t_rot_limits(void) {
    af_log_rot_in_t in;
    uint32_t del_n; int new_seg;
    uint32_t fb[24];
    for (int i = 0; i < 24; i++) fb[i] = 10000;

    in.fs_total = 12476416;
    in.fs_used = 5000000;
    in.add_bytes = 7000;
    in.nfiles = 24;
    in.cur_bytes = 10000;
    CHECK(af_log_rot_plan(&in, fb, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n == 1);

    uint32_t fb20[20];
    for (int i = 0; i < 20; i++) fb20[i] = 13000;
    in.nfiles = 20;
    in.cur_bytes = 13000;
    in.add_bytes = 2000;
    CHECK(af_log_rot_plan(&in, fb20, &del_n, &new_seg) == 0);
    CHECK(new_seg == 0);
    CHECK(del_n == 0);

    in.add_bytes = 3000;
    CHECK(af_log_rot_plan(&in, fb20, &del_n, &new_seg) == 0);
    CHECK(new_seg == 0);
    CHECK(del_n == 1);

    uint32_t fb1[1] = {16384};
    in.nfiles = 1;
    in.cur_bytes = 16384;
    in.add_bytes = 16384;
    CHECK(af_log_rot_plan(&in, fb1, &del_n, &new_seg) == 0);

    in.nfiles = 1;
    in.cur_bytes = 1000;
    in.add_bytes = 16000;
    CHECK(af_log_rot_plan(&in, fb1, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n <= 1);
}

static void t_rot_reserve(void) {
    af_log_rot_in_t in;
    uint32_t del_n; int new_seg;

    in.fs_total = 12476416;
    in.fs_used = 9300000;
    in.add_bytes = 500;
    in.nfiles = 0;
    in.cur_bytes = 0;
    CHECK(af_log_rot_plan(&in, NULL, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n == 0);

    uint32_t fb4[4] = {16384, 16384, 16384, 16384};
    in.nfiles = 4;
    in.cur_bytes = 16384;
    in.fs_used = 9360000;
    CHECK(af_log_rot_plan(&in, fb4, &del_n, &new_seg) == 0);
    CHECK(new_seg == 1);
    CHECK(del_n == 1);

    uint32_t fb2[2] = {16384, 16384};
    in.nfiles = 2;
    in.cur_bytes = 16384;
    in.fs_used = 11000000;
    CHECK(af_log_rot_plan(&in, fb2, &del_n, &new_seg) == -1);
    CHECK(del_n == 2);

    in.fs_total = 0;
    in.fs_used = 0;
    in.nfiles = 0;
    in.cur_bytes = 0;
    CHECK(af_log_rot_plan(&in, NULL, &del_n, &new_seg) == 0);

    in.fs_total = 1000;
    in.fs_used = 2000;
    in.nfiles = 0;
    in.cur_bytes = 0;
    int rc = af_log_rot_plan(&in, NULL, &del_n, &new_seg);
    CHECK(rc == 0 || rc == -1);
}

static void t_flush_due(void) {
    CHECK(af_log_flush_due(0, 0, 0, 0, 200, 0) == 0);
    CHECK(af_log_flush_due(59, 0, 1, 0, 200, 0) == 0);
    CHECK(af_log_flush_due(60, 0, 1, 0, 200, 0) == 1);
    CHECK(af_log_flush_due(29, 0, 1, 1, 200, 0) == 0);
    CHECK(af_log_flush_due(30, 0, 1, 1, 200, 0) == 1);
    CHECK(af_log_flush_due(30, 0, 1, 0, 200, 0) == 0);
    CHECK(af_log_flush_due(30, 0, 150, 0, 200, 0) == 1);
    CHECK(af_log_flush_due(30, 0, 149, 0, 200, 0) == 0);

    CHECK(af_log_flush_due(119, 0, 1, 0, 200, 1) == 0);
    CHECK(af_log_flush_due(120, 0, 1, 0, 200, 1) == 1);
    CHECK(af_log_flush_due(60, 0, 1, 1, 200, 1) == 1);

    CHECK(af_log_flush_due(239, 0, 1, 0, 200, 2) == 0);
    CHECK(af_log_flush_due(240, 0, 1, 0, 200, 2) == 1);

    CHECK(af_log_flush_due(959, 0, 1, 0, 200, 4) == 0);
    CHECK(af_log_flush_due(960, 0, 1, 0, 200, 4) == 1);
    CHECK(af_log_flush_due(959, 0, 1, 0, 200, 99) == 0);
    CHECK(af_log_flush_due(960, 0, 1, 0, 200, 99) == 1);

    CHECK(af_log_flush_due(10, 20, 1, 0, 200, 0) == 1);
}

int main(void) {
    RUN(t_level_name_parse);
    RUN(t_store_roundtrip);
    RUN(t_store_cap);
    RUN(t_parse_reject);
    RUN(t_json_exact);
    RUN(t_json_esc);
    RUN(t_iso);
    RUN(t_text);
    RUN(t_query);
    RUN(t_seg_name);
    RUN(t_last_seq);
    RUN(t_scan_start);
    RUN(t_rot_basic);
    RUN(t_rot_limits);
    RUN(t_rot_reserve);
    RUN(t_flush_due);

    if (g_real > 0) {
        printf("REAL_SAMPLES %d\n", g_real);
    } else {
        printf("REAL_SAMPLES 0 (dump not found)\n");
    }
    TESTS_DONE();
    return 0;
}
