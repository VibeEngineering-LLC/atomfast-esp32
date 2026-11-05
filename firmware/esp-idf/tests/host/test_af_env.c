#include "af_test.h"
#include "af_env.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static void put16(uint8_t *d, int v) { d[0]=v&0xFF; d[1]=(v>>8)&0xFF; }

static void t_encode_decode_roundtrip(void) {
    af_env_rec_t r = { .ts = 1761300000, .t_c100 = 2650, .p_pa = 100218, .flags = 3 };
    uint8_t out[14];
    af_env_encode(&r, out);
    
    CHECK(out[0] == (uint8_t)(1761300000 & 0xFF));
    CHECK(out[1] == (uint8_t)((1761300000 >> 8) & 0xFF));
    CHECK(out[2] == (uint8_t)((1761300000 >> 16) & 0xFF));
    CHECK(out[3] == (uint8_t)((1761300000 >> 24) & 0xFF));
    CHECK(out[10] == 3);
    CHECK(out[11] == 0);
    CHECK((out[12] | (out[13] << 8)) == af_crc16(out, 12));

    af_env_rec_t dec;
    CHECK(af_env_decode(out, &dec) == 0);
    CHECK(dec.ts == 1761300000);
    CHECK(dec.t_c100 == 2650);
    CHECK(dec.p_pa == 100218);
    CHECK(dec.flags == 3);

    af_env_rec_t r_neg = { .ts = 1761300000, .t_c100 = -1234, .p_pa = 100218, .flags = 3 };
    af_env_encode(&r_neg, out);
    CHECK(af_env_decode(out, &dec) == 0);
    CHECK(dec.t_c100 == -1234);

    af_env_rec_t r_t_only = { .ts = 1761300000, .t_c100 = -550, .p_pa = 0, .flags = AF_ENV_F_T };
    af_env_encode(&r_t_only, out);
    CHECK(af_env_decode(out, &dec) == 0);
    CHECK(dec.t_c100 == -550);
    CHECK(dec.p_pa == 0);
}

static void t_decode_errors(void) {
    af_env_rec_t r = { .ts = 1761300000, .t_c100 = 2650, .p_pa = 100218, .flags = 3 };
    uint8_t out[14];
    af_env_encode(&r, out);
    af_env_rec_t dec;

    out[4] ^= 0xFF;
    CHECK(af_env_decode(out, &dec) == -1);

    af_env_encode(&r, out);
    out[11] = 1;
    uint16_t crc = af_crc16(out, 12);
    out[12] = crc & 0xFF;
    out[13] = (crc >> 8) & 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(out, &dec) == -2);
    CHECK(dec.ts == 77);

    af_env_encode(&r, out);
    out[10] = 0x10 | 3;
    crc = af_crc16(out, 12);
    out[12] = crc & 0xFF;
    out[13] = (crc >> 8) & 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(out, &dec) == -3);
    CHECK(dec.ts == 77);

    af_env_encode(&r, out);
    out[10] = 0;
    crc = af_crc16(out, 12);
    out[12] = crc & 0xFF;
    out[13] = (crc >> 8) & 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(out, &dec) == -4);
    CHECK(dec.ts == 77);

    af_env_encode(&r, out);
    out[10] = AF_ENV_F_P;
    crc = af_crc16(out, 12);
    out[12] = crc & 0xFF;
    out[13] = (crc >> 8) & 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(out, &dec) == -5);
    CHECK(dec.ts == 77);

    af_env_encode(&r, out);
    out[10] = AF_ENV_F_T;
    crc = af_crc16(out, 12);
    out[12] = crc & 0xFF;
    out[13] = (crc >> 8) & 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(out, &dec) == -5);
    CHECK(dec.ts == 77);
}

