#include "af_test.h"
#include "af_sens_req.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static int P(const char *s, uint8_t primary, af_sens_cmd_t *c) {
    return af_sens_parse(s, strlen(s), primary, c);
}

static void t_body_ok(void) {
    CHECK(af_sens_body_ok("{\"a\":1}\n", 8) == 1);
    CHECK(af_sens_body_ok("", 0) == 0);
    CHECK(af_sens_body_ok(NULL, 0) == 0);
    
    char nul_body[] = {'{', '\0', '}', 0};
    CHECK(af_sens_body_ok(nul_body, 3) == 0);
    
    char b1[] = {0x01, 'a', 0};
    CHECK(af_sens_body_ok(b1, 2) == 0);
    
    char b1f[] = {0x1F, 'a', 0};
    CHECK(af_sens_body_ok(b1f, 2) == 0);
    
    char b7f[] = {0x7F, 'a', 0};
    CHECK(af_sens_body_ok(b7f, 2) == 0);
    
    char utf8[] = {0xD0, 0x80, 0};
    CHECK(af_sens_body_ok(utf8, 2) == 0);
    
    char backslash[] = "{\"\\u0000\"}";
    CHECK(af_sens_body_ok(backslash, strlen(backslash)) == 0);
    
    char tab_nl[] = "{\t\n}";
    CHECK(af_sens_body_ok(tab_nl, 4) == 1);
}

static void t_scan(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    CHECK(P("{\"action\":\"scan\"}", 1, &c) == 0);
    CHECK(c.kind == AF_SENS_CMD_SCAN);
    
    CHECK(P("{\"action\":\"scan\",\"sensor\":1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"scan\",\"x\":1}", 1, &c) == AF_SENS_E_KEY);
    CHECK(P("{\"action\":1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"reboot\"}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P(" { \"action\" : \"scan\" } ", 1, &c) == 0);
}

static void t_add(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    
    CHECK(P("{\"action\":\"add\",\"model\":\"sht3x\",\"addr\":68}", 1, &c) == 0);
    CHECK(c.kind == AF_SENS_CMD_ADD);
    CHECK(c.model == AF_SNS_SHT3X);
    CHECK(c.addr == 0x44);
    CHECK(c.enabled == 1);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"sht3x\",\"addr\":\"0x44\"}", 1, &c) == 0);
    CHECK(c.addr == 0x44);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"sht3x\",\"addr\":\"0X45\"}", 1, &c) == 0);
    CHECK(c.addr == 0x45);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":\"0x77\",\"enabled\":false}", 1, &c) == 0);
    CHECK(c.enabled == 0);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":68}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":128}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":-1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":68.5}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"aht2x\",\"addr\":\"0x38\"}", 1, &c) == 0);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":\"44\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":\"0x444\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":\"0xZZ\"}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"BMP280\",\"addr\":118}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp180\",\"addr\":118}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":1,\"addr\":118}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"addr\":118}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":118,\"enabled\":\"true\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":118,\"enabled\":1}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":118,\"t_off\":1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"add\",\"model\":\"bmp280\",\"addr\":118,\"period_s\":10}", 1, &c) == AF_SENS_E_VALUE);
}

static void t_remove(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    
    CHECK(P("{\"action\":\"remove\",\"sensor\":3}", 1, &c) == 0);
    CHECK(c.kind == AF_SENS_CMD_REMOVE);
    CHECK(c.sensor == 3);
    
    CHECK(P("{\"action\":\"remove\",\"sensor\":0}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"remove\",\"sensor\":9}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"remove\",\"sensor\":2.5}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"remove\",\"sensor\":\"3\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"remove\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"action\":\"remove\",\"sensor\":1,\"enabled\":true}", 1, &c) == AF_SENS_E_VALUE);
}

