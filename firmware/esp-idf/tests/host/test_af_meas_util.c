#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"
#include <math.h>
#include <string.h>

#define UTC0 1792859805u

static void t_params_ok(void) {
    af_meas_params_t p;
    p = t_params();
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    CHECK(af_meas_params_check(&p, 0, 0) == 0);
    CHECK(af_meas_params_check(NULL, 1, UTC0) == AF_MEAS_E_VALUE);

    af_meas_params_t p2 = t_params();
    memset(p2.name, 'a', 32);
    p2.name[32] = '\0';
    CHECK(af_meas_params_check(&p2, 1, UTC0) == 0);

    af_meas_params_t p3 = t_params();
    memset(p3.name, 'a', sizeof(p3.name));
    CHECK(af_meas_params_check(&p3, 1, UTC0) == AF_MEAS_E_VALUE);

    af_meas_params_t p4 = t_params();
    strcpy(p4.name, "\xD0\xA4\xD0\xBE\xD0\xBD");
    CHECK(af_meas_params_check(&p4, 1, UTC0) == 0);

    af_meas_params_t p5 = t_params();
    strcpy(p5.name, "a\x01");
    CHECK(af_meas_params_check(&p5, 1, UTC0) == AF_MEAS_E_VALUE);
}

static void t_params_bounds(void) {
    af_meas_params_t p;

    p = t_params(); p.n = 1000; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    p = t_params(); p.n = 1001; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.n = 0; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.t_s = 10; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    p = t_params(); p.t_s = 9; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.t_s = 3600000; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    p = t_params(); p.t_s = 3600001; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.pause_s = 3600000; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    p = t_params(); p.pause_s = 3600001; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.lost_max = 60; CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
    p = t_params(); p.lost_max = 61; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.retry_invalid = 2; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.file_store = 2; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.no_overwrite = 2; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.stop_mode = 2; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
    p = t_params(); p.start_mode = 3; CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
}

static void t_params_sigma(void) {
    af_meas_params_t p;

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 0;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_SIGMA);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 2;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_SIGMA);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1000;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_SIGMA);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 2; p.eps_x100 = 5;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_SIGMA);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.eps_x100 = 9;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.eps_x100 = 10;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.eps_x100 = 5000;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.eps_x100 = 5001;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.t_min_s = 0;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.t_min_s = 20; p.t_max_s = 10;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.t_max_s = 3600001;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.t_min_s = 10; p.t_max_s = 10;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.stop_mode = AF_MEAS_STOP_SIGMA; p.n = 1; p.t_s = 0;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
}

static void t_params_start(void) {
    af_meas_params_t p;

    p = t_params(); p.start_mode = AF_MEAS_START_DELAY; p.delay_s = 0;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.start_mode = AF_MEAS_START_DELAY; p.delay_s = 1;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.start_mode = AF_MEAS_START_DELAY; p.delay_s = 604800;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.start_mode = AF_MEAS_START_DELAY; p.delay_s = 604801;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.start_mode = AF_MEAS_START_AT; p.start_at = UTC0 + 100;
    CHECK(af_meas_params_check(&p, 0, 0) == AF_MEAS_E_TIME);
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.start_mode = AF_MEAS_START_AT; p.start_at = UTC0 - 5;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.start_mode = AF_MEAS_START_AT; p.start_at = UTC0 - 6;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.start_mode = AF_MEAS_START_AT; p.start_at = UTC0 + 2678400;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.start_mode = AF_MEAS_START_AT; p.start_at = UTC0 + 2678401;
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);
}

static void t_params_file(void) {
    af_meas_params_t p;

    p = t_params(); p.file_store = 1;
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.file_store = 1; strcpy(p.prefix, "");
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.file_store = 1; strcpy(p.prefix, "A B");
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.file_store = 1; memset(p.prefix, 'A', 16); p.prefix[16] = '\0';
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.file_store = 1; strcpy(p.name_tpl, "{bad}");
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.file_store = 1; strcpy(p.name_tpl, "");
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.file_store = 1; memset(p.name_tpl, 'a', 47); p.name_tpl[47] = '\0';
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);

    p = t_params(); p.file_store = 1; memset(p.name_tpl, 'a', 48); p.name_tpl[48] = '\0';
    CHECK(af_meas_params_check(&p, 1, UTC0) == AF_MEAS_E_VALUE);

    p = t_params(); p.file_store = 0; strcpy(p.name_tpl, "{bad}");
    CHECK(af_meas_params_check(&p, 1, UTC0) == 0);
}