static void t_csv_row(void) {
    char buf[256];
    int ret;
    ret = af_env_csv_header(buf, sizeof(buf));
    CHECK(ret == (int)strlen("ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n"));
    CHECK(strcmp(buf, "ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n") == 0);

    af_env_rec_t r = { .ts = 1761300000, .t_c100 = 2650, .p_pa = 100218, .flags = 3 };
    ret = af_env_csv(&r, buf, sizeof(buf));
    CHECK(ret == (int)strlen("1761300000,env,1,bmp280,26.50,,751.70,100218,3\n"));
    CHECK(strcmp(buf, "1761300000,env,1,bmp280,26.50,,751.70,100218,3\n") == 0);

    af_env_rec_t r_t = { .ts = 1761300060, .t_c100 = -550, .p_pa = 0, .flags = 1 };
    ret = af_env_csv(&r_t, buf, sizeof(buf));
    CHECK(ret == (int)strlen("1761300060,env,1,bmp280,-5.50,,,,1\n"));
    CHECK(strcmp(buf, "1761300060,env,1,bmp280,-5.50,,,,1\n") == 0);

    af_env_rec_t r_p = { .ts = 1761300090, .t_c100 = 0, .p_pa = 100000, .flags = 2 };
    ret = af_env_csv(&r_p, buf, sizeof(buf));
    CHECK(ret == (int)strlen("1761300090,env,1,bmp280,,,750.06,100000,2\n"));
    CHECK(strcmp(buf, "1761300090,env,1,bmp280,,,750.06,100000,2\n") == 0);

    char small[10];
    CHECK(af_env_csv(&r, small, 10) == -1);
    CHECK(small[0] == 0);
    CHECK(af_env_csv(&r, buf, 0) == -1);
    CHECK(af_env_csv(&r, NULL, 256) == -1);
    char tiny[5];
    CHECK(af_env_csv_header(tiny, 5) == -1);
}

static void t_cfg(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    CHECK(c.enabled == 1);
    CHECK(c.t_off10 == 0);
    CHECK(c.p_off10 == 0);
    CHECK(c.agg_s == AF_ENV_AGG_DEF);
    CHECK(c.period_s == AF_ENV_PERIOD_DEF);
    CHECK(AF_ENV_PERIOD_DEF == 300); /* e16e: оператор 2026-10-27, период опроса датчика среды по умолчанию 5 мин */
    CHECK(af_env_cfg_valid(&c) == 1);

    af_env_cfg_t bad;
    af_env_cfg_default(&bad);
    bad.t_off10 = 51; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.t_off10 = -51; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.t_off10 = 0; bad.p_off10 = 201; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.p_off10 = -201; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.p_off10 = 0; bad.agg_s = 0; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.agg_s = 61; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.agg_s = 1; bad.period_s = 4; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.period_s = 601; CHECK(af_env_cfg_valid(&bad) == 0);
    bad.period_s = 30; bad.enabled = 2; CHECK(af_env_cfg_valid(&bad) == 0);

    af_env_cfg_t edge;
    af_env_cfg_default(&edge);
    edge.t_off10 = 50; edge.p_off10 = 200; edge.agg_s = 1; edge.period_s = 5; edge.enabled = 0;
    CHECK(af_env_cfg_valid(&edge) == 1);
    edge.t_off10 = -50; edge.p_off10 = -200; edge.agg_s = 60; edge.period_s = 600;
    CHECK(af_env_cfg_valid(&edge) == 1);

    CHECK(af_env_cfg_valid(NULL) == 0);

    af_env_cfg_t cl;
    cl.enabled = 5; cl.t_off10 = -300; cl.p_off10 = 999; cl.agg_s = 0; cl.period_s = 9999;
    af_env_cfg_clamp(&cl);
    CHECK(cl.enabled == 1);
    CHECK(cl.t_off10 == -50);
    CHECK(cl.p_off10 == 200);
    CHECK(cl.agg_s == 1);
    CHECK(cl.period_s == 600);

    af_env_cfg_t cl2;
    cl2.enabled = 0; cl2.t_off10 = 7; cl2.p_off10 = -7; cl2.agg_s = 200; cl2.period_s = 1;
    af_env_cfg_clamp(&cl2);
    CHECK(cl2.enabled == 0);
    CHECK(cl2.t_off10 == 7);
    CHECK(cl2.p_off10 == -7);
    CHECK(cl2.agg_s == 60);
    CHECK(cl2.period_s == 5);

    af_env_cfg_clamp(NULL);
}