static void t_patch(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    
    CHECK(P("{\"sensor\":2,\"enabled\":false,\"t_off\":0.3,\"p_off\":-2.5,\"h_off\":1,\"period_s\":60,\"agg_s\":10}", 1, &c) == 0);
    CHECK(c.kind == AF_SENS_CMD_PATCH);
    CHECK(c.sensor == 2);
    CHECK(c.has_patch == 1);
    CHECK(c.has_agg == 1);
    CHECK(c.agg_s == 10);
    CHECK(c.patch.has_enabled == 1);
    CHECK(c.patch.enabled == 0);
    CHECK(fabsf(c.patch.t_off - 0.3f) < 1e-6f);
    CHECK(fabsf(c.patch.p_off - (-2.5f)) < 1e-6f);
    CHECK(fabsf(c.patch.h_off - 1.0f) < 1e-6f);
    CHECK(c.patch.period_s == 60);
    
    CHECK(P("{\"sensor\":1,\"t_off\":5}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"t_off\":-5}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"t_off\":5.01}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"p_off\":20}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"p_off\":-20}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"p_off\":20.01}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"h_off\":10}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"h_off\":-10}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"h_off\":10.01}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"period_s\":5}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"period_s\":600}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"period_s\":4}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"period_s\":601}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"period_s\":5.5}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"agg_s\":1}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"agg_s\":60}", 1, &c) == 0);
    CHECK(P("{\"sensor\":1,\"agg_s\":0}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"agg_s\":61}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"enabled\":true}", 1, &c) == 0);
    CHECK(P("{\"sensor\":8,\"enabled\":true}", 1, &c) == 0);
    CHECK(P("{\"sensor\":0,\"enabled\":true}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":9,\"enabled\":true}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"t_off\":\"0.3\"}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"enabled\":1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"enabled\":\"true\"}", 1, &c) == AF_SENS_E_VALUE);
    
    CHECK(P("{\"sensor\":1,\"period_s\":1e999}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1,\"t_off\":1e400}", 1, &c) != 0);
}

static void t_patch_legacy(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    
    CHECK(P("{\"enabled\":true,\"t_off\":0,\"p_off\":0,\"agg_s\":5,\"period_s\":30}", 1, &c) == 0);
    CHECK(c.sensor == 1);
    CHECK(c.has_patch == 1);
    CHECK(c.has_agg == 1);
    
    CHECK(P("{\"enabled\":true,\"t_off\":0,\"p_off\":0,\"agg_s\":5,\"period_s\":30}", 0, &c) == AF_SENS_E_NOSENSOR);
    
    CHECK(P("{\"agg_s\":7}", 0, &c) == 0);
    CHECK(c.has_patch == 0);
    CHECK(c.has_agg == 1);
    CHECK(c.sensor == 0);
    
    CHECK(P("{}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"sensor\":1}", 1, &c) == AF_SENS_E_VALUE);
    CHECK(P("{\"model\":\"bmp280\",\"enabled\":true}", 1, &c) == AF_SENS_E_VALUE);
}

static void t_json_errors(void) {
    af_sens_cmd_t c;
    memset(&c, 0, sizeof(c));
    
    CHECK(P("", 1, &c) == AF_SENS_E_BODY);
    CHECK(P("{", 1, &c) == AF_SENS_E_JSON);
    CHECK(P("[]", 1, &c) == AF_SENS_E_JSON);
    CHECK(P("5", 1, &c) == AF_SENS_E_JSON);
    CHECK(P("null", 1, &c) == AF_SENS_E_JSON);
    CHECK(P("\"x\"", 1, &c) == AF_SENS_E_JSON);
    
    CHECK(P("{\"sensor\":1,\"sensor\":2}", 1, &c) == AF_SENS_E_KEY);
    CHECK(P("{\"unknown\":1}", 1, &c) == AF_SENS_E_KEY);
    
    char nul_body[] = {'{', '\0', '}', 0};
    CHECK(af_sens_parse(nul_body, 3, 1, &c) == AF_SENS_E_BODY);
    
    char b1[] = {0x01, 'a', 0};
    CHECK(af_sens_parse(b1, 2, 1, &c) == AF_SENS_E_BODY);
    
    CHECK(af_sens_parse("{}", 2, 1, NULL) == AF_SENS_E_BODY);
}

static void t_cmd_zeroed(void) {
    af_sens_cmd_t c;
    memset(&c, 0xFF, sizeof(c));
    
    CHECK(P("{\"unknown\":1}", 1, &c) == AF_SENS_E_KEY);
    
    af_sens_cmd_t zero;
    memset(&zero, 0, sizeof(zero));
    CHECK(memcmp(&c, &zero, sizeof(c)) == 0);
}

