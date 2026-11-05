#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "af_test.h"
#include "af_settings.h"

static void t_window_valid_reject(void)
{
    CHECK(af_settings_window_valid(0) == 0);
    CHECK(af_settings_window_valid(59) == 0);
    CHECK(af_settings_window_valid(61) == 0);
    CHECK(af_settings_window_valid(90) == 0);
    CHECK(af_settings_window_valid(1260) == 0);
    CHECK(af_settings_window_valid(1201) == 0);
    CHECK(af_settings_window_valid(30) == 0);
    CHECK(af_settings_window_valid(119) == 0);
}

static void t_window_valid_accept(void)
{
    CHECK(af_settings_window_valid(60) == 1);
    CHECK(af_settings_window_valid(120) == 1);
    CHECK(af_settings_window_valid(300) == 1);
    CHECK(af_settings_window_valid(600) == 1);
    CHECK(af_settings_window_valid(1200) == 1);
}

static void t_sigma_valid(void)
{
    CHECK(af_settings_sigma_valid(1) == 1);
    CHECK(af_settings_sigma_valid(2) == 1);
    CHECK(af_settings_sigma_valid(3) == 1);
    CHECK(af_settings_sigma_valid(0) == 0);
    CHECK(af_settings_sigma_valid(4) == 0);
    CHECK(af_settings_sigma_valid(255) == 0);
}

static void t_defaults(void)
{
    af_settings_t s;
    af_settings_defaults(&s);
    CHECK(s.store_raw == 1);
    CHECK(s.store_agg == 1);
    CHECK(s.store_env == 1);
    CHECK(s.window_s == 60);
    CHECK(s.sigma_k == 1);
    CHECK(s.ble_ext == 0);
}

static void t_sanitize_ok(void)
{
    af_settings_t s;
    af_settings_defaults(&s);
    uint32_t mask = af_settings_sanitize(&s);
    CHECK(mask == 0);
    CHECK(s.store_raw == 1);
    CHECK(s.store_agg == 1);
    CHECK(s.store_env == 1);
    CHECK(s.window_s == 60);
    CHECK(s.sigma_k == 1);
    CHECK(s.ble_ext == 0);
}

static void t_sanitize_replace(void)
{
    af_settings_t s;

    af_settings_defaults(&s);
    s.store_raw = 2;
    uint32_t m = af_settings_sanitize(&s);
    CHECK(m == AF_SET_R_RAW);
    CHECK(s.store_raw == 1);

    af_settings_defaults(&s);
    s.window_s = 90;
    m = af_settings_sanitize(&s);
    CHECK(m == AF_SET_R_WINDOW);
    CHECK(s.window_s == 60);

    af_settings_defaults(&s);
    s.sigma_k = 4;
    m = af_settings_sanitize(&s);
    CHECK(m == AF_SET_R_SIGMA);
    CHECK(s.sigma_k == 1);

    af_settings_defaults(&s);
    s.ble_ext = 7;
    m = af_settings_sanitize(&s);
    CHECK(m == AF_SET_R_BLE_EXT);
    CHECK(s.ble_ext == 0);

    af_settings_defaults(&s);
    s.store_agg = 9;
    s.store_env = 9;
    m = af_settings_sanitize(&s);
    CHECK(m == (AF_SET_R_AGG | AF_SET_R_ENV));

    af_settings_defaults(&s);
    s.store_raw = 5;
    s.store_agg = 7;
    s.store_env = 3;
    s.window_s = 9999;
    s.sigma_k = 99;
    s.ble_ext = 9;
    m = af_settings_sanitize(&s);
    CHECK(m == 0x3F);
}

static void t_json_get_defaults(void)
{
    af_settings_t s;
    af_settings_defaults(&s);
    char buf[128];
    int len = af_settings_json_get(&s, buf, sizeof(buf));
    CHECK(len == 73);
    CHECK(strcmp(buf, "{\"store\":{\"raw\":1,\"agg\":1,\"env\":1},\"window_s\":60,\"sigma_k\":1,\"ble_ext\":0}") == 0);
}

static void t_json_get_values(void)
{
    af_settings_t s;
    s.store_raw = 0;
    s.store_agg = 1;
    s.store_env = 0;
    s.window_s = 1200;
    s.sigma_k = 3;
    s.ble_ext = 1;
    char buf[128];
    int len = af_settings_json_get(&s, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, "{\"store\":{\"raw\":0,\"agg\":1,\"env\":0},\"window_s\":1200,\"sigma_k\":3,\"ble_ext\":1}") == 0);
}