static void t_make_rec_real(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    af_env_rec_t out;
    float t_shown, p_shown;
    int ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);
    CHECK(out.ts == 1761300000);
    CHECK(out.t_c100 == 2650);
    CHECK(out.p_pa >= 100217 && out.p_pa <= 100219);
    CHECK(out.flags == 3);
    CHECK(fabsf(t_shown - 26.5f) < 0.001f);
    CHECK(fabsf(p_shown - 751.70f) < 0.01f);
}

static void t_make_rec_offsets(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    c.t_off10 = -15;
    c.p_off10 = 25;
    af_env_rec_t out;
    float t_shown, p_shown;
    int ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);
    CHECK(out.t_c100 == 2500);
    CHECK(out.p_pa >= 100549 && out.p_pa <= 100553);
    CHECK(out.flags == 7);

    af_env_cfg_t c2;
    af_env_cfg_default(&c2);
    c2.t_off10 = 500;
    af_env_rec_t out2;
    float t2, p2;
    ret = af_env_make_rec(&c2, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out2, &t2, &p2);
    CHECK(ret == 0);
    CHECK(out2.t_c100 == 3150);
}

static void t_make_rec_caps_disabled(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    af_env_rec_t out;
    float t_shown, p_shown;
    int ret;

    ret = af_env_make_rec(&c, AF_CAP_TEMP, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);
    CHECK(out.flags == AF_ENV_F_T);
    CHECK(out.p_pa == 0);
    CHECK(isnan(p_shown));

    ret = af_env_make_rec(&c, AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);
    CHECK(out.flags == AF_ENV_F_P);
    CHECK(out.t_c100 == 0);

    ret = af_env_make_rec(&c, 0, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);

    c.enabled = 0;
    out.ts = 77;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -1);
    CHECK(out.ts == 77);
    CHECK(isnan(t_shown));
    CHECK(isnan(p_shown));

    c.enabled = 1;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 0, &out, &t_shown, &p_shown);
    CHECK(ret == -3);
    CHECK(fabsf(t_shown - 26.5f) < 0.001f);

    CHECK(af_env_make_rec(NULL, AF_CAP_TEMP, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown) == -4);
    CHECK(af_env_make_rec(&c, AF_CAP_TEMP, 26.5f, 100218.0f, 1761300000, NULL, &t_shown, &p_shown) == -4);
}

static void t_make_rec_implausible(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    af_env_rec_t out;
    float t_shown, p_shown;
    int ret;

    out.ts = 77;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 90.0f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);
    CHECK(out.ts == 77);

    out.ts = 77;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, -41.0f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);
    CHECK(out.ts == 77);

    out.ts = 77;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 20000.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);
    CHECK(out.ts == 77);

    out.ts = 77;
    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 120000.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);
    CHECK(out.ts == 77);

    out.ts = 77;
    ret = af_env_make_rec(&c, 0, 26.5f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == -2);
    CHECK(out.ts == 77);

    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 85.0f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);

    ret = af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, -40.0f, 100218.0f, 1761300000, &out, &t_shown, &p_shown);
    CHECK(ret == 0);
}