static void t_tpl(void) {
    char out[64];

    CHECK(af_meas_tpl_ok("{prefix}_{n:000}") == 1);
    CHECK(af_meas_tpl_ok("{date}_{time}") == 1);
    CHECK(af_meas_tpl_ok("abc-1.2_x y") == 1);
    {
        char s[49]; memset(s, 'a', 48); s[48] = '\0';
        CHECK(af_meas_tpl_ok(s) == 1);
    }

    CHECK(af_meas_tpl_ok("{n:00}") == 0);
    CHECK(af_meas_tpl_ok("{n}") == 0);
    CHECK(af_meas_tpl_ok("{prefix") == 0);
    CHECK(af_meas_tpl_ok("}") == 0);
    CHECK(af_meas_tpl_ok(".hidden") == 0);
    CHECK(af_meas_tpl_ok(" lead") == 0);
    CHECK(af_meas_tpl_ok("trail ") == 0);
    CHECK(af_meas_tpl_ok("a/b") == 0);
    CHECK(af_meas_tpl_ok("a\\b") == 0);
    CHECK(af_meas_tpl_ok("\xD0\xA4") == 0);
    CHECK(af_meas_tpl_ok("") == 0);
    CHECK(af_meas_tpl_ok(NULL) == 0);
    {
        char s[50]; memset(s, 'a', 49); s[49] = '\0';
        CHECK(af_meas_tpl_ok(s) == 0);
    }

    memset(out, 'X', sizeof(out));
    int len = af_meas_tpl_expand("{prefix}_{n:000}", "AtomFast", 7, 0, out, sizeof(out));
    CHECK(len == 16);
    CHECK(strcmp(out, "AtomFast_007.csv") == 0);
    CHECK(strlen(out) == 16);

    memset(out, 'X', sizeof(out));
    len = af_meas_tpl_expand("{prefix}_{n:000}", "AtomFast", 1234, 0, out, sizeof(out));
    CHECK(len == 17);
    CHECK(strcmp(out, "AtomFast_1234.csv") == 0);
    CHECK(strlen(out) == 17);

    memset(out, 'X', sizeof(out));
    len = af_meas_tpl_expand("{date}_{time}", "x", 1, 1792859805u, out, sizeof(out));
    CHECK(len == 19);
    CHECK(strcmp(out, "20261024_163645.csv") == 0);
    CHECK(strlen(out) == 19);

    memset(out, 'X', sizeof(out));
    len = af_meas_tpl_expand("{date}_{time}", "x", 1, 0, out, sizeof(out));
    CHECK(len == 19);
    CHECK(strcmp(out, "00000000_000000.csv") == 0);
    CHECK(strlen(out) == 19);

    memset(out, 'X', sizeof(out));
    len = af_meas_tpl_expand("{prefix}_{n:000}", "bad name", 7, 0, out, sizeof(out));
    CHECK(len == -1);

    memset(out, 'X', sizeof(out));
    len = af_meas_tpl_expand("{prefix}_{n:000}", "AtomFast", 7, 0, out, 10);
    CHECK(len == -1);
    CHECK(out[10] == 'X');

    {
        char tpl[48]; memset(tpl, 'a', 47); tpl[47] = '\0';
        memset(out, 'X', sizeof(out));
        len = af_meas_tpl_expand(tpl, "x", 1, 0, out, sizeof(out));
        CHECK(len == 51);
        CHECK(strlen(out) == 51);
    }

    {
        char tpl[49]; memset(tpl, 'a', 48); tpl[48] = '\0';
        memset(out, 'X', sizeof(out));
        len = af_meas_tpl_expand(tpl, "x", 1, 0, out, sizeof(out));
        CHECK(len == -1);
    }
}