static void t_json_cap(void)
{
    af_settings_t s;
    af_settings_defaults(&s);

    char buf[74];
    for (int i = 0; i < 74; i++) buf[i] = (char)0x5A;
    int r = af_settings_json_get(&s, buf, 74);
    CHECK(r == 73);

    for (int i = 0; i < 74; i++) buf[i] = (char)0x5A;
    r = af_settings_json_get(&s, buf, 73);
    CHECK(r == AF_SET_E_CAP);
    for (int i = 0; i < 74; i++) CHECK(buf[i] == (char)0x5A);

    buf[0] = (char)0x5A;
    r = af_settings_json_get(&s, buf, 0);
    CHECK(r == AF_SET_E_CAP);

    buf[0] = (char)0x5A;
    r = af_settings_json_get(&s, buf, 1);
    CHECK(r == AF_SET_E_CAP);
    CHECK(buf[0] == (char)0x5A);

    char nc[57];
    for (int i = 0; i < 57; i++) nc[i] = (char)0x5A;
    r = af_settings_json_need_confirm(5, 60, nc, 56);
    CHECK(r == AF_SET_E_CAP);
    for (int i = 0; i < 57; i++) CHECK(nc[i] == (char)0x5A);

    char ok[28];
    for (int i = 0; i < 28; i++) ok[i] = (char)0x5A;
    r = af_settings_json_ok(5, ok, 27);
    CHECK(r == AF_SET_E_CAP);
    for (int i = 0; i < 28; i++) CHECK(ok[i] == (char)0x5A);
}

static void t_json_need_confirm_ok(void)
{
    char buf[128];
    int r = af_settings_json_need_confirm(5, 60, buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strcmp(buf, "{\"error\":\"need_confirm\",\"agg_count\":5,\"window_s_old\":60}") == 0);

    r = af_settings_json_ok(5, buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strcmp(buf, "{\"ok\":true,\"agg_cleared\":5}") == 0);

    r = af_settings_json_need_confirm(4294967295u, 60, buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strstr(buf, "4294967295") != NULL);
}

static void t_json_error(void)
{
    char buf[128];
    int r;

    r = af_settings_json_error(AF_SET_E_UNKNOWN_FIELD, "foo", buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strcmp(buf, "{\"error\":\"unknown_field\",\"field\":\"foo\"}") == 0);

    r = af_settings_json_error(AF_SET_E_BAD_VALUE, "window_s", buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strcmp(buf, "{\"error\":\"bad_value\",\"field\":\"window_s\"}") == 0);

    r = af_settings_json_error(AF_SET_E_SYNTAX, NULL, buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strcmp(buf, "{\"error\":\"syntax\"}") == 0);

    r = af_settings_json_error(AF_SET_E_BAD_VALUE, "a\"b", buf, sizeof(buf));
    CHECK(r > 0);
    CHECK(strstr(buf, "a_b") != NULL);

    r = af_settings_json_error(AF_SET_OK, "x", buf, sizeof(buf));
    CHECK(r == AF_SET_E_ARG);

    char eb[40];
    for (int i = 0; i < 40; i++) eb[i] = (char)0x5A;
    r = af_settings_json_error(AF_SET_E_UNKNOWN_FIELD, "foo", eb, 39);
    CHECK(r == AF_SET_E_CAP);
    for (int i = 0; i < 40; i++) CHECK(eb[i] == (char)0x5A);
}

static void t_roundtrip(void)
{
    const char *body = "{\"store\":{\"raw\":0,\"agg\":0,\"env\":1},\"window_s\":600,\"sigma_k\":2,\"ble_ext\":1}";
    af_settings_patch_t p;
    memset(&p, 0, sizeof(p));
    int r = af_settings_parse(body, strlen(body), &p);
    CHECK(r == AF_SET_OK);

    af_settings_t cur, out;
    af_settings_defaults(&cur);
    uint32_t agg_cleared = 0;
    r = af_settings_apply(&cur, &p, 0, &out, &agg_cleared);
    CHECK(r == AF_SET_OK);

    char buf[128];
    int len = af_settings_json_get(&out, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, body) == 0);
}

static void t_parse_window_reject(void)
{
    const char *b[] = {
        "{\"window_s\":59}",
        "{\"window_s\":61}",
        "{\"window_s\":90}",
        "{\"window_s\":1260}",
        "{\"window_s\":0}"
    };
    for (int i = 0; i < 5; i++) {
        af_settings_patch_t p;
        memset(&p, 0, sizeof p);
        CHECK(af_settings_parse(b[i], strlen(b[i]), &p) == AF_SET_E_BAD_VALUE);
        CHECK(strcmp(p.err_field, "window_s") == 0);
    }
}

