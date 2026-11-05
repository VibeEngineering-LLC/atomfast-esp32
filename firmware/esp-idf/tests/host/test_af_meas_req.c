#include "af_test.h"
#include "af_meas.h"
#include "af_meas_req.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

#define PS(body, p) af_mreq_parse_start((body), strlen(body), &(p))

static void t_req_body(void) {
    CHECK(af_mreq_body_ok("{\"n\":1}", strlen("{\"n\":1}")) == 1);
    CHECK(af_mreq_body_ok("{\"a\":\t1\n}\r", strlen("{\"a\":\t1\n}\r")) == 1);
    CHECK(af_mreq_body_ok("{\"name\":\"\xD0\xA4\"}", strlen("{\"name\":\"\xD0\xA4\"}")) == 1);

    CHECK(af_mreq_body_ok("", strlen("")) == 0);
    CHECK(af_mreq_body_ok(NULL, 0) == 0);
    CHECK(af_mreq_body_ok("{\"n\":\x01}", strlen("{\"n\":\x01}")) == 0);
    char nul_body[3] = {'{', '\0', '}'};
    CHECK(af_mreq_body_ok(nul_body, 3) == 0);
    CHECK(af_mreq_body_ok("{\"n\":\x7f}", strlen("{\"n\":\x7f}")) == 0);
    CHECK(af_mreq_body_ok("{\"a\":\"\\u0041\"}", strlen("{\"a\":\"\\u0041\"}")) == 0);
    CHECK(af_mreq_body_ok("\xC0\x80", strlen("\xC0\x80")) == 0);
    CHECK(af_mreq_body_ok("\xD0", strlen("\xD0")) == 0);
    CHECK(af_mreq_body_ok("\xFF", strlen("\xFF")) == 0);
    CHECK(af_mreq_body_ok("\xE0\x80\x80", strlen("\xE0\x80\x80")) == 0);
}

static void t_req_start_ok(void) {
    af_meas_params_t p;
    int rc;

    // (a) Пустой объект
    rc = PS("{}", p);
    CHECK(rc == 0);
    CHECK(p.n == 1);
    CHECK(p.t_s == 600);
    CHECK(p.eps_x100 == 100);
    CHECK(p.retry_invalid == 1);
    CHECK(p.lost_max == 2);
    CHECK(p.stop_mode == AF_MEAS_STOP_TIME);
    CHECK(p.start_mode == AF_MEAS_START_NOW);
    CHECK(strcmp(p.prefix, "AtomFast") == 0);
    CHECK(strlen(p.name) == 0);

    // (b) Кириллица UTF-8
    rc = PS("{\"n\":5,\"t_s\":60,\"pause_s\":10,\"name\":\"\xD0\xA4\xD0\xBE\xD0\xBD\"}", p);
    CHECK(rc == 0);
    CHECK(p.n == 5);
    CHECK(p.t_s == 60);
    CHECK(p.pause_s == 10);
    const char *expected_name = "\xD0\xA4\xD0\xBE\xD0\xBD";
    CHECK(memcmp(p.name, expected_name, 6) == 0);

    // (c) Полный набор полей
    const char *body_c = "{\"name\":\"A\",\"n\":3,\"t_s\":120,\"pause_s\":5,\"retry_invalid\":false,\"lost_max\":4,\"stop_mode\":\"time\",\"eps_pct\":2.5,\"t_min_s\":20,\"t_max_s\":900,\"start_mode\":\"delay\",\"delay_s\":30,\"file_store\":true,\"name_template\":\"{prefix}_{date}\",\"prefix\":\"Bg\",\"no_overwrite\":false}";
    rc = PS(body_c, p);
    CHECK(rc == 0);
    CHECK(p.n == 3);
    CHECK(p.t_s == 120);
    CHECK(p.pause_s == 5);
    CHECK(p.retry_invalid == 0);
    CHECK(p.lost_max == 4);
    CHECK(p.stop_mode == AF_MEAS_STOP_TIME);
    CHECK(p.eps_x100 == 250);
    CHECK(p.t_min_s == 20);
    CHECK(p.t_max_s == 900);
    CHECK(p.start_mode == AF_MEAS_START_DELAY);
    CHECK(p.delay_s == 30);
    CHECK(p.file_store == 1);
    CHECK(p.no_overwrite == 0);
    CHECK(strcmp(p.prefix, "Bg") == 0);
    CHECK(strcmp(p.name_tpl, "{prefix}_{date}") == 0);
    CHECK(strcmp(p.name, "A") == 0);

    // (d) endless
    rc = PS("{\"endless\":true}", p);
    CHECK(rc == 0);
    CHECK(p.n == 0);

    rc = PS("{\"endless\":true,\"n\":0}", p);
    CHECK(rc == 0);
    CHECK(p.n == 0);

    rc = PS("{\"endless\":false,\"n\":4}", p);
    CHECK(rc == 0);
    CHECK(p.n == 4);

    // (e) stop_mode sigma, start_mode at
    rc = PS("{\"stop_mode\":\"sigma\",\"n\":1}", p);
    CHECK(rc == 0);
    CHECK(p.stop_mode == AF_MEAS_STOP_SIGMA);

    rc = PS("{\"start_mode\":\"at\",\"start_at\":1792860000}", p);
    CHECK(rc == 0);
    CHECK(p.start_mode == AF_MEAS_START_AT);
    CHECK(p.start_at == 1792860000);

    // (f) Границы
    rc = PS("{\"n\":1000}", p);
    CHECK(rc == 0);

    rc = PS("{\"n\":0}", p);
    CHECK(rc == 0);
    CHECK(p.n == 0);

    rc = PS("{\"t_s\":3600000}", p);
    CHECK(rc == 0);
}