static void t_name_suffix(void) {
    char out[64];

    memset(out, 'X', sizeof(out));
    int len = af_meas_name_suffix("AtomFast_007.csv", 1, out, sizeof(out));
    CHECK(len == 20);
    CHECK(strcmp(out, "AtomFast_007_001.csv") == 0);
    CHECK(strlen(out) == 20);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix("AtomFast_007.csv", 999, out, sizeof(out));
    CHECK(len == 20);
    CHECK(strcmp(out, "AtomFast_007_999.csv") == 0);
    CHECK(strlen(out) == 20);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix("AtomFast_007.csv", 0, out, sizeof(out));
    CHECK(len == -1);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix("AtomFast_007.csv", 1000, out, sizeof(out));
    CHECK(len == -1);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix(".csv", 1, out, sizeof(out));
    CHECK(len == -1);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix("abc.txt", 1, out, sizeof(out));
    CHECK(len == -1);

    memset(out, 'X', sizeof(out));
    len = af_meas_name_suffix("AtomFast_007.csv", 1, out, 15);
    CHECK(len == -1);

    {
        char base[52]; memset(base, 'a', 47); strcpy(base + 47, ".csv");
        memset(out, 'X', sizeof(out));
        len = af_meas_name_suffix(base, 1, out, sizeof(out));
        CHECK(len == 55);
        CHECK(strlen(out) == 55);
    }

    {
        char base[53]; memset(base, 'a', 48); strcpy(base + 48, ".csv");
        memset(out, 'X', sizeof(out));
        len = af_meas_name_suffix(base, 1, out, sizeof(out));
        CHECK(len == -1);
    }
}

static void t_stat(void) {
    CHECK(af_meas_n_target(100) == 10000);
    CHECK(af_meas_n_target(50) == 40000);
    CHECK(af_meas_n_target(500) == 400);
    CHECK(af_meas_n_target(5000) == 4);
    CHECK(af_meas_n_target(10) == 1000000);
    CHECK(af_meas_n_target(101) == 9803);
    CHECK(af_meas_n_target(9) == 0);
    CHECK(af_meas_n_target(5001) == 0);
    CHECK(af_meas_n_target(0) == 0);

    CHECK(near(af_meas_cps_sigma(100, 10.0), 1.0, 1e-6));
    CHECK(near(af_meas_cps_sigma(10000, 100.0), 1.0, 1e-6));
    CHECK(near(af_meas_cps_sigma(400, 20.0), 1.0, 1e-6));
    CHECK(isnan(af_meas_cps_sigma(5, 0.0)));
    CHECK(isnan(af_meas_cps_sigma(5, -1.0)));
    CHECK(isnan(af_meas_cps_sigma(5, INFINITY)));

    CHECK(near(af_meas_eps_pct(10000), 1.0, 1e-5));
    CHECK(near(af_meas_eps_pct(100), 10.0, 1e-5));
    CHECK(near(af_meas_eps_pct(1), 100.0, 1e-5));
    CHECK(isnan(af_meas_eps_pct(0)));
}

static void t_evict(void) {
    CHECK(af_meas_evict_need(0, 0, 0, 100) == 0);
    CHECK(af_meas_evict_need(7, 0, 100, 50) == 0);
    CHECK(af_meas_evict_need(8, 0, 100, 50) == 1);
    CHECK(af_meas_evict_need(9, 0, 100, 50) == 1);
    CHECK(af_meas_evict_need(7, 262044, 100, 50) == 0);
    CHECK(af_meas_evict_need(7, 262044, 101, 50) == 1);
    CHECK(af_meas_evict_need(7, 0, 100, 25) == 0);
    CHECK(af_meas_evict_need(7, 0, 100, 24) == 1);
}

