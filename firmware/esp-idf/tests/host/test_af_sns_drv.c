#include "af_test.h"
#include "af_sns_drv.h"
#include "sns_vectors.h"
#include <math.h>
#include <string.h>

static uint8_t crc8_table[256];
static int crc8_table_init = 0;

static void init_crc8_table(void) {
    if (crc8_table_init) return;
    for (int i = 0; i < 256; i++) {
        uint8_t crc = (uint8_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
        crc8_table[i] = crc;
    }
    crc8_table_init = 1;
}

static uint8_t ref_crc8(const uint8_t *d, size_t n) {
    init_crc8_table();
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc = crc8_table[crc ^ d[i]];
    }
    return crc;
}

static void t_crc8(void) {
    uint8_t data[2] = {0xBE, 0xEF};
    CHECK(af_crc8(data, 2) == SV_CRC8_BEEF);
    CHECK(af_crc8(NULL, 0) == 0xFF);
    uint8_t empty[1] = {0x00};
    CHECK(af_crc8(empty, 0) == 0xFF);

    for (int i = 0; i < 256; i++) {
        uint8_t d[2] = {(uint8_t)i, (uint8_t)(255 - i)};
        CHECK(af_crc8(d, 2) == ref_crc8(d, 2));
    }
}

static void t_sht3x_vectors(void) {
    for (int i = 0; i < 6; i++) {
        float t_c = 0.0f, rh = 0.0f;
        int ret = af_sht3x_parse(SV_SHT[i].raw, &t_c, &rh);
        CHECK(ret == 0);
        CHECK(fabsf(t_c - (float)SV_SHT[i].t_c) < 1e-3f);
        CHECK(fabsf(rh - (float)SV_SHT[i].rh) < 1e-3f);
    }

    float t_c = 0.0f, rh = 0.0f;
    uint8_t raw[6];
    memcpy(raw, SV_SHT[0].raw, 6);
    
    t_c = -999.0f; rh = -999.0f;
    raw[2] ^= 0xFF;
    CHECK(af_sht3x_parse(raw, &t_c, &rh) == -2);
    CHECK(t_c == -999.0f);
    CHECK(rh == -999.0f);

    memcpy(raw, SV_SHT[0].raw, 6);
    t_c = -999.0f; rh = -999.0f;
    raw[5] ^= 0xFF;
    CHECK(af_sht3x_parse(raw, &t_c, &rh) == -3);
    CHECK(t_c == -999.0f);
    CHECK(rh == -999.0f);

    memcpy(raw, SV_SHT[0].raw, 6);
    t_c = -999.0f; rh = -999.0f;
    raw[0] ^= 0xFF;
    CHECK(af_sht3x_parse(raw, &t_c, &rh) == -2);
    CHECK(t_c == -999.0f);
    CHECK(rh == -999.0f);

    CHECK(af_sht3x_parse(SV_SHT[0].raw, NULL, &rh) == -1);
    CHECK(af_sht3x_parse(SV_SHT[0].raw, &t_c, NULL) == -1);
    CHECK(af_sht3x_parse(NULL, &t_c, &rh) == -1);

    uint8_t st_ok[3] = {0xBE, 0xEF, 0x92};
    CHECK(af_sht3x_status_ok(st_ok) == 1);
    uint8_t st_bad[3] = {0xBE, 0xEF, 0x93};
    CHECK(af_sht3x_status_ok(st_bad) == 0);
    CHECK(af_sht3x_status_ok(NULL) == 0);
}