static void t_convert_datasheet(void) {
    uint8_t cal24[24];
    put16(&cal24[0], 27504);
    put16(&cal24[2], 26435);
    put16(&cal24[4], -1000);
    put16(&cal24[6], 36477);
    put16(&cal24[8], -10685);
    put16(&cal24[10], 3024);
    put16(&cal24[12], 2855);
    put16(&cal24[14], 140);
    put16(&cal24[16], -7);
    put16(&cal24[18], 15500);
    put16(&cal24[20], -14600);
    put16(&cal24[22], 6000);

    af_bmp280_cal_t cal;
    CHECK(af_bmp280_parse_cal(cal24, 24, &cal) == 0);

    uint8_t raw6[6] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00};
    float t_c, p_pa;
    CHECK(af_env_bmp280_convert(&cal, raw6, &t_c, &p_pa) == 0);
    CHECK(fabsf(t_c - 25.08f) <= 0.01f);
    CHECK(fabsf(p_pa - 100653.0f) <= 2.0f);

    CHECK(af_env_bmp280_convert(NULL, raw6, &t_c, &p_pa) == -1);
    CHECK(af_env_bmp280_convert(&cal, NULL, &t_c, &p_pa) == -1);
    CHECK(af_env_bmp280_convert(&cal, raw6, NULL, &p_pa) == -1);
    CHECK(af_env_bmp280_convert(&cal, raw6, &t_c, NULL) == -1);

    uint8_t raw_t_miss[6] = {0x65, 0x5A, 0xC0, 0x80, 0x00, 0x00};
    CHECK(af_env_bmp280_convert(&cal, raw_t_miss, &t_c, &p_pa) == -2);

    uint8_t raw_p_miss[6] = {0x80, 0x00, 0x00, 0x7E, 0xED, 0x00};
    CHECK(af_env_bmp280_convert(&cal, raw_p_miss, &t_c, &p_pa) == 0);
    CHECK(fabsf(t_c - 25.08f) <= 0.01f);
    CHECK(isnan(p_pa));
}

static void t_ctrl_and_meas_time(void) {
    CHECK(af_bmp280_ctrl(5, 5, 1) == 0xB5);
    CHECK(af_bmp280_ctrl(0, 0, 0) == 0);
    CHECK(af_bmp280_ctrl(1, 1, 1) == 0x25);
    CHECK(af_bmp280_ctrl(2, 5, 3) == ((2 << 5) | (5 << 2) | 3));
    CHECK(af_bmp280_ctrl(AF_ENV_OSRS_T, AF_ENV_OSRS_P, AF_BMP280_MODE_FORCED) == 0xB5);

    CHECK(af_bmp280_meas_ms(5, 5) == 76);
    CHECK(af_bmp280_meas_ms(1, 1) == 7);
    CHECK(af_bmp280_meas_ms(1, 0) == 4);
    CHECK(af_bmp280_meas_ms(0, 0) == 2);
    CHECK(af_bmp280_meas_ms(3, 3) == 21);
    CHECK(af_bmp280_meas_ms(7, 7) == 76);
}

/* долг #SA-3 (выжившие мутанты V08, V24-26, V34, V37): округление, флаг CORR, флаги 7 в записи, inf и окно 0 */
static void t_rounding_flags_inf(void) {
    af_env_cfg_t c;
    af_env_cfg_default(&c);
    af_env_rec_t out, dec;
    float ts, ps;
    CHECK(af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.505f, 100218.6f, 1761300000, &out, &ts, &ps) == 0);
    CHECK(out.t_c100 == 2651);   /* 2650.5 округляется вверх, усечение даст 2650 */
    CHECK(out.p_pa == 100219u);  /* 100218.6 -> 100219 */
    CHECK(af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, -5.505f, 100218.0f, 1761300000, &out, &ts, &ps) == 0);
    CHECK(out.t_c100 == -551);
    c.p_off10 = 25; /* только давление: флаг CORR обязан взвестись */
    CHECK(af_env_make_rec(&c, AF_CAP_TEMP | AF_CAP_PRESS, 26.5f, 100218.0f, 1761300000, &out, &ts, &ps) == 0);
    CHECK(out.flags == (AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_CORR));
    uint8_t b[AF_REC_ENV_SIZE];
    af_env_encode(&out, b);
    CHECK(af_env_decode(b, &dec) == 0);
    CHECK(dec.flags == 7);
    af_liveavg_t a;
    af_liveavg_init(&a);
    CHECK(af_liveavg_add(&a, INFINITY, 0.1f) == 0);
    CHECK(a.n == 0);
    af_liveavg_init(&a);
    CHECK(af_liveavg_tick(&a, 50, 0) == 0);
    CHECK(af_liveavg_add(&a, 5.0f, 0.5f) == 1);
    CHECK(af_liveavg_tick(&a, 50, 0) == 0); /* окно 0 трактуется как 1 с: в ту же секунду выдачи нет */
    CHECK(af_liveavg_tick(&a, 51, 0) == 1);
}