static void t_utf8(void) {
    CHECK(af_meas_utf8_ok("\xD0\xA4\xD0\xBE\xD0\xBD", 6) == 1);
    CHECK(af_meas_utf8_ok("", 0) == 1);
    CHECK(af_meas_utf8_ok(NULL, 0) == 1);
    CHECK(af_meas_utf8_ok("\xE0\xA0\x80", 3) == 1);
    CHECK(af_meas_utf8_ok("\xED\x9F\xBF", 3) == 1);
    CHECK(af_meas_utf8_ok("\xF0\x90\x80\x80", 4) == 1);
    CHECK(af_meas_utf8_ok("\xF4\x8F\xBF\xBF", 4) == 1);

    CHECK(af_meas_utf8_ok(NULL, 3) == 0);
    CHECK(af_meas_utf8_ok("\xC0\x80", 2) == 0);
    CHECK(af_meas_utf8_ok("\xC1\xBF", 2) == 0);
    CHECK(af_meas_utf8_ok("\xE0\x80\x80", 3) == 0);
    CHECK(af_meas_utf8_ok("\xED\xA0\x80", 3) == 0);
    CHECK(af_meas_utf8_ok("\xF0\x80\x80\x80", 4) == 0);
    CHECK(af_meas_utf8_ok("\xF4\x90\x80\x80", 4) == 0);
    CHECK(af_meas_utf8_ok("\xF5\x80\x80\x80", 4) == 0);
    CHECK(af_meas_utf8_ok("\xD0", 1) == 0);
    CHECK(af_meas_utf8_ok("\xE2\x82", 2) == 0);
    CHECK(af_meas_utf8_ok("a\x01", 2) == 0);
    CHECK(af_meas_utf8_ok("a\x7f", 2) == 0);
    CHECK(af_meas_utf8_ok("a\nb", 3) == 0);
    CHECK(af_meas_utf8_ok("a\0b", 3) == 0);
    CHECK(af_meas_utf8_ok("\x80", 1) == 0);
}

static void t_ident(void) {
    CHECK(af_meas_ident_ok("AtomFast", 16, 0) == 1);
    CHECK(af_meas_ident_ok("", 16, 0) == 1);
    CHECK(af_meas_ident_ok("_-.", 16, 0) == 1);
    {
        char s[17]; memset(s, 'A', 16); s[16] = '\0';
        CHECK(af_meas_ident_ok(s, 16, 0) == 1);
    }

    CHECK(af_meas_ident_ok("a b", 16, 0) == 0);
    CHECK(af_meas_ident_ok("a/b", 16, 0) == 0);
    CHECK(af_meas_ident_ok("a:b", 16, 0) == 0);
    CHECK(af_meas_ident_ok(NULL, 16, 0) == 0);
    {
        char s[18]; memset(s, 'A', 17); s[17] = '\0';
        CHECK(af_meas_ident_ok(s, 16, 0) == 0);
    }

    CHECK(af_meas_ident_ok("a b", 16, 1) == 1);
}

static void t_civil(void) {
    int y, mo, d, h, mi, s;

    af_meas_civil(0, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 1970 && mo == 1 && d == 1 && h == 0 && mi == 0 && s == 0);

    af_meas_civil(86399, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 1970 && mo == 1 && d == 1 && h == 23 && mi == 59 && s == 59);

    af_meas_civil(951782400, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2000 && mo == 2 && d == 29 && h == 0 && mi == 0 && s == 0);

    af_meas_civil(1709164800, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2024 && mo == 2 && d == 29 && h == 0 && mi == 0 && s == 0);

    af_meas_civil(1740787200, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2025 && mo == 3 && d == 1 && h == 0 && mi == 0 && s == 0);

    af_meas_civil(1767225600, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2026 && mo == 1 && d == 1 && h == 0 && mi == 0 && s == 0);

    af_meas_civil(1792859805, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2026 && mo == 10 && d == 24 && h == 16 && mi == 36 && s == 45);

    af_meas_civil(4102444800, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2100 && mo == 1 && d == 1 && h == 0 && mi == 0 && s == 0);
}

int main(void) {
    RUN(t_params_ok);
    RUN(t_params_bounds);
    RUN(t_params_sigma);
    RUN(t_params_start);
    RUN(t_params_file);
    RUN(t_tpl);
    RUN(t_name_suffix);
    RUN(t_stat);
    RUN(t_evict);
    RUN(t_utf8);
    RUN(t_ident);
    RUN(t_civil);
    TESTS_DONE();
}