static void t_aht2x_vectors(void) {
    for (int i = 0; i < 5; i++) {
        float t_c = 0.0f, rh = 0.0f;
        int ret = af_aht2x_parse(SV_AHT[i].raw, &t_c, &rh);
        CHECK(ret == 0);
        CHECK(fabsf(t_c - (float)SV_AHT[i].t_c) < 1e-3f);
        CHECK(fabsf(rh - (float)SV_AHT[i].rh) < 1e-3f);
    }

    uint8_t raw[7];
    float t_c = 0.0f, rh = 0.0f;
    memcpy(raw, SV_AHT[0].raw, 7);
    
    raw[0] |= 0x80;
    raw[6] = ref_crc8(raw, 6);
    CHECK(af_aht2x_parse(raw, &t_c, &rh) == -2);

    memcpy(raw, SV_AHT[0].raw, 7);
    raw[0] &= ~0x08;
    raw[6] = ref_crc8(raw, 6);
    CHECK(af_aht2x_parse(raw, &t_c, &rh) == -3);

    memcpy(raw, SV_AHT[0].raw, 7);
    raw[0] = 0x80;
    raw[6] = ref_crc8(raw, 6);
    CHECK(af_aht2x_parse(raw, &t_c, &rh) == -2);

    memcpy(raw, SV_AHT[0].raw, 7);
    raw[6] ^= 0x01;
    CHECK(af_aht2x_parse(raw, &t_c, &rh) == -4);

    CHECK(af_aht2x_parse(NULL, &t_c, &rh) == -1);
    CHECK(af_aht2x_parse(SV_AHT[0].raw, NULL, &rh) == -1);
    CHECK(af_aht2x_parse(SV_AHT[0].raw, &t_c, NULL) == -1);
}

static void t_bme280_cal_parse(void) {
    af_bme280_cal_t c;
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, SV_BME_CAL_B, &c) == 0);
    
    CHECK(c.tp.t1 == SV_BME_T1);
    CHECK(c.tp.t2 == SV_BME_T2);
    CHECK(c.tp.t3 == SV_BME_T3);
    CHECK(c.tp.p1 == SV_BME_P1);
    CHECK(c.tp.p2 == SV_BME_P2);
    CHECK(c.tp.p3 == SV_BME_P3);
    CHECK(c.tp.p4 == SV_BME_P4);
    CHECK(c.tp.p5 == SV_BME_P5);
    CHECK(c.tp.p6 == SV_BME_P6);
    CHECK(c.tp.p7 == SV_BME_P7);
    CHECK(c.tp.p8 == SV_BME_P8);
    CHECK(c.tp.p9 == SV_BME_P9);
    CHECK(c.h1 == SV_BME_H1);
    CHECK(c.h2 == SV_BME_H2);
    CHECK(c.h3 == SV_BME_H3);
    CHECK(c.h4 == SV_BME_H4);
    CHECK(c.h5 == SV_BME_H5);
    CHECK(c.h6 == SV_BME_H6);

    uint8_t a[26];
    memcpy(a, SV_BME_CAL_A, 26);
    uint8_t b[7];
    int16_t h2 = -300;
    uint8_t h3 = 7;
    int16_t h4 = -300;
    int16_t h5 = -200;
    int8_t h6 = -25;

    b[0] = (uint8_t)(h2 & 0xFF);
    b[1] = (uint8_t)((h2 >> 8) & 0xFF);
    b[2] = h3;
    b[3] = (uint8_t)((h4 >> 4) & 0xFF);
    b[4] = (uint8_t)((h4 & 0x0F) | ((h5 & 0x0F) << 4));
    b[5] = (uint8_t)((h5 >> 4) & 0xFF);
    b[6] = (uint8_t)(h6 & 0xFF);

    af_bme280_cal_t c2;
    CHECK(af_bme280_parse_cal(a, b, &c2) == 0);
    CHECK(c2.h2 == h2);
    CHECK(c2.h3 == h3);
    CHECK(c2.h4 == h4);
    CHECK(c2.h5 == h5);
    CHECK(c2.h6 == h6);

    CHECK(af_bme280_parse_cal(NULL, SV_BME_CAL_B, &c) == -1);
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, NULL, &c) == -1);
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, SV_BME_CAL_B, NULL) == -1);

    uint8_t a_bad[26];
    memcpy(a_bad, SV_BME_CAL_A, 26);
    a_bad[0] = 0;
    a_bad[1] = 0;
    CHECK(af_bme280_parse_cal(a_bad, SV_BME_CAL_B, &c) != 0);
}