static void t_liveavg_basic(void) {
    af_liveavg_t a;
    af_liveavg_init(&a);
    CHECK(af_liveavg_tick(&a, 100, 5) == 0);
    CHECK(a.has_last_emit == 1);
    CHECK(a.last_emit_s == 100);

    CHECK(af_liveavg_add(&a, 10.0f, 0.10f) == 1);
    CHECK(af_liveavg_add(&a, 20.0f, 0.20f) == 1);
    CHECK(af_liveavg_add(&a, 30.0f, 0.30f) == 1);

    CHECK(af_liveavg_tick(&a, 104, 5) == 0);
    CHECK(af_liveavg_tick(&a, 105, 5) == 1);
    CHECK(fabsf(a.cps_avg - 20.0f) < 1e-4f);
    CHECK(fabsf(a.rate_avg - 0.20f) < 1e-4f);
    CHECK(a.avg_n == 3);
    CHECK(a.valid == 1);
    CHECK(a.n == 0);

    CHECK(af_liveavg_tick(&a, 110, 5) == 0);
    CHECK(a.valid == 1);
    CHECK(a.avg_n == 3);
}

static void t_liveavg_reject_and_window(void) {
    af_liveavg_t a;
    af_liveavg_init(&a);
    CHECK(af_liveavg_add(&a, -1.0f, 0.1f) == 0);
    CHECK(af_liveavg_add(&a, NAN, 0.1f) == 0);
    CHECK(af_liveavg_add(&a, 1.0f, NAN) == 0);
    CHECK(af_liveavg_add(&a, 1.0f, INFINITY) == 0);
    CHECK(af_liveavg_add(&a, 1.0f, -0.01f) == 0);
    CHECK(af_liveavg_add(&a, 1.0f, 1000.0f) == 0);
    CHECK(af_liveavg_add(&a, 1.0f, 999.0f) == 1);
    CHECK(a.n == 1);
    CHECK(af_liveavg_add(NULL, 1.0f, 1.0f) == 0);

    af_liveavg_t a2;
    af_liveavg_init(&a2);
    CHECK(af_liveavg_tick(&a2, 50, 0) == 0);
    CHECK(af_liveavg_add(&a2, 5.0f, 0.5f) == 1);
    CHECK(af_liveavg_tick(&a2, 51, 0) == 1);

    af_liveavg_t a3;
    af_liveavg_init(&a3);
    CHECK(af_liveavg_tick(&a3, 50, 1000) == 0);
    CHECK(af_liveavg_add(&a3, 5.0f, 0.5f) == 1);
    CHECK(af_liveavg_tick(&a3, 109, 1000) == 0);
    CHECK(af_liveavg_tick(&a3, 110, 1000) == 1);

    af_liveavg_t a4;
    af_liveavg_init(&a4);
    CHECK(af_liveavg_tick(&a4, 200, 5) == 0);
    CHECK(af_liveavg_add(&a4, 5.0f, 0.5f) == 1);
    CHECK(af_liveavg_tick(&a4, 100, 5) == 0);
    CHECK(a4.n == 0);
    CHECK(a4.last_emit_s == 100);

    CHECK(af_liveavg_tick(NULL, 1, 1) == 0);
    af_liveavg_init(NULL);
}

int main(void) {
    RUN(t_encode_decode_roundtrip);
    RUN(t_decode_errors);
    RUN(t_csv_row);
    RUN(t_cfg);
    RUN(t_make_rec_real);
    RUN(t_make_rec_offsets);
    RUN(t_make_rec_caps_disabled);
    RUN(t_make_rec_implausible);
    RUN(t_convert_datasheet);
    RUN(t_ctrl_and_meas_time);
    RUN(t_rounding_flags_inf);
    RUN(t_liveavg_basic);
    RUN(t_liveavg_reject_and_window);
    TESTS_DONE();
    return 0;
}