static void t_parse_window_accept(void)
{
    struct { const char *s; uint16_t v; } cases[] = {
        { "{\"window_s\":60}",  60 },
        { "{\"window_s\":300}", 300 },
        { "{\"window_s\":1200}",1200 }
    };
    for (int i = 0; i < 3; i++) {
        af_settings_patch_t p;
        memset(&p, 0, sizeof p);
        CHECK(af_settings_parse(cases[i].s, strlen(cases[i].s), &p) == AF_SET_OK);
        CHECK(p.has_window == 1);
        CHECK(p.window_s == cases[i].v);
    }
}

static void t_parse_sigma(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"sigma_k\":0}", strlen("{\"sigma_k\":0}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "sigma_k") == 0);
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"sigma_k\":4}", strlen("{\"sigma_k\":4}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "sigma_k") == 0);
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"sigma_k\":2}", strlen("{\"sigma_k\":2}"), &p) == AF_SET_OK);
    CHECK(p.has_sigma == 1);
    CHECK(p.sigma_k == 2);
}

static void t_parse_unknown(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"foo\":1}", strlen("{\"foo\":1}"), &p) == AF_SET_E_UNKNOWN_FIELD);
    CHECK(strcmp(p.err_field, "foo") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":300,\"bar\":true}", strlen("{\"window_s\":300,\"bar\":true}"), &p) == AF_SET_E_UNKNOWN_FIELD);
    CHECK(strcmp(p.err_field, "bar") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"zzz\":1}}", strlen("{\"store\":{\"zzz\":1}}"), &p) == AF_SET_E_UNKNOWN_FIELD);
    CHECK(strcmp(p.err_field, "store.zzz") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window\":60}", strlen("{\"window\":60}"), &p) == AF_SET_E_UNKNOWN_FIELD);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_sx\":60}", strlen("{\"window_sx\":60}"), &p) == AF_SET_E_UNKNOWN_FIELD);
}

static void t_parse_partial(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"sigma_k\":3}", strlen("{\"sigma_k\":3}"), &p) == AF_SET_OK);
    CHECK(p.has_sigma == 1);
    CHECK(p.has_raw == 0);
    CHECK(p.has_agg == 0);
    CHECK(p.has_env == 0);
    CHECK(p.has_window == 0);
    CHECK(p.has_ble_ext == 0);
    CHECK(p.has_confirm == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"agg\":0}}", strlen("{\"store\":{\"agg\":0}}"), &p) == AF_SET_OK);
    CHECK(p.has_agg == 1);
    CHECK(p.store_agg == 0);
    CHECK(p.has_raw == 0);
    CHECK(p.has_env == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"ble_ext\":1}", strlen("{\"ble_ext\":1}"), &p) == AF_SET_OK);
    CHECK(p.has_ble_ext == 1);
    CHECK(p.ble_ext == 1);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{}", strlen("{}"), &p) == AF_SET_OK);
    CHECK(p.has_raw == 0 && p.has_agg == 0 && p.has_env == 0);
    CHECK(p.has_window == 0 && p.has_sigma == 0 && p.has_ble_ext == 0);
    CHECK(p.has_confirm == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("  { }  ", strlen("  { }  "), &p) == AF_SET_OK);
    CHECK(p.has_raw == 0 && p.has_agg == 0 && p.has_env == 0);
    CHECK(p.has_window == 0 && p.has_sigma == 0 && p.has_ble_ext == 0);
    CHECK(p.has_confirm == 0);
}