static void t_bme280_vectors(void) {
    af_bme280_cal_t cal;
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, SV_BME_CAL_B, &cal) == 0);

    double max_dt = 0.0, max_dp = 0.0, max_drh = 0.0;
    for (int i = 0; i < 12; i++) {
        float t_c, p_pa, rh;
        int ret = af_bme280_convert(&cal, SV_BME[i].raw, &t_c, &p_pa, &rh);
        CHECK(ret == 0);
        
        double dt = fabs((double)t_c - SV_BME[i].t_c);
        double dp = fabs((double)p_pa - SV_BME[i].p_pa);
        double drh = fabs((double)rh - SV_BME[i].rh);
        
        if (dt > max_dt) max_dt = dt;
        if (dp > max_dp) max_dp = dp;
        if (drh > max_drh) max_drh = drh;

        CHECK(dt < 0.01);
        CHECK(dp < 0.5);
        CHECK(drh < 0.1);
    }
    printf("INFO bme280 max_dev t=%.4f p=%.4f rh=%.4f\n", max_dt, max_dp, max_drh);

    float t_c, p_pa, rh;
    CHECK(af_bme280_convert(&cal, SV_BME[0].raw, &t_c, &p_pa, &rh) == 0);
    CHECK(fabsf(t_c - 25.08f) < 0.01f);
    CHECK(fabsf(p_pa - 100653.0f) < 1.0f);
}

static void t_bme280_skips(void) {
    af_bme280_cal_t cal;
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, SV_BME_CAL_B, &cal) == 0);

    uint8_t raw[8];
    memcpy(raw, SV_BME[0].raw, 8);
    raw[6] = 0x80;
    raw[7] = 0x00;
    float t_c, p_pa, rh;
    int ret = af_bme280_convert(&cal, raw, &t_c, &p_pa, &rh);
    CHECK(ret == 0);
    CHECK(isnan(rh));

    memcpy(raw, SV_BME[0].raw, 8);
    raw[3] = 0x80;
    raw[4] = 0x00;
    raw[5] = 0x00;
    t_c = -999.0f; p_pa = -999.0f; rh = -999.0f;
    ret = af_bme280_convert(&cal, raw, &t_c, &p_pa, &rh);
    CHECK(ret == -2);
    CHECK(t_c == -999.0f);
    CHECK(p_pa == -999.0f);
    CHECK(rh == -999.0f);

    CHECK(af_bme280_convert(NULL, SV_BME[0].raw, &t_c, &p_pa, &rh) == -1);
    CHECK(af_bme280_convert(&cal, NULL, &t_c, &p_pa, &rh) == -1);
}

static void t_bme280_hum_range(void) {
    af_bme280_cal_t cal;
    CHECK(af_bme280_parse_cal(SV_BME_CAL_A, SV_BME_CAL_B, &cal) == 0);

    int32_t t_fine = 128000;
    float prev_val = -1.0f;
    int first = 1;
    for (int adc_h = 0; adc_h <= 65535; adc_h += 997) {
        float h = af_bme280_hum(&cal, adc_h, t_fine);
        if (adc_h == 0x8000) {
            CHECK(isnan(h));
        } else {
            CHECK(h >= 0.0f && h <= 100.0f);
            if (!first) {
                CHECK(h >= prev_val);
            }
            prev_val = h;
            first = 0;
        }
    }
}

int main(void) {
    RUN(t_crc8);
    RUN(t_sht3x_vectors);
    RUN(t_aht2x_vectors);
    RUN(t_bme280_cal_parse);
    RUN(t_bme280_vectors);
    RUN(t_bme280_skips);
    RUN(t_bme280_hum_range);
    TESTS_DONE();
}