static void t_req_start_err(void) {
    af_meas_params_t p;
    int rc;

    rc = PS("{\"zzz\":1}", p);
    CHECK(rc == AF_MEAS_E_KEY);

    rc = PS("{\"n\":1,\"n\":2}", p);
    CHECK(rc == AF_MEAS_E_KEY);

    rc = PS("[1]", p);
    CHECK(rc == AF_MEAS_E_JSON);

    rc = PS("{", p);
    CHECK(rc == AF_MEAS_E_JSON);

    rc = PS("{\"n\":\"5\"}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"n\":1.5}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"n\":-1}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"n\":1001}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"t_s\":0}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"t_s\":3600001}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"retry_invalid\":1}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"stop_mode\":\"x\"}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"start_mode\":1}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"lost_max\":256}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"start_at\":4294967296}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"endless\":true,\"n\":3}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    // Имя из 33 символов 'a'
    char body_name[128];
    strcpy(body_name, "{\"name\":\"");
    for (int i = 0; i < 33; i++) strcat(body_name, "a");
    strcat(body_name, "\"}");
    rc = PS(body_name, p);
    CHECK(rc == AF_MEAS_E_VALUE);

    // Имя из 32 символов 'a'
    strcpy(body_name, "{\"name\":\"");
    for (int i = 0; i < 32; i++) strcat(body_name, "a");
    strcat(body_name, "\"}");
    rc = PS(body_name, p);
    CHECK(rc == 0);

    // Префикс из 17 символов
    strcpy(body_name, "{\"prefix\":\"");
    for (int i = 0; i < 17; i++) strcat(body_name, "a");
    strcat(body_name, "\"}");
    rc = PS(body_name, p);
    CHECK(rc == AF_MEAS_E_VALUE);

    // name_template из 49 символов
    strcpy(body_name, "{\"name_template\":\"");
    for (int i = 0; i < 49; i++) strcat(body_name, "a");
    strcat(body_name, "\"}");
    rc = PS(body_name, p);
    CHECK(rc == AF_MEAS_E_VALUE);

    // Пустое тело
    rc = af_mreq_parse_start("", 0, &p);
    CHECK(rc == AF_MEAS_E_BODY);

    // Тело с байтом 0x01
    char bad_body[8] = {'{', '"', 'n', '"', ':', '\x01', '}', '\0'};
    rc = af_mreq_parse_start(bad_body, 7, &p);
    CHECK(rc == AF_MEAS_E_BODY);

    // Тело с обратной косой
    char backslash_body[8] = {'{', '"', 'a', '"', ':', '\\', '}', '\0'};
    rc = af_mreq_parse_start(backslash_body, 7, &p);
    CHECK(rc == AF_MEAS_E_BODY);

    // NULL указатель на параметры
    rc = af_mreq_parse_start("{}", 2, NULL);
    CHECK(rc == AF_MEAS_E_BODY);
}

static void t_req_eps(void) {
    af_meas_params_t p;
    int rc;

    rc = PS("{\"eps_pct\":0.1}", p);
    CHECK(rc == 0);
    CHECK(p.eps_x100 == 10);

    rc = PS("{\"eps_pct\":50}", p);
    CHECK(rc == 0);
    CHECK(p.eps_x100 == 5000);

    rc = PS("{\"eps_pct\":100}", p);
    CHECK(rc == 0);
    CHECK(p.eps_x100 == 10000);

    rc = PS("{\"eps_pct\":1}", p);
    CHECK(rc == 0);
    CHECK(p.eps_x100 == 100);

    rc = PS("{\"eps_pct\":0.15}", p);
    CHECK(rc == 0);
    CHECK(p.eps_x100 == 15);

    rc = PS("{\"eps_pct\":0.155}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"eps_pct\":0.001}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"eps_pct\":0}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"eps_pct\":-1}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"eps_pct\":100.01}", p);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = PS("{\"eps_pct\":\"1\"}", p);
    CHECK(rc == AF_MEAS_E_VALUE);
}

static void t_req_id(void) {
    uint32_t id;
    int rc;

    rc = af_mreq_parse_id(NULL, 0, 0, &id);
    CHECK(rc == 0);
    CHECK(id == 0);

    rc = af_mreq_parse_id("", strlen(""), 1, &id);
    CHECK(rc == AF_MEAS_E_BODY);

    rc = af_mreq_parse_id("{\"id\":5}", strlen("{\"id\":5}"), 0, &id);
    CHECK(rc == 0);
    CHECK(id == 5);

    rc = af_mreq_parse_id("{\"id\":4294967295}", strlen("{\"id\":4294967295}"), 0, &id);
    CHECK(rc == 0);
    CHECK(id == 4294967295u);

    rc = af_mreq_parse_id("{\"id\":0}", strlen("{\"id\":0}"), 0, &id);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = af_mreq_parse_id("{\"id\":-1}", strlen("{\"id\":-1}"), 0, &id);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = af_mreq_parse_id("{\"id\":\"5\"}", strlen("{\"id\":\"5\"}"), 0, &id);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = af_mreq_parse_id("{\"id\":1.5}", strlen("{\"id\":1.5}"), 0, &id);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = af_mreq_parse_id("{\"x\":1}", strlen("{\"x\":1}"), 0, &id);
    CHECK(rc == AF_MEAS_E_KEY);

    rc = af_mreq_parse_id("{\"id\":1,\"id\":2}", strlen("{\"id\":1,\"id\":2}"), 0, &id);
    CHECK(rc == AF_MEAS_E_KEY);

    rc = af_mreq_parse_id("{}", strlen("{}"), 1, &id);
    CHECK(rc == AF_MEAS_E_VALUE);

    rc = af_mreq_parse_id("{}", strlen("{}"), 0, &id);
    CHECK(rc == 0);

    rc = af_mreq_parse_id("[]", strlen("[]"), 0, &id);
    CHECK(rc == AF_MEAS_E_JSON);

    char bad_body[8] = {'{', '"', 'i', 'd', '"', ':', '\x01', '\0'};
    rc = af_mreq_parse_id(bad_body, 7, 0, &id);
    CHECK(rc == AF_MEAS_E_BODY);
}

static void t_req_query(void) {
    uint32_t val;
    int rc;

    rc = af_mreq_query_u32(NULL, "id", &val);
    CHECK(rc == 0);

    rc = af_mreq_query_u32("", "id", &val);
    CHECK(rc == 0);

    rc = af_mreq_query_u32("id=5", "id", &val);
    CHECK(rc == 1);
    CHECK(val == 5);

    rc = af_mreq_query_u32("a=1&id=22&b=3", "id", &val);
    CHECK(rc == 1);
    CHECK(val == 22);

    rc = af_mreq_query_u32("idx=5", "id", &val);
    CHECK(rc == 0);

    rc = af_mreq_query_u32("id=", "id", &val);
    CHECK(rc == -1);

    rc = af_mreq_query_u32("id=abc", "id", &val);
    CHECK(rc == -1);

    rc = af_mreq_query_u32("id=4294967295", "id", &val);
    CHECK(rc == 1);
    CHECK(val == 4294967295u);

    rc = af_mreq_query_u32("id=4294967296", "id", &val);
    CHECK(rc == -1);

    rc = af_mreq_query_u32("id=99999999999", "id", &val);
    CHECK(rc == -1);

    rc = af_mreq_query_u32("id=5&id=7", "id", &val);
    CHECK(rc == 1);
    CHECK(val == 5);

    rc = af_mreq_query_u32("limit=10&id=3", "limit", &val);
    CHECK(rc == 1);
    CHECK(val == 10);
}

int main(void) {
    RUN(t_req_body);
    RUN(t_req_start_ok);
    RUN(t_req_start_err);
    RUN(t_req_eps);
    RUN(t_req_id);
    RUN(t_req_query);
    TESTS_DONE();
    return 0;
}