static void t_parse_store(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"raw\":0,\"agg\":1,\"env\":0}}",
                            strlen("{\"store\":{\"raw\":0,\"agg\":1,\"env\":0}}"), &p) == AF_SET_OK);
    CHECK(p.has_raw == 1 && p.store_raw == 0);
    CHECK(p.has_agg == 1 && p.store_agg == 1);
    CHECK(p.has_env == 1 && p.store_env == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"raw\":true,\"env\":false}}",
                            strlen("{\"store\":{\"raw\":true,\"env\":false}}"), &p) == AF_SET_OK);
    CHECK(p.has_raw == 1 && p.store_raw == 1);
    CHECK(p.has_env == 1 && p.store_env == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"raw\":2}}", strlen("{\"store\":{\"raw\":2}}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "store.raw") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":{\"env\":\"1\"}}", strlen("{\"store\":{\"env\":\"1\"}}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "store.env") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"store\":5}", strlen("{\"store\":5}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "store") == 0);
}

static void t_parse_flags(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"ble_ext\":2}", strlen("{\"ble_ext\":2}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "ble_ext") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"confirm_clear_agg\":true}", strlen("{\"confirm_clear_agg\":true}"), &p) == AF_SET_OK);
    CHECK(p.has_confirm == 1);
    CHECK(p.confirm_clear_agg == 1);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"confirm_clear_agg\":false}", strlen("{\"confirm_clear_agg\":false}"), &p) == AF_SET_OK);
    CHECK(p.has_confirm == 1);
    CHECK(p.confirm_clear_agg == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"confirm_clear_agg\":3}", strlen("{\"confirm_clear_agg\":3}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "confirm_clear_agg") == 0);
}

static void t_parse_types(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":\"300\"}", strlen("{\"window_s\":\"300\"}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "window_s") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":true}", strlen("{\"window_s\":true}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "window_s") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"sigma_k\":false}", strlen("{\"sigma_k\":false}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "sigma_k") == 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":300.5}", strlen("{\"window_s\":300.5}"), &p) < 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":-60}", strlen("{\"window_s\":-60}"), &p) < 0);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":123456}", strlen("{\"window_s\":123456}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "window_s") == 0);
}

static void t_parse_syntax(void)
{
    af_settings_patch_t p;
    const char *bad[] = {
        "",
        "{",
        "{\"window_s\"",
        "{\"window_s\":}",
        "{\"window_s\":60",
        "{\"window_s\":60}x",
        "[\"window_s\"]",
        "{'window_s':60}",
        "{\"window_s\" 60}"
    };
    for (int i = 0; i < 9; i++) {
        memset(&p, 0, sizeof p);
        CHECK(af_settings_parse(bad[i], strlen(bad[i]), &p) == AF_SET_E_SYNTAX);
    }

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse(NULL, 0, &p) == AF_SET_E_ARG);

    CHECK(af_settings_parse("{\"window_s\":60}", strlen("{\"window_s\":60}"), NULL) == AF_SET_E_ARG);
}

static void t_parse_last_wins(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":300,\"window_s\":600}",
                            strlen("{\"window_s\":300,\"window_s\":600}"), &p) == AF_SET_OK);
    CHECK(p.has_window == 1);
    CHECK(p.window_s == 600);

    memset(&p, 0, sizeof p);
    CHECK(af_settings_parse("{\"window_s\":59,\"foo\":1}",
                            strlen("{\"window_s\":59,\"foo\":1}"), &p) == AF_SET_E_BAD_VALUE);
    CHECK(strcmp(p.err_field, "window_s") == 0);
}

static void t_parse_ws_len(void)
{
    af_settings_patch_t p;
    memset(&p, 0, sizeof p);
    const char *ws = "  { \"window_s\" : 300 }  ";
    CHECK(af_settings_parse(ws, strlen(ws), &p) == AF_SET_OK);
    CHECK(p.has_window == 1);
    CHECK(p.window_s == 300);

    memset(&p, 0, sizeof p);
    const char *tab = "{\t\"window_s\"\t:\t300\t}";
    CHECK(af_settings_parse(tab, strlen(tab), &p) == AF_SET_OK);
    CHECK(p.window_s == 300);

    memset(&p, 0, sizeof p);
    const char *nl = "{\n\"window_s\"\n:\n300\n}";
    CHECK(af_settings_parse(nl, strlen(nl), &p) == AF_SET_OK);
    CHECK(p.window_s == 300);

    /* Buffer contains the full 16-char string + NUL, but len=15 excludes the closing '}'.
       The parser must honour len, not the NUL terminator. */
    memset(&p, 0, sizeof p);
    char buf[32];
    memcpy(buf, "{\"window_s\":300}", 17);
    CHECK(af_settings_parse(buf, 15, &p) == AF_SET_E_SYNTAX);
}

static void t_apply_no_change(void) {
    af_settings_t cur, out;
    af_settings_patch_t p;
    uint32_t ac = 99;
    af_settings_defaults(&cur);
    CHECK(af_settings_parse("{\"sigma_k\":2}", strlen("{\"sigma_k\":2}"), &p) == AF_SET_OK);
    CHECK(af_settings_apply(&cur, &p, 100, &out, &ac) == AF_SET_OK);
    CHECK(out.sigma_k == 2);
    CHECK(out.store_raw == 1);
    CHECK(out.store_agg == 1);
    CHECK(out.store_env == 1);
    CHECK(out.window_s == 60);
    CHECK(out.ble_ext == 0);
    CHECK(ac == 0);
}