static void t_parse_model(void) {
    uint8_t model = 0;
    uint8_t enabled = 0;
    
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":false}", strlen("{\"model\":\"bmp280\",\"enabled\":false}"), &model, &enabled) == 0);
    CHECK(model == AF_SNS_BMP280);
    CHECK(enabled == 0);
    
    CHECK(af_sens_parse_model("{\"model\":\"sht3x\",\"enabled\":true}", strlen("{\"model\":\"sht3x\",\"enabled\":true}"), &model, &enabled) == 0);
    CHECK(model == AF_SNS_SHT3X);
    CHECK(enabled == 1);
    
    CHECK(af_sens_parse_model("{\"model\":\"bme280\",\"enabled\":true}", strlen("{\"model\":\"bme280\",\"enabled\":true}"), &model, &enabled) == 0);
    CHECK(model == AF_SNS_BME280);
    
    CHECK(af_sens_parse_model("{\"model\":\"aht2x\",\"enabled\":true}", strlen("{\"model\":\"aht2x\",\"enabled\":true}"), &model, &enabled) == 0);
    CHECK(model == AF_SNS_AHT2X);
    
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\"}", strlen("{\"model\":\"bmp280\"}"), &model, &enabled) == AF_SENS_E_VALUE);
    CHECK(af_sens_parse_model("{\"enabled\":true}", strlen("{\"enabled\":true}"), &model, &enabled) == AF_SENS_E_VALUE);
    
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":\"false\"}", strlen("{\"model\":\"bmp280\",\"enabled\":\"false\"}"), &model, &enabled) == AF_SENS_E_VALUE);
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":0}", strlen("{\"model\":\"bmp280\",\"enabled\":0}"), &model, &enabled) == AF_SENS_E_VALUE);
    
    CHECK(af_sens_parse_model("{\"model\":\"BMP280\",\"enabled\":true}", strlen("{\"model\":\"BMP280\",\"enabled\":true}"), &model, &enabled) == AF_SENS_E_VALUE);
    CHECK(af_sens_parse_model("{\"model\":\"bmp180\",\"enabled\":true}", strlen("{\"model\":\"bmp180\",\"enabled\":true}"), &model, &enabled) == AF_SENS_E_VALUE);
    CHECK(af_sens_parse_model("{\"model\":1,\"enabled\":true}", strlen("{\"model\":1,\"enabled\":true}"), &model, &enabled) == AF_SENS_E_VALUE);
    
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":true,\"addr\":1}", strlen("{\"model\":\"bmp280\",\"enabled\":true,\"addr\":1}"), &model, &enabled) == AF_SENS_E_KEY);
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"model\":\"bmp280\",\"enabled\":true}", strlen("{\"model\":\"bmp280\",\"model\":\"bmp280\",\"enabled\":true}"), &model, &enabled) == AF_SENS_E_KEY);
    
    CHECK(af_sens_parse_model("[]", 2, &model, &enabled) == AF_SENS_E_JSON);
    CHECK(af_sens_parse_model("", 0, &model, &enabled) == AF_SENS_E_BODY);
    
    model = 77;
    enabled = 77;
    CHECK(af_sens_parse_model("{\"model\":\"bmp180\",\"enabled\":true}", strlen("{\"model\":\"bmp180\",\"enabled\":true}"), &model, &enabled) == AF_SENS_E_VALUE);
    CHECK(model == 77);
    CHECK(enabled == 77);
    
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":true}", strlen("{\"model\":\"bmp280\",\"enabled\":true}"), NULL, &enabled) == AF_SENS_E_BODY);
    CHECK(af_sens_parse_model("{\"model\":\"bmp280\",\"enabled\":true}", strlen("{\"model\":\"bmp280\",\"enabled\":true}"), &model, NULL) == AF_SENS_E_BODY);
}

int main(void) {
    RUN(t_body_ok);
    RUN(t_scan);
    RUN(t_add);
    RUN(t_remove);
    RUN(t_patch);
    RUN(t_patch_legacy);
    RUN(t_json_errors);
    RUN(t_cmd_zeroed);
    RUN(t_parse_model);
    TESTS_DONE();
    return 0;
}