static void t_apply_need_confirm(void) {
    af_settings_t cur, out;
    af_settings_patch_t p;
    uint32_t ac = 99;
    af_settings_defaults(&cur);
    CHECK(af_settings_parse("{\"window_s\":300}", strlen("{\"window_s\":300}"), &p) == AF_SET_OK);
    CHECK(af_settings_apply(&cur, &p, 5, &out, &ac) == AF_SET_NEED_CONFIRM);
    CHECK(out.store_raw == cur.store_raw);
    CHECK(out.store_agg == cur.store_agg);
    CHECK(out.store_env == cur.store_env);
    CHECK(out.window_s == cur.window_s);
    CHECK(out.window_s == 60);
    CHECK(out.sigma_k == cur.sigma_k);
    CHECK(out.ble_ext == cur.ble_ext);
    CHECK(ac == 0);
    CHECK(af_settings_parse("{\"window_s\":300,\"sigma_k\":3}", strlen("{\"window_s\":300,\"sigma_k\":3}"), &p) == AF_SET_OK);
    ac = 99;
    CHECK(af_settings_apply(&cur, &p, 5, &out, &ac) == AF_SET_NEED_CONFIRM);
    CHECK(out.sigma_k == 1);
    CHECK(out.window_s == 60);
    CHECK(ac == 0);
}

static void t_apply_agg_cleared(void) {
    af_settings_t cur, out;
    af_settings_patch_t p;
    uint32_t ac = 99;
    af_settings_defaults(&cur);
    CHECK(af_settings_parse("{\"window_s\":300,\"confirm_clear_agg\":true}", strlen("{\"window_s\":300,\"confirm_clear_agg\":true}"), &p) == AF_SET_OK);
    CHECK(af_settings_apply(&cur, &p, 5, &out, &ac) == AF_SET_OK);
    CHECK(out.window_s == 300);
    CHECK(ac == 5);
    ac = 99;
    CHECK(af_settings_apply(&cur, &p, 0, &out, &ac) == AF_SET_OK);
    CHECK(out.window_s == 300);
    CHECK(ac == 0);
    CHECK(af_settings_parse("{\"window_s\":300}", strlen("{\"window_s\":300}"), &p) == AF_SET_OK);
    ac = 99;
    CHECK(af_settings_apply(&cur, &p, 0, &out, &ac) == AF_SET_OK);
    CHECK(out.window_s == 300);
    CHECK(ac == 0);
}

static void t_apply_same_window(void) {
    af_settings_t cur, out;
    af_settings_patch_t p;
    uint32_t ac = 99;
    af_settings_defaults(&cur);
    cur.window_s = 300;
    CHECK(af_settings_parse("{\"window_s\":300}", strlen("{\"window_s\":300}"), &p) == AF_SET_OK);
    CHECK(af_settings_apply(&cur, &p, 9, &out, &ac) == AF_SET_OK);
    CHECK(ac == 0);
}

static void t_apply_args(void) {
    af_settings_t cur, out;
    af_settings_patch_t p;
    uint32_t ac = 0;
    af_settings_defaults(&cur);
    CHECK(af_settings_parse("{\"sigma_k\":2}", strlen("{\"sigma_k\":2}"), &p) == AF_SET_OK);
    CHECK(af_settings_apply(NULL, &p, 0, &out, &ac) == AF_SET_E_ARG);
    CHECK(af_settings_apply(&cur, NULL, 0, &out, &ac) == AF_SET_E_ARG);
    CHECK(af_settings_apply(&cur, &p, 0, NULL, &ac) == AF_SET_E_ARG);
    CHECK(af_settings_apply(&cur, &p, 0, &out, NULL) == AF_SET_E_ARG);
}

int main(void) {
    RUN(t_window_valid_reject);
    RUN(t_window_valid_accept);
    RUN(t_sigma_valid);
    RUN(t_defaults);
    RUN(t_sanitize_ok);
    RUN(t_sanitize_replace);
    RUN(t_parse_window_reject);
    RUN(t_parse_window_accept);
    RUN(t_parse_sigma);
    RUN(t_parse_unknown);
    RUN(t_parse_partial);
    RUN(t_parse_store);
    RUN(t_parse_flags);
    RUN(t_parse_types);
    RUN(t_parse_syntax);
    RUN(t_parse_last_wins);
    RUN(t_parse_ws_len);
    RUN(t_apply_no_change);
    RUN(t_apply_need_confirm);
    RUN(t_apply_agg_cleared);
    RUN(t_apply_same_window);
    RUN(t_apply_args);
    RUN(t_json_get_defaults);
    RUN(t_json_get_values);
    RUN(t_json_cap);
    RUN(t_json_need_confirm_ok);
    RUN(t_json_error);
    RUN(t_roundtrip);
    TESTS_DONE();
}
